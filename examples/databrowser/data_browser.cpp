#include "data_browser.hpp"
#include "adapters/data_source.hpp"
#include "ui/icons.hpp"
#include "imgui_datagrid.hpp"
#include "ui/filetype_sniffer.hpp"

#include <cstdio>
#include <cstring>
#include <format>
#include <string>

namespace datagrid {

int DataBrowser::nextInstanceId_ = 0;

DataBrowser::DataBrowser(adapters::DataSourcePtr source_, const std::string& title)
    : source(std::move(source_)), windowTitle(title)
{
    if (IsConnected()) {
        LoadSchema();
        if (!tables.empty())
            SelectTable(tables.front().name);
    }

    instanceId_ = ++nextInstanceId_;
    idSuffix_   = std::format("_{}", instanceId_);
    inspector_.SetInstanceId(instanceId_);
}

void DataBrowser::SetLayout(const ui::ResponsiveLayout& l)
{
    layout_ = l;
}

void DataBrowser::SetCodeFont(ImFont* f) noexcept
{
    hexView_.SetFont(f);
}

void DataBrowser::OpenInspector(const std::string& tableName)
{
    const std::string target = tableName.empty() ? query.table : tableName;
    if (IsConnected() && !target.empty())
        inspector_.Open(source.get(), target);
}

void DataBrowser::SetColumnCustomizer(ColumnCustomizer fn)
{
    columnCustomizer = std::move(fn);
    if (columnsReady && columnCustomizer)
        columnCustomizer(columns);
}

void DataBrowser::SetOnRowClick(RowCallback fn)
{
    onRowClick = std::move(fn);
}
void DataBrowser::SetOnRowDblClick(RowCallback fn)
{
    onRowDblClick = std::move(fn);
}

void DataBrowser::SetDataSource(adapters::DataSourcePtr newSource)
{
    source = std::move(newSource);
    tables.clear();
    columns.clear();
    rows.clear();
    query        = {};
    totalRows    = 0;
    columnsReady = false;
    schemaLoaded = false;
    needsRefresh = false;
    lastError.clear();
    statusMsg.clear();
    sqlEditor_.buf[0] = '\0';
    sqlEditor_.error.clear();
    sqlEditor_.status.clear();
    filterBuf[0] = '\0';
    searchBuf[0] = '\0';
    gridState    = {};

    if (IsConnected()) {
        LoadSchema();
        if (!tables.empty())
            SelectTable(tables.front().name);
    }
}

bool DataBrowser::IsConnected() const
{
    return source && source->IsConnected();
}

std::string DataBrowser::AdapterLabel() const
{
    return source ? source->AdapterLabel() : "(no source)";
}

std::string DataBrowser::WindowTitle() const
{
    return windowTitle;
}
void DataBrowser::SetWindowTitle(const std::string& t)
{
    windowTitle = t;
}

void DataBrowser::InvalidateData()
{
    needsRefresh = true;
}

void DataBrowser::SetPreContentHook(PreContentHook fn)
{
    preContentHook_ = std::move(fn);
}

void DataBrowser::NavigateTo(const std::string& tableOrPath)
{
    SelectTable(tableOrPath);
}

adapters::IDataSource* DataBrowser::GetSource() const
{
    return source.get();
}

bool DataBrowser::IsInspectorOpen() const
{
    return inspector_.IsOpen();
}
void DataBrowser::CloseInspector()
{
    inspector_.Close();
}

void DataBrowser::SetDragSourceCallback(DragSourceCallback fn)
{
    dragSourceCb_ = std::move(fn);
}
void DataBrowser::SetDropHandler(DropHandler fn)
{
    dropHandler_ = std::move(fn);
}
void DataBrowser::SetOpenCallback(OpenCallback fn)
{
    openCb_ = std::move(fn);
}
std::string DataBrowser::CurrentTable() const
{
    return query.table;
}

std::vector<std::string> DataBrowser::GetCurrentColumnKeys() const
{
    std::vector<std::string> keys;
    keys.reserve(columns.size());
    for (const auto& c : columns)
        keys.push_back(c.key);
    return keys;
}

std::string DataBrowser::ImGuiWindowId() const
{
    return windowTitle + "##dbw" + idSuffix_;
}

std::string DataBrowser::InspectorImGuiWindowId() const
{
    return inspector_.WindowId();
}

std::string DataBrowser::InspectorWindowLabel() const
{
    if (!IsInspectorOpen())
        return {};
    const auto& analysis = inspector_.GetAnalysis();
    return analysis.tableName.empty() ? std::string{} : "Inspector \xe2\x80\x94 " + analysis.tableName;
}

void DataBrowser::LoadSchema()
{
    if (!IsConnected())
        return;

    const auto        cats    = source->GetCatalogs();
    const std::string catalog = cats.empty() ? "" : cats.front();
    tables                    = source->GetTables(catalog);
    schemaLoaded              = true;
}

void DataBrowser::SelectTable(const std::string& tableName)
{
    if (query.table == tableName && columnsReady)
        return;

    query        = {}; // reset filters / sort / page
    query.table  = tableName;
    columnsReady = false;
    rows.clear();
    gridState    = {};
    filterBuf[0] = '\0';
    searchBuf[0] = '\0';
    filterColumn.clear();
    searchColumn.clear();

    BuildColumns();
    needsRefresh = true;
}

void DataBrowser::BuildColumns()
{
    columns.clear();
    if (!IsConnected() || query.table.empty())
        return;

    const auto infos = source->GetColumns(query.table);

    columns.reserve(infos.size());
    for (const auto& info : infos) {
        ImGuiExt::ColumnDef col;
        col.key          = info.name;
        col.label        = info.name;
        col.sortable     = true;
        col.visible      = true;
        col.semanticHint = info.displayHint;

        const std::string& t        = info.typeName;
        auto               contains = [&](const char* sub) { return t.find(sub) != std::string::npos; };

        if (contains("INT") || contains("int") || contains("REAL") || contains("real") || contains("FLOAT") ||
            contains("float") || contains("NUMERIC") || contains("numeric") || contains("DOUBLE") ||
            contains("double") || contains("NUMBER") || contains("number")) {
            col.type = ImGuiExt::ColumnType::Number;
        } else if (contains("DATE") || contains("date") || contains("TIME") || contains("time")) {
            col.type      = ImGuiExt::ColumnType::Date;
            col.initWidth = 130.0f;
        } else {
            col.type = ImGuiExt::ColumnType::Text;
        }

        if (info.primaryKey) {
            col.initWidth = 48.0f;
            col.type      = ImGuiExt::ColumnType::Number;
        }

        if (!info.displayHint.empty()) {
            col.type      = ImGuiExt::ColumnType::Custom;
            col.initWidth = 80.0f;
            col.sortable  = false;
        }

        columns.push_back(std::move(col));
    }

    for (const auto& info : infos) {
        if (!info.primaryKey && filterColumn.empty())
            filterColumn = info.name;

        if (searchColumn.empty() &&
            (info.typeName.find("TEXT") != std::string::npos || info.typeName.find("CHAR") != std::string::npos ||
             info.typeName.find("text") != std::string::npos || info.typeName.find("char") != std::string::npos ||
             info.typeName.empty())) { // CSV gives empty typeName for all
            searchColumn = info.name;
        }
    }

    if (columnCustomizer)
        columnCustomizer(columns);

    if (editMode_ && source && source->SupportsWrite()) {
        const auto colInfos = source->GetColumns(query.table);
        for (size_t i = 0; i < columns.size() && i < colInfos.size(); ++i) {
            if (!colInfos[i].primaryKey)
                columns[i].editable = true;
        }
    }

    columnsReady = true;
}

void DataBrowser::RefreshData()
{
    needsRefresh = false;
    lastError.clear();

    if (!IsConnected() || query.table.empty())
        return;

    const adapters::QueryResult res = source->ExecuteQuery(query);

    if (!res.ok()) {
        lastError = res.error;
        rows.clear();
        totalRows = 0;
        return;
    }

    rows      = res.rows;
    totalRows = source->CountQuery(query);

    char buf[128];
    std::snprintf(buf, sizeof(buf), "%d rows  (%.1f ms)", totalRows, res.executionMs);
    statusMsg = buf;
}

void DataBrowser::Render()
{
    if (needsRefresh)
        RefreshData();

    const std::string winId = windowTitle + "##dbw" + idSuffix_;
    ImGui::Begin(winId.c_str());
    focused_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootWindow);

    if (!IsConnected()) {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "No data source connected.");
        if (source)
            ImGui::TextDisabled("Last error: %s", source->LastError().c_str());
        ImGui::End();
        return;
    }

    DrawToolbar();

    if (preContentHook_)
        preContentHook_();

    const std::string layoutId = "##dblayout" + idSuffix_;
    if (ImGui::BeginChild(layoutId.c_str(), ImVec2(0.0f, ImGui::GetContentRegionAvail().y), false)) {
        if (showSidebar && !layout_.isPhone()) {
            DrawSidebar();
            ImGui::SameLine();
        }
        DrawMainContent();
    }
    ImGui::EndChild();

    RenderInsertPopup();
    RenderDeleteConfirm();
    hexView_.Render();   // must be inside Begin/End: OpenPopup + BeginPopupModal share the same window context

    ImGui::End();

    if (layout_.isPhone())
        DrawPhoneOverlay();

    inspector_.Render() ;
}

void DataBrowser::DrawToolbar()
{
    if (layout_.isPhone()) {
        if (ImGui::Button(ui::icons::Bars))
            phoneOverlayOpen_ = !phoneOverlayOpen_;
    } else {
        if (ImGui::Button(showSidebar ? "\xe2\x86\x90" : "\xe2\x86\x92")) // ← / →
            showSidebar = !showSidebar;
    }
    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button,
                          sqlEditor_.visible ? ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive)
                                            : ImGui::GetStyleColorVec4(ImGuiCol_Button));
    if (ImGui::Button("SQL"))
        sqlEditor_.visible = !sqlEditor_.visible;
    ImGui::PopStyleColor();
    ImGui::SameLine();

    if (ImGui::Button(ui::icons::InfoCircle) && IsConnected() && !query.table.empty())
        OpenInspector();
    ImGui::SameLine();

    if (source && source->SupportsWrite()) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button,
                              editMode_ ? ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive)
                                        : ImGui::GetStyleColorVec4(ImGuiCol_Button));
        if (ImGui::Button(ui::icons::Pencil)) {
            editMode_ = !editMode_;
            BuildColumns();
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("%s", editMode_ ? "Edit mode ON  (click to disable)" : "Edit mode OFF (click to enable)");
    }

    if (editMode_ && source && source->SupportsWrite() && !query.table.empty()) {
        ImGui::SameLine();
        if (ImGui::Button("+##ins")) {
            const auto colInfos = source->GetColumns(query.table);
            insertFields_.resize(colInfos.size());
            for (auto& f : insertFields_)
                std::memset(f.buf, 0, sizeof(f.buf));
            crudError_.clear();
            showInsertPopup_ = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("Insert row");

        ImGui::SameLine();
        const bool hasSel = (gridState.selectedRow >= 0 && gridState.selectedRow < static_cast<int>(rows.size()));
        if (!hasSel)
            ImGui::BeginDisabled();
        if (ImGui::Button("-##del")) {
            crudError_.clear();
            showDeleteConfirm_ = true;
        }
        if (!hasSel)
            ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("Delete selected row");
    }

    if (!editError_.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "  %s %s", ui::icons::Pencil, editError_.c_str());
    }

    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", AdapterLabel().c_str());

    if (!lastError.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "  %s  %s", ui::icons::Warning, lastError.c_str());
    } else if (!statusMsg.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("  %s", statusMsg.c_str());
    }
}

void DataBrowser::DrawSidebarContent()
{
    ImGui::Text("Tables");
    ImGui::Separator();

    if (tables.empty()) {
        ImGui::TextDisabled("(none)");
    } else {
        for (const auto& tbl : tables) {
            const bool selected = (tbl.name == query.table);

            const char*       icon  = (tbl.kind == "view") ? " v " : (tbl.kind == "csv") ? " c " : " t ";
            const std::string label = icon + tbl.name;

            if (ImGui::Selectable(label.c_str(), selected)) {
                if (!selected)
                    SelectTable(tbl.name);
                if (layout_.isPhone())
                    phoneOverlayOpen_ = false;
            }

            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !tbl.catalog.empty()) {
                ImGui::BeginTooltip();
                ImGui::TextDisabled("%s  (%s)", tbl.name.c_str(), tbl.kind.c_str());
                ImGui::TextDisabled("%s", tbl.catalog.c_str());
                ImGui::EndTooltip();
            }

            {
                const std::string ctxId = "##ctx_" + tbl.name;
                if (ImGui::BeginPopupContextItem(ctxId.c_str())) {
                    ImGui::TextDisabled("%s", tbl.name.c_str());
                    ImGui::Separator();
                    if (ImGui::MenuItem((std::string(ui::icons::InfoCircle) + " Inspect\xe2\x80\xa6").c_str()))
                        OpenInspector(tbl.name);
                    ImGui::EndPopup();
                }
            }
        }
    }

    if (!query.table.empty()) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Filter");
        ImGui::Separator();

        if (!filterColumn.empty()) {
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::BeginCombo("##filtercol", filterColumn.c_str())) {
                for (const auto& col : columns) {
                    const bool isSel = (col.key == filterColumn);
                    if (ImGui::Selectable(col.label.c_str(), isSel)) {
                        filterColumn = col.key;
                        filterBuf[0] = '\0';
                        query.whereExact.clear();
                        query.page   = 0;
                        needsRefresh = true;
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::SetNextItemWidth(-1.0f);
            const bool enterF =
                ImGui::InputText("##filterval", filterBuf, sizeof(filterBuf), ImGuiInputTextFlags_EnterReturnsTrue);

            if (ImGui::Button("Apply##f", ImVec2(-1.0f, 0.0f)) || enterF) {
                query.whereExact.clear();
                if (filterBuf[0] != '\0')
                    query.whereExact[filterColumn] = filterBuf;
                query.page   = 0;
                needsRefresh = true;
            }

            if (ImGui::Button("Clear##f", ImVec2(-1.0f, 0.0f))) {
                filterBuf[0] = '\0';
                query.whereExact.clear();
                query.page   = 0;
                needsRefresh = true;
            }
        }

        if (!searchColumn.empty()) {
            ImGui::Spacing();
            ImGui::Text("Search  (%s)", searchColumn.c_str());
            ImGui::SetNextItemWidth(-1.0f);
            const bool enterS =
                ImGui::InputText("##searchval", searchBuf, sizeof(searchBuf), ImGuiInputTextFlags_EnterReturnsTrue);

            if (ImGui::Button("Apply##s", ImVec2(-1.0f, 0.0f)) || enterS) {
                query.searchColumn = searchColumn;
                query.searchValue  = searchBuf;
                query.page         = 0;
                needsRefresh       = true;
            }

            if (ImGui::Button("Clear##s", ImVec2(-1.0f, 0.0f))) {
                searchBuf[0] = '\0';
                query.searchColumn.clear();
                query.searchValue.clear();
                query.page   = 0;
                needsRefresh = true;
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Columns")) {
            if (columnsReady)
                ImGuiExt::DataGridColumnVisibility(columns);
        }
    }
}

void DataBrowser::DrawSidebar()
{
    ImGui::BeginChild(("##dbsidebar" + idSuffix_).c_str(), ImVec2(layout_.sidebarWidth, 0.0f), true);
    DrawSidebarContent();
    ImGui::EndChild();
}


void DataBrowser::DrawPhoneOverlay()
{
    if (!phoneOverlayOpen_)
        return;

    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGui::SetNextWindowPos(ImVec2(0, ImGui::GetFrameHeight()));
    ImGui::SetNextWindowSize(
        ImVec2(layout_.sidebarWidth, static_cast<float>(layout_.logicalH) - ImGui::GetFrameHeight()));

    constexpr ImGuiWindowFlags overlayFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                              ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::Begin("##phone_overlay", &phoneOverlayOpen_, overlayFlags)) {
        if (ImGui::Button((std::string(ui::icons::Times) + "  Close").c_str(), ImVec2(-1.0f, 0.0f)))
            phoneOverlayOpen_ = false;
        ImGui::Separator();
        DrawSidebarContent();
    }
    ImGui::End();
}


void DataBrowser::RenderInsertPopup()
{
    if (showInsertPopup_)
        ImGui::OpenPopup("Insert Row##ins");

    ImGui::SetNextWindowSize(ImVec2(480.0f, 0.0f), ImGuiCond_Always);
    if (!ImGui::BeginPopupModal("Insert Row##ins", &showInsertPopup_, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::TextDisabled("Table: %s", query.table.c_str());
    ImGui::Separator();
    ImGui::Spacing();

    const auto      colInfos = source->GetColumns(query.table);
    constexpr float kLabelW  = 160.0f;

    for (size_t i = 0; i < colInfos.size() && i < insertFields_.size(); ++i) {
        const auto& ci = colInfos[i];
        if (ci.primaryKey)
            ImGui::TextDisabled("%s (PK, auto)", ci.name.c_str());
        else {
            ImGui::Text("%s", ci.name.c_str());
            ImGui::SameLine(kLabelW);
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            const std::string id = "##ins_" + ci.name;
            ImGui::InputText(id.c_str(), insertFields_[i].buf, sizeof(insertFields_[i].buf));
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Insert", ImVec2(120.0f, 0.0f))) {
        std::unordered_map<std::string, std::string> vals;
        for (size_t i = 0; i < colInfos.size() && i < insertFields_.size(); ++i) {
            if (!colInfos[i].primaryKey && insertFields_[i].buf[0] != '\0')
                vals[colInfos[i].name] = insertFields_[i].buf;
        }
        auto res = source->InsertRow(query.table, vals);
        if (!res) {
            crudError_ = res.error();
        } else {
            crudError_.clear();
            needsRefresh     = true;
            showInsertPopup_ = false;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
        showInsertPopup_ = false;
        ImGui::CloseCurrentPopup();
    }

    if (!crudError_.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Error: %s", crudError_.c_str());
    }

    ImGui::EndPopup();
}

void DataBrowser::RenderDeleteConfirm()
{
    if (showDeleteConfirm_)
        ImGui::OpenPopup("Confirm Delete##del");

    if (!ImGui::BeginPopupModal("Confirm Delete##del", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::Text("Delete row %d from  \"%s\"?", gridState.selectedRow + 1, query.table.c_str());
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.72f, 0.15f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.88f, 0.20f, 0.20f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.60f, 0.10f, 0.10f, 1.0f));
    if (ImGui::Button("Delete", ImVec2(120.0f, 0.0f))) {
        ImGui::PopStyleColor(3);

        const auto                                   colInfos = source->GetColumns(query.table);
        const auto&                                  rowData  = rows[static_cast<size_t>(gridState.selectedRow)];
        std::unordered_map<std::string, std::string> pkVals;
        for (size_t i = 0; i < colInfos.size() && i < columns.size(); ++i) {
            if (colInfos[i].primaryKey)
                pkVals[columns[i].key] = (i < rowData.size()) ? rowData[i] : "";
        }
        if (pkVals.empty()) {
            crudError_ = "Table has no primary key — cannot delete";
        } else {
            auto res = source->DeleteRow(query.table, pkVals);
            if (!res) {
                crudError_ = res.error();
            } else {
                crudError_.clear();
                gridState.selectedRow = -1;
                needsRefresh          = true;
                showDeleteConfirm_    = false;
                ImGui::CloseCurrentPopup();
            }
        }
        if (!crudError_.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Error: %s", crudError_.c_str());
        ImGui::EndPopup();
        return;
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
        showDeleteConfirm_ = false;
        ImGui::CloseCurrentPopup();
    }

    if (!crudError_.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Error: %s", crudError_.c_str());
    }
    ImGui::EndPopup();
}

void DataBrowser::DrawMainContent()
{
    const float paginH = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;

    ImGui::BeginChild(("##dbmain" + idSuffix_).c_str(), ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_NoScrollbar);

    if (ui::DrawSqlEditor(sqlEditor_, source.get())) {
        columns.clear();
        for (const auto& ci : sqlEditor_.resultColumns) {
            ImGuiExt::ColumnDef col;
            col.key   = ci.name;
            col.label = ci.name;
            columns.push_back(std::move(col));
        }
        if (columnCustomizer)
            columnCustomizer(columns);
        columnsReady = true;

        rows      = std::move(sqlEditor_.resultRows);
        totalRows = static_cast<int>(rows.size());
        query.page = 0;
        gridState  = {};

        char buf[128];
        std::snprintf(buf, sizeof(buf), "%d rows  (%.1f ms)", totalRows, sqlEditor_.resultMs);
        statusMsg = buf;
        lastError.clear();
    }

    if (!columnsReady) {
        ImGui::TextDisabled("Select a table from the sidebar.");
        ImGui::EndChild();
        return;
    }

    const float gridH = ImGui::GetContentRegionAvail().y - paginH;

    const bool hasImageCols =
        std::ranges::any_of(columns, [](const ImGuiExt::ColumnDef& c) { return !c.semanticHint.empty(); });

    ImGuiExt::DataGridOptions opts;
    const std::string         gridId = "##dbgrid" + idSuffix_;
    opts.id                          = gridId.c_str();
    opts.maxHeight                   = std::max(gridH, 80.0f);
    opts.minRowHeight                = hasImageCols ? 72.0f : layout_.touchTargetPx; // 0 on desktop, 44 on mobile

    opts.onRowClick = [this](int rowIdx) {
        if (onRowClick && rowIdx >= 0 && rowIdx < (int)rows.size()) {
            // Snapshot before the callback fires: the callback may navigate to a
            // new directory which calls SelectTable() → rows.clear(), invalidating
            // any reference into rows taken before the call.
            const auto rowSnap = rows[static_cast<size_t>(rowIdx)];
            onRowClick(rowIdx, rowSnap);
        }
    };

    opts.onRowDblClick = [this](int rowIdx) {
        if (onRowDblClick && rowIdx >= 0 && rowIdx < (int)rows.size()) {
            const auto rowSnap = rows[static_cast<size_t>(rowIdx)];
            onRowDblClick(rowIdx, rowSnap);
        }
    };

    opts.onCellEdit = [this](int rowIdx, int colIdx, const std::string& newValue) -> bool {
        if (!source || !source->SupportsWrite())
            return false;
        if (rowIdx < 0 || rowIdx >= static_cast<int>(rows.size()))
            return false;
        if (colIdx < 0 || colIdx >= static_cast<int>(columns.size()))
            return false;

        const auto& rowData  = rows[static_cast<size_t>(rowIdx)];
        const auto  colInfos = source->GetColumns(query.table);

        std::unordered_map<std::string, std::string> pkVals;
        for (size_t i = 0; i < colInfos.size() && i < columns.size(); ++i) {
            if (colInfos[i].primaryKey)
                pkVals[columns[i].key] = (i < rowData.size()) ? rowData[i] : "";
        }
        if (pkVals.empty()) {
            editError_ = "Table has no primary key — cannot update";
            return false;
        }

        auto res = source->UpdateRow(query.table, pkVals, {{columns[static_cast<size_t>(colIdx)].key, newValue}});

        if (!res) {
            editError_ = res.error();
            return false;
        }
        editError_.clear();
        needsRefresh = true;
        return true;
    };

    opts.contextMenu = [this](int rowIdx) {
        if (rowIdx < 0 || rowIdx >= (int)rows.size())
            return;

        ImGui::TextDisabled("%s  —  row %d", query.table.c_str(), rowIdx + 1);
        ImGui::Separator();

        if (ImGui::BeginMenu("Copy cell")) {
            const auto& row = rows[static_cast<size_t>(rowIdx)];
            for (size_t c = 0; c < columns.size() && c < row.size(); ++c) {
                const std::string label = columns[c].label + ": " + row[c];
                if (ImGui::MenuItem(label.c_str()))
                    ImGui::SetClipboardText(row[c].c_str());
            }
            ImGui::EndMenu();
        }

        if (ImGui::MenuItem("Copy row (TSV)")) {
            const auto& row = rows[static_cast<size_t>(rowIdx)];
            std::string tsv;
            for (size_t c = 0; c < row.size(); ++c) {
                if (c)
                    tsv += '\t';
                tsv += row[c];
            }
            ImGui::SetClipboardText(tsv.c_str());
        }

        const bool isFs = source && source->AdapterName() == "filesystem";
        if (isFs) {
            ImGui::Separator();

            int pathCol = -1, kindCol = -1;
            for (int c = 0; c < static_cast<int>(columns.size()); ++c) {
                if (columns[c].key == "path") pathCol = c;
                if (columns[c].key == "kind") kindCol = c;
            }

            const auto&       row   = rows[static_cast<std::size_t>(rowIdx)];
            const std::string fpath = (pathCol >= 0 && pathCol < (int)row.size()) ? row[pathCol] : "";
            const std::string fkind = (kindCol >= 0 && kindCol < (int)row.size()) ? row[kindCol] : "";

            if (!fpath.empty()) {
                const std::string openLbl =
                    std::string(ui::icons::ExternalLink) + "  Open\xe2\x80\xa6";
                if (ImGui::MenuItem(openLbl.c_str())) {
                    if (openCb_) openCb_(fpath, "auto");
                }

                // .app bundles (and other executable dir packages) — show explicit Launch item.
                if (fkind == "dir" && ui::IsExecutableFile(fpath)) {
                    const std::string lbl = std::string(ui::icons::Play) + "  Open Application";
                    if (ImGui::MenuItem(lbl.c_str()))
                        if (openCb_) openCb_(fpath, "system");
                }

                if (fkind == "file") {
                    if (ui::IsImageFile(fpath)) {
                        const std::string lbl = std::string(ui::icons::Image) + "  Open as Image";
                        if (ImGui::MenuItem(lbl.c_str()))
                            if (openCb_) openCb_(fpath, "image");
                    }

                    if (ui::IsSqliteFile(fpath)) {
                        const std::string lbl = std::string(ui::icons::Database) + "  Open as SQLite";
                        if (ImGui::MenuItem(lbl.c_str()))
                            if (openCb_) openCb_(fpath, "sqlite");
                    }

                    if (ui::IsPdfFile(fpath)) {
                        const std::string lbl = std::string(ui::icons::ExternalLink) + "  Open PDF";
                        if (ImGui::MenuItem(lbl.c_str()))
                            if (openCb_) openCb_(fpath, "system");
                    }

                    if (ui::IsExecutableFile(fpath)) {
                        const std::string lbl = std::string(ui::icons::Play) + "  Launch";
                        if (ImGui::MenuItem(lbl.c_str()))
                            if (openCb_) openCb_(fpath, "system");
                    }

                    if (ui::IsTextFile(fpath)) {
                        const std::string lbl = std::string(ui::icons::File) + "  Open as Text";
                        if (ImGui::MenuItem(lbl.c_str()))
                            if (openCb_) openCb_(fpath, "text");
                    }

                    ImGui::Separator();
                    if (ImGui::MenuItem("Inspect Bytes\xe2\x80\xa6"))
                        hexView_.OpenFile(fpath);
                }
            }
        }

        ImGui::Separator();
        if (ImGui::BeginMenu("Inspect Cell Bytes\xe2\x80\xa6")) {
            const auto& row = rows[static_cast<size_t>(rowIdx)];
            for (size_t c = 0; c < columns.size() && c < row.size(); ++c) {
                std::string itemLabel = columns[c].label;
                const auto  colInfos  = source ? source->GetColumns(query.table)
                                               : std::vector<adapters::ColumnInfo>{};
                if (c < colInfos.size()) {
                    const auto& tn = colInfos[c].typeName;
                    if (tn.find("BLOB") != std::string::npos ||
                        tn.find("blob") != std::string::npos)
                        itemLabel += "  [BLOB]";
                }
                if (ImGui::MenuItem(itemLabel.c_str())) {
                    const std::string lbl = columns[c].label
                                            + "  \xe2\x80\x94  "  // " — "
                                            + query.table
                                            + "  \xc2\xb7  row "  // " · row "
                                            + std::to_string(rowIdx + 1);
                    hexView_.Open(lbl, row[c]);
                }
            }
            ImGui::EndMenu();
        }
    };

    if (dragSourceCb_) {
        opts.onRowDragSource = [this](int rowIdx) {
            if (rowIdx >= 0 && rowIdx < static_cast<int>(rows.size()))
                dragSourceCb_(rowIdx, rows[static_cast<std::size_t>(rowIdx)]);
        };
    }

    if (ImGuiExt::DataGrid(columns, rows, gridState, opts)) {
        if (gridState.sortChanged) {
            query.sortColumn    = gridState.sortColumnKey;
            query.sortAscending = gridState.sortAscending;
            query.page          = 0;
            needsRefresh        = true;
        }
    }

    // BeginDragDropTarget() checks IsItemHovered() — the EndTable() item
    // covers the entire table area, so this works without an extra overlay.
    if (dropHandler_ && ImGui::BeginDragDropTarget()) {
        for (const char* t : {"DATAGRID_ROW", "DATAGRID_FILE"}) {
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(t)) {
                dropHandler_(t, p->Data, static_cast<std::size_t>(p->DataSize));
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGuiExt::DataGridPagination(query.page, query.pageSize, totalRows))
        needsRefresh = true;

    ImGui::EndChild();
}
} // namespace datagrid