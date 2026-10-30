# SPDX-License-Identifier: LGPL-2.1-or-later

add_executable(Import_tests_run
        Feature.cpp
)

target_include_directories(Import_tests_run PUBLIC
        ${CMAKE_BINARY_DIR}
)
