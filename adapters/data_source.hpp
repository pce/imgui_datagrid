#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Adapters {

using Error = std::string;

struct ColumnInfo
{
    std::string name;
    std::string typeName;
    bool        nullable   = true;
    bool        primaryKey = false;

    /// Optional display hint propagated to the UI layer.
    /// Built-in values:
    ///   ""             — default text rendering
    ///   "image_path"   — cell value is a file-system path to an image
    ///   "image_blob"   — cell value contains raw image bytes (e.g. SQLite BLOB)
    /// Adapters set this in GetColumns(); the widget layer reads it via
    /// ColumnDef::semanticHint after BuildColumns() copies it across.
    std::string displayHint;
};

struct TableInfo
{
    std::string catalog;
    std::string schema;
    std::string name;
    std::string kind;
};

struct DataQuery
{
    std::string table;

    std::unordered_map<std::string, std::string> whereExact;

    std::string searchColumn;
    std::string searchValue;

    std::string sortColumn;
    bool        sortAscending = true;

    int page     = 0;
    int pageSize = 20;
};

struct QueryResult
{
    std::vector<ColumnInfo>               columns;
    std::vector<std::vector<std::string>> rows;
    std::string                           error;
    std::int64_t                          rowsAffected = 0;
    double                                executionMs  = 0.0;

    [[nodiscard]] bool ok() const { return error.empty(); }
    [[nodiscard]] bool empty() const { return rows.empty(); }
};

struct ConnectionParams
{
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

class IDataSource
{
  public:
    virtual ~IDataSource() = default;

    IDataSource(const IDataSource&)            = delete;
    IDataSource& operator=(const IDataSource&) = delete;

    IDataSource(IDataSource&&)            = default;
    IDataSource& operator=(IDataSource&&) = default;

    [[nodiscard]] virtual std::string AdapterName() const    = 0;
    [[nodiscard]] virtual std::string AdapterVersion() const = 0;
    [[nodiscard]] virtual std::string AdapterLabel() const   = 0;

    virtual std::expected<void, Error> Connect(const ConnectionParams& params) = 0;
    virtual void                       Disconnect()                            = 0;

    [[nodiscard]] virtual bool        IsConnected() const = 0;
    [[nodiscard]] virtual std::string LastError() const   = 0;

    [[nodiscard]] virtual std::vector<std::string> GetCatalogs() const = 0;

    [[nodiscard]] virtual std::vector<TableInfo> GetTables(const std::string& catalog) const = 0;

    [[nodiscard]] virtual std::vector<ColumnInfo> GetColumns(const std::string& table) const = 0;

    [[nodiscard]] virtual QueryResult ExecuteQuery(const DataQuery& q) const = 0;

    [[nodiscard]] virtual int CountQuery(const DataQuery& q) const = 0;

    [[nodiscard]] virtual QueryResult Execute(const std::string& sql) const = 0;

    /// Returns true when the adapter is connected AND the connection is
    /// not marked read-only.  Override to add adapter-specific conditions
    /// (e.g. insufficient privileges).  The UI uses this to hide edit
    /// controls; require_writable() uses it to guard write operations.
    [[nodiscard]] virtual bool SupportsWrite() const { return IsConnected() && !readOnly_; }

    /// Update a single row identified by `pkValues`.
    /// `newValues` maps column-name → new string value.
    /// Default: returns an error (read-only adapter).
    virtual std::expected<void, Error> UpdateRow(const std::string&                                  table,
                                                 const std::unordered_map<std::string, std::string>& pkValues,
                                                 const std::unordered_map<std::string, std::string>& newValues)
    {
        (void)table;
        (void)pkValues;
        (void)newValues;
        return std::unexpected(Error{"Adapter is read-only"});
    }

    /// Insert a new row.  `values` maps column-name → string value.
    /// Omit auto-increment / generated columns — the DB assigns them.
    virtual std::expected<void, Error> InsertRow(const std::string&                                  table,
                                                 const std::unordered_map<std::string, std::string>& values)
    {
        (void)table;
        (void)values;
        return std::unexpected(Error{"Adapter is read-only"});
    }

    /// Delete the row identified by `pkValues`.
    /// `pkValues` maps PK column-name → string value.
    virtual std::expected<void, Error> DeleteRow(const std::string&                                  table,
                                                 const std::unordered_map<std::string, std::string>& pkValues)
    {
        (void)table;
        (void)pkValues;
        return std::unexpected(Error{"Adapter is read-only"});
    }

  protected:
    IDataSource() = default;

    /// Set by Connect() implementations from ConnectionParams::readOnly.
    /// Drives SupportsWrite() and require_writable().
    bool readOnly_ = false;

    /// Write-operation guard for .and_then() chains.
    ///
    /// Usage in any adapter write method:
    ///
    ///   return require_writable()
    ///       .and_then([&]{ return require(!pkValues.empty(), Error{"No PK"}); })
    ///       .and_then([&]() -> std::expected<void, Error> { /* write */ });
    ///
    /// Returns std::unexpected("Not connected")     when !IsConnected().
    /// Returns std::unexpected("Connection is read-only") when readOnly_.
    /// Returns {} (success) otherwise — letting .and_then() proceed.
    [[nodiscard]] std::expected<void, Error> require_writable() const
    {
        if (!IsConnected())
            return std::unexpected(Error{"Not connected"});
        if (!SupportsWrite())
            return std::unexpected(Error{"Connection is read-only"});
        return {};
    }
};

using DataSourcePtr = std::unique_ptr<IDataSource>;

} // namespace Adapters
