#pragma once
#include "../data_source.hpp"
#include "../query/tabular_query.hpp"

#include <string>
#include <vector>

namespace Adapters {

class CsvAdapter final : public IDataSource
{
  public:
    CsvAdapter();
    ~CsvAdapter() override = default;

    [[nodiscard]] std::string AdapterName() const override { return "csv"; }
    [[nodiscard]] std::string AdapterVersion() const override { return "1.0.0"; }
    [[nodiscard]] std::string AdapterLabel() const override;

    /// connectionString = path to the delimited file.
    /// csvSeparator     = field delimiter (default ',').
    std::expected<void, Error> Connect(const ConnectionParams& params) override;
    void                       Disconnect() override;
    [[nodiscard]] bool         IsConnected() const override;
    [[nodiscard]] std::string  LastError() const override;

    [[nodiscard]] std::vector<std::string> GetCatalogs() const override;
    [[nodiscard]] std::vector<TableInfo>   GetTables(const std::string& catalog) const override;
    [[nodiscard]] std::vector<ColumnInfo>  GetColumns(const std::string& table) const override;

    [[nodiscard]] QueryResult ExecuteQuery(const DataQuery& q) const override;
    [[nodiscard]] int         CountQuery(const DataQuery& q) const override;

    /// Execute a SQL SELECT statement against the loaded CSV data.
    /// Supports: WHERE col op val, LIKE/ILIKE/GLOB, BETWEEN, IN,
    ///           ORDER BY, LIMIT, OFFSET.
    /// Numeric coercion is applied automatically for <, >, <= >=.
    [[nodiscard]] QueryResult Execute(const std::string& sql) const override;

  private:
    std::string                           filePath_;
    std::string                           tableName_; ///< Filename stem (e.g. "emails")
    char                                  separator_ = ',';
    std::vector<ColumnInfo>               header_;    ///< Column metadata from row 0
    std::vector<std::vector<std::string>> allRows_;   ///< Data rows (header excluded)
    std::string                           lastError_;
    bool                                  connected_ = false;

    /// Parse one delimited line into fields (RFC 4180 quoting).
    [[nodiscard]] static std::vector<std::string> ParseRow(const std::string& line, char sep);

    [[nodiscard]] static std::string StemOf(const std::string& path);

    [[nodiscard]] std::vector<std::vector<std::string>> ApplyFilters(const DataQuery& q) const;

    /// Build a TabularSoA from the loaded data; all columns default to Text.
    [[nodiscard]] Query::TabularSoA BuildSoA() const;
};

} // namespace Adapters
