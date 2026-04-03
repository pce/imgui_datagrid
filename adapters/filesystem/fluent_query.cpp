#include "fluent_query.hpp"
#include "../platform_detect.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <format>
#include <ranges>
#include <string>

namespace Adapters::Fs {

namespace {
std::string FmtSize(std::uintmax_t bytes, bool isDir)
{
    if (isDir)
        return "—";
    constexpr std::uintmax_t KB = 1024, MB = KB * 1024, GB = MB * 1024, TB = GB * 1024;
    if (bytes < KB)
        return std::to_string(bytes) + " B";
    if (bytes < MB)
        return std::format("{:.1f} KB", static_cast<double>(bytes) / KB);
    if (bytes < GB)
        return std::format("{:.1f} MB", static_cast<double>(bytes) / MB);
    if (bytes < TB)
        return std::format("{:.1f} GB", static_cast<double>(bytes) / GB);
    return std::format("{:.1f} TB", static_cast<double>(bytes) / TB);
}

std::string FmtTime(const fs::file_time_type& t)
{
    if (t == fs::file_time_type{})
        return "—";
    using namespace std::chrono;
    const auto sysTp = time_point_cast<system_clock::duration>(file_clock::to_sys(t));
    const std::time_t tt = system_clock::to_time_t(sysTp);
    std::tm tmBuf{};
    if (!Platform::localtime_safe(&tt, &tmBuf))
        return "?";
    return std::format("{:04}-{:02}-{:02} {:02}:{:02}",
        tmBuf.tm_year + 1900, tmBuf.tm_mon + 1, tmBuf.tm_mday,
        tmBuf.tm_hour, tmBuf.tm_min);
}

std::string FmtPerms(fs::perms p)
{
    auto bit = [&](fs::perms b, char c) { return (p & b) != fs::perms::none ? c : '-'; };
    char s[10];
    s[0] = bit(fs::perms::owner_read, 'r');
    s[1] = bit(fs::perms::owner_write, 'w');
    s[2] = bit(fs::perms::owner_exec, 'x');
    s[3] = bit(fs::perms::group_read, 'r');
    s[4] = bit(fs::perms::group_write, 'w');
    s[5] = bit(fs::perms::group_exec, 'x');
    s[6] = bit(fs::perms::others_read, 'r');
    s[7] = bit(fs::perms::others_write, 'w');
    s[8] = bit(fs::perms::others_exec, 'x');
    s[9] = '\0';
    return s;
}

FilesystemEntry MakeEntry(const fs::directory_entry& de, bool followSymlinks)
{
    FilesystemEntry e;
    std::error_code ec;

    const bool isSym  = de.is_symlink(ec);
    fs::path   target = de.path();
    ec.clear();

    if (isSym && followSymlinks) {
        const auto canon = fs::canonical(de.path(), ec);
        if (!ec)
            target = canon;
        ec.clear();
    }

    e.name = de.path().filename().string();
    e.path = de.path().string();

    // status follows symlinks; symlink_status reports the link itself.
    // On Windows the distinction is moot — always use status.
    const auto status = [&]() {
        if constexpr (Platform::isWindows)
            return fs::status(target, ec);
        else
            return followSymlinks ? fs::status(target, ec)
                                  : fs::symlink_status(de.path(), ec);
    }();

    if (!ec) {
        if (fs::is_directory(status))
            e.kind = "dir";
        else if (fs::is_regular_file(status))
            e.kind = "file";
        else if (isSym)
            e.kind = "symlink";
        else
            e.kind = "other";
    } else {
        e.kind = isSym ? "symlink" : "other";
    }
    ec.clear();

    const bool isDir = (e.kind == "dir");

    if (!isDir && fs::is_regular_file(status)) {
        e.sizeBytes = fs::file_size(target, ec);
        if (ec)
            e.sizeBytes = 0;
        ec.clear();
    }
    e.sizeStr = FmtSize(e.sizeBytes, isDir);

    e.modTime = fs::last_write_time(target, ec);
    if (!ec)
        e.modified = FmtTime(e.modTime);
    ec.clear();

    if (status.permissions() != fs::perms::unknown)
        e.permissions = FmtPerms(status.permissions());

    return e;
}

std::vector<std::string> EntryToRow(const FilesystemEntry& e)
{
    return {e.name, e.kind, e.sizeStr, e.modified, e.permissions, e.path};
}

std::vector<ColumnInfo> StandardColumns()
{
    return {
        {"name",       "TEXT", false, false},
        {"kind",       "TEXT", false, false},
        {"size",       "TEXT", true,  false},
        {"modified",   "TEXT", true,  false},
        {std::string{Platform::kPermColName}, "TEXT", true, false},
        {"path",       "TEXT", false, false},
    };
}

std::string ToLower(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// SQL LIKE pattern matching (% = any sequence, _ = one char).
// O(n + m) time, O(1) space — classic two-pointer backtracking.
// caseSensitive = false  →  ILIKE / filesystem-style matching
// caseSensitive = true   →  strict SQL LIKE
bool LikeMatch(std::string_view text, std::string_view pat, bool caseSensitive = false)
{
    auto eq = [caseSensitive](char a, char b) -> bool {
        if (caseSensitive)
            return a == b;
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    };

    size_t ti = 0, pi = 0;
    size_t starPi = SIZE_MAX, starTi = 0; // last '%' anchor

    while (ti < text.size()) {
        if (pi < pat.size() && (pat[pi] == '_' || eq(text[ti], pat[pi]))) {
            ++ti;
            ++pi;
        } else if (pi < pat.size() && pat[pi] == '%') {
            starPi = pi++;   // record anchor, advance pattern
            starTi = ti;     // remember text position for backtrack
        } else if (starPi != SIZE_MAX) {
            pi = starPi + 1; // retry pattern after last '%'
            ti = ++starTi;   // advance text anchor by one
        } else {
            return false;
        }
    }
    // Consume any trailing '%' in pattern
    while (pi < pat.size() && pat[pi] == '%')
        ++pi;
    return pi == pat.size();
}

// Glob pattern matching (* = any sequence, ? = one char), case-insensitive.
// Same O(n + m) / O(1) algorithm — no translation to LIKE needed.
bool GlobMatch(std::string_view text, std::string_view pat)
{
    auto eq = [](char a, char b) -> bool {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    };

    size_t ti = 0, pi = 0;
    size_t starPi = SIZE_MAX, starTi = 0;

    while (ti < text.size()) {
        if (pi < pat.size() && (pat[pi] == '?' || eq(text[ti], pat[pi]))) {
            ++ti;
            ++pi;
        } else if (pi < pat.size() && pat[pi] == '*') {
            starPi = pi++;
            starTi = ti;
        } else if (starPi != SIZE_MAX) {
            pi = starPi + 1;
            ti = ++starTi;
        } else {
            return false;
        }
    }
    while (pi < pat.size() && pat[pi] == '*')
        ++pi;
    return pi == pat.size();
}
} // anonymous namespace

EntryPred kind_is(std::string_view k)
{
    std::string kk{k};
    return EntryPred{[kk](const FilesystemEntry& e) { return e.kind == kk; }};
}
EntryPred is_file()
{
    return kind_is("file");
}
EntryPred is_dir()
{
    return kind_is("dir");
}
EntryPred is_symlink()
{
    return kind_is("symlink");
}

EntryPred ext_is(std::string_view ext)
{
    const std::string lo = ToLower(std::string{ext});
    return EntryPred{[lo](const FilesystemEntry& e) {
        const std::string eExt = ToLower(fs::path(e.name).extension().string());
        return eExt == lo;
    }};
}

EntryPred ext_in(std::vector<std::string> exts)
{
    for (auto& s : exts)
        s = ToLower(s);
    return EntryPred{[exts = std::move(exts)](const FilesystemEntry& e) {
        const std::string eExt = ToLower(fs::path(e.name).extension().string());
        return std::ranges::find(exts, eExt) != exts.end();
    }};
}

EntryPred name_eq(std::string_view name)
{
    std::string n{name};
    return EntryPred{[n](const FilesystemEntry& e) { return e.name == n; }};
}

EntryPred name_contains(std::string_view sub)
{
    const std::string lo = ToLower(std::string{sub});
    return EntryPred{[lo](const FilesystemEntry& e) { return ToLower(e.name).find(lo) != std::string::npos; }};
}

// LIKE: case-sensitive (standard SQL behaviour)
EntryPred name_like(std::string_view pattern)
{
    std::string p{pattern};
    return EntryPred{[p](const FilesystemEntry& e) { return LikeMatch(e.name, p, /*caseSensitive=*/true); }};
}

// ILIKE: case-insensitive (PostgreSQL extension, useful for filenames)
EntryPred name_ilike(std::string_view pattern)
{
    std::string p{pattern};
    return EntryPred{[p](const FilesystemEntry& e) { return LikeMatch(e.name, p, /*caseSensitive=*/false); }};
}

// GLOB: always case-insensitive (filesystem convention)
EntryPred name_glob(std::string_view pattern)
{
    std::string p{pattern};
    return EntryPred{[p](const FilesystemEntry& e) { return GlobMatch(e.name, p); }};
}

EntryPred size_eq(std::uintmax_t n)
{
    return EntryPred{[n](const FilesystemEntry& e) { return e.sizeBytes == n; }};
}
EntryPred size_gt(std::uintmax_t n)
{
    return EntryPred{[n](const FilesystemEntry& e) { return e.sizeBytes > n; }};
}
EntryPred size_lt(std::uintmax_t n)
{
    return EntryPred{[n](const FilesystemEntry& e) { return e.sizeBytes < n; }};
}
EntryPred size_ge(std::uintmax_t n)
{
    return EntryPred{[n](const FilesystemEntry& e) { return e.sizeBytes >= n; }};
}
EntryPred size_le(std::uintmax_t n)
{
    return EntryPred{[n](const FilesystemEntry& e) { return e.sizeBytes <= n; }};
}
EntryPred size_between(std::uintmax_t lo, std::uintmax_t hi)
{
    return EntryPred{[lo, hi](const FilesystemEntry& e) { return e.sizeBytes >= lo && e.sizeBytes <= hi; }};
}

FluentQuery::FluentQuery(fs::path dir) : dir_(std::move(dir)) {}

FluentQuery FluentQuery::from(fs::path dir)
{
    return FluentQuery{std::move(dir)};
}

FluentQuery& FluentQuery::recursive(bool on, int maxDepth)
{
    recursive_ = on;
    maxDepth_  = maxDepth;
    return *this;
}
FluentQuery& FluentQuery::where(EntryPred p)
{
    if (p)
        predicates_.push_back(std::move(p));
    return *this;
}
FluentQuery& FluentQuery::show_hidden(bool show)
{
    showHidden_ = show;
    return *this;
}
FluentQuery& FluentQuery::dirs_first(bool on)
{
    dirsFirst_ = on;
    return *this;
}
FluentQuery& FluentQuery::select(std::vector<std::string> cols)
{
    selectedCols_ = std::move(cols);
    return *this;
}
FluentQuery& FluentQuery::order_by(std::string_view col, bool asc)
{
    sortColumn_    = col;
    sortAscending_ = asc;
    return *this;
}
FluentQuery& FluentQuery::limit(int n)
{
    limit_ = n;
    return *this;
}
FluentQuery& FluentQuery::offset(int n)
{
    offset_ = n;
    return *this;
}

std::vector<FilesystemEntry> FluentQuery::entries() const
{
    std::vector<FilesystemEntry> all;
    std::error_code              ec;

    constexpr bool kFollowSymlinks = true;

    auto addEntry = [&](const fs::directory_entry& de) {
        if (!showHidden_ && !de.path().filename().empty()) {
            const auto fname = de.path().filename().string();
            if (!fname.empty() && fname[0] == '.')
                return;
        }
        all.push_back(MakeEntry(de, kFollowSymlinks));
    };

    if (recursive_) {
        fs::recursive_directory_iterator rit(dir_, fs::directory_options::skip_permission_denied, ec);
        if (ec)
            return {};
        for (const auto& entry : rit) {
            if (maxDepth_ >= 0 && rit.depth() > maxDepth_) {
                rit.disable_recursion_pending();
                continue;
            }
            addEntry(entry);
        }
    } else {
        for (const auto& entry : fs::directory_iterator(dir_, fs::directory_options::skip_permission_denied, ec)) {
            addEntry(entry);
        }
    }

    if (hasSqlPlan_ && sqlPlan_.hasPred) {
        // Columns: name(0) kind(1) size-num(2) modified(3) perms(4)
        //          path(5) extension(6) ext(7=alias) modtime-num(8)
        const size_t n = all.size();

        Query::TabularSoA soa;
        soa.rowCount = n;
        soa.colNames = {"name", "kind", "size", "modified",
                        std::string{Platform::kPermColName},
                        "path", "extension", "ext", "modtime"};
        soa.colTypes = {
            Query::ColType::Text,    // name
            Query::ColType::Text,    // kind
            Query::ColType::Numeric, // size
            Query::ColType::Text,    // modified
            Query::ColType::Text,    // permissions / attributes
            Query::ColType::Text,    // path
            Query::ColType::Text,    // extension
            Query::ColType::Text,    // ext (alias)
            Query::ColType::Numeric, // modtime (raw int64 for range queries)
        };
        const size_t ncols    = soa.colNames.size();
        const size_t permCol  = soa.col_index(Platform::kPermColName);
        soa.strCols.resize(ncols);
        soa.numCols.resize(ncols);
        for (auto& v : soa.strCols)
            v.reserve(n);
        soa.numCols[2].reserve(n); // size
        soa.numCols[8].reserve(n); // modtime

        for (const auto& e : all) {
            const std::string ext = fs::path(e.name).extension().string();
            soa.strCols[0].push_back(e.name);
            soa.strCols[1].push_back(e.kind);
            soa.strCols[2].push_back(e.sizeStr);
            soa.strCols[3].push_back(e.modified);
            soa.strCols[permCol].push_back(e.permissions);
            soa.strCols[5].push_back(e.path);
            soa.strCols[6].push_back(ext);
            soa.strCols[7].push_back(ext); // ext alias
            soa.strCols[8].push_back(e.modified);
            soa.numCols[2].push_back(static_cast<int64_t>(e.sizeBytes));
            soa.numCols[8].push_back(static_cast<int64_t>(e.modTime.time_since_epoch().count()));
        }

        const std::vector<uint8_t> mask = Query::TabularQuery::filter_mask(sqlPlan_, soa);

        std::vector<FilesystemEntry> filtered;
        filtered.reserve(n);
        for (size_t i = 0; i < n; ++i)
            if (mask[i])
                filtered.push_back(std::move(all[i]));
        all = std::move(filtered);

    } else if (!predicates_.empty()) {
        // Fluent-API path: row-by-row EntryPred evaluation.
        auto combined = [&](const FilesystemEntry& e) {
            return std::ranges::all_of(predicates_, [&](const EntryPred& p) { return p(e); });
        };
        auto filtered = all | std::views::filter(combined);
        all           = std::vector<FilesystemEntry>(filtered.begin(), filtered.end());
    }

    // Schwartzian transform: pre-compute lowercase sort keys once so the
    // comparator never allocates inside the O(n log n) loop.
    const bool bySize = (sortColumn_ == "size");
    const bool byMod  = (sortColumn_ == "modified");
    const bool byKind = (sortColumn_ == "kind");
    const bool byName = !bySize && !byMod && !byKind;
    const bool doDF   = dirsFirst_ && (byName || byKind);
    const bool asc    = sortAscending_;

    struct SortKey
    {
        size_t      idx;
        std::string lo;
    };
    std::vector<SortKey> keys;
    keys.reserve(all.size());
    for (size_t i = 0; i < all.size(); ++i)
        keys.push_back({i, byName ? ToLower(all[i].name) : std::string{}});

    std::stable_sort(keys.begin(), keys.end(), [&](const SortKey& ai, const SortKey& bi) {
        const FilesystemEntry& a = all[ai.idx];
        const FilesystemEntry& b = all[bi.idx];
        if (doDF) {
            const bool aD = (a.kind == "dir"), bD = (b.kind == "dir");
            if (aD != bD)
                return aD;
        }
        if (bySize)
            return asc ? a.sizeBytes < b.sizeBytes : a.sizeBytes > b.sizeBytes;
        if (byMod)
            return asc ? a.modTime < b.modTime : a.modTime > b.modTime;
        if (byKind)
            return asc ? a.kind < b.kind : a.kind > b.kind;
        return asc ? ai.lo < bi.lo : ai.lo > bi.lo;
    });

    std::vector<FilesystemEntry> sorted;
    sorted.reserve(all.size());
    for (const auto& k : keys)
        sorted.push_back(std::move(all[k.idx]));
    all = std::move(sorted);

    if (offset_ > 0 && offset_ < static_cast<int>(all.size()))
        all.erase(all.begin(), all.begin() + offset_);
    if (limit_ >= 0 && limit_ < static_cast<int>(all.size()))
        all.resize(static_cast<size_t>(limit_));

    return all;
}

int FluentQuery::count() const
{
    FluentQuery tmp = *this;
    tmp.limit_      = -1;
    tmp.offset_     = 0;
    return static_cast<int>(tmp.entries().size());
}

QueryResult FluentQuery::execute() const
{
    const auto t0 = std::chrono::steady_clock::now();

    QueryResult result;
    result.columns = StandardColumns();

    if (!selectedCols_.empty()) {
        std::vector<ColumnInfo> kept;
        for (const auto& ci : result.columns) {
            if (std::ranges::find(selectedCols_, ci.name) != selectedCols_.end())
                kept.push_back(ci);
        }
        // Handle virtual columns: extension, stem
        for (const auto& col : selectedCols_) {
            if (col == "extension" || col == "stem")
                kept.push_back({col, "TEXT", true, false});
        }
        result.columns = std::move(kept);
    }

    const std::vector<FilesystemEntry> ents = entries();

    // Precompute column-name → row-index map once (O(1) per lookup vs O(n) find_if)
    const auto                              stdCols = StandardColumns();
    std::unordered_map<std::string, size_t> colIdx;
    colIdx.reserve(stdCols.size());
    for (size_t i = 0; i < stdCols.size(); ++i)
        colIdx[stdCols[i].name] = i;

    result.rows.reserve(ents.size());
    for (const auto& e : ents) {
        auto row = EntryToRow(e); // 6 standard columns
        if (selectedCols_.empty()) {
            result.rows.push_back(std::move(row));
        } else {
            std::vector<std::string> projected;
            projected.reserve(selectedCols_.size());
            for (const auto& col : selectedCols_) {
                const auto it = colIdx.find(col);
                if (it != colIdx.end() && it->second < row.size()) {
                    projected.push_back(row[it->second]);
                } else if (col == "extension") {
                    projected.push_back(fs::path(e.name).extension().string());
                } else if (col == "stem") {
                    projected.push_back(fs::path(e.name).stem().string());
                }
            }
            result.rows.push_back(std::move(projected));
        }
    }

    const auto t1      = std::chrono::steady_clock::now();
    result.executionMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return result;
}

std::expected<FluentQuery, std::string> FluentQuery::from_sql(std::string_view sql)
{
    auto planRes = Query::TabularQuery::parse(sql);
    if (!planRes)
        return std::unexpected(planRes.error());
    Query::QueryPlan plan = std::move(*planRes);

    // Handle glob-style recursive suffix: /path/** → recursive + strip /**
    std::string path = plan.fromSource;
    if (path.size() >= 3) {
        const auto tail = path.substr(path.size() - 3);
        if (tail == "/**" || tail == "\\**") {
            plan.recursive = true;
            path.resize(path.size() - 3);
            plan.fromSource = path;
        }
    }

    FluentQuery q{fs::path(path)};
    q.hasSqlPlan_ = true;
    q.sqlPlan_    = plan;
    q.recursive_  = plan.recursive;
    if (!plan.selectCols.empty())
        q.selectedCols_ = plan.selectCols;
    if (!plan.orderByCol.empty()) {
        q.sortColumn_    = plan.orderByCol;
        q.sortAscending_ = plan.orderAsc;
    }
    if (plan.limitVal >= 0)
        q.limit_ = plan.limitVal;
    if (plan.offsetVal > 0)
        q.offset_ = plan.offsetVal;

    return q;
}

} // namespace Adapters::Fs
