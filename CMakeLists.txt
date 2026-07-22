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
add_executable(remember
    src/main.c
    src/cli.c
)
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
add_executable(remember_tests
    tests/test_main.c
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
target_include_directories(remember_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
add_dependencies(remember_tests remember)

enable_testing()
add_test(
    NAME remember_tests
    COMMAND remember_tests $<TARGET_FILE:remember>
)
set_tests_properties(remember_tests PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
    # Use find so paths with spaces work (shell globs break under VERBATIM).
    add_custom_target(format
        COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_SOURCE_DIR}
            ${CLANG_FORMAT} -i --style=file
            src/main.c src/cli.c src/cli.h src/exit_codes.h
            tests/test.h tests/harness.h tests/harness.c tests/register.h
            tests/test_main.c tests/test_cli_global.c tests/test_add.c
            tests/test_get_list_delete.c tests/test_search.c tests/test_update.c
            tests/test_json_db_config.c tests/test_key.c
            tests/test_schema_config.c tests/test_verification_edges.c
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
