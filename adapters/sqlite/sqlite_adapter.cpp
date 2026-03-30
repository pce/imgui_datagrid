#include "sqlite_adapter.hpp"
#include "../adapter_registry.hpp"

#include <SQLiteCpp/SQLiteCpp.h>
#include <chrono>
#include <sstream>
#include <string>
#include <vector>

// Self-register before main() — no changes to App or main needed.
REGISTER_ADAPTER(Adapters::SQLiteAdapter, "sqlite")

namespace Adapters {

// ============================================================
//  Impl — hidden SQLiteCpp state
// ============================================================
struct SQLiteAdapter::Impl {
    std::unique_ptr<SQLite::Database> db;
    std::string                       path;
    std::string                       lastError;
    bool                              readOnly = false;
};

// ============================================================
//  Move operations  —  defined here (not in the header) so that
//  unique_ptr<Impl> can be moved with a *complete* Impl type.
//  Declaring = default in the header would fail because Impl is
//  only forward-declared there.
// ============================================================
SQLiteAdapter::SQLiteAdapter(SQLiteAdapter&&) noexcept            = default;
SQLiteAdapter& SQLiteAdapter::operator=(SQLiteAdapter&&) noexcept = default;

// ============================================================
//  Construction / destruction
// ============================================================
SQLiteAdapter::SQLiteAdapter()
    : pImpl_(std::make_unique<Impl>())
{}

SQLiteAdapter::~SQLiteAdapter()
{
    Disconnect();
}

// ============================================================
//  Connection lifecycle
// ============================================================
std::expected<void, Error> SQLiteAdapter::Connect(const ConnectionParams& params)
{
    Disconnect();
    pImpl_->path      = params.connectionString;
    pImpl_->lastError = {};

    try {
        const int flags = (params.readOnly && *params.readOnly)
            ? SQLite::OPEN_READONLY
            : SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE;

        pImpl_->db = std::make_unique<SQLite::Database>(pImpl_->path, flags);

        // Store read-only flag for SupportsWrite()
        pImpl_->readOnly = (params.readOnly && *params.readOnly);

#ifdef HAVE_SQLCIPHER
        // SQLCipher: apply encryption key before any schema access
        if (params.password && !params.password->empty()) {
            try {
                pImpl_->db->exec("PRAGMA key = '" + *params.password + "';");
                // Verify the key is correct — wrong key → sqlite_master is unreadable
                SQLite::Statement chk(*pImpl_->db, "SELECT count(*) FROM sqlite_master");
                chk.executeStep();
            } catch (const SQLite::Exception& e) {
                pImpl_->lastError =
                    std::string{"Wrong passphrase or not an encrypted database: "} + e.what();
                pImpl_->db.reset();
                return std::unexpected(pImpl_->lastError);
            }
        }
#endif

        if (!pImpl_->readOnly)
            pImpl_->db->exec("PRAGMA journal_mode=WAL;");

        return {};
    } catch (const SQLite::Exception& e) {
        pImpl_->lastError = e.what();
        return std::unexpected(std::string{e.what()});
    }
}

void SQLiteAdapter::Disconnect()
{
    pImpl_->db.reset();
}

bool SQLiteAdapter::IsConnected() const
{
    return pImpl_->db != nullptr;
}

std::string SQLiteAdapter::LastError() const
{
    return pImpl_->lastError;
}

// ============================================================
//  Adapter identity
// ============================================================
std::string SQLiteAdapter::AdapterLabel() const
{
    if (!IsConnected()) return "SQLite (disconnected)";
    try {
        SQLite::Statement stmt(*pImpl_->db, "SELECT sqlite_version()");
        if (stmt.executeStep())
            return "SQLite " + std::string(stmt.getColumn(0).getText());
    } catch (...) {}
    return "SQLite";
}

// ============================================================
//  Schema navigation
// ============================================================
std::vector<std::string> SQLiteAdapter::GetCatalogs() const
{
    if (!IsConnected()) return {};
    // SQLite doesn't have catalogs — return the file path as the single entry
    return { pImpl_->path };
}

std::vector<TableInfo> SQLiteAdapter::GetTables(const std::string& /*catalog*/) const
{
    if (!IsConnected()) return {};

    try {
        // sqlite_master holds tables AND views; we surface both.
        SQLite::Statement stmt(
            *pImpl_->db,
            "SELECT type, name FROM sqlite_master "
            "WHERE type IN ('table','view') AND name NOT LIKE 'sqlite_%' "
            "ORDER BY type, name"
        );

        std::vector<TableInfo> tables;
        while (stmt.executeStep()) {
            TableInfo t;
            t.kind    = stmt.getColumn(0).getText();   // "table" or "view"
            t.name    = stmt.getColumn(1).getText();
            t.catalog = pImpl_->path;
            tables.push_back(std::move(t));
        }
        return tables;
    } catch (const SQLite::Exception& e) {
        pImpl_->lastError = e.what();
        return {};
    }
}

std::vector<ColumnInfo> SQLiteAdapter::GetColumns(const std::string& table) const
{
    if (!IsConnected()) return {};

    try {
        // PRAGMA table_info returns: cid, name, type, notnull, dflt_value, pk
        SQLite::Statement stmt(
            *pImpl_->db,
            "PRAGMA table_info(" + table + ")"   // table comes from GetTables(), not user input
        );

        std::vector<ColumnInfo> cols;
        while (stmt.executeStep()) {
            ColumnInfo c;
            c.name       = stmt.getColumn(1).getText();  // name
            c.typeName   = stmt.getColumn(2).getText();  // type
            c.nullable   = (stmt.getColumn(3).getInt() == 0); // notnull=0 → nullable
            c.primaryKey = (stmt.getColumn(5).getInt() != 0); // pk != 0
            cols.push_back(std::move(c));
        }
        return cols;
    } catch (const SQLite::Exception& e) {
        pImpl_->lastError = e.what();
        return {};
    }
}

// ============================================================
//  Internal: WHERE clause builder
//
//  All user-supplied filter values are pushed into `outBinds`
//  and referenced via `?` placeholders — no string interpolation
//  of values, so SQL injection is structurally impossible here.
// ============================================================
static void BuildWhere(
    const DataQuery&          q,
    std::string&              sql,
    std::vector<std::string>& outBinds)
{
    std::vector<std::string> clauses;

    for (const auto& [col, val] : q.whereExact) {
        clauses.push_back(col + " = ?");
        outBinds.push_back(val);
    }

    if (!q.searchColumn.empty() && !q.searchValue.empty()) {
        clauses.push_back(q.searchColumn + " LIKE ?");
        outBinds.push_back("%" + q.searchValue + "%");
    }

    if (clauses.empty()) return;

    sql += " WHERE ";
    for (size_t i = 0; i < clauses.size(); ++i) {
        if (i > 0) sql += " AND ";
        sql += clauses[i];
    }
}

// ============================================================
//  ExecuteQuery  —  structured, paginated query
// ============================================================
QueryResult SQLiteAdapter::ExecuteQuery(const DataQuery& q) const
{
    QueryResult result;
    if (!IsConnected()) {
        result.error = "Not connected.";
        return result;
    }

    try {
        const auto t0 = std::chrono::steady_clock::now();

        std::vector<std::string> binds;
        std::string sql = "SELECT * FROM " + q.table;
        BuildWhere(q, sql, binds);

        if (!q.sortColumn.empty())
            sql += " ORDER BY " + q.sortColumn + (q.sortAscending ? " ASC" : " DESC");

        sql += " LIMIT ? OFFSET ?";

        SQLite::Statement stmt(*pImpl_->db, sql);

        int idx = 1;
        for (const auto& v : binds) stmt.bind(idx++, v);
        stmt.bind(idx++, q.pageSize);
        stmt.bind(idx,   q.page * q.pageSize);

        // Column metadata from the first result set
        const int colCount = stmt.getColumnCount();
        result.columns.reserve(static_cast<size_t>(colCount));
        for (int c = 0; c < colCount; ++c) {
            ColumnInfo ci;
            ci.name     = stmt.getColumnName(c);
            // getDeclaredType() is available on SQLiteCpp for declared type
            ci.typeName = stmt.getColumnDeclaredType(c) ? stmt.getColumnDeclaredType(c) : "";
            result.columns.push_back(std::move(ci));
        }

        while (stmt.executeStep()) {
            std::vector<std::string> row;
            row.reserve(static_cast<size_t>(colCount));
            for (int c = 0; c < colCount; ++c) {
                const SQLite::Column col = stmt.getColumn(c);
                row.emplace_back(col.isNull() ? "" : col.getText());
            }
            result.rows.push_back(std::move(row));
        }

        const auto t1 = std::chrono::steady_clock::now();
        result.executionMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

    } catch (const SQLite::Exception& e) {
        pImpl_->lastError = e.what();
        result.error      = e.what();
    }

    return result;
}

// ============================================================
//  CountQuery  —  total matching rows (for pagination bar)
// ============================================================
int SQLiteAdapter::CountQuery(const DataQuery& q) const
{
    if (!IsConnected()) return 0;

    try {
        std::vector<std::string> binds;
        std::string sql = "SELECT COUNT(*) FROM " + q.table;
        BuildWhere(q, sql, binds);

        SQLite::Statement stmt(*pImpl_->db, sql);

        int idx = 1;
        for (const auto& v : binds) stmt.bind(idx++, v);

        if (stmt.executeStep())
            return stmt.getColumn(0).getInt();

        return 0;
    } catch (const SQLite::Exception& e) {
        pImpl_->lastError = e.what();
        return 0;
    }
}

// ============================================================
//  Execute  —  raw SQL (query editor path)
//
//  Returns ALL matching rows with no pagination.
//  The caller (DataBrowser SQL editor) is responsible for
//  injection safety of user-supplied SQL strings.
// ============================================================
QueryResult SQLiteAdapter::Execute(const std::string& sql) const
{
    QueryResult result;
    if (!IsConnected()) {
        result.error = "Not connected.";
        return result;
    }

    try {
        const auto t0 = std::chrono::steady_clock::now();

        SQLite::Statement stmt(*pImpl_->db, sql);
        const int colCount = stmt.getColumnCount();

        result.columns.reserve(static_cast<size_t>(colCount));
        for (int c = 0; c < colCount; ++c) {
            ColumnInfo ci;
            ci.name     = stmt.getColumnName(c);
            ci.typeName = stmt.getColumnDeclaredType(c) ? stmt.getColumnDeclaredType(c) : "";
            result.columns.push_back(std::move(ci));
        }

        while (stmt.executeStep()) {
            std::vector<std::string> row;
            row.reserve(static_cast<size_t>(colCount));
            for (int c = 0; c < colCount; ++c) {
                const SQLite::Column col = stmt.getColumn(c);
                row.emplace_back(col.isNull() ? "" : col.getText());
            }
            result.rows.push_back(std::move(row));
        }

        result.rowsAffected = pImpl_->db->getChanges();

        const auto t1 = std::chrono::steady_clock::now();
        result.executionMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

    } catch (const SQLite::Exception& e) {
        pImpl_->lastError = e.what();
        result.error      = e.what();
    }

    return result;
}

// ============================================================
//  Write support
// ============================================================
bool SQLiteAdapter::SupportsWrite() const
{
    return IsConnected() && !pImpl_->readOnly;
}

std::expected<void, Adapters::Error> SQLiteAdapter::UpdateRow(
    const std::string&                                   table,
    const std::unordered_map<std::string, std::string>& pkValues,
    const std::unordered_map<std::string, std::string>& newValues)
{
    if (!IsConnected())   return std::unexpected(Error{"Not connected"});
    if (pImpl_->readOnly) return std::unexpected(Error{"Connection is read-only"});
    if (pkValues.empty()) return std::unexpected(Error{"No primary key supplied"});
    if (newValues.empty()) return {};   // nothing to update

    // Build:  UPDATE <table> SET col1=?, col2=? WHERE pk1=? AND pk2=?
    std::string sql = "UPDATE " + table + " SET ";
    std::vector<std::string> binds;
    binds.reserve(newValues.size() + pkValues.size());

    bool first = true;
    for (const auto& [col, val] : newValues) {
        if (!first) sql += ", ";
        sql += col + " = ?";
        binds.push_back(val);
        first = false;
    }

    sql += " WHERE ";
    first = true;
    for (const auto& [col, val] : pkValues) {
        if (!first) sql += " AND ";
        sql += col + " = ?";
        binds.push_back(val);
        first = false;
    }

    try {
        SQLite::Statement stmt(*pImpl_->db, sql);
        for (int i = 0; i < static_cast<int>(binds.size()); ++i)
            stmt.bind(i + 1, binds[i]);
        stmt.exec();
        return {};
    } catch (const SQLite::Exception& e) {
        pImpl_->lastError = e.what();
        return std::unexpected(Error{e.what()});
    }
}

std::expected<void, Adapters::Error> SQLiteAdapter::InsertRow(
    const std::string&                                   table,
    const std::unordered_map<std::string, std::string>& values)
{
    if (!IsConnected())   return std::unexpected(Error{"Not connected"});
    if (pImpl_->readOnly) return std::unexpected(Error{"Connection is read-only"});
    if (values.empty())   return std::unexpected(Error{"No column values supplied"});

    // INSERT INTO <table> (col1, col2, ...) VALUES (?, ?, ...)
    std::string cols, placeholders;
    std::vector<std::string> binds;
    binds.reserve(values.size());
    bool first = true;
    for (const auto& [col, val] : values) {
        if (!first) { cols += ", "; placeholders += ", "; }
        cols         += col;
        placeholders += "?";
        binds.push_back(val);
        first = false;
    }
    const std::string sql =
        "INSERT INTO " + table + " (" + cols + ") VALUES (" + placeholders + ")";

    try {
        SQLite::Statement stmt(*pImpl_->db, sql);
        for (int i = 0; i < static_cast<int>(binds.size()); ++i)
            stmt.bind(i + 1, binds[i]);
        stmt.exec();
        return {};
    } catch (const SQLite::Exception& e) {
        pImpl_->lastError = e.what();
        return std::unexpected(Error{e.what()});
    }
}

std::expected<void, Adapters::Error> SQLiteAdapter::DeleteRow(
    const std::string&                                   table,
    const std::unordered_map<std::string, std::string>& pkValues)
{
    if (!IsConnected())   return std::unexpected(Error{"Not connected"});
    if (pImpl_->readOnly) return std::unexpected(Error{"Connection is read-only"});
    if (pkValues.empty()) return std::unexpected(Error{"No primary key values supplied"});

    // DELETE FROM <table> WHERE pk1 = ? AND pk2 = ?
    std::string sql = "DELETE FROM " + table + " WHERE ";
    std::vector<std::string> binds;
    binds.reserve(pkValues.size());
    bool first = true;
    for (const auto& [col, val] : pkValues) {
        if (!first) sql += " AND ";
        sql += col + " = ?";
        binds.push_back(val);
        first = false;
    }

    try {
        SQLite::Statement stmt(*pImpl_->db, sql);
        for (int i = 0; i < static_cast<int>(binds.size()); ++i)
            stmt.bind(i + 1, binds[i]);
        stmt.exec();
        return {};
    } catch (const SQLite::Exception& e) {
        pImpl_->lastError = e.what();
        return std::unexpected(Error{e.what()});
    }
}

} // namespace Adapters
