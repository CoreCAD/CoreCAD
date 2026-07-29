# SPDX-License-Identifier: LGPL-2.1-or-later
# /**************************************************************************
#                                                                           *
#    Copyright (c) 2026 Cruth (Sean Barton)                                 *
#                                                                           *
#    This file is part of FreeCAD.                                          *
#                                                                           *
#    FreeCAD is free software: you can redistribute it and/or modify it     *
#    under the terms of the GNU Lesser General Public License as            *
#    published by the Free Software Foundation, either version 2.1 of the   *
#    License, or (at your option) any later version.                        *
#                                                                           *
#    FreeCAD is distributed in the hope that it will be useful, but         *
#    WITHOUT ANY WARRANTY; without even the implied warranty of             *
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
#    Lesser General Public License for more details.                        *
#                                                                           *
#    You should have received a copy of the GNU Lesser General Public       *
#    License along with FreeCAD. If not, see                                *
#    <https://www.gnu.org/licenses/>.                                       *
#                                                                           *
# **************************************************************************/

# GUI-only Assembly tests. These inspect the built Coin scene graph, which only
# exists with a running GUI, so they are registered from InitGui.py (never under
# the headless FreeCADCmd lane) and run in CI via the xvfb GUI-test lane, which
# discovers test units whose name contains "Gui".

import TestApp

from AssemblyTests.TestStepAssemblyImportDraw import TestStepAssemblyImportDraw

# Use the modules so that code checkers don't complain (flake8)
True if TestStepAssemblyImportDraw else False
