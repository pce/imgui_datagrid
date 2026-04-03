#include "sqlite_adapter.hpp"
#include "../adapter_registry.hpp"
#include "../utils/expected_utils.hpp"

#include <SQLiteCpp/SQLiteCpp.h>
#include <chrono>
#include <sstream>
#include <string>
#include <vector>

namespace {
const datagrid::adapters::RegisterAdapter<datagrid::adapters::SQLiteAdapter> kSQLiteReg{"sqlite"};
}

namespace datagrid::adapters {

struct SQLiteAdapter::Impl
{
    std::unique_ptr<SQLite::Database> db;
    std::string                       path;
    std::string                       lastError;
    bool                              readOnly = false;
};

SQLiteAdapter::SQLiteAdapter(SQLiteAdapter&&) noexcept            = default;
SQLiteAdapter& SQLiteAdapter::operator=(SQLiteAdapter&&) noexcept = default;

SQLiteAdapter::SQLiteAdapter() : pImpl_(std::make_unique<Impl>()) {}

SQLiteAdapter::~SQLiteAdapter()
{
    Disconnect();
}

std::expected<void, Error> SQLiteAdapter::Connect(const ConnectionParams& params)
{
    Disconnect();
    pImpl_->path      = params.connectionString;
    pImpl_->lastError = {};

    try {
        const int flags = (params.readOnly && *params.readOnly) ? SQLite::OPEN_READONLY
                                                                : SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE;

        pImpl_->db       = std::make_unique<SQLite::Database>(pImpl_->path, flags);
        pImpl_->readOnly = (params.readOnly && *params.readOnly);
        readOnly_        = pImpl_->readOnly; // sync base-class guard

#ifdef HAVE_SQLCIPHER
        if (params.password && !params.password->empty()) {
            try {
                pImpl_->db->exec("PRAGMA key = '" + *params.password + "';");
                SQLite::Statement chk(*pImpl_->db, "SELECT count(*) FROM sqlite_master");
                chk.executeStep();
            } catch (const SQLite::Exception& e) {
                pImpl_->lastError = std::string{"Wrong passphrase or not an encrypted database: "} + e.what();
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

std::string SQLiteAdapter::AdapterLabel() const
{
    if (!IsConnected())
        return "SQLite (disconnected)";
    try {
        SQLite::Statement stmt(*pImpl_->db, "SELECT sqlite_version()");
        if (stmt.executeStep())
            return "SQLite " + std::string(stmt.getColumn(0).getText());
    } catch (...) {
    }
    return "SQLite";
}

std::vector<std::string> SQLiteAdapter::GetCatalogs() const
{
    if (!IsConnected())
        return {};
    return {pImpl_->path};
}

std::vector<TableInfo> SQLiteAdapter::GetTables(const std::string& /*catalog*/) const
{
    if (!IsConnected())
        return {};

    try {
        SQLite::Statement stmt(*pImpl_->db,
                               "SELECT type, name FROM sqlite_master "
                               "WHERE type IN ('table','view') AND name NOT LIKE 'sqlite_%' "
                               "ORDER BY type, name");

        std::vector<TableInfo> tables;
        while (stmt.executeStep()) {
            TableInfo t;
            t.kind    = stmt.getColumn(0).getText();
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
    if (!IsConnected())
        return {};

    try {
        SQLite::Statement stmt(*pImpl_->db, "PRAGMA table_info(" + table + ")");

        std::vector<ColumnInfo> cols;
        while (stmt.executeStep()) {
            ColumnInfo c;
            c.name       = stmt.getColumn(1).getText();
            c.typeName   = stmt.getColumn(2).getText();
            c.nullable   = (stmt.getColumn(3).getInt() == 0);
            c.primaryKey = (stmt.getColumn(5).getInt() != 0);

            // Detect image columns by declared type or naming convention.
            // BLOB / IMAGE types carry raw bytes  → "image_blob"
            // Text columns whose name suggests an image path → "image_path"
            {
                std::string typeUp = c.typeName;
                for (auto& ch : typeUp)
                    ch = static_cast<char>(toupper(static_cast<unsigned char>(ch)));

                std::string nameLo = c.name;
                for (auto& ch : nameLo)
                    ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));

                auto nameHas = [&](const char* sub) { return nameLo.find(sub) != std::string::npos; };

                if (typeUp == "BLOB" || typeUp == "IMAGE") {
                    c.displayHint = "image_blob";
                } else if (nameHas("image") || nameHas("photo") || nameHas("avatar") || nameHas("thumb") ||
                           nameHas("thumbnail") || nameHas("icon")) {
                    c.displayHint = "image_path";
                }
            }

            cols.push_back(std::move(c));
        }
        return cols;
    } catch (const SQLite::Exception& e) {
        pImpl_->lastError = e.what();
        return {};
    }
}

void BuildWhere(const DataQuery& q, std::string& sql, std::vector<std::string>& outBinds)
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

    if (clauses.empty())
        return;

    sql += " WHERE ";
    for (size_t i = 0; i < clauses.size(); ++i) {
        if (i > 0)
            sql += " AND ";
        sql += clauses[i];
    }
}

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
        std::string              sql = "SELECT * FROM " + q.table;
        BuildWhere(q, sql, binds);

        if (!q.sortColumn.empty())
            sql += " ORDER BY " + q.sortColumn + (q.sortAscending ? " ASC" : " DESC");

        sql += " LIMIT ? OFFSET ?";

        SQLite::Statement stmt(*pImpl_->db, sql);

        int idx = 1;
        for (const auto& v : binds)
            stmt.bind(idx++, v);
        stmt.bind(idx++, q.pageSize);
        stmt.bind(idx, q.page * q.pageSize);

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

        const auto t1      = std::chrono::steady_clock::now();
        result.executionMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

    } catch (const SQLite::Exception& e) {
        pImpl_->lastError = e.what();
        result.error      = e.what();
    }

    return result;
}

int SQLiteAdapter::CountQuery(const DataQuery& q) const
{
    if (!IsConnected())
        return 0;

    try {
        std::vector<std::string> binds;
        std::string              sql = "SELECT COUNT(*) FROM " + q.table;
        BuildWhere(q, sql, binds);

        SQLite::Statement stmt(*pImpl_->db, sql);

        int idx = 1;
        for (const auto& v : binds)
            stmt.bind(idx++, v);

        if (stmt.executeStep())
            return stmt.getColumn(0).getInt();

        return 0;
    } catch (const SQLite::Exception& e) {
        pImpl_->lastError = e.what();
        return 0;
    }
}

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
        const int         colCount = stmt.getColumnCount();

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

        const auto t1      = std::chrono::steady_clock::now();
        result.executionMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

    } catch (const SQLite::Exception& e) {
        pImpl_->lastError = e.what();
        result.error      = e.what();
    }

    return result;
}

std::expected<void, Error>
SQLiteAdapter::UpdateRow(const std::string&                                  table,
                         const std::unordered_map<std::string, std::string>& pkValues,
                         const std::unordered_map<std::string, std::string>& newValues)
{
    if (newValues.empty())
        return {};

    return require_writable()
        .and_then([&] { return require(!pkValues.empty(), Error{"No primary key supplied"}); })
        .and_then([&]() -> std::expected<void, Error> {
            std::string              sql = "UPDATE " + table + " SET ";
            std::vector<std::string> binds;
            binds.reserve(newValues.size() + pkValues.size());

            bool first = true;
            for (const auto& [col, val] : newValues) {
                if (!first)
                    sql += ", ";
                sql += col + " = ?";
                binds.push_back(val);
                first = false;
            }

            sql += " WHERE ";
            first = true;
            for (const auto& [col, val] : pkValues) {
                if (!first)
                    sql += " AND ";
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
        });
}

std::expected<void, Error>
SQLiteAdapter::InsertRow(const std::string& table, const std::unordered_map<std::string, std::string>& values)
{
    return require_writable()
        .and_then([&] { return require(!values.empty(), Error{"No column values supplied"}); })
        .and_then([&]() -> std::expected<void, Error> {
            std::string              cols, placeholders;
            std::vector<std::string> binds;
            binds.reserve(values.size());
            bool first = true;
            for (const auto& [col, val] : values) {
                if (!first) {
                    cols += ", ";
                    placeholders += ", ";
                }
                cols += col;
                placeholders += "?";
                binds.push_back(val);
                first = false;
            }
            const std::string sql = "INSERT INTO " + table + " (" + cols + ") VALUES (" + placeholders + ")";

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
        });
}

std::expected<void, Error>
SQLiteAdapter::DeleteRow(const std::string& table, const std::unordered_map<std::string, std::string>& pkValues)
{
    return require_writable()
        .and_then([&] { return require(!pkValues.empty(), Error{"No primary key values supplied"}); })
        .and_then([&]() -> std::expected<void, Error> {
            std::string              sql = "DELETE FROM " + table + " WHERE ";
            std::vector<std::string> binds;
            binds.reserve(pkValues.size());
            bool first = true;
            for (const auto& [col, val] : pkValues) {
                if (!first)
                    sql += " AND ";
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
        });
}

} // namespace datagrid::adapters
