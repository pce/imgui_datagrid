#pragma once
#include "adapters/data_source.hpp"
#include "data_browser.hpp"
#include "drag_drop.hpp"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace UI {

class DropDialogManager
{
  public:
    /// Called when user confirms "Open in new window"
    std::function<void(const std::string& adapter, const std::string& path)> onOpenNewWindow;

    /// Called when user confirms "Replace this window's connection"
    std::function<void(const std::string& adapter, const std::string& path, DataBrowser* target)> onReplaceWindow;

    /// DB file dragged onto a DB browser window (*.db / *.sqlite3 / *.duckdb).
    void TriggerDbFileOpen(const FilePayload& fp, DataBrowser* target);

    /// Row from any browser dragged onto a writable DB browser.
    /// targetCols  — result of target->GetSource()->GetColumns(table)
    /// srcColNames / srcValues — decoded from RowPayload::rowData
    void TriggerRowInsert(const RowPayload&                 rp,
                          DataBrowser*                      target,
                          std::vector<Adapters::ColumnInfo> targetCols,
                          std::vector<std::string>          srcColNames,
                          std::vector<std::string>          srcValues);

    /// File dragged onto a filesystem browser window.
    /// dstDir — current directory of the target FS browser.
    void TriggerFsCopyMove(const FilePayload& fp, std::string dstDir, DataBrowser* target);

    /// Queryable file (CSV / Parquet / JSON) dragged onto a DuckDB browser —
    /// prompts for a view name then calls duck->ScanFile().
    void TriggerFileToView(const FilePayload& fp, DataBrowser* target);

    void Render();

    [[nodiscard]] bool IsOpen() const noexcept { return kind_ != Kind::None; }

  private:
    enum class Kind
    {
        None,
        DbFileOpen,
        RowInsert,
        FsCopyMove,
        FileToView
    };
    Kind kind_ = Kind::None;

    DataBrowser* target_ = nullptr;

    FilePayload filePayload_{};

    FileDbType  sniffedType_   = FileDbType::Unknown;
    bool        openNewWindow_ = true; // true=new window, false=replace

    struct ColumnMapping
    {
        std::string targetColumn;
        std::string typeName;
        bool        nullable     = true;
        bool        isPrimaryKey = false;
        // -2 = auto/skip; -1 = manual; >=0 = index into srcColNames_
        int  sourceChoice   = -1;
        char manualBuf[256] = {};
    };

    RowPayload                 rowPayload_{};
    std::vector<ColumnMapping> mappings_;
    std::vector<std::string>   srcColNames_;
    std::vector<std::string>   srcValues_;
    bool                       insertMode_ = true; // true=INSERT, false=UPDATE
    std::string                crudError_;

    FilePayload fsCopyPayload_{};
    std::string fsDstDir_;
    char        fsDstBuf_[1024] = {};

    FilePayload fileToViewPayload_{};
    char        fileToViewNameBuf_[128] = {};
    std::string fileToViewError_;

    void RenderDbFileOpenDialog();
    void RenderRowInsertDialog();
    void RenderFsCopyMoveDialog();
    void RenderFileToViewDialog();
    void Close();
};

} // namespace UI

