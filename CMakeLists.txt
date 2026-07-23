cmake_minimum_required(VERSION 3.20)
project(remember VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
# Keep language dialect ISO C11 (no gnu11), but still request POSIX APIs via
# feature-test macros on *project* targets only (not the amalgamation).
# CMAKE_C_EXTENSIONS OFF alone hides strdup/mkdtemp etc. on glibc with -std=c11.
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- options -----------------------------------------------------------------
# Sanitizers on instrumented targets (CLI + store unit tests). OFF in Release.
option(REMEMBER_ENABLE_SANITIZERS "Enable ASan/UBSan (and LSan on Linux)" ON)
# When ON, also request leak detection (no-op / noisy on macOS; real on Linux).
option(REMEMBER_ENABLE_LSAN "Request leak detection with ASan (Linux CI)" OFF)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(REMEMBER_ENABLE_SANITIZERS OFF)
endif()

# --- Layer A: hard compile flags (project code only; amalgamation uses -w) --
add_compile_options(
    -Wall
    -Wextra
    -Werror
    -Wshadow
    -Wconversion
    -Wsign-conversion
    -Wformat=2
    -Wformat-security
    -Wmissing-prototypes
    -Wstrict-prototypes
    -Wmissing-declarations
    -Wnull-dereference
    -Wdouble-promotion
    -Wwrite-strings
    -Wpointer-arith
    -Wundef
    -Wempty-body
    -Wredundant-decls
    -Wstrict-overflow=2
    -pedantic
    -fno-omit-frame-pointer
)

# -Wcast-qual on production TUs only. Tests build execv argv from const string
# tables (POSIX API is char *const[]); keeping -Wcast-qual there is pure noise.
set_property(SOURCE
    src/main.c
    src/cli.c
    src/store_sqlite.c
    APPEND PROPERTY COMPILE_OPTIONS -Wcast-qual
)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O2 -DNDEBUG)
else()
    add_compile_options(-g -O1)
endif()

# Sanitizer flags shared by instrumented targets.
# Leak detection is ASan's LSan (Linux): enable via ASAN_OPTIONS=detect_leaks=1
# (REMEMBER_ENABLE_LSAN / CI). Do not add -fsanitize=leak separately — it conflicts
# with -fsanitize=address on several toolchains.
set(REMEMBER_SANITIZER_FLAGS "")
if(REMEMBER_ENABLE_SANITIZERS)
    list(APPEND REMEMBER_SANITIZER_FLAGS -fsanitize=address,undefined)
endif()

if(REMEMBER_ENABLE_LSAN)
    set(REMEMBER_ASAN_DETECT_LEAKS "1")
else()
    set(REMEMBER_ASAN_DETECT_LEAKS "0")
endif()

function(remember_apply_sanitizers target)
    if(REMEMBER_SANITIZER_FLAGS)
        target_compile_options(${target} PRIVATE ${REMEMBER_SANITIZER_FLAGS})
        target_link_options(${target} PRIVATE ${REMEMBER_SANITIZER_FLAGS})
    endif()
endfunction()

# glibc + pure -std=c11 hides POSIX prototypes unless we request them.
# Apply only on Linux project targets (not amalgamation, not macOS — Darwin
# already exposes these APIs, and _XOPEN_SOURCE can hide mkdtemp / break SDKs).
function(remember_apply_posix_feature_macros target)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_compile_definitions(${target} PRIVATE
            _POSIX_C_SOURCE=200809L
            _XOPEN_SOURCE=700
        )
    endif()
endfunction()

# --- SQLite amalgamation (sole SQLite TU; relaxed warnings) ------------------
add_library(sqlite3 STATIC third_party/sqlite/sqlite3.c)
target_include_directories(sqlite3 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite)
# Compile options: SQLite recommended set tailored to a single-threaded CLI.
# See https://www.sqlite.org/compile.html — and docs/engineering-notes.md.
target_compile_definitions(sqlite3 PUBLIC
    SQLITE_ENABLE_FTS5
    SQLITE_OMIT_LOAD_EXTENSION
    SQLITE_DQS=0
    SQLITE_THREADSAFE=0
    SQLITE_DEFAULT_MEMSTATUS=0
    SQLITE_LIKE_DOESNT_MATCH_BLOBS
    SQLITE_OMIT_DEPRECATED
    SQLITE_OMIT_DECLTYPE
    SQLITE_OMIT_PROGRESS_CALLBACK
    SQLITE_OMIT_SHARED_CACHE
    SQLITE_DEFAULT_FOREIGN_KEYS=1
    SQLITE_DEFAULT_FILE_PERMISSIONS=0600
    SQLITE_SECURE_DELETE
)
if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_definitions(sqlite3 PUBLIC SQLITE_ENABLE_API_ARMOR)
endif()
target_compile_options(sqlite3 PRIVATE -w)

# Shared app sources (compiled per-target so the fork/exec harness parent can
# stay uninstrumented while the CLI and store unit binary take sanitizers).
set(REMEMBER_LIB_SOURCES
    src/cli.c
    src/store_sqlite.c
)

# --- remember CLI (instrumented) ---------------------------------------------
add_executable(remember
    src/main.c
    ${REMEMBER_LIB_SOURCES}
)
target_include_directories(remember PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(remember PRIVATE sqlite3)
target_compile_definitions(remember PRIVATE
    REMEMBER_VERSION="${PROJECT_VERSION}"
)
remember_apply_posix_feature_macros(remember)
remember_apply_sanitizers(remember)

# Non-sanitized CLI for macOS leaks(1) (leaks cannot inspect ASan heaps).
add_executable(remember_plain
    src/main.c
    ${REMEMBER_LIB_SOURCES}
)
target_include_directories(remember_plain PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(remember_plain PRIVATE sqlite3)
target_compile_definitions(remember_plain PRIVATE
    REMEMBER_VERSION="${PROJECT_VERSION}"
)
remember_apply_posix_feature_macros(remember_plain)

# --- full black-box harness (NOT instrumented — forks ASan child) ------------
set(REMEMBER_TEST_SOURCES
    tests/test_main.c
    tests/test.c
    tests/harness.c
    tests/test_cli_global.c
    tests/test_store.c
    tests/test_add.c
    tests/test_get_list_delete.c
    tests/test_search.c
    tests/test_update.c
    tests/test_json_db_config.c
    tests/test_key.c
    tests/test_schema_config.c
    tests/test_verification_edges.c
)
add_executable(remember_tests
    ${REMEMBER_TEST_SOURCES}
    ${REMEMBER_LIB_SOURCES}
)
target_include_directories(remember_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(remember_tests PRIVATE sqlite3)
target_compile_definitions(remember_tests PRIVATE
    REMEMBER_VERSION="${PROJECT_VERSION}"
)
remember_apply_posix_feature_macros(remember_tests)
add_dependencies(remember_tests remember)

# --- store unit tests under ASan (no fork/exec) — leak/corruption gate -------
add_executable(remember_store_tests
    tests/test_store_main.c
    tests/test.c
    tests/harness.c
    tests/test_store.c
    src/store_sqlite.c
)
target_include_directories(remember_store_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(remember_store_tests PRIVATE sqlite3)
remember_apply_posix_feature_macros(remember_store_tests)
remember_apply_sanitizers(remember_store_tests)

enable_testing()
# Green gate: implemented suites via black-box harness + ASan store units.
add_test(NAME scaffold_gate
    COMMAND remember_tests $<TARGET_FILE:remember> --only cli_global,store
)
set_tests_properties(scaffold_gate PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
add_test(NAME store_asan_gate
    COMMAND remember_store_tests
)
set_tests_properties(store_asan_gate PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    ENVIRONMENT
        "ASAN_OPTIONS=detect_leaks=${REMEMBER_ASAN_DETECT_LEAKS}:halt_on_error=1:abort_on_error=1:allocator_may_return_null=1;UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1"
)

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

# Full clang static analyzer build (separate build dir; slow; used by CI).
find_program(SCAN_BUILD scan-build)
if(SCAN_BUILD)
    add_custom_target(analyze
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${CMAKE_SOURCE_DIR}/build-analyze
        COMMAND ${SCAN_BUILD} --status-bugs
            cmake -S ${CMAKE_SOURCE_DIR} -B ${CMAKE_SOURCE_DIR}/build-analyze
            -DCMAKE_C_COMPILER=clang
            -DREMEMBER_ENABLE_SANITIZERS=OFF
        COMMAND ${SCAN_BUILD} --status-bugs
            cmake --build ${CMAKE_SOURCE_DIR}/build-analyze
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "scan-build static analysis (fails on reported bugs)"
        VERBATIM
        USES_TERMINAL
    )
endif()

# macOS: leaks(1) on a short CLI invocation list (heuristic leak gate).
# Uses remember_plain — leaks(1) cannot inspect AddressSanitizer heaps.
if(APPLE)
    add_custom_target(leaks-macos
        COMMAND ${CMAKE_SOURCE_DIR}/scripts/run-leaks-macos.sh $<TARGET_FILE:remember_plain>
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "macOS leaks(1) on remember_plain CLI smoke paths"
        VERBATIM
        USES_TERMINAL
        DEPENDS remember_plain
    )
endif()
