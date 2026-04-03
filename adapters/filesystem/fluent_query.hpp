#pragma once
#include "../utils/tabular_query.hpp"
#include "filesystem_adapter.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace datagrid::adapters {

inline namespace literals {
constexpr std::uintmax_t operator""_B(unsigned long long n)
{
    return n;
}
constexpr std::uintmax_t operator""_KB(unsigned long long n)
{
    return n * 1024ULL;
}
constexpr std::uintmax_t operator""_MB(unsigned long long n)
{
    return n * 1024ULL * 1024;
}
constexpr std::uintmax_t operator""_GB(unsigned long long n)
{
    return n * 1024ULL * 1024 * 1024;
}
} // namespace literals

/// Wraps std::function<bool(FilesystemEntry)> and adds &&, ||, ! operators
/// so predicates compose naturally without losing the callable syntax.
struct EntryPred
{
    using Fn = std::function<bool(const FilesystemEntry&)>;
    Fn fn;

    EntryPred() = default;
    /* implicit */ EntryPred(Fn f) : fn(std::move(f)) {} // NOLINT(google-explicit-constructor)

    bool operator()(const FilesystemEntry& e) const { return fn && fn(e); }

    [[nodiscard]] EntryPred operator&&(const EntryPred& rhs) const
    {
        Fn a = fn, b = rhs.fn;
        return EntryPred{[a, b](const FilesystemEntry& e) { return a(e) && b(e); }};
    }
    [[nodiscard]] EntryPred operator||(const EntryPred& rhs) const
    {
        Fn a = fn, b = rhs.fn;
        return EntryPred{[a, b](const FilesystemEntry& e) { return a(e) || b(e); }};
    }
    [[nodiscard]] EntryPred operator!() const
    {
        Fn a = fn;
        return EntryPred{[a](const FilesystemEntry& e) { return !a(e); }};
    }
    [[nodiscard]] explicit operator bool() const { return fn != nullptr; }
};

/// Match by file kind ("file", "dir", "symlink", "other")
[[nodiscard]] EntryPred kind_is(std::string_view k);

[[nodiscard]] EntryPred is_file();
[[nodiscard]] EntryPred is_dir();
[[nodiscard]] EntryPred is_symlink();

/// Case-insensitive exact extension match (include the dot: ".cpp")
[[nodiscard]] EntryPred ext_is(std::string_view ext);
[[nodiscard]] EntryPred ext_in(std::vector<std::string> exts);

/// Exact filename match (case-sensitive)
[[nodiscard]] EntryPred name_eq(std::string_view name);
/// Case-insensitive substring match
[[nodiscard]] EntryPred name_contains(std::string_view sub);
/// SQL LIKE pattern: % = any substring, _ = one char.
/// Case-SENSITIVE — matches standard SQL behaviour.
/// Use name_ilike() for case-insensitive filename matching.
[[nodiscard]] EntryPred name_like(std::string_view pattern);
/// SQL ILIKE pattern: same as LIKE but case-insensitive (PostgreSQL extension).
/// Preferred for filesystem work where filenames are often mixed-case.
[[nodiscard]] EntryPred name_ilike(std::string_view pattern);
/// Glob pattern: * = any substring, ? = one char (always case-insensitive).
[[nodiscard]] EntryPred name_glob(std::string_view pattern);

/// Size comparisons (in bytes)
[[nodiscard]] EntryPred size_eq(std::uintmax_t n);
[[nodiscard]] EntryPred size_gt(std::uintmax_t n);
[[nodiscard]] EntryPred size_lt(std::uintmax_t n);
[[nodiscard]] EntryPred size_ge(std::uintmax_t n);
[[nodiscard]] EntryPred size_le(std::uintmax_t n);
[[nodiscard]] EntryPred size_between(std::uintmax_t lo, std::uintmax_t hi);

class FluentQuery
{
  public:
    explicit FluentQuery(fs::path dir);

    /// Recurse into subdirectories.  maxDepth = -1 means unlimited.
    FluentQuery& recursive(bool on = true, int maxDepth = -1);

    /// Add a predicate.  Multiple calls are ANDed together.
    FluentQuery& where(EntryPred pred);
    /// Show hidden entries (names starting with '.').  Default: false.
    FluentQuery& show_hidden(bool show = true);
    /// When sorting by name or kind, list directories before files.
    /// Default: true (matches the convention of most file managers).
    /// Pass false for pure SQL-style ordering with no special dir treatment.
    FluentQuery& dirs_first(bool on = true);

    /// Choose which columns to include in execute()'s QueryResult.
    /// Recognises: name, kind, size, modified, permissions, path, extension, stem.
    /// Default (empty list) = all standard six columns.
    FluentQuery& select(std::vector<std::string> columns);

    /// column: name | kind | size | modified.  Default ascending.
    FluentQuery& order_by(std::string_view column, bool ascending = true);

    FluentQuery& limit(int n);
    FluentQuery& offset(int n);

    /// Inject a VFS access-check predicate.
    ///
    /// When set, `entries()` calls `fn(dir_)` before iterating — returning an
    /// empty result when access is denied.  This decouples FluentQuery from any
    /// concrete VFS type: callers pass a lambda that wraps their Rfs, mock, or
    /// in-memory backend:
    ///
    ///   query.with_access([&rfs](const fs::path& p) {
    ///       auto r = rfs.resolve(p);
    ///       return r && r->readable();
    ///   });
    ///
    /// Passing nullptr (the default) disables the check.
    FluentQuery& with_access(std::function<bool(const fs::path&)> fn);

    /// Run the query and return a DataBrowser-compatible QueryResult.
    [[nodiscard]] QueryResult execute() const;
    /// Count matching entries without building row data.
    [[nodiscard]] int count() const;
    /// Return the raw filtered+sorted FilesystemEntry list (before projection).
    [[nodiscard]] std::vector<FilesystemEntry> entries() const;

    [[nodiscard]] static FluentQuery from(fs::path dir);
    /// Parse a minimal SQL string into a FluentQuery.
    /// Returns std::unexpected with a descriptive error on parse failure.
    [[nodiscard]] static std::expected<FluentQuery, std::string> from_sql(std::string_view sql);

  private:
    fs::path                 dir_;
    bool                     recursive_ = false;
    int                      maxDepth_  = -1;
    std::vector<EntryPred>   predicates_; ///< fluent-API predicates (scalar path)
    std::string              sortColumn_    = "name";
    bool                     sortAscending_ = true;
    std::vector<std::string> selectedCols_; ///< empty = all columns
    int                      limit_      = -1;
    int                      offset_     = 0;
    bool                     showHidden_ = false;
    bool                     dirsFirst_  = true;

    /// SQL path: set by from_sql(), triggers SoA bitmask filter in entries().
    bool        hasSqlPlan_ = false;
    QueryPlan   sqlPlan_;

    /// Optional VFS gate — set by with_access().  Null = no check (default).
    std::function<bool(const fs::path&)> accessCheck_;
};

} // namespace datagrid::adapters
