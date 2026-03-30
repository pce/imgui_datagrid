cmake_minimum_required(VERSION 3.24)
enable_testing()

project(imgui_datagrid VERSION 1.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

# ── Dear ImGui (core only — platform backend provided by sokol_imgui.h) ──────
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
        # No imgui_impl_* files — sokol_imgui.h handles rendering integration
    )
    target_include_directories(imgui
        INTERFACE ${imgui_SOURCE_DIR}
    )
else()
    message(STATUS "[local] Dear ImGui")
endif()

# ── Sokol (MIT) ───────────────────────────────────────────────────────────────
# Single-header C libraries: sokol_app + sokol_gfx + sokol_glue + sokol_log
# + util/sokol_imgui.  No build step needed — headers only.
# Backend is selected per-platform by the example's CMakeLists:
#   macOS   → SOKOL_METAL   (Cocoa + Metal, compiled as Obj-C++)
#   Windows → SOKOL_D3D11
#   Linux   → SOKOL_GLCORE
#   Web     → SOKOL_WGPU    (Emscripten + WebGPU)
find_path(SOKOL_INCLUDE_DIR NAMES sokol_app.h
    HINTS ${sokol_SOURCE_DIR}
    NO_DEFAULT_PATH
)
if (NOT SOKOL_INCLUDE_DIR)
    message(STATUS "[FetchContent] Sokol (single-header C libraries)")
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

# ── SQLCipher (optional — encrypted SQLite) ───────────────────────────────────
# Enable with:  cmake -DDATAGRID_USE_SQLCIPHER=ON ..
# Requires:     brew install sqlcipher    (macOS / Homebrew)
#               apt install libsqlcipher-dev  (Debian/Ubuntu)
option(DATAGRID_USE_SQLCIPHER "Build with SQLCipher encrypted-SQLite support" OFF)

if (DATAGRID_USE_SQLCIPHER)
    find_library(SQLCIPHER_LIBRARY
        NAMES sqlcipher
        HINTS
            /opt/homebrew/opt/sqlcipher/lib
            /opt/homebrew/lib
            /usr/local/opt/sqlcipher/lib
            /usr/lib /usr/local/lib
    )
    find_path(SQLCIPHER_INCLUDE_DIR
        NAMES sqlite3.h
        HINTS
            /opt/homebrew/opt/sqlcipher/include/sqlcipher
            /opt/homebrew/include/sqlcipher
            /usr/local/opt/sqlcipher/include/sqlcipher
            /usr/include/sqlcipher
    )

    if (SQLCIPHER_LIBRARY AND SQLCIPHER_INCLUDE_DIR)
        message(STATUS "[SQLCipher] Library : ${SQLCIPHER_LIBRARY}")
        message(STATUS "[SQLCipher] Include : ${SQLCIPHER_INCLUDE_DIR}")
        # Tell SQLiteCpp to use our external SQLite (= SQLCipher) rather than its bundled one
        set(SQLITECPP_INTERNAL_SQLITE  OFF CACHE BOOL "" FORCE)
        set(SQLite3_LIBRARIES    "${SQLCIPHER_LIBRARY}"    CACHE FILEPATH "" FORCE)
        set(SQLite3_INCLUDE_DIRS "${SQLCIPHER_INCLUDE_DIR}" CACHE PATH    "" FORCE)
        set(DATAGRID_HAVE_SQLCIPHER TRUE)
    else()
        message(WARNING "[SQLCipher] Library or headers not found. "
                        "Install via 'brew install sqlcipher' and re-run cmake. "
                        "Falling back to plain SQLite (no encryption).")
        set(DATAGRID_USE_SQLCIPHER OFF CACHE BOOL "" FORCE)
    endif()
endif()

# ── SQLiteCpp (MIT) ───────────────────────────────────────────────────────────
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

# ── nlohmann/json (MIT) ───────────────────────────────────────────────────────
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

# ── datagrid_adapters ─────────────────────────────────────────────────────────
# Renderer-agnostic adapter layer — no windowing or graphics dependency.
add_library(datagrid_adapters STATIC
    adapters/adapter_registry.cpp
    adapters/csv/csv_adapter.cpp
    adapters/sqlite/sqlite_adapter.cpp
    adapters/filesystem/filesystem_adapter.cpp
    adapters/utils/type_inferrer.cpp
)

target_include_directories(datagrid_adapters
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(datagrid_adapters PUBLIC cxx_std_23)

target_link_libraries(datagrid_adapters PUBLIC SQLiteCpp)

if (DATAGRID_HAVE_SQLCIPHER)
    target_compile_definitions(datagrid_adapters PUBLIC HAVE_SQLCIPHER)
    target_include_directories(datagrid_adapters PRIVATE ${SQLCIPHER_INCLUDE_DIR})
endif()

# ── imgui_datagrid_lib ────────────────────────────────────────────────────────
# Drop-in Dear ImGui extension — two files, no windowing/rendering dependency.
add_library(imgui_datagrid_lib STATIC imgui_datagrid.cpp)

target_include_directories(imgui_datagrid_lib
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(imgui_datagrid_lib
    PUBLIC IMGUI_DATAGRID_USE_JSON
)

target_compile_features(imgui_datagrid_lib PUBLIC cxx_std_23)

target_link_libraries(imgui_datagrid_lib
    PUBLIC
        imgui
        nlohmann_json::nlohmann_json
)

# ── Example application ───────────────────────────────────────────────────────
add_subdirectory(examples/databrowser)

# ── Tests ─────────────────────────────────────────────────────────────────────
if (NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    add_subdirectory(tests)
endif()
