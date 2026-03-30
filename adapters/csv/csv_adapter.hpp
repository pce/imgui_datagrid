#pragma once
/// CsvAdapter — IDataSource implementation for delimited text files.
/// See docs/adapters.md for the full adapter authoring guide.
///
/// The entire file is loaded into memory on Connect() — suitable for
/// files up to a few hundred thousand rows.  Filtering, sorting and
/// pagination are performed in-memory.
///
/// CONNECTION STRING
///   p.adapterName      = "csv";
///   p.connectionString = "/data/report.csv";
///   p.csvSeparator     = ',';   // optional, default ','
///
/// The "table name" exposed to the sidebar is the filename stem
/// (e.g. "emails" for "emails.csv").  Execute(sql) is not supported;
/// use ExecuteQuery() for all data access.

#include "../data_source.hpp"

#include <string>
#include <vector>

namespace Adapters {

class CsvAdapter final : public IDataSource {
public:
    CsvAdapter();
    ~CsvAdapter() override = default;

    // ── Adapter identity ──────────────────────────────────────────────────
    [[nodiscard]] std::string AdapterName()    const override { return "csv"; }
    [[nodiscard]] std::string AdapterVersion() const override { return "1.0.0"; }
    [[nodiscard]] std::string AdapterLabel()   const override;

    // ── Connection lifecycle ──────────────────────────────────────────────
    /// connectionString = path to the delimited file.
    /// csvSeparator     = field delimiter (default ',').
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

    // ── Raw SQL (not supported) ───────────────────────────────────────────
    /// Always returns an error — CSV has no query engine.  Use ExecuteQuery().
    [[nodiscard]] QueryResult Execute(const std::string& sql) const override;

private:
    std::string             filePath_;
    std::string             tableName_;   ///< Filename stem (e.g. "emails")
    char                    separator_  = ',';
    std::vector<ColumnInfo> header_;      ///< Column metadata from row 0
    std::vector<std::vector<std::string>> allRows_; ///< Data rows (header excluded)
    std::string             lastError_;
    bool                    connected_  = false;

    // ── Helpers ───────────────────────────────────────────────────────────

    /// Parse one delimited line into fields (RFC 4180 quoting).
    [[nodiscard]] static std::vector<std::string> ParseRow(const std::string& line, char sep);

    /// Derive the table name from a file path (stem without extension).
    [[nodiscard]] static std::string StemOf(const std::string& path);

    /// Apply whereExact + searchColumn/searchValue filters to allRows_.
    [[nodiscard]] std::vector<std::vector<std::string>> ApplyFilters(const DataQuery& q) const;
};

} // namespace Adapters
