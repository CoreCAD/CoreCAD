# SPDX-License-Identifier: LGPL-2.1-or-later

from __future__ import annotations

from Base.Metadata import export

from App.DocumentObject import DocumentObject

@export(Include="Mod/Assembly/App/Joint.h", Namespace="Assembly")
class Joint(DocumentObject):
    """
    A mate constraint between two assembly components.

    Author: Cruth contributors
    License: LGPL-2.1-or-later
    """
