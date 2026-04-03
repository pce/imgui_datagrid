#include "drag_drop_dialog.hpp"
#include "icons.hpp"

#include "imgui.h"

#ifdef DATAGRID_HAVE_DUCKDB
#include "adapters/duckdb/duckdb_adapter.hpp"
#endif

#include <cstring>
#include <filesystem>
#include <format>
#include <unordered_map>

namespace fs = std::filesystem;

namespace datagrid::ui {

void DropDialogManager::TriggerDbFileOpen(const io::FilePayload& fp, DataBrowser* target)
{
    filePayload_   = fp;
    target_        = target;
    sniffedType_   = io::SniffDbType(fp.path);
    openNewWindow_ = true;
    crudError_.clear();
    kind_ = Kind::DbFileOpen;
    ImGui::OpenPopup("##dd_dbfile");
}

void DropDialogManager::TriggerRowInsert(const io::RowPayload&                 rp,
                                         DataBrowser*                      target,
                                         std::vector<adapters::ColumnInfo> targetCols,
                                         std::vector<std::string>          srcColNames,
                                         std::vector<std::string>          srcValues)
{
    rowPayload_  = rp;
    target_      = target;
    srcColNames_ = std::move(srcColNames);
    srcValues_   = std::move(srcValues);
    insertMode_  = true;
    crudError_.clear();

    mappings_.clear();
    mappings_.reserve(targetCols.size());

    for (const auto& col : targetCols) {
        ColumnMapping m;
        m.targetColumn = col.name;
        m.typeName     = col.typeName;
        m.nullable     = col.nullable;
        m.isPrimaryKey = col.primaryKey;
        m.sourceChoice = -2;
        std::memset(m.manualBuf, 0, sizeof(m.manualBuf));

        for (int i = 0; i < static_cast<int>(srcColNames_.size()); ++i) {
            if (srcColNames_[i] == col.name) {
                m.sourceChoice = i;
                break;
            }
        }

        mappings_.push_back(std::move(m));
    }

    kind_ = Kind::RowInsert;
    ImGui::OpenPopup("##dd_rowinsert");
}

void DropDialogManager::TriggerFsCopyMove(const io::FilePayload& fp, std::string dstDir, DataBrowser* target)
{
    fsCopyPayload_ = fp;
    fsDstDir_      = std::move(dstDir);
    target_        = target;
    crudError_.clear();
    std::snprintf(fsDstBuf_, sizeof(fsDstBuf_), "%s", fsDstDir_.c_str());
    kind_ = Kind::FsCopyMove;
    ImGui::OpenPopup("##dd_fscopy");
}

void DropDialogManager::Close()
{
    kind_   = Kind::None;
    target_ = nullptr;
    mappings_.clear();
    srcColNames_.clear();
    srcValues_.clear();
    crudError_.clear();
    fsDstDir_.clear();
}

void DropDialogManager::Render()
{
    switch (kind_) {
        case Kind::DbFileOpen:
            RenderDbFileOpenDialog();
            break;
        case Kind::RowInsert:
            RenderRowInsertDialog();
            break;
        case Kind::FsCopyMove:
            RenderFsCopyMoveDialog();
            break;
        case Kind::FileToView:
            RenderFileToViewDialog();
            break;
        default:
            break;
    }
}

void DropDialogManager::RenderDbFileOpenDialog()
{
    ImGui::SetNextWindowSize(ImVec2(440.0f, 0.0f), ImGuiCond_Always);
    if (!ImGui::BeginPopupModal("##dd_dbfile", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        // Popup was dismissed externally (e.g. ClosePopupsOverWindow).
        if (kind_ == Kind::DbFileOpen)
            Close();
        return;
    }

    ImGui::TextUnformatted(icons::Database);
    ImGui::SameLine(0.0f, 6.0f);
    ImGui::TextUnformatted(filePayload_.name);
    ImGui::TextDisabled("  %s", filePayload_.path);

    ImGui::Separator();
    ImGui::Spacing();

    const char* typeStr = (sniffedType_ == io::FileDbType::SQLite)   ? "SQLite database"
                          : (sniffedType_ == io::FileDbType::DuckDB) ? "DuckDB database"
                                                                 : "Unknown type";
    ImGui::Text("Detected: %s", typeStr);
    ImGui::Spacing();

    if (ImGui::RadioButton("Open in new window", openNewWindow_))
        openNewWindow_ = true;
    if (ImGui::RadioButton("Replace current window", !openNewWindow_))
        openNewWindow_ = false;

    if (!openNewWindow_ && target_) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
        ImGui::TextWrapped("%s  This will close \"%s\" and open the new file in its place.",
                           icons::Warning,
                           target_->WindowTitle().c_str());
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
        ImGui::CloseCurrentPopup();
        Close();
    }
    ImGui::SameLine();

    const bool canOpen = (sniffedType_ != io::FileDbType::Unknown);
    ImGui::BeginDisabled(!canOpen);
    if (ImGui::Button("Open", ImVec2(100.0f, 0.0f))) {
        const std::string adapter = std::string(io::AdapterForDbType(sniffedType_));
        const std::string path    = filePayload_.path;
        if (openNewWindow_) {
            if (onOpenNewWindow)
                onOpenNewWindow(adapter, path);
        } else {
            if (onReplaceWindow)
                onReplaceWindow(adapter, path, target_);
        }
        ImGui::CloseCurrentPopup();
        Close();
    }
    ImGui::EndDisabled();

    ImGui::EndPopup();
}

void DropDialogManager::RenderRowInsertDialog()
{
    ImGui::SetNextWindowSize(ImVec2(520.0f, 0.0f), ImGuiCond_Always);
    if (!ImGui::BeginPopupModal("##dd_rowinsert", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (kind_ == Kind::RowInsert)
            Close();
        return;
    }

    ImGui::TextUnformatted(icons::Database);
    ImGui::SameLine(0.0f, 6.0f);
    // U+2192 RIGHTWARDS ARROW encoded as UTF-8: 0xE2 0x86 0x92
    ImGui::Text("Insert / Update Row \xe2\x86\x92 %s", rowPayload_.tableName);
    ImGui::Spacing();

    {
        const ImVec4 accent = ImGui::GetStyle().Colors[ImGuiCol_ButtonActive];

        if (insertMode_) {
            ImGui::PushStyleColor(ImGuiCol_Button, accent);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accent);
        }
        if (ImGui::Button("  NEW ENTRY  ", ImVec2(130.0f, 0.0f)))
            insertMode_ = true;
        if (insertMode_)
            ImGui::PopStyleColor(2);

        ImGui::SameLine();

        if (!insertMode_) {
            ImGui::PushStyleColor(ImGuiCol_Button, accent);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accent);
        }
        if (ImGui::Button("  UPDATE ENTRY  ", ImVec2(130.0f, 0.0f)))
            insertMode_ = false;
        if (!insertMode_)
            ImGui::PopStyleColor(2);
    }
    ImGui::Separator();

    constexpr ImGuiTableFlags kTblFlags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;

    if (ImGui::BeginTable("##dd_mapping", 3, kTblFlags)) {
        ImGui::TableSetupColumn("Target Column", ImGuiTableColumnFlags_WidthStretch, 0.35f);
        ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthStretch, 0.30f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.35f);
        ImGui::TableHeadersRow();

        for (int i = 0; i < static_cast<int>(mappings_.size()); ++i) {
            auto& m = mappings_[i];
            ImGui::TableNextRow();

            const bool isAutoInsert = insertMode_ && m.isPrimaryKey;
            if (isAutoInsert)
                ImGui::BeginDisabled();

            ImGui::TableSetColumnIndex(0);
            if (m.isPrimaryKey) {
                ImGui::TextDisabled("%s", icons::Lock);
                ImGui::SameLine(0.0f, 4.0f);
            }
            ImGui::Text("%s (%s)", m.targetColumn.c_str(), m.typeName.c_str());
            if (!insertMode_ && m.isPrimaryKey) {
                ImGui::SameLine(0.0f, 4.0f);
                ImGui::TextDisabled("[WHERE]");
            }

            ImGui::TableSetColumnIndex(1);
            {
                const char* preview = (m.sourceChoice == -2)   ? "(auto/skip)"
                                      : (m.sourceChoice == -1) ? "(manual)"
                                      : (m.sourceChoice < static_cast<int>(srcColNames_.size()))
                                          ? srcColNames_[m.sourceChoice].c_str()
                                          : "(invalid)";

                const std::string comboId = std::format("##src_{}", i);
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::BeginCombo(comboId.c_str(), preview)) {
                    if (ImGui::Selectable("(auto/skip)", m.sourceChoice == -2))
                        m.sourceChoice = -2;
                    if (ImGui::Selectable("(manual)", m.sourceChoice == -1))
                        m.sourceChoice = -1;
                    for (int j = 0; j < static_cast<int>(srcColNames_.size()); ++j) {
                        const bool sel = (m.sourceChoice == j);
                        if (ImGui::Selectable(srcColNames_[j].c_str(), sel))
                            m.sourceChoice = j;
                        if (sel)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            }

            ImGui::TableSetColumnIndex(2);
            if (isAutoInsert) {
                ImGui::TextDisabled("(auto)");
            } else if (m.sourceChoice >= 0 && m.sourceChoice < static_cast<int>(srcValues_.size())) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
                ImGui::TextUnformatted(srcValues_[m.sourceChoice].c_str());
                ImGui::PopStyleColor();
            } else if (m.sourceChoice == -1) {
                const std::string inputId = std::format("##val_{}", i);
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputText(inputId.c_str(), m.manualBuf, sizeof(m.manualBuf));
            } else {
                ImGui::TextDisabled("(skip)");
            }

            if (isAutoInsert)
                ImGui::EndDisabled();
        }

        ImGui::EndTable();
    }

    if (!crudError_.empty()) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
        ImGui::TextWrapped("%s  %s", icons::Warning, crudError_.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
        ImGui::CloseCurrentPopup();
        Close();
    }
    ImGui::SameLine();

    const char* actionLabel = insertMode_ ? "INSERT" : "UPDATE";
    if (ImGui::Button(actionLabel, ImVec2(100.0f, 0.0f))) {
        crudError_.clear();

        if (insertMode_) {
            std::unordered_map<std::string, std::string> vals;
            for (const auto& m : mappings_) {
                if (m.isPrimaryKey)  continue;
                if (m.sourceChoice == -2) continue;

                std::string val;
                if (m.sourceChoice >= 0 && m.sourceChoice < static_cast<int>(srcValues_.size())) {
                    val = srcValues_[m.sourceChoice];
                } else if (m.sourceChoice == -1) {
                    val = m.manualBuf;
                } else {
                    continue;
                }
                vals[m.targetColumn] = std::move(val);
            }

            auto res = target_->GetSource()->InsertRow(std::string(rowPayload_.tableName), vals);
            if (!res) {
                crudError_ = res.error();
            } else {
                target_->InvalidateData();
                ImGui::CloseCurrentPopup();
                Close();
            }
        } else {
            std::unordered_map<std::string, std::string> pkVals;
            std::unordered_map<std::string, std::string> newVals;

            for (const auto& m : mappings_) {
                if (m.sourceChoice == -2) continue;

                std::string val;
                if (m.sourceChoice >= 0 && m.sourceChoice < static_cast<int>(srcValues_.size())) {
                    val = srcValues_[m.sourceChoice];
                } else if (m.sourceChoice == -1) {
                    val = m.manualBuf;
                } else {
                    continue;
                }

                if (m.isPrimaryKey)
                    pkVals[m.targetColumn] = std::move(val);
                else
                    newVals[m.targetColumn] = std::move(val);
            }

            auto res = target_->GetSource()->UpdateRow(std::string(rowPayload_.tableName), pkVals, newVals);
            if (!res) {
                crudError_ = res.error();
            } else {
                target_->InvalidateData();
                ImGui::CloseCurrentPopup();
                Close();
            }
        }
    }

    ImGui::EndPopup();
}

void DropDialogManager::RenderFsCopyMoveDialog()
{
    ImGui::SetNextWindowSize(ImVec2(480.0f, 0.0f), ImGuiCond_Always);
    if (!ImGui::BeginPopupModal("##dd_fscopy", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (kind_ == Kind::FsCopyMove)
            Close();
        return;
    }

    const bool  isDir   = std::string_view(fsCopyPayload_.kind) == "dir";
    const char* srcIcon = isDir ? icons::Folder : icons::File;

    ImGui::TextUnformatted(srcIcon);
    ImGui::SameLine(0.0f, 6.0f);
    ImGui::TextUnformatted(fsCopyPayload_.name);
    ImGui::TextDisabled("  from: %s", fsCopyPayload_.path);

    ImGui::Spacing();

    ImGui::TextUnformatted(icons::Folder);
    ImGui::SameLine(0.0f, 4.0f);
    ImGui::TextUnformatted("Destination:");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##dd_fsdst", fsDstBuf_, sizeof(fsDstBuf_));

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Cancel", ImVec2(90.0f, 0.0f))) {
        ImGui::CloseCurrentPopup();
        Close();
    }
    ImGui::SameLine();

    const auto doFsOp = [&](bool move) {
        const fs::path  src = fsCopyPayload_.path;
        const fs::path  dst = fs::path(fsDstBuf_) / fsCopyPayload_.name;
        std::error_code ec;

        if (move) {
            fs::rename(src, dst, ec);
        } else {
            const auto copyFlags = fs::copy_options::overwrite_existing | fs::copy_options::recursive;
            fs::copy(src, dst, copyFlags, ec);
        }

        if (ec) {
            crudError_ = ec.message();
        } else {
            if (target_)
                target_->InvalidateData();
            ImGui::CloseCurrentPopup();
            Close();
        }
    };

    if (ImGui::Button("COPY", ImVec2(90.0f, 0.0f)))
        doFsOp(false);
    ImGui::SameLine();
    if (ImGui::Button("MOVE", ImVec2(90.0f, 0.0f)))
        doFsOp(true);

    if (!crudError_.empty()) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
        ImGui::TextWrapped("%s  %s", icons::Warning, crudError_.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::EndPopup();
}

void DropDialogManager::TriggerFileToView(const io::FilePayload& fp, DataBrowser* target)
{
    kind_              = Kind::FileToView;
    target_            = target;
    fileToViewPayload_ = fp;
    fileToViewError_.clear();

    const std::string stem = fs::path(fp.path).stem().string();
    std::snprintf(fileToViewNameBuf_, sizeof(fileToViewNameBuf_), "%s", stem.c_str());

    ImGui::OpenPopup("Create View from File");
}

void DropDialogManager::RenderFileToViewDialog()
{
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(480.0f, 0.0f), ImGuiCond_Appearing);

    if (!ImGui::BeginPopupModal("Create View from File", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::TextUnformatted("Create DuckDB view from file:");
    ImGui::TextDisabled("%s", fileToViewPayload_.path);
    ImGui::Spacing();

    constexpr float kLabelW = 90.0f;
    ImGui::Text("View name");
    ImGui::SameLine(kLabelW);
    ImGui::SetNextItemWidth(-1.0f);
    const bool enter = ImGui::InputText(
        "##viewname", fileToViewNameBuf_, sizeof(fileToViewNameBuf_), ImGuiInputTextFlags_EnterReturnsTrue);

    if (!fileToViewError_.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", fileToViewError_.c_str());
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const bool doCreate = ImGui::Button("Create View", ImVec2(120.0f, 0.0f)) || enter;
    ImGui::SameLine();
    const bool doCancel = ImGui::Button("Cancel", ImVec2(80.0f, 0.0f));

    if (doCreate && fileToViewNameBuf_[0] != '\0' && target_) {
#ifdef DATAGRID_HAVE_DUCKDB
        auto* duck = dynamic_cast<Adapters::DuckDBAdapter*>(target_->GetSource());
        if (duck) {
            const auto res = duck->ScanFile(fileToViewPayload_.path, std::string{fileToViewNameBuf_});
            if (!res) {
                fileToViewError_ = res.error();
            } else {
                target_->InvalidateData();
                fileToViewError_.clear();
                ImGui::CloseCurrentPopup();
                Close();
            }
        }
#else
        // DuckDB not available in this build — close without action.
        ImGui::CloseCurrentPopup();
        Close();
#endif
    }

    if (doCancel) {
        ImGui::CloseCurrentPopup();
        Close();
    }

    ImGui::EndPopup();
}

} // namespace datagrid::ui

