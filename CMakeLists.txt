cmake_minimum_required(VERSION 3.24)
enable_testing()

project(imgui_datagrid VERSION 1.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── LLVM libc++ auto-link (macOS + upstream LLVM only) ────────────────────────
# When CMAKE_CXX_COMPILER is an upstream LLVM clang++ (not AppleClang) that
# ships its own libc++ next to the binary, use that libc++ for both compilation
# and linking.  This avoids undefined-symbol errors caused by mixing LLVM's
# newer libc++ headers with Apple's older system libc++ at link time.
if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND APPLE)
    get_filename_component(_clang_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(_llvm_root "${_clang_bin}"         DIRECTORY)
    set(_llvm_lib "${_llvm_root}/lib")
    if (EXISTS "${_llvm_lib}/libc++.dylib")
        message(STATUS "[libc++] upstream LLVM libc++ → ${_llvm_lib}")
        # Compile with LLVM's libc++ headers.
        add_compile_options(-stdlib=libc++)
        # Link: use LLVM's libc++ and its separate libc++abi (not re-exported by
        # LLVM's libc++ unlike Apple's system libc++), and set rpath so the
        # binary finds the right dylibs at runtime.
        add_link_options(
            -stdlib=libc++
            -L${_llvm_lib}
            -lc++abi
            -Wl,-rpath,${_llvm_lib}
        )
    endif()
endif()

include(FetchContent)

option(DATAGRID_ENABLE_CLANG_FORMAT "Register format / check-format targets" ON)
if (DATAGRID_ENABLE_CLANG_FORMAT)
    if (NOT CLANG_FORMAT_EXE)
        find_program(CLANG_FORMAT_EXE
            NAMES clang-format clang-format-18 clang-format-17 clang-format-16
            HINTS
                /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin
                /opt/homebrew/bin /usr/local/bin
        )
    endif()
    if (CLANG_FORMAT_EXE)
        message(STATUS "[clang-format] ${CLANG_FORMAT_EXE}")
        file(GLOB_RECURSE _ALL_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/adapters/*.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/adapters/*.hpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/*.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/*.hpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.hpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/imgui_datagrid.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/imgui_datagrid.hpp"
        )
        add_custom_target(format
            COMMAND ${CLANG_FORMAT_EXE} -i --style=file ${_ALL_SOURCES}
            COMMENT "clang-format: reformatting project sources" VERBATIM
        )
        set(_CHECK_FORMAT_ALL)
        if (CMAKE_BUILD_TYPE STREQUAL "Release")
            set(_CHECK_FORMAT_ALL ALL)
        endif()
        # check-format fails the Release gate on mis-formatted commits.
        add_custom_target(check-format ${_CHECK_FORMAT_ALL}
            COMMAND ${CLANG_FORMAT_EXE} --dry-run --Werror --style=file ${_ALL_SOURCES}
            COMMENT "clang-format: checking formatting (Release gate)" VERBATIM
        )
    else()
        message(STATUS "[clang-format] not found — format/check-format targets unavailable")
    endif()
endif()

option(DATAGRID_ENABLE_ASAN "Enable AddressSanitizer in Debug builds" ON)
if (DATAGRID_ENABLE_ASAN
        AND (CMAKE_BUILD_TYPE STREQUAL "Debug" OR DATAGRID_SANITIZE_NON_DEBUG)
        AND NOT MSVC AND NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    # Full compile+link probe avoids false negatives with AppleClang where the
    # ASan runtime library is separate from the -fsanitize=address frontend flag.
    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_FLAGS        "-fsanitize=address -fno-omit-frame-pointer")
    set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=address")
    check_cxx_source_compiles("int main(){}" _COMPILER_SUPPORTS_ASAN)
    unset(CMAKE_REQUIRED_FLAGS)
    unset(CMAKE_REQUIRED_LINK_OPTIONS)
    if (_COMPILER_SUPPORTS_ASAN)
        message(STATUS "[ASan] Enabled")
        add_library(datagrid_asan INTERFACE)
        target_compile_options(datagrid_asan INTERFACE -fsanitize=address -fno-omit-frame-pointer -g)
        target_link_options(datagrid_asan INTERFACE -fsanitize=address)
    else()
        message(STATUS "[ASan] Not supported by compiler — skipped")
    endif()
else()
    message(STATUS "[ASan] Disabled")
endif()

option(DATAGRID_ENABLE_UBSAN  "Enable UBSan in Debug builds" ON)
option(DATAGRID_SANITIZE_NON_DEBUG "Also apply sanitizers in non-Debug builds" OFF)
if (DATAGRID_ENABLE_UBSAN
        AND (CMAKE_BUILD_TYPE STREQUAL "Debug" OR DATAGRID_SANITIZE_NON_DEBUG)
        AND NOT MSVC AND NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_FLAGS        "-fsanitize=undefined")
    set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=undefined")
    check_cxx_source_compiles("int main(){}" _COMPILER_SUPPORTS_UBSAN)
    unset(CMAKE_REQUIRED_FLAGS)
    unset(CMAKE_REQUIRED_LINK_OPTIONS)
    if (_COMPILER_SUPPORTS_UBSAN)
        message(STATUS "[UBSan] Enabled")
        add_library(datagrid_ubsan INTERFACE)
        target_compile_options(datagrid_ubsan INTERFACE -fsanitize=undefined -fno-omit-frame-pointer -g)
        target_link_options(datagrid_ubsan INTERFACE -fsanitize=undefined)
    else()
        message(STATUS "[UBSan] Not supported by compiler — skipped")
    endif()
else()
    message(STATUS "[UBSan] Disabled")
endif()

option(DATAGRID_ENABLE_CCACHE "Use ccache to speed up recompilation" ON)
if (DATAGRID_ENABLE_CCACHE)
    find_program(CCACHE_EXE ccache)
    if (CCACHE_EXE)
        message(STATUS "[ccache] ${CCACHE_EXE}")
        if (NOT CMAKE_C_COMPILER_LAUNCHER)
            set(CMAKE_C_COMPILER_LAUNCHER   "${CCACHE_EXE}" CACHE STRING "" FORCE)
        endif()
        if (NOT CMAKE_CXX_COMPILER_LAUNCHER)
            set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_EXE}" CACHE STRING "" FORCE)
        endif()
    else()
        message(STATUS "[ccache] not found")
    endif()
endif()

option(DATAGRID_ENABLE_WARNINGS "Enable extra compiler warnings on project targets" ON)
if (DATAGRID_ENABLE_WARNINGS)
    add_library(datagrid_warnings INTERFACE)
    if (MSVC)
        target_compile_options(datagrid_warnings INTERFACE /W4)
    else()
        target_compile_options(datagrid_warnings INTERFACE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wno-unused-parameter)
    endif()
endif()

include(CheckIPOSupported)
check_ipo_supported(RESULT _ipo_ok)
message(STATUS "[LTO] ${_ipo_ok}")

# Dear ImGui — core only; sokol_imgui.h provides the platform/rendering backend.
find_package(imgui QUIET)
if (NOT imgui_FOUND)
    message(STATUS "[FetchContent] Dear ImGui v1.92.6")
    FetchContent_Declare(
        ImGui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.92.6
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(ImGui)
    add_library(imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
    )
    target_include_directories(imgui INTERFACE ${imgui_SOURCE_DIR})
else()
    message(STATUS "[local] Dear ImGui")
endif()

# Sokol: single-header C libraries (sokol_app, sokol_gfx, sokol_glue, sokol_log,
# util/sokol_imgui). Headers only — no build step needed.
# Platform backend is selected in examples/databrowser/CMakeLists.txt.
find_path(SOKOL_INCLUDE_DIR NAMES sokol_app.h HINTS ${sokol_SOURCE_DIR} NO_DEFAULT_PATH)
if (NOT SOKOL_INCLUDE_DIR)
    message(STATUS "[FetchContent] Sokol")
    FetchContent_Declare(
        sokol
        GIT_REPOSITORY https://github.com/floooh/sokol.git
        GIT_TAG        master
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(sokol)
    set(SOKOL_INCLUDE_DIR ${sokol_SOURCE_DIR} CACHE PATH "Sokol headers" FORCE)
    message(STATUS "[FetchContent] Sokol → ${SOKOL_INCLUDE_DIR}")
else()
    message(STATUS "[local] Sokol → ${SOKOL_INCLUDE_DIR}")
endif()

# Enable with:  cmake -DDATAGRID_USE_SQLCIPHER=ON ..
# Requires:     brew install sqlcipher  /  apt install libsqlcipher-dev
option(DATAGRID_USE_SQLCIPHER "Build with SQLCipher encrypted-SQLite support" OFF)
if (DATAGRID_USE_SQLCIPHER)
    find_library(SQLCIPHER_LIBRARY NAMES sqlcipher
        HINTS /opt/homebrew/opt/sqlcipher/lib /opt/homebrew/lib
              /usr/local/opt/sqlcipher/lib /usr/lib /usr/local/lib)
    find_path(SQLCIPHER_INCLUDE_DIR NAMES sqlite3.h
        HINTS /opt/homebrew/opt/sqlcipher/include/sqlcipher /opt/homebrew/include/sqlcipher
              /usr/local/opt/sqlcipher/include/sqlcipher /usr/include/sqlcipher)
    if (SQLCIPHER_LIBRARY AND SQLCIPHER_INCLUDE_DIR)
        message(STATUS "[SQLCipher] ${SQLCIPHER_LIBRARY}")
        # Tell SQLiteCpp to use our external SQLite (= SQLCipher) rather than its bundled one.
        set(SQLITECPP_INTERNAL_SQLITE  OFF CACHE BOOL "" FORCE)
        set(SQLite3_LIBRARIES    "${SQLCIPHER_LIBRARY}"    CACHE FILEPATH "" FORCE)
        set(SQLite3_INCLUDE_DIRS "${SQLCIPHER_INCLUDE_DIR}" CACHE PATH    "" FORCE)
        set(DATAGRID_HAVE_SQLCIPHER TRUE)
    else()
        message(WARNING "[SQLCipher] Not found — falling back to plain SQLite.")
        set(DATAGRID_USE_SQLCIPHER OFF CACHE BOOL "" FORCE)
    endif()
endif()

find_package(SQLiteCpp QUIET)
if (NOT SQLiteCpp_FOUND)
    message(STATUS "[FetchContent] SQLiteCpp 3.3.3")
    FetchContent_Declare(
        SQLiteCpp
        GIT_REPOSITORY https://github.com/SRombauts/SQLiteCpp.git
        GIT_TAG        3.3.3
        GIT_SHALLOW    TRUE
    )
    set(SQLITECPP_RUN_CPPLINT    OFF CACHE BOOL "" FORCE)
    set(SQLITECPP_RUN_CPPCHECK   OFF CACHE BOOL "" FORCE)
    set(SQLITECPP_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(SQLITECPP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SQLITECPP_INTERNAL_SQLITE ON  CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(SQLiteCpp)
else()
    message(STATUS "[local] SQLiteCpp")
endif()

# hanickadot/polyfill-23 — header-only C++23 polyfills (move_only_function, etc.)
# Provides hana23::move_only_function when __cpp_lib_move_only_function is absent.
FetchContent_Declare(
    hana_polyfill23
    GIT_REPOSITORY https://github.com/hanickadot/polyfill-23.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(hana_polyfill23)

find_package(nlohmann_json QUIET)
if (NOT nlohmann_json_FOUND)
    message(STATUS "[FetchContent] nlohmann/json v3.11.3")
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG        v3.11.3
        GIT_SHALLOW    TRUE
    )
    set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
    set(JSON_Install    OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(nlohmann_json)
else()
    message(STATUS "[local] nlohmann/json")
endif()

# DuckDB: enable with  cmake -DDATAGRID_USE_DUCKDB=ON
# Resolution order:
#   1. CMake package  (vcpkg / conan / system cmake config)
#   2. Manual find    (brew, apt, DUCKDB_ROOT)
#   3. FetchContent   (source amalgamation — libduckdb-src.zip, compiled like other deps)
option(DATAGRID_USE_DUCKDB "Build with DuckDB in-process OLAP adapter (+ UDF support)" OFF)
if (DATAGRID_USE_DUCKDB)
    find_package(duckdb QUIET)
    if (duckdb_FOUND)
        message(STATUS "[DuckDB] Found via CMake package")
        set(DATAGRID_HAVE_DUCKDB TRUE)
    else()
        find_library(DUCKDB_LIBRARY NAMES duckdb
            HINTS ${DUCKDB_ROOT}/lib /opt/homebrew/opt/duckdb/lib /opt/homebrew/lib
                  /usr/local/opt/duckdb/lib /usr/local/lib /usr/lib)
        find_path(DUCKDB_INCLUDE_DIR NAMES duckdb.hpp
            HINTS ${DUCKDB_ROOT}/include /opt/homebrew/opt/duckdb/include /opt/homebrew/include
                  /usr/local/opt/duckdb/include /usr/local/include /usr/include)
        if (DUCKDB_LIBRARY AND DUCKDB_INCLUDE_DIR)
            message(STATUS "[DuckDB] System: ${DUCKDB_LIBRARY}")
            add_library(duckdb::duckdb SHARED IMPORTED)
            set_target_properties(duckdb::duckdb PROPERTIES
                IMPORTED_LOCATION             "${DUCKDB_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${DUCKDB_INCLUDE_DIR}")
            set(DATAGRID_HAVE_DUCKDB TRUE)
        endif()
    endif()

    if (NOT DATAGRID_HAVE_DUCKDB)
        set(DATAGRID_DUCKDB_VERSION "v1.5.1" CACHE STRING "DuckDB release tag to fetch")
        message(STATUS "[FetchContent] DuckDB ${DATAGRID_DUCKDB_VERSION} source amalgamation")
        FetchContent_Declare(
            duckdb_src
            URL                    "https://github.com/duckdb/duckdb/releases/download/${DATAGRID_DUCKDB_VERSION}/libduckdb-src.zip"
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        FetchContent_MakeAvailable(duckdb_src)

        # DuckDB v1.5.1 amalgamation has QueryMetrics use unique_ptr<ActiveTimer>
        # while ActiveTimer is forward-declared only.  LLVM 22 libc++ correctly
        # rejects this in C++23 (constexpr ~unique_ptr requires a complete type).
        # cmake/patch_duckdb.py makes ~QueryMetrics() non-inline so its body is
        # compiled after ActiveTimer is fully defined.
        execute_process(
            COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_duckdb.py"
                    "${duckdb_src_SOURCE_DIR}/duckdb.hpp"
            RESULT_VARIABLE _duckdb_patch_rc
        )
        if (NOT _duckdb_patch_rc EQUAL 0)
            message(FATAL_ERROR "[DuckDB] patch_duckdb.py failed (exit ${_duckdb_patch_rc})")
        endif()

        add_library(duckdb_amalgamation STATIC ${duckdb_src_SOURCE_DIR}/duckdb.cpp)
        target_include_directories(duckdb_amalgamation PUBLIC ${duckdb_src_SOURCE_DIR})
        target_compile_options(duckdb_amalgamation PRIVATE -w)
        add_library(duckdb::duckdb ALIAS duckdb_amalgamation)
        set(DATAGRID_HAVE_DUCKDB TRUE)
        message(STATUS "[DuckDB] Building amalgamation from ${duckdb_src_SOURCE_DIR}")
    endif()
endif()

# PostgreSQL: enable with  cmake -DDATAGRID_USE_PGSQL=ON
# Requires:   brew install postgresql@16   /   apt install libpq-dev
option(DATAGRID_USE_PGSQL "Build PostgreSQL adapter via libpqxx 8" OFF)
if (DATAGRID_USE_PGSQL)
    find_package(PostgreSQL QUIET)
    if (NOT PostgreSQL_FOUND)
        message(WARNING
            "[PostgreSQL] libpq not found — DATAGRID_USE_PGSQL disabled.\n"
            "  macOS:  brew install postgresql@16\n"
            "  Debian: sudo apt install libpq-dev")
        set(DATAGRID_USE_PGSQL OFF CACHE BOOL "" FORCE)
    endif()
endif()

if (DATAGRID_USE_PGSQL)
    find_package(libpqxx 8 QUIET)
    if (libpqxx_FOUND)
        message(STATUS "[libpqxx] Found via CMake package (${libpqxx_VERSION})")
        set(DATAGRID_HAVE_PGSQL TRUE)
    else()
        find_library(PQXX_LIBRARY   NAMES pqxx
            HINTS /opt/homebrew/opt/libpqxx/lib /opt/homebrew/lib /usr/local/lib /usr/lib)
        find_path(PQXX_INCLUDE_DIR  NAMES pqxx/pqxx
            HINTS /opt/homebrew/opt/libpqxx/include /opt/homebrew/include /usr/local/include /usr/include)
        if (PQXX_LIBRARY AND PQXX_INCLUDE_DIR)
            message(STATUS "[libpqxx] System: ${PQXX_LIBRARY}")
            add_library(libpqxx::pqxx STATIC IMPORTED)
            set_target_properties(libpqxx::pqxx PROPERTIES
                IMPORTED_LOCATION             "${PQXX_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${PQXX_INCLUDE_DIR}"
                INTERFACE_LINK_LIBRARIES      "PostgreSQL::PostgreSQL")
            set(DATAGRID_HAVE_PGSQL TRUE)
        endif()
    endif()

    if (NOT DATAGRID_HAVE_PGSQL)
        message(STATUS "[FetchContent] libpqxx 8.0.0")
        FetchContent_Declare(
            libpqxx
            GIT_REPOSITORY https://github.com/jtv/libpqxx.git
            GIT_TAG        8.0.0
            GIT_SHALLOW    TRUE
        )
        set(BUILD_TESTING    OFF CACHE BOOL "" FORCE)
        set(SKIP_PQXX_SHARED ON  CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(libpqxx)
        set(DATAGRID_HAVE_PGSQL TRUE)
        message(STATUS "[libpqxx] Building from source")
    endif()
endif()

add_library(datagrid_adapters STATIC    adapters/adapter_registry.cpp
    adapters/csv/csv_adapter.cpp
    adapters/sqlite/sqlite_adapter.cpp
    adapters/filesystem/filesystem_adapter.cpp
    adapters/filesystem/fluent_query.cpp
    adapters/query/tabular_query.cpp
    adapters/utils/type_inferrer.cpp
)
target_include_directories(datagrid_adapters PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${hana_polyfill23_SOURCE_DIR}/include
)
target_compile_definitions(datagrid_adapters PUBLIC COMPAT_HAS_HANA_POLYFILL23)
target_compile_features(datagrid_adapters PUBLIC cxx_std_23)
target_link_libraries(datagrid_adapters PUBLIC SQLiteCpp)
if (TARGET datagrid_asan)
    target_link_libraries(datagrid_adapters PUBLIC datagrid_asan)
endif()
if (TARGET datagrid_ubsan)
    target_link_libraries(datagrid_adapters PUBLIC datagrid_ubsan)
endif()
if (TARGET datagrid_warnings)
    target_link_libraries(datagrid_adapters PRIVATE datagrid_warnings)
endif()
if (DATAGRID_HAVE_SQLCIPHER)
    target_compile_definitions(datagrid_adapters PUBLIC HAVE_SQLCIPHER)
    target_include_directories(datagrid_adapters PRIVATE ${SQLCIPHER_INCLUDE_DIR})
endif()
if (DATAGRID_HAVE_DUCKDB)
    target_sources(datagrid_adapters PRIVATE adapters/duckdb/duckdb_adapter.cpp)
    target_link_libraries(datagrid_adapters PUBLIC duckdb::duckdb)
    target_compile_definitions(datagrid_adapters PUBLIC DATAGRID_HAVE_DUCKDB)
    message(STATUS "[DuckDB] Adapter enabled")
endif()

if (DATAGRID_HAVE_PGSQL)
    target_sources(datagrid_adapters PRIVATE adapters/postgres/postgres_adapter.cpp)
    target_link_libraries(datagrid_adapters PUBLIC libpqxx::pqxx PostgreSQL::PostgreSQL)
    target_compile_definitions(datagrid_adapters PUBLIC DATAGRID_HAVE_PGSQL)
    message(STATUS "[PostgreSQL] Adapter enabled")
endif()

add_library(imgui_datagrid_lib STATIC imgui_datagrid.cpp)
target_include_directories(imgui_datagrid_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${hana_polyfill23_SOURCE_DIR}/include
)
target_compile_definitions(imgui_datagrid_lib PUBLIC
    IMGUI_DATAGRID_USE_JSON
    COMPAT_HAS_HANA_POLYFILL23
)
target_compile_features(imgui_datagrid_lib PUBLIC cxx_std_23)
target_link_libraries(imgui_datagrid_lib PUBLIC imgui nlohmann_json::nlohmann_json)
if (TARGET datagrid_asan)
    target_link_libraries(imgui_datagrid_lib PUBLIC datagrid_asan)
endif()
if (TARGET datagrid_ubsan)
    target_link_libraries(imgui_datagrid_lib PUBLIC datagrid_ubsan)
endif()
if (TARGET datagrid_warnings)
    target_link_libraries(imgui_datagrid_lib PRIVATE datagrid_warnings)
endif()

if (_ipo_ok AND CMAKE_BUILD_TYPE STREQUAL "Release")
    set_target_properties(datagrid_adapters  PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
    set_target_properties(imgui_datagrid_lib PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

find_path(STB_INCLUDE_DIR NAMES stb_image.h NO_DEFAULT_PATH)
if (NOT STB_INCLUDE_DIR)
    message(STATUS "[FetchContent] stb")
    FetchContent_Declare(stb GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG master GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(stb)
    set(STB_INCLUDE_DIR ${stb_SOURCE_DIR} CACHE PATH "stb headers" FORCE)
else()
    message(STATUS "[local] stb → ${STB_INCLUDE_DIR}")
endif()

add_subdirectory(examples/databrowser)

if (NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    add_subdirectory(tests)
endif()
