#include "csv_adapter.hpp"
#include "../adapter_registry.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>
#include <string>



namespace datagrid::adapters {

CsvAdapter::CsvAdapter() = default;


std::vector<std::string> CsvAdapter::
    ParseRow(const std::string& line, char sep)
{
    std::vector<std::string> fields;
    std::string              field;
    bool                     inQuotes = false;

    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];

        if (inQuotes) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    field += '"';
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                field += c;
            }
        } else {
            if (c == '"') {
                inQuotes = true;
            } else if (c == sep) {
                fields.push_back(std::move(field));
                field.clear();
            } else if (c == '\r') {
            } else {
                field += c;
            }
        }
    }

    fields.push_back(std::move(field));
    return fields;
}

std::string CsvAdapter::StemOf(const std::string& path)
{
    const size_t slashPos  = path.find_last_of("/\\");
    const size_t nameStart = (slashPos == std::string::npos) ? 0 : slashPos + 1;

    const size_t dotPos  = path.find_last_of('.');
    const size_t nameEnd = (dotPos != std::string::npos && dotPos > nameStart) ? dotPos : path.size();

    return path.substr(nameStart, nameEnd - nameStart);
}

std::expected<void, Error> CsvAdapter::Connect(const ConnectionParams& params)
{
    Disconnect();
    lastError_.clear();
    filePath_  = params.connectionString;
    tableName_ = StemOf(filePath_);
    separator_ = params.csvSeparator.value_or(',');

    std::ifstream file(filePath_);
    if (!file.is_open()) {
        lastError_ = "Cannot open file: " + filePath_;
        return std::unexpected(lastError_);
    }

    std::string line;
    bool        firstLine = true;

    while (std::getline(file, line)) {
        if (line.empty() || line == "\r")
            continue;

        auto fields = ParseRow(line, separator_);

        if (firstLine) {
            header_.clear();
            header_.reserve(fields.size());
            for (const auto& name : fields) {
                ColumnInfo ci;
                ci.name     = name;
                ci.typeName = "TEXT";
                ci.nullable = true;
                header_.push_back(std::move(ci));
            }
            firstLine = false;
        } else {
            fields.resize(header_.size(), "");
            allRows_.push_back(std::move(fields));
        }
    }

    if (firstLine) {
        lastError_ = "File is empty or has no header row: " + filePath_;
        return std::unexpected(lastError_);
    }

    connected_ = true;
    readOnly_  = params.readOnly.value_or(false);
    return {};
}

void CsvAdapter::Disconnect()
{
    connected_ = false;
    readOnly_  = false;
    header_.clear();
    allRows_.clear();
    filePath_.clear();
    tableName_.clear();
}

bool CsvAdapter::IsConnected() const
{
    return connected_;
}

std::string CsvAdapter::LastError() const
{
    return lastError_;
}

std::string CsvAdapter::AdapterLabel() const
{
    if (!connected_)
        return "CSV (disconnected)";
    return "CSV  " + filePath_ + "  (" + std::to_string(allRows_.size()) + " rows)" +
           (readOnly_ ? "  [read-only]" : "");
}

std::vector<std::string> CsvAdapter::GetCatalogs() const
{
    if (!connected_)
        return {};
    return {filePath_};
}

std::vector<TableInfo> CsvAdapter::GetTables(const std::string& /*catalog*/) const
{
    if (!connected_)
        return {};

    TableInfo t;
    t.name    = tableName_;
    t.kind    = "csv";
    t.catalog = filePath_;
    return {t};
}

std::vector<ColumnInfo> CsvAdapter::GetColumns(const std::string& table) const
{
    if (!connected_ || table != tableName_)
        return {};
    return header_;
}

std::vector<std::vector<std::string>> CsvAdapter::ApplyFilters(const DataQuery& q) const
{
    std::unordered_map<std::string, size_t> colIndex;
    for (size_t i = 0; i < header_.size(); ++i)
        colIndex[header_[i].name] = i;

    std::vector<std::vector<std::string>> result;
    result.reserve(allRows_.size());

    for (const auto& row : allRows_) {
        bool pass = true;

        for (const auto& [col, val] : q.whereExact) {
            auto it = colIndex.find(col);
            if (it == colIndex.end()) {
                pass = false;
                break;
            }
            const std::string& cell = (it->second < row.size()) ? row[it->second] : "";
            if (cell != val) {
                pass = false;
                break;
            }
        }

        if (!pass)
            continue;

        if (!q.searchColumn.empty() && !q.searchValue.empty()) {
            auto it = colIndex.find(q.searchColumn);
            if (it == colIndex.end()) {
                pass = false;
            } else {
                const std::string& cell       = (it->second < row.size()) ? row[it->second] : "";
                std::string        cellLower  = cell;
                std::string        valueLower = q.searchValue;
                auto               toLower    = [](std::string& s) {
                    for (char& c : s)
                        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                };
                toLower(cellLower);
                toLower(valueLower);
                if (cellLower.find(valueLower) == std::string::npos)
                    pass = false;
            }
        }

        if (pass)
            result.push_back(row);
    }

    return result;
}

QueryResult CsvAdapter::ExecuteQuery(const DataQuery& q) const
{
    QueryResult result;

    if (!connected_) {
        result.error = "Not connected.";
        return result;
    }

    const auto t0 = std::chrono::steady_clock::now();

    auto filtered = ApplyFilters(q);

    if (!q.sortColumn.empty()) {
        size_t sortIdx = std::string::npos;
        for (size_t i = 0; i < header_.size(); ++i) {
            if (header_[i].name == q.sortColumn) {
                sortIdx = i;
                break;
            }
        }

        if (sortIdx != std::string::npos) {
            std::stable_sort(filtered.begin(),
                             filtered.end(),
                             [&](const std::vector<std::string>& a, const std::vector<std::string>& b) {
                                 const std::string& va = (sortIdx < a.size()) ? a[sortIdx] : "";
                                 const std::string& vb = (sortIdx < b.size()) ? b[sortIdx] : "";

                                 try {
                                     const double da = std::stod(va);
                                     const double db = std::stod(vb);
                                     return q.sortAscending ? (da < db) : (da > db);
                                 } catch (...) {
                                 }

                                 return q.sortAscending ? (va < vb) : (va > vb);
                             });
        }
    }

    const int total  = static_cast<int>(filtered.size());
    const int offset = q.page * q.pageSize;
    const int end    = std::min(offset + q.pageSize, total);

    result.columns = header_;

    if (offset < total) {
        result.rows.reserve(static_cast<size_t>(end - offset));
        for (int i = offset; i < end; ++i)
            result.rows.push_back(filtered[static_cast<size_t>(i)]);
    }

    const auto t1      = std::chrono::steady_clock::now();
    result.executionMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

    return result;
}

int CsvAdapter::CountQuery(const DataQuery& q) const
{
    if (!connected_)
        return 0;
    return static_cast<int>(ApplyFilters(q).size());
}

QueryResult CsvAdapter::Execute(const std::string& sql) const
{
    QueryResult result;
    if (!connected_) {
        result.error = "Not connected.";
        return result;
    }
    if (allRows_.empty()) {
        result.columns = header_;
        return result;
    }

    auto planRes = TabularQuery::parse(sql);
    if (!planRes) {
        result.error = "SQL parse error: " + planRes.error();
        return result;
    }
    const QueryPlan& plan = *planRes;

    if (!plan.fromSource.empty() && plan.fromSource != tableName_ && plan.fromSource != "*") {
        result.error = "Unknown table '" + plan.fromSource + "'. Available: '" + tableName_ + "'.";
        return result;
    }

    return TabularQuery::execute(plan, BuildSoA(), header_);
}

TabularSoA CsvAdapter::BuildSoA() const
{
    // CSV columns are all Text; TabularQuery applies dynamic numeric coercion
    // for < > <= >= operators (SQLite affinity behaviour).
    std::vector<std::string>    names;
    std::vector<ColType> types;
    names.reserve(header_.size());
    types.reserve(header_.size());
    for (const auto& ci : header_) {
        names.push_back(ci.name);
        types.push_back(ColType::Text);
    }
    return TabularSoA::from_rows(names, types, allRows_);
}

} // namespace datagrid::adapters
