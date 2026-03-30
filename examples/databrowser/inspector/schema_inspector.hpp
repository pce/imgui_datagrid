#pragma once
/// SchemaInspector — per-table schema analysis panel for DataBrowser.
/// See docs/databrowser.md for usage context.
///
/// For every column in the selected table it shows:
///   • Declared type (ColumnInfo::typeName from the adapter)
///   • Inferred type (TypeInfer::InferColumnType on sample rows)
///   • Confidence bar (green ≥ 0.9, yellow ≥ 0.7, red < 0.7)
///   • Nullable / PK flags, up to 3 sample values, null %
///
/// For CSV sources it additionally shows the detected delimiter
/// and header-detection confidence.
///
/// USAGE
///   1. Construct once as a member of DataBrowser.
///   2. Call Open() when the user requests inspection.
///      Open() fetches data synchronously — keep sampleSize ≤ 200.
///   3. Call Render() every frame inside your ImGui render loop.
///      Manages its own ImGui::Begin / End.
///
/// Thread safety: NOT provided — call from the render thread only.
/// The IDataSource pointer is NOT retained after Open() returns.

#include "imgui.h"
#include "adapters/data_source.hpp"
#include "adapters/utils/type_inferrer.hpp"

#include <array>
#include <string>
#include <vector>

namespace Inspector {

// ── ColumnAnalysis ─────────────────────────────────────────────────────────
/// Per-column cached analysis result.
struct ColumnAnalysis {
    // From adapter schema
    std::string name;
    std::string declaredType;  ///< Raw typeName from ColumnInfo (may be empty)
    bool        nullable   = true;
    bool        primaryKey = false;

    // From TypeInferrer
    TypeInfer::TypeResult inference;

    // First 3 distinct non-empty sample values
    std::array<std::string, 3> samples     = {};
    int                        sampleCount = 0;  ///< How many of the 3 slots are filled
};

// ── TableAnalysis ──────────────────────────────────────────────────────────
/// Top-level cached analysis for one table.
struct TableAnalysis {
    std::string                 tableName;
    std::string                 adapterLabel;
    int                         totalRows  = 0;
    int                         sampleRows = 0;  ///< Rows actually fetched
    std::vector<ColumnAnalysis> columns;

    // CSV-specific fields (populated when adapterName == "csv")
    bool                       hasCsvInfo          = false;
    TypeInfer::DelimiterResult csvDelimiter         = {};
    TypeInfer::HeaderResult    csvHeader            = {};
    char                       configuredDelimiter  = ',';
};

// ── SchemaInspector ────────────────────────────────────────────────────────
class SchemaInspector {
public:
    SchemaInspector() = default;

    // Non-copyable / non-movable — owns its own analysis state.
    SchemaInspector(const SchemaInspector&)            = delete;
    SchemaInspector& operator=(const SchemaInspector&) = delete;
    SchemaInspector(SchemaInspector&&)                 = delete;
    SchemaInspector& operator=(SchemaInspector&&)      = delete;

    // ── API ───────────────────────────────────────────────────────────────

    /// Fetch up to `sampleSize` rows from `tableName` and run type inference.
    /// Replaces any previously cached result.  `source` is not retained.
    void Open(
        Adapters::IDataSource* source,
        const std::string&     tableName,
        int                    sampleSize = 100
    );

    /// Render the inspector window.  Call every frame.
    /// No-op if the inspector has never been opened or has been closed.
    void Render();

    // ── State ─────────────────────────────────────────────────────────────
    [[nodiscard]] bool                IsOpen()       const { return isOpen_;                        }
    [[nodiscard]] bool                HasData()      const { return !analysis_.tableName.empty();   }
    [[nodiscard]] const TableAnalysis& GetAnalysis() const { return analysis_;                      }
    void Close() { isOpen_ = false; }

    /// Set by the owning DataBrowser so the window has a unique ImGui ID
    /// when two browser windows are open simultaneously.
    void SetInstanceId(int id) { instanceId_ = id; }

private:
    // ── Rendering helpers ─────────────────────────────────────────────────
    void RenderSummaryHeader();
    void RenderColumnsTable();
    void RenderColumnRow(const ColumnAnalysis& col, int rowIdx);
    void RenderCsvSection();

    /// Small progress bar coloured green / yellow / red by `value`.
    static void ConfidenceBar(float value, float width = 60.0f);

    // ── State ─────────────────────────────────────────────────────────────
    TableAnalysis analysis_;
    bool          isOpen_ = false;
    int           instanceId_ = 0;  ///< Used to disambiguate window IDs
};

} // namespace Inspector
