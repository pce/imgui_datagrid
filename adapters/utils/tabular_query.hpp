#pragma once
#include "../data_source.hpp"

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace datagrid::adapters {

enum class ColType
{
    Text,
    Numeric
};

enum class PredOp
{
    Eq,
    Ne,
    Lt,
    Gt,
    Le,
    Ge,      ///< =  !=/<>  <  >  <=  >=
    Like,    ///< SQL LIKE  (case-sensitive, % _ wildcards)
    ILike,   ///< SQL ILIKE (case-insensitive)
    Glob,    ///< Glob      (* ? wildcards, case-insensitive)
    Between, ///< col BETWEEN lo AND hi
    In,      ///< col IN (...)
};

struct AtomicPred
{
    std::string              colName;
    PredOp                   op = PredOp::Eq;
    std::string              strVal;    ///< string operand
    int64_t                  numLo = 0; ///< numeric lo (or single value for Eq/Ne/…)
    int64_t                  numHi = 0; ///< numeric hi (BETWEEN upper bound)
    std::vector<std::string> inVals;    ///< IN (...)
};

struct PredNode
{
    enum class Kind
    {
        Atom,
        And,
        Or,
        Not
    } kind = Kind::Atom;
    AtomicPred            atom;
    std::vector<PredNode> children; ///< And/Or: 2+; Not: exactly 1
};

struct QueryPlan
{
    std::vector<std::string> selectCols;        ///< empty = SELECT *
    std::string              fromSource;        ///< path (FS) or table name (CSV/SQLite)
    bool                     recursive = false; ///< RECURSIVE keyword (filesystem)
    bool                     hasPred   = false;
    PredNode                 pred;              ///< valid when hasPred == true
    std::string              orderByCol;
    bool                     orderAsc  = true;
    int                      limitVal  = -1;
    int                      offsetVal = 0;
};

/// Columnar in-memory representation.
///
/// Hot numeric columns are stored in `numCols` as int64 arrays — a tight
/// stride-1 loop over them auto-vectorises (AVX-512/AVX2/NEON) and enables
/// explicit SIMD passes.  String columns in `strCols` are used for text preds.
///
/// Column order in strCols / numCols mirrors colNames exactly.
/// For Numeric columns strCols[i] still holds the formatted string (for SELECT
/// projection); numCols[i] holds the raw int64 for filter passes.
/// For Text columns numCols[i] is empty.
struct TabularSoA
{
    size_t                                rowCount = 0;
    std::vector<std::string>              colNames;
    std::vector<ColType>                  colTypes;
    std::vector<std::vector<std::string>> strCols; ///< [colIdx][rowIdx]
    std::vector<std::vector<int64_t>>     numCols; ///< [colIdx][rowIdx], Numeric only

    /// Build from AoS rows.
    /// colTypes.size() must equal colNames.size().
    /// For Numeric columns, cell strings are parsed via stoll (non-parseable → 0).
    static TabularSoA from_rows(const std::vector<std::string>&              colNames,
                                const std::vector<ColType>&                  colTypes,
                                const std::vector<std::vector<std::string>>& rows);

    /// Column index by name (case-insensitive).  Returns SIZE_MAX if not found.
    [[nodiscard]] size_t col_index(std::string_view name) const;
};

class TabularQuery
{
  public:
    /// Parse a SQL SELECT statement into a QueryPlan.
    /// `plan.fromSource` receives the raw FROM value — caller interprets it.
    [[nodiscard]] static std::expected<QueryPlan, std::string> parse(std::string_view sql);

    /// Return a per-row bitmask: 1 = survives predicates, 0 = filtered out.
    /// Used by FluentQuery::entries() to avoid going through string QueryResult.
    [[nodiscard]] static std::vector<uint8_t> filter_mask(const QueryPlan& plan, const TabularSoA& soa);

    /// Full pipeline: filter → sort → offset/limit → project columns.
    [[nodiscard]] static QueryResult
    execute(const QueryPlan& plan, const TabularSoA& soa, const std::vector<ColumnInfo>& schema);

  private:
    static void applyNode(const PredNode& node, const TabularSoA& soa, std::vector<uint8_t>& mask);
    static void applyAtom(const AtomicPred& a, const TabularSoA& soa, std::vector<uint8_t>& mask);

    // Inner loops kept separate so the compiler can auto-vectorise them.
    // Numeric path: tight int64 stride-1 loop (SIMD-friendly).
    static void
    applyNumericOp(PredOp op, int64_t lo, int64_t hi, const std::vector<int64_t>& col, std::vector<uint8_t>& mask);
    // Text path: LIKE / ILIKE / GLOB / equality on string columns.
    static void
    applyTextOp(PredOp op, const std::string& val, const std::vector<std::string>& col, std::vector<uint8_t>& mask);
    static void
    applyInOp(const std::vector<std::string>& vals, const std::vector<std::string>& col, std::vector<uint8_t>& mask);
};

} // namespace datagrid::adapters
