#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <expected>
#include <vector>

namespace Adapters {

using Error = std::string;

struct ColumnInfo {
    std::string name;
    std::string typeName;
    bool        nullable   = true;
    bool        primaryKey = false;
};

struct TableInfo {
    std::string catalog;
    std::string schema;
    std::string name;
    std::string kind;
};

struct DataQuery {
    std::string table;

    std::unordered_map<std::string, std::string> whereExact;

    std::string searchColumn;
    std::string searchValue;

    std::string sortColumn;
    bool        sortAscending = true;

    int page     = 0;
    int pageSize = 20;
};

struct QueryResult {
    std::vector<ColumnInfo>               columns;
    std::vector<std::vector<std::string>> rows;
    std::string                           error;
    std::int64_t                          rowsAffected = 0;
    double                                executionMs  = 0.0;

    [[nodiscard]] bool ok()    const { return error.empty(); }
    [[nodiscard]] bool empty() const { return rows.empty(); }
};

struct ConnectionParams {
    std::string adapterName;
    std::string connectionString;

    std::optional<std::string> username;
    std::optional<std::string> password;
    std::optional<std::string> database;
    std::optional<int>         port;
    std::optional<int>         timeoutSeconds;
    std::optional<char>        csvSeparator;
    std::optional<bool>        readOnly;
};

class IDataSource {
public:
    virtual ~IDataSource() = default;

    IDataSource(const IDataSource&)            = delete;
    IDataSource& operator=(const IDataSource&) = delete;

    IDataSource(IDataSource&&)            = default;
    IDataSource& operator=(IDataSource&&) = default;

    [[nodiscard]] virtual std::string AdapterName()    const = 0;
    [[nodiscard]] virtual std::string AdapterVersion() const = 0;
    [[nodiscard]] virtual std::string AdapterLabel()   const = 0;

    virtual std::expected<void, Error> Connect(const ConnectionParams& params) = 0;
    virtual void Disconnect()                            = 0;

    [[nodiscard]] virtual bool        IsConnected() const = 0;
    [[nodiscard]] virtual std::string LastError()   const = 0;

    [[nodiscard]] virtual std::vector<std::string> GetCatalogs()                         const = 0;

    [[nodiscard]] virtual std::vector<TableInfo>   GetTables(const std::string& catalog) const = 0;

    [[nodiscard]] virtual std::vector<ColumnInfo>  GetColumns(const std::string& table)  const = 0;

    [[nodiscard]] virtual QueryResult ExecuteQuery(const DataQuery& q) const = 0;

    [[nodiscard]] virtual int         CountQuery  (const DataQuery& q) const = 0;

    [[nodiscard]] virtual QueryResult Execute(const std::string& sql) const = 0;

    /// Returns true when the adapter supports mutating data (INSERT/UPDATE/DELETE).
    /// Default: false (read-only).
    [[nodiscard]] virtual bool SupportsWrite() const { return false; }

    /// Update a single row identified by `pkValues`.
    /// `newValues` maps column-name → new string value.
    /// Default: returns an error (read-only adapter).
    virtual std::expected<void, Error> UpdateRow(
        const std::string&                                   table,
        const std::unordered_map<std::string, std::string>& pkValues,
        const std::unordered_map<std::string, std::string>& newValues)
    {
        (void)table; (void)pkValues; (void)newValues;
        return std::unexpected(Error{"Adapter is read-only"});
    }

    /// Insert a new row.  `values` maps column-name → string value.
    /// Omit auto-increment / generated columns — the DB assigns them.
    virtual std::expected<void, Error> InsertRow(
        const std::string&                                   table,
        const std::unordered_map<std::string, std::string>& values)
    {
        (void)table; (void)values;
        return std::unexpected(Error{"Adapter is read-only"});
    }

    /// Delete the row identified by `pkValues`.
    /// `pkValues` maps PK column-name → string value.
    virtual std::expected<void, Error> DeleteRow(
        const std::string&                                   table,
        const std::unordered_map<std::string, std::string>& pkValues)
    {
        (void)table; (void)pkValues;
        return std::unexpected(Error{"Adapter is read-only"});
    }

protected:
    IDataSource() = default;
};

using DataSourcePtr = std::unique_ptr<IDataSource>;

}
