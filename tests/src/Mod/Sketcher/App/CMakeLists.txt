# SPDX-License-Identifier: LGPL-2.1-or-later

add_executable(Sketcher_tests_run
        Constraint.cpp
        EntityIdentity.cpp
        SketcherTestHelpers.cpp
        SketchObject.cpp
        SketchObjectChanges.cpp
        SketchObjectSymmetric.cpp
        SketchRecipe.cpp
)

add_subdirectory(planegcs)
