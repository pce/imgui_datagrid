#pragma once
/// DataBrowser — generic data-browser panel that works with any IDataSource adapter.
/// See docs/databrowser.md for the full usage guide.
///
///  ┌──────────────────────────────────────────────────────────┐
///  │ [toolbar: adapter label | sidebar toggle | status]       │
///  ├──────────────┬───────────────────────────────────────────┤
///  │              │  [SQL editor strip — collapsible]         │
///  │  Sidebar     ├───────────────────────────────────────────┤
///  │  ─────────   │                                           │
///  │  Tables      │         DataGrid                          │
///  │  ─────────   │                                           │
///  │  Filters     │                                           │
///  │  ─────────   ├───────────────────────────────────────────┤
///  │  Columns     │  [pagination bar]                         │
///  └──────────────┴───────────────────────────────────────────┘

#include "imgui_datagrid.hpp"
#include "adapters/data_source.hpp"
#include "responsive_layout.hpp"
#include "inspector/schema_inspector.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>


class DataBrowser {
public:
    // ── Construction ──────────────────────────────────────────────────────────

    /// Construct with an already-connected data source.
    /// The browser takes ownership of the adapter.
    explicit DataBrowser(
        Adapters::DataSourcePtr source,
        const std::string&      windowTitle = "Data Browser"
    );

    ~DataBrowser() = default;

    // Non-copyable — owns a live data source connection.
    DataBrowser(const DataBrowser&)            = delete;
    DataBrowser& operator=(const DataBrowser&) = delete;

    // Non-movable — SchemaInspector member explicitly deletes its move ops.
    DataBrowser(DataBrowser&&)                 = delete;
    DataBrowser& operator=(DataBrowser&&)      = delete;

    // ── Responsive layout ─────────────────────────────────────────────────────

    /// Inject the current layout before calling Render().
    void SetLayout(const ResponsiveLayout& layout);

    // ── Column customisation ──────────────────────────────────────────────────

    /// Callback invoked once after columns are built from the adapter schema.
    /// Use it to set per-column widths, types, renderers, and visibility.
    using ColumnCustomizer = std::function<void(std::vector<ImGuiExt::ColumnDef>&)>;
    void SetColumnCustomizer(ColumnCustomizer fn);

    // ── Pre-content hook (adapter-specific toolbar/nav bar) ───────────────────

    /// Rendered once per frame inside the DataBrowser window, between the toolbar
    /// and the main sidebar/grid layout.  Use it to inject adapter-specific UI
    /// (e.g. a filesystem navigation bar) without coupling DataBrowser to any
    /// specific adapter.  Pass nullptr to clear.
    using PreContentHook = std::function<void()>;
    void SetPreContentHook(PreContentHook fn);

    // ── Row callbacks ─────────────────────────────────────────────────────────

    /// Receives the zero-based row index into the current page and the row data.
    using RowCallback = std::function<void(int rowIdx, const std::vector<std::string>& row)>;
    void SetOnRowClick   (RowCallback fn);
    void SetOnRowDblClick(RowCallback fn);

    // ── Per-frame entry point ─────────────────────────────────────────────────

    /// Call once per frame inside your ImGui begin/end block.
    /// Manages its own ImGui::Begin / ImGui::End.
    void Render();

    // ── Inspector ─────────────────────────────────────────────────────────────

    /// Open the schema inspector for the given table, or the current table
    /// if tableName is empty.  Safe to call from UI callbacks.
    void OpenInspector(const std::string& tableName = "");

    // ── Runtime adapter swap ──────────────────────────────────────────────────

    /// Replace the current data source at runtime (e.g. user opens a different
    /// file).  Resets all query state and triggers a full re-initialise.
    void SetDataSource(Adapters::DataSourcePtr source);

    // ── Accessors ─────────────────────────────────────────────────────────────

    [[nodiscard]] bool        IsConnected()  const;
    [[nodiscard]] std::string AdapterLabel() const;
    [[nodiscard]] std::string WindowTitle()  const;
    void SetWindowTitle(const std::string& title);

    /// Navigate to `tableOrPath` — equivalent to clicking the table in the
    /// sidebar.  For filesystem adapters `tableOrPath` is the directory path.
    void NavigateTo(const std::string& tableOrPath);

    /// Raw pointer to the underlying data source — useful for dynamic_cast to
    /// adapter-specific types in callbacks / hooks.  Never nullptr while connected.
    [[nodiscard]] Adapters::IDataSource* GetSource() const;

    // ── Inspector forwarding ──────────────────────────────────────────────────
    [[nodiscard]] bool IsInspectorOpen() const;
    void               CloseInspector();

    /// Force a data reload on the next Render() call.
    void InvalidateData();

private:
    // ── Sub-panel renderers ───────────────────────────────────────────────────
    void DrawToolbar();
    void DrawSidebar();
    void DrawSidebarContent();  ///< Shared by sidebar panel and phone overlay
    void DrawPhoneOverlay();    ///< Full-screen sidebar overlay for Phone layout
    void DrawMainContent();
    void DrawSqlEditor();
    void RenderInsertPopup();
    void RenderDeleteConfirm();

    // ── Data helpers ──────────────────────────────────────────────────────────
    void LoadSchema();                             ///< Load tables from adapter; refresh sidebar list
    void SelectTable(const std::string& tableName); ///< Rebuild columns, reset pagination
    void BuildColumns();                           ///< Rebuild columns from adapter schema + customizer
    void RefreshData();                            ///< Re-execute current query; update rows + totalRows

    // ── Data source ───────────────────────────────────────────────────────────
    Adapters::DataSourcePtr               source;

    // ── Query state ───────────────────────────────────────────────────────────
    Adapters::DataQuery                   query;
    int                                   totalRows     = 0;

    // ── View data (current page) ──────────────────────────────────────────────
    std::vector<ImGuiExt::ColumnDef>      columns;
    std::vector<std::vector<std::string>> rows;
    ImGuiExt::DataGridState               gridState;

    // ── Schema cache ──────────────────────────────────────────────────────────
    std::vector<Adapters::TableInfo>      tables;

    // ── Responsive layout ─────────────────────────────────────────────────────
    ResponsiveLayout                      layout_;

    // ── Sidebar UI state ──────────────────────────────────────────────────────
    bool                                  showSidebar       = true;
    bool                                  phoneOverlayOpen_ = false;

    // Exact-match filter (maps to DataQuery::whereExact)
    std::string                           filterColumn;
    char                                  filterBuf[256]  = {};

    // Substring search (maps to DataQuery::searchColumn / searchValue)
    std::string                           searchColumn;
    char                                  searchBuf[256]  = {};

    // ── SQL editor state ──────────────────────────────────────────────────────
    bool                                  showSqlEditor = false;
    char                                  sqlBuf[2048]  = {};
    std::string                           sqlError;
    std::string                           sqlStatus;    ///< e.g. "42 rows  (3.1 ms)"

    // ── Status ────────────────────────────────────────────────────────────────
    std::string                           lastError;
    std::string                           statusMsg;

    // ── Misc ──────────────────────────────────────────────────────────────────
    std::string                           windowTitle;
    bool                                  needsRefresh  = false;

    // ── Edit mode ─────────────────────────────────────────────────────────
    bool        editMode_  = false;   ///< Toggled by the ✎ toolbar button
    std::string editError_;           ///< Last UpdateRow error message, if any

    // ── CRUD popup state ──────────────────────────────────────────────────────
    bool        showInsertPopup_    = false;
    bool        showDeleteConfirm_  = false;
    struct InsertField { char buf[256] = {}; };
    std::vector<InsertField> insertFields_;  ///< One slot per column, rebuilt on popup open
    std::string crudError_;

    bool                                  columnsReady  = false;
    bool                                  schemaLoaded  = false;

    // ── Inspector ─────────────────────────────────────────────────────────────
    Inspector::SchemaInspector            inspector_;

    // ── Callbacks ─────────────────────────────────────────────────────────────
    ColumnCustomizer                      columnCustomizer;
    RowCallback                           onRowClick;
    RowCallback                           onRowDblClick;

    PreContentHook preContentHook_;

    // ── Unique instance ID (keeps ImGui window IDs distinct when two browsers open) ──
    int         instanceId_;
    std::string idSuffix_;       ///< "_N" — appended to ImGui IDs
    static int  nextInstanceId_; ///< static counter, incremented in constructor
};
