# ***************************************************************************
# *   Copyright (c) 2018 looooo <sppedflyer@gmail.com>                      *
# *                                                                         *
# *   This file is part of the FreeCAD CAx development system.              *
# *                                                                         *
# *   This program is free software; you can redistribute it and/or modify  *
# *   it under the terms of the GNU Lesser General Public License (LGPL)    *
# *   as published by the Free Software Foundation; either version 2 of     *
# *   the License, or (at your option) any later version.                   *
# *   for detail see the LICENCE text file.                                 *
# *                                                                         *
# *   FreeCAD is distributed in the hope that it will be useful,            *
# *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
# *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
# *   GNU Library General Public License for more details.                  *
# *                                                                         *
# *   You should have received a copy of the GNU Library General Public     *
# *   License along with FreeCAD; if not, write to the Free Software        *
# *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  *
# *   USA                                                                   *
# *                                                                         *
# ***************************************************************************/

import os
import ast
import unittest
import FreeCAD as App


def test_python_syntax(rootdir, whitelist=None):
    whitelist = whitelist or []
    log = []
    for sub_dir, dirs, files in os.walk(rootdir):
        for fn in files:
            kargs = {}
            kargs["encoding"] = "utf-8"
            if (not fn in whitelist) and os.path.splitext(fn)[1] == ".py":
                path = os.path.join(sub_dir, fn)
                try:
                    with open(path, **kargs) as py_file:
                        source = py_file.read()
                except OSError as err:
                    # The tree lists a file that cannot be read. A build tree links its scripts
                    # back to the source tree, so a script deleted from the source leaves a link
                    # that is still listed and no longer leads anywhere. Reported by name with
                    # everything else, rather than ending the walk with a traceback from the
                    # middle of it and saying nothing about the files after it.
                    log.append("{} could not be read: {}".format(path, err))
                    continue
                try:
                    ast.parse(source)
                except SyntaxError as err:
                    log.append(str(err).replace("<unknown>", path))
    message = "\n\n" + "#" * 30 + "\n"
    message += "{} python files could not be read or parsed:\n\n".format(len(log))
    for i, m in enumerate(log):
        message += str(i + 1) + " " + m + "\n"
    if log:
        raise RuntimeError(
            "there are some files that cannot be read or parsed with the used "
            "python-interpreter" + message
        )
    else:
        return


class PythonSyntaxTestCase(unittest.TestCase):
    """
    Test Case to test python syntax of all python files in FreeCAD
    """

    def setUp(self):
        self.whitelist = []
        self.whitelist += [
            "ap203_configuration_controlled_3d_design_of_mechanical_parts_and_assemblies_mim_lf.py"
        ]
        self.whitelist += ["automotive_design.py"]
        self.whitelist += ["ifc2x3.py"]
        self.whitelist += ["ifc4.py"]

    def testAll(self):
        mod_dir = os.path.join(App.getHomePath(), "Mod")
        test_python_syntax(mod_dir, self.whitelist)
