#include "data_browser.hpp"
#include "imgui_datagrid.hpp"
#include "adapters/data_source.hpp"

#include <cstdio>
#include <cstring>
#include <format>
#include <string>

// ============================================================
//  SetLayout / OpenInspector
// ============================================================

// ============================================================
//  Construction
// ============================================================

int DataBrowser::nextInstanceId_ = 0;

DataBrowser::DataBrowser(Adapters::DataSourcePtr source_, const std::string& title)
    : source(std::move(source_))
    , windowTitle(title)
{
    if (IsConnected()) {
        LoadSchema();
        // Auto-select the first table so the grid isn't empty on first open
        if (!tables.empty())
            SelectTable(tables.front().name);
    }

    instanceId_ = ++nextInstanceId_;
    idSuffix_   = std::format("_{}", instanceId_);
    inspector_.SetInstanceId(instanceId_);
}

void DataBrowser::SetLayout(const ResponsiveLayout& l)
{
    layout_ = l;
}

void DataBrowser::OpenInspector(const std::string& tableName)
{
    const std::string target = tableName.empty() ? query.table : tableName;
    if (IsConnected() && !target.empty())
        inspector_.Open(source.get(), target);
}

// ============================================================
//  Callback registration
// ============================================================

void DataBrowser::SetColumnCustomizer(ColumnCustomizer fn)
{
    columnCustomizer = std::move(fn);
    // Re-apply immediately if columns are already built
    if (columnsReady && columnCustomizer)
        columnCustomizer(columns);
}

void DataBrowser::SetOnRowClick(RowCallback fn)    { onRowClick    = std::move(fn); }
void DataBrowser::SetOnRowDblClick(RowCallback fn) { onRowDblClick = std::move(fn); }

// ============================================================
//  Runtime adapter swap
// ============================================================

void DataBrowser::SetDataSource(Adapters::DataSourcePtr newSource)
{
    source       = std::move(newSource);
    // Reset all derived state
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
    sqlError.clear();
    sqlStatus.clear();
    filterBuf[0] = '\0';
    searchBuf[0] = '\0';
    sqlBuf[0]    = '\0';
    gridState    = {};

    if (IsConnected()) {
        LoadSchema();
        if (!tables.empty())
            SelectTable(tables.front().name);
    }
}

// ============================================================
//  Accessors
// ============================================================

bool DataBrowser::IsConnected() const
{
    return source && source->IsConnected();
}

std::string DataBrowser::AdapterLabel() const
{
    return source ? source->AdapterLabel() : "(no source)";
}

std::string DataBrowser::WindowTitle() const   { return windowTitle; }
void DataBrowser::SetWindowTitle(const std::string& t) { windowTitle = t; }

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

Adapters::IDataSource* DataBrowser::GetSource() const
{
    return source.get();
}

bool DataBrowser::IsInspectorOpen() const { return inspector_.IsOpen(); }
void DataBrowser::CloseInspector()        { inspector_.Close(); }

// ============================================================
//  Data helpers
// ============================================================

void DataBrowser::LoadSchema()
{
    if (!IsConnected()) return;

    const auto cats = source->GetCatalogs();
    const std::string catalog = cats.empty() ? "" : cats.front();
    tables = source->GetTables(catalog);
    schemaLoaded = true;
}

void DataBrowser::SelectTable(const std::string& tableName)
{
    if (query.table == tableName && columnsReady) return;

    query            = {};          // reset filters / sort / page
    query.table      = tableName;
    columnsReady     = false;
    rows.clear();
    gridState        = {};
    filterBuf[0]     = '\0';
    searchBuf[0]     = '\0';
    filterColumn.clear();
    searchColumn.clear();

    BuildColumns();
    needsRefresh = true;
}

void DataBrowser::BuildColumns()
{
    columns.clear();
    if (!IsConnected() || query.table.empty()) return;

    const auto infos = source->GetColumns(query.table);

    columns.reserve(infos.size());
    for (const auto& info : infos) {
        ImGuiExt::ColumnDef col;
        col.key      = info.name;
        col.label    = info.name;
        col.sortable = true;
        col.visible  = true;

        // Heuristic type mapping from declared SQL/adapter type
        const std::string& t = info.typeName;
        auto contains = [&](const char* sub) {
            return t.find(sub) != std::string::npos;
        };

        if (contains("INT") || contains("int") ||
            contains("REAL") || contains("real") ||
            contains("FLOAT") || contains("float") ||
            contains("NUMERIC") || contains("numeric") ||
            contains("DOUBLE") || contains("double") ||
            contains("NUMBER") || contains("number")) {
            col.type = ImGuiExt::ColumnType::Number;
        } else if (contains("DATE") || contains("date") ||
                   contains("TIME") || contains("time")) {
            col.type      = ImGuiExt::ColumnType::Date;
            col.initWidth = 130.0f;
        } else {
            col.type = ImGuiExt::ColumnType::Text;
        }

        // Primary-key columns: narrow, number-aligned
        if (info.primaryKey) {
            col.initWidth = 48.0f;
            col.type      = ImGuiExt::ColumnType::Number;
        }

        columns.push_back(std::move(col));
    }

    // Choose default filter/search columns from schema:
    //   filterColumn  → first non-pk column
    //   searchColumn  → first TEXT-like column
    for (const auto& info : infos) {
        if (!info.primaryKey && filterColumn.empty())
            filterColumn = info.name;

        if (searchColumn.empty() &&
            (info.typeName.find("TEXT") != std::string::npos ||
             info.typeName.find("CHAR") != std::string::npos ||
             info.typeName.find("text") != std::string::npos ||
             info.typeName.find("char") != std::string::npos ||
             info.typeName.empty())) {    // CSV gives empty typeName for all
            searchColumn = info.name;
        }
    }

    // Let the application customise columns (widths, renderers, labels, …)
    if (columnCustomizer)
        columnCustomizer(columns);

    // Mark non-PK columns as editable when edit mode is on
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

    if (!IsConnected() || query.table.empty()) return;

    const Adapters::QueryResult res = source->ExecuteQuery(query);

    if (!res.ok()) {
        lastError = res.error;
        rows.clear();
        totalRows = 0;
        return;
    }

    rows      = res.rows;
    totalRows = source->CountQuery(query);

    char buf[128];
    std::snprintf(buf, sizeof(buf),
                  "%d rows  (%.1f ms)", totalRows, res.executionMs);
    statusMsg = buf;
}

// ============================================================
//  Render
// ============================================================

void DataBrowser::Render()
{
    // Deferred data load — never inside a nested BeginTable/BeginChild
    if (needsRefresh)
        RefreshData();

    const std::string winId = windowTitle + "##dbw" + idSuffix_;
    ImGui::Begin(winId.c_str());

    if (!IsConnected()) {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                           "No data source connected.");
        if (source)
            ImGui::TextDisabled("Last error: %s", source->LastError().c_str());
        ImGui::End();
        return;
    }

    DrawToolbar();

    // Adapter-specific hook (e.g. filesystem navigation bar)
    if (preContentHook_) preContentHook_();

    // Reserve the full remaining height for the two-column layout child
    const std::string layoutId = "##dblayout" + idSuffix_;
    if (ImGui::BeginChild(layoutId.c_str(),
                          ImVec2(0.0f, ImGui::GetContentRegionAvail().y),
                          false))
    {
        // Phone: sidebar is a floating overlay, not inline
        if (showSidebar && !layout_.isPhone()) {
            DrawSidebar();
            ImGui::SameLine();
        }
        DrawMainContent();
    }
    ImGui::EndChild();

    RenderInsertPopup();
    RenderDeleteConfirm();

    ImGui::End();

    // Phone overlay must be drawn OUTSIDE the main window Begin/End pair
    if (layout_.isPhone())
        DrawPhoneOverlay();

    // Schema inspector is its own floating window
    inspector_.Render();
}

// ============================================================
//  DrawToolbar
// ============================================================

void DataBrowser::DrawToolbar()
{
    // ── Sidebar toggle ────────────────────────────────────────────────────
    if (layout_.isPhone()) {
        // Phone: hamburger button opens overlay
        if (ImGui::Button("☰"))
            phoneOverlayOpen_ = !phoneOverlayOpen_;
    } else {
        if (ImGui::Button(showSidebar ? "<|" : "|>"))
            showSidebar = !showSidebar;
    }
    ImGui::SameLine();

    // ── SQL editor toggle ─────────────────────────────────────────────────
    ImGui::PushStyleColor(ImGuiCol_Button,
        showSqlEditor ? ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive)
                      : ImGui::GetStyleColorVec4(ImGuiCol_Button));
    if (ImGui::Button("SQL"))
        showSqlEditor = !showSqlEditor;
    ImGui::PopStyleColor();
    ImGui::SameLine();

    // ── Inspector button ──────────────────────────────────────────────────
    if (ImGui::Button("ℹ") && IsConnected() && !query.table.empty())
        OpenInspector();
    ImGui::SameLine();

    // Write-mode toggle — only shown when the adapter supports mutations
    if (source && source->SupportsWrite()) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button,
            editMode_ ? ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive)
                      : ImGui::GetStyleColorVec4(ImGuiCol_Button));
        if (ImGui::Button("\xe2\x9c\x8e")) {   // ✎ pencil
            editMode_ = !editMode_;
            BuildColumns();   // re-apply editable flags
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("%s", editMode_ ? "Edit mode ON  (click to disable)"
                                              : "Edit mode OFF (click to enable)");
    }

    // ── Insert / Delete buttons (write mode + table selected) ─────────────
    if (editMode_ && source && source->SupportsWrite() && !query.table.empty()) {
        ImGui::SameLine();
        if (ImGui::Button("+##ins")) {
            // Seed one InsertField per schema column
            const auto colInfos = source->GetColumns(query.table);
            insertFields_.resize(colInfos.size());
            for (auto& f : insertFields_) std::memset(f.buf, 0, sizeof(f.buf));
            crudError_.clear();
            showInsertPopup_ = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("Insert row");

        ImGui::SameLine();
        const bool hasSel = (gridState.selectedRow >= 0 &&
                             gridState.selectedRow < static_cast<int>(rows.size()));
        if (!hasSel) ImGui::BeginDisabled();
        if (ImGui::Button("-##del")) {
            crudError_.clear();
            showDeleteConfirm_ = true;
        }
        if (!hasSel) ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("Delete selected row");
    }

    if (!editError_.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                           "  \xe2\x9c\x8e %s", editError_.c_str());
    }

    // ── Adapter label ─────────────────────────────────────────────────────
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", AdapterLabel().c_str());

    // ── Status / error ────────────────────────────────────────────────────
    if (!lastError.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                           "  ⚠  %s", lastError.c_str());
    } else if (!statusMsg.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("  %s", statusMsg.c_str());
    }
}

// ============================================================
//  DrawSidebar
// ============================================================

// ============================================================
//  DrawSidebarContent  —  shared inner rendering for both
//  the inline sidebar (tablet/desktop) and the phone overlay.
// ============================================================
void DataBrowser::DrawSidebarContent()
{
    // ── Table list ────────────────────────────────────────────────────────
    ImGui::Text("Tables");
    ImGui::Separator();

    if (tables.empty()) {
        ImGui::TextDisabled("(none)");
    } else {
        for (const auto& tbl : tables) {
            const bool selected = (tbl.name == query.table);

            const char* icon = (tbl.kind == "view") ? " v "
                             : (tbl.kind == "csv")  ? " c "
                             :                        " t ";
            const std::string label = icon + tbl.name;

            if (ImGui::Selectable(label.c_str(), selected)) {
                if (!selected) SelectTable(tbl.name);
                if (layout_.isPhone()) phoneOverlayOpen_ = false;
            }

            // Hover tooltip
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) &&
                !tbl.catalog.empty())
            {
                ImGui::BeginTooltip();
                ImGui::TextDisabled("%s  (%s)", tbl.name.c_str(), tbl.kind.c_str());
                ImGui::TextDisabled("%s",        tbl.catalog.c_str());
                ImGui::EndTooltip();
            }

            // Right-click → Inspect
            {
                const std::string ctxId = "##ctx_" + tbl.name;
                if (ImGui::BeginPopupContextItem(ctxId.c_str())) {
                    ImGui::TextDisabled("%s", tbl.name.c_str());
                    ImGui::Separator();
                    if (ImGui::MenuItem("ℹ Inspect…"))
                        OpenInspector(tbl.name);
                    ImGui::EndPopup();
                }
            }
        }
    }

    // ── Generic filter strip ──────────────────────────────────────────────
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
            const bool enterF = ImGui::InputText(
                "##filterval", filterBuf, sizeof(filterBuf),
                ImGuiInputTextFlags_EnterReturnsTrue);

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
            const bool enterS = ImGui::InputText(
                "##searchval", searchBuf, sizeof(searchBuf),
                ImGuiInputTextFlags_EnterReturnsTrue);

            if (ImGui::Button("Apply##s", ImVec2(-1.0f, 0.0f)) || enterS) {
                query.searchColumn = searchColumn;
                query.searchValue  = searchBuf;
                query.page         = 0;
                needsRefresh       = true;
            }

            if (ImGui::Button("Clear##s", ImVec2(-1.0f, 0.0f))) {
                searchBuf[0]       = '\0';
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

// ============================================================
//  DrawSidebar  —  inline child window (tablet / desktop)
// ============================================================
void DataBrowser::DrawSidebar()
{
    ImGui::BeginChild(("##dbsidebar" + idSuffix_).c_str(), ImVec2(layout_.sidebarWidth, 0.0f), true);
    DrawSidebarContent();
    ImGui::EndChild();
}

// ============================================================
//  DrawPhoneOverlay  —  full-screen overlay sidebar for phones
// ============================================================
void DataBrowser::DrawPhoneOverlay()
{
    if (!phoneOverlayOpen_) return;

    // Darken the background
    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGui::SetNextWindowPos(ImVec2(0, ImGui::GetFrameHeight()));
    ImGui::SetNextWindowSize(ImVec2(layout_.sidebarWidth,
                                   static_cast<float>(layout_.logicalH)
                                   - ImGui::GetFrameHeight()));

    constexpr ImGuiWindowFlags overlayFlags =
        ImGuiWindowFlags_NoTitleBar  |
        ImGuiWindowFlags_NoResize    |
        ImGuiWindowFlags_NoMove      |
        ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::Begin("##phone_overlay", &phoneOverlayOpen_, overlayFlags)) {
        // Close button at top
        if (ImGui::Button("✕  Close", ImVec2(-1.0f, 0.0f)))
            phoneOverlayOpen_ = false;
        ImGui::Separator();
        DrawSidebarContent();
    }
    ImGui::End();
}

// ============================================================
//  DrawSqlEditor
// ============================================================

void DataBrowser::DrawSqlEditor()
{
    if (!showSqlEditor) return;

    // Fixed-height input strip above the grid
    constexpr float editorHeight = 70.0f;

    ImGui::PushStyleColor(ImGuiCol_ChildBg,
        ImVec4(0.08f, 0.08f, 0.08f, 1.0f));
    ImGui::BeginChild("##sqleditor", ImVec2(0.0f, editorHeight), true);

    ImGui::InputTextMultiline(
        "##sql", sqlBuf, sizeof(sqlBuf),
        ImVec2(ImGui::GetContentRegionAvail().x
               - ImGui::CalcTextSize("Run ").x
               - ImGui::GetStyle().ItemSpacing.x * 3,
               editorHeight - ImGui::GetStyle().WindowPadding.y * 2),
        ImGuiInputTextFlags_None
    );

    ImGui::SameLine();
    ImGui::BeginGroup();
    if (ImGui::Button("Run") && sqlBuf[0] != '\0') {
        sqlError.clear();
        sqlStatus.clear();

        const Adapters::QueryResult res = source->Execute(sqlBuf);

        if (!res.ok()) {
            sqlError = res.error;
        } else {
            // Rebuild columns from the result metadata
            columns.clear();
            for (const auto& ci : res.columns) {
                ImGuiExt::ColumnDef col;
                col.key   = ci.name;
                col.label = ci.name;
                columns.push_back(std::move(col));
            }
            if (columnCustomizer) columnCustomizer(columns);
            columnsReady = true;

            rows      = res.rows;
            totalRows = static_cast<int>(rows.size()); // all rows in memory
            query.page = 0;
            gridState  = {};

            char buf[128];
            std::snprintf(buf, sizeof(buf),
                          "%d rows  (%.1f ms)",
                          totalRows, res.executionMs);
            sqlStatus  = buf;
            statusMsg  = sqlStatus;
            lastError.clear();
        }
    }

    if (ImGui::Button("Clear")) {
        sqlBuf[0] = '\0';
        sqlError.clear();
        sqlStatus.clear();
    }
    ImGui::EndGroup();

    ImGui::EndChild();
    ImGui::PopStyleColor();

    if (!sqlError.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                           "SQL Error: %s", sqlError.c_str());
    else if (!sqlStatus.empty())
        ImGui::TextDisabled("%s", sqlStatus.c_str());
}

// ============================================================
//  RenderInsertPopup  —  modal for new-row creation
// ============================================================
void DataBrowser::RenderInsertPopup()
{
    if (showInsertPopup_)
        ImGui::OpenPopup("Insert Row##ins");

    ImGui::SetNextWindowSize(ImVec2(480.0f, 0.0f), ImGuiCond_Always);
    if (!ImGui::BeginPopupModal("Insert Row##ins", &showInsertPopup_,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::TextDisabled("Table: %s", query.table.c_str());
    ImGui::Separator();
    ImGui::Spacing();

    const auto colInfos = source->GetColumns(query.table);
    constexpr float kLabelW = 160.0f;

    for (size_t i = 0; i < colInfos.size() && i < insertFields_.size(); ++i) {
        const auto& ci = colInfos[i];
        // Show PK columns as dimmed read-only hints
        if (ci.primaryKey)
            ImGui::TextDisabled("%s (PK, auto)", ci.name.c_str());
        else {
            ImGui::Text("%s", ci.name.c_str());
            ImGui::SameLine(kLabelW);
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            const std::string id = "##ins_" + ci.name;
            ImGui::InputText(id.c_str(), insertFields_[i].buf,
                             sizeof(insertFields_[i].buf));
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
            needsRefresh      = true;
            showInsertPopup_  = false;
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
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                           "Error: %s", crudError_.c_str());
    }

    ImGui::EndPopup();
}

// ============================================================
//  RenderDeleteConfirm  —  confirmation dialog before deletion
// ============================================================
void DataBrowser::RenderDeleteConfirm()
{
    if (showDeleteConfirm_)
        ImGui::OpenPopup("Confirm Delete##del");

    if (!ImGui::BeginPopupModal("Confirm Delete##del", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::Text("Delete row %d from  \"%s\"?",
                gridState.selectedRow + 1, query.table.c_str());
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.72f, 0.15f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.88f, 0.20f, 0.20f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.60f, 0.10f, 0.10f, 1.0f));
    if (ImGui::Button("Delete", ImVec2(120.0f, 0.0f))) {
        ImGui::PopStyleColor(3);

        // Build PK predicate
        const auto colInfos = source->GetColumns(query.table);
        const auto& rowData = rows[static_cast<size_t>(gridState.selectedRow)];
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
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                               "Error: %s", crudError_.c_str());
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
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                           "Error: %s", crudError_.c_str());
    }
    ImGui::EndPopup();
}

// ============================================================
//  DrawMainContent
// ============================================================

void DataBrowser::DrawMainContent()
{
    // Reserve space for the pagination bar.
    // DrawSqlEditor() renders inline so GetContentRegionAvail() already
    // accounts for the consumed height — no extra deduction needed.
    const float paginH = ImGui::GetFrameHeightWithSpacing()
                       + ImGui::GetStyle().ItemSpacing.y;

    ImGui::BeginChild(("##dbmain" + idSuffix_).c_str(),
                      ImVec2(0.0f, 0.0f),
                      false,
                      ImGuiWindowFlags_NoScrollbar);

    DrawSqlEditor();

    if (!columnsReady) {
        ImGui::TextDisabled("Select a table from the sidebar.");
        ImGui::EndChild();
        return;
    }

    const float gridH = ImGui::GetContentRegionAvail().y - paginH;

    ImGuiExt::DataGridOptions opts;
    const std::string gridId = "##dbgrid" + idSuffix_;
    opts.id           = gridId.c_str();
    opts.maxHeight    = std::max(gridH, 80.0f);
    opts.minRowHeight = layout_.touchTargetPx;   // 0 on desktop, 44 on mobile

    opts.onRowClick = [this](int rowIdx) {
        if (onRowClick && rowIdx >= 0 && rowIdx < (int)rows.size())
            onRowClick(rowIdx, rows[static_cast<size_t>(rowIdx)]);
    };

    opts.onRowDblClick = [this](int rowIdx) {
        if (onRowDblClick && rowIdx >= 0 && rowIdx < (int)rows.size())
            onRowDblClick(rowIdx, rows[static_cast<size_t>(rowIdx)]);
    };

    opts.onCellEdit = [this](int rowIdx, int colIdx, const std::string& newValue) -> bool {
        if (!source || !source->SupportsWrite()) return false;
        if (rowIdx < 0 || rowIdx >= static_cast<int>(rows.size()))    return false;
        if (colIdx < 0 || colIdx >= static_cast<int>(columns.size())) return false;

        // Build PK predicate from current row data
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

        auto res = source->UpdateRow(
            query.table, pkVals,
            {{columns[static_cast<size_t>(colIdx)].key, newValue}});

        if (!res) {
            editError_ = res.error();
            return false;
        }
        editError_.clear();
        needsRefresh = true;
        return true;
    };

    opts.contextMenu = [this](int rowIdx) {
        if (rowIdx < 0 || rowIdx >= (int)rows.size()) return;

        // Header: show table + row number
        ImGui::TextDisabled("%s  —  row %d", query.table.c_str(), rowIdx + 1);
        ImGui::Separator();

        // Copy cell values submenu
        if (ImGui::BeginMenu("Copy cell")) {
            const auto& row = rows[static_cast<size_t>(rowIdx)];
            for (size_t c = 0; c < columns.size() && c < row.size(); ++c) {
                const std::string label = columns[c].label + ": " + row[c];
                if (ImGui::MenuItem(label.c_str()))
                    ImGui::SetClipboardText(row[c].c_str());
            }
            ImGui::EndMenu();
        }

        // Copy whole row as tab-separated values
        if (ImGui::MenuItem("Copy row (TSV)")) {
            const auto& row = rows[static_cast<size_t>(rowIdx)];
            std::string tsv;
            for (size_t c = 0; c < row.size(); ++c) {
                if (c) tsv += '\t';
                tsv += row[c];
            }
            ImGui::SetClipboardText(tsv.c_str());
        }
    };

    // ── Draw the grid ──────────────────────────────────────────────────────
    if (ImGuiExt::DataGrid(columns, rows, gridState, opts)) {
        if (gridState.sortChanged) {
            query.sortColumn    = gridState.sortColumnKey;
            query.sortAscending = gridState.sortAscending;
            query.page          = 0;
            needsRefresh        = true;
        }
    }

    // ── Pagination ─────────────────────────────────────────────────────────
    if (ImGuiExt::DataGridPagination(query.page, query.pageSize, totalRows))
        needsRefresh = true;

    ImGui::EndChild();
}
