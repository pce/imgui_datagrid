#include "schema_inspector.hpp"

#include <algorithm>
#include <cstdio>
#include <format>
#include <unordered_set>

namespace Inspector {

// ============================================================
//  Open  —  fetch sample rows, run inference, cache results
// ============================================================

void SchemaInspector::Open(
    Adapters::IDataSource* source,
    const std::string&     tableName,
    int                    sampleSize)
{
    analysis_ = {};

    if (!source || !source->IsConnected() || tableName.empty()) {
        isOpen_ = true;   // open the window so the error is visible
        return;
    }

    analysis_.tableName   = tableName;
    analysis_.adapterLabel = source->AdapterLabel();

    // ── Column metadata from adapter schema ───────────────────────────────
    const auto colInfos = source->GetColumns(tableName);

    // ── Fetch sample rows ─────────────────────────────────────────────────
    Adapters::DataQuery q;
    q.table    = tableName;
    q.pageSize = sampleSize;
    q.page     = 0;

    const Adapters::QueryResult res = source->ExecuteQuery(q);
    analysis_.sampleRows = static_cast<int>(res.rows.size());

    // CountQuery uses the same (no-filter) DataQuery → total rows
    {
        Adapters::DataQuery countQ;
        countQ.table    = tableName;
        countQ.pageSize = 1;
        analysis_.totalRows = source->CountQuery(countQ);
    }

    // ── Build per-column analysis ─────────────────────────────────────────
    analysis_.columns.reserve(colInfos.size());

    for (size_t c = 0; c < colInfos.size(); ++c) {
        ColumnAnalysis col;
        col.name         = colInfos[c].name;
        col.declaredType = colInfos[c].typeName;
        col.nullable     = colInfos[c].nullable;
        col.primaryKey   = colInfos[c].primaryKey;

        // Collect all values for this column across all sample rows
        std::vector<std::string> allValues;
        allValues.reserve(res.rows.size());

        std::unordered_set<std::string> seenSamples;

        for (const auto& row : res.rows) {
            if (c < row.size()) {
                allValues.push_back(row[c]);

                // Collect up to 3 distinct non-empty sample values
                if (col.sampleCount < 3 && !row[c].empty()) {
                    if (seenSamples.insert(row[c]).second) {
                        col.samples[static_cast<size_t>(col.sampleCount)] = row[c];
                        ++col.sampleCount;
                    }
                }
            }
        }

        col.inference = TypeInfer::InferColumnType(allValues);
        analysis_.columns.push_back(std::move(col));
    }

    // ── CSV-specific analysis ─────────────────────────────────────────────
    // The adapter name tells us if we're looking at a CSV source.
    // We re-examine the first two data rows (already parsed) to run
    // header and delimiter detection so the inspector can show results.
    if (source->AdapterName() == "csv") {
        analysis_.hasCsvInfo = true;

        // Reconstruct first two raw rows from the parsed data for delimiter
        // detection.  We don't have the raw lines here, so we approximate by
        // joining cells back with the two most common candidates and checking
        // which reconstruction round-trips cleanest.  Practically, we just
        // run DetectDelimiter on the joined strings as a best-effort.
        //
        // For proper delimiter detection the CsvAdapter itself stores the
        // result in its Connect() path; here we re-derive it from samples.
        if (res.rows.size() >= 1) {
            // Build pseudo-lines by joining with candidate delimiters and
            // picking the one DetectDelimiter likes most.
            // Since we only have parsed data (post-split), we ask the adapter
            // what delimiter it used via a convention: the first column of the
            // first row from a CSV is a valid field, not a line.
            // Fall back to running DetectDelimiter on the column names joined
            // with common candidates.
            const auto& firstRow  = analysis_.columns;
            std::vector<std::string> names;
            names.reserve(firstRow.size());
            for (const auto& col : firstRow) names.push_back(col.name);

            // Build a pseudo header line with each candidate delimiter and
            // let DetectDelimiter vote.
            std::vector<std::string> pseudoLines;
            for (char sep : {',', '\t', ';', '|', ':'}) {
                std::string line;
                for (size_t i = 0; i < names.size(); ++i) {
                    if (i) line += sep;
                    line += names[i];
                }
                pseudoLines.push_back(line);
                if (!res.rows.empty()) {
                    const auto& row = res.rows.front();
                    std::string dataLine;
                    for (size_t i = 0; i < row.size(); ++i) {
                        if (i) dataLine += sep;
                        dataLine += row[i];
                    }
                    pseudoLines.push_back(dataLine);
                }
            }
            analysis_.csvDelimiter = TypeInfer::DetectDelimiter(pseudoLines);

            // Header detection: compare column names vs first data row values
            if (!res.rows.empty()) {
                std::vector<std::string> dataRow;
                dataRow.reserve(analysis_.columns.size());
                for (const auto& col : analysis_.columns) {
                    if (!res.rows.empty() && !res.rows.front().empty())
                        dataRow.push_back(res.rows.front().front());
                }
                // Use column names as "first row" and first data row as "second row"
                std::vector<std::string> firstFields, secondFields;
                for (const auto& col : analysis_.columns)
                    firstFields.push_back(col.name);
                if (!res.rows.empty()) {
                    const auto& dr = res.rows.front();
                    for (size_t i = 0; i < analysis_.columns.size() && i < dr.size(); ++i)
                        secondFields.push_back(dr[i]);
                }
                analysis_.csvHeader = TypeInfer::DetectHeader(firstFields, secondFields);
            }
        }
    }

    isOpen_ = true;
}

// ============================================================
//  ConfidenceBar
// ============================================================

void SchemaInspector::ConfidenceBar(float value, float width)
{
    // Colour: green ≥ 0.9, yellow ≥ 0.7, red < 0.7
    ImVec4 colour;
    if      (value >= 0.90f) colour = ImVec4(0.20f, 0.75f, 0.30f, 1.0f);
    else if (value >= 0.70f) colour = ImVec4(0.85f, 0.75f, 0.10f, 1.0f);
    else                     colour = ImVec4(0.85f, 0.25f, 0.20f, 1.0f);

    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, colour);
    char overlay[16];
    std::snprintf(overlay, sizeof(overlay), "%.0f%%", value * 100.0f);
    ImGui::ProgressBar(value, ImVec2(width, ImGui::GetTextLineHeight()), overlay);
    ImGui::PopStyleColor();
}

// ============================================================
//  Render
// ============================================================

void SchemaInspector::Render()
{
    if (!isOpen_) return;

    const float minW = 640.0f;
    const float minH = 300.0f;
    ImGui::SetNextWindowSizeConstraints(ImVec2(minW, minH), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::SetNextWindowSize(ImVec2(900.0f, 480.0f), ImGuiCond_FirstUseEver);

    const std::string title = std::format("Schema Inspector \xe2\x80\x94 {}##si_{}",
                                          analysis_.tableName, instanceId_);
    if (!ImGui::Begin(title.c_str(), &isOpen_, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    if (analysis_.tableName.empty()) {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "No analysis data — call Open() first.");
        ImGui::End();
        return;
    }

    RenderSummaryHeader();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    RenderColumnsTable();

    if (analysis_.hasCsvInfo) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        RenderCsvSection();
    }

    // Escape key closes the inspector when this window is focused
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        isOpen_ = false;
    }

    ImGui::End();
}

// ============================================================
//  RenderSummaryHeader
// ============================================================

void SchemaInspector::RenderSummaryHeader()
{
    ImGui::Text("Table");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", analysis_.tableName.c_str());

    ImGui::SameLine(0, 24);
    ImGui::TextDisabled("via");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", analysis_.adapterLabel.c_str());

    ImGui::SameLine(0, 24);
    ImGui::TextDisabled("%d rows total", analysis_.totalRows);
    ImGui::SameLine();
    ImGui::TextDisabled("(%d sampled)", analysis_.sampleRows);

    ImGui::SameLine(0, 24);
    ImGui::TextDisabled("%zu columns", analysis_.columns.size());
}

// ============================================================
//  RenderColumnsTable
// ============================================================

void SchemaInspector::RenderColumnsTable()
{
    constexpr ImGuiTableFlags flags =
        ImGuiTableFlags_Borders    |
        ImGuiTableFlags_RowBg      |
        ImGuiTableFlags_ScrollY    |
        ImGuiTableFlags_Resizable  |
        ImGuiTableFlags_SizingStretchProp;

    // Reserve enough height for the CSV section below (if present)
    const float reserveH = analysis_.hasCsvInfo
        ? ImGui::GetTextLineHeightWithSpacing() * 7.0f
        : 0.0f;
    const float tableH = ImGui::GetContentRegionAvail().y - reserveH - 32.0f;

    if (!ImGui::BeginTable("##si_cols", 8, flags, ImVec2(0, std::max(tableH, 80.0f))))
        return;

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Column",       ImGuiTableColumnFlags_WidthStretch, 1.4f);
    ImGui::TableSetupColumn("Declared",     ImGuiTableColumnFlags_WidthStretch, 0.8f);
    ImGui::TableSetupColumn("Inferred",     ImGuiTableColumnFlags_WidthStretch, 0.8f);
    ImGui::TableSetupColumn("Confidence",   ImGuiTableColumnFlags_WidthFixed,   72.0f);
    ImGui::TableSetupColumn("Null %",       ImGuiTableColumnFlags_WidthFixed,   52.0f);
    ImGui::TableSetupColumn("PK",           ImGuiTableColumnFlags_WidthFixed,   28.0f);
    ImGui::TableSetupColumn("Nullable",     ImGuiTableColumnFlags_WidthFixed,   56.0f);
    ImGui::TableSetupColumn("Samples",      ImGuiTableColumnFlags_WidthStretch, 2.0f);
    ImGui::TableHeadersRow();

    for (int i = 0; i < static_cast<int>(analysis_.columns.size()); ++i)
        RenderColumnRow(analysis_.columns[static_cast<size_t>(i)], i);

    ImGui::EndTable();
}

// ============================================================
//  RenderColumnRow
// ============================================================

void SchemaInspector::RenderColumnRow(const ColumnAnalysis& col, int rowIdx)
{
    ImGui::TableNextRow();
    ImGui::PushID(rowIdx);

    // ── Column name ────────────────────────────────────────────────────────
    ImGui::TableSetColumnIndex(0);
    if (col.primaryKey)
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "%s", col.name.c_str());
    else
        ImGui::TextUnformatted(col.name.c_str());

    // ── Declared type ──────────────────────────────────────────────────────
    ImGui::TableSetColumnIndex(1);
    if (col.declaredType.empty())
        ImGui::TextDisabled("—");
    else
        ImGui::TextDisabled("%s", col.declaredType.c_str());

    // ── Inferred type ──────────────────────────────────────────────────────
    ImGui::TableSetColumnIndex(2);
    {
        const char* tname = TypeInfer::TypeName(col.inference.type);
        // Colour text by inferred type for quick scanning
        ImVec4 col_colour;
        switch (col.inference.type) {
            case TypeInfer::InferredType::Boolean:  col_colour = ImVec4(0.9f, 0.5f, 0.9f, 1.0f); break;
            case TypeInfer::InferredType::Integer:  col_colour = ImVec4(0.4f, 0.85f, 1.0f, 1.0f); break;
            case TypeInfer::InferredType::Real:     col_colour = ImVec4(0.3f, 0.9f, 0.7f, 1.0f); break;
            case TypeInfer::InferredType::DateTime: col_colour = ImVec4(1.0f, 0.75f, 0.3f, 1.0f); break;
            case TypeInfer::InferredType::Date:     col_colour = ImVec4(1.0f, 0.65f, 0.2f, 1.0f); break;
            default:                                col_colour = ImGui::GetStyleColorVec4(ImGuiCol_Text); break;
        }
        ImGui::TextColored(col_colour, "%s", tname);
    }

    // ── Confidence bar ─────────────────────────────────────────────────────
    ImGui::TableSetColumnIndex(3);
    ConfidenceBar(col.inference.confidence, ImGui::GetContentRegionAvail().x);

    // ── Null % ────────────────────────────────────────────────────────────
    ImGui::TableSetColumnIndex(4);
    if (col.inference.total > 0) {
        const float nullPct =
            static_cast<float>(col.inference.nullCount) /
            static_cast<float>(col.inference.total) * 100.0f;
        if (col.inference.nullCount > 0)
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%.0f%%", nullPct);
        else
            ImGui::TextDisabled("0%%");
    } else {
        ImGui::TextDisabled("—");
    }

    // ── PK ────────────────────────────────────────────────────────────────
    ImGui::TableSetColumnIndex(5);
    if (col.primaryKey)
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "PK");
    else
        ImGui::TextDisabled("—");

    // ── Nullable ──────────────────────────────────────────────────────────
    ImGui::TableSetColumnIndex(6);
    ImGui::TextUnformatted(col.nullable ? "yes" : "no");

    // ── Sample values ─────────────────────────────────────────────────────
    ImGui::TableSetColumnIndex(7);
    if (col.sampleCount == 0) {
        ImGui::TextDisabled("(empty)");
    } else {
        // Show first two samples inline; rest in tooltip
        for (int s = 0; s < std::min(col.sampleCount, 2); ++s) {
            if (s > 0) {
                ImGui::SameLine();
                ImGui::TextDisabled("·");
                ImGui::SameLine();
            }
            ImGui::TextUnformatted(col.samples[static_cast<size_t>(s)].c_str());
        }
        if (col.sampleCount > 0 && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ImGui::BeginTooltip();
            ImGui::TextDisabled("Sample values for  %s:", col.name.c_str());
            ImGui::Separator();
            for (int s = 0; s < col.sampleCount; ++s) {
                ImGui::BulletText("%s", col.samples[static_cast<size_t>(s)].c_str());
            }
            if (col.inference.total > 0) {
                ImGui::Spacing();
                ImGui::TextDisabled("min: %s", col.inference.minValue.c_str());
                ImGui::TextDisabled("max: %s", col.inference.maxValue.c_str());
            }
            ImGui::EndTooltip();
        }
    }

    ImGui::PopID();
}

// ============================================================
//  RenderCsvSection
// ============================================================

void SchemaInspector::RenderCsvSection()
{
    ImGui::Text("CSV Analysis");
    ImGui::Separator();

    // Delimiter
    ImGui::Text("Detected delimiter:");
    ImGui::SameLine();
    const char delim = analysis_.csvDelimiter.delimiter;
    const char* delimName =
        (delim == ',')  ? "comma (,)"     :
        (delim == '\t') ? "tab (\\t)"     :
        (delim == ';')  ? "semicolon (;)" :
        (delim == '|')  ? "pipe (|)"      :
        (delim == ':')  ? "colon (:)"     : "unknown";
    ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.6f, 1.0f), "%s", delimName);
    ImGui::SameLine(0, 12);
    ImGui::TextDisabled("confidence:");
    ImGui::SameLine();
    ConfidenceBar(analysis_.csvDelimiter.confidence, 60.0f);

    ImGui::Spacing();

    // Header detection
    ImGui::Text("Header row detected:");
    ImGui::SameLine();
    if (analysis_.csvHeader.hasHeader)
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.4f, 1.0f), "yes");
    else
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "no");
    ImGui::SameLine(0, 12);
    ImGui::TextDisabled("confidence:");
    ImGui::SameLine();
    ConfidenceBar(analysis_.csvHeader.confidence, 60.0f);

    if (!analysis_.csvHeader.reason.empty()) {
        ImGui::SameLine(0, 12);
        ImGui::TextDisabled("(%s)", analysis_.csvHeader.reason.c_str());
    }
}

} // namespace Inspector
