# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: Copyright (C) 2026 CoreCAD Contributors
#
# CoreRibbon — clean ribbon Mod for CoreCAD.
#
# Source lives in a separate private repo (corecad-ribbon), detected at
# configure time via CORECAD_CORERIBBON_DIR.  This CMakeLists.txt only
# contains the build/install rules; all Python source is external.
#
# If CORECAD_CORERIBBON_DIR is not set, this target is a no-op.

if(NOT BUILD_GUI)
    return()
endif()

if(NOT CORECAD_CORERIBBON_DIR OR NOT EXISTS "${CORECAD_CORERIBBON_DIR}/InitGui.py")
    message(STATUS "CoreRibbon: source not found — skipping (standard toolbar/menu UI will be used)")
    return()
endif()

message(STATUS "CoreRibbon: using source from ${CORECAD_CORERIBBON_DIR}")

set(_src "${CORECAD_CORERIBBON_DIR}")
set(_dst "${CMAKE_BINARY_DIR}/Mod/CoreRibbon")

# Root-level Python files
set(CoreRibbon_RootFiles
    __init__.py
    Init.py
    InitGui.py
)

# core/ subdirectory
set(CoreRibbon_CoreFiles
    core/__init__.py
    core/app_menu.py
    core/command_map.py
    core/config.py
    core/constants.py
    core/controller.py
)

# widgets/ subdirectory
set(CoreRibbon_WidgetFiles
    widgets/__init__.py
    widgets/bar.py
    widgets/panel.py
)

# Absolute paths for dependency tracking
set(_root_sources "")
foreach(_f IN LISTS CoreRibbon_RootFiles)
    list(APPEND _root_sources "${_src}/${_f}")
endforeach()

set(_core_sources "")
foreach(_f IN LISTS CoreRibbon_CoreFiles)
    list(APPEND _core_sources "${_src}/${_f}")
endforeach()

set(_widget_sources "")
foreach(_f IN LISTS CoreRibbon_WidgetFiles)
    list(APPEND _widget_sources "${_src}/${_f}")
endforeach()

set(_qss "${_src}/styles/ribbon.qss")

# Stamp file triggers the copy command when any source changes
set(_stamp "${_dst}/.copy_stamp")

add_custom_command(
    OUTPUT "${_stamp}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_dst}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_dst}/core"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_dst}/widgets"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_dst}/styles"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${_root_sources}
        "${_dst}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${_core_sources}
        "${_dst}/core"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${_widget_sources}
        "${_dst}/widgets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${_qss}"
        "${_dst}/styles/ribbon.qss"
    COMMAND ${CMAKE_COMMAND} -E touch "${_stamp}"
    DEPENDS ${_root_sources} ${_core_sources} ${_widget_sources} "${_qss}"
    COMMENT "Syncing CoreRibbon to build directory"
)

add_custom_target(CoreRibbonSync ALL
    DEPENDS "${_stamp}"
)

# Install rules (for packaged builds)
install(FILES ${_root_sources}   DESTINATION Mod/CoreRibbon)
install(FILES ${_core_sources}   DESTINATION Mod/CoreRibbon/core)
install(FILES ${_widget_sources} DESTINATION Mod/CoreRibbon/widgets)
install(FILES "${_qss}"          DESTINATION Mod/CoreRibbon/styles)
