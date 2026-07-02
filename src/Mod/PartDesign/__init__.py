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
