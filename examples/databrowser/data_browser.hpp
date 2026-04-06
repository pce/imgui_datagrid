#pragma once
#include "adapters/data_source.hpp"
#include "imgui.h"
#include "imgui_datagrid.hpp"
#include "inspector/schema_inspector.hpp"
#include "ui/hex_view.hpp"
#include "ui/responsive_layout.hpp"
#include "ui/sql_editor.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "io/platform.hpp"

namespace datagrid {

class DataBrowser
{
  public:
    /// Construct with an already-connected data source.
    /// The browser takes ownership of the adapter.
    explicit DataBrowser(adapters::DataSourcePtr source, const std::string& windowTitle = "Data Browser");

    ~DataBrowser() = default;

    // Non-copyable — owns a live data source connection.
    DataBrowser(const DataBrowser&)            = delete;
    DataBrowser& operator=(const DataBrowser&) = delete;

    // Non-movable — SchemaInspector member explicitly deletes its move ops.
    DataBrowser(DataBrowser&&)            = delete;
    DataBrowser& operator=(DataBrowser&&) = delete;

    /// Inject the current layout before calling Render().
    void SetLayout(const ui::ResponsiveLayout& layout);

    /// Callback invoked once after columns are built from the adapter schema.
    /// Use it to set per-column widths, types, renderers, and visibility.
    using ColumnCustomizer = std::function<void(std::vector<ImGuiExt::ColumnDef>&)>;
    void SetColumnCustomizer(ColumnCustomizer fn);

    /// Rendered once per frame inside the DataBrowser window, between the toolbar
    /// and the main sidebar/grid layout.  Use it to inject adapter-specific UI
    /// (e.g. a filesystem navigation bar) without coupling DataBrowser to any
    /// specific adapter.  Pass nullptr to clear.
    using PreContentHook = std::function<void()>;
    void SetPreContentHook(PreContentHook fn);

    /// Receives the zero-based row index into the current page and the row data.
    using RowCallback = std::function<void(int rowIdx, const std::vector<std::string>& row)>;
    void SetOnRowClick(RowCallback fn);
    void SetOnRowDblClick(RowCallback fn);

    /// Call once per frame inside your ImGui begin/end block.
    /// Manages its own ImGui::Begin / ImGui::End.
    void Render();

    /// Open the schema inspector for the given table, or the current table
    /// if tableName is empty.  Safe to call from UI callbacks.
    void OpenInspector(const std::string& tableName = "");

    /// Replace the current data source at runtime (e.g. user opens a different
    /// file).  Resets all query state and triggers a full re-initialise.
    void SetDataSource(adapters::DataSourcePtr source);

    [[nodiscard]] bool        IsConnected() const;
    [[nodiscard]] std::string AdapterLabel() const;
    [[nodiscard]] std::string WindowTitle() const;
    void                      SetWindowTitle(const std::string& title);

    /// Navigate to `tableOrPath` — equivalent to clicking the table in the
    /// sidebar.  For filesystem adapters `tableOrPath` is the directory path.
    void NavigateTo(const std::string& tableOrPath);

    /// Raw pointer to the underlying data source — useful for dynamic_cast to
    /// adapter-specific types in callbacks / hooks.  Never nullptr while connected.
    [[nodiscard]] adapters::IDataSource* GetSource() const;

    [[nodiscard]] bool IsInspectorOpen() const;
    void               CloseInspector();

    /// Force a data reload on the next Render() call.
    void InvalidateData();

    /// Enable row dragging. The callback is called inside ImGui::BeginDragDropSource —
    /// it must call ImGui::SetDragDropPayload() and may render a drag-preview tooltip.
    using DragSourceCallback = std::function<void(int rowIdx, const std::vector<std::string>& row)>;
    void SetDragSourceCallback(DragSourceCallback fn);

    /// Set a drop handler. Called when a payload is dropped on the DataGrid area.
    /// `payloadType` is the ImGui type string; `data`/`dataSize` is the raw payload.
    using DropHandler = std::function<void(const char* payloadType, const void* data, std::size_t dataSize)>;
    void SetDropHandler(DropHandler fn);

    /// Open-file callback — invoked from the row context menu when the user selects
    /// an "Open …" item.  `how` is one of:
    ///   "auto"    — smart dispatch (App decides based on content / extension)
    ///   "image"   — open in image viewer
    ///   "text"    — open in text viewer
    ///   "sqlite"  — open as SQLite database
    ///   "hex"     — inspect raw bytes in hex view
    ///   "system"  — hand off to the OS default application
    using OpenCallback = std::function<void(const std::string& path, const std::string& how)>;
    void SetOpenCallback(OpenCallback fn);

    /// Set the monospace font forwarded to the byte-inspector hex view.
    /// Call whenever the active theme changes.
    void SetCodeFont(ImFont* f) noexcept;

    /// Column keys for the current table (order matches GetSource()->GetColumns()).
    /// Used by drag-source callbacks to build the key=value row encoding.
    [[nodiscard]] std::vector<std::string> GetCurrentColumnKeys() const;

    /// Read-only access to the rows currently displayed (current page).
    [[nodiscard]] const std::vector<std::vector<std::string>>& GetCurrentRows() const { return rows; }

    /// Zero-based index of the column whose key matches `key`, or -1 if not found.
    [[nodiscard]] int GetColumnIndex(const std::string& key) const
    {
        for (int i = 0; i < static_cast<int>(columns.size()); ++i)
            if (columns[i].key == key) return i;
        return -1;
    }

    [[nodiscard]] std::string CurrentTable() const;

    /// Zero-based instance ID (unique per DataBrowser lifetime).
    [[nodiscard]] int InstanceId() const { return instanceId_; }

    /// True when this DataBrowser window was focused during the last rendered frame.
    /// Updated inside Render() — one frame lag is expected and acceptable.
    [[nodiscard]] bool IsFocused() const { return focused_; }

    /// The full ImGui window-ID string passed to ImGui::Begin() every frame.
    /// Use with ImGui::SetWindowFocus() to bring this window to the front.
    [[nodiscard]] std::string ImGuiWindowId() const;

    /// If the schema inspector is open, returns its ImGui window ID for SetWindowFocus.
    /// Returns an empty string when the inspector is closed or has no data yet.
    [[nodiscard]] std::string InspectorImGuiWindowId() const;

    /// Display label of the inspector's current table, or empty if not open.
    [[nodiscard]] std::string InspectorWindowLabel() const;

  private:
    void DrawToolbar();
    void DrawSidebar();
    void DrawSidebarContent(); ///< Shared by sidebar panel and phone overlay
    void DrawPhoneOverlay();   ///< Full-screen sidebar overlay for Phone layout
    void DrawMainContent();
    void RenderInsertPopup();
    void RenderDeleteConfirm();

    void LoadSchema();                              ///< Load tables from adapter; refresh sidebar list
    void SelectTable(const std::string& tableName); ///< Rebuild columns, reset pagination
    void BuildColumns();                            ///< Rebuild columns from adapter schema + customizer
    void RefreshData();                             ///< Re-execute current query; update rows + totalRows

    adapters::DataSourcePtr source;

    adapters::DataQuery query;
    int                 totalRows = 0;

    std::vector<ImGuiExt::ColumnDef>      columns;
    std::vector<std::vector<std::string>> rows;
    ImGuiExt::DataGridState               gridState;

    std::vector<adapters::TableInfo> tables;

    ui::ResponsiveLayout layout_;

    bool        showSidebar       = true;
    bool        phoneOverlayOpen_ = false;

    // Exact-match filter (maps to DataQuery::whereExact)
    std::string filterColumn;
    char        filterBuf[256] = {};

    // Substring search (maps to DataQuery::searchColumn / searchValue)
    std::string searchColumn;
    char        searchBuf[256] = {};

    ui::SqlEditorState sqlEditor_; ///< Embedded SQL editor widget state

    std::string lastError;
    std::string statusMsg;

    std::string windowTitle;
    bool        needsRefresh = false;

    bool        editMode_ = false; ///< Toggled by the ✎ toolbar button
    bool        focused_  = false; ///< True when this DataBrowser window had focus last frame
    std::string editError_;        ///< Last UpdateRow error message, if any

    bool showInsertPopup_   = false;
    bool showDeleteConfirm_ = false;
    struct InsertField
    {
        char buf[256] = {};
    };
    std::vector<InsertField> insertFields_; ///< One slot per column, rebuilt on popup open
    std::string              crudError_;

    bool columnsReady = false;
    bool schemaLoaded = false;

    inspector::SchemaInspector inspector_;
    ui::HexViewDialog          hexView_; ///< Byte-inspector popup (context menu → Inspect Bytes…)

    ColumnCustomizer columnCustomizer;
    RowCallback      onRowClick;
    RowCallback      onRowDblClick;

    PreContentHook preContentHook_;

    DragSourceCallback dragSourceCb_;
    DropHandler        dropHandler_;
    OpenCallback       openCb_;

    int         instanceId_;
    std::string idSuffix_; ///< "_N" — appended to ImGui IDs
    static int  nextInstanceId_;
};


} // namespace datagrid
