# SPDX-License-Identifier: LGPL-2.1-or-later

from SketcherTests.TestPlacementUpdate import TestSketchPlacementUpdate
from SketcherTests.TestConstraintCommandsGui import TestConstraintCommandsGui
from SketcherTests.TestOnViewParameterGui import TestOnViewParameterGui

# Use the module so that code checkers don't complain (flake8)
(
    True
    if TestSketchPlacementUpdate and TestOnViewParameterGui and TestConstraintCommandsGui
    else False
)
