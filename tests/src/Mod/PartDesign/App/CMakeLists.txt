# SPDX-License-Identifier: LGPL-2.1-or-later

add_executable(PartDesign_tests_run
        BackwardCompatibility.cpp
        BodyShapeCapability.cpp
        DatumPlane.cpp
        FeatureDressUp.cpp
        ShapeBinder.cpp
        Pad.cpp
        PadRecipe.cpp
        MoveFeatureToBody.cpp
        AddFeature.cpp
        FindInterferingPairs.cpp
        ReconcileMultiOutput.cpp
        SpawnScopeSiblings.cpp
        ToolReaches.cpp
        WorldFrame.cpp
)

set(PartDesignTestData_Files
        TestModels/ModelFromV021.FCStd
        TestModels/TwoLengthsPadWithExpression.FCStd
        TestModels/TwoLengthsPadCyclicExpr.FCStd
        TestModels/TwoLengthsPadCrossObjectRef.FCStd
)

ADD_CUSTOM_TARGET(PartDesignTestData ALL
        SOURCES ${PartDesignTestData_Files}
)

fc_target_copy_resource(PartDesignTestData
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_BINARY_DIR}/tests
        ${PartDesignTestData_Files}
)
