#pragma once
#include "../data_source.hpp"
#include "../utils/udf_provider.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <string_view>

// Forward-declare DuckDB types so the public header stays free of duckdb.hpp.
// The full definition lives behind the pImpl wall in the .cpp.
namespace duckdb {
class DataChunk;
class ExpressionState;
class Vector;
using scalar_function_t = std::function<void(DataChunk&, ExpressionState&, Vector&)>;
} // namespace duckdb

namespace datagrid::adapters {

class DuckDBAdapter final : public IDataSource, public IUDFProvider
{
  public:
    DuckDBAdapter();
    ~DuckDBAdapter() override;

    // User-declared destructor suppresses implicit move generation.
    // Defined = default in the .cpp where Impl is complete.
    DuckDBAdapter(DuckDBAdapter&&) noexcept;
    DuckDBAdapter& operator=(DuckDBAdapter&&) noexcept;

    [[nodiscard]] std::string AdapterName() const override { return "duckdb"; }
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

    /// Always true for DuckDB; the adapter can register UDFs as long as
    /// a connection is open.
    [[nodiscard]] bool SupportsUDF() const noexcept override { return true; }

    /// Register a user-defined scalar function that handles DuckDB's
    /// DataChunk / UnifiedVectorFormat directly — identical to calling
    ///
    ///   conn.CreateVectorizedFunction<R, Args...>(name, fn);
    ///
    /// Use this when you need full control over vectorisation, null handling,
    /// or want to replicate the pattern from the DuckDB docs:
    ///
    ///   template<typename T>
    ///   void udf_copy(DataChunk& args, ExpressionState&, Vector& result) {
    ///       result.SetVectorType(VectorType::FLAT_VECTOR);
    ///       UnifiedVectorFormat vf;
    ///       args.data[0].ToUnifiedFormat(args.size(), vf);
    ///       auto* out = FlatVector::GetData<T>(result);
    ///       for (idx_t i = 0; i < args.size(); ++i) {
    ///           const auto idx = vf.sel->get_index(i);
    ///           if (!vf.validity.RowIsValid(idx)) continue;
    ///           out[i] = reinterpret_cast<const T*>(vf.data)[idx];
    ///       }
    ///   }
    ///
    ///   // Registration:
    ///   adapter.RegisterVectorized<int32_t, int32_t>(
    ///       "udf_copy_int", &udf_copy<int32_t>);
    ///
    /// For simple lambdas prefer the portable RegisterScalar<R, Args...>
    /// from IUDFProvider — it builds the trampoline automatically.
    template<typename R, typename... Args>
    [[nodiscard]] std::expected<void, Error> RegisterVectorized(std::string_view name, duckdb::scalar_function_t fn)
    {
        return RegisterVectorizedImpl(name, std::move(fn), {ScalarTypeOf<Args>...}, ScalarTypeOf<R>);
    }

    /// Register a CSV / TSV / Parquet / JSON file as a named DuckDB VIEW.
    /// If `viewName` is empty the file stem is sanitised and used as the name
    /// (e.g. "sales_data.csv" → view "sales_data").
    /// After registration the view appears automatically in GetTables() /
    /// the DataBrowser sidebar.
    std::expected<void, Error> ScanFile(const std::string& filePath, const std::string& viewName = "");

    /// Scan all queryable files in `dirPath` and register each as a VIEW.
    /// Supported extensions: .csv .tsv .parquet .json .ndjson .jsonl .txt
    /// Existing views of the same name are replaced (CREATE OR REPLACE VIEW).
    std::expected<void, Error> ScanDirectory(const std::string& dirPath);

    /// Return the list of view names that were registered via ScanFile /
    /// ScanDirectory (not regular DuckDB tables).
    [[nodiscard]] std::vector<std::string> GetFileSources() const;

    [[nodiscard]] static bool IsQueryableExtension(std::string_view ext) noexcept;

  protected:
    [[nodiscard]] std::expected<void, Error> RegisterScalarImpl(ScalarUDFDesc desc) override;

  private:
    // Keeps duckdb.hpp out of the public header; the template above
    // forwards here after capturing the compile-time type information as
    // runtime ScalarType lists.
    [[nodiscard]] std::expected<void, Error> RegisterVectorizedImpl(std::string_view          name,
                                                                    duckdb::scalar_function_t fn,
                                                                    std::vector<ScalarType>   argTypes,
                                                                    ScalarType                returnType);

    struct Impl;
    std::unique_ptr<Impl> pImpl_;
};

} // namespace datagrid::adapters
