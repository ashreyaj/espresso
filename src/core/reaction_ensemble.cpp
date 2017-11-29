/** reaction ensemble method according to smith94x for the reaction ensemble at constant volume and temperature, for the reaction ensemble at constant pressure additionally employ a barostat!
*NOTE: a chemical reaction consists of a forward and backward reaction. Here both reactions have to be defined seperately.
*The extent of the reaction is here chosen to be +1.
*If the reaction trial move for a dissociation of HA is accepted then there is one more dissociated ion pair H+ and A-
*/

/** @file */ 

#include "reaction_ensemble.hpp"
#include "random.hpp" //for random numbers
#include "energy.hpp"	//for calculate_current_potential_energy_of_system
#include "integrate.hpp" //for time_step
#include <fstream> //for std::ifstream, std::ofstream for input output into files
#include "utils/Histogram.hpp"
#include "partCfg_global.hpp"
#include "statistics.hpp" //for distto
#include <stdio.h> //for getline()
#include "utils.hpp" // for PI and random vectors
#include "global.hpp" //for access to global variables
#include "particle_data.hpp" //for particle creation, modification

namespace ReactionEnsemble{

ReactionEnsemble::ReactionEnsemble(){}

ReactionEnsemble::~ReactionEnsemble(){
}

/**
* Performs a randomly selected reaction in the reaction ensemble
*/
int ReactionEnsemble::do_reaction(int reaction_steps){
    for (int i = 0; i< reaction_steps; i++){
        int reaction_id=i_random(m_current_reaction_system.nr_single_reactions);
    	generic_oneway_reaction(reaction_id, reaction_ensemble_mode);
    }
	return 0;
}

/**
* Adds a reaction to the reaction system
*/
void ReactionEnsemble::add_reaction(double equilibrium_constant, std::vector<int> _reactant_types, std::vector<int> _reactant_coefficients, std::vector<int> _product_types, std::vector<int> _product_coefficients){
            single_reaction new_reaction;
            int len_reactant_types= (int) _reactant_types.size();
            int len_product_types= (int) _product_types.size();
            new_reaction.len_reactant_types=len_reactant_types;
            new_reaction.len_product_types=len_product_types;
            
            new_reaction.equilibrium_constant=equilibrium_constant;
            new_reaction.reactant_types=_reactant_types;
            new_reaction.len_reactant_types=len_reactant_types;
            new_reaction.reactant_coefficients=_reactant_coefficients;
            
            new_reaction.product_types=_product_types;
            new_reaction.len_product_types=len_product_types;
            new_reaction.product_coefficients=_product_coefficients;
            
            new_reaction.nu_bar=calculate_nu_bar(new_reaction.reactant_coefficients,  new_reaction.product_coefficients);
            
            //if everything is fine:
            m_current_reaction_system.reactions.push_back(new_reaction);
            m_current_reaction_system.nr_single_reactions+=1;
            
            //assign different types an index in a growing list that starts at and is incremented by 1 for each new type
            int status=update_type_index(new_reaction.reactant_types, new_reaction.product_types);
            if(status==ES_ERROR)
                 throw std::runtime_error("could not initialize gc particle list for types\n");

}

/**
* Checks whether all necessary variables for the reaction ensemble have been set.
*/
void ReactionEnsemble::check_reaction_ensemble(){
	/**checks the reaction_ensemble struct for valid parameters */
	if(m_current_reaction_system.nr_single_reactions==0){
	    throw std::runtime_error("Reaction system not initialized");
	}
	if(m_current_reaction_system.standard_pressure_in_simulation_units<0 and not (std::abs(m_constant_pH-(-10)) > std::numeric_limits<double>::epsilon() ) ){
	    throw std::runtime_error("Please initialize your reaction ensemble standard pressure before calling initialize.\n");
	}
	if(m_current_reaction_system.temperature<0){
	    throw std::runtime_error("Temperatures cannot be negative. Please provide a temperature (in k_B T) to the simulation. Normally it should be 1.0. This will be used directly to calculate beta:=1/(k_B T) which occurs in the exp(-beta*E)\n");
	}
	#ifdef ELECTROSTATICS
	//check for the existence of default charges for all types that take part in the reactions
	for (int i=0; i<m_current_reaction_system.nr_different_types; i++) {
		if(m_current_reaction_system.charges_of_types[m_current_reaction_system.type_index[i]]==m_invalid_charge) {
		    std::string message = std::string("Forgot to assign charge to type") +std::to_string(m_current_reaction_system.charges_of_types[m_current_reaction_system.type_index[i]]);
		    throw std::runtime_error(message);
		}
	}
	#endif
}

//boring helper functions
/**
* Automatically sets the volume which is used by the reaction ensemble to the volume of a cuboid box
*/
void ReactionEnsemble::set_cuboid_reaction_ensemble_volume(){
	if (m_current_reaction_system.volume <0)
		m_current_reaction_system.volume=box_l[0]*box_l[1]*box_l[2];
}

/**
* Calculates the factorial expression which occurs in the reaction ensemble acceptance probability
*/
double ReactionEnsemble::factorial_Ni0_divided_by_factorial_Ni0_plus_nu_i(int Ni0, int nu_i) {
	double value;
	if (nu_i == 0) {
		value=1.0;
	} else {
		value=1.0;
		if (nu_i > 0) {
			for(int i=1;i <= nu_i;i++) {
				value=value*1.0/(Ni0+i);
			}
		} else {
			int abs_nu_i = (int) (-1.0 * nu_i);
			for(int i=0;i < abs_nu_i;i++) {
				value= value*(Ni0-i);
			}
		}
	}
	return value;
}

/**
* Checks wether all particles exist for the provided reaction.
*/
bool ReactionEnsemble::all_reactant_particles_exist(int reaction_id) {
	bool enough_particles=true;
	for(int i=0;i<m_current_reaction_system.reactions[reaction_id].len_reactant_types;i++){
		int current_number;
		number_of_particles_with_type(m_current_reaction_system.reactions[reaction_id].reactant_types[i], &current_number);
		if(current_number<m_current_reaction_system.reactions[reaction_id].reactant_coefficients[i]){
			enough_particles=false;
			break;
		}
	}
	return enough_particles;
}

/**
* Stores the particle property of a random particle of the provided type into the provided vector
*/
void ReactionEnsemble::append_particle_property_of_random_particle(int type, std::vector<stored_particle_property>& list_of_particles){
	int p_id ;
	find_particle_type(type, &p_id);
	stored_particle_property property_of_part={p_id,\
						   m_current_reaction_system.charges_of_types[find_index_of_type(type)],\
						   type
						  };
	list_of_particles.push_back(property_of_part);
}

/**
*Performs a trial reaction move
*/
void ReactionEnsemble::make_reaction_attempt(single_reaction& current_reaction, std::vector<stored_particle_property>& changed_particles_properties, std::vector<int>& p_ids_created_particles, std::vector<stored_particle_property>& hidden_particles_properties){
	//create or hide particles of types with corresponding types in reaction
	for(int i=0;i<std::min(current_reaction.len_product_types,current_reaction.len_reactant_types);i++){
		//change std::min(reactant_coefficients(i),product_coefficients(i)) many particles of reactant_types(i) to product_types(i)
		for(int j=0;j<std::min(current_reaction.product_coefficients[i],current_reaction.reactant_coefficients[i]);j++){
			append_particle_property_of_random_particle(current_reaction.reactant_types[i], changed_particles_properties);
			replace_particle(changed_particles_properties.back().p_id,current_reaction.product_types[i]);
		}
		//create product_coefficients(i)-reactant_coefficients(i) many product particles iff product_coefficients(i)-reactant_coefficients(i)>0,
		//iff product_coefficients(i)-reactant_coefficients(i)<0, hide this number of reactant particles
		if ( current_reaction.product_coefficients[i]-current_reaction.reactant_coefficients[i] >0) {
			for(int j=0; j< current_reaction.product_coefficients[i]-current_reaction.reactant_coefficients[i] ;j++) {
				int p_id=create_particle(current_reaction.product_types[i]);
				p_ids_created_particles.push_back(p_id);
			}
		} else if (current_reaction.reactant_coefficients[i]-current_reaction.product_coefficients[i] >0) {
			for(int j=0;j<current_reaction.reactant_coefficients[i]-current_reaction.product_coefficients[i];j++) {
				append_particle_property_of_random_particle(current_reaction.reactant_types[i], hidden_particles_properties);			
				hide_particle(hidden_particles_properties.back().p_id,current_reaction.reactant_types[i]);
			}
		}
	}
	//create or hide particles of types with noncorresponding replacement types
	for(int i=std::min(current_reaction.len_product_types,current_reaction.len_reactant_types);i< std::max(current_reaction.len_product_types,current_reaction.len_reactant_types);i++ ) {
		if(current_reaction.len_product_types<current_reaction.len_reactant_types){
			//hide superfluous reactant_types particles
			for(int j=0;j<current_reaction.reactant_coefficients[i];j++){
				append_particle_property_of_random_particle(current_reaction.reactant_types[i], hidden_particles_properties);
				hide_particle(hidden_particles_properties.back().p_id,current_reaction.reactant_types[i]);
			}
		} else {
			//create additional product_types particles
			for(int j=0;j<current_reaction.product_coefficients[i];j++){
				int p_id= create_particle(current_reaction.product_types[i]);
				p_ids_created_particles.push_back(p_id);
			}
		}
	}	
	
}

/**
* Calculates the whole product of factorial expressions which occur in the reaction ensemble acceptance probability
*/
double ReactionEnsemble::calculate_factorial_expression(single_reaction& current_reaction, int* old_particle_numbers){
	double factorial_expr=1.0;	
	//factorial contribution of reactants
	for(int i=0;i<current_reaction.len_reactant_types;i++) {
		int nu_i=-1*current_reaction.reactant_coefficients[i];
		int N_i0= old_particle_numbers[find_index_of_type(current_reaction.reactant_types[i])];
		factorial_expr=factorial_expr*factorial_Ni0_divided_by_factorial_Ni0_plus_nu_i(N_i0,nu_i); //zeta = 1 (see smith paper) since we only perform one reaction at one call of the function
	}
	//factorial contribution of products
	for(int i=0;i<current_reaction.len_product_types;i++) {
		int nu_i=current_reaction.product_coefficients[i];
		int N_i0= old_particle_numbers[find_index_of_type(current_reaction.product_types[i])];
		factorial_expr=factorial_expr*factorial_Ni0_divided_by_factorial_Ni0_plus_nu_i(N_i0,nu_i); //zeta = 1 (see smith paper) since we only perform one reaction at one call of the function
	}
	return factorial_expr;
}

/**
* Restores the previosly stored particle properties. This funtion is invoked when a reaction attempt is rejected.
*/
void ReactionEnsemble::restore_properties(std::vector<stored_particle_property>& property_list ,const int number_of_saved_properties){
	//this function restores all properties of all particles provided in the property list, the format of the property list is (p_id,charge,type) repeated for each particle that occurs in that list
	for(int i=0;i<property_list.size();i++) {
		double charge= property_list[i].charge;
		int type= property_list[i].type;
		#ifdef ELECTROSTATICS
		//set charge
		set_particle_q(property_list[i].p_id, charge);
		#endif
		//set type
		set_particle_type(property_list[i].p_id, type);
	}
}

/**
* Calculates the expression in the acceptance probability in the reaction ensemble
*/
double ReactionEnsemble::calculate_boltzmann_factor_reaction_ensemble(single_reaction& current_reaction, double E_pot_old, double E_pot_new, std::vector<int>& old_particle_numbers){
	/**calculate the acceptance probability in the reaction ensemble */
	const double volume = m_current_reaction_system.volume;
	const double factorial_expr=calculate_factorial_expression(current_reaction, old_particle_numbers.data());

	const double beta =1.0/m_current_reaction_system.temperature;
	const double standard_pressure_in_simulation_units=m_current_reaction_system.standard_pressure_in_simulation_units;
	//calculate boltzmann factor
	return std::pow(volume*beta*standard_pressure_in_simulation_units, current_reaction.nu_bar) * current_reaction.equilibrium_constant * factorial_expr * exp(-beta * (E_pot_new - E_pot_old));
}

/**
*generic one way reaction
*A+B+...+G +... --> K+...X + Z +...
*you need to use 2A --> B instead of A+A --> B since in the last case you assume distinctness of the particles, however both ways to describe the reaction are equivalent in the thermodynamic limit
*further it is crucial for the function in which order you provide the reactant and product types since particles will be replaced correspondingly! If there are less reactants than products, new product particles are created randomly in the box. Matching particles simply change the types. If there are more reactants than products, old reactant particles are deleted.
 */
bool ReactionEnsemble::generic_oneway_reaction(int reaction_id, int reaction_modus){
	single_reaction& current_reaction=m_current_reaction_system.reactions[reaction_id];
	//Wang-Landau begin
	int old_state_index = -1;
	if(reaction_modus==reaction_ensemble_wang_landau_mode){
		old_state_index=get_flattened_index_wang_landau_of_current_state();
		if(old_state_index>=0){
			if(m_current_wang_landau_system.histogram[old_state_index]>=0)
				m_current_wang_landau_system.monte_carlo_trial_moves+=1;
		}		
	}
	//Wang-Landau end
	if (!all_reactant_particles_exist(reaction_id)) {
		//makes sure, no incomplete reaction is performed -> only need to consider rollback of complete reactions
		
		//Wang-Landau begin
		//increase the wang landau potential and histogram at the current nbar (this case covers the cases nbar=0 or nbar=1)
		if(reaction_modus==reaction_ensemble_wang_landau_mode)
			update_wang_landau_potential_and_histogram(old_state_index);
		//Wang-Landau end
		
		return false;
	}
	
	//calculate potential energy
	const double E_pot_old=calculate_current_potential_energy_of_system(); //only consider potential energy since we assume that the kinetic part drops out in the process of calculating ensemble averages (kinetic part may be seperated and crossed out)
	
	//find reacting molecules in reactants and save their properties for later recreation if step is not accepted
	//do reaction
	//save old particle_numbers
	std::vector<int> old_particle_numbers((unsigned long) m_current_reaction_system.nr_different_types);
	if(reaction_modus!=constant_pH_mode){
		for(int type_index=0;type_index<m_current_reaction_system.nr_different_types;type_index++)
			number_of_particles_with_type(m_current_reaction_system.type_index[type_index], &(old_particle_numbers[type_index])); // here could be optimized by not going over all types but only the types that occur in the reaction
	}

	std::vector<int> p_ids_created_particles;
	std::vector<stored_particle_property> hidden_particles_properties;
	std::vector<stored_particle_property> changed_particles_properties;
	const int number_of_saved_properties=3; //save p_id, charge and type of the reactant particle, only thing we need to hide the particle and recover it
	make_reaction_attempt(current_reaction, changed_particles_properties, p_ids_created_particles, hidden_particles_properties);
	
	const double E_pot_new=calculate_current_potential_energy_of_system();


	//Wang-Landau begin
	//save new_state_index
	int new_state_index = -1;
	if(reaction_modus==reaction_ensemble_wang_landau_mode)
		new_state_index=get_flattened_index_wang_landau_of_current_state();
	double bf;
	if(reaction_modus==reaction_ensemble_mode){
		bf=calculate_boltzmann_factor_reaction_ensemble(current_reaction, E_pot_old, E_pot_new, old_particle_numbers);
	}else if (reaction_modus==reaction_ensemble_wang_landau_mode)
		bf=calculate_boltzmann_factor_reaction_ensemble_wang_landau(current_reaction, E_pot_old, E_pot_new, old_particle_numbers, old_state_index, new_state_index, false);
	else if (reaction_modus==constant_pH_mode)
		bf=calculate_boltzmann_factor_consant_pH(current_reaction, E_pot_old, E_pot_new);
	else
		throw std::runtime_error("Reaction mode is unknown");
	bool reaction_is_accepted=false;
	//Wang-Landau begin
	int accepted_state = -1;
	//Wang-Landau end
	if ( d_random() < bf ) {
		//accept
		if(reaction_modus==reaction_ensemble_wang_landau_mode)
			accepted_state=new_state_index;
		
		//delete hidden reactant_particles (remark: dont delete changed particles)
		//extract ids of to be deleted particles
		//XXX obsolete? and sort them. needed since delete_particle changes particle p_ids. start deletion from the largest p_id onwards
		int len_hidden_particles_properties= (int) hidden_particles_properties.size();
		std::vector<int> to_be_deleted_hidden_ids(len_hidden_particles_properties);
		std::vector<int> to_be_deleted_hidden_types(len_hidden_particles_properties);
		for(int i=0;i<len_hidden_particles_properties;i++) {
		    int p_id=(int) hidden_particles_properties[i].p_id;
			to_be_deleted_hidden_ids[i]=p_id;
			to_be_deleted_hidden_types[i]=hidden_particles_properties[i].type;
//			set_particle_type(p_id, hidden_particles_properties[i].type); //change back type otherwise the bookkeeping algorithm is not working
		}
		std::sort(to_be_deleted_hidden_ids.begin(),to_be_deleted_hidden_ids.end(),std::greater<int>());
		
		for(int i=0;i<len_hidden_particles_properties;i++){
			delete_particle(to_be_deleted_hidden_ids[i]); //delete particle
		}
		reaction_is_accepted= true;
	} else {
		//reject
		if(reaction_modus==reaction_ensemble_wang_landau_mode)
			accepted_state=old_state_index;
		//reverse reaction
		//1) delete created product particles
		std::sort(p_ids_created_particles.begin(),p_ids_created_particles.end(),std::greater<int>());// needed since delete_particle changes particle p_ids. start deletion from the largest p_id onwards //TODO obsolete? //XXX
		for(int i=0;i<p_ids_created_particles.size();i++){
			delete_particle(p_ids_created_particles[i]);
		}
		//2)restore previously hidden reactant particles
		restore_properties(hidden_particles_properties, number_of_saved_properties);
		//2)restore previously changed reactant particles
		restore_properties(changed_particles_properties, number_of_saved_properties);
		reaction_is_accepted= false;
	}
	if(reaction_modus==reaction_ensemble_wang_landau_mode)
		update_wang_landau_potential_and_histogram(accepted_state);

	return reaction_is_accepted;
}

/**
* Calculates the change in particle numbers for the given reaction
*/
int ReactionEnsemble::calculate_nu_bar(std::vector<int>& reactant_coefficients, std::vector<int>& product_coefficients){
	//should only be used at when defining a new reaction
	int len_reactant_types=reactant_coefficients.size();
	int len_product_types=product_coefficients.size();
	int nu_bar =0;
	for(int i=0;i<len_reactant_types;i++){
		nu_bar-=reactant_coefficients[i];
	}
	for(int i=0;i<len_product_types;i++){
		nu_bar+=product_coefficients[i];
	}
	return nu_bar;
}

/**
* Adds types to an index. the index is later used inside of the Reaction ensemble algorithm to determine e.g. the charge which is associated to a type.
*/
void ReactionEnsemble::add_types_to_index(std::vector<int>& type_list){
    int len_type_list=type_list.size();
	for (int i =0; i<len_type_list;i++){
		bool type_i_is_known=is_in_list(type_list[i],m_current_reaction_system.type_index);
		if (!type_i_is_known){
			m_current_reaction_system.type_index.push_back(type_list[i]);
			m_current_reaction_system.nr_different_types+=1;
			init_type_map(type_list[i]); //make types known in espresso
		}
	}
	init_type_map(m_current_reaction_system.non_interacting_type);
}

/**
* Adds types to an index and copes with the case that no index was present before. the index is later used inside of the Reaction ensemble algorithm to determine e.g. the charge which is associated to a type.
*/
int ReactionEnsemble::update_type_index(std::vector<int>& reactant_types, std::vector<int>& product_types){
	//should only be used when defining a new reaction
	int len_reactant_types=reactant_types.size();
	int len_product_types=product_types.size();
	int status_gc_init=0;
	if(m_current_reaction_system.type_index.empty()){
		if(len_reactant_types>0)
			m_current_reaction_system.type_index.push_back(reactant_types[0]);
		else
			m_current_reaction_system.type_index.push_back(product_types[0]);
		m_current_reaction_system.nr_different_types=1;
		init_type_map(m_current_reaction_system.type_index[0]); //make types known in espresso
	}
	add_types_to_index(reactant_types,status_gc_init);
	add_types_to_index(product_types,status_gc_init);
	
	//increase m_current_reaction_system.charges_of_types length
	m_current_reaction_system.charges_of_types.push_back(m_invalid_charge);
	return status_gc_init;
}

/**
* Finds the index of a given type.
*/
int ReactionEnsemble::find_index_of_type(int type){
	int index =-100; //initialize to invalid index
	for(int i=0; i<m_current_reaction_system.nr_different_types;i++){
		if(type==m_current_reaction_system.type_index[i]){
			index=i;
			break;
		}
	}
	if(index<0){
		throw std::runtime_error("Invalid Index");
		
	}
	return index;
}

/**
* Replaces a particle with the given particle id to be of a certain type. This especially means that the particle type and the particle charge are changed.
*/
int ReactionEnsemble::replace_particle(int p_id, int desired_type){
	int err_code_type=set_particle_type(p_id, desired_type);
	int err_code_q = 0.0;
	#ifdef ELECTROSTATICS
	err_code_q=set_particle_q(p_id, m_current_reaction_system.charges_of_types[find_index_of_type(desired_type)]);
	#endif
	return (err_code_q bitor err_code_type);
}

/**
* Hides a particle from short ranged interactions and from the electrostatic interaction. Additional hiding from interactions would need to be implemented here.
*/
int ReactionEnsemble::hide_particle(int p_id, int previous_type){
	/**
    *remove_charge and put type to a non existing one --> no interactions anymore it is as if the particle was non existing (currently only type-based interactions are swithced off, as well as the electrostatic interaction)  
    *hide_particle() does not break bonds for simple reactions. as long as there are no reactions like 2A -->B where one of the reacting A particles occurs in the polymer (think of bond breakages if the monomer in the polymer gets deleted in the reaction). This constraint is not of fundamental reason, but there would be a need for a rule for such "collision" reactions (a reaction like the one above).
    */
	#ifdef ELECTROSTATICS
	//set charge
	set_particle_q(p_id, 0.0);
	#endif
	//set type
	int err_code_type=set_particle_type(p_id, m_current_reaction_system.non_interacting_type);
	return err_code_type;
}

/**
* Deletes the particle with the given p_id. This method is intended to only delete unbonded particles since bonds are coupled to ids. The methods is aware of holes in the particle id range and fills them if a particle gets created.
*/

int ReactionEnsemble::delete_particle(int p_id) {
	/**deletes the particle with the provided id and stores if it created a hole at that position in the particle id range */
	if (p_id == max_seen_particle) {
		// last particle, just delete
		remove_particle(p_id);
		// remove all saved empty p_ids which are greater than the max_seen_particle this is needed in order to avoid the creation of holes
        for (auto p_id_iter = m_empty_p_ids_smaller_than_max_seen_particle.begin(); p_id_iter != m_empty_p_ids_smaller_than_max_seen_particle.end(); ){
            if( (*p_id_iter) >=max_seen_particle)
                p_id_iter=m_empty_p_ids_smaller_than_max_seen_particle.erase(p_id_iter); //update iterator after container was modified
            else
                ++p_id_iter;
        }
	} else if (p_id <= max_seen_particle){
        remove_particle(p_id);
        m_empty_p_ids_smaller_than_max_seen_particle.push_back(p_id);
	} else {
	    throw std::runtime_error("Particle id is greater than the max seen particle id");
	}
	return 0;
}

/**
* Writes a random position inside the central box into the provided array.
*/
void ReactionEnsemble::get_random_position_in_box (double* out_pos) {
	if(m_current_reaction_system.box_is_cylindric_around_z_axis) {
		//see http://mathworld.wolfram.com/DiskPointPicking.html
		double random_radius=m_current_reaction_system.cyl_radius*std::sqrt(d_random()); //for uniform disk point picking in cylinder
		double phi=2.0*PI*d_random();
		out_pos[0]=random_radius*cos(phi);
		out_pos[1]=random_radius*sin(phi);
		while (std::pow(out_pos[0],2)+std::pow(out_pos[1],2)<=std::pow(m_current_reaction_system.exclusion_radius,2)){
			random_radius=m_current_reaction_system.cyl_radius*std::sqrt(d_random());
			out_pos[0]=random_radius*cos(phi);
			out_pos[1]=random_radius*sin(phi);		
		}
		out_pos[0]+=m_current_reaction_system.cyl_x;
		out_pos[1]+=m_current_reaction_system.cyl_y;
		out_pos[2]=box_l[2]*d_random();
	} else if (m_current_reaction_system.box_has_wall_constraints) {
		out_pos[0]=box_l[0]*d_random();
		out_pos[1]=box_l[1]*d_random();	
		out_pos[2]=m_current_reaction_system.slab_start_z+(m_current_reaction_system.slab_end_z-m_current_reaction_system.slab_start_z)*d_random();
	}else{
		//cubic case
		out_pos[0]=box_l[0]*d_random();
		out_pos[1]=box_l[1]*d_random();
		out_pos[2]=box_l[2]*d_random();
	
	}
} 

/**
* Writes a random position inside the central box into the provided array. Additionally it proposes points with a small radii more often than a uniform random probability density would do it.
*/
void ReactionEnsemble::get_random_position_in_box_enhanced_proposal_of_small_radii (double* out_pos) {
	double random_radius=m_current_reaction_system.cyl_radius*d_random(); //for enhanced proposal of small radii, needs correction within metropolis hasting algorithm, proposal density is p(x,y)=1/(2*pi*cyl_radius*r(x,y)), that means small radii are proposed more often
	double phi=2.0*PI*d_random();
	out_pos[0]=random_radius*cos(phi);
	out_pos[1]=random_radius*sin(phi);
	while (std::pow(out_pos[0],2)+std::pow(out_pos[1],2)<=std::pow(m_current_reaction_system.exclusion_radius,2) or std::pow(out_pos[0],2)+std::pow(out_pos[1],2) > std::pow(m_current_reaction_system.cyl_radius,2)){
		random_radius=m_current_reaction_system.cyl_radius*d_random();
		out_pos[0]=random_radius*cos(phi);
		out_pos[1]=random_radius*sin(phi);		
	}
	out_pos[0]+=m_current_reaction_system.cyl_x;
	out_pos[1]+=m_current_reaction_system.cyl_y;
	out_pos[2]=box_l[2]*d_random();
}

/**
* Creates a particle at the end of the observed particle id range.
*/
int ReactionEnsemble::create_particle(int desired_type){
    int p_id;
    if(m_empty_p_ids_smaller_than_max_seen_particle.size()>0){
        auto p_id_iter= std::min_element(std::begin(m_empty_p_ids_smaller_than_max_seen_particle), std::end(m_empty_p_ids_smaller_than_max_seen_particle));
        p_id=*p_id_iter;
        m_empty_p_ids_smaller_than_max_seen_particle.erase(p_id_iter);
    }else{
        p_id=max_seen_particle+1;
    }
	double pos_vec[3];
	
	//create random velocity vector according to Maxwell Boltzmann distribution for components
	double vel[3];
	//we usse mass=1 for all particles, think about adapting this
	vel[0]=std::pow(2*PI*m_current_reaction_system.temperature,-3.0/2.0)*gaussian_random()*time_step;//scale for internal use in espresso
	vel[1]=std::pow(2*PI*m_current_reaction_system.temperature,-3.0/2.0)*gaussian_random()*time_step;//scale for internal use in espresso
	vel[2]=std::pow(2*PI*m_current_reaction_system.temperature,-3.0/2.0)*gaussian_random()*time_step;//scale for internal use in espresso
	double charge= m_current_reaction_system.charges_of_types[find_index_of_type(desired_type)];
	bool particle_inserted_too_close_to_another_one=true;
	int max_insert_tries=1000;
	int insert_tries=0;
	double min_dist=m_current_reaction_system.exclusion_radius; //setting of a minimal distance is allowed to avoid overlapping configurations if there is a repulsive potential. States with very high energies have a probability of almost zero and therefore do not contribute to ensemble averages.
	if(min_dist!=0){
		while(particle_inserted_too_close_to_another_one && insert_tries<max_insert_tries) {
			get_random_position_in_box(pos_vec);
			place_particle(p_id,pos_vec);
			//set type
			set_particle_type(p_id, desired_type);
			#ifdef ELECTROSTATICS
			//set charge
			set_particle_q(p_id, charge);
			#endif
			//set velocities
			set_particle_v(p_id,vel);
			double d_min=distto(partCfg(), pos_vec,p_id); //TODO also catch constraints with an IFDEF CONSTRAINTS here, but only interesting, when doing MD/ HMC because then the system might explode easily here due to high forces
			insert_tries+=1;
			if(d_min>m_current_reaction_system.exclusion_radius)
				particle_inserted_too_close_to_another_one=false;
		}
	}else{
		get_random_position_in_box(pos_vec);
		place_particle(p_id,pos_vec);
		//set type
		set_particle_type(p_id, desired_type);	
		//set velocities
		set_particle_v(p_id,vel);
		#ifdef ELECTROSTATICS
		//set charge
		set_particle_q(p_id, charge);
		#endif
	}
	if(insert_tries>max_insert_tries){
		throw std::runtime_error("No particle inserted");
	}
	return p_id;
}


//the following 2 functions are directly taken from ABHmath.tcl
/**
* Calculates the normed vector of a given vector
*/
std::vector<double> vecnorm(std::vector<double> vec, double desired_length){
	for(int i=0;i<vec.size();i++){
		vec[i]=vec[i]/utils::veclen(vec)*desired_length;	
	}
	return vec;
}

/**
* Calculates a uniformly distributed vector on a sphere of given radius.
*/
std::vector<double> vec_random(double desired_length){
	/**returns a random vector of length len
	*(uniform distribution on a sphere)
	*This is done by chosing 3 uniformly distributed random numbers [-1,1]
	*If the length of the resulting vector is <= 1.0 the vector is taken and normalized
	*to the desired length, otherwise the procedure is repeated until succes.
	*On average the procedure needs 5.739 random numbers per vector.
	*(This is probably not the most efficient way, but it works!)
	*/
	std::vector<double> vec;
	while(1){
		for(int i=0;i<3;i++){
			vec.push_back(2*d_random()-1.0);
		}
		if (utils::veclen(vec)<=1)
			break;
	}
	vecnorm(vec,desired_length);
	return vec;
}

/**
* Adds a random vector of given length to the provided array named vector.
*/
void ReactionEnsemble::add_random_vector(double* vector, int len_vector, double length_of_displacement){
	//adds a vector which is uniformly distributed on a sphere
	std::vector<double> random_direction_vector = vec_random(length_of_displacement);
	for(int i=0;i<len_vector;i++){
		vector[i]+=random_direction_vector[i];
	}
}

/**
* Performs a global mc move for a particle of the provided type.
*/
bool ReactionEnsemble::do_global_mc_move_for_particles_of_type(int type, int start_id_polymer, int end_id_polymer, int particle_number_of_type, bool use_wang_landau){
	
	m_tried_configurational_MC_moves+=1;
	bool got_accepted=false;

	int old_state_index=-1;
	if(use_wang_landau){
		old_state_index=get_flattened_index_wang_landau_of_current_state();
		if(old_state_index>=0){
			if(m_current_wang_landau_system.histogram[old_state_index]>=0)
				m_current_wang_landau_system.monte_carlo_trial_moves+=1;
		}
	}

	int p_id;
	number_of_particles_with_type(type, &(particle_number_of_type));
	if(particle_number_of_type==0){
		//reject
		if(m_current_wang_landau_system.do_energy_reweighting && use_wang_landau){
			update_wang_landau_potential_and_histogram(old_state_index);
		}
		return got_accepted;
	}


	const double E_pot_old=calculate_current_potential_energy_of_system();

	std::vector<double> particle_positions(3*particle_number_of_type);
	int changed_particle_counter=0;
	std::vector<int> p_id_s_changed_particles(particle_number_of_type);

	//save old_position
	std::vector<double> temp_pos(3);
	get_random_position_in_box(temp_pos.data());
	while(changed_particle_counter<particle_number_of_type){
		if(changed_particle_counter==0){
			find_particle_type(type, &p_id);
		}else{
			//determine a p_id you have not touched yet
			while(is_in_list(p_id,p_id_s_changed_particles) or changed_particle_counter==0){
				find_particle_type(type, &p_id); //check wether you already touched this p_id
			}
		}

		auto part = get_particle_data(p_id);
		double ppos[3];
		memmove(ppos, part->r.p, 3*sizeof(double));

		particle_positions[3*changed_particle_counter]=ppos[0];
		particle_positions[3*changed_particle_counter+1]=ppos[1];
		particle_positions[3*changed_particle_counter+2]=ppos[2];
		place_particle(p_id,temp_pos.data());
		p_id_s_changed_particles[changed_particle_counter]=p_id;
		changed_particle_counter+=1;
	}
	
	//propose new positions
	changed_particle_counter=0;
	int max_tries=100*particle_number_of_type;//important for very dense systems
	int attempts=0;
	std::vector<double> new_pos(3);
	while(changed_particle_counter<particle_number_of_type){
		p_id=p_id_s_changed_particles[changed_particle_counter];
		bool particle_inserted_too_close_to_another_one=true;
		while(particle_inserted_too_close_to_another_one && attempts < max_tries){
			//change particle position
			get_random_position_in_box(new_pos.data());
//			get_random_position_in_box_enhanced_proposal_of_small_radii(new_pos); //enhanced proposal of small radii
			place_particle(p_id,new_pos.data());
			double d_min=distto(partCfg(), new_pos.data(),p_id);
			if(d_min>m_current_reaction_system.exclusion_radius){
				particle_inserted_too_close_to_another_one=false;
			}
			attempts+=1;
		}
		changed_particle_counter+=1;
	}
		if(attempts==max_tries){
		//reversing
		//create particles again at the positions they were
		for(int i=0;i<particle_number_of_type;i++)
			place_particle(p_id_s_changed_particles[i],&particle_positions[3*i]);
	}
	
	
	//change polymer conformation if start and end id are provided
	std::vector<double> old_pos_polymer_particle(3*(end_id_polymer-start_id_polymer+1));
	if(start_id_polymer>=0 && end_id_polymer >=0 ){
		
		for(int i=start_id_polymer;i<=end_id_polymer;i++){
			auto part = get_particle_data(i);
			//move particle to new position nearby
			const double length_of_displacement=0.05;
			add_random_vector(part->r.p, 3, length_of_displacement);
			place_particle(i,part->r.p);
		}
		
	}
	
	const double E_pot_new=calculate_current_potential_energy_of_system();
	double beta =1.0/m_current_reaction_system.temperature;
	
	int new_state_index;
	double bf=1.0;
	if(use_wang_landau){
		new_state_index=get_flattened_index_wang_landau_of_current_state();
		std::vector<int> dummy_old_particle_numbers;
		single_reaction& temp_unimportant_arbitrary_reaction=m_current_reaction_system.reactions[0]; //arbitrary reference to single reaction
		bf=calculate_boltzmann_factor_reaction_ensemble_wang_landau(temp_unimportant_arbitrary_reaction, E_pot_old, E_pot_new, dummy_old_particle_numbers, old_state_index, new_state_index, true);
	}else{
		bf=std::min(1.0, bf*exp(-beta*(E_pot_new-E_pot_old))); //Metropolis Algorithm since proposal density is symmetric
	}
	
	
//	//correct for enhanced proposal of small radii by using the metropolis hastings algorithm for asymmetric proposal densities.
//	double old_radius=std::sqrt(std::pow(particle_positions[0]-m_current_reaction_system.cyl_x,2)+std::pow(particle_positions[1]-m_current_reaction_system.cyl_y,2));
//	double new_radius=std::sqrt(std::pow(new_pos[0]-m_current_reaction_system.cyl_x,2)+std::pow(new_pos[1]-m_current_reaction_system.cyl_y,2));
//	bf=std::min(1.0, bf*exp(-beta*(E_pot_new-E_pot_old))*new_radius/old_radius); //Metropolis-Hastings Algorithm for asymmetric proposal density

	if(d_random()<bf){
		//accept
		m_accepted_configurational_MC_moves+=1;
		got_accepted=true;
		if(use_wang_landau && m_current_wang_landau_system.do_energy_reweighting){
			//modify wang_landau histogram and potential
			update_wang_landau_potential_and_histogram(new_state_index);		
		}
	}else{
		//reject
		//modify wang_landau histogram and potential
		if(use_wang_landau && m_current_wang_landau_system.do_energy_reweighting)
			update_wang_landau_potential_and_histogram(old_state_index);

		//create particles again at the positions they were
		for(int i=0;i<particle_number_of_type;i++)
			place_particle(p_id_s_changed_particles[i],&particle_positions[3*i]);
		//restore polymer particle again at original position
		if(start_id_polymer>=0 && end_id_polymer >=0 ){
			//place_particle(random_polymer_particle_id, old_pos_polymer_particle);
			for(int i=start_id_polymer;i<=end_id_polymer;i++)
				place_particle(i,&old_pos_polymer_particle[3*i]);
		}
	}
	return got_accepted;
}

///////////////////////////////////////////// Wang-Landau algorithm

/**
* Adds a new collective variable (CV) of the type degree of association to the Wang-Landau sampling
*/
void ReactionEnsemble::add_new_CV_degree_of_association(int associated_type, double CV_minimum, double CV_maximum, std::vector<int> corresponding_acid_types){
            collective_variable new_collective_variable;
            new_collective_variable.associated_type=associated_type;
            new_collective_variable.CV_minimum=CV_minimum;
            new_collective_variable.CV_maximum=CV_maximum;
            new_collective_variable.corresponding_acid_types=corresponding_acid_types;
            new_collective_variable.delta_CV=calculate_delta_degree_of_association(new_collective_variable);
            m_current_wang_landau_system.collective_variables.push_back(new_collective_variable);
            initialize_wang_landau();
}

/**
* Adds a new collective variable (CV) of the type potential energy to the Wang-Landau sampling
*/
void ReactionEnsemble::add_new_CV_potential_energy(std::string filename, double delta_CV){
            collective_variable new_collective_variable;
            new_collective_variable.energy_boundaries_filename=filename;
            new_collective_variable.delta_CV=delta_CV;
            m_current_wang_landau_system.collective_variables.push_back(new_collective_variable);
            initialize_wang_landau();
}

/**
* Returns the flattened index of the multidimensional Wang-Landau histogram
*/
int ReactionEnsemble::get_flattened_index_wang_landau(std::vector<double>& current_state, std::vector<double>& collective_variables_minimum_values, std::vector<double>& collective_variables_maximum_values, std::vector<double>& delta_collective_variables_values, int nr_collective_variables){
	int index=-10; //negative number is not allowed as index and therefore indicates error
	std::vector<int> individual_indices{}; //pre result
	individual_indices.resize(nr_collective_variables,-1); //initialize individual_indices to -1
	std::vector<int> nr_subindices_of_collective_variable =m_current_wang_landau_system.nr_subindices_of_collective_variable;

	//check for the current state to be an allowed state in the [range collective_variables_minimum_values:collective_variables_maximum_values], else return a negative index
	for(int collective_variable_i=0;collective_variable_i<nr_collective_variables;collective_variable_i++){
		if(current_state[collective_variable_i]>collective_variables_maximum_values[collective_variable_i]+delta_collective_variables_values[collective_variable_i]*0.98 || current_state[collective_variable_i]<collective_variables_minimum_values[collective_variable_i]-delta_collective_variables_values[collective_variable_i]*0.01)
			return -10;
	}

	for(int collective_variable_i=0;collective_variable_i<nr_collective_variables;collective_variable_i++){
		if(collective_variable_i==m_current_wang_landau_system.collective_variables.size()-1 &&
			 m_current_wang_landau_system.do_energy_reweighting)	//for energy collective variable (simple truncating conversion desired)
			individual_indices[collective_variable_i]=(int) ((current_state[collective_variable_i]-collective_variables_minimum_values[collective_variable_i])/delta_collective_variables_values[collective_variable_i]);
		else	//for degree of association collective variables (rounding conversion desired)
			individual_indices[collective_variable_i]=(int) ((current_state[collective_variable_i]-collective_variables_minimum_values[collective_variable_i])/delta_collective_variables_values[collective_variable_i]+0.5);
		if(individual_indices[collective_variable_i]<0 or individual_indices[collective_variable_i]>=nr_subindices_of_collective_variable[collective_variable_i]) // sanity check
			return -10;
	}
	
	//get flattened index from individual_indices
	index=0; //this is already part of the algorithm to find the correct index
	for(int collective_variable_i=0;collective_variable_i<nr_collective_variables;collective_variable_i++){
		int factor=1;
		for(int j=collective_variable_i+1;j<nr_collective_variables;j++){
			factor*=nr_subindices_of_collective_variable[j];
		}
		index+=factor*individual_indices[collective_variable_i];
		
	}	
	return index;
}

/**
* Returns the flattened index of the multidimensional Wang-Landau histogram for the current state of the simulation.
*/
int ReactionEnsemble::get_flattened_index_wang_landau_of_current_state(){
	int nr_collective_variables=m_current_wang_landau_system.collective_variables.size();
	//get current state
	std::vector<double> current_state(nr_collective_variables);
	for(int CV_i=0;CV_i<nr_collective_variables;CV_i++){
		current_state[CV_i]=m_current_wang_landau_system.collective_variables[CV_i].determine_current_state_in_current_collective_variable();
	}
	//get collective_variables_minimum_values
	std::vector<double> collective_variables_minimum_values(nr_collective_variables);
	for(int CV_i=0;CV_i<nr_collective_variables;CV_i++){
		collective_variables_minimum_values[CV_i]=m_current_wang_landau_system.collective_variables[CV_i].CV_minimum;	
	}
	//get collective_variables_maximum_values
	std::vector<double> collective_variables_maximum_values(nr_collective_variables);
	for(int CV_i=0;CV_i<nr_collective_variables;CV_i++){
		collective_variables_maximum_values[CV_i]=m_current_wang_landau_system.collective_variables[CV_i].CV_maximum;	
	}
	//get delta_collective_variables_values
	std::vector<double> delta_collective_variables_values(nr_collective_variables);
	for(int CV_i=0;CV_i<nr_collective_variables;CV_i++){
		delta_collective_variables_values[CV_i]=m_current_wang_landau_system.collective_variables[CV_i].delta_CV;	
	}
	int index=get_flattened_index_wang_landau(current_state, collective_variables_minimum_values, collective_variables_maximum_values, delta_collective_variables_values, nr_collective_variables);
	return index;
}

/**
* Returns the minimum value of the collective variable on a delta_CV spaced grid which starts at 0
*/
double get_minimum_CV_value_on_delta_CV_spaced_grid(double min_CV_value, double delta_CV) {
	//assume grid has it s origin at 0
	double minimum_CV_value_on_delta_CV_spaced_grid=floor(min_CV_value/delta_CV)*delta_CV;
	return minimum_CV_value_on_delta_CV_spaced_grid;
}


/**
* Calculates the smallest difference in the degree of association which can be observed when changing the degree of association by one single reaction.
*/
double ReactionEnsemble::calculate_delta_degree_of_association(collective_variable& current_collective_variable){
	//calculate Delta in the degree of association so that EVERY reaction step is driven.
	int total_number_of_corresponding_acid=0;
	for(int corresponding_type_i=0; corresponding_type_i<current_collective_variable.corresponding_acid_types.size();corresponding_type_i++){
		int num_of_current_type;
		number_of_particles_with_type(current_collective_variable.corresponding_acid_types[corresponding_type_i],&num_of_current_type);
		total_number_of_corresponding_acid+=num_of_current_type;
	}
	double delta=1.0/total_number_of_corresponding_acid;
	//now modify the minimum value of the CV to lie on the grid
	current_collective_variable.CV_minimum=get_minimum_CV_value_on_delta_CV_spaced_grid(current_collective_variable.CV_minimum,delta);
	return delta ;
}

/**
* Initializes the Wang-Landau histogram.
*/
int ReactionEnsemble::get_num_needed_bins(){
	int needed_bins=1;
	for(int CV_i=0;CV_i<m_current_wang_landau_system.collective_variables.size();CV_i++){
		collective_variable& current_collective_variable=m_current_wang_landau_system.collective_variables[CV_i];
		needed_bins=needed_bins*(int((current_collective_variable.CV_maximum-current_collective_variable.CV_minimum)/current_collective_variable.delta_CV)+1); // plus 1 needed for degrees of association related part of histogram (think of only one acid particle)
	}
    return needed_bins;
}

/**
* Finds the minimum non negative value in the provided double array and returns this value.
*/
double find_minimum_non_negative_value(double* list, int len){
	double minimum =list[0];
	for (int i=0;i<len;i++){
		if(minimum<0)
			minimum=list[i];//think of negative histogram values that indicate not allowed energies in the case of an energy observable
		if(list[i]<minimum && list[i]>=0)
			minimum=list[i];	
	}
	return minimum;
}

/**
* Finds the maximum in a double array and returns it.
*/
double find_maximum(double* list, int len){
	double maximum =list[0];
	for (int i=0;i<len;i++){
		if(list[i]>maximum)
			maximum=list[i];	
	}
	return maximum;
}

/**
* Initializes the current Wang-Landau system.
*/
int ReactionEnsemble::initialize_wang_landau(){
	
	//initialize deltas for collective variables which are of the type of a degree of association
	int energy_collective_variable_index=-10;
	std::vector<double> min_boundaries_energies;
	std::vector<double> max_boundaries_energies;
	for(int collective_variable_i=0; collective_variable_i<m_current_wang_landau_system.collective_variables.size();collective_variable_i++){
		collective_variable* current_collective_variable=&m_current_wang_landau_system.collective_variables[collective_variable_i];
		int flattened_index_previous_run=0; //len_histogram of energy preparation run
		if(!current_collective_variable->energy_boundaries_filename.empty()){
			//found a collective variable which is not of the type of an energy
			m_current_wang_landau_system.do_energy_reweighting=true;
			energy_collective_variable_index=collective_variable_i;
			//load energy boundaries from file
			FILE* pFile;
			pFile = fopen(current_collective_variable->energy_boundaries_filename.c_str(),"r");
			if (pFile==nullptr){
			    throw std::runtime_error("ERROR: energy boundaries file for the specific system could not be read.\n");
				// Note that you cannot change the other collective variables in the pre-production run and the production run
				return ES_ERROR;
			}
			//save minimum and maximum energies as a function of the other collective variables under m_current_wang_landau_system.energ...
			char *line = nullptr;
			size_t len = 0;
			ssize_t length_line;
			const char* delim="\t ";
			length_line =getline(&line, &len, pFile);//dummy call of getline to get rid of header line (first line in file)
			while ((length_line = getline(&line, &len, pFile)) != -1) {
				int counter_words_in_line=0;
				for(char* word=strtok(line,delim);word!=nullptr;word=strtok(nullptr,delim)){
					if(counter_words_in_line<m_current_wang_landau_system.collective_variables.size()-1){
						counter_words_in_line+=1;
						continue;
					}else if(counter_words_in_line==m_current_wang_landau_system.collective_variables.size()-1){
						double energy_boundary_minimum=atof(word);
						counter_words_in_line+=1;
						min_boundaries_energies.push_back(energy_boundary_minimum);
					}else if(counter_words_in_line==m_current_wang_landau_system.collective_variables.size()){
						double energy_boundary_maximum=atof(word);
						max_boundaries_energies.push_back(energy_boundary_maximum);
						counter_words_in_line+=1;							
					}
					
				}
				flattened_index_previous_run+=1;		
			}
			current_collective_variable->CV_minimum=*(std::min_element(min_boundaries_energies.begin(),min_boundaries_energies.end()));
			current_collective_variable->CV_maximum=*(std::max_element(max_boundaries_energies.begin(),max_boundaries_energies.end()));
		}
	}

	m_current_wang_landau_system.nr_subindices_of_collective_variable.resize(m_current_wang_landau_system.collective_variables.size(),0);
	int new_collective_variable_i=m_current_wang_landau_system.collective_variables.size()-1;
	m_current_wang_landau_system.nr_subindices_of_collective_variable[new_collective_variable_i]=int((m_current_wang_landau_system.collective_variables[new_collective_variable_i].CV_maximum-m_current_wang_landau_system.collective_variables[new_collective_variable_i].CV_minimum)/m_current_wang_landau_system.collective_variables[new_collective_variable_i].delta_CV)+1; //+1 for collecive variables which are of type degree of association

	//construct (possibly higher dimensional) histogram over Gamma (the room which should be equally sampled when the wang-landau algorithm has converged)
	int needed_bins=get_num_needed_bins();
	m_current_wang_landau_system.histogram.resize(needed_bins,0); //initialize new values with 0

	//construct (possibly higher dimensional) wang_landau potential over Gamma (the room which should be equally sampled when the wang-landau algorithm has converged)
	m_current_wang_landau_system.wang_landau_potential.resize(needed_bins,0); //initialize new values with 0
	
	m_current_wang_landau_system.used_bins=needed_bins; //initialize for 1/t wang_landau algorithm
	
	if(energy_collective_variable_index>=0){
		//make values in histogram and wang landau potential negative if they are not allowed at the given degree of association, because the energy boundaries prohibit them

		int empty_bins_in_memory=0;

		for(int flattened_index=0;flattened_index<m_current_wang_landau_system.wang_landau_potential.size();flattened_index++){
			//unravel index
			std::vector<int> unraveled_index(m_current_wang_landau_system.collective_variables.size());
			Utils::unravel_index(m_current_wang_landau_system.nr_subindices_of_collective_variable.data(),m_current_wang_landau_system.collective_variables.size(),flattened_index,unraveled_index.data());
			//use unraveled index
			double current_energy=unraveled_index[energy_collective_variable_index]*m_current_wang_landau_system.collective_variables[energy_collective_variable_index].delta_CV+m_current_wang_landau_system.collective_variables[energy_collective_variable_index].CV_minimum;
			if(current_energy>max_boundaries_energies[get_flattened_index_wang_landau_without_energy_collective_variable(flattened_index,energy_collective_variable_index)] || current_energy<min_boundaries_energies[get_flattened_index_wang_landau_without_energy_collective_variable(flattened_index,energy_collective_variable_index)]-m_current_wang_landau_system.collective_variables[energy_collective_variable_index].delta_CV ){
				m_current_wang_landau_system.histogram[flattened_index]=m_current_wang_landau_system.int_fill_value;
				m_current_wang_landau_system.wang_landau_potential[flattened_index]=m_current_wang_landau_system.double_fill_value;
				empty_bins_in_memory+=1;	
			}
		}
		
		m_current_wang_landau_system.used_bins=m_current_wang_landau_system.wang_landau_potential.size()-empty_bins_in_memory;
		
	}
	
	return ES_OK;
}

/**
* Calculates the expression which occurs in the Wang-Landau acceptance probability.
*/
double ReactionEnsemble::calculate_boltzmann_factor_reaction_ensemble_wang_landau(single_reaction& current_reaction, double E_pot_old, double E_pot_new, std::vector<int>& old_particle_numbers, int old_state_index, int new_state_index, bool only_make_configuration_changing_move){
	/**determine the acceptance probabilities of the reaction move 
	* in wang landau reaction ensemble
	*/
	double beta =1.0/m_current_reaction_system.temperature;
	double bf;
	if(m_current_wang_landau_system.do_not_sample_reaction_partition_function || only_make_configuration_changing_move){
		bf=1.0;
	}else{
		const double volume = m_current_reaction_system.volume;
		double factorial_expr=calculate_factorial_expression(current_reaction, old_particle_numbers.data());
		double standard_pressure_in_simulation_units=m_current_reaction_system.standard_pressure_in_simulation_units;
		bf=std::pow(volume*beta*standard_pressure_in_simulation_units, current_reaction.nu_bar) * current_reaction.equilibrium_constant * factorial_expr;
	}
	
	if(!m_current_wang_landau_system.do_energy_reweighting){
		bf= bf * exp(-beta * (E_pot_new - E_pot_old));
	} else {
		//pass
	}
	//look wether the proposed state lies in the reaction coordinate space Gamma and add the Wang-Landau modification factor, this is a bit nasty due to the energy collective variable case (memory layout of storage array of the histogram and the wang_landau_potential values is "cuboid")
	if(old_state_index>=0 && new_state_index>=0){
		if(m_current_wang_landau_system.histogram[new_state_index]>=0 &&m_current_wang_landau_system.histogram[old_state_index]>=0 ){
			bf=std::min(1.0, bf*exp(m_current_wang_landau_system.wang_landau_potential[old_state_index]-m_current_wang_landau_system.wang_landau_potential[new_state_index])); //modify boltzmann factor according to wang-landau algorithm, according to grand canonical simulation paper "Density-of-states Monte Carlo method for simulation of fluids"
			//this makes the new state being accepted with the conditinal probability bf (bf is a transition probability = conditional probability from the old state to move to the new state)
		}else{
			if(m_current_wang_landau_system.histogram[new_state_index]>=0 &&m_current_wang_landau_system.histogram[old_state_index]<0 )
				bf=10;//this makes the reaction get accepted, since we found a state in Gamma
			else if (m_current_wang_landau_system.histogram[new_state_index]<0 &&m_current_wang_landau_system.histogram[old_state_index]<0)
				bf=10;//accept, in order to be able to sample new configs, which might lie in Gamma
			else if(m_current_wang_landau_system.histogram[new_state_index]<0 &&m_current_wang_landau_system.histogram[old_state_index]>=0)
				bf=-10;//this makes the reaction get rejected, since the new state is not in Gamma while the old sate was in Gamma
		}
	}else if(old_state_index<0 && new_state_index>=0){
		bf=10;	//this makes the reaction get accepted, since we found a state in Gamma
	}else if(old_state_index<0 && new_state_index<0){
		bf=10;	//accept, in order to be able to sample new configs, which might lie in Gamma
	}else if(old_state_index>=0 && new_state_index<0){
		bf=-10; //this makes the reaction get rejected, since the new state is not in Gamma while the old sate was in Gamma
	}
	return bf;	
}

void ReactionEnsemble::update_wang_landau_potential_and_histogram(int index_of_state_after_acceptance_or_rejection){
	/**increase the wang landau potential and histogram at the current nbar */
	if(index_of_state_after_acceptance_or_rejection>=0 ){
		if(m_current_wang_landau_system.histogram[index_of_state_after_acceptance_or_rejection]>=0){
			m_current_wang_landau_system.histogram[index_of_state_after_acceptance_or_rejection]+=1;
			m_current_wang_landau_system.wang_landau_potential[index_of_state_after_acceptance_or_rejection]+=m_current_wang_landau_system.wang_landau_parameter;
		}
	}

}


/** Performs a randomly selected reaction using the Wang-Landau algorithm.
*make sure to perform additional configuration changing steps, after the reaction step! like in Density-of-states Monte Carlo method for simulation of fluids Yan, De Pablo. this can be done with MD in the case of the no-energy-reweighting case, or with the functions do_global_mc_move_for_particles_of_type
*perform additional Monte-carlo moves to to sample configurational partition function
*according to "Density-of-states Monte Carlo method for simulation of fluids"
do as many steps as needed to get to a new conformation (compare Density-of-states Monte Carlo method for simulation of fluids Yan, De Pablo)*/
int ReactionEnsemble::do_reaction_wang_landau(){
	m_WL_tries+=m_current_wang_landau_system.wang_landau_steps;
	bool got_accepted=false;
	for(int step=0;step<m_current_wang_landau_system.wang_landau_steps;step++){
		int reaction_id=i_random(m_current_reaction_system.nr_single_reactions);
		got_accepted=generic_oneway_reaction(reaction_id, reaction_ensemble_wang_landau_mode);
		if(got_accepted){
			m_WL_accepted_moves+=1;
		}
		if(can_refine_wang_landau_one_over_t()&& m_WL_tries%10000==0){
			//check for convergence
			if(achieved_desired_number_of_refinements_one_over_t()){
				ReactionEnsemble::write_wang_landau_results_to_file(m_current_wang_landau_system.output_filename);
				return -10; //return negative value to indicate that the Wang-Landau algorithm has converged
			}
			refine_wang_landau_parameter_one_over_t();
		}
	}
	//shift wang landau potential minimum to zero
	if(m_WL_tries%(std::max(90000,9*m_current_wang_landau_system.wang_landau_steps))==0){
		//for numerical stability here we also subtract the minimum positive value of the wang_landau_potential from the wang_landau potential, allowed since only the difference in the wang_landau potential is of interest.
		double minimum_wang_landau_potential=find_minimum_non_negative_value(m_current_wang_landau_system.wang_landau_potential.data(),m_current_wang_landau_system.wang_landau_potential.size());
		for(int i=0;i<m_current_wang_landau_system.wang_landau_potential.size();i++){
			if(m_current_wang_landau_system.wang_landau_potential[i]>=0)//check for wether we are in the valid range of the collective variable
				m_current_wang_landau_system.wang_landau_potential[i]-=minimum_wang_landau_potential;	
		}
		//write out preliminary wang-landau potential results
		write_wang_landau_results_to_file(m_current_wang_landau_system.output_filename);
	}
	return 0;	
}


//boring helper functions


/**
*Determines wether we can reduce the Wang-Landau parameter
*/
bool ReactionEnsemble::can_refine_wang_landau_one_over_t(){
	double minimum_required_value=0.80*average_list_of_allowed_entries(m_current_wang_landau_system.histogram); // This is an additional constraint to sample configuration space better. Use flatness criterion according to 1/t algorithm as long as you are not in 1/t regime.
	if(m_current_wang_landau_system.do_energy_reweighting)
		minimum_required_value=20; //get faster in energy reweighting case

	return *(std::min_element(m_current_wang_landau_system.histogram.begin(),m_current_wang_landau_system.histogram.end())) > minimum_required_value || m_system_is_in_1_over_t_regime == true;
}

/**
*Reset the Wang-Landau histogram.
*/
void ReactionEnsemble::reset_histogram(){
	printf("Histogram is flat. Refining. Previous Wang-Landau modification parameter was %f.\n",m_current_wang_landau_system.wang_landau_parameter);
	fflush(stdout);
	
	for(int i=0;i<m_current_wang_landau_system.wang_landau_potential.size();i++){
		if(m_current_wang_landau_system.histogram[i]>=0){//checks for validity of index i (think of energy collective variables, in a cubic memory layout there will be indices which are not allowed by the energy boundaries. These values will be initalized with a negative fill value)
			m_current_wang_landau_system.histogram[i]=0;
		}	
	}
	
}

/**
*Refine the Wang-Landau parameter using the 1/t rule.
*/
void ReactionEnsemble::refine_wang_landau_parameter_one_over_t(){
	double monte_carlo_time = double(m_current_wang_landau_system.monte_carlo_trial_moves)/m_current_wang_landau_system.used_bins;
	if ( m_current_wang_landau_system.wang_landau_parameter/2.0 <=1.0/monte_carlo_time || m_system_is_in_1_over_t_regime){
		m_current_wang_landau_system.wang_landau_parameter= 1.0/monte_carlo_time;
		if(!m_system_is_in_1_over_t_regime){
			m_system_is_in_1_over_t_regime=true;
			printf("Refining: Wang-Landau parameter is now 1/t.\n");
		}
	} else {
		reset_histogram();
		m_current_wang_landau_system.wang_landau_parameter= m_current_wang_landau_system.wang_landau_parameter/2.0;
		
	}
}

/**
*Determine whether the desired number of refinements was achieved.
*/
bool ReactionEnsemble::achieved_desired_number_of_refinements_one_over_t() {
	if(m_current_wang_landau_system.wang_landau_parameter < m_current_wang_landau_system.final_wang_landau_parameter) {
		printf("Achieved desired number of refinements\n");
		return true;
	} else {
		return false;
	}

}



/**
*Writes the Wang-Landau potential to file.
*/
void ReactionEnsemble::write_wang_landau_results_to_file(std::string full_path_to_output_filename){

	FILE* pFile;
	pFile = fopen(full_path_to_output_filename.c_str(),"w");
	if (pFile==nullptr){
	    throw std::runtime_error("ERROR: Wang-Landau file could not be written\n");
	}else{
		std::vector<int> nr_subindices_of_collective_variable =m_current_wang_landau_system.nr_subindices_of_collective_variable;
		for(int flattened_index=0;flattened_index<m_current_wang_landau_system.wang_landau_potential.size();flattened_index++){
			//unravel index
			if(std::abs(m_current_wang_landau_system.wang_landau_potential[flattened_index]-m_current_wang_landau_system.double_fill_value)>1){ //only output data if they are not equal to m_current_reaction_system.double_fill_value. This if ensures that for the energy observable not allowed energies (energies in the interval [global_E_min, global_E_max]) in the multidimensional wang landau potential are printed out, since the range [E_min(nbar), E_max(nbar)] for each nbar may be a different one
				std::vector<int> unraveled_index(m_current_wang_landau_system.collective_variables.size());
				Utils::unravel_index(nr_subindices_of_collective_variable.data(),m_current_wang_landau_system.collective_variables.size(),flattened_index,unraveled_index.data());
				//use unraveled index
				for(int i=0;i<m_current_wang_landau_system.collective_variables.size();i++){
					fprintf(pFile, "%f ",unraveled_index[i]*m_current_wang_landau_system.collective_variables[i].delta_CV+m_current_wang_landau_system.collective_variables[i].CV_minimum);
				}
				fprintf(pFile, "%f \n", m_current_wang_landau_system.wang_landau_potential[flattened_index]);
			}
		}
		fflush(pFile);
		fclose(pFile);
	}

}

/**
*Update the minimum and maximum observed energies using the current state. Needed for perliminary energy reweighting runs.
*/
int ReactionEnsemble::update_maximum_and_minimum_energies_at_current_state(){
	if(m_current_wang_landau_system.minimum_energies_at_flat_index.size()==0 || m_current_wang_landau_system.maximum_energies_at_flat_index.size()==0){
		m_current_wang_landau_system.minimum_energies_at_flat_index.resize(m_current_wang_landau_system.wang_landau_potential.size(),m_current_wang_landau_system.double_fill_value);
		m_current_wang_landau_system.maximum_energies_at_flat_index.resize(m_current_wang_landau_system.wang_landau_potential.size(),m_current_wang_landau_system.double_fill_value);
	}
	
	const double E_pot_current=calculate_current_potential_energy_of_system();
	int index=get_flattened_index_wang_landau_of_current_state();

	//update stored energy values
	if( (( E_pot_current<m_current_wang_landau_system.minimum_energies_at_flat_index[index])|| std::abs(m_current_wang_landau_system.minimum_energies_at_flat_index[index] -m_current_wang_landau_system.double_fill_value)<std::numeric_limits<double>::epsilon()) ) {
		m_current_wang_landau_system.minimum_energies_at_flat_index[index]=E_pot_current;
	}
	if( ((E_pot_current>m_current_wang_landau_system.maximum_energies_at_flat_index[index]) || std::abs(m_current_wang_landau_system.maximum_energies_at_flat_index[index] -m_current_wang_landau_system.double_fill_value)<std::numeric_limits<double>::epsilon()) ) {
		m_current_wang_landau_system.maximum_energies_at_flat_index[index]= E_pot_current;
	}
	

	return 0;
}

/**
*Write out an energy boundary file using the energy boundaries observed in a preliminary energy reweighting run.
*/
void ReactionEnsemble::write_out_preliminary_energy_run_results (std::string full_path_to_output_filename) {
	FILE* pFile;
	pFile = fopen(full_path_to_output_filename.c_str(),"w");
	if(pFile==nullptr){
	    throw std::runtime_error("ERROR: Wang-Landau file could not be written\n");
	}else{
		fprintf(pFile, "#nbar E_min E_max\n");
		std::vector<int> nr_subindices_of_collective_variable =m_current_wang_landau_system.nr_subindices_of_collective_variable;

		for(int flattened_index=0;flattened_index<m_current_wang_landau_system.wang_landau_potential.size();flattened_index++){
			//unravel index
			std::vector<int> unraveled_index(m_current_wang_landau_system.collective_variables.size());
			Utils::unravel_index(nr_subindices_of_collective_variable.data(),m_current_wang_landau_system.collective_variables.size(),flattened_index,unraveled_index.data());
			//use unraveled index
			for(int i=0;i<m_current_wang_landau_system.collective_variables.size();i++){
				fprintf(pFile, "%f ",unraveled_index[i]*m_current_wang_landau_system.collective_variables[i].delta_CV+m_current_wang_landau_system.collective_variables[i].CV_minimum);
			}
			fprintf(pFile, "%f %f \n", m_current_wang_landau_system.minimum_energies_at_flat_index[flattened_index], m_current_wang_landau_system.maximum_energies_at_flat_index[flattened_index]);
		}
		fflush(pFile);
		fclose(pFile);
	}
}


/**
*Returns the flattened index of a given flattened index without the energy collective variable.
*/
int ReactionEnsemble::get_flattened_index_wang_landau_without_energy_collective_variable(int flattened_index_with_energy_collective_variable, int collective_variable_index_energy_observable){
	std::vector<int> nr_subindices_of_collective_variable=m_current_wang_landau_system.nr_subindices_of_collective_variable;
	//unravel index
	std::vector<int> unraveled_index(m_current_wang_landau_system.collective_variables.size());
	Utils::unravel_index(nr_subindices_of_collective_variable.data(),m_current_wang_landau_system.collective_variables.size(),flattened_index_with_energy_collective_variable,unraveled_index.data());
	//use unraveled index
	const int nr_collective_variables=m_current_wang_landau_system.collective_variables.size()-1; //forget the last collective variable (the energy collective variable)
	std::vector<double> current_state(nr_collective_variables);
	for(int i=0;i<nr_collective_variables;i++){
		current_state[i]=unraveled_index[i]*m_current_wang_landau_system.collective_variables[i].delta_CV+m_current_wang_landau_system.collective_variables[i].CV_minimum;
	}
	
	//get collective_variables_minimum_values
	std::vector<double> collective_variables_minimum_values(nr_collective_variables);
	for(int CV_i=0;CV_i<nr_collective_variables;CV_i++){
		collective_variables_minimum_values[CV_i]=m_current_wang_landau_system.collective_variables[CV_i].CV_minimum;	
	}
	//get collective_variables_maximum_values
	std::vector<double> collective_variables_maximum_values(nr_collective_variables);
	for(int CV_i=0;CV_i<nr_collective_variables;CV_i++){
		collective_variables_maximum_values[CV_i]=m_current_wang_landau_system.collective_variables[CV_i].CV_maximum;	
	}
	//get delta_collective_variables_values
	std::vector<double> delta_collective_variables_values(nr_collective_variables);
	for(int CV_i=0;CV_i<nr_collective_variables;CV_i++){
		delta_collective_variables_values[CV_i]=m_current_wang_landau_system.collective_variables[CV_i].delta_CV;	
	}
	int index=get_flattened_index_wang_landau(current_state, collective_variables_minimum_values, collective_variables_maximum_values, delta_collective_variables_values, nr_collective_variables);
	return index;
}

/** remove bins from the range of to be sampled values if they have not been sampled.
*use with caution otherwise you produce unpyhsical results, do only use when you know what you want to do. This can make wang landau converge on a reduced set Gamma. use this function e.g. in do_reaction_wang_landau() for the diprotonic acid
*compare "Wang-Landau sampling with self-adaptive range" by Troester and Dellago
*/
void ReactionEnsemble::remove_bins_that_have_not_been_sampled(){
	int removed_bins=0;
	for(int k=0;k<m_current_wang_landau_system.wang_landau_potential.size();k++){
		if(m_current_wang_landau_system.wang_landau_potential[k]==0){
			removed_bins+=1;
			// criterion is derived from the canonical partition function and the ration of two summands for the same particle number
			m_current_wang_landau_system.histogram[k]=m_current_wang_landau_system.int_fill_value;
			m_current_wang_landau_system.wang_landau_potential[k]=m_current_wang_landau_system.double_fill_value;
		}
		
	}
	printf("Removed %d bins from the Wang-Landau spectrum\n",removed_bins);
	//update used bins
	m_current_wang_landau_system.used_bins-=removed_bins;
}

/**
*Writes the Wang-Landau parameter, the histogram and the potential to a file. You can restart a Wang-Landau simulation using this information. Additionally you should store the positions of the particles. Not storing them introduces small, small statistical errors.
*/
int ReactionEnsemble::write_wang_landau_checkpoint(std::string identifier){
	std::ofstream outfile;

	//write current wang landau parameters (wang_landau_parameter, monte_carlo_trial_moves, flat_index_of_current_state)
	outfile.open(std::string("checkpoint_wang_landau_parameters_")+identifier);
	outfile << m_current_wang_landau_system.wang_landau_parameter << " " << m_current_wang_landau_system.monte_carlo_trial_moves << " " << get_flattened_index_wang_landau_of_current_state() << "\n" ;	
	outfile.close();

	//write histogram
	outfile.open(std::string("checkpoint_wang_landau_histogram_")+identifier);
	for(int i=0;i<m_current_wang_landau_system.wang_landau_potential.size();i++){
		outfile << m_current_wang_landau_system.histogram[i] <<"\n" ;
	}
	outfile.close();
	//write wang landau potential
	outfile.open(std::string("checkpoint_wang_landau_potential_")+identifier);
	for(int i=0;i<m_current_wang_landau_system.wang_landau_potential.size();i++){
		outfile << m_current_wang_landau_system.wang_landau_potential[i]<<"\n";	
	}
	outfile.close();
	return 0;
}


/**
*Loads the Wang-Landau checkpoint
*/
int ReactionEnsemble::load_wang_landau_checkpoint(std::string identifier){
	std::ifstream infile;
	
	//restore wang landau parameters
	infile.open(std::string("checkpoint_wang_landau_parameters_")+identifier);
	if(infile.is_open()) {
		
		double wang_landau_parameter_entry;
		int wang_landau_monte_carlo_trial_moves_entry;
		int flat_index_of_state_at_checkpointing;
		int line=0;
		while (infile >> wang_landau_parameter_entry >> wang_landau_monte_carlo_trial_moves_entry >> flat_index_of_state_at_checkpointing)
		{
			m_current_wang_landau_system.wang_landau_parameter=wang_landau_parameter_entry;
			m_current_wang_landau_system.monte_carlo_trial_moves=wang_landau_monte_carlo_trial_moves_entry;
			line+=1;
		}
		infile.close();
	} else {
		std::cout << "Exception opening " << std::string("checkpoint_wang_landau_parameters_")+identifier  << "\n" << std::flush;
	}
	
	//restore histogram
	infile.open(std::string("checkpoint_wang_landau_histogram_")+identifier);
	if(infile.is_open()){ 
		int hist_entry;
		int line=0;
		while (infile >> hist_entry)
		{
			m_current_wang_landau_system.histogram[line]=hist_entry;
			line+=1;
		}
		infile.close();
	} else {
		std::cout << "Exception opening/ reading " << std::string("checkpoint_wang_landau_histogram_")+identifier << "\n" << std::flush;
	}
	
	//restore wang landau potential
	infile.open(std::string("checkpoint_wang_landau_potential_")+identifier);
	if(infile.is_open()) {	
		double wang_landau_potential_entry;
		int line=0;
		while (infile >> wang_landau_potential_entry)
		{
			m_current_wang_landau_system.wang_landau_potential[line]=wang_landau_potential_entry;
			line+=1;
		}
		infile.close();
	} else {
		std::cout << "Exception opening " << std::string("checkpoint_wang_landau_potential_")+identifier  << "\n" << std::flush;
	}
	
	//possible task: restore state in which the system was when the checkpoint was written. However as long as checkpointing and restoring the system form the checkpoint is rare this should not matter statistically.
	
	return 0;
}



int ReactionEnsemble::get_random_p_id(){
    int random_p_id = i_random(max_seen_particle);
    while(is_in_list(random_p_id, m_empty_p_ids_smaller_than_max_seen_particle))
        random_p_id = i_random(max_seen_particle);
    return random_p_id;
}



/**
* Constant-pH Ensemble, for derivation see Reed and Reed 1992
* For the constant pH reactions you need to provide the deprotonation and afterwards the corresponding protonation reaction (in this order). If you want to deal with multiple reactions do it multiple times.
* Note that there is a difference in the usecase of the constant pH reactions and the above reaction ensemble. For the constant pH simulation directily the **apparent equilibrium constant which carries a unit** needs to be provided -- this is different from the reaction ensemble above, where the dimensionless reaction constant needs to be provided. Again: For the constant-pH algorithm not the dimensionless reaction constant needs to be provided here, but the apparent reaction constant.
*/

/**
*Performs a reaction in the constant pH ensemble
*/
int ReactionEnsemble::do_reaction_constant_pH(){
	//get a list of reactions where a randomly selected particle type occurs in the reactant list. the selection probability of the particle types has to be proportional to the number of occurances of the number of particles with this type
	
	//for optimizations this list could be determined during the initialization
	std::vector<int> list_of_reaction_ids_with_given_reactant_type;
	while(list_of_reaction_ids_with_given_reactant_type.size()==0) { // avoid selecting a (e.g. salt) particle which does not take part in a reaction
		int random_p_id =get_random_p_id(); // only used to determine which reaction is attempted.
		auto part = get_particle_data(random_p_id);

		if(part==nullptr)
			continue;

		int type_of_random_p_id = part->p.type;

		//construct list of reactions with the above reactant type
		for(int reaction_i=0;reaction_i<m_current_reaction_system.nr_single_reactions;reaction_i++){
			single_reaction& current_reaction=m_current_reaction_system.reactions[reaction_i];
			for(int reactant_i=0; reactant_i< 1; reactant_i++){ //reactant_i<1 since it is assumed in this place that the types A, and HA occur in the first place only. These are the types that should be switched, H+ should not be switched
				if(current_reaction.reactant_types[reactant_i]== type_of_random_p_id){
					list_of_reaction_ids_with_given_reactant_type.push_back(reaction_i);
					break;
				}
			}
		}
	}
	
	//randomly select a reaction to be performed
	int reaction_id=list_of_reaction_ids_with_given_reactant_type[i_random(list_of_reaction_ids_with_given_reactant_type.size())];
	generic_oneway_reaction(reaction_id, constant_pH_mode);
	return 0;
}

double ReactionEnsemble::calculate_boltzmann_factor_consant_pH(single_reaction& current_reaction, double E_pot_old, double E_pot_new){
	/**
    *Calculates the expression in the acceptance probability of the constant pH method.
    */
	double ln_bf;
	double pKa;
	const double beta =1.0/m_current_reaction_system.temperature;
	if(current_reaction.nu_bar > 0){ //deprotonation of monomer
		pKa = -log10(current_reaction.equilibrium_constant);
		ln_bf= (E_pot_new - E_pot_old)- 1.0/beta*log(10)*(m_constant_pH-pKa) ;
	}else{ //protonation of monomer (yields neutral monomer)
		pKa = -(-log10(current_reaction.equilibrium_constant)); //additional minus, since in this case 1/Ka is stored in the equilibrium constant
		
		ln_bf= (E_pot_new - E_pot_old)+ 1.0/beta*log(10)*(m_constant_pH-pKa);
	}
	double bf=exp(-beta*ln_bf);
	return bf;
}

}
