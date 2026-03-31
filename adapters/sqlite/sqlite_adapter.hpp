#pragma once
#include "../data_source.hpp"

#include <memory>
#include <string>

namespace Adapters {

class SQLiteAdapter final : public IDataSource
{
  public:
    SQLiteAdapter();
    ~SQLiteAdapter() override;

    // User-declared destructor suppresses implicit move generation.
    // Defined = default in the .cpp where Impl is complete so that
    // unique_ptr<Impl> has a complete type to destroy / move.
    SQLiteAdapter(SQLiteAdapter&&) noexcept;
    SQLiteAdapter& operator=(SQLiteAdapter&&) noexcept;

    [[nodiscard]] std::string AdapterName() const override { return "sqlite"; }
    [[nodiscard]] std::string AdapterVersion() const override { return "1.0.0"; }
    [[nodiscard]] std::string AdapterLabel() const override;

    std::expected<void, Error> Connect(const ConnectionParams& params) override;
    void                       Disconnect() override;
    [[nodiscard]] bool         IsConnected() const override;
    [[nodiscard]] std::string  LastError() const override;

    [[nodiscard]] std::vector<std::string> GetCatalogs() const override;
    [[nodiscard]] std::vector<TableInfo>   GetTables(const std::string& catalog) const override;
    [[nodiscard]] std::vector<ColumnInfo>  GetColumns(const std::string& table) const override;

    [[nodiscard]] QueryResult ExecuteQuery(const DataQuery& q) const override;
    [[nodiscard]] int         CountQuery(const DataQuery& q) const override;

    [[nodiscard]] QueryResult Execute(const std::string& sql) const override;

    std::expected<void, Error> UpdateRow(const std::string&                                  table,
                                         const std::unordered_map<std::string, std::string>& pkValues,
                                         const std::unordered_map<std::string, std::string>& newValues) override;

    std::expected<void, Error> InsertRow(const std::string&                                  table,
                                         const std::unordered_map<std::string, std::string>& values) override;

    std::expected<void, Error> DeleteRow(const std::string&                                  table,
                                         const std::unordered_map<std::string, std::string>& pkValues) override;

  private:
    struct Impl;
    std::unique_ptr<Impl> pImpl_;
};

} // namespace Adapters
