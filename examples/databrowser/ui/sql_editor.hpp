#pragma once
#include "adapters/data_source.hpp"
#include <string>
#include <vector>

namespace UI {

/// State for the embedded SQL editor widget — one instance per DataBrowser.
/// Holds only the widget's own persistent data; query results are returned
/// via the Render() return value so the caller can integrate them freely.
struct SqlEditorState
{
    bool        visible = false;  ///< Toggled by the toolbar "SQL" button
    char        buf[2048] = {};   ///< Text entered in the editor
    std::string error;            ///< Last execution error, if any
    std::string status;           ///< e.g. "42 rows  (3.1 ms)"

    // Populated when the last Execute() succeeded:
    std::vector<Adapters::ColumnInfo>     resultColumns;
    std::vector<std::vector<std::string>> resultRows;
    float                                 resultMs = 0.0f;
};

/// Render the SQL editor strip inside the current ImGui window.
/// Must be called every frame while state.visible is true (or unconditionally —
/// returns immediately when not visible).
///
/// Returns true if a new query was successfully executed; on true the caller
/// should read state.resultColumns / resultRows / resultMs and update its view.
bool DrawSqlEditor(SqlEditorState& state, Adapters::IDataSource* source);

} // namespace UI

