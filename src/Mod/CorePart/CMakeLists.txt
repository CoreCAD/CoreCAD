# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: Copyright (C) 2026 CoreCAD Contributors
#
# CorePart — pure-Python combined Part + PartDesign workbench.
# No C++ code; only InitGui.py needs to be installed / copied to the build tree.

if(BUILD_GUI)
    set(CorePart_Scripts
        InitGui.py
    )

    INSTALL(
        FILES ${CorePart_Scripts}
        DESTINATION Mod/CorePart
    )

    ADD_CUSTOM_TARGET(CorePartScripts ALL
        SOURCES ${CorePart_Scripts}
    )

    fc_copy_sources(CorePartScripts "${CMAKE_BINARY_DIR}/Mod/CorePart" ${CorePart_Scripts})
endif(BUILD_GUI)
