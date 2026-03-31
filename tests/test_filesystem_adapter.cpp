#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "adapters/data_source.hpp"
#include "adapters/filesystem/filesystem_adapter.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;

struct TempDir
{
    fs::path dir;

    static void writeFile(const fs::path& p, std::size_t bytes)
    {
        std::ofstream f(p, std::ios::binary);
        for (std::size_t i = 0; i < bytes; ++i)
            f.put('x');
    }

    TempDir()
    {
        static std::atomic<int> s_id{0};
        dir = fs::temp_directory_path() / ("dg_fs_" + std::to_string(getpid()) + "_" + std::to_string(++s_id));
        fs::create_directories(dir / "subdir");
        writeFile(dir / "main.cpp", 400);
        writeFile(dir / "utils.hpp", 800);
        writeFile(dir / "readme.txt", 1200);
        writeFile(dir / "image.png", 50000);
        writeFile(dir / ".hidden_file", 10);
        writeFile(dir / "subdir" / "nested.cpp", 300);
        writeFile(dir / "subdir" / "deep.hpp", 100);
    }

    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

TEST_CASE("FilesystemAdapter: connect to valid dir", "[fs_adapter]")
{
    TempDir                     tmp;
    Adapters::FilesystemAdapter adapter;
    Adapters::ConnectionParams  p;
    p.connectionString = tmp.dir.string();

    REQUIRE(adapter.Connect(p).has_value());
    CHECK(adapter.IsConnected());
    CHECK(adapter.LastError().empty());
}

TEST_CASE("FilesystemAdapter: connect to non-existent dir", "[fs_adapter]")
{
    Adapters::FilesystemAdapter adapter;
    Adapters::ConnectionParams  p;
    p.connectionString = "/no/such/path/dg_fs_missing_xyz";

    CHECK_FALSE(adapter.Connect(p).has_value());
    CHECK_FALSE(adapter.IsConnected());
    CHECK_FALSE(adapter.LastError().empty());
}

TEST_CASE("FilesystemAdapter: Disconnect resets state", "[fs_adapter]")
{
    TempDir                     tmp;
    Adapters::FilesystemAdapter adapter;
    Adapters::ConnectionParams  p;
    p.connectionString = tmp.dir.string();

    REQUIRE(adapter.Connect(p).has_value());
    REQUIRE(adapter.IsConnected());

    adapter.Disconnect();
    CHECK_FALSE(adapter.IsConnected());
}

TEST_CASE("FilesystemAdapter: AdapterLabel contains path", "[fs_adapter]")
{
    TempDir                     tmp;
    Adapters::FilesystemAdapter adapter;
    Adapters::ConnectionParams  p;
    p.connectionString = tmp.dir.string();
    REQUIRE(adapter.Connect(p).has_value());

    const std::string label = adapter.AdapterLabel();
    // Label should contain either the path or "Filesystem"
    const bool containsPath =
        label.find(tmp.dir.filename().string()) != std::string::npos || label.find("Filesystem") != std::string::npos;
    CHECK(containsPath);
}

TEST_CASE("FilesystemAdapter: ListTables returns entries", "[fs_adapter]")
{
    TempDir                     tmp;
    Adapters::FilesystemAdapter adapter;
    Adapters::ConnectionParams  p;
    p.connectionString = tmp.dir.string();
    REQUIRE(adapter.Connect(p).has_value());

    // GetTables(catalog) returns subdirectories of the given path
    auto tables = adapter.GetTables(tmp.dir.string());
    REQUIRE_FALSE(tables.empty());
    // subdir is the only direct subdirectory
    bool foundSubdir = false;
    for (const auto& t : tables)
        if (t.name.find("subdir") != std::string::npos)
            foundSubdir = true;
    CHECK(foundSubdir);
}

TEST_CASE("FilesystemAdapter: GetColumns returns filesystem columns", "[fs_adapter]")
{
    TempDir                     tmp;
    Adapters::FilesystemAdapter adapter;
    Adapters::ConnectionParams  p;
    p.connectionString = tmp.dir.string();
    REQUIRE(adapter.Connect(p).has_value());

    auto cols = adapter.GetColumns(tmp.dir.string());
    REQUIRE(cols.size() >= 3);

    bool hasName = false, hasKind = false, hasSize = false;
    for (const auto& c : cols) {
        if (c.name == "name")
            hasName = true;
        if (c.name == "kind")
            hasKind = true;
        if (c.name == "size")
            hasSize = true;
    }
    CHECK(hasName);
    CHECK(hasKind);
    CHECK(hasSize);
}

TEST_CASE("FilesystemAdapter: Execute SQL SELECT *", "[fs_adapter]")
{
    TempDir                     tmp;
    Adapters::FilesystemAdapter adapter;
    Adapters::ConnectionParams  p;
    p.connectionString = tmp.dir.string();
    REQUIRE(adapter.Connect(p).has_value());

    std::string sql    = "SELECT * FROM '" + tmp.dir.string() + "'";
    auto        result = adapter.Execute(sql);
    REQUIRE(result.ok());
    CHECK_FALSE(result.rows.empty());
}

TEST_CASE("FilesystemAdapter: Execute SQL WHERE extension", "[fs_adapter]")
{
    TempDir                     tmp;
    Adapters::FilesystemAdapter adapter;
    Adapters::ConnectionParams  p;
    p.connectionString = tmp.dir.string();
    REQUIRE(adapter.Connect(p).has_value());

    std::string allSql = "SELECT * FROM '" + tmp.dir.string() + "'";
    std::string hppSql = "SELECT * FROM '" + tmp.dir.string() + "' WHERE extension = '.hpp'";

    auto allResult = adapter.Execute(allSql);
    auto hppResult = adapter.Execute(hppSql);

    REQUIRE(allResult.ok());
    REQUIRE(hppResult.ok());
    // Filtering to .hpp returns fewer rows than SELECT *
    CHECK(hppResult.rows.size() < allResult.rows.size());
    // Only utils.hpp at root (non-recursive)
    CHECK(hppResult.rows.size() == 1);
}

TEST_CASE("FilesystemAdapter: Execute SQL ORDER BY size DESC LIMIT 1", "[fs_adapter]")
{
    TempDir                     tmp;
    Adapters::FilesystemAdapter adapter;
    Adapters::ConnectionParams  p;
    p.connectionString = tmp.dir.string();
    REQUIRE(adapter.Connect(p).has_value());

    std::string sql    = "SELECT * FROM '" + tmp.dir.string() + "' ORDER BY size DESC LIMIT 1";
    auto        result = adapter.Execute(sql);
    REQUIRE(result.ok());
    REQUIRE(result.rows.size() == 1);
    // Column 0 is "name"; image.png is the largest file
    CHECK(result.rows[0][0] == "image.png");
}

TEST_CASE("FilesystemAdapter: readOnly mode SupportsWrite false", "[fs_adapter]")
{
    TempDir                     tmp;
    Adapters::FilesystemAdapter adapter;
    Adapters::ConnectionParams  p;
    p.connectionString = tmp.dir.string();
    p.readOnly         = true;

    REQUIRE(adapter.Connect(p).has_value());
    CHECK(adapter.IsConnected());
    CHECK_FALSE(adapter.SupportsWrite());
}

TEST_CASE("FilesystemAdapter: AdapterLabel shows read-only suffix", "[fs_adapter]")
{
    TempDir                     tmp;
    Adapters::FilesystemAdapter adapter;
    Adapters::ConnectionParams  p;
    p.connectionString = tmp.dir.string();
    p.readOnly         = true;

    REQUIRE(adapter.Connect(p).has_value());
    CHECK_THAT(adapter.AdapterLabel(), Catch::Matchers::ContainsSubstring("read-only"));
}

TEST_CASE("FilesystemAdapter: not connected Execute returns error", "[fs_adapter]")
{
    Adapters::FilesystemAdapter adapter; // not connected
    auto                        result = adapter.Execute("SELECT * FROM '/some/path'");
    CHECK_FALSE(result.ok());
    CHECK_FALSE(result.error.empty());
}
