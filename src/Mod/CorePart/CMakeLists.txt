# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: Copyright (C) 2026 CoreCAD Contributors
#
# CorePart — pure-Python combined Part + PartDesign workbench.
# No C++ code; all Python is copied to the build tree via a stamp-file pattern
# (same approach as CoreRibbon).

if(NOT BUILD_GUI)
    return()
endif()

# CorePart icons are compiled into FreeCADGui via src/Gui/CMakeLists.txt.
# No separate library or import needed here.

set(_src "${CMAKE_CURRENT_SOURCE_DIR}")
set(_dst "${CMAKE_BINARY_DIR}/Mod/CorePart")

# ── Open-source files ─────────────────────────────────────────────────────────

set(_open_root_sources
    "${_src}/InitGui.py"
)

set(_open_render_sources
    "${_src}/render/__init__.py"
    "${_src}/render/interface.py"
    "${_src}/render/coin_backend.py"
    "${_src}/render/registry.py"
)

set(_open_stamp "${_dst}/.copy_stamp")

add_custom_command(
    OUTPUT "${_open_stamp}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_dst}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_dst}/render"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${_open_root_sources} "${_dst}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${_open_render_sources} "${_dst}/render"
    COMMAND ${CMAKE_COMMAND} -E touch "${_open_stamp}"
    DEPENDS ${_open_root_sources} ${_open_render_sources}
    COMMENT "Syncing CorePart open-source files to build directory"
)

add_custom_target(CorePartScripts ALL
    DEPENDS "${_open_stamp}"
    SOURCES ${_open_root_sources} ${_open_render_sources}
)

install(FILES ${_open_root_sources}   DESTINATION Mod/CorePart)
install(FILES ${_open_render_sources} DESTINATION Mod/CorePart/render)

# ── Proprietary extensions (optional — requires corecad-part repo) ────────────

if(DEFINED CORECAD_COREPART_DIR AND EXISTS "${CORECAD_COREPART_DIR}")
    message(STATUS "CorePart: proprietary extensions found at ${CORECAD_COREPART_DIR}")

    set(_pro "${CORECAD_COREPART_DIR}")

    set(_pro_root_sources
        "${_pro}/_pro_init.py"
    )
    set(_pro_feature_sources
        "${_pro}/features/__init__.py"
        "${_pro}/features/section_view.py"
        "${_pro}/features/geometry_metrics.py"
    )
    set(_pro_render_ext_sources
        "${_pro}/render_ext/__init__.py"
        "${_pro}/render_ext/wgpu_backend.py"
    )

    set(_pro_stamp "${_dst}/.pro_copy_stamp")

    add_custom_command(
        OUTPUT "${_pro_stamp}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_dst}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_dst}/features"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_dst}/render_ext"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${_pro_root_sources} "${_dst}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${_pro_feature_sources} "${_dst}/features"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${_pro_render_ext_sources} "${_dst}/render_ext"
        COMMAND ${CMAKE_COMMAND} -E touch "${_pro_stamp}"
        DEPENDS ${_pro_root_sources} ${_pro_feature_sources} ${_pro_render_ext_sources}
        COMMENT "Syncing CorePart proprietary extensions to build directory"
    )

    add_custom_target(CorePartProScripts ALL
        DEPENDS "${_pro_stamp}"
        SOURCES ${_pro_root_sources} ${_pro_feature_sources} ${_pro_render_ext_sources}
    )

    install(FILES ${_pro_root_sources}        DESTINATION Mod/CorePart)
    install(FILES ${_pro_feature_sources}     DESTINATION Mod/CorePart/features)
    install(FILES ${_pro_render_ext_sources}  DESTINATION Mod/CorePart/render_ext)
else()
    message(STATUS "CorePart: proprietary extensions not found (set CORECAD_COREPART_DIR to enable)")
endif()
