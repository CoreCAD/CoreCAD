# SPDX-License-Identifier: LGPL-2.1-or-later

import _PartDesign

makeFilletArc = _PartDesign.makeFilletArc

# Cruth §8.5/§4.6: pure spawn-vs-extend query (P8 UI/API parity, no side effects).
resolveBaseBody = _PartDesign.resolveBaseBody

# Cruth §4.6/#17: explicit Body creation, separated from the lookup above.
spawnBody = _PartDesign.spawnBody

# Cruth §11: reverse lookup from a feature to the Body whose pipeline emits it.
# Derived on demand (walked along the BaseFeature chain), never stored — not ownership.
findBodyOf = _PartDesign.findBodyOf

# Cruth ownership-query contract: N-valued reverse lookup — every Body a feature backs
# (a multi-output Tip backs one Body per output component). Derived, never stored.
bodiesOf = _PartDesign.bodiesOf

# Cruth ownership-query contract (P7 fail-loud): resolve feature + picked sub-element to
# the single Body meant; raises on a multi-output feature asked with no sub-element.
bodyOf = _PartDesign.bodyOf

# Cruth §8.5 (#27): re-home a feature onto another Body (or None to spawn a fresh one) —
# the model half of the feature-creation "Merge result" control.
moveFeatureToBody = _PartDesign.moveFeatureToBody


def makeFeature(profile, featureType, subs=None, body=None, recompute=True):
    """Create a PartDesign feature the GUI's way: the Body EMERGES from the feature.

    Use this in scripts instead of ``doc.addObject('PartDesign::Body')``. An
    up-front, empty Body is the container-era gesture the marker model rejects
    (Cruth §4.6/§8.5) — it now fails loudly. This is the scriptable equivalent of
    the GUI's prepareProfileBased flow, assembled from the same shared primitives
    (resolveBaseBody + spawnBody + Body.addFeature), so a script and a click
    produce identical structure (P8 API/UI parity).

    profile      the sketch (Part::Part2DObject) the feature is built on.
    featureType  'Pad', 'Pocket', ... or a full 'PartDesign::Pad' type name.
    subs         optional sub-elements of the profile (e.g. ['Face6']); when given
                 the Profile is set as (profile, subs), else the whole profile.
    body         optional Body to extend. Default None: resolve the Body from the
                 profile's anchor chain and spawn one if the chain reaches none
                 (the auto-spawn case). Pass a Body to force-extend it, or for a
                 non-sketch profile the anchor walk cannot resolve.
    recompute    recompute the document before returning (default True).

    Returns the new feature. Requires a CAD (Part-type) document — one with a
    document-level world frame, e.g. ``App.newDocument(type='Part')``; spawnBody
    fails loudly otherwise.
    """
    doc = profile.Document
    ftype = featureType if "::" in featureType else "PartDesign::" + featureType

    # Resolve the base Body (pure query) and spawn one if the anchor chain reaches
    # none — exactly the GUI's spawn-vs-extend decision, side-effect-free lookup first.
    if body is None:
        body = resolveBaseBody(profile)
        if body is None:
            body = spawnBody(doc)

    # Birth the feature at document level, then splice it into the Body's pipeline
    # (Tip + BaseFeature chain — a pipeline edit, not a container add).
    feat = doc.addObject(ftype, doc.getUniqueObjectName(ftype.split("::")[-1]))
    body.addFeature(feat)

    # Point the feature at its profile, whole or sub-region.
    if hasattr(feat, "Profile"):
        feat.Profile = (profile, list(subs)) if subs else profile

    if recompute:
        doc.recompute()
    return feat
