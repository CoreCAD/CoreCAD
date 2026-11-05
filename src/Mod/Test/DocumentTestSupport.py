# ***************************************************************************
# *   Copyright (c) 2003 Juergen Riegel <juergen.riegel@web.de>             *
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


def _placedGroup(doc, name="Group"):
    """A container with its own placement that owns what it holds (a geo-feature group).

    App::Part used to be the stock example; the type is retired, so the tests that exercise
    geo-feature-group behaviour build the same thing from the generic extensions."""
    grp = doc.addObject("App::GeometryPython", name)
    grp.addExtension("App::GeoFeatureGroupExtensionPython")
    grp.addExtension("App::PlacementExtensionPython")
    return grp


class Proxy:
    def __init__(self, obj):
        self.Dictionary = {}
        self.obj = obj
        obj.Proxy = self

    def dumps(self):
        return self.Dictionary

    def loads(self, data):
        self.Dictionary = data


class MyFeature:
    def __init__(self, obj):
        obj.Proxy = self
        obj.addProperty("App::PropertyLinkList", "propLink")

    def onDocumentRestored(self, obj):
        if hasattr(obj, "propLink"):
            obj.removeProperty("propLink")
