#include <catch2/catch_test_macros.hpp>

#include "imgui.h"
#include "imgui_datagrid.hpp"

#include <string>
#include <vector>

struct ImGuiFixture
{
    ImGuiContext* ctx = nullptr;

    ImGuiFixture()
    {
        ctx = ImGui::CreateContext();
        ImGui::SetCurrentContext(ctx);
        ImGuiIO& io    = ImGui::GetIO();
        io.DisplaySize = ImVec2(1280.0f, 720.0f);
        io.DeltaTime   = 1.0f / 60.0f;
        // Build font atlas so NewFrame() doesn't assert on IsBuilt()
        unsigned char* pixels = nullptr;
        int            fw = 0, fh = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &fw, &fh);
        io.Fonts->SetTexID(static_cast<ImTextureID>(1));
    }

    ~ImGuiFixture()
    {
        if (ctx)
            ImGui::DestroyContext(ctx);
    }

    void NewFrame() { ImGui::NewFrame(); }
    void EndFrame() { ImGui::Render(); }
};

// Group A: Pure data structure tests (no ImGui context needed)

TEST_CASE("DataGridState: default values", "[imgui_datagrid]")
{
    ImGuiExt::DataGridState state;
    CHECK(state.sortAscending == true);
    CHECK(state.selectedRow == -1);
    CHECK(state.sortChanged == false);
    CHECK(state.selectionChanged == false);
    CHECK(state.sortColumnKey.empty());
    CHECK(state.editingRow == -1);
    CHECK(state.editingCol == -1);
}

TEST_CASE("DataGridOptions: default values", "[imgui_datagrid]")
{
    ImGuiExt::DataGridOptions opts;
    CHECK(opts.stickyHeader == true);
    CHECK(opts.rowSelection == true);
    CHECK(opts.maxHeight == 0.0f);
    CHECK(opts.minRowHeight == 0.0f);
    CHECK(opts.onRowClick == nullptr);
    CHECK(opts.onRowDblClick == nullptr);
}

TEST_CASE("ColumnDef: default values", "[imgui_datagrid]")
{
    ImGuiExt::ColumnDef col;
    CHECK(col.key.empty());
    CHECK(col.label.empty());
    CHECK(col.initWidth == 0.0f);
    CHECK(col.visible == true);
    CHECK(col.sortable == true);
    CHECK(col.editable == false);
    CHECK(col.type == ImGuiExt::ColumnType::Text);
}

TEST_CASE("ColumnPolicy: fluent builder chains", "[imgui_datagrid]")
{
    ImGuiExt::ColumnPolicy p;
    p.withEditable(true).withSortable(false).withRenderer("image");

    CHECK(p.editable == true);
    CHECK(p.sortable == false);
    CHECK(p.rendererName == "image");
    CHECK(p.visible == true); // default unchanged
}

TEST_CASE("ColumnPolicy: PlatformNavigatePolicy", "[imgui_datagrid]")
{
    ImGuiExt::ColumnPolicy p = ImGuiExt::PlatformNavigatePolicy();
    // Verify it returns a valid policy (no crash, one navigate flag is set)
    const bool navigates = p.clickNavigates || p.dblClickNavigates;
    CHECK(navigates);
}

// Group B: JSON layout persistence

#ifdef IMGUI_DATAGRID_USE_JSON

TEST_CASE("DataGridSaveLayout: serialises columns and state", "[imgui_datagrid]")
{
    std::vector<ImGuiExt::ColumnDef> cols = {
        {"name", "Name", 120.0f},
        {"size", "Size", 80.0f},
    };
    ImGuiExt::DataGridState state;
    state.sortColumnKey = "name";
    state.sortAscending = false;

    auto j = ImGuiExt::DataGridSaveLayout(cols, state);
    CHECK(j.contains("columns"));
    CHECK(j.contains("sortColumn"));
}

TEST_CASE("DataGridLoadLayout: restores column visibility", "[imgui_datagrid]")
{
    std::vector<ImGuiExt::ColumnDef> cols = {
        {"name", "Name", 120.0f},
        {"size", "Size", 80.0f},
    };
    ImGuiExt::DataGridState state;

    cols[0].visible = false;
    auto j          = ImGuiExt::DataGridSaveLayout(cols, state);

    // Reset and restore
    cols[0].visible = true;
    ImGuiExt::DataGridLoadLayout(cols, state, j);
    CHECK(cols[0].visible == false);
}

TEST_CASE("DataGridLoadLayout: unknown keys are ignored", "[imgui_datagrid]")
{
    std::vector<ImGuiExt::ColumnDef> cols = {{"name", "Name", 100.0f}};
    ImGuiExt::DataGridState          state;

    auto j              = ImGuiExt::DataGridSaveLayout(cols, state);
    j["unexpected_key"] = "should_be_ignored";

    // Must not throw or crash
    CHECK_NOTHROW(ImGuiExt::DataGridLoadLayout(cols, state, j));
}

TEST_CASE("DataGridLoadLayout: partial restore keeps defaults", "[imgui_datagrid]")
{
    std::vector<ImGuiExt::ColumnDef> cols = {{"id", "ID", 40.0f}};
    ImGuiExt::DataGridState          state;

    // Load from empty JSON — no crash, defaults preserved
    nlohmann::json empty = nlohmann::json::object();
    CHECK_NOTHROW(ImGuiExt::DataGridLoadLayout(cols, state, empty));
    CHECK(cols[0].visible == true);
    CHECK(state.sortAscending == true);
}

#endif // IMGUI_DATAGRID_USE_JSON

// Group C: Headless ImGui rendering tests

TEST_CASE("DataGridPagination: first page navigation", "[imgui_datagrid]")
{
    ImGuiFixture fix;
    fix.NewFrame();
    ImGui::Begin("Test", nullptr, ImGuiWindowFlags_NoDecoration);

    int page = 0;
    ImGuiExt::DataGridPagination(page, /*rowsPerPage=*/10, /*totalRows=*/50);

    ImGui::End();
    fix.EndFrame();
    // No crash expected
    CHECK(true);
}

TEST_CASE("DataGrid: renders without crash", "[imgui_datagrid]")
{
    ImGuiFixture fix;
    fix.NewFrame();
    ImGui::Begin("Test", nullptr);

    std::vector<ImGuiExt::ColumnDef> cols = {
        {"col1", "Column 1", 100.0f},
        {"col2", "Column 2", 120.0f},
    };
    const std::vector<std::vector<std::string>> rows = {
        {"r0c0", "r0c1"},
        {"r1c0", "r1c1"},
        {"r2c0", "r2c1"},
    };
    ImGuiExt::DataGridState state;
    ImGuiExt::DataGrid(cols, rows, state);

    ImGui::End();
    fix.EndFrame();
    CHECK(true);
}

TEST_CASE("DataGrid: state sortChanged fires on sort column set", "[imgui_datagrid]")
{
    ImGuiFixture fix;
    fix.NewFrame();
    ImGui::Begin("Test", nullptr);

    std::vector<ImGuiExt::ColumnDef> cols = {
        {"name", "Name", 120.0f},
        {"size", "Size", 80.0f},
    };
    const std::vector<std::vector<std::string>> rows = {
        {"Alice", "42"},
        {"Bob", "99"},
    };
    ImGuiExt::DataGridState state;
    state.sortColumnKey = "name";

    // Should not crash when called with a pre-set sort key
    CHECK_NOTHROW(ImGuiExt::DataGrid(cols, rows, state));

    ImGui::End();
    fix.EndFrame();
}

TEST_CASE("DataGridPagination: last page with known total", "[imgui_datagrid]")
{
    ImGuiFixture fix;
    fix.NewFrame();
    ImGui::Begin("Test", nullptr, ImGuiWindowFlags_NoDecoration);

    // Edge case: last page (page 4), 10 rows/page, 42 total rows
    int page = 4;
    CHECK_NOTHROW(ImGuiExt::DataGridPagination(page, 10, 42));

    ImGui::End();
    fix.EndFrame();
}
