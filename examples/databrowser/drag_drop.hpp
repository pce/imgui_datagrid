#pragma once
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace UI {

inline constexpr const char* kRowPayloadType  = "DATAGRID_ROW";
inline constexpr const char* kFilePayloadType = "DATAGRID_FILE";

/// Dragged from any DataBrowser row.
struct RowPayload
{
    int  sourceBrowserId = 0;
    int  pageRowIndex    = 0;
    char adapterName[32] = {}; // "sqlite", "duckdb", "filesystem"
    char tableName[256]  = {}; // current table name or path
    char rowData[2048]   = {}; // "colName=value\n" per line (key=value\n format)
};

/// Dragged from a filesystem browser row.
struct FilePayload
{
    int  sourceBrowserId = 0;
    char path[1024]      = {};
    char extension[32]   = {}; // lowercase ".sqlite3", ".duckdb", etc.
    char kind[8]         = {}; // "file" or "dir"
    char name[256]       = {}; // filename only (basename)
};

static_assert(std::is_trivially_copyable_v<RowPayload>, "RowPayload must be trivially copyable");
static_assert(std::is_trivially_copyable_v<FilePayload>, "FilePayload must be trivially copyable");

enum class FileDbType : uint8_t
{
    Unknown,
    SQLite,
    DuckDB
};

/// Lowercase the extension including the dot.  Returns "" if no extension.
[[nodiscard]] inline std::string FileExtension(std::string_view path) noexcept
{
    const auto pos = path.rfind('.');
    if (pos == std::string_view::npos)
        return {};

    // Ensure the dot belongs to the filename, not a directory component.
    const auto sep = path.find_last_of("/\\");
    if (sep != std::string_view::npos && pos < sep)
        return {};

    std::string ext{path.substr(pos)};
    std::transform(
        ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

/// Sniff the database type: first tries extension, then reads magic bytes.
///   SQLite magic : first 16 bytes == "SQLite format 3\0"
///   DuckDB magic : first  4 bytes == 0x44 0x55 0x43 0x4B  ("DUCK")
[[nodiscard]] inline FileDbType SniffDbType(std::string_view path) noexcept
{
    const std::string ext = FileExtension(path);
    if (ext == ".sqlite" || ext == ".sqlite3" || ext == ".db" || ext == ".db3")
        return FileDbType::SQLite;
    if (ext == ".duckdb" || ext == ".ddb")
        return FileDbType::DuckDB;

    std::FILE* f = std::fopen(std::string(path).c_str(), "rb");
    if (!f)
        return FileDbType::Unknown;

    char buf[16] = {};
    const auto got = std::fread(buf, 1, sizeof(buf), f);
    std::fclose(f);

    if (got >= 4 && static_cast<unsigned char>(buf[0]) == 0x44u && static_cast<unsigned char>(buf[1]) == 0x55u &&
        static_cast<unsigned char>(buf[2]) == 0x43u && static_cast<unsigned char>(buf[3]) == 0x4Bu)
        return FileDbType::DuckDB;

    if (got == 16 && std::memcmp(buf, "SQLite format 3\0", 16) == 0)
        return FileDbType::SQLite;

    return FileDbType::Unknown;
}

/// Adapter name string for a sniffed file type.
[[nodiscard]] inline std::string_view AdapterForDbType(FileDbType t) noexcept
{
    switch (t) {
        case FileDbType::SQLite:
            return "sqlite";
        case FileDbType::DuckDB:
            return "duckdb";
        default:
            return "";
    }
}

/// Encode a parallel (keys, values) pair into the rowData "key=value\n" format.
/// Truncates silently to fit within 2047 chars.
[[nodiscard]] inline std::string EncodeRowData(const std::vector<std::string>& keys,
                                               const std::vector<std::string>& values) noexcept
{
    std::string result;
    result.reserve(256);
    const std::size_t n = std::min(keys.size(), values.size());
    for (std::size_t i = 0; i < n; ++i) {
        std::string line;
        line.reserve(keys[i].size() + 1 + values[i].size() + 1);
        line = keys[i];
        line += '=';
        line += values[i];
        line += '\n';

        if (result.size() + line.size() > 2047u)
            break;
        result += line;
    }
    return result;
}

/// Decode "key=value\n" rowData into (keys, values) vectors.
/// Only splits on the FIRST '=' per line.
[[nodiscard]] inline std::pair<std::vector<std::string>, std::vector<std::string>>
ParseRowData(std::string_view data) noexcept
{
    std::vector<std::string> keys;
    std::vector<std::string> values;

    while (!data.empty()) {
        const auto nl   = data.find('\n');
        const auto line = (nl == std::string_view::npos) ? data : data.substr(0, nl);

        if (!line.empty()) {
            const auto eq = line.find('=');
            if (eq != std::string_view::npos) {
                keys.emplace_back(line.substr(0, eq));
                values.emplace_back(line.substr(eq + 1));
            }
        }

        if (nl == std::string_view::npos)
            break;
        data = data.substr(nl + 1);
    }

    return {std::move(keys), std::move(values)};
}

} // namespace UI
