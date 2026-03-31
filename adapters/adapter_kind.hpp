#pragma once
#include <cstdint>
#include <optional>
#include <string_view>

namespace Adapters {

enum class AdapterKind : std::uint8_t
{
    SQLite,
    CSV,
    Filesystem,
    DuckDB,
    PostgreSQL,
};

[[nodiscard]] constexpr std::string_view name_of(AdapterKind k) noexcept
{
    switch (k) {
        case AdapterKind::SQLite:
            return "sqlite";
        case AdapterKind::CSV:
            return "csv";
        case AdapterKind::Filesystem:
            return "filesystem";
        case AdapterKind::DuckDB:
            return "duckdb";
        case AdapterKind::PostgreSQL:
            return "pgsql";
    }
    return "unknown";
}

[[nodiscard]] inline std::optional<AdapterKind> kind_of(std::string_view name) noexcept
{
    if (name == "sqlite")     return AdapterKind::SQLite;
    if (name == "csv")        return AdapterKind::CSV;
    if (name == "filesystem") return AdapterKind::Filesystem;
    if (name == "duckdb")     return AdapterKind::DuckDB;
    if (name == "pgsql")      return AdapterKind::PostgreSQL;
    return std::nullopt;
}

} // namespace Adapters
