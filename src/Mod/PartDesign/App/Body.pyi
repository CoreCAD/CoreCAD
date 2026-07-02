# SPDX-License-Identifier: LGPL-2.1-or-later

from __future__ import annotations

from Base.Metadata import export
from Part.BodyBase import BodyBase
from typing import Final

@export(
    Include="Mod/PartDesign/App/Body.h",
    FatherInclude="Mod/Part/App/BodyBasePy.h",
)
class Body(BodyBase):
    """
    PartDesign body class

    Author: Juergen Riegel (FreeCAD@juergen-riegel.net)
    Licence: LGPL
    """

    VisibleFeature: Final[object] = ...
    """Return the visible feature of this body"""

    def insertObject(self, feature: object, target: object, after: bool = False, /) -> None:
        """
        Insert the feature into the body after the given feature.

        @param feature  The feature to insert into the body
        @param target   The feature relative which one should be inserted the given.
          If target is NULL than insert into the end if where is InsertBefore
          and into the begin if where is InsertAfter.
        @param after    if true insert the feature after the target. Default is false.

        @note the method doesn't modify the Tip unlike addFeature()
        """
        ...

    def addFeature(self, feature: object, /) -> object:
        """
        Splice an already-created feature into this Body's pipeline (BaseFeature chain + Tip),
        advancing the Tip.

        Cruth §11 step 5e: a pipeline edit, not a container add — the feature must already
        exist in the document; the Body does not create or own it. Replaces the retired
        GroupExtension addObject(). Returns the added feature.
        """
        ...

    def removeFeature(self, feature: object, /) -> None:
        """
        Remove a feature from this Body's pipeline, rewiring the BaseFeature chain and
        retreating the Tip. The feature is not destroyed. Must be called before the feature
        is removed from the document. Replaces the retired GroupExtension removeObject().
        """
        ...

    def addFeatures(self, features: object, /) -> None:
        """
        Splice a list of already-created features into this Body's pipeline (Cruth §11 step 5e).
        Convenience over addFeature; used when re-homing features between bodies.
        """
        ...

    def removeFeatures(self, features: object, /) -> None:
        """
        Remove a list of features from this Body's pipeline (Cruth §11 step 5e). Convenience
        over removeFeature; used when re-homing features between bodies.
        """
        ...

    def breakOutInstance(self, /) -> object:
        """
        Cruth §5.6: break this pattern instance out into its own independent,
        frozen Body. Captures this Body's solid from the pattern output, re-homes
        it into a new Body backed by a BakedShape feature, and skips the instance
        on the pattern. The result is fully severed from the pattern.

        Must be called on a Body whose Tip is a multi-output pattern feature.

        @return the new frozen Body, or None on failure.
        """
        ...
