#pragma once
#include "adapters/data_source.hpp"
#include "adapters/utils/type_inferrer.hpp"
#include "imgui.h"

#include <array>
#include <format>
#include <string>
#include <vector>

namespace datagrid::inspector {

/// Per-column cached analysis result.
struct ColumnAnalysis
{
    // From adapter schema
    std::string name;
    std::string declaredType; ///< Raw typeName from ColumnInfo (may be empty)
    bool        nullable   = true;
    bool        primaryKey = false;

    // From type_inferrer
    adapters::utils::TypeResult inference;

    // First 3 distinct non-empty sample values
    std::array<std::string, 3> samples     = {};
    int                        sampleCount = 0; ///< How many of the 3 slots are filled
};

/// Top-level cached analysis for one table.
struct TableAnalysis
{
    std::string                 tableName;
    std::string                 adapterLabel;
    int                         totalRows  = 0;
    int                         sampleRows = 0; ///< Rows actually fetched
    std::vector<ColumnAnalysis> columns;

    // CSV-specific fields (populated when adapterName == "csv")
    bool                       hasCsvInfo          = false;
    adapters::utils::DelimiterResult csvDelimiter        = {};
    adapters::utils::HeaderResult    csvHeader           = {};
    char                       configuredDelimiter = ',';
};

class SchemaInspector
{
  public:
    SchemaInspector() = default;

    // Non-copyable / non-movable — owns its own analysis state.
    SchemaInspector(const SchemaInspector&)            = delete;
    SchemaInspector& operator=(const SchemaInspector&) = delete;
    SchemaInspector(SchemaInspector&&)                 = delete;
    SchemaInspector& operator=(SchemaInspector&&)      = delete;

    /// Fetch up to `sampleSize` rows from `tableName` and run type inference.
    /// Replaces any previously cached result.  `source` is not retained.
    void Open(adapters::IDataSource* source, const std::string& tableName, int sampleSize = 100);

    /// Render the inspector window.  Call every frame.
    /// No-op if the inspector has never been opened or has been closed.
    void Render();

    [[nodiscard]] bool                 IsOpen() const { return isOpen_; }
    [[nodiscard]] bool                 HasData() const { return !analysis_.tableName.empty(); }
    [[nodiscard]] const TableAnalysis& GetAnalysis() const { return analysis_; }
    void                               Close() { isOpen_ = false; }

    /// Set by the owning DataBrowser so the window has a unique ImGui ID
    /// when two browser windows are open simultaneously.
    void SetInstanceId(int id) { instanceId_ = id; }

    /// Returns the exact ImGui window ID string used in Render() —
    /// useful for SetWindowFocus() calls from outside the inspector.
    /// Returns an empty string if no table has been analysed yet.
    [[nodiscard]] std::string WindowId() const
    {
        if (analysis_.tableName.empty())
            return {};
        return std::format("Schema Inspector \xe2\x80\x94 {}##si_{}", analysis_.tableName, instanceId_);
    }

  private:
    void RenderSummaryHeader();
    void RenderColumnsTable();
    void RenderColumnRow(const ColumnAnalysis& col, int rowIdx);
    void RenderCsvSection();

    /// Small progress bar coloured green / yellow / red by `value`.
    static void ConfidenceBar(float value, float width = 60.0f);

    TableAnalysis analysis_;
    bool          isOpen_     = false;
    int           instanceId_ = 0; ///< Used to disambiguate window IDs
};

} // namespace datagrid::inspector
