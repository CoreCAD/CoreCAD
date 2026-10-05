macro(SetupLibGit2)
    # -------------------------------- libgit2 --------------------------------
    # Reads a document's past versions out of the git repository it is kept in.
    find_path(LIBGIT2_INCLUDE_DIR git2.h)
    find_library(LIBGIT2_LIBRARY NAMES git2 libgit2)
    if(NOT LIBGIT2_INCLUDE_DIR OR NOT LIBGIT2_LIBRARY)
        message(FATAL_ERROR "libgit2 not found.")
    endif()
    if(NOT TARGET libgit2::libgit2)
        add_library(libgit2::libgit2 UNKNOWN IMPORTED)
        set_target_properties(libgit2::libgit2 PROPERTIES
            IMPORTED_LOCATION "${LIBGIT2_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${LIBGIT2_INCLUDE_DIR}")
    endif()
endmacro(SetupLibGit2)
