#include "utils.hpp"
#include <string>

namespace ReactionEnsemble {

struct single_reaction {
  // strict input to the algorithm
  std::vector<int> reactant_types;
  int len_reactant_types;
  std::vector<int> reactant_coefficients;
  std::vector<int> product_types;
  int len_product_types;
  std::vector<int> product_coefficients;
  double equilibrium_constant;
  // calculated values that are stored for performance reasons
  int nu_bar;
};

struct reaction_system {
  int nr_single_reactions;
  std::vector<single_reaction> reactions;
  std::vector<int> type_index;
  int nr_different_types; // is equal to length type_index
  std::vector<double> charges_of_types;
  double standard_pressure_in_simulation_units;
  double temperature;
  double exclusion_radius; // this is used as a kind of hard sphere radius, if
                           // particles are closer than that it is assumed that
                           // their interaction energy gets approximately
                           // infinite => these configurations do not contribute
                           // to the partition function and ensemble averages.
  double volume;
  bool box_is_cylindric_around_z_axis;
  double cyl_radius;
  double cyl_x;
  double cyl_y;
  bool box_has_wall_constraints;
  double slab_start_z;
  double slab_end_z;
  int non_interacting_type;
};

struct stored_particle_property {
  int p_id;
  double charge;
  int type;
};

struct collective_variable {
  double CV_minimum;
  double CV_maximum;
  double delta_CV;
  double (*determine_current_state_in_collective_variable_with_index)(
      int,
      void *
          m_wang_landau_system); // declare a function pointer with name
                                 // determine_current_state_in_this_collective_variable
                                 // that has to point to a function that has 1
                                 // int input parameter (for the
                                 // collective_variable_index) and another input
                                 // for a void pointer and returns a double
  // for collective variables of type degree of association
  std::vector<int> corresponding_acid_types; // is NULL if the current collective variable
                                 // has nothing to do with a degree of
                                 // association
  int associated_type;
  // for collective variables of type energy
  std::string energy_boundaries_filename;
};

struct wang_landau_system {
  std::vector<int> histogram;
  std::vector<double> wang_landau_potential; // equals the logarithm to basis e of the
                                 // degeneracy of the states
  std::vector<collective_variable> collective_variables;
  std::vector<int> nr_subindices_of_collective_variable;
  double wang_landau_parameter; // equals the logarithm to basis e of the
                                // modification factor of the degeneracy of
                                // states when the state is visited
  double initial_wang_landau_parameter;

  int int_fill_value;
  double double_fill_value;

  int number_of_monte_carlo_moves_between_check_of_convergence;
  double final_wang_landau_parameter;
  int used_bins;               // for 1/t algorithm
  int monte_carlo_trial_moves; // for 1/t algorithm

  int wang_landau_steps; // may be used for performance improvements, when you
                         // do not want to record other observables in the tcl
                         // script
  std::string output_filename;

  std::vector<double>
      minimum_energies_at_flat_index; // only present in energy preparation run
  std::vector<double>
      maximum_energies_at_flat_index; // only present in energy preparation run

  bool do_energy_reweighting;
  int polymer_start_id;
  int polymer_end_id;
  bool fix_polymer;
  bool do_not_sample_reaction_partition_function;
};

double calculate_degree_of_association(int index_of_current_collective_variable,
                                       void *m_wang_landau_system);
double calculate_current_potential_energy_of_system_wrap(
    int unimportant_int, void *unimportant_wang_landau_system);


class ReactionEnsemble {

public:
  ReactionEnsemble();
  ~ReactionEnsemble();

  reaction_system m_current_reaction_system = {
      .nr_single_reactions = 0,
      .reactions = std::vector<single_reaction>(),
      .type_index = std::vector<int>(),
      .nr_different_types = 0,
      .charges_of_types = std::vector<double>(),
      .standard_pressure_in_simulation_units = -10,
      .temperature = -10.0,
      .exclusion_radius = 0.0,
      .volume = -10,
      .box_is_cylindric_around_z_axis = false,
      .cyl_radius = -10,
      .cyl_x = -10,
      .cyl_y = -10,
      .box_has_wall_constraints = false,
      .slab_start_z = -10,
      .slab_end_z = -10,
      .non_interacting_type = 100}; // the standard_pressure_in_simulation_units
                                    // is an input parameter for the reaction
                                    // ensemble;

  int m_accepted_configurational_MC_moves = 0;
  int m_tried_configurational_MC_moves = 0;
  bool m_system_is_in_1_over_t_regime = false;

  void set_cuboid_reaction_ensemble_volume();
  int do_reaction(int reaction_steps);
  void check_reaction_ensemble();
  int calculate_nu_bar(std::vector<int>& reactant_coefficients,
                       std::vector<int>& product_coefficients); // should only be used at when
                                               // defining a new reaction
  int update_type_index(std::vector<int>& reactant_types,
                        std::vector<int>& product_types); // assign different types an
                                                // index in a growing list that
                                                // starts at 0 and is
                                                // incremented by 1 for each new
                                                // type. the entry in the index
                                                // at place i is the
                                                // "type_value". therefore the
                                                // type of type "typevalue" has
                                                // the index i;
                                                bool generic_oneway_reaction(int reaction_id, int reaction_modus);
  int find_index_of_type(int type);
  bool do_global_mc_move_for_particles_of_type(int type, int start_id_polymer,
                                               int end_id_polymer,
                                               int particle_number_of_type,
                                               const bool use_wang_landau);
  int delete_particle(int p_id);
  void add_reaction(double equilibrium_constant,
                    std::vector<int> _reactant_types,
                    std::vector<int> _reactant_coefficients,
                    std::vector<int> _product_types,
                    std::vector<int> _product_coefficients);
  ///////////////////////////////////////////// Wang-Landau algorithm

  wang_landau_system m_current_wang_landau_system = {
      .histogram = std::vector<int>(),
      .wang_landau_potential = std::vector<double>(),
      .collective_variables = std::vector<collective_variable>(),
      .nr_subindices_of_collective_variable=std::vector<int>(),
      .wang_landau_parameter = 1.0,
      .initial_wang_landau_parameter = 1.0,
      .int_fill_value = -10,
      .double_fill_value = -10.0,
      .number_of_monte_carlo_moves_between_check_of_convergence = 5000,
      .final_wang_landau_parameter = 0.00001,
      .used_bins = -10,
      .monte_carlo_trial_moves = 0,
      .wang_landau_steps = 1,
      .output_filename = "",
      .minimum_energies_at_flat_index = std::vector<double>(),
      .maximum_energies_at_flat_index = std::vector<double>(),
      .do_energy_reweighting = false,
      .polymer_start_id = -10,
      .polymer_end_id = -10,
      .fix_polymer = false,
      .do_not_sample_reaction_partition_function = false,
      }; // use negative value as fill value since it cannot occur in
                  // the wang_landau algorithm in the histogram and in the wang
                  // landau potential, use only 1 wang_landau_steps if you want
                  // to record other observables in the tcl script.

  void
  add_new_CV_degree_of_association(int associated_type, double CV_minimum,
                                   double CV_maximum,
                                   std::vector<int> _corresponding_acid_types);
  void add_new_CV_potential_energy(std::string filename, double delta_CV);
  int do_reaction_wang_landau();
  int update_maximum_and_minimum_energies_at_current_state(); // use for
                                                              // preliminary
                                                              // energy
                                                              // reweighting
                                                              // runs
  void write_out_preliminary_energy_run_results(std::string filename);

  // checkpointing, only designed to reassign values of a previous simulation to
  // a new simulation with the same initialization process
  int write_wang_landau_checkpoint(std::string identifier);
  int load_wang_landau_checkpoint(std::string identifier);
  void write_wang_landau_results_to_file(std::string full_path_to_output_filename);

  /////////////////////////////////////////////  Constant-pH Reactions
  int do_reaction_constant_pH();
  double m_constant_pH = -10;

private:
  // declaration of boring helper functions or member variables
  int m_invalid_charge =
      -10000; // this is the default charge which is assigned to a type which
              // occurs in a reaction. this charge has to be overwritten. if it
              // is not overwritten the reaction ensemble will complain.
  enum m_reaction_mode {
    reaction_ensemble_mode,
    reaction_ensemble_wang_landau_mode,
    constant_pH_mode
  };
  double factorial_Ni0_divided_by_factorial_Ni0_plus_nu_i(int Ni0, int nu_i);
  bool all_reactant_particles_exist(int reaction_id);
  int replace(int p_id, int desired_type);
  int create_particle(int desired_type);
  std::vector<int> m_empty_p_ids_smaller_than_max_seen_particle;
  int hide_particle(int p_id, int previous_type);
  void remove_bins_that_have_not_been_sampled();

    /**
    *Calculates the average of an integer array (used for the histogram of the Wang-Landau algorithm). It excludes values which are initialized to be negative. Those values indicate that the Wang-Landau algorithm should not sample those values. The values still occur in the list because we can only store "rectangular" value ranges.
    */

    template <typename T>
    double average_list_of_allowed_entries(T vector){
	    double result=0.0;
	    int counter_allowed_entries=0;
	    for(int i=0;i<vector.size();i++){
		    if(vector[i]>=0){ //checks for validity of index i (think of energy collective variables, in a cubic memory layout there will be indices which are not allowed by the energy boundaries. These values will be initalized with a negative fill value)
			    result+=(double) vector[i];
			    counter_allowed_entries+=1;
		    }
	    }
	    return result/counter_allowed_entries;
    }

    /**
    * Checks wether an integer is in an array of integers.
    */
    template <typename T>
    bool is_in_list(T value, std::vector<T> list){
	    for(int i=0;i<list.size();i++){
		    if(list[i]==value)
			    return true;
	    }
	    return false;
    }
    
  void append_particle_property_of_random_particle(
      int type, std::vector<stored_particle_property> &list_of_particles);
  void make_reaction_attempt(
      single_reaction& current_reaction,
      std::vector<stored_particle_property> &changed_particles_properties,
      std::vector<int> &p_ids_created_particles,
      std::vector<stored_particle_property> &hidden_particles_properties);
  double calculate_factorial_expression(single_reaction& current_reaction,
                                        int *old_particle_numbers);
  void restore_properties(std::vector<stored_particle_property>& property_list,
                          const int number_of_saved_properties);
  int add_types_to_index(std::vector<int>& type_list, int status_gc_init);
  double calculate_boltzmann_factor_reaction_ensemble(
      single_reaction& current_reaction, double E_pot_old, double E_pot_new,
      std::vector<int> &old_particle_numbers);
  void add_random_vector(double *vector, int len_vector,
                         double length_of_displacement);
  int get_flattened_index_wang_landau(
      double *current_state, double *collective_variables_minimum_values,
      double *collective_variables_maximum_values,
      double *delta_collective_variables_values, int nr_collective_variables);
  void get_random_position_in_box(double *out_pos);
  void
  get_random_position_in_box_enhanced_proposal_of_small_radii(double *out_pos);
  
  // declarations wang_landau
  int initialize_wang_landau(); // may first be called after all collective
                                // variables are added
  bool can_refine_wang_landau_one_over_t();
  bool achieved_desired_number_of_refinements_one_over_t();
  void refine_wang_landau_parameter_one_over_t();
  int get_flattened_index_wang_landau_of_current_state();
  void update_wang_landau_potential_and_histogram(
      int index_of_state_after_acceptance_or_rejection);
  double calculate_boltzmann_factor_reaction_ensemble_wang_landau(
      single_reaction& current_reaction, double E_pot_old, double E_pot_new,
      std::vector<int> &old_particle_numbers, int old_state_index,
      int new_state_index, bool only_make_configuration_changing_move);
  int get_flattened_index_wang_landau_without_energy_collective_variable(
      int flattened_index_with_energy_collective_variable,
      int collective_variable_index_energy_observable); // needed for energy
                                                        // collective variable
  double calculate_delta_degree_of_association(collective_variable& current_collective_variable);
  void initialize_histogram();
  void initialize_wang_landau_potential();
  void reset_histogram();
  int m_WL_accepted_moves = 0;
  int m_WL_tries = 0;

  // declarations constant pH
  int get_random_p_id();
  double
  calculate_boltzmann_factor_consant_pH(single_reaction& current_reaction,
                                        double E_pot_old, double E_pot_new);
};
}
