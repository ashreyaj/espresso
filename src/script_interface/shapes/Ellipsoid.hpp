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

#ifndef SCRIPT_INTERFACE_SHAPES_ELLIPSOID_HPP
#define SCRIPT_INTERFACE_SHAPES_ELLIPSOID_HPP

#include "Shape.hpp"
#include "core/shapes/Ellipsoid.hpp"

namespace ScriptInterface {
namespace Shapes {

class Ellipsoid : public Shape {
public:
  Ellipsoid() : m_ellipsoid(new ::Shapes::Ellipsoid()) {
    add_parameters({{"center", m_ellipsoid->pos()},
                    {"a", m_ellipsoid->semiaxis_a()},
                    {"b", m_ellipsoid->semiaxis_b()},
                    {"c", m_ellipsoid->semiaxis_c()},
                    {"direction", m_ellipsoid->direction()}});
  }

  const std::string name() const override { return "Shapes::Ellipsoid"; }

  std::shared_ptr<::Shapes::Shape> shape() const override { return m_ellipsoid; }

private:
  std::shared_ptr<::Shapes::Ellipsoid> m_ellipsoid;
};

} /* namespace Shapes */
} /* namespace ScriptInterface */

#endif
