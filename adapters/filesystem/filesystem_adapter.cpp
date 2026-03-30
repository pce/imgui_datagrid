#include "filesystem_adapter.hpp"
#include "../adapter_registry.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sstream>
#include <system_error>

#if defined(_WIN32)
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#else
#   include <pwd.h>
#   include <unistd.h>
#endif

// Self-register before main() — no changes to App or main needed.
REGISTER_ADAPTER(Adapters::FilesystemAdapter, "filesystem")

namespace Adapters {

// ============================================================
//  Construction
// ============================================================

FilesystemAdapter::FilesystemAdapter() = default;

// ============================================================
//  Adapter identity
// ============================================================

std::string FilesystemAdapter::AdapterLabel() const
{
    if (!connected_) return "Filesystem (disconnected)";
    return "Filesystem  " + currentPath_.string();
}

// ============================================================
//  Connection lifecycle
// ============================================================

std::expected<void, Error> FilesystemAdapter::Connect(const ConnectionParams& params)
{
    Disconnect();
    lastError_.clear();

    const fs::path startPath = params.connectionString.empty()
        ? HomeDir()
        : fs::path(params.connectionString);

    std::error_code ec;
    if (!fs::is_directory(startPath, ec)) {
        lastError_ = "Not a directory: " + startPath.string();
        if (ec) lastError_ += " (" + ec.message() + ")";
        return std::unexpected(lastError_);
    }

    const fs::path canonical = fs::canonical(startPath, ec);
    currentPath_ = ec ? startPath : canonical;
    connected_   = true;
    return {};
}

void FilesystemAdapter::Disconnect()
{
    connected_ = false;
    currentPath_.clear();
    lastError_.clear();
}

bool FilesystemAdapter::IsConnected() const { return connected_; }
std::string FilesystemAdapter::LastError() const { return lastError_; }

// ============================================================
//  Schema navigation
// ============================================================

std::vector<std::string> FilesystemAdapter::GetCatalogs() const
{
    if (!connected_) return {};

    std::vector<std::string> catalogs;

    const std::string homeStr = HomeDir().string();
    const std::string rootStr = RootDir().string();

    catalogs.push_back(homeStr);
    if (homeStr != rootStr)
        catalogs.push_back(rootStr);

    for (const auto& bm : bookmarks_) {
        if (bm != homeStr && bm != rootStr)
            catalogs.push_back(bm);
    }

    return catalogs;
}

std::vector<TableInfo> FilesystemAdapter::GetTables(
    const std::string& catalog) const
{
    if (!connected_) return {};

    const fs::path dir = catalog.empty() ? currentPath_ : fs::path(catalog);

    std::vector<TableInfo> tables;
    std::error_code ec;

    fs::directory_iterator it(dir, ec);
    if (ec) return tables;

    for (const auto& entry : it) {
        std::error_code entryEc;

        // Only interested in subdirectories here (the flat listing is
        // handled by ExecuteQuery; GetTables powers the sidebar tree).
        if (!entry.is_directory(entryEc) || entryEc) continue;

        const std::string name = entry.path().filename().string();

        // Respect the show-hidden setting for sidebar nodes too.
        if (!showHidden_ && !name.empty() && name[0] == '.') continue;

        TableInfo t;
        t.name    = entry.path().string();   // full absolute path
        t.kind    = "dir";
        t.catalog = dir.string();
        tables.push_back(std::move(t));
    }

    std::sort(tables.begin(), tables.end(),
        [](const TableInfo& a, const TableInfo& b) { return a.name < b.name; });

    return tables;
}

std::vector<ColumnInfo> FilesystemAdapter::GetColumns(
    const std::string& /*table*/) const
{
    // Fixed schema for every directory listing.
    // Column indices used by navigation callbacks:
    //   0  name         TEXT    filename only
    //   1  kind         TEXT    "dir" | "file" | "symlink" | "other"
    //   2  size         TEXT    human-readable, right-aligned
    //   3  modified     TEXT    "YYYY-MM-DD HH:MM", right-aligned
    //   4  permissions  TEXT    POSIX rwx string
    //   5  path         TEXT    absolute path (wire to double-click callback)
    return {
        { "name",        "TEXT", false, false },
        { "kind",        "TEXT", false, false },
        { "size",        "TEXT", true,  false },
        { "modified",    "TEXT", true,  false },
        { "permissions", "TEXT", true,  false },
        { "path",        "TEXT", false, false },
    };
}

// ============================================================
//  Internal: enumerate one directory
// ============================================================

std::vector<FilesystemEntry>
FilesystemAdapter::EnumerateDir(const fs::path& dir) const
{
    std::vector<FilesystemEntry> entries;
    std::error_code ec;

    fs::directory_iterator it(dir, ec);
    if (ec) {
        // Store the error but return an empty list — callers show an error row
        const_cast<std::string&>(lastError_) =
            "Cannot read directory: " + dir.string() + " (" + ec.message() + ")";
        return entries;
    }

    for (const auto& de : it) {
        std::error_code entryEc;
        const std::string name = de.path().filename().string();

        if (!showHidden_ && !name.empty() && name[0] == '.') continue;

        FilesystemEntry e;
        e.name = name;
        e.path = de.path().string();

        // ── Determine kind ──────────────────────────────────────────────────
        if (de.is_symlink(entryEc)) {
            e.kind = "symlink";
        } else if (de.is_directory(entryEc)) {
            e.kind = "dir";
        } else if (de.is_regular_file(entryEc)) {
            e.kind = "file";
        } else {
            e.kind = "other";
        }
        if (entryEc) { e.kind = "other"; entryEc.clear(); }

        // ── Resolve symlink target for metadata if requested ────────────────
        const bool isSymlink = (e.kind == "symlink");
        fs::path metaPath = de.path();
        if (isSymlink && followSymlinks_) {
            const auto canonical = fs::canonical(de.path(), entryEc);
            if (!entryEc) {
                metaPath = canonical;
                // Kind of the resolved target
                if (fs::is_directory(metaPath, entryEc))
                    e.kind = "dir";
                else if (fs::is_regular_file(metaPath, entryEc))
                    e.kind = "file";
            }
            entryEc.clear();
        }

        // ── Size ────────────────────────────────────────────────────────────
        if (e.kind == "file") {
            const auto sz = fs::file_size(metaPath, entryEc);
            if (!entryEc) {
                e.sizeBytes = sz;
                e.sizeStr   = FormatSize(sz, false);
            } else {
                e.sizeStr = "?";
                entryEc.clear();
            }
        } else {
            // Directories and symlinks-to-dirs show an em dash
            e.sizeStr = "\xe2\x80\x94";   // UTF-8 em dash "—"
        }

        // ── Modified time ────────────────────────────────────────────────────
        const auto modTime = fs::last_write_time(metaPath, entryEc);
        if (!entryEc) {
            e.modTime  = modTime;
            e.modified = FormatTime(modTime);
        } else {
            e.modified = "?";
            entryEc.clear();
        }

        // ── Permissions ──────────────────────────────────────────────────────
        const auto status = followSymlinks_
            ? fs::status(de.path(), entryEc)
            : fs::symlink_status(de.path(), entryEc);
        e.permissions = entryEc ? "?????????" : FormatPerms(status.permissions());
        entryEc.clear();

        entries.push_back(std::move(e));
    }

    return entries;
}

// ============================================================
//  Internal: apply DataQuery filters and sort
// ============================================================

std::vector<FilesystemEntry>
FilesystemAdapter::ApplyQuery(
    std::vector<FilesystemEntry> entries,
    const DataQuery&             q) const
{
    // ── Exact-match filter (only "kind" makes sense here) ───────────────────
    auto kindIt = q.whereExact.find("kind");
    if (kindIt != q.whereExact.end() && !kindIt->second.empty()) {
        const std::string& wantKind = kindIt->second;
        entries.erase(
            std::remove_if(entries.begin(), entries.end(),
                [&](const FilesystemEntry& e) { return e.kind != wantKind; }),
            entries.end()
        );
    }

    // ── Substring search (case-insensitive, on "name" or "kind") ───────────
    if (!q.searchColumn.empty() && !q.searchValue.empty()) {
        std::string needle = q.searchValue;
        for (char& c : needle) c = static_cast<char>(
            std::tolower(static_cast<unsigned char>(c)));

        entries.erase(
            std::remove_if(entries.begin(), entries.end(),
                [&](const FilesystemEntry& e) {
                    std::string hay = (q.searchColumn == "kind") ? e.kind : e.name;
                    for (char& c : hay) c = static_cast<char>(
                        std::tolower(static_cast<unsigned char>(c)));
                    return hay.find(needle) == std::string::npos;
                }),
            entries.end()
        );
    }

    // ── Sort ────────────────────────────────────────────────────────────────
    //
    // Convention: when sorting by name (or no sort), directories appear
    // before files within each sorted group — the same convention used by
    // macOS Finder, GNOME Files, and most file managers.
    //
    const bool asc      = q.sortAscending;
    const bool byName   = q.sortColumn.empty() || q.sortColumn == "name";
    const bool bySize   = (q.sortColumn == "size");
    const bool byMod    = (q.sortColumn == "modified");
    const bool byKind   = (q.sortColumn == "kind");
    const bool dirsFirst = byName || byKind;

    std::stable_sort(entries.begin(), entries.end(),
        [&](const FilesystemEntry& a, const FilesystemEntry& b)
        {
            // Directories always before files unless explicitly sorting by
            // something other than name
            if (dirsFirst) {
                const bool aDir = (a.kind == "dir");
                const bool bDir = (b.kind == "dir");
                if (aDir != bDir) return aDir;   // true → a comes first
            }

            if (bySize) {
                return asc ? (a.sizeBytes < b.sizeBytes)
                           : (a.sizeBytes > b.sizeBytes);
            }
            if (byMod) {
                return asc ? (a.modTime < b.modTime)
                           : (a.modTime > b.modTime);
            }
            if (byKind) {
                return asc ? (a.kind < b.kind) : (a.kind > b.kind);
            }

            // Default / name: case-insensitive
            std::string an = a.name, bn = b.name;
            for (char& c : an) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            for (char& c : bn) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return asc ? (an < bn) : (an > bn);
        }
    );

    return entries;
}

// ============================================================
//  ExecuteQuery / CountQuery / Execute
// ============================================================

QueryResult FilesystemAdapter::ExecuteQuery(const DataQuery& q) const
{
    QueryResult result;
    if (!connected_) { result.error = "Not connected."; return result; }

    const fs::path dir = q.table.empty() ? currentPath_ : fs::path(q.table);

    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        result.error = "Not a directory: " + dir.string();
        return result;
    }

    const auto t0 = std::chrono::steady_clock::now();

    auto entries = EnumerateDir(dir);
    entries      = ApplyQuery(std::move(entries), q);

    result.columns = GetColumns(q.table);

    const int total  = static_cast<int>(entries.size());
    const int offset = q.page * q.pageSize;
    const int end    = std::min(offset + q.pageSize, total);

    if (offset < total) {
        result.rows.reserve(static_cast<size_t>(end - offset));
        for (int i = offset; i < end; ++i)
            result.rows.push_back(EntryToRow(entries[static_cast<size_t>(i)]));
    }

    const auto t1      = std::chrono::steady_clock::now();
    result.executionMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return result;
}

int FilesystemAdapter::CountQuery(const DataQuery& q) const
{
    if (!connected_) return 0;

    const fs::path dir = q.table.empty() ? currentPath_ : fs::path(q.table);

    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return 0;

    auto entries = EnumerateDir(dir);
    entries      = ApplyQuery(std::move(entries), q);
    return static_cast<int>(entries.size());
}

QueryResult FilesystemAdapter::Execute(const std::string& /*sql*/) const
{
    QueryResult r;
    r.error = "FilesystemAdapter does not support raw SQL.\n"
              "Use ExecuteQuery() with query.table = \"/absolute/path\".";
    return r;
}

// ============================================================
//  Navigation
// ============================================================

void FilesystemAdapter::SetCurrentPath(const std::string& absolutePath)
{
    std::error_code ec;
    const fs::path p(absolutePath);

    if (!fs::is_directory(p, ec)) {
        lastError_ = "Not a directory: " + absolutePath;
        return;
    }

    lastError_.clear();
    const fs::path canonical = fs::canonical(p, ec);
    currentPath_ = ec ? p : canonical;
}

std::string FilesystemAdapter::GetCurrentPath() const
{
    return currentPath_.string();
}

std::string FilesystemAdapter::GetParentPath() const
{
    const fs::path parent = currentPath_.parent_path();
    // parent_path() of "/" or "C:\" returns itself
    return (parent == currentPath_) ? currentPath_.string() : parent.string();
}

bool FilesystemAdapter::NavigateUp()
{
    const fs::path parent = currentPath_.parent_path();
    if (parent == currentPath_) return false;   // already at root
    currentPath_ = parent;
    return true;
}

void FilesystemAdapter::NavigateHome()
{
    currentPath_ = HomeDir();
}

bool FilesystemAdapter::EntryIsDir(const std::string& absolutePath) const
{
    std::error_code ec;
    return fs::is_directory(fs::path(absolutePath), ec);
}

bool FilesystemAdapter::EntryIsFile(const std::string& absolutePath) const
{
    std::error_code ec;
    return fs::is_regular_file(fs::path(absolutePath), ec);
}

// ============================================================
//  Bookmarks
// ============================================================

void FilesystemAdapter::AddBookmark(const std::string& absolutePath)
{
    std::error_code ec;
    if (!fs::is_directory(fs::path(absolutePath), ec)) return;
    for (const auto& bm : bookmarks_)
        if (bm == absolutePath) return;
    bookmarks_.push_back(absolutePath);
}

void FilesystemAdapter::RemoveBookmark(const std::string& absolutePath)
{
    bookmarks_.erase(
        std::remove(bookmarks_.begin(), bookmarks_.end(), absolutePath),
        bookmarks_.end()
    );
}

std::vector<std::string> FilesystemAdapter::Bookmarks() const
{
    std::vector<std::string> all;
    all.push_back(HomeDir().string());
    all.push_back(RootDir().string());
    for (const auto& bm : bookmarks_) all.push_back(bm);
    return all;
}

// ============================================================
//  Options
// ============================================================

void FilesystemAdapter::SetShowHidden(bool show)      { showHidden_     = show;   }
bool FilesystemAdapter::GetShowHidden()         const { return showHidden_;       }
void FilesystemAdapter::SetFollowSymlinks(bool follow){ followSymlinks_ = follow; }
bool FilesystemAdapter::GetFollowSymlinks()     const { return followSymlinks_;   }

// ============================================================
//  Static formatting helpers
// ============================================================

std::string FilesystemAdapter::FormatSize(std::uintmax_t bytes, bool isDir)
{
    if (isDir) return "\xe2\x80\x94";   // UTF-8 "—"

    constexpr std::uintmax_t KB = 1024;
    constexpr std::uintmax_t MB = 1024 * KB;
    constexpr std::uintmax_t GB = 1024 * MB;
    constexpr std::uintmax_t TB = 1024 * GB;

    char buf[32];
    if      (bytes < KB) std::snprintf(buf, sizeof(buf), "%llu B",  (unsigned long long)bytes);
    else if (bytes < MB) std::snprintf(buf, sizeof(buf), "%.1f KB", bytes / (double)KB);
    else if (bytes < GB) std::snprintf(buf, sizeof(buf), "%.1f MB", bytes / (double)MB);
    else if (bytes < TB) std::snprintf(buf, sizeof(buf), "%.2f GB", bytes / (double)GB);
    else                 std::snprintf(buf, sizeof(buf), "%.2f TB", bytes / (double)TB);
    return buf;
}

std::string FilesystemAdapter::FormatTime(const fs::file_time_type& t)
{
    // C++17 does not provide file_clock::to_sys().  The portable trick is to
    // compute the offset between file_clock::now() and system_clock::now()
    // and apply it to the supplied time point.
    using namespace std::chrono;

    const auto delta   = t - fs::file_time_type::clock::now();
    const auto sysTp   = system_clock::now()
                         + duration_cast<system_clock::duration>(delta);
    const std::time_t tt = system_clock::to_time_t(sysTp);

#if defined(_WIN32)
    std::tm tmBuf{};
    if (localtime_s(&tmBuf, &tt) != 0) return "?";
    std::tm* tp = &tmBuf;
#else
    std::tm tmBuf{};
    std::tm* tp = ::localtime_r(&tt, &tmBuf);
    if (!tp) return "?";
#endif

    char buf[20];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", tp);
    return buf;
}

std::string FilesystemAdapter::FormatPerms(fs::perms p)
{
    using P = fs::perms;
    auto bit = [&](fs::perms mask, char ch) -> char {
        return (p & mask) != P::none ? ch : '-';
    };
    char s[10];
    s[0] = bit(P::owner_read,   'r');
    s[1] = bit(P::owner_write,  'w');
    s[2] = bit(P::owner_exec,   'x');
    s[3] = bit(P::group_read,   'r');
    s[4] = bit(P::group_write,  'w');
    s[5] = bit(P::group_exec,   'x');
    s[6] = bit(P::others_read,  'r');
    s[7] = bit(P::others_write, 'w');
    s[8] = bit(P::others_exec,  'x');
    s[9] = '\0';
    return s;
}

std::vector<std::string>
FilesystemAdapter::EntryToRow(const FilesystemEntry& e)
{
    return {
        e.name,         // [0] name
        e.kind,         // [1] kind
        e.sizeStr,      // [2] size
        e.modified,     // [3] modified
        e.permissions,  // [4] permissions
        e.path,         // [5] path  ← wire to double-click in SetOnRowDblClick()
    };
}

// ============================================================
//  Platform-specific home / root paths
// ============================================================

fs::path FilesystemAdapter::HomeDir()
{
#if defined(_WIN32)
    // USERPROFILE is set by Windows for every user session.
    const char* home = std::getenv("USERPROFILE");
    if (home) {
        std::error_code ec;
        const fs::path p(home);
        if (fs::is_directory(p, ec)) return p;
    }
    // Fallback: HOMEDRIVE + HOMEPATH (legacy / some server environments)
    const char* drive = std::getenv("HOMEDRIVE");
    const char* hpath = std::getenv("HOMEPATH");
    if (drive && hpath) {
        std::error_code ec;
        const fs::path p(std::string(drive) + hpath);
        if (fs::is_directory(p, ec)) return p;
    }
    return fs::path("C:\\");
#else
    // Prefer $HOME so that sudo / su overrides are respected.
    const char* home = std::getenv("HOME");
    if (home && home[0] != '\0') {
        std::error_code ec;
        const fs::path p(home);
        if (fs::is_directory(p, ec)) return p;
    }
    // Fall back to the passwd database.
    const struct passwd* pw = ::getpwuid(::getuid());
    if (pw && pw->pw_dir && pw->pw_dir[0] != '\0')
        return fs::path(pw->pw_dir);
    return fs::path("/");
#endif
}

fs::path FilesystemAdapter::RootDir()
{
#if defined(_WIN32)
    // Return the root of the drive that contains the current directory.
    char buf[MAX_PATH];
    if (GetCurrentDirectoryA(MAX_PATH, buf))
        return fs::path(buf).root_path();
    return fs::path("C:\\");
#else
    return fs::path("/");
#endif
}

} // namespace Adapters
