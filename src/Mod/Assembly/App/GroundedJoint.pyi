# SPDX-License-Identifier: LGPL-2.1-or-later

from __future__ import annotations

from Base.Metadata import export

from App.DocumentObject import DocumentObject

@export(Include="Mod/Assembly/App/GroundedJoint.h", Namespace="Assembly")
class GroundedJoint(DocumentObject):
    """
    A grounded joint that pins one assembly component in place.

    Author: Cruth contributors
    License: LGPL-2.1-or-later
    """
