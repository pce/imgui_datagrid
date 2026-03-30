#pragma once
/// SQLiteAdapter — IDataSource implementation for SQLite databases.
/// Uses SQLiteCpp (MIT) for safe RAII access.
///
/// CONNECTION STRING
///   p.adapterName      = "sqlite";
///   p.connectionString = "/path/to/data.db";
///   p.readOnly         = true;   // optional, default false
///
/// Self-registers via REGISTER_ADAPTER in sqlite_adapter.cpp —
/// no changes to main() are required.

#include "../data_source.hpp"

#include <memory>
#include <string>

namespace Adapters {

class SQLiteAdapter final : public IDataSource {
public:
    SQLiteAdapter();
    ~SQLiteAdapter() override;

    // User-declared destructor suppresses implicit move generation.
    // Defined = default in the .cpp where Impl is complete so that
    // unique_ptr<Impl> has a complete type to destroy / move.
    SQLiteAdapter(SQLiteAdapter&&)            noexcept;
    SQLiteAdapter& operator=(SQLiteAdapter&&) noexcept;

    // ── Adapter identity ──────────────────────────────────────────────────
    [[nodiscard]] std::string AdapterName()    const override { return "sqlite"; }
    [[nodiscard]] std::string AdapterVersion() const override { return "1.0.0"; }
    [[nodiscard]] std::string AdapterLabel()   const override;

    // ── Connection lifecycle ──────────────────────────────────────────────
    std::expected<void, Error> Connect(const ConnectionParams& params) override;
    void        Disconnect()                            override;
    [[nodiscard]] bool        IsConnected()             const override;
    [[nodiscard]] std::string LastError()               const override;

    // ── Schema navigation ─────────────────────────────────────────────────
    [[nodiscard]] std::vector<std::string> GetCatalogs()                         const override;
    [[nodiscard]] std::vector<TableInfo>   GetTables(const std::string& catalog) const override;
    [[nodiscard]] std::vector<ColumnInfo>  GetColumns(const std::string& table)  const override;

    // ── Structured queries ────────────────────────────────────────────────
    [[nodiscard]] QueryResult ExecuteQuery(const DataQuery& q) const override;
    [[nodiscard]] int         CountQuery  (const DataQuery& q) const override;

    // ── Raw SQL ───────────────────────────────────────────────────────────
    [[nodiscard]] QueryResult Execute(const std::string& sql) const override;

    // ── Write support ─────────────────────────────────────────────────────
    [[nodiscard]] bool SupportsWrite() const override;

    std::expected<void, Error> UpdateRow(
        const std::string&                                   table,
        const std::unordered_map<std::string, std::string>& pkValues,
        const std::unordered_map<std::string, std::string>& newValues) override;

    std::expected<void, Error> InsertRow(
        const std::string&                                   table,
        const std::unordered_map<std::string, std::string>& values) override;

    std::expected<void, Error> DeleteRow(
        const std::string&                                   table,
        const std::unordered_map<std::string, std::string>& pkValues) override;

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl_;
};

} // namespace Adapters
