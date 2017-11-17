/*
  Copyright (C) 2010,2011,2012,2013,2014 The ESPResSo project
  Copyright (C) 2002,2003,2004,2005,2006,2007,2008,2009,2010
  Max-Planck-Institute for Polymer Research, Theory Group

  This file is part of ESPResSo.

  ESPResSo is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  ESPResSo is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef SCRIPT_INTERFACE_OBSERVABLES_PARAMLESSOBSERVABLE_HPP
#define SCRIPT_INTERFACE_OBSERVABLES_PARAMLESSOBSERVABLE_HPP

#include "ScriptInterface.hpp"

#include <memory>

#include "Observable.hpp"
#include "core/observables/Observable.hpp"
#include "core/observables/StressTensor.hpp"
#include "core/observables/StressTensorAcf.hpp"

namespace ScriptInterface {
namespace Observables {

#define NEW_PARAMLESS_OBSERVABLE(obs_name)                                     \
  class obs_name : public Observable {                                         \
  public:                                                                      \
    obs_name() : m_observable(new ::Observables::obs_name()){};                \
                                                                               \
    const std::string name() const override {                                  \
      return "Observables::" #obs_name;                                        \
    }                                                                          \
                                                                               \
    std::shared_ptr<::Observables::Observable> observable() const override {   \
      return m_observable;                                                     \
    };                                                                         \
                                                                               \
  private:                                                                     \
    std::shared_ptr<::Observables::obs_name> m_observable;                     \
  };

NEW_PARAMLESS_OBSERVABLE(StressTensor)
NEW_PARAMLESS_OBSERVABLE(StressTensorAcf)

} /* namespace Observables */
} /* namespace ScriptInterface */

#endif
