cmake_minimum_required(VERSION 3.20)
project(remember VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Strict project flags (third_party will use relaxed flags later)
add_compile_options(
    -Wall
    -Wextra
    -Werror
    -Wshadow
    -Wconversion
    -Wsign-conversion
    -Wformat=2
    -Wmissing-prototypes
    -Wstrict-prototypes
    -Wmissing-declarations
    -pedantic
    -fno-omit-frame-pointer
)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O2 -DNDEBUG)
else()
    add_compile_options(-g -O1)
endif()

# --- remember CLI ------------------------------------------------------------
set(REMEMBER_APP_SOURCES
    src/main.c
    src/cli.c
)
add_executable(remember ${REMEMBER_APP_SOURCES})
target_include_directories(remember PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(remember PRIVATE
    REMEMBER_VERSION="${PROJECT_VERSION}"
)

# Sanitizers on the CLI under test only — not on remember_tests (fork/exec harness
# deadlocks under ASan when both parent and child are instrumented).
if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(remember PRIVATE -fsanitize=address,undefined)
    target_link_options(remember PRIVATE -fsanitize=address,undefined)
endif()

# --- tests -------------------------------------------------------------------
set(REMEMBER_TEST_SOURCES
    tests/test_main.c
    tests/test.c
    tests/harness.c
    tests/test_cli_global.c
    tests/test_add.c
    tests/test_get_list_delete.c
    tests/test_search.c
    tests/test_update.c
    tests/test_json_db_config.c
    tests/test_key.c
    tests/test_schema_config.c
    tests/test_verification_edges.c
)
add_executable(remember_tests ${REMEMBER_TEST_SOURCES})
target_include_directories(remember_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
add_dependencies(remember_tests remember)

enable_testing()
# Green gate: only the implemented suites. Extend the --only list as each step
# lands (e.g. --only cli_global,add). Must always pass, so a regression in
# finished work is not masked by the still-failing WIP suites.
add_test(NAME scaffold_gate
    COMMAND remember_tests $<TARGET_FILE:remember> --only cli_global
)
set_tests_properties(scaffold_gate PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
# Full WIP suite is expected to fail during TDD; run it manually to see progress:
#   ./remember_tests $<path-to-remember>

# clang-format over the same file set the lint script checks (src + tests).
find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
    file(GLOB_RECURSE REMEMBER_FORMAT_FILES CONFIGURE_DEPENDS
        ${CMAKE_SOURCE_DIR}/src/*.c ${CMAKE_SOURCE_DIR}/src/*.h
        ${CMAKE_SOURCE_DIR}/tests/*.c ${CMAKE_SOURCE_DIR}/tests/*.h
    )
    add_custom_target(format
        COMMAND ${CLANG_FORMAT} -i --style=file ${REMEMBER_FORMAT_FILES}
        COMMENT "Running clang-format"
        VERBATIM
    )
endif()

add_custom_target(lint
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/lint-all.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running lint suite"
    VERBATIM
    USES_TERMINAL
)
