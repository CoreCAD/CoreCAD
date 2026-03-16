# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: Copyright (C) 2026 CoreCAD Contributors
#
# CorePart — pure-Python combined Part + PartDesign workbench.
# No C++ code; only InitGui.py needs to be installed / copied to the build tree.

if(BUILD_GUI)
    set(CorePart_Scripts
        InitGui.py
    )

    set(CorePart_Render_Scripts
        render/__init__.py
        render/interface.py
        render/coin_backend.py
        render/registry.py
    )

    INSTALL(
        FILES ${CorePart_Scripts}
        DESTINATION Mod/CorePart
    )

    INSTALL(
        FILES ${CorePart_Render_Scripts}
        DESTINATION Mod/CorePart/render
    )

    ADD_CUSTOM_TARGET(CorePartScripts ALL
        SOURCES ${CorePart_Scripts} ${CorePart_Render_Scripts}
    )

    fc_copy_sources(CorePartScripts "${CMAKE_BINARY_DIR}/Mod/CorePart" ${CorePart_Scripts})
    fc_copy_sources(CorePartScripts "${CMAKE_BINARY_DIR}/Mod/CorePart/render" ${CorePart_Render_Scripts})

    # ----------------------------------------------------------------
    # Proprietary extensions (optional — requires corecad-part repo)
    # ----------------------------------------------------------------
    if(DEFINED CORECAD_COREPART_DIR AND EXISTS "${CORECAD_COREPART_DIR}")
        message(STATUS "CorePart: proprietary extensions found at ${CORECAD_COREPART_DIR}")

        set(CorePart_Pro_Scripts
            ${CORECAD_COREPART_DIR}/_pro_init.py
        )
        set(CorePart_Pro_Features
            ${CORECAD_COREPART_DIR}/features/__init__.py
            ${CORECAD_COREPART_DIR}/features/section_view.py
            ${CORECAD_COREPART_DIR}/features/geometry_metrics.py
        )
        set(CorePart_Pro_RenderExt
            ${CORECAD_COREPART_DIR}/render_ext/__init__.py
            ${CORECAD_COREPART_DIR}/render_ext/wgpu_backend.py
        )

        INSTALL(FILES ${CorePart_Pro_Scripts}   DESTINATION Mod/CorePart)
        INSTALL(FILES ${CorePart_Pro_Features}  DESTINATION Mod/CorePart/features)
        INSTALL(FILES ${CorePart_Pro_RenderExt} DESTINATION Mod/CorePart/render_ext)

        fc_copy_sources(CorePartScripts "${CMAKE_BINARY_DIR}/Mod/CorePart"
            ${CorePart_Pro_Scripts})
        fc_copy_sources(CorePartScripts "${CMAKE_BINARY_DIR}/Mod/CorePart/features"
            ${CorePart_Pro_Features})
        fc_copy_sources(CorePartScripts "${CMAKE_BINARY_DIR}/Mod/CorePart/render_ext"
            ${CorePart_Pro_RenderExt})
    else()
        message(STATUS "CorePart: proprietary extensions not found (set CORECAD_COREPART_DIR to enable)")
    endif()
endif(BUILD_GUI)
