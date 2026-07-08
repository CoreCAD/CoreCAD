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
