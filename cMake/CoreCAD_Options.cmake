# CoreCAD_Options.cmake
# CoreCAD-specific build option overrides.
# This file is included after InitializeFreeCADBuildOptions() so these settings
# take precedence over upstream defaults. Add/remove workbenches here to control
# what is compiled into CoreCAD.

# ── Branding icon overlay (build-tree only) ───────────────────────────────────
include(cMake/CoreCAD_Branding.cmake)

# Stage Windows installer icons into the build tree.
# The real icons come from corecad-assets; source tree keeps defaults.
corecad_stage_assets(
    SRC_DIR       "${CMAKE_SOURCE_DIR}/package/WindowsInstaller/icons"
    ASSETS_SUBDIR "package/WindowsInstaller/icons"
    OUTPUT_DIR    CORECAD_INSTALLER_ICONS_DIR
)

# ── Branding assets ───────────────────────────────────────────────────────────
# Copy branding.xml into the build bin/ directory whenever the source changes.
# Uses a custom command so the copy is re-run on build, not just at configure time.
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/bin/branding.xml
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/corecad/branding.xml
        ${CMAKE_BINARY_DIR}/bin/branding.xml
    DEPENDS ${CMAKE_SOURCE_DIR}/corecad/branding.xml
    COMMENT "Copying branding.xml to build directory"
)
add_custom_target(CoreCAD_branding ALL
    DEPENDS ${CMAKE_BINARY_DIR}/bin/branding.xml
)

# ── UI defaults ───────────────────────────────────────────────────────────────
# Show the splash screen by default on a fresh install (no saved preference).
# OFF for dev builds to avoid the startup blip; set ON for production releases.
option(CORECAD_SHOW_SPLASH "Show splash screen on startup by default" OFF)
if(CORECAD_SHOW_SPLASH)
    add_compile_definitions(CORECAD_SHOW_SPLASH)
endif()

# ── CoreRibbon ────────────────────────────────────────────────────
# Clean ribbon Mod for CoreCAD.  Source lives in a separate private repo.
# Auto-detected from ~/Repos/corecad-ribbon, or set CORECAD_CORERIBBON_DIR
# explicitly.  BUILD_CORERIBBON is forced ON; the subdirectory is a no-op
# when CORECAD_CORERIBBON_DIR is absent (falls back to standard toolbar UI).
set(BUILD_CORERIBBON ON CACHE BOOL "CoreCAD: Build CoreRibbon Mod" FORCE)

set(CORECAD_CORERIBBON_DIR "" CACHE PATH
    "Path to corecad-ribbon checkout (auto-detected if empty)")

if(NOT CORECAD_CORERIBBON_DIR OR NOT EXISTS "${CORECAD_CORERIBBON_DIR}/InitGui.py")
    if(DEFINED ENV{HOME} AND EXISTS "$ENV{HOME}/Repos/corecad-ribbon/InitGui.py")
        set(CORECAD_CORERIBBON_DIR "$ENV{HOME}/Repos/corecad-ribbon"
            CACHE PATH "" FORCE)
    endif()
endif()

# ── Disabled modules ──────────────────────────────────────────────────────────
# Addon Manager: users cannot install or remove workbenches at runtime.
set(BUILD_ADDONMGR          OFF CACHE BOOL "CoreCAD: Addon Manager disabled" FORCE)

# Out-of-scope workbenches excluded from CoreCAD's curated set.
set(BUILD_ROBOT             OFF CACHE BOOL "CoreCAD: Robotics simulation excluded" FORCE)
set(BUILD_OPENSCAD          OFF CACHE BOOL "CoreCAD: OpenSCAD integration excluded" FORCE)
set(BUILD_WEB               OFF CACHE BOOL "CoreCAD: Embedded web browser excluded" FORCE)
set(BUILD_TUX               OFF CACHE BOOL "CoreCAD: Novelty/mascot module excluded" FORCE)
set(BUILD_BIM               OFF CACHE BOOL "CoreCAD: Architecture/BIM excluded" FORCE)
set(BUILD_IDF               OFF CACHE BOOL "CoreCAD: PCB board file import excluded" FORCE)
set(BUILD_POINTS            OFF CACHE BOOL "CoreCAD: Point cloud tools excluded" FORCE)
set(BUILD_REVERSEENGINEERING OFF CACHE BOOL "CoreCAD: Reverse engineering excluded" FORCE)
set(BUILD_INSPECTION        OFF CACHE BOOL "CoreCAD: Point cloud inspection excluded (requires POINTS)" FORCE)
set(BUILD_PLOT              OFF CACHE BOOL "CoreCAD: Legacy matplotlib plotter excluded" FORCE)
