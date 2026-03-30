# =============================================================================
#  tests/CMakeLists.txt
#
#  Builds the test binary (datagrid_tests) that links ONLY against:
#    • datagrid_adapters  (SQLiteAdapter, CsvAdapter, AdapterRegistry,
#                          TypeInferrer — no imgui, no raylib)
#    • Catch2 v3
#
#  datagrid_adapters is defined in the root CMakeLists and carries its
#  PUBLIC include directory (the repo root), so all test sources can use:
#
#    #include "adapters/csv/CsvAdapter.hpp"
#    #include "adapters/sqlite/SQLiteAdapter.hpp"
#    #include "adapters/utils/TypeInferrer.hpp"
#    #include "adapters/AdapterRegistry.hpp"
#    #include "adapters/IDataSource.hpp"
#
#  No raylib, no imgui, no rlimgui — keeps the test build fast and proves
#  the adapter layer is independent of the rendering stack.
# =============================================================================

cmake_minimum_required(VERSION 3.24)

include(FetchContent)

# ── Catch2 v3 ────────────────────────────────────────────────────────────────
find_package(Catch2 3 QUIET)
if (NOT Catch2_FOUND)
    message(STATUS "[FetchContent] Catch2 v3.6.0")
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.6.0
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(Catch2)
else()
    message(STATUS "[local] Catch2 ${Catch2_VERSION}")
endif()

# ── Test executable ───────────────────────────────────────────────────────────
add_executable(datagrid_tests
    test_type_inferrer.cpp
    test_csv_adapter.cpp
    test_sqlite_adapter.cpp
    test_adapter_registry.cpp
)

target_compile_features(datagrid_tests PRIVATE cxx_std_17)

# datagrid_adapters (defined in root CMakeLists) carries:
#   - all adapter + TypeInferrer sources already compiled
#   - PUBLIC include dir → repo root (so all #include "adapters/..." paths resolve)
#   - PUBLIC link dep  → SQLiteCpp
target_link_libraries(datagrid_tests
    PRIVATE
        datagrid_adapters       # adapter layer — no imgui, no raylib
        Catch2::Catch2WithMain
)

# ── CTest integration ─────────────────────────────────────────────────────────
include(CTest)
include(Catch)
catch_discover_tests(datagrid_tests)
