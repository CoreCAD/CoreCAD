# CoreCAD_Branding.cmake — branding asset overlay at configure time
#
# When CORECAD_ASSETS_DIR points to a local corecad-assets checkout (or a
# directory prepared by scripts/fetch-branding.sh), icon and image files
# listed in scripts/branding-assets.txt are staged into the build tree and
# compiled into the binary via Qt resources.  The source tree is never
# modified.
#
# Usage in a module CMakeLists.txt:
#
#   corecad_stage_resources(
#       SOURCE_DIR   "${CMAKE_CURRENT_SOURCE_DIR}/Icons"
#       ASSETS_SUBDIR "src/Gui/Icons"
#       QRC_FILE     "${CMAKE_CURRENT_SOURCE_DIR}/Icons/resource.qrc"
#       OUTPUT_QRC   my_qrc_var
#   )
#   # my_qrc_var now holds the path to either the original or staged QRC.

# ── Option ────────────────────────────────────────────────────────────────────
set(CORECAD_ASSETS_DIR "" CACHE PATH
    "Path to corecad-assets checkout for branding (auto-detected if empty)")

# Auto-detect a local checkout at ~/Repos/corecad-assets
if(NOT CORECAD_ASSETS_DIR OR NOT EXISTS "${CORECAD_ASSETS_DIR}")
    if(DEFINED ENV{HOME} AND EXISTS "$ENV{HOME}/Repos/corecad-assets")
        set(CORECAD_ASSETS_DIR "$ENV{HOME}/Repos/corecad-assets"
            CACHE PATH "" FORCE)
    endif()
endif()

if(CORECAD_ASSETS_DIR AND EXISTS "${CORECAD_ASSETS_DIR}")
    set(CORECAD_BRANDING_AVAILABLE TRUE CACHE INTERNAL "")
    message(STATUS "CoreCAD branding: using assets from ${CORECAD_ASSETS_DIR}")
else()
    set(CORECAD_BRANDING_AVAILABLE FALSE CACHE INTERNAL "")
    message(STATUS "CoreCAD branding: no assets directory found, using placeholders")
endif()

# ── Read the allowlist ────────────────────────────────────────────────────────
# Parse scripts/branding-assets.txt into a list of glob patterns.
set(_CORECAD_BRANDING_PATTERNS "")
set(_branding_cfg "${CMAKE_SOURCE_DIR}/scripts/branding-assets.txt")
if(EXISTS "${_branding_cfg}")
    file(STRINGS "${_branding_cfg}" _branding_lines)
    foreach(_line IN LISTS _branding_lines)
        # Skip comments and blank lines
        string(STRIP "${_line}" _line)
        if(_line STREQUAL "" OR _line MATCHES "^#")
            continue()
        endif()
        list(APPEND _CORECAD_BRANDING_PATTERNS "${_line}")
    endforeach()
endif()

# ── Function ──────────────────────────────────────────────────────────────────
function(corecad_stage_resources)
    cmake_parse_arguments(CSR "" "SOURCE_DIR;ASSETS_SUBDIR;QRC_FILE;OUTPUT_QRC" "" ${ARGN})

    if(NOT CORECAD_BRANDING_AVAILABLE)
        # No assets available — use the original QRC unchanged
        set(${CSR_OUTPUT_QRC} "${CSR_QRC_FILE}" PARENT_SCOPE)
        return()
    endif()

    # Determine which allowlist patterns apply to this ASSETS_SUBDIR.
    # e.g. if ASSETS_SUBDIR is "src/Gui/Icons" and a pattern is
    # "src/Gui/Icons/*.svg", we extract the file glob "*.svg".
    set(_applicable_globs "")
    foreach(_pat IN LISTS _CORECAD_BRANDING_PATTERNS)
        string(FIND "${_pat}" "${CSR_ASSETS_SUBDIR}/" _pos)
        if(_pos EQUAL 0)
            string(LENGTH "${CSR_ASSETS_SUBDIR}/" _prefix_len)
            string(SUBSTRING "${_pat}" ${_prefix_len} -1 _file_glob)
            list(APPEND _applicable_globs "${_file_glob}")
        endif()
    endforeach()

    if(NOT _applicable_globs)
        # No allowlist patterns match this subdir — use original QRC
        set(${CSR_OUTPUT_QRC} "${CSR_QRC_FILE}" PARENT_SCOPE)
        return()
    endif()

    # Create a staging directory keyed on the assets subdir
    string(REPLACE "/" "_" _staging_name "${CSR_ASSETS_SUBDIR}")
    set(_staging "${CMAKE_BINARY_DIR}/branding-staging/${_staging_name}")

    # Copy the entire source directory tree into staging (preserving subdirs)
    file(GLOB_RECURSE _src_files RELATIVE "${CSR_SOURCE_DIR}" "${CSR_SOURCE_DIR}/*")
    foreach(_rel IN LISTS _src_files)
        configure_file("${CSR_SOURCE_DIR}/${_rel}" "${_staging}/${_rel}" COPYONLY)
    endforeach()

    # Overlay branding assets — only files that match the allowlist globs
    # AND already exist in the source tree (we never add files the QRC
    # doesn't reference).
    set(_assets_dir "${CORECAD_ASSETS_DIR}/${CSR_ASSETS_SUBDIR}")
    set(_applied 0)
    if(EXISTS "${_assets_dir}")
        foreach(_glob IN LISTS _applicable_globs)
            file(GLOB _matches "${_assets_dir}/${_glob}")
            foreach(_src IN LISTS _matches)
                get_filename_component(_name "${_src}" NAME)
                if(EXISTS "${_staging}/${_name}")
                    configure_file("${_src}" "${_staging}/${_name}" COPYONLY)
                    math(EXPR _applied "${_applied} + 1")
                endif()
            endforeach()
        endforeach()
    endif()
    message(STATUS "CoreCAD branding: staged ${_applied} override(s) for ${CSR_ASSETS_SUBDIR}")

    # Copy the QRC file into staging and fix any relative paths that
    # reference files outside the source directory (e.g. ../../Doc/...).
    get_filename_component(_qrc_name "${CSR_QRC_FILE}" NAME)
    get_filename_component(_qrc_dir  "${CSR_QRC_FILE}" DIRECTORY)
    file(READ "${CSR_QRC_FILE}" _qrc_content)

    # Find all <file ...>../something</file> entries and make them absolute
    string(REGEX MATCHALL "<file[^>]*>[.][.]/[^<]+" _parent_refs "${_qrc_content}")
    foreach(_ref IN LISTS _parent_refs)
        string(REGEX MATCH ">([^<]+)" _match "${_ref}")
        set(_rel_path "${CMAKE_MATCH_1}")
        get_filename_component(_abs_path "${_qrc_dir}/${_rel_path}" ABSOLUTE)
        string(REPLACE ">${_rel_path}" ">${_abs_path}" _ref_fixed "${_ref}")
        string(REPLACE "${_ref}" "${_ref_fixed}" _qrc_content "${_qrc_content}")
    endforeach()

    file(WRITE "${_staging}/${_qrc_name}" "${_qrc_content}")
    set(${CSR_OUTPUT_QRC} "${_staging}/${_qrc_name}" PARENT_SCOPE)
endfunction()
