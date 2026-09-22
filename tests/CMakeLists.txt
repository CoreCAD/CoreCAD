# SPDX-License-Identifier: LGPL-2.1-or-later

if(FREECAD_USE_EXTERNAL_GTEST)
    find_package(GTest REQUIRED)
    set(Google_Tests_LIBS ${GTEST_LIBRARIES} ${GMOCK_LIBRARIES})
else()
    if ( EXISTS "${CMAKE_SOURCE_DIR}/tests/lib/googletest" )
        include_directories( ${CMAKE_SOURCE_DIR}/tests/lib/googletest/include/ )
        include_directories( ${CMAKE_SOURCE_DIR}/tests/lib/googlemock/include/ )
    else()
        message( SEND_ERROR "The Google Test submodule is not available. Please run git submodule update --init" )
    endif()
endif()

if(MSVC)
    add_compile_options(/wd4251)

    option(
        gtest_force_shared_crt
        "Use shared (DLL) run-time lib even when Google Test is built as static lib."
        ON)
    option(gtest_disable_pthreads "Disable uses of pthreads in gtest." ON)

    set(Google_Tests_LIBS
        oldnames.lib
        debug msvcrtd.lib
        debug msvcprtd.lib
        optimized msvcrt.lib
        optimized msvcprt.lib
    )

    # Universal C runtime introduced in VS 2015 (cl version 19)
    if(NOT(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "19"))
        list(APPEND Google_Tests_LIBS
            debug vcruntimed.lib
            debug ucrtd.lib
            debug concrtd.lib
            optimized vcruntime.lib
            optimized ucrt.lib
            optimized concrt.lib
        )
    endif()
endif()

if(WIN32)
    add_definitions(-DCOIN_DLL)
endif(WIN32)

if(NOT BUILD_DYNAMIC_LINK_PYTHON)
    list(APPEND Google_Tests_LIBS
        ${Python3_LIBRARIES}
    )
endif()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
)

set(CMAKE_AUTOMOC ON)

function(setup_qt_test)
    foreach(_testname ${ARGN})
        add_executable(${_testname}_Tests_run ${_testname}.cpp)
        add_test(NAME ${_testname}_Tests_run COMMAND ${_testname}_Tests_run)

        if(NOT BUILD_DYNAMIC_LINK_PYTHON)
            list(APPEND ${_testname}_LIBS
                ${Python3_LIBRARIES}
            )
        endif()

        target_include_directories(${_testname}_Tests_run PUBLIC
            SYSTEM
            ${QtTest_INCLUDE_DIRS})
        target_link_libraries(${_testname}_Tests_run
            FreeCADApp
            FreeCADGui
            ${QtTest_LIBRARIES}
            ${${_testname}_LIBS})

        if(WIN32)
            set_target_properties(${_testname}_Tests_run PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
            set_tests_properties(${_testname}_Tests_run PROPERTIES
                LABELS "Qt"
                ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                ENVIRONMENT_MODIFICATION "${_test_env_mod}")
        else()
            set_target_properties(${_testname}_Tests_run PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
            set_tests_properties(${_testname}_Tests_run PROPERTIES
                LABELS "Qt"
                ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        endif()
    endforeach()
endfunction()

set(TestExecutables
    App_tests_run
    Base_tests_run
    Misc_tests_run
    Zipios_tests_run
)

# NOTE: The following tests don't yet run on Windows because they can't find the *.pyd files needed by each FreeCAD
# module.

if(BUILD_GUI)
    list (APPEND TestExecutables Gui_tests_run)
endif()
if(BUILD_ASSEMBLY)
    list (APPEND TestExecutables Assembly_tests_run)
endif(BUILD_ASSEMBLY)
if(BUILD_IMPORT)
    list (APPEND TestExecutables Import_tests_run)
endif(BUILD_IMPORT)
if(BUILD_MATERIAL)
    list (APPEND TestExecutables Material_tests_run)
endif(BUILD_MATERIAL)
if(BUILD_MEASURE)
    list (APPEND TestExecutables Measure_tests_run)
endif(BUILD_MEASURE)
if(BUILD_MESH)
    list (APPEND TestExecutables Mesh_tests_run)
endif(BUILD_MESH)
if(BUILD_MESH_PART)
    list (APPEND TestExecutables MeshPart_tests_run)
endif(BUILD_MESH_PART)
if(BUILD_PART)
    list (APPEND TestExecutables Part_tests_run)
endif(BUILD_PART)
if(BUILD_PART_DESIGN)
    list (APPEND TestExecutables PartDesign_tests_run)
endif(BUILD_PART_DESIGN)
if(BUILD_POINTS)
    list (APPEND TestExecutables Points_tests_run)
endif(BUILD_POINTS)
if(BUILD_SKETCHER)
    list (APPEND TestExecutables Sketcher_tests_run)
endif(BUILD_SKETCHER)
if(BUILD_SPREADSHEET)
    list (APPEND TestExecutables Spreadsheet_tests_run)
endif()
if(BUILD_START)
    list (APPEND TestExecutables Start_tests_run)
endif()
if(BUILD_TECHDRAW)
    list (APPEND TestExecutables TechDraw_tests_run)
endif(BUILD_TECHDRAW)

# -------------------------

# On Windows, DLLs must be on PATH for test executables to load them.
# Third-party DLLs live in conda/pixi Library/bin, FreeCAD core DLLs in bin/,
# and module DLLs (.pyd) in Mod/*/ subdirectories.
if(WIN32)
    set(_test_env_mod "PATH=path_list_prepend:${CMAKE_BINARY_DIR}/bin")
    cmake_path(GET CMAKE_PREFIX_PATH PARENT_PATH _conda_prefix)
    if(EXISTS "${_conda_prefix}/Library/bin")
        list(APPEND _test_env_mod "PATH=path_list_prepend:${_conda_prefix}/Library/bin")
    elseif(EXISTS "${CMAKE_PREFIX_PATH}/bin")
        list(APPEND _test_env_mod "PATH=path_list_prepend:${CMAKE_PREFIX_PATH}/bin")
    endif()
    file(GLOB _mod_dirs LIST_DIRECTORIES true "${CMAKE_BINARY_DIR}/Mod/*")
    foreach(_mod_dir ${_mod_dirs})
        if(IS_DIRECTORY "${_mod_dir}")
            list(APPEND _test_env_mod "PATH=path_list_prepend:${_mod_dir}")
        endif()
    endforeach()
endif()

if ( NOT FREECAD_USE_EXTERNAL_GTEST )
    if(WIN32)
        set(BUILD_SHARED_LIBS OFF)
    endif()
    add_subdirectory(lib)
endif()
add_subdirectory(src)

include(GoogleTest)
set(CMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE PRE_TEST)

foreach (exe ${TestExecutables})
    if(WIN32)
        if(CMAKE_CONFIGURATION_TYPES)
            set(OUTPUT_DIR ${CMAKE_BINARY_DIR}/bin)
            foreach(OUTPUT_CONFIG Debug Release RelWithDebInfo MinSizeRel)
                string(TOUPPER "${OUTPUT_CONFIG}" UPPER_CONFIG)
                set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${UPPER_CONFIG} ${OUTPUT_DIR}/${OUTPUT_CONFIG})
            endforeach()
        else()
            set_target_properties(${exe} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
            set_target_properties(${exe} PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
        endif()
        # gtest_discover_tests requires running the exe to list tests, which fails on Windows
        # because third-party DLLs are not on PATH during CMake script execution. Register each
        # test executable as a single ctest entry instead.
        add_test(NAME ${exe} COMMAND ${exe})
        set_tests_properties(${exe} PROPERTIES
            ENVIRONMENT_MODIFICATION "${_test_env_mod}")
    else()
        set_target_properties(${exe} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
        gtest_discover_tests(${exe})
    endif()
endforeach()

# -------------------------------------------------------------------------------------------
# The Python test suites
# -------------------------------------------------------------------------------------------
#
# These are the modules under src/Mod that register themselves in FreeCAD.__unit_test__ and run
# through `FreeCADCmd -t <name>`. Until now nothing here ran them: ctest knew only about the C++
# tests, so a Python suite could fail for weeks without anything saying so -- which is how a
# document that saved and would not reopen sat unnoticed.
#
# One ctest entry per suite, never one for all of them, because `-t 0` stops at the first suite
# that aborts and takes the rest of the run with it: a single entry would report one failure and
# hide however many suites came after it.
#
# PYTHON_TEST_SUITES is the list ctest runs. It is written by hand and compared with the list the
# program actually registers by the PythonSuites entry below, so the two cannot drift apart
# quietly.
set(PYTHON_TEST_SUITES
    BaseTests
    Document
    Metadata
    MeshTestsApp
    StringHasher
    TestAssemblyWorkbench
    TestCoinNodeSnapshots
    TestDraft
    TestImport
    TestMaterialsApp
    TestPartApp
    TestPartDesignApp
    TestPythonSyntax
    TestSketcherApp
    TestSpreadsheet
    TestSurfaceApp
    TestTechDrawApp
    UnicodeTests
    UnitTests
)

# Suites that do not pass today. They are registered here rather than left out, so that every run
# of ctest lists them as not run instead of saying nothing at all, and so the day one of them is
# fixed it is one line that leaves this list. The list is empty: every registered suite passes.
set(PYTHON_TEST_SUITES_FAILING
)

foreach(_suite ${PYTHON_TEST_SUITES})
    add_test(NAME Python_${_suite} COMMAND $<TARGET_FILE:FreeCADMainCmd> -t ${_suite})
    if(WIN32)
        set_tests_properties(Python_${_suite} PROPERTIES
            ENVIRONMENT_MODIFICATION "${_test_env_mod}")
    endif()
    if(${_suite} IN_LIST PYTHON_TEST_SUITES_FAILING)
        set_tests_properties(Python_${_suite} PROPERTIES DISABLED TRUE)
    endif()
endforeach()

string(REPLACE ";" "\;" _python_suites_for_env "${PYTHON_TEST_SUITES}")
add_test(NAME Python_SuiteRegistry
         COMMAND $<TARGET_FILE:FreeCADMainCmd> ${CMAKE_CURRENT_SOURCE_DIR}/PythonSuites.py)
set_tests_properties(Python_SuiteRegistry PROPERTIES
    ENVIRONMENT "CRUTH_PYTHON_TEST_SUITES=${_python_suites_for_env}")
if(WIN32)
    set_tests_properties(Python_SuiteRegistry PROPERTIES
        ENVIRONMENT_MODIFICATION "${_test_env_mod}")
endif()
