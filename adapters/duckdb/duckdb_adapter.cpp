#include "duckdb_adapter.hpp"
#include "../adapter_registry.hpp"
#include "../utils/expected_utils.hpp"
#include <algorithm>
#include <any>
#include <cctype>
#include <chrono>
#include <duckdb.hpp>
#include <filesystem>
#include <functional>
#include <ranges>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

namespace {
const datagrid::adapters::RegisterAdapter<datagrid::adapters::DuckDBAdapter> kDuckDBReg{"duckdb"};
}

namespace datagrid::adapters {

// Constrains ToQueryResult() to any DuckDB result type that exposes the
// minimal surface we need (MaterializedQueryResult satisfies this).
template<typename R>
concept DuckDBResultLike = requires(R& r, duckdb::idx_t i) {
    { r.HasError() } -> std::convertible_to<bool>;
    { r.ColumnCount() } -> std::convertible_to<duckdb::idx_t>;
    { r.RowCount() } -> std::convertible_to<duckdb::idx_t>;
    { r.names[i] } -> std::convertible_to<std::string>;
    { r.types[i].ToString() } -> std::convertible_to<std::string>;
    { r.GetValue(i, i) };
};

/// Double-quote a SQL identifier, escaping embedded double-quotes.
[[nodiscard]] static std::string QuoteId(const std::string& name)
{
    std::string out;
    out.reserve(name.size() + 2);
    out += '"';
    for (char c : name) {
        if (c == '"')
            out += '"'; // double to escape
        out += c;
    }
    out += '"';
    return out;
}

/// Single-quote a SQL string literal, escaping embedded single-quotes.
/// Defined for completeness / direct-embed scenarios; parameterised queries
/// are preferred and used everywhere internally.
[[maybe_unused]] [[nodiscard]] static std::string EscapeStr(const std::string& val)
{
    std::string out;
    out.reserve(val.size() + 2);
    out += '\'';
    for (char c : val) {
        if (c == '\'')
            out += '\''; // double to escape
        out += c;
    }
    out += '\'';
    return out;
}

/// Apply display-hint heuristics to a ColumnInfo in place.
///   typeName contains "BLOB"                                → "image_blob"
///   column name contains image/photo/avatar/thumb/icon/… → "image_path"
void SetDisplayHint(ColumnInfo& ci)
{
    std::string typeUp = ci.typeName;
    for (auto& ch : typeUp)
        ch = static_cast<char>(toupper(static_cast<unsigned char>(ch)));

    if (typeUp.find("BLOB") != std::string::npos) {
        ci.displayHint = "image_blob";
        return;
    }

    std::string nameLo = ci.name;
    for (auto& ch : nameLo)
        ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));

    auto nameHas = [&](const char* sub) { return nameLo.find(sub) != std::string::npos; };

    if (nameHas("image") || nameHas("photo") || nameHas("avatar") || nameHas("thumb") || nameHas("thumbnail") ||
        nameHas("icon")) {
        ci.displayHint = "image_path";
    }
}

/// Convert any DuckDB result type (concept-constrained) to our QueryResult.
/// Does NOT set executionMs — the caller is responsible for timing.
template<DuckDBResultLike R>
[[nodiscard]] static QueryResult ToQueryResult(R& r)
{
    QueryResult out;
    if (r.HasError()) {
        out.error = r.GetError();
        return out;
    }

    const auto colCount = r.ColumnCount();
    out.columns.reserve(colCount);
    for (duckdb::idx_t c = 0; c < colCount; ++c) {
        ColumnInfo ci;
        ci.name     = r.names[c];
        ci.typeName = r.types[c].ToString();
        SetDisplayHint(ci);
        out.columns.push_back(std::move(ci));
    }

    const auto rowCount = r.RowCount();
    out.rows.reserve(rowCount);
    for (duckdb::idx_t row = 0; row < rowCount; ++row) {
        std::vector<std::string> rowVec;
        rowVec.reserve(colCount);
        for (duckdb::idx_t c = 0; c < colCount; ++c) {
            const duckdb::Value val = r.GetValue(c, row);
            rowVec.emplace_back(val.IsNull() ? "" : val.ToString());
        }
        out.rows.push_back(std::move(rowVec));
    }

    return out;
}

/// WHERE-clause builder used by ExecuteQuery() and CountQuery().
/// Returns the SQL fragment (" WHERE col=$1 AND …") and the corresponding
/// string bind values in positional order.
struct WhereResult
{
    std::string              fragment; // empty string when there are no predicates
    std::vector<std::string> binds;
};

[[nodiscard]] static WhereResult BuildWhereClause(const DataQuery& q)
{
    WhereResult              wr;
    std::vector<std::string> clauses;
    int                      paramIdx = 1;

    for (const auto& [col, val] : q.whereExact) {
        clauses.push_back(QuoteId(col) + " = $" + std::to_string(paramIdx++));
        wr.binds.push_back(val);
    }

    if (!q.searchColumn.empty() && !q.searchValue.empty()) {
        clauses.push_back(QuoteId(q.searchColumn) + " LIKE $" + std::to_string(paramIdx++));
        wr.binds.push_back("%" + q.searchValue + "%");
    }

    if (!clauses.empty()) {
        wr.fragment = " WHERE ";
        for (size_t i = 0; i < clauses.size(); ++i) {
            if (i > 0)
                wr.fragment += " AND ";
            wr.fragment += clauses[i];
        }
    }

    return wr;
}

struct DuckDBAdapter::Impl
{
    std::unique_ptr<duckdb::DuckDB>     db;
    std::unique_ptr<duckdb::Connection> conn;
    std::string                         path;
    mutable std::string                 lastError;
    bool                                readOnly = false;
    std::set<std::string>               fileSources;
};

DuckDBAdapter::DuckDBAdapter() : pImpl_(std::make_unique<Impl>()) {}

DuckDBAdapter::~DuckDBAdapter()
{
    Disconnect();
}

// move operations defined here so that unique_ptr<Impl> has a complete type
DuckDBAdapter::DuckDBAdapter(DuckDBAdapter&&) noexcept            = default;
DuckDBAdapter& DuckDBAdapter::operator=(DuckDBAdapter&&) noexcept = default;

std::expected<void, Error> DuckDBAdapter::Connect(const ConnectionParams& params)
{
    Disconnect();
    pImpl_->lastError = {};

    // Empty connection string → in-memory database.
    const std::string pathStr = params.connectionString.empty() ? ":memory:" : params.connectionString;
    pImpl_->path              = pathStr;
    pImpl_->readOnly          = params.readOnly.value_or(false);
    readOnly_                 = pImpl_->readOnly; // sync base-class guard

    try {
        duckdb::DBConfig config;
        if (pImpl_->readOnly)
            config.options.access_mode = duckdb::AccessMode::READ_ONLY;

        // DuckDB treats nullptr (or ":memory:") as an in-memory database.
        const char* dbPath = (pathStr == ":memory:") ? nullptr : pathStr.c_str();
        pImpl_->db         = std::make_unique<duckdb::DuckDB>(dbPath, &config);
        pImpl_->conn       = std::make_unique<duckdb::Connection>(*pImpl_->db);

        return {};
    } catch (const std::exception& e) {
        pImpl_->lastError = e.what();
        pImpl_->conn.reset();
        pImpl_->db.reset();
        return std::unexpected(Error{e.what()});
    }
}

void DuckDBAdapter::Disconnect()
{
    pImpl_->conn.reset();
    pImpl_->db.reset();
}

bool DuckDBAdapter::IsConnected() const
{
    return pImpl_->db != nullptr && pImpl_->conn != nullptr;
}

std::string DuckDBAdapter::LastError() const
{
    return pImpl_->lastError;
}

std::string DuckDBAdapter::AdapterLabel() const
{
    if (!IsConnected())
        return "DuckDB (disconnected)";
    auto r = pImpl_->conn->Query("SELECT library_version FROM pragma_version()");
    if (!r->HasError() && r->RowCount() > 0)
        return "DuckDB " + r->GetValue(0, 0).ToString();
    return "DuckDB";
}

std::vector<std::string> DuckDBAdapter::GetCatalogs() const
{
    if (!IsConnected())
        return {};

    auto result = pImpl_->conn->Query("SHOW DATABASES");
    if (!result->HasError() && result->RowCount() > 0) {
        std::vector<std::string> catalogs;
        catalogs.reserve(result->RowCount());
        for (duckdb::idx_t row = 0; row < result->RowCount(); ++row)
            catalogs.emplace_back(result->GetValue(0, row).ToString());
        return catalogs;
    }

    // Fall back to the connection path (e.g. ":memory:" or the file path).
    return {pImpl_->path};
}

std::vector<TableInfo> DuckDBAdapter::GetTables(const std::string& catalog) const
{
    if (!IsConnected())
        return {};

    const std::string baseQuery = "SELECT table_type, table_name "
                                  "FROM information_schema.tables "
                                  "WHERE table_schema = 'main' "
                                  "  AND table_type IN ('BASE TABLE', 'VIEW') "
                                  "ORDER BY table_type, table_name";

    const std::string catalogQuery = "SELECT table_type, table_name "
                                     "FROM information_schema.tables "
                                     "WHERE table_catalog = $1 AND table_schema = 'main' "
                                     "  AND table_type IN ('BASE TABLE', 'VIEW') "
                                     "ORDER BY table_type, table_name";

    std::unique_ptr<duckdb::MaterializedQueryResult> result;

    if (catalog.empty()) {
        result = pImpl_->conn->Query(baseQuery);
    } else {
        auto stmt = pImpl_->conn->Prepare(catalogQuery);
        if (stmt->HasError()) {
            pImpl_->lastError = stmt->GetError();
            return {};
        }
        duckdb::vector<duckdb::Value> dvals  = {duckdb::Value(catalog)};
        auto                          execQR = stmt->Execute(dvals, /*allow_stream_result=*/false);
        if (execQR->HasError()) {
            pImpl_->lastError = execQR->GetError();
            return {};
        }
        result.reset(static_cast<duckdb::MaterializedQueryResult*>(execQR.release()));
    }

    if (result->HasError()) {
        pImpl_->lastError = result->GetError();
        return {};
    }

    std::vector<TableInfo> tables;
    tables.reserve(result->RowCount());
    for (duckdb::idx_t row = 0; row < result->RowCount(); ++row) {
        TableInfo         t;
        const std::string tableType = result->GetValue(0, row).ToString();
        t.kind                      = (tableType == "BASE TABLE") ? "table" : "view";
        t.name                      = result->GetValue(1, row).ToString();
        t.catalog                   = catalog.empty() ? pImpl_->path : catalog;
        t.schema                    = "main";
        tables.push_back(std::move(t));
    }
    return tables;
}

std::vector<ColumnInfo> DuckDBAdapter::GetColumns(const std::string& table) const
{
    if (!IsConnected())
        return {};

    // DESCRIBE "table" columns: column_name(0), column_type(1), null(2), key(3), default(4), extra(5)
    const std::string sql    = "DESCRIBE " + QuoteId(table);
    auto              result = pImpl_->conn->Query(sql);

    if (result->HasError()) {
        pImpl_->lastError = result->GetError();
        return {};
    }

    std::vector<ColumnInfo> cols;
    cols.reserve(result->RowCount());
    for (duckdb::idx_t row = 0; row < result->RowCount(); ++row) {
        ColumnInfo ci;
        ci.name       = result->GetValue(0, row).ToString();            // column_name
        ci.typeName   = result->GetValue(1, row).ToString();            // column_type
        ci.nullable   = (result->GetValue(2, row).ToString() == "YES"); // null col
        ci.primaryKey = (result->GetValue(3, row).ToString() == "PRI"); // key col
        SetDisplayHint(ci);
        cols.push_back(std::move(ci));
    }
    return cols;
}

QueryResult DuckDBAdapter::ExecuteQuery(const DataQuery& q) const
{
    QueryResult result;
    if (!IsConnected()) {
        result.error = "Not connected.";
        return result;
    }

    try {
        const auto t0 = std::chrono::steady_clock::now();

        auto [whereFragment, binds] = BuildWhereClause(q);

        std::string sql = "SELECT * FROM " + QuoteId(q.table) + whereFragment;

        if (!q.sortColumn.empty())
            sql += " ORDER BY " + QuoteId(q.sortColumn) + (q.sortAscending ? " ASC" : " DESC");

        sql += " LIMIT " + std::to_string(q.pageSize);
        sql += " OFFSET " + std::to_string(q.page * q.pageSize);

        std::unique_ptr<duckdb::MaterializedQueryResult> res;
        if (binds.empty()) {
            res = pImpl_->conn->Query(sql);
        } else {
            auto stmt = pImpl_->conn->Prepare(sql);
            if (stmt->HasError()) {
                result.error      = stmt->GetError();
                pImpl_->lastError = result.error;
                return result;
            }
            auto view = binds | std::views::transform([](const std::string& s) { return duckdb::Value(s); });
            duckdb::vector<duckdb::Value> dvals(view.begin(), view.end());
            auto                          execQR = stmt->Execute(dvals, /*allow_stream_result=*/false);
            if (execQR->HasError()) {
                result.error      = execQR->GetError();
                pImpl_->lastError = result.error;
                return result;
            }
            res.reset(static_cast<duckdb::MaterializedQueryResult*>(execQR.release()));
        }

        const auto t1 = std::chrono::steady_clock::now();

        result             = ToQueryResult(*res);
        result.executionMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

        if (!result.ok())
            pImpl_->lastError = result.error;

    } catch (const std::exception& e) {
        pImpl_->lastError = e.what();
        result.error      = e.what();
    }

    return result;
}

int DuckDBAdapter::CountQuery(const DataQuery& q) const
{
    if (!IsConnected())
        return 0;

    try {
        auto [whereFragment, binds] = BuildWhereClause(q);

        const std::string sql = "SELECT COUNT(*) FROM " + QuoteId(q.table) + whereFragment;

        std::unique_ptr<duckdb::MaterializedQueryResult> res;
        if (binds.empty()) {
            res = pImpl_->conn->Query(sql);
        } else {
            auto stmt = pImpl_->conn->Prepare(sql);
            if (stmt->HasError()) {
                pImpl_->lastError = stmt->GetError();
                return 0;
            }
            auto view = binds | std::views::transform([](const std::string& s) { return duckdb::Value(s); });
            duckdb::vector<duckdb::Value> dvals(view.begin(), view.end());
            auto                          execQR = stmt->Execute(dvals, /*allow_stream_result=*/false);
            if (execQR->HasError()) {
                pImpl_->lastError = execQR->GetError();
                return 0;
            }
            res.reset(static_cast<duckdb::MaterializedQueryResult*>(execQR.release()));
        }

        if (res->HasError()) {
            pImpl_->lastError = res->GetError();
            return 0;
        }

        if (res->RowCount() > 0) {
            try {
                return std::stoi(res->GetValue(0, 0).ToString());
            } catch (...) {
            }
        }
        return 0;

    } catch (const std::exception& e) {
        pImpl_->lastError = e.what();
        return 0;
    }
}

QueryResult DuckDBAdapter::Execute(const std::string& sql) const
{
    QueryResult result;
    if (!IsConnected()) {
        result.error = "Not connected.";
        return result;
    }

    try {
        const auto t0 = std::chrono::steady_clock::now();

        auto res = pImpl_->conn->Query(sql);

        const auto t1 = std::chrono::steady_clock::now();

        result             = ToQueryResult(*res);
        result.executionMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

        if (!result.ok()) {
            pImpl_->lastError = result.error;
        } else {
            // DuckDB wraps DML (INSERT / UPDATE / DELETE) results as a single
            // row with one column named "Count" containing the affected-row count.
            if (result.columns.size() == 1 && result.columns[0].name == "Count" && result.rows.size() == 1 &&
                !result.rows[0].empty()) {
                try {
                    result.rowsAffected = std::stoll(result.rows[0][0]);
                } catch (...) {
                }
            }
        }

    } catch (const std::exception& e) {
        pImpl_->lastError = e.what();
        result.error      = e.what();
    }

    return result;
}

std::expected<void, Error> DuckDBAdapter::UpdateRow(const std::string&                                  table,
                                                    const std::unordered_map<std::string, std::string>& pkValues,
                                                    const std::unordered_map<std::string, std::string>& newValues)
{
    if (newValues.empty())
        return {};

    return require_writable()
        .and_then([&] { return require(!pkValues.empty(), Error{"No primary key supplied"}); })
        .and_then([&]() -> std::expected<void, Error> {
            std::vector<std::string> binds;
            binds.reserve(newValues.size() + pkValues.size());

            std::string sql      = "UPDATE " + QuoteId(table) + " SET ";
            int         paramIdx = 1;
            bool        first    = true;
            for (const auto& [col, val] : newValues) {
                if (!first)
                    sql += ", ";
                sql += QuoteId(col) + " = $" + std::to_string(paramIdx++);
                binds.push_back(val);
                first = false;
            }

            sql += " WHERE ";
            first = true;
            for (const auto& [col, val] : pkValues) {
                if (!first)
                    sql += " AND ";
                sql += QuoteId(col) + " = $" + std::to_string(paramIdx++);
                binds.push_back(val);
                first = false;
            }

            auto stmt = pImpl_->conn->Prepare(sql);
            if (stmt->HasError())
                return std::unexpected(Error{stmt->GetError()});

            auto view = binds | std::views::transform([](const std::string& s) { return duckdb::Value(s); });
            duckdb::vector<duckdb::Value> dvals(view.begin(), view.end());

            auto res = stmt->Execute(dvals, /*allow_stream_result=*/false);
            if (res->HasError()) {
                pImpl_->lastError = res->GetError();
                return std::unexpected(Error{res->GetError()});
            }
            return {};
        });
}

std::expected<void, Error> DuckDBAdapter::InsertRow(const std::string&                                  table,
                                                    const std::unordered_map<std::string, std::string>& values)
{
    return require_writable()
        .and_then([&] { return require(!values.empty(), Error{"No column values supplied"}); })
        .and_then([&]() -> std::expected<void, Error> {
            std::vector<std::string> binds;
            binds.reserve(values.size());

            std::string cols, placeholders;
            int         paramIdx = 1;
            bool        first    = true;
            for (const auto& [col, val] : values) {
                if (!first) {
                    cols += ", ";
                    placeholders += ", ";
                }
                cols += QuoteId(col);
                placeholders += "$" + std::to_string(paramIdx++);
                binds.push_back(val);
                first = false;
            }

            const std::string sql = "INSERT INTO " + QuoteId(table) + " (" + cols + ") VALUES (" + placeholders + ")";

            auto stmt = pImpl_->conn->Prepare(sql);
            if (stmt->HasError())
                return std::unexpected(Error{stmt->GetError()});

            auto view = binds | std::views::transform([](const std::string& s) { return duckdb::Value(s); });
            duckdb::vector<duckdb::Value> dvals(view.begin(), view.end());

            auto res = stmt->Execute(dvals, /*allow_stream_result=*/false);
            if (res->HasError()) {
                pImpl_->lastError = res->GetError();
                return std::unexpected(Error{res->GetError()});
            }
            return {};
        });
}

std::expected<void, Error> DuckDBAdapter::DeleteRow(const std::string&                                  table,
                                                    const std::unordered_map<std::string, std::string>& pkValues)
{
    return require_writable()
        .and_then([&] { return require(!pkValues.empty(), Error{"No primary key values supplied"}); })
        .and_then([&]() -> std::expected<void, Error> {
            std::vector<std::string> binds;
            binds.reserve(pkValues.size());

            std::string sql      = "DELETE FROM " + QuoteId(table) + " WHERE ";
            int         paramIdx = 1;
            bool        first    = true;
            for (const auto& [col, val] : pkValues) {
                if (!first)
                    sql += " AND ";
                sql += QuoteId(col) + " = $" + std::to_string(paramIdx++);
                binds.push_back(val);
                first = false;
            }

            auto stmt = pImpl_->conn->Prepare(sql);
            if (stmt->HasError())
                return std::unexpected(Error{stmt->GetError()});

            auto view = binds | std::views::transform([](const std::string& s) { return duckdb::Value(s); });
            duckdb::vector<duckdb::Value> dvals(view.begin(), view.end());

            auto res = stmt->Execute(dvals, /*allow_stream_result=*/false);
            if (res->HasError()) {
                pImpl_->lastError = res->GetError();
                return std::unexpected(Error{res->GetError()});
            }
            return {};
        });
}

/// Build a valid SQL identifier from an arbitrary file stem.
/// Replaces every character that isn't [A-Za-z0-9_] with '_' and
/// prepends '_' if the first character is a digit.
std::string SanitizeViewName(std::string_view stem)
{
    std::string out;
    out.reserve(stem.size() + 1);
    for (const char c : stem) {
        out += (std::isalnum(static_cast<unsigned char>(c)) || c == '_') ? c : '_';
    }
    if (!out.empty() && std::isdigit(static_cast<unsigned char>(out[0])))
        out.insert(out.begin(), '_');
    return out.empty() ? "file_source" : out;
}

/// Map a lowercased file extension to the DuckDB reader function.
/// Returns "" for unsupported extensions.
std::string_view ReaderFunctionFor(std::string_view ext) noexcept
{
    if (ext == ".csv" || ext == ".tsv" || ext == ".txt")
        return "read_csv_auto";
    if (ext == ".parquet")
        return "read_parquet";
    if (ext == ".json" || ext == ".ndjson" || ext == ".jsonl")
        return "read_json_auto";
    return {};
}

bool DuckDBAdapter::IsQueryableExtension(std::string_view ext) noexcept
{
    return !ReaderFunctionFor(ext).empty();
}

std::expected<void, Error> DuckDBAdapter::ScanFile(const std::string& filePath, const std::string& viewName)
{
    if (!IsConnected())
        return std::unexpected(Error{"Not connected"});

    namespace fs = std::filesystem;

    // Compute the lowercased extension
    const fs::path p{filePath};
    std::string    ext = p.extension().string();
    std::transform(
        ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    const auto reader = ReaderFunctionFor(ext);
    if (reader.empty())
        return std::unexpected(Error{"Unsupported file extension for DuckDB scan: " + ext});

    // Resolve the view name
    const std::string vname = viewName.empty() ? SanitizeViewName(p.stem().string()) : viewName;

    // Escape the file path (single quotes + backslash)
    std::string escaped;
    escaped.reserve(filePath.size() + 2);
    for (const char c : filePath) {
        if (c == '\'')
            escaped += "''";
        else
            escaped += c;
    }

    const std::string sql =
        "CREATE OR REPLACE VIEW " + QuoteId(vname) + " AS SELECT * FROM " + std::string(reader) + "('" + escaped + "')";

    auto res = pImpl_->conn->Query(sql);
    if (res->HasError()) {
        pImpl_->lastError = res->GetError();
        return std::unexpected(Error{pImpl_->lastError});
    }

    pImpl_->fileSources.insert(vname);
    return {};
}

std::expected<void, Error> DuckDBAdapter::ScanDirectory(const std::string& dirPath)
{
    if (!IsConnected())
        return std::unexpected(Error{"Not connected"});

    namespace fs = std::filesystem;

    std::error_code ec;
    if (!fs::is_directory(dirPath, ec))
        return std::unexpected(Error{dirPath + " is not a directory"});

    std::vector<std::string> failures;

    for (const auto& entry : fs::directory_iterator(dirPath, ec)) {
        if (!entry.is_regular_file())
            continue;

        std::string ext = entry.path().extension().string();
        std::transform(
            ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (!IsQueryableExtension(ext))
            continue;

        const auto result = ScanFile(entry.path().string());
        if (!result)
            failures.emplace_back(entry.path().filename().string() + ": " + result.error());
    }

    if (!failures.empty()) {
        std::string msg = "Some files could not be scanned:";
        for (const auto& f : failures) {
            msg += "\n  ";
            msg += f;
        }
        return std::unexpected(Error{msg});
    }
    return {};
}

std::vector<std::string> DuckDBAdapter::GetFileSources() const
{
    if (!pImpl_)
        return {};
    return {pImpl_->fileSources.begin(), pImpl_->fileSources.end()};
}

namespace {

/// Map a compile-time C++ type to the corresponding duckdb::LogicalType.
/// A missing specialisation causes a clear static_assert at instantiation
/// time — add the type here rather than handling it silently at runtime.
template<typename T>
[[nodiscard]] duckdb::LogicalType DuckLogicalType()
{
    if constexpr (std::is_same_v<T, bool>)
        return duckdb::LogicalType::BOOLEAN;
    else if constexpr (std::is_same_v<T, int32_t>)
        return duckdb::LogicalType::INTEGER;
    else if constexpr (std::is_same_v<T, int64_t>)
        return duckdb::LogicalType::BIGINT;
    else if constexpr (std::is_same_v<T, float>)
        return duckdb::LogicalType::FLOAT;
    else if constexpr (std::is_same_v<T, double>)
        return duckdb::LogicalType::DOUBLE;
    else if constexpr (std::is_same_v<T, std::string>)
        return duckdb::LogicalType::VARCHAR;
    else
        static_assert(sizeof(T) == 0,
                      "DuckDB: no LogicalType mapping for this C++ type; "
                      "add a specialisation or choose a supported ScalarType.");
}

/// Build a runtime duckdb::LogicalType from a ScalarType tag via
/// VisitScalarType + DuckLogicalType<T>().
[[nodiscard]] duckdb::LogicalType DuckLogicalTypeOf(ScalarType t)
{
    return VisitScalarType(t, []<typename T>(std::type_identity<T>) { return DuckLogicalType<T>(); });
}

/// Extract a value of type T from a DuckDB UnifiedVectorFormat at
/// pre-resolved selection index idx (= vf.sel->get_index(i)).
///
///  • bool        → stored as uint8_t; non-zero means true.
///  • std::string → copy-constructed from DuckDB's string_t.
///  • arithmetic  → plain reinterpret_cast of the data pointer.
template<typename T>
[[nodiscard]] T DuckGet(const duckdb::UnifiedVectorFormat& vf, duckdb::idx_t idx)
{
    if constexpr (std::is_same_v<T, bool>) {
        return reinterpret_cast<const uint8_t*>(vf.data)[idx] != 0u;
    } else if constexpr (std::is_same_v<T, std::string>) {
        return reinterpret_cast<const duckdb::string_t*>(vf.data)[idx].GetString();
    } else {
        return reinterpret_cast<const T*>(vf.data)[idx];
    }
}

/// Store a value of type T into a DuckDB FLAT_VECTOR result at position i.
///
///  • std::string → intern into DuckDB's string heap via AddString; stores
///                  the returned string_t handle into the flat array.
///  • everything else → write directly into the typed flat array.
template<typename T>
void DuckSet(duckdb::Vector& result, duckdb::idx_t i, const T& val)
{
    if constexpr (std::is_same_v<T, std::string>) {
        duckdb::FlatVector::GetData<duckdb::string_t>(result)[i] = duckdb::StringVector::AddString(result, val);
    } else {
        duckdb::FlatVector::GetData<T>(result)[i] = val;
    }
}

// Each builder captures a typed std::function and returns a
// duckdb::scalar_function_t (the DataChunk/UnifiedVectorFormat handler that
// DuckDB calls for every batch of rows).
//
// NULL semantics: if any input for a row is NULL the output is also marked
// NULL and the user function is NOT called for that row.

/// Unary trampoline — wraps std::function<R(Arg)>.
template<typename R, typename Arg>
[[nodiscard]] duckdb::scalar_function_t MakeUnaryTrampoline(std::function<R(Arg)> fn)
{
    return [fn](duckdb::DataChunk& args, duckdb::ExpressionState&, duckdb::Vector& result) {
        result.SetVectorType(duckdb::VectorType::FLAT_VECTOR);

        duckdb::UnifiedVectorFormat vf;
        args.data[0].ToUnifiedFormat(args.size(), vf);

        auto& outValidity = duckdb::FlatVector::Validity(result);

        for (duckdb::idx_t i = 0; i < args.size(); ++i) {
            const auto idx = vf.sel->get_index(i);
            if (!vf.validity.RowIsValid(idx)) {
                outValidity.SetInvalid(i);
                continue;
            }
            DuckSet<R>(result, i, fn(DuckGet<Arg>(vf, idx)));
        }
    };
}

/// Binary trampoline — wraps std::function<R(A1, A2)>.
/// Each argument vector is unified independently so that dictionary /
/// constant / flat input vectors are all handled uniformly.
template<typename R, typename A1, typename A2>
[[nodiscard]] duckdb::scalar_function_t MakeBinaryTrampoline(std::function<R(A1, A2)> fn)
{
    return [fn](duckdb::DataChunk& args, duckdb::ExpressionState&, duckdb::Vector& result) {
        result.SetVectorType(duckdb::VectorType::FLAT_VECTOR);

        duckdb::UnifiedVectorFormat vf0, vf1;
        args.data[0].ToUnifiedFormat(args.size(), vf0);
        args.data[1].ToUnifiedFormat(args.size(), vf1);

        auto& outValidity = duckdb::FlatVector::Validity(result);

        for (duckdb::idx_t i = 0; i < args.size(); ++i) {
            const auto i0 = vf0.sel->get_index(i);
            const auto i1 = vf1.sel->get_index(i);
            if (!vf0.validity.RowIsValid(i0) || !vf1.validity.RowIsValid(i1)) {
                outValidity.SetInvalid(i);
                continue;
            }
            DuckSet<R>(result, i, fn(DuckGet<A1>(vf0, i0), DuckGet<A2>(vf1, i1)));
        }
    };
}

/// Ternary trampoline — wraps std::function<R(A1, A2, A3)>.
template<typename R, typename A1, typename A2, typename A3>
[[nodiscard]] duckdb::scalar_function_t MakeTernaryTrampoline(std::function<R(A1, A2, A3)> fn)
{
    return [fn](duckdb::DataChunk& args, duckdb::ExpressionState&, duckdb::Vector& result) {
        result.SetVectorType(duckdb::VectorType::FLAT_VECTOR);

        duckdb::UnifiedVectorFormat vf0, vf1, vf2;
        args.data[0].ToUnifiedFormat(args.size(), vf0);
        args.data[1].ToUnifiedFormat(args.size(), vf1);
        args.data[2].ToUnifiedFormat(args.size(), vf2);

        auto& outValidity = duckdb::FlatVector::Validity(result);

        for (duckdb::idx_t i = 0; i < args.size(); ++i) {
            const auto i0 = vf0.sel->get_index(i);
            const auto i1 = vf1.sel->get_index(i);
            const auto i2 = vf2.sel->get_index(i);
            if (!vf0.validity.RowIsValid(i0) || !vf1.validity.RowIsValid(i1) || !vf2.validity.RowIsValid(i2)) {
                outValidity.SetInvalid(i);
                continue;
            }
            DuckSet<R>(result, i, fn(DuckGet<A1>(vf0, i0), DuckGet<A2>(vf1, i1), DuckGet<A3>(vf2, i2)));
        }
    };
}

} // anonymous namespace

//
// Dispatches on the runtime ScalarType tags packed into ScalarUDFDesc to
// recover the concrete std::function<R(Args...)> from the type-erased
// std::any callable, builds the matching DataChunk trampoline, and
// registers it with DuckDB's Connection::CreateScalarFunction.
//
// C++23 generic-lambda tag-dispatch pattern used throughout:
//
//   VisitScalarType(t, [&]<typename T>(std::type_identity<T>) { … });
//
// Each invocation instantiates the lambda body with T bound to the concrete
// C++ type that corresponds to t at compile time.  The nested form peels off
// one type per argument level.  Explicit return types on each lambda prevent
// deduction ambiguity across the six ScalarType variants.

std::expected<void, Error> DuckDBAdapter::RegisterScalarImpl(ScalarUDFDesc desc)
{
    return require(IsConnected(), Error{"Not connected"}).and_then([&]() -> std::expected<void, Error> {
        try {
            const auto arity = desc.argTypes.size();

            if (arity == 1) {
                return VisitScalarType(
                    desc.returnType, [&]<typename R>(std::type_identity<R>) -> std::expected<void, Error> {
                        return VisitScalarType(
                            desc.argTypes[0], [&]<typename A>(std::type_identity<A>) -> std::expected<void, Error> {
                                auto* fn = std::any_cast<std::function<R(A)>>(&desc.callable);
                                if (!fn)
                                    return std::unexpected(Error{"UDF '" + desc.name +
                                                                 "': callable type mismatch "
                                                                 "(check R and Arg template parameters)"});

                                pImpl_->conn->CreateVectorizedFunction(desc.name,
                                                                       {DuckLogicalType<A>()},
                                                                       DuckLogicalType<R>(),
                                                                       MakeUnaryTrampoline<R, A>(*fn));
                                return {};
                            });
                    });
            }

            if (arity == 2) {
                return VisitScalarType(
                    desc.returnType, [&]<typename R>(std::type_identity<R>) -> std::expected<void, Error> {
                        return VisitScalarType(
                            desc.argTypes[0], [&]<typename A1>(std::type_identity<A1>) -> std::expected<void, Error> {
                                return VisitScalarType(
                                    desc.argTypes[1],
                                    [&]<typename A2>(std::type_identity<A2>) -> std::expected<void, Error> {
                                        auto* fn = std::any_cast<std::function<R(A1, A2)>>(&desc.callable);
                                        if (!fn)
                                            return std::unexpected(Error{"UDF '" + desc.name +
                                                                         "': callable type mismatch "
                                                                         "(check R, A1, A2 template parameters)"});

                                        pImpl_->conn->CreateVectorizedFunction(
                                            desc.name,
                                            {DuckLogicalType<A1>(), DuckLogicalType<A2>()},
                                            DuckLogicalType<R>(),
                                            MakeBinaryTrampoline<R, A1, A2>(*fn));
                                        return {};
                                    });
                            });
                    });
            }

            if (arity == 3) {
                return VisitScalarType(
                    desc.returnType, [&]<typename R>(std::type_identity<R>) -> std::expected<void, Error> {
                        return VisitScalarType(
                            desc.argTypes[0], [&]<typename A1>(std::type_identity<A1>) -> std::expected<void, Error> {
                                return VisitScalarType(
                                    desc.argTypes[1],
                                    [&]<typename A2>(std::type_identity<A2>) -> std::expected<void, Error> {
                                        return VisitScalarType(
                                            desc.argTypes[2],
                                            [&]<typename A3>(std::type_identity<A3>) -> std::expected<void, Error> {
                                                auto* fn = std::any_cast<std::function<R(A1, A2, A3)>>(&desc.callable);
                                                if (!fn)
                                                    return std::unexpected(
                                                        Error{"UDF '" + desc.name +
                                                              "': callable type mismatch "
                                                              "(check R, A1, A2, A3 template parameters)"});

                                                pImpl_->conn->CreateVectorizedFunction(
                                                    desc.name,
                                                    {DuckLogicalType<A1>(),
                                                     DuckLogicalType<A2>(),
                                                     DuckLogicalType<A3>()},
                                                    DuckLogicalType<R>(),
                                                    MakeTernaryTrampoline<R, A1, A2, A3>(*fn));
                                                return {};
                                            });
                                    });
                            });
                    });
            }

            return std::unexpected(Error{"DuckDB UDF '" + desc.name + "': unsupported arity " + std::to_string(arity) +
                                         " (supported: 1, 2, 3)"});

        } catch (const std::exception& e) {
            pImpl_->lastError = e.what();
            return std::unexpected(Error{e.what()});
        }
    });
}

//
// Called by the type-safe RegisterVectorized<R, Args...> template in the
// header after it has captured the compile-time type information as runtime
// ScalarType lists.  Converts those back to duckdb::LogicalType and registers
// the raw DataChunk handler via Connection::CreateScalarFunction.
//
// This lets the public header stay free of duckdb.hpp while still exposing
// the full vectorized UDF API to callers who include the complete DuckDB
// headers themselves.

std::expected<void, Error> DuckDBAdapter::RegisterVectorizedImpl(std::string_view          name,
                                                                 duckdb::scalar_function_t fn,
                                                                 std::vector<ScalarType>   argTypes,
                                                                 ScalarType                returnType)
{
    return require(IsConnected(), Error{"Not connected"}).and_then([&]() -> std::expected<void, Error> {
        try {
            std::vector<duckdb::LogicalType> duckArgs;
            duckArgs.reserve(argTypes.size());
            for (const auto t : argTypes)
                duckArgs.push_back(DuckLogicalTypeOf(t));

            pImpl_->conn->CreateVectorizedFunction(
                std::string(name), std::move(duckArgs), DuckLogicalTypeOf(returnType), std::move(fn));
            return {};
        } catch (const std::exception& e) {
            pImpl_->lastError = e.what();
            return std::unexpected(Error{e.what()});
        }
    });
}

} // namespace datagrid::adapters
