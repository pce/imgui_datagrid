#pragma once
#include "../data_source.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace datagrid::adapters {

enum class FsCapability {
    PosixPermissions,
    WindowsAttributes
};

struct FilesystemEntry
{
    std::string name;                  ///< Filename only (no directory part)
    std::string kind;                  ///< "dir" | "file" | "symlink" | "other"
    std::string sizeStr;               ///< Human-readable size ("1.4 MB", "—" for dirs)
    std::string modified;              ///< ISO-8601 "YYYY-MM-DD HH:MM"
    std::string path;                  ///< Absolute path (hidden grid column used in callbacks)
    std::string permissions;           ///< POSIX-style "rwxr-xr-x"

    std::uintmax_t     sizeBytes = 0;  ///< Raw byte count for numeric sorting
    fs::file_time_type modTime   = {}; ///< Raw mtime for chronological sorting
};

class FilesystemAdapter final : public IDataSource
{
  public:
    FilesystemAdapter();
    ~FilesystemAdapter() override = default;

    FilesystemAdapter(const FilesystemAdapter&)            = delete;
    FilesystemAdapter& operator=(const FilesystemAdapter&) = delete;

    FilesystemAdapter(FilesystemAdapter&&) noexcept            = default;
    FilesystemAdapter& operator=(FilesystemAdapter&&) noexcept = default;

    [[nodiscard]] std::string AdapterName() const override { return "filesystem"; }
    [[nodiscard]] std::string AdapterVersion() const override { return "1.0.0"; }
    [[nodiscard]] std::string AdapterLabel() const override;

    /// connectionString = starting directory path.  Empty → home directory.
    std::expected<void, Error> Connect(const ConnectionParams& params) override;
    void                       Disconnect() override;
    [[nodiscard]] bool         IsConnected() const override;
    [[nodiscard]] std::string  LastError() const override;

    /// Returns bookmarked root paths (home dir, filesystem root, user additions).
    [[nodiscard]] std::vector<std::string> GetCatalogs() const override;

    /// Returns one TableInfo per direct subdirectory of `catalog`.
    /// TableInfo::name = absolute path,  TableInfo::kind = "dir".
    [[nodiscard]] std::vector<TableInfo> GetTables(const std::string& catalog) const override;

    /// Returns the fixed six-column schema (name, kind, size, modified, permissions, path).
    /// Column indices are stable — callbacks may rely on row[5] == absolute path.
    [[nodiscard]] std::vector<ColumnInfo> GetColumns(const std::string& table) const override;

    /// query.table = absolute directory path to enumerate.
    ///
    /// Supported DataQuery fields:
    ///   whereExact["kind"]   = "dir" | "file"
    ///   searchColumn / searchValue  → substring filter on "name"
    ///   sortColumn           = "name" | "size" | "modified" | "kind"
    ///   sortAscending, page, pageSize
    [[nodiscard]] QueryResult ExecuteQuery(const DataQuery& q) const override;
    [[nodiscard]] int         CountQuery(const DataQuery& q) const override;

    /// Not applicable — always returns an error result.
    [[nodiscard]] datagrid::adapters::QueryResult Execute(const std::string& sql) const override;

    void                      SetCurrentPath(const std::string& absolutePath);
    [[nodiscard]] std::string GetCurrentPath() const;
    [[nodiscard]] std::string GetParentPath() const;

    /// Navigate up one level.  Returns false if already at the root.
    bool NavigateUp();
    void NavigateHome();

    [[nodiscard]] bool EntryIsDir(const std::string& absolutePath) const;
    [[nodiscard]] bool EntryIsFile(const std::string& absolutePath) const;

    /// Add a path to the catalog list.  Ignored if not an existing directory.
    void AddBookmark(const std::string& absolutePath);
    /// Remove a user-added bookmark.  Built-in entries (home, root) are immutable.
    void                                   RemoveBookmark(const std::string& absolutePath);
    [[nodiscard]] std::vector<std::string> Bookmarks() const;

    void               SetShowHidden(bool show);
    void               SetFollowSymlinks(bool follow);
    [[nodiscard]] bool GetShowHidden() const;
    [[nodiscard]] bool GetFollowSymlinks() const;

  private:
    /// Enumerate entries in `dir` respecting hidden / symlink settings.
    [[nodiscard]] std::vector<FilesystemEntry> EnumerateDir(const fs::path& dir) const;

    /// Apply DataQuery filters and sort to a flat entry list.
    [[nodiscard]] std::vector<FilesystemEntry> ApplyQuery(std::vector<FilesystemEntry> entries,
                                                          const DataQuery&             q) const;

    [[nodiscard]] static std::string              FormatSize(std::uintmax_t bytes, bool isDir);
    [[nodiscard]] static std::string              FormatTime(const fs::file_time_type& t);
    [[nodiscard]] static std::string              FormatPerms(fs::perms p);
    [[nodiscard]] static std::vector<std::string> EntryToRow(const FilesystemEntry& e);

    [[nodiscard]] static fs::path HomeDir();
    [[nodiscard]] static fs::path RootDir();

    [[nodiscard]] bool HasCapability(FsCapability capability) const;

    fs::path                 currentPath_;
    std::vector<std::string> bookmarks_;
    std::string              lastError_;
    bool                     connected_      = false;
    bool                     showHidden_     = false;
    bool                     followSymlinks_ = true;
};

} // namespace datagrid::adapters
