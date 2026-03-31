#include "postgres_adapter.hpp"
#include "../adapter_registry.hpp"
#include "../utils/expected_utils.hpp"

#include <pqxx/pqxx>
#   include <chrono>
#include <sstream>
#include <string>
#include <vector>

namespace {
const Adapters::RegisterAdapter<Adapters::PostgresAdapter> kPostgresReg{"Postgres"};
}

namespace Adapters {

struct PostgresAdapter::Impl
{
    std::unique_ptr<pqxx::connection> conn;
    std::string                       lastError;
    std::string                       path;
    std::string                       lastError;
    bool                              readOnly = false;
};

PostgresAdapter::PostgresAdapter(PostgresAdapter&&) noexcept            = default;
PostgresAdapter& PostgresAdapter::operator=(PostgresAdapter&&) noexcept = default;

PostgresAdapter::PostgresAdapter() : pImpl_(std::make_unique<Impl>()) {}

PostgresAdapter::~PostgresAdapter()
{
    Disconnect();
}

std::expected<void, Error>
PostgresAdapter::Connect(const ConnectionParams& params)
{
    try {
        pImpl_->conn = std::make_unique<pqxx::connection>(params.connectionString);
        return {};
    } catch (const std::exception& e) {
        pImpl_->lastError = e.what();
        return std::unexpected(Error{e.what()});
    }
}

void PostgresAdapter::Disconnect()
{
    pImpl_->conn.reset();
}

bool PostgresAdapter::IsConnected() const
{
    return pImpl_->conn != nullptr;
}

std::string PostgresAdapter::LastError() const
{
    return pImpl_->lastError;
}

std::string PostgresAdapter::AdapterLabel() const
{
    if (!IsConnected())
        return "Postgres (disconnected)";
    try {
        Postgres::Statement stmt(*pImpl_->conn, "SELECT Postgres_version()");
        if (stmt.executeStep())
            return "Postgres " + std::string(stmt.getColumn(0).getText());
    } catch (...) {
    }
    return "Postgres";
}

std::vector<std::string> PostgresAdapter::GetCatalogs() const
{
    if (!IsConnected())
        return {};
    return {pImpl_->path};
}

std::vector<TableInfo> PostgresAdapter::GetTables(const std::string& /*catalog*/) const
{
    if (!IsConnected())
        return {};

    try {
        Postgres::Statement stmt(*pImpl_->conn,
                               "SELECT type, name FROM Postgres_master "
                               "WHERE type IN ('table','view') AND name NOT LIKE 'Postgres_%' "
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
    } catch (const Postgres::Exception& e) {
        pImpl_->lastError = e.what();
        return {};
    }
}

std::vector<ColumnInfo> PostgresAdapter::GetColumns(const std::string& table) const
{
    if (!IsConnected())
        return {};

    try {
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
    } catch (const Postgres::Exception& e) {
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

QueryResult PostgresAdapter::Execute(const std::string& sql) const
{
    QueryResult result;

    try {
        pqxx::work txn(*pImpl_->conn);
        pqxx::result r = txn.exec(sql);

        // columns
        for (const auto& field : r.columns()) {
            ColumnInfo ci;
            ci.name = field.name();
            result.columns.push_back(ci);
        }

        // rows
        for (const auto& row : r) {
            std::vector<std::string> out;
            for (const auto& field : row)
                out.push_back(field.is_null() ? "" : field.c_str());
            result.rows.push_back(std::move(out));
        }

        txn.commit();
    } catch (const std::exception& e) {
        result.error = e.what();
    }

    return result;
}

int PostgresAdapter::CountQuery(const DataQuery& q) const
{
    if (!IsConnected())
        return 0;

    try {
        std::vector<std::string> binds;
        std::string              sql = "SELECT COUNT(*) FROM " + q.table;
        BuildWhere(q, sql, binds);

        Postgres::Statement stmt(*pImpl_->conn, sql);

        int idx = 1;
        for (const auto& v : binds)
            stmt.bind(idx++, v);

        if (stmt.executeStep())
            return stmt.getColumn(0).getInt();

        return 0;
    } catch (const Postgres::Exception& e) {
        pImpl_->lastError = e.what();
        return 0;
    }
}

QueryResult PostgresAdapter::Execute(const std::string& sql) const
{
    QueryResult result;
    if (!IsConnected()) {
        result.error = "Not connected.";
        return result;
    }

    try {
        const auto t0 = std::chrono::steady_clock::now();

        Postgres::Statement stmt(*pImpl_->conn, sql);
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
                const Postgres::Column col = stmt.getColumn(c);
                row.emplace_back(col.isNull() ? "" : col.getText());
            }
            result.rows.push_back(std::move(row));
        }

        result.rowsAffected = pImpl_->conn->getChanges();

        const auto t1      = std::chrono::steady_clock::now();
        result.executionMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

    } catch (const Postgres::Exception& e) {
        pImpl_->lastError = e.what();
        result.error      = e.what();
    }

    return result;
}

std::expected<void, Adapters::Error>
PostgresAdapter::UpdateRow(const std::string&                                  table,
                         const std::unordered_map<std::string, std::string>& pkValues,
                         const std::unordered_map<std::string, std::string>& newValues)
{
    if (newValues.empty())
        return {};

    return require_writable()
        .and_then([&] { return Utils::require(!pkValues.empty(), Error{"No primary key supplied"}); })
        .and_then([&]() -> std::expected<void, Error> {
            std::string sql = "UPDATE " + table + " SET ";
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
                Postgres::Statement stmt(*pImpl_->conn, sql);
                for (int i = 0; i < static_cast<int>(binds.size()); ++i)
                    stmt.bind(i + 1, binds[i]);
                stmt.exec();
                return {};
            } catch (const Postgres::Exception& e) {
                pImpl_->lastError = e.what();
                return std::unexpected(Error{e.what()});
            }
        });
}

std::expected<void, Adapters::Error>
PostgresAdapter::InsertRow(const std::string& table, const std::unordered_map<std::string, std::string>& values)
{
    return require_writable()
        .and_then([&] { return Utils::require(!values.empty(), Error{"No column values supplied"}); })
        .and_then([&]() -> std::expected<void, Error> {
            std::string cols, placeholders;
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
                Postgres::Statement stmt(*pImpl_->conn, sql);
                for (int i = 0; i < static_cast<int>(binds.size()); ++i)
                    stmt.bind(i + 1, binds[i]);
                stmt.exec();
                return {};
            } catch (const Postgres::Exception& e) {
                pImpl_->lastError = e.what();
                return std::unexpected(Error{e.what()});
            }
        });
}

std::expected<void, Adapters::Error>
PostgresAdapter::DeleteRow(const std::string& table, const std::unordered_map<std::string, std::string>& pkValues)
{
    return require_writable()
        .and_then([&] { return Utils::require(!pkValues.empty(), Error{"No primary key values supplied"}); })
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
                Postgres::Statement stmt(*pImpl_->conn, sql);
                for (int i = 0; i < static_cast<int>(binds.size()); ++i)
                    stmt.bind(i + 1, binds[i]);
                stmt.exec();
                return {};
            } catch (const Postgres::Exception& e) {
                pImpl_->lastError = e.what();
                return std::unexpected(Error{e.what()});
            }
        });
}

} // namespace Adapters
