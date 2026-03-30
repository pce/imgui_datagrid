#pragma once
// ============================================================
//  imgui_datagrid.hpp  —  Dear ImGui DataGrid extension
//
//  Drop-in, single translation-unit widget.
//  Requires Dear ImGui >= 1.87 (Tables API) and C++23.
//
//  USAGE
//  ─────
//  1.  Copy imgui_datagrid.hpp + imgui_datagrid.cpp into your project.
//  2.  Define IMGUI_DATAGRID_USE_JSON before including this header
//      (and ensure nlohmann/json.hpp is reachable) to enable
//      layout save / restore helpers.
//
//  QUICK START
//  ───────────
//      std::vector<ImGuiExt::ColumnDef> cols = {
//          { .key="id",      .label="#",    .type=ImGuiExt::ColumnType::Number, .initWidth=40  },
//          { .key="from",    .label="From", .initWidth=160 },
//          { .key="subject", .label="Subject" },
//      };
//      ImGuiExt::DataGridState   state;
//      ImGuiExt::DataGridOptions opts{ .id="##mail",
//                                      .onRowDblClick=[&](int r){ open(r); } };
//      // per frame:
//      if (ImGuiExt::DataGrid(cols, rows, state, opts) && state.sortChanged)
//          reload(state.sortColumnKey, state.sortAscending);
//      ImGuiExt::DataGridPagination(page, pageSize, totalRows);
//
//  LICENSE: MIT
// ============================================================

#include "imgui.h"
#include <functional>
#include <string>
#include <vector>

#ifdef IMGUI_DATAGRID_USE_JSON
#  include <nlohmann/json.hpp>
#endif

namespace ImGuiExt {

// ── ColumnType ─────────────────────────────────────────────────────────────
/// Controls default cell alignment and sort/tooltip behaviour.
enum class ColumnType {
    Text,    ///< Left-aligned plain text (default)
    Number,  ///< Right-aligned numeric string
    Date,    ///< Right-aligned; sorts correctly with ISO-8601 strings
    Custom,  ///< Rendering fully delegated to ColumnDef::renderer
};

// ── ColumnPolicy ───────────────────────────────────────────────────────────
/// Declares the runtime behaviour of a column.
/// Apply via DataBrowser::SetColumnCustomizer or load from JSON config.
///
/// C++23 fluent builder — deducing `this` lets you chain methods without CRTP:
///
///   ColumnPolicy p;
///   p.withEditable(true).withRenderer("image").withClickNavigates(true);
///
struct ColumnPolicy {
    bool        editable        = false;  ///< Inline edit on double-click
    bool        sortable        = true;
    bool        visible         = true;
    bool        resizable       = true;
    std::string rendererName;   ///< "" = default text, "image", "badge", "path", …
    bool        clickNavigates  = false;  ///< Single-click navigates (macOS convention)
    bool        dblClickNavigates = false; ///< Double-click navigates (Windows convention)

    // C++23: deducing `this` — fluent setters that work on both lvalue & rvalue
    auto& withEditable        (this auto& self, bool v)              { self.editable         = v; return self; }
    auto& withVisible         (this auto& self, bool v)              { self.visible          = v; return self; }
    auto& withSortable        (this auto& self, bool v)              { self.sortable         = v; return self; }
    auto& withRenderer        (this auto& self, std::string_view r)  { self.rendererName     = r; return self; }
    auto& withClickNavigates  (this auto& self, bool v)              { self.clickNavigates   = v; return self; }
    auto& withDblClickNavigates(this auto& self, bool v)             { self.dblClickNavigates= v; return self; }
};

/// Convenience: returns the navigate-trigger policy for the current platform.
/// macOS: click navigates.  All others: double-click navigates.
[[nodiscard]] inline ColumnPolicy PlatformNavigatePolicy()
{
    ColumnPolicy p;
#if defined(__APPLE__)
    p.clickNavigates = true;
#else
    p.dblClickNavigates = true;
#endif
    return p;
}

// ── ColumnDef ──────────────────────────────────────────────────────────────
/// Describes one column.  Declaration order == display order.
struct ColumnDef {
    /// Internal key used in DataGridState::sortColumnKey and callbacks.
    std::string key;
    /// Text shown in the column header.
    std::string label;

    /// Initial width in pixels.  0 = ImGui auto / stretch.
    float initWidth = 0.0f;

    bool visible   = true;
    bool sortable  = true;
    bool resizable = true;
    bool editable  = false;  ///< Allow inline editing (double-click to activate)

    ColumnType            type       = ColumnType::Text;
    ImGuiTableColumnFlags extraFlags = ImGuiTableColumnFlags_None;

    /// Custom cell renderer.  When set, called instead of the built-in
    /// text rendering for every cell in this column.
    ///
    ///   col.renderer = [](const std::string& v, int /*row*/) {
    ///       if (ImGui::SmallButton(("Open##" + v).c_str()))
    ///           OpenFile(v);
    ///   };
    std::function<void(const std::string& value, int rowIndex)> renderer = nullptr;

    /// Called when a cell in this column is single-clicked.
    /// Useful for "click path → navigate" (macOS) or custom cell actions.
    std::function<void(const std::string& value, int rowIdx)> onCellClick    = nullptr;

    /// Called when a cell in this column is double-clicked.
    /// Useful for "double-click path → navigate" (Windows/Linux).
    std::function<void(const std::string& value, int rowIdx)> onCellDblClick = nullptr;
};

// ── DataGridState ──────────────────────────────────────────────────────────
/// Persistent widget state — own this as a class member across frames.
///
/// The one-frame signal fields (sortChanged, selectionChanged) are
/// automatically cleared at the start of every DataGrid() call.
struct DataGridState {
    std::string sortColumnKey;
    bool        sortAscending    = true;

    /// Index into the `rows` vector of the selected row; -1 = none.
    int         selectedRow      = -1;

    // One-frame signals — read immediately after DataGrid() returns.
    bool        sortChanged      = false;
    bool        selectionChanged = false;

    // ── Inline-edit state (managed by DataGrid; caller reads onCellEdit callback) ──
    int  editingRow    = -1;     ///< -1 = not editing
    int  editingCol    = -1;
    bool editFocusDone = false;  ///< true once SetKeyboardFocusHere was called
    char editBuf[512]  = {};
};

// ── DataGridOptions ────────────────────────────────────────────────────────
/// Per-call configuration and callbacks.  Safe to construct as a temporary.
struct DataGridOptions {
    /// ImGui table ID — must be unique per window if multiple grids are shown.
    const char* id = "##DataGrid";

    /// Maximum scrollable height in pixels.  0 = fill remaining space.
    float maxHeight = 0.0f;

    bool stickyHeader = true;  ///< Freeze header row while scrolling
    bool rowSelection = true;  ///< Highlight and track clicked rows

    /// Additional ImGuiTableFlags OR-ed in at BeginTable.
    ImGuiTableFlags extraFlags = ImGuiTableFlags_None;

    /// Minimum row height in logical pixels.
    /// Pass layout.touchTargetPx (44 px) for phone / tablet targets.
    float minRowHeight = 0.0f;

    std::function<void(int rowIndex)> onRowClick    = nullptr;  ///< Single-click row
    std::function<void(int rowIndex)> onRowDblClick = nullptr;  ///< Double-click row

    /// Right-click context menu.  Called inside BeginPopupContextItem —
    /// render MenuItem calls here; no Begin/EndPopup needed.
    std::function<void(int rowIndex)> contextMenu = nullptr;

    /// Called when the user commits an inline cell edit (Enter / focus-loss).
    /// `colIdx` is the logical index into `columns` (including hidden columns).
    /// Return true to accept the value; false to reject (data refreshes either way).
    std::function<bool(int rowIdx, int colIdx, const std::string& newValue)>
        onCellEdit = nullptr;
};

// ── Public API ─────────────────────────────────────────────────────────────

/// Draw the data grid.
///
/// @param columns  Column definitions.  Visibility flags may be mutated by
///                 ImGui's built-in hide/show header context menu.
/// @param rows     Page data.  rows[r][c] must align with columns[c].
///                 Caller handles pagination — pass only the current page.
/// @param state    Persistent state object (sort + selection).
/// @param options  Per-call options and callbacks.
/// @returns true if state changed this frame (sort or selection).
[[nodiscard]] bool DataGrid(
    std::vector<ColumnDef>&                      columns,
    const std::vector<std::vector<std::string>>& rows,
    DataGridState&                               state,
    const DataGridOptions&                       options = {}
);

/// Render checkboxes to toggle per-column visibility.
/// Place inside a CollapsingHeader, BeginChild, or BeginPopup.
void DataGridColumnVisibility(std::vector<ColumnDef>& columns);

/// Compact pagination bar:  ← Page N / M   N rows →
///
/// @param currentPage  Zero-based index; mutated on user interaction.
/// @param rowsPerPage  Rows per page (used to derive the last page).
/// @param totalRows    Total rows across all pages; -1 = unknown
///                     (disables last-page guard and page-count label).
/// @returns true if the page changed this frame.
[[nodiscard]] bool DataGridPagination(int& currentPage, int rowsPerPage, int totalRows = -1);

// ── Optional JSON layout persistence ──────────────────────────────────────
#ifdef IMGUI_DATAGRID_USE_JSON

/// Serialise column visibility / widths and current sort state to JSON.
[[nodiscard]] nlohmann::json DataGridSaveLayout(
    const std::vector<ColumnDef>& columns,
    const DataGridState&          state
);

/// Restore column visibility / widths and sort state from JSON.
/// Unknown keys are silently ignored.
void DataGridLoadLayout(
    std::vector<ColumnDef>& columns,
    DataGridState&          state,
    const nlohmann::json&   j
);

/// Write layout to a file on disk.  Returns false on I/O error.
[[nodiscard]] bool DataGridSaveLayoutToFile(
    const std::vector<ColumnDef>& columns,
    const DataGridState&          state,
    const std::string&            filepath
);

/// Read layout from a file on disk.
/// Returns false on I/O error or JSON parse failure.
[[nodiscard]] bool DataGridLoadLayoutFromFile(
    std::vector<ColumnDef>& columns,
    DataGridState&          state,
    const std::string&      filepath
);

#endif // IMGUI_DATAGRID_USE_JSON

} // namespace ImGuiExt
