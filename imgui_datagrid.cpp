#include "imgui_datagrid.hpp"

#include <algorithm>
#include <cstdio>
#include <format>

#ifdef IMGUI_DATAGRID_USE_JSON
#  include <fstream>
#endif

namespace ImGuiExt {

// ============================================================
//  Internal helpers
// ============================================================

/// Draw a single cell using the column's type-specific default rendering.
/// ImGui tables already set a per-column clip rect, so long strings are
/// clipped visually without extra PushClipRect calls.  We just show a
/// tooltip when the text would overflow the available column width.
static void DrawCellDefault(const ColumnDef& col, const std::string& value)
{
    const float colW  = ImGui::GetContentRegionAvail().x;
    const float textW = ImGui::CalcTextSize(value.c_str()).x;

    // Right-align numbers and dates when text fits within the column
    if (col.type == ColumnType::Number || col.type == ColumnType::Date) {
        const float offset = colW - textW;
        if (offset > 0.0f)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
    }

    ImGui::TextUnformatted(value.c_str());

    // Show full value in a tooltip when text is clipped by the column width
    if (textW > colW && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
        ImGui::TextUnformatted(value.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

static void DrawCell(const ColumnDef& col, const std::string& value, int rowIdx)
{
    if (col.renderer)
        col.renderer(value, rowIdx);
    else
        DrawCellDefault(col, value);
}

// ============================================================
//  DataGrid
// ============================================================

bool DataGrid(
    std::vector<ColumnDef>&                         columns,
    const std::vector<std::vector<std::string>>&    rows,
    DataGridState&                                  state,
    const DataGridOptions&                          options)
{
    // Reset one-frame signals
    state.sortChanged      = false;
    state.selectionChanged = false;

    // ── Count visible columns ────────────────────────────────────────────────
    int visibleCount = 0;
    for (const auto& c : columns)
        if (c.visible) ++visibleCount;

    if (visibleCount == 0) {
        ImGui::TextDisabled("(no visible columns)");
        return false;
    }

    // ── Table flags ──────────────────────────────────────────────────────────
    const ImGuiTableFlags flags =
        ImGuiTableFlags_Borders     |
        ImGuiTableFlags_RowBg       |
        ImGuiTableFlags_Resizable   |
        ImGuiTableFlags_Reorderable |
        ImGuiTableFlags_Hideable    |
        ImGuiTableFlags_Sortable    |
        ImGuiTableFlags_ScrollY     |
        options.extraFlags;

    const float height = (options.maxHeight > 0.0f)
        ? options.maxHeight
        : ImGui::GetContentRegionAvail().y;

    bool changed = false;

    if (!ImGui::BeginTable(options.id, visibleCount, flags, ImVec2(0.0f, height)))
        return false;

    // ── Column setup ─────────────────────────────────────────────────────────
    if (options.stickyHeader)
        ImGui::TableSetupScrollFreeze(0, 1);

    for (const auto& col : columns) {
        if (!col.visible) continue;

        ImGuiTableColumnFlags cf = col.extraFlags;
        if (!col.sortable)       cf |= ImGuiTableColumnFlags_NoSort;
        if (!col.resizable)      cf |= ImGuiTableColumnFlags_NoResize;
        if (col.initWidth > 0.0f) cf |= ImGuiTableColumnFlags_WidthFixed;

        ImGui::TableSetupColumn(col.label.c_str(), cf, col.initWidth);
    }

    ImGui::TableHeadersRow();

    // ── Sort spec handling ───────────────────────────────────────────────────
    // Map the ImGui visible-column index back to our ColumnDef key.
    if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs()) {
        if (specs->SpecsDirty && specs->SpecsCount > 0) {
            const int targetVisIdx = (int)specs->Specs[0].ColumnIndex;
            int visIdx = 0;
            for (const auto& col : columns) {
                if (!col.visible) continue;
                if (visIdx == targetVisIdx) {
                    const bool newAsc =
                        (specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending);
                    if (col.key != state.sortColumnKey || newAsc != state.sortAscending) {
                        state.sortColumnKey = col.key;
                        state.sortAscending = newAsc;
                        state.sortChanged   = true;
                        changed             = true;
                    }
                    break;
                }
                ++visIdx;
            }
            specs->SpecsDirty = false;
        }
    }

    // ── Rows — virtual scrolling via ImGuiListClipper ─────────────────────
    ImGuiListClipper clipper;
    clipper.Begin((int)rows.size());

    while (clipper.Step()) {
        for (int rowIdx = clipper.DisplayStart; rowIdx < clipper.DisplayEnd; ++rowIdx) {
            const auto& row           = rows[rowIdx];
            const bool  isEditingRow  = (state.editingRow == rowIdx);

            ImGui::TableNextRow(ImGuiTableRowFlags_None, options.minRowHeight);

            bool firstVisible = true;
            int  visColIdx    = 0;

            for (size_t c = 0; c < columns.size(); ++c) {
                const ColumnDef& col = columns[c];
                if (!col.visible) continue;

                ImGui::TableSetColumnIndex(visColIdx++);

                // ── First visible column: row-spanning Selectable ────────────
                if (firstVisible) {
                    firstVisible = false;

                    if (options.rowSelection && !isEditingRow) {
                        const bool isSel  = (state.selectedRow == rowIdx);
                        auto selId = std::format("##row{}", rowIdx);

                        constexpr ImGuiSelectableFlags sf =
                            ImGuiSelectableFlags_SpanAllColumns |
                            ImGuiSelectableFlags_AllowOverlap;

                        if (ImGui::Selectable(selId.c_str(), isSel, sf, ImVec2(0.0f, 0.0f))) {
                            if (state.selectedRow != rowIdx) {
                                state.selectedRow      = rowIdx;
                                state.selectionChanged = true;
                                changed                = true;
                                if (options.onRowClick)
                                    options.onRowClick(rowIdx);
                            }
                        }

                        // Double-click (row level — only fires if no editable cell captured it)
                        if (ImGui::IsItemHovered() &&
                            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
                            state.editingRow == -1)
                        {
                            if (options.onRowDblClick)
                                options.onRowDblClick(rowIdx);
                        }

                        // Right-click context menu
                        if (options.contextMenu) {
                            auto ctxId = std::format("##ctx{}", rowIdx);
                            if (ImGui::BeginPopupContextItem(ctxId.c_str())) {
                                options.contextMenu(rowIdx);
                                ImGui::EndPopup();
                            }
                        }

                        ImGui::SameLine();
                    }
                }

                const std::string& cellVal = (c < row.size()) ? row[c] : "";

                // ── Inline editor ────────────────────────────────────────────
                if (isEditingRow && state.editingCol == (int)c) {
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                    if (!state.editFocusDone) {
                        ImGui::SetKeyboardFocusHere();
                        state.editFocusDone = true;
                    }
                    const bool committed =
                        ImGui::InputText("##celled", state.editBuf, sizeof(state.editBuf),
                                         ImGuiInputTextFlags_EnterReturnsTrue |
                                         ImGuiInputTextFlags_AutoSelectAll);
                    if (committed || ImGui::IsItemDeactivatedAfterEdit()) {
                        if (options.onCellEdit)
                            options.onCellEdit(rowIdx, (int)c, state.editBuf);
                        state.editingRow    = -1;
                        state.editingCol    = -1;
                        state.editFocusDone = false;
                        changed             = true;
                    } else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                        state.editingRow    = -1;
                        state.editingCol    = -1;
                        state.editFocusDone = false;
                    }
                } else {
                    // ── Normal cell render ───────────────────────────────────
                    DrawCell(col, cellVal, rowIdx);

                    // ── Per-column cell action callbacks ─────────────────────
                    // These fire before the generic row callbacks, so a
                    // "navigate on click" column action doesn't also trigger
                    // row selection on the same click.
                    if ((col.onCellClick || col.onCellDblClick) &&
                        ImGui::IsItemHovered())
                    {
                        if (col.onCellClick &&
                            ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                        {
                            col.onCellClick(cellVal, rowIdx);
                        }
                        if (col.onCellDblClick &&
                            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        {
                            col.onCellDblClick(cellVal, rowIdx);
                        }
                    }

                    // Detect double-click on editable column → start editing
                    if (col.editable && options.onCellEdit && state.editingRow == -1 &&
                        ImGui::IsItemHovered() &&
                        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    {
                        state.editingRow    = rowIdx;
                        state.editingCol    = (int)c;
                        state.editFocusDone = false;
                        std::snprintf(state.editBuf, sizeof(state.editBuf),
                                      "%s", cellVal.c_str());
                    }
                }
            }
        }
    }

    clipper.End();
    ImGui::EndTable();
    return changed;
}

// ============================================================
//  DataGridColumnVisibility
// ============================================================

void DataGridColumnVisibility(std::vector<ColumnDef>& columns)
{
    for (auto& col : columns) {
        const char* lbl = col.label.empty() ? col.key.c_str() : col.label.c_str();
        ImGui::Checkbox(lbl, &col.visible);
    }
}

// ============================================================
//  DataGridPagination
// ============================================================

bool DataGridPagination(int& currentPage, int rowsPerPage, int totalRows)
{
    bool changed = false;

    ImGui::Separator();

    // ── Previous ────────────────────────────────────────────────────────────
    const bool canPrev = (currentPage > 0);
    if (!canPrev) ImGui::BeginDisabled();
    if (ImGui::ArrowButton("##pgprev", ImGuiDir_Left)) {
        --currentPage;
        changed = true;
    }
    if (!canPrev) ImGui::EndDisabled();

    ImGui::SameLine();

    // ── Label ───────────────────────────────────────────────────────────────
    if (totalRows >= 0) {
        // Avoid divide-by-zero; clamp rowsPerPage to at least 1
        const int rpp      = (rowsPerPage > 0) ? rowsPerPage : 1;
        const int lastPage = (totalRows > 0) ? ((totalRows - 1) / rpp) : 0;
        ImGui::Text("%s", std::format("Page {} / {}  ({} rows)", currentPage + 1, lastPage + 1, totalRows).c_str());
    } else {
        ImGui::Text("%s", std::format("Page {}", currentPage + 1).c_str());
    }

    ImGui::SameLine();

    // ── Next ─────────────────────────────────────────────────────────────────
    bool canNext = true;
    if (totalRows >= 0) {
        const int rpp      = (rowsPerPage > 0) ? rowsPerPage : 1;
        const int lastPage = (totalRows > 0) ? ((totalRows - 1) / rpp) : 0;
        canNext = (currentPage < lastPage);
    }

    if (!canNext) ImGui::BeginDisabled();
    if (ImGui::ArrowButton("##pgnext", ImGuiDir_Right)) {
        ++currentPage;
        changed = true;
    }
    if (!canNext) ImGui::EndDisabled();

    return changed;
}

// ============================================================
//  Optional: nlohmann/json layout persistence
// ============================================================
#ifdef IMGUI_DATAGRID_USE_JSON

nlohmann::json DataGridSaveLayout(
    const std::vector<ColumnDef>& columns,
    const DataGridState&          state)
{
    nlohmann::json j;
    j["sortColumn"]    = state.sortColumnKey;
    j["sortAscending"] = state.sortAscending;
    j["selectedRow"]   = state.selectedRow;

    auto& jcols = j["columns"] = nlohmann::json::array();
    for (const auto& col : columns) {
        jcols.push_back({
            { "key",       col.key        },
            { "visible",   col.visible    },
            { "initWidth", col.initWidth  },
        });
    }
    return j;
}

void DataGridLoadLayout(
    std::vector<ColumnDef>& columns,
    DataGridState&          state,
    const nlohmann::json&   j)
{
    if (j.contains("sortColumn"))    state.sortColumnKey = j["sortColumn"].get<std::string>();
    if (j.contains("sortAscending")) state.sortAscending = j["sortAscending"].get<bool>();
    if (j.contains("selectedRow"))   state.selectedRow   = j["selectedRow"].get<int>();

    if (!j.contains("columns")) return;
    for (const auto& jcol : j["columns"]) {
        const std::string key = jcol.value("key", "");
        for (auto& col : columns) {
            if (col.key != key) continue;
            col.visible   = jcol.value("visible",   true);
            col.initWidth = jcol.value("initWidth", 0.0f);
            break;
        }
    }
}

bool DataGridSaveLayoutToFile(
    const std::vector<ColumnDef>& columns,
    const DataGridState&          state,
    const std::string&            filepath)
{
    try {
        std::ofstream f(filepath);
        if (!f.is_open()) return false;
        f << DataGridSaveLayout(columns, state).dump(2);
        return f.good();
    } catch (...) {
        return false;
    }
}

bool DataGridLoadLayoutFromFile(
    std::vector<ColumnDef>& columns,
    DataGridState&          state,
    const std::string&      filepath)
{
    try {
        std::ifstream f(filepath);
        if (!f.is_open()) return false;
        nlohmann::json j;
        f >> j;
        DataGridLoadLayout(columns, state, j);
        return true;
    } catch (...) {
        return false;
    }
}

#endif // IMGUI_DATAGRID_USE_JSON

} // namespace ImGuiExt
