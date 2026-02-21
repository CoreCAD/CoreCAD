# CoreCAD_Options.cmake
# CoreCAD-specific build option overrides.
# This file is included after InitializeFreeCADBuildOptions() so these settings
# take precedence over upstream defaults. Add/remove workbenches here to control
# what is compiled into CoreCAD.

# ── Branding assets ───────────────────────────────────────────────────────────
# Copy branding.xml from the tracked source location into the build bin/ directory.
# The application reads this file at startup from alongside the executable.
file(COPY ${CMAKE_SOURCE_DIR}/corecad/branding.xml DESTINATION ${CMAKE_BINARY_DIR}/bin)

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
