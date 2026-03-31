#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

// Including the adapter headers forces their RegisterAdapter<T> static
// initialisers to run before the tests execute, exactly as they would
// in the real application binary.
#include "adapters/adapter_registry.hpp"
#include "adapters/csv/csv_adapter.hpp"
#include "adapters/sqlite/sqlite_adapter.hpp"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

static fs::path WriteTempCsv(const std::string& name, const std::string& content)
{
    const fs::path p = fs::temp_directory_path() / name;
    std::ofstream  f(p, std::ios::out | std::ios::trunc);
    f << content;
    return p;
}

struct TempFile
{
    fs::path path;
    TempFile(const std::string& name, const std::string& content) : path(WriteTempCsv(name, content)) {}
    ~TempFile()
    {
        std::error_code ec;
        fs::remove(path, ec);
    }
    std::string str() const { return path.string(); }
};

TEST_CASE("AdapterRegistry: sqlite adapter is self-registered", "[registry]")
{
    REQUIRE(Adapters::AdapterRegistry::Has("sqlite"));
}

TEST_CASE("AdapterRegistry: csv adapter is self-registered", "[registry]")
{
    REQUIRE(Adapters::AdapterRegistry::Has("csv"));
}

TEST_CASE("AdapterRegistry: unknown adapter name returns false", "[registry]")
{
    CHECK_FALSE(Adapters::AdapterRegistry::Has("nonexistent_adapter_xyz"));
    CHECK_FALSE(Adapters::AdapterRegistry::Has(""));
    CHECK_FALSE(Adapters::AdapterRegistry::Has("SQLITE")); // case-sensitive
}

TEST_CASE("AdapterRegistry: RegisteredAdapters includes sqlite and csv", "[registry]")
{
    const auto names = Adapters::AdapterRegistry::RegisteredAdapters();

    bool hasSqlite = false;
    bool hasCsv    = false;
    for (const auto& n : names) {
        if (n == "sqlite")
            hasSqlite = true;
        if (n == "csv")
            hasCsv = true;
    }

    CHECK(hasSqlite);
    CHECK(hasCsv);
}

TEST_CASE("AdapterRegistry: RegisteredAdapters returns sorted list", "[registry]")
{
    const auto names = Adapters::AdapterRegistry::RegisteredAdapters();
    for (size_t i = 1; i < names.size(); ++i)
        CHECK(names[i - 1] <= names[i]); // lexicographically non-decreasing
}

TEST_CASE("AdapterRegistry: Count is at least 2 (sqlite + csv)", "[registry]")
{
    CHECK(Adapters::AdapterRegistry::Count() >= 2);
}

TEST_CASE("AdapterRegistry: Create(sqlite) returns a non-null, unconnected adapter", "[registry]")
{
    auto ds = Adapters::AdapterRegistry::Create("sqlite");

    REQUIRE(ds != nullptr);
    CHECK_FALSE(ds->IsConnected());
    CHECK(ds->AdapterName() == "sqlite");
}

TEST_CASE("AdapterRegistry: Create(csv) returns a non-null, unconnected adapter", "[registry]")
{
    auto ds = Adapters::AdapterRegistry::Create("csv");

    REQUIRE(ds != nullptr);
    CHECK_FALSE(ds->IsConnected());
    CHECK(ds->AdapterName() == "csv");
}

TEST_CASE("AdapterRegistry: Create with unknown name returns nullptr", "[registry]")
{
    auto ds = Adapters::AdapterRegistry::Create("does_not_exist");
    CHECK(ds == nullptr);
}

TEST_CASE("AdapterRegistry: Create returns a fresh instance each call", "[registry]")
{
    auto a = Adapters::AdapterRegistry::Create("sqlite");
    auto b = Adapters::AdapterRegistry::Create("sqlite");

    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    // Two distinct heap allocations
    CHECK(a.get() != b.get());
}

TEST_CASE("AdapterRegistry: CreateConnected with :memory: returns connected sqlite adapter", "[registry]")
{
    Adapters::ConnectionParams params;
    params.adapterName      = "sqlite";
    params.connectionString = ":memory:";

    auto ds = Adapters::AdapterRegistry::CreateConnected("sqlite", params);

    REQUIRE(ds.has_value());
    CHECK((*ds)->IsConnected());
    CHECK((*ds)->AdapterName() == "sqlite");
}

TEST_CASE("AdapterRegistry: CreateConnected error is empty on success", "[registry]")
{
    Adapters::ConnectionParams params;
    params.adapterName      = "sqlite";
    params.connectionString = ":memory:";

    auto ds = Adapters::AdapterRegistry::CreateConnected("sqlite", params);

    REQUIRE(ds.has_value());
}

TEST_CASE("AdapterRegistry: CreateConnected with bad sqlite path returns nullptr + error", "[registry]")
{
    Adapters::ConnectionParams params;
    params.adapterName      = "sqlite";
    params.connectionString = "/no/such/directory/that/cannot/exist/db.sqlite";

    auto ds = Adapters::AdapterRegistry::CreateConnected("sqlite", params);

    CHECK_FALSE(ds.has_value());
    CHECK_FALSE(ds.error().empty());
}

TEST_CASE("AdapterRegistry: CreateConnected nullptr outError is safe", "[registry]")
{
    Adapters::ConnectionParams params;
    params.adapterName      = "sqlite";
    params.connectionString = "/definitely/not/a/valid/path.db";

    // Must not crash when outError is nullptr
    REQUIRE_NOTHROW(Adapters::AdapterRegistry::CreateConnected("sqlite", params));
}

TEST_CASE("AdapterRegistry: CreateConnected with valid csv file returns connected adapter", "[registry]")
{
    TempFile tmp("reg_test.csv", "id,name\n1,Alice\n2,Bob\n");

    Adapters::ConnectionParams params;
    params.adapterName      = "csv";
    params.connectionString = tmp.str();

    auto ds = Adapters::AdapterRegistry::CreateConnected("csv", params);

    REQUIRE(ds.has_value());
    CHECK((*ds)->IsConnected());
    CHECK((*ds)->AdapterName() == "csv");
}

TEST_CASE("AdapterRegistry: CreateConnected with missing csv file returns nullptr + error", "[registry]")
{
    Adapters::ConnectionParams params;
    params.adapterName      = "csv";
    params.connectionString = "/no/such/path/missing_xyz.csv";

    auto ds = Adapters::AdapterRegistry::CreateConnected("csv", params);

    CHECK_FALSE(ds.has_value());
    CHECK_FALSE(ds.error().empty());
}

TEST_CASE("AdapterRegistry: CreateConnected with unknown adapter name returns nullptr", "[registry]")
{
    Adapters::ConnectionParams params;
    params.adapterName      = "postgres";
    params.connectionString = "host=localhost dbname=test";

    auto ds = Adapters::AdapterRegistry::CreateConnected("postgres", params);

    CHECK_FALSE(ds.has_value());
    CHECK_FALSE(ds.error().empty());
    CHECK_THAT(ds.error(), Catch::Matchers::ContainsSubstring("postgres"));
}

TEST_CASE("AdapterRegistry: Register adds a new factory at runtime", "[registry]")
{
    const std::string testKey = "test_runtime_adapter_xyz";

    // Should not exist before registration
    REQUIRE_FALSE(Adapters::AdapterRegistry::Has(testKey));

    // Register a factory that creates a SQLiteAdapter under a custom name
    Adapters::AdapterRegistry::Register(
        testKey, []() -> Adapters::DataSourcePtr { return std::make_unique<Adapters::SQLiteAdapter>(); });

    REQUIRE(Adapters::AdapterRegistry::Has(testKey));

    auto ds = Adapters::AdapterRegistry::Create(testKey);
    REQUIRE(ds != nullptr);

    // Clean up: re-register with nullptr-returning lambda to avoid
    // pollution across tests (map entry stays but factory is safe to call)
    Adapters::AdapterRegistry::Register(testKey, []() -> Adapters::DataSourcePtr { return nullptr; });
}

TEST_CASE("AdapterRegistry: Register overwrites an existing factory", "[registry]")
{
    const std::string overwriteKey = "test_overwrite_adapter_xyz";

    // First factory: returns a SQLiteAdapter
    Adapters::AdapterRegistry::Register(
        overwriteKey, []() -> Adapters::DataSourcePtr { return std::make_unique<Adapters::SQLiteAdapter>(); });

    auto first = Adapters::AdapterRegistry::Create(overwriteKey);
    REQUIRE(first != nullptr);
    CHECK(first->AdapterName() == "sqlite");

    // Second factory: returns a CsvAdapter
    Adapters::AdapterRegistry::Register(
        overwriteKey, []() -> Adapters::DataSourcePtr { return std::make_unique<Adapters::CsvAdapter>(); });

    auto second = Adapters::AdapterRegistry::Create(overwriteKey);
    REQUIRE(second != nullptr);
    CHECK(second->AdapterName() == "csv");

    // Clean up
    Adapters::AdapterRegistry::Register(overwriteKey, []() -> Adapters::DataSourcePtr { return nullptr; });
}

TEST_CASE("AdapterRegistry: full round-trip sqlite create→connect→query", "[registry][integration]")
{
    Adapters::ConnectionParams params;
    params.adapterName      = "sqlite";
    params.connectionString = ":memory:";

    auto ds = Adapters::AdapterRegistry::CreateConnected("sqlite", params);
    REQUIRE(ds.has_value());
    REQUIRE((*ds)->IsConnected());

    // Create a table and insert a row
    auto createResult = (*ds)->Execute("CREATE TABLE items (id INTEGER PRIMARY KEY, label TEXT NOT NULL)");
    CHECK(createResult.ok());

    auto insertResult = (*ds)->Execute("INSERT INTO items VALUES (1, 'hello')");
    CHECK(insertResult.ok());

    // Query back via structured path
    Adapters::DataQuery q;
    q.table    = "items";
    q.pageSize = 10;

    auto qr = (*ds)->ExecuteQuery(q);
    REQUIRE(qr.ok());
    REQUIRE(qr.rows.size() == 1);
    CHECK(qr.rows[0][0] == "1");
    CHECK(qr.rows[0][1] == "hello");

    CHECK((*ds)->CountQuery(q) == 1);
}

TEST_CASE("AdapterRegistry: full round-trip csv create→connect→query", "[registry][integration]")
{
    TempFile tmp("reg_roundtrip.csv",
                 "product,price,in_stock\n"
                 "Widget,9.99,true\n"
                 "Gadget,49.99,false\n"
                 "Doohickey,1.99,true\n");

    Adapters::ConnectionParams params;
    params.adapterName      = "csv";
    params.connectionString = tmp.str();

    auto ds = Adapters::AdapterRegistry::CreateConnected("csv", params);
    REQUIRE(ds.has_value());
    REQUIRE((*ds)->IsConnected());

    const auto tables = (*ds)->GetTables("");
    REQUIRE(tables.size() == 1);
    CHECK(tables[0].name == "reg_roundtrip");

    Adapters::DataQuery q;
    q.table    = "reg_roundtrip";
    q.pageSize = 100;

    auto qr = (*ds)->ExecuteQuery(q);
    REQUIRE(qr.ok());
    CHECK(qr.rows.size() == 3);
    CHECK((*ds)->CountQuery(q) == 3);
}
