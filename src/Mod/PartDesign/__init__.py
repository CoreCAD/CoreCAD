# SPDX-License-Identifier: LGPL-2.1-or-later

import _PartDesign

makeFilletArc = _PartDesign.makeFilletArc

# Cruth §8.5/§4.6: shared auto-spawn entry point (P8 UI/API parity).
resolveBaseBody = _PartDesign.resolveBaseBody

# Cruth §11: reverse lookup from a feature to the Body whose pipeline emits it.
# Derived on demand (walked along the BaseFeature chain), never stored — not ownership.
findBodyOf = _PartDesign.findBodyOf
