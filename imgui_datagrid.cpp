#include "imgui_datagrid.hpp"
#include "compat/move_only_function.hpp"

#include <functional>
#include <cstdio>
#include <format>
#include <fstream>

namespace ImGuiExt {

// Deferred callbacks fired after EndTable()
using DeferredFn = compat::move_only_function<void() noexcept>;

static void DrawCellDefault(const ColumnDef& col, const std::string& value)
{
    const float colW  = ImGui::GetContentRegionAvail().x;
    const float textW = ImGui::CalcTextSize(value.c_str()).x;

    if (col.type == ColumnType::Number || col.type == ColumnType::Date) {
        const float offset = colW - textW;
        if (offset > 0.0f)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
    }

    ImGui::TextUnformatted(value.c_str());

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


bool DataGrid(std::vector<ColumnDef>&                      columns,
              const std::vector<std::vector<std::string>>& rows,
              DataGridState&                               state,
              const DataGridOptions&                       options)
{
    state.sortChanged      = false;
    state.selectionChanged = false;

    int visibleCount = 0;
    for (const auto& c : columns)
        if (c.visible)
            ++visibleCount;

    if (visibleCount == 0) {
        ImGui::TextDisabled("(no visible columns)");
        return false;
    }

    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                                  ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_Sortable |
                                  ImGuiTableFlags_ScrollY | options.extraFlags;

    const float height = (options.maxHeight > 0.0f) ? options.maxHeight : ImGui::GetContentRegionAvail().y;

    bool changed = false;

    std::vector<DeferredFn> deferred;

    if (!ImGui::BeginTable(options.id, visibleCount, flags, ImVec2(0.0f, height)))
        return false;

    if (options.stickyHeader)
        ImGui::TableSetupScrollFreeze(0, 1);

    for (const auto& col : columns) {
        if (!col.visible)
            continue;

        ImGuiTableColumnFlags cf = col.extraFlags;
        if (!col.sortable)
            cf |= ImGuiTableColumnFlags_NoSort;
        if (!col.resizable)
            cf |= ImGuiTableColumnFlags_NoResize;
        if (col.initWidth > 0.0f)
            cf |= ImGuiTableColumnFlags_WidthFixed;

        ImGui::TableSetupColumn(col.label.c_str(), cf, col.initWidth);
    }


    if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs()) {
        if (specs->SpecsDirty && specs->SpecsCount > 0) {
            const int targetVisIdx = specs->Specs[0].ColumnIndex;
            int       visIdx       = 0;
            for (const auto& col : columns) {
                if (!col.visible)
                    continue;
                if (visIdx == targetVisIdx) {
                    const bool newAsc = (specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending);
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

    // Ctrl/Cmd+A: select all rows on the current page.
    {
        const ImGuiIO& io  = ImGui::GetIO();
        const bool     mod = io.KeyCtrl || io.KeySuper;
        if (mod && ImGui::IsKeyPressed(ImGuiKey_A, /*repeat=*/false) &&
            ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
            state.anchorRow     = 0;
            state.anchorCol     = 0;
            state.extentRow     = static_cast<int>(rows.size() - 1);
            state.extentCol     = static_cast<int>(columns.size() - 1);
            state.selectAllRows = true;
            state.selectionChanged = true;
            changed                = true;
            if (options.onCellSelectionChanged)
                options.onCellSelectionChanged(0, 0, state.extentRow, state.extentCol);
        }
    }

    // Manual header row — functionally identical to TableHeadersRow() but also
    // detects left-clicks on individual column headers for whole-column selection.
    int headerClickedCol = -1;
    {
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        int vh = 0;
        for (int ci = 0; ci < static_cast<int>(columns.size()); ++ci) {
            if (!columns[ci].visible) continue;
            ImGui::TableSetColumnIndex(vh++);
            ImGui::TableHeader(columns[ci].label.c_str());
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                headerClickedCol = ci;
        }
    }
    if (headerClickedCol >= 0) {
        state.anchorRow        = 0;
        state.anchorCol        = headerClickedCol;
        state.extentRow        = (int)rows.size() - 1;
        state.extentCol        = headerClickedCol;
        state.selectAllRows    = false;
        state.selectionChanged = true;
        changed                = true;
        if (options.onCellSelectionChanged)
            options.onCellSelectionChanged(0, headerClickedCol, state.extentRow, headerClickedCol);
    }

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows.size()));

    while (clipper.Step()) {
        for (int rowIdx = clipper.DisplayStart; rowIdx < clipper.DisplayEnd; ++rowIdx) {
            const auto& row          = rows[rowIdx];
            const bool  isEditingRow = (state.editingRow == rowIdx);

            ImGui::TableNextRow(ImGuiTableRowFlags_None, options.minRowHeight);

            bool firstVisible = true;
            int  visColIdx    = 0;

            for (size_t c = 0; c < columns.size(); ++c) {
                const ColumnDef& col = columns[c];
                if (!col.visible)
                    continue;

                ImGui::TableSetColumnIndex(visColIdx++);

                // Cell selection highlight (drawn before content).
                if (state.IsCellSelected(rowIdx, static_cast<int>(c)))
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg,
                                          ImGui::GetColorU32(ImGuiCol_TextSelectedBg));

                if (firstVisible) {
                    firstVisible = false;

                    if (options.rowSelection && !isEditingRow) {
                        const bool isSel = (state.selectedRow == rowIdx);
                        auto       selId = std::format("##row{}", rowIdx);

                        constexpr ImGuiSelectableFlags sf =
                            ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap;

                        if (ImGui::Selectable(selId.c_str(), isSel, sf, ImVec2(0.0f, 0.0f))) {
                            if (state.selectedRow != rowIdx) {
                                state.selectedRow      = rowIdx;
                                state.selectionChanged = true;
                                changed                = true;
                                if (options.onRowClick) {
                                    deferred.emplace_back([cb = options.onRowClick, rowIdx]() noexcept { cb(rowIdx); });
                                }
                            }
                        }

                        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
                            state.editingRow == -1) {
                            if (options.onRowDblClick) {
                                deferred.emplace_back([cb = options.onRowDblClick, rowIdx]() noexcept { cb(rowIdx); });
                            }
                        }

                        if (options.contextMenu) {
                            auto ctxId = std::format("##ctx{}", rowIdx);
                            if (ImGui::BeginPopupContextItem(ctxId.c_str())) {
                                options.contextMenu(rowIdx);
                                ImGui::EndPopup();
                            }
                        }

                        // Row drag-and-drop source.
                        // BeginDragDropSource() is called on the Selectable item
                        // (the last rendered widget).  The callback is responsible
                        // for calling ImGui::SetDragDropPayload() and may also
                        // render a custom drag-preview tooltip.
                        if (options.onRowDragSource) {
                            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                                options.onRowDragSource(rowIdx);
                                ImGui::EndDragDropSource();
                            }
                        }

                        ImGui::SameLine();
                    }
                }

                const std::string& cellVal = (c < row.size()) ? row[c] : "";

                if (isEditingRow && state.editingCol == static_cast<int>(c)) {
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                    if (!state.editFocusDone) {
                        ImGui::SetKeyboardFocusHere();
                        state.editFocusDone = true;
                    }
                    const bool committed =
                        ImGui::InputText("##celled",
                                         state.editBuf,
                                         sizeof(state.editBuf),
                                         ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                    if (committed || ImGui::IsItemDeactivatedAfterEdit()) {
                        if (options.onCellEdit)
                            options.onCellEdit(rowIdx, static_cast<int>(c), state.editBuf);
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
                    DrawCell(col, cellVal, rowIdx);

                    if (ImGui::IsItemHovered()) {
                        auto dispatch = [&](const CellAction& action, bool triggered) {
                            if (!triggered)
                                return;
                            std::visit(overloaded{
                                           [](const CellNoAction&) {},
                                           [](const CellNavigate&) {},
                                           [](const CellOpen&) {},
                                           [&](const CellCustom& ca) {
                                               if (ca.fn)
                                                   ca.fn(cellVal, rowIdx);
                                           },
                                       },
                                       action);
                        };

                        dispatch(col.onClick, ImGui::IsMouseClicked(ImGuiMouseButton_Left));
                        dispatch(col.onDblClick, ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left));
                    }

                    if (col.editable && options.onCellEdit && state.editingRow == -1 && ImGui::IsItemHovered() &&
                        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        state.editingRow    = rowIdx;
                        state.editingCol    = static_cast<int>(c);
                        state.editFocusDone = false;
                        std::snprintf(state.editBuf, sizeof(state.editBuf), "%s", cellVal.c_str());
                    }

                    // Cell-click selection: Shift+click extends the rectangle.
                    if (state.editingRow == -1 && ImGui::IsItemHovered() &&
                        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                        const bool shift = ImGui::GetIO().KeyShift;
                        if (!shift || state.anchorRow < 0 || state.anchorCol < 0) {
                            state.anchorRow = rowIdx;
                            state.anchorCol = static_cast<int>(c);
                            state.extentRow = rowIdx;
                            state.extentCol = static_cast<int>(c);
                        } else {
                            state.extentRow = rowIdx;
                            state.extentCol = static_cast<int>(c);
                        }
                        state.selectAllRows    = false;
                        state.selectionChanged = true;
                        changed                = true;
                        if (options.onCellSelectionChanged) {
                            const int r0 = state.anchorRow < state.extentRow ? state.anchorRow : state.extentRow;
                            const int c0 = state.anchorCol < state.extentCol ? state.anchorCol : state.extentCol;
                            const int r1 = state.anchorRow > state.extentRow ? state.anchorRow : state.extentRow;
                            const int c1 = state.anchorCol > state.extentCol ? state.anchorCol : state.extentCol;
                            options.onCellSelectionChanged(r0, c0, r1, c1);
                        }
                    }
                }
            }
        }
    }

    clipper.End();
    ImGui::EndTable();

    for (auto& fn : deferred) {
        fn();
    }

    return changed;
}


void DataGridColumnVisibility(std::vector<ColumnDef>& columns)
{
    for (auto& col : columns) {
        const char* lbl = col.label.empty() ? col.key.c_str() : col.label.c_str();
        ImGui::Checkbox(lbl, &col.visible);
    }
}


bool DataGridPagination(int& currentPage, int rowsPerPage, int totalRows)
{
    bool changed = false;

    ImGui::Separator();

    const bool canPrev = (currentPage > 0);
    if (!canPrev)
        ImGui::BeginDisabled();
    if (ImGui::ArrowButton("##pgprev", ImGuiDir_Left)) {
        --currentPage;
        changed = true;
    }
    if (!canPrev)
        ImGui::EndDisabled();

    ImGui::SameLine();

    if (totalRows >= 0) {
        const int rpp      = (rowsPerPage > 0) ? rowsPerPage : 1;
        const int lastPage = (totalRows > 0) ? ((totalRows - 1) / rpp) : 0;
        ImGui::Text("%s", std::format("Page {} / {}  ({} rows)", currentPage + 1, lastPage + 1, totalRows).c_str());
    } else {
        ImGui::Text("%s", std::format("Page {}", currentPage + 1).c_str());
    }

    ImGui::SameLine();

    bool canNext = true;
    if (totalRows >= 0) {
        const int rpp      = (rowsPerPage > 0) ? rowsPerPage : 1;
        const int lastPage = (totalRows > 0) ? ((totalRows - 1) / rpp) : 0;
        canNext            = (currentPage < lastPage);
    }

    if (!canNext)
        ImGui::BeginDisabled();
    if (ImGui::ArrowButton("##pgnext", ImGuiDir_Right)) {
        ++currentPage;
        changed = true;
    }
    if (!canNext)
        ImGui::EndDisabled();

    return changed;
}

#ifdef IMGUI_DATAGRID_USE_JSON

nlohmann::json DataGridSaveLayout(const std::vector<ColumnDef>& columns, const DataGridState& state)
{
    nlohmann::json j;
    j["sortColumn"]    = state.sortColumnKey;
    j["sortAscending"] = state.sortAscending;
    j["selectedRow"]   = state.selectedRow;

    auto& jcols = j["columns"] = nlohmann::json::array();
    for (const auto& col : columns) {
        jcols.push_back({
            {"key", col.key},
            {"visible", col.visible},
            {"initWidth", col.initWidth},
        });
    }
    return j;
}

void DataGridLoadLayout(std::vector<ColumnDef>& columns, DataGridState& state, const nlohmann::json& j)
{
    if (j.contains("sortColumn"))
        state.sortColumnKey = j["sortColumn"].get<std::string>();
    if (j.contains("sortAscending"))
        state.sortAscending = j["sortAscending"].get<bool>();
    if (j.contains("selectedRow"))
        state.selectedRow = j["selectedRow"].get<int>();

    if (!j.contains("columns"))
        return;
    for (const auto& basic_json : j["columns"]) {
        const std::string key = basic_json.value("key", "");
        for (auto& col : columns) {
            if (col.key != key)
                continue;
            col.visible   = basic_json.value("visible", true);
            col.initWidth = basic_json.value("initWidth", 0.0f);
            break;
        }
    }
}

bool DataGridSaveLayoutToFile(const std::vector<ColumnDef>& columns,
                              const DataGridState&          state,
                              const std::string&            filepath)
{
    try {
        std::ofstream f(filepath);
        if (!f.is_open())
            return false;
        f << DataGridSaveLayout(columns, state).dump(2);
        return f.good();
    } catch (...) {
        return false;
    }
}

bool DataGridLoadLayoutFromFile(std::vector<ColumnDef>& columns, DataGridState& state, const std::string& filepath)
{
    try {
        std::ifstream f(filepath);
        if (!f.is_open())
            return false;
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
