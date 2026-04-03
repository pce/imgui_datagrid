#pragma once
// MIT License

#include "imgui.h"
#include <functional>
#include <string>
#include <variant>
#include <vector>

#ifdef IMGUI_DATAGRID_USE_JSON
#include <nlohmann/json.hpp>
#endif

namespace ImGuiExt {


/// Controls default cell alignment and sort/tooltip behaviour.
enum class ColumnType
{
    Text,   ///< Left-aligned plain text (default)
    Number, ///< Right-aligned numeric string
    Date,   ///< Right-aligned; sorts correctly with ISO-8601 strings
    Custom, ///< Rendering fully delegated to ColumnDef::renderer
};

// Construct a visitor from a set of lambdas:
//   std::visit(overloaded{ [](TypeA a){…}, [](TypeB b){…} }, myVariant);
template<typename... Ts>
struct overloaded : Ts...
{
    using Ts::operator()...;
};

// Typed variant replaces nullable std::function slots for click actions;
// std::visit dispatches at zero overhead.
//
//   col.onClick = CellNavigate{};
//   col.onClick = CellCustom{[](auto& v, int r){ … }};

struct CellNoAction
{}; ///< Do nothing (default)
struct CellNavigate
{}; ///< Navigate into path (FS adapter)
struct CellOpen
{}; ///< Open with OS default viewer
struct CellCustom
{
    std::function<void(const std::string& value, int rowIdx)> fn;
};

/// Discriminated union of all cell-click actions.
using CellAction = std::variant<CellNoAction, CellNavigate, CellOpen, CellCustom>;

/// Declares the runtime behaviour of a column.
/// Apply via DataBrowser::SetColumnCustomizer or load from JSON config.
///
/// C++23 fluent builder — deducing `this` lets you chain methods without CRTP:
///
///   ColumnPolicy p;
///   p.withEditable(true).withRenderer("image").withClickNavigates(true);
///
struct ColumnPolicy
{
    bool        editable  = false; ///< Inline edit on double-click
    bool        sortable  = true;
    bool        visible   = true;
    bool        resizable = true;
    std::string rendererName;              ///< "" = default text, "image", "badge", "path", …
    bool        clickNavigates    = false; ///< Single-click navigates (macOS convention)
    bool        dblClickNavigates = false; ///< Double-click navigates (Windows convention)

    // Deducing `this` (C++23) — fluent setters work on both lvalue & rvalue.
    auto& withEditable(this auto& self, bool v)
    {
        self.editable = v;
        return self;
    }
    auto& withVisible(this auto& self, bool v)
    {
        self.visible = v;
        return self;
    }
    auto& withSortable(this auto& self, bool v)
    {
        self.sortable = v;
        return self;
    }
    auto& withRenderer(this auto& self, std::string_view r)
    {
        self.rendererName = r;
        return self;
    }
    auto& withClickNavigates(this auto& self, bool v)
    {
        self.clickNavigates = v;
        return self;
    }
    auto& withDblClickNavigates(this auto& self, bool v)
    {
        self.dblClickNavigates = v;
        return self;
    }
};

/// Convenience: returns the navigate-trigger policy for the current platform.
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

/// Describes one column.  Declaration order == display order.
struct ColumnDef
{
    /// Internal key used in DataGridState::sortColumnKey and callbacks.
    std::string key;
    /// Text shown in the column header.
    std::string label;

    /// Initial width in pixels.  0 = ImGui auto / stretch.
    float initWidth = 0.0f;

    bool visible   = true;
    bool sortable  = true;
    bool resizable = true;
    bool editable  = false; ///< Allow inline editing (double-click to activate)

    /// Opaque hint propagated from ColumnInfo::displayHint by BuildColumns().
    /// The widget itself never reads this field — it exists solely so that
    /// ColumnCustomizer callbacks can branch on it without a separate lookup.
    /// Built-in values: "" | "image_path" | "image_blob"
    std::string semanticHint;

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

    /// Action fired on single-click.  Use CellNavigate / CellOpen / CellCustom.
    CellAction onClick = CellNoAction{};
    /// Action fired on double-click.
    CellAction onDblClick = CellNoAction{};
};

/// Persistent widget state — own this as a class member across frames.
///
/// The one-frame signal fields (sortChanged, selectionChanged) are
/// automatically cleared at the start of every DataGrid() call.
struct DataGridState
{
    std::string sortColumnKey;
    bool        sortAscending = true;

    /// Index into the `rows` vector of the selected row (row highlight / CRUD ops); -1 = none.
    int selectedRow = -1;

    // One-frame signals — read immediately after DataGrid() returns.
    bool sortChanged      = false;
    bool selectionChanged = false;

    int  editingRow    = -1;
    int  editingCol    = -1;
    bool editFocusDone = false;
    char editBuf[512]  = {};

    // ── Cell-level selection (Excel / Numbers style) ───────────────────────
    // O(1) rectangle: anchorRow/Col = click anchor; extentRow/Col = Shift-extension.
    // anchorCol == -1 → entire anchor row selected (Selectable-driven).
    // selectAllRows (Ctrl+A) logically includes every row in the current query.
    int  anchorRow     = -1;
    int  anchorCol     = -1;
    int  extentRow     = -1;
    int  extentCol     = -1;
    bool selectAllRows = false;

    /// O(1) test — true when cell (r, c) is inside the current selection rectangle.
    [[nodiscard]] bool IsCellSelected(const int r, int c) const noexcept
    {
        if (selectAllRows) return true;
        if (anchorRow < 0) return false;
        if (anchorCol < 0) return (r == anchorRow);   // row-only selection
        const int er = extentRow < 0 ? anchorRow : extentRow;
        const int ec = extentCol < 0 ? anchorCol : extentCol;
        const int r0 = anchorRow < er ? anchorRow : er;
        const int r1 = anchorRow > er ? anchorRow : er;
        const int c0 = anchorCol < ec ? anchorCol : ec;
        const int c1 = anchorCol > ec ? anchorCol : ec;
        return r >= r0 && r <= r1 && c >= c0 && c <= c1;
    }
};

/// Per-call configuration and callbacks.  Safe to construct as a temporary.
struct DataGridOptions
{
    /// ImGui table ID — must be unique per window if multiple grids are shown.
    const char* id = "##DataGrid";

    /// Maximum scrollable height in pixels.  0 = fill remaining space.
    float maxHeight = 0.0f;

    bool stickyHeader = true; ///< Freeze header row while scrolling
    bool rowSelection = true; ///< Highlight and track clicked rows

    /// Additional ImGuiTableFlags OR-ed in at BeginTable.
    ImGuiTableFlags extraFlags = ImGuiTableFlags_None;

    /// Minimum row height in logical pixels.
    /// Pass layout.touchTargetPx (44 px) for phone / tablet targets.
    float minRowHeight = 0.0f;

    std::function<void(int rowIndex)> onRowClick    = nullptr; ///< Single-click row
    std::function<void(int rowIndex)> onRowDblClick = nullptr; ///< Double-click row

    /// When set, rows become draggable via ImGui drag-and-drop.
    /// Called inside BeginDragDropSource — the callback MUST call
    /// ImGui::SetDragDropPayload(...) to register the payload, and MAY
    /// render a custom drag-preview tooltip (ImGui::Text / Image etc.).
    ///
    /// Example:
    ///   opts.onRowDragSource = [&](int rowIdx) {
    ///       ImGui::SetDragDropPayload("MY_TYPE", &rowIdx, sizeof(rowIdx));
    ///       ImGui::Text("Row %d", rowIdx + 1);   // drag preview
    ///   };
    std::function<void(int rowIndex)> onRowDragSource = nullptr;

    /// Right-click context menu.  Called inside BeginPopupContextItem —
    /// render MenuItem calls here; no Begin/EndPopup needed.
    std::function<void(int rowIndex)> contextMenu = nullptr;

    /// Called when the user commits an inline cell edit (Enter / focus-loss).
    /// `colIdx` is the logical index into `columns` (including hidden columns).
    /// Return true to accept the value; false to reject (data refreshes either way).
    std::function<bool(int rowIdx, int colIdx, const std::string& newValue)> onCellEdit = nullptr;

    /// Called when the cell-level selection rectangle changes.
    /// r0/c0 = top-left corner, r1/c1 = bottom-right (all page-relative indices).
    /// All -1 means the selection was cleared.
    std::function<void(int r0, int c0, int r1, int c1)> onCellSelectionChanged = nullptr;
};


/// Draw the data grid.
///
/// @param columns  Column definitions.  Visibility flags may be mutated by
///                 ImGui's built-in hide/show header context menu.
/// @param rows     Page data.  rows[r][c] must align with columns[c].
///                 Caller handles pagination — pass only the current page.
/// @param state    Persistent state object (sort + selection).
/// @param options  Per-call options and callbacks.
/// @returns true if state changed this frame (sort or selection).
[[nodiscard]] bool DataGrid(std::vector<ColumnDef>&                      columns,
                            const std::vector<std::vector<std::string>>& rows,
                            DataGridState&                               state,
                            const DataGridOptions&                       options = {});

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

#ifdef IMGUI_DATAGRID_USE_JSON

/// Serialise column visibility / widths and current sort state to JSON.
[[nodiscard]] nlohmann::json DataGridSaveLayout(const std::vector<ColumnDef>& columns, const DataGridState& state);

/// Restore column visibility / widths and sort state from JSON.
/// Unknown keys are silently ignored.
void DataGridLoadLayout(std::vector<ColumnDef>& columns, DataGridState& state, const nlohmann::json& j);

/// Write layout to a file on disk.  Returns false on I/O error.
[[nodiscard]] bool DataGridSaveLayoutToFile(const std::vector<ColumnDef>& columns,
                                            const DataGridState&          state,
                                            const std::string&            filepath);

/// Read layout from a file on disk.
/// Returns false on I/O error or JSON parse failure.
[[nodiscard]] bool
DataGridLoadLayoutFromFile(std::vector<ColumnDef>& columns, DataGridState& state, const std::string& filepath);

#endif // IMGUI_DATAGRID_USE_JSON

} // namespace ImGuiExt
