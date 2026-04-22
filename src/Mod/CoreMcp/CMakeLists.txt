# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: Copyright (C) 2026 CoreCAD Contributors
#
# CoreMcp — Model Context Protocol addon for CoreCAD.
#
# Source lives in a separate repo (corecad-mcp, fork of neka-nat/freecad-mcp),
# detected at configure time via CORECAD_COREMCP_DIR.  This CMakeLists.txt
# only contains the build/install rules; all Python source is external.
#
# The addon deploys under its upstream name (FreeCADMCP) so Python imports
# and workbench registration match the upstream project.  Only the build
# coordinator is CoreCAD-branded.
#
# If CORECAD_COREMCP_DIR is not set or the addon source is missing, this
# target is a no-op.

if(NOT BUILD_GUI)
    return()
endif()

if(NOT CORECAD_COREMCP_DIR OR NOT EXISTS "${CORECAD_COREMCP_DIR}/InitGui.py")
    message(STATUS "CoreMcp: source not found — skipping (MCP bridge will not be available)")
    return()
endif()

message(STATUS "CoreMcp: using source from ${CORECAD_COREMCP_DIR}")

set(_src "${CORECAD_COREMCP_DIR}")
set(_dst "${CMAKE_BINARY_DIR}/Mod/FreeCADMCP")

set(CoreMcp_RootFiles
    Init.py
    InitGui.py
)

set(CoreMcp_RpcFiles
    rpc_server/__init__.py
    rpc_server/parts_library.py
    rpc_server/rpc_server.py
    rpc_server/serialize.py
)

set(_root_sources "")
foreach(_f IN LISTS CoreMcp_RootFiles)
    list(APPEND _root_sources "${_src}/${_f}")
endforeach()

set(_rpc_sources "")
foreach(_f IN LISTS CoreMcp_RpcFiles)
    list(APPEND _rpc_sources "${_src}/${_f}")
endforeach()

set(_stamp "${_dst}/.copy_stamp")

add_custom_command(
    OUTPUT "${_stamp}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_dst}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_dst}/rpc_server"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${_root_sources}
        "${_dst}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${_rpc_sources}
        "${_dst}/rpc_server"
    COMMAND ${CMAKE_COMMAND} -E touch "${_stamp}"
    DEPENDS ${_root_sources} ${_rpc_sources}
    COMMENT "Syncing CoreMcp (FreeCADMCP addon) to build directory"
)

add_custom_target(CoreMcpSync ALL
    DEPENDS "${_stamp}"
)

install(FILES ${_root_sources} DESTINATION Mod/FreeCADMCP)
install(FILES ${_rpc_sources}  DESTINATION Mod/FreeCADMCP/rpc_server)
