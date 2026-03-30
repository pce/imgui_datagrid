#include <catch2/catch_test_macros.hpp>

#include "adapters/sqlite/sqlite_adapter.hpp"
#include "adapters/data_source.hpp"

// ============================================================
//  Helpers
// ============================================================

// Creates a connected in-memory SQLiteAdapter and seeds it with a simple table.
static Adapters::SQLiteAdapter MakeSeededAdapter()
{
    Adapters::SQLiteAdapter adapter;
    Adapters::ConnectionParams p;
    p.connectionString = ":memory:";

    REQUIRE(adapter.Connect(p).has_value());

    adapter.Execute("CREATE TABLE people ("
                    "  id    INTEGER PRIMARY KEY,"
                    "  name  TEXT    NOT NULL,"
                    "  age   INTEGER,"
                    "  score REAL"
                    ")");

    adapter.Execute("INSERT INTO people VALUES (1, 'Alice',  30, 95.5)");
    adapter.Execute("INSERT INTO people VALUES (2, 'Bob',    25, 87.0)");
    adapter.Execute("INSERT INTO people VALUES (3, 'Charlie',35, 91.2)");
    adapter.Execute("INSERT INTO people VALUES (4, 'Diana',  28, 78.4)");
    adapter.Execute("INSERT INTO people VALUES (5, 'Eve',    22, 99.1)");

    return adapter;
}

// ============================================================
//  Connection lifecycle
// ============================================================

TEST_CASE("SQLiteAdapter: connect to in-memory DB", "[sqlite]")
{
    Adapters::SQLiteAdapter adapter;
    Adapters::ConnectionParams p;
    p.connectionString = ":memory:";

    REQUIRE(adapter.Connect(p).has_value());
    REQUIRE(adapter.IsConnected());
    CHECK(adapter.LastError().empty());
}

TEST_CASE("SQLiteAdapter: connect to non-existent path fails gracefully", "[sqlite]")
{
    Adapters::SQLiteAdapter adapter;
    Adapters::ConnectionParams p;
    p.connectionString = "/no/such/directory/missing.db";
    p.readOnly         = true;   // OPEN_READONLY on a missing file must fail

    REQUIRE_FALSE(adapter.Connect(p).has_value());
    REQUIRE_FALSE(adapter.IsConnected());
    CHECK_FALSE(adapter.LastError().empty());
}

TEST_CASE("SQLiteAdapter: disconnect clears connection", "[sqlite]")
{
    Adapters::SQLiteAdapter adapter;
    Adapters::ConnectionParams p;
    p.connectionString = ":memory:";
    REQUIRE(adapter.Connect(p).has_value());

    adapter.Disconnect();
    CHECK_FALSE(adapter.IsConnected());
}

TEST_CASE("SQLiteAdapter: re-connect is safe", "[sqlite]")
{
    Adapters::SQLiteAdapter adapter;
    Adapters::ConnectionParams p;
    p.connectionString = ":memory:";

    REQUIRE(adapter.Connect(p).has_value());
    REQUIRE(adapter.Connect(p).has_value());   // second Connect — should not crash or leak
    REQUIRE(adapter.IsConnected());
}

// ============================================================
//  AdapterLabel
// ============================================================

TEST_CASE("SQLiteAdapter: AdapterLabel contains SQLite version", "[sqlite]")
{
    Adapters::SQLiteAdapter adapter;
    Adapters::ConnectionParams p;
    p.connectionString = ":memory:";
    REQUIRE(adapter.Connect(p).has_value());

    const std::string label = adapter.AdapterLabel();
    CHECK_FALSE(label.empty());
    // Should contain "SQLite" and a version number like "3.x"
    CHECK(label.find("SQLite") != std::string::npos);
}

TEST_CASE("SQLiteAdapter: AdapterLabel when disconnected", "[sqlite]")
{
    Adapters::SQLiteAdapter adapter;
    const std::string label = adapter.AdapterLabel();
    CHECK_FALSE(label.empty());   // should return a safe fallback string
}

// ============================================================
//  AdapterName / AdapterVersion
// ============================================================

TEST_CASE("SQLiteAdapter: AdapterName is 'sqlite'", "[sqlite]")
{
    Adapters::SQLiteAdapter adapter;
    CHECK(adapter.AdapterName() == "sqlite");
}

TEST_CASE("SQLiteAdapter: AdapterVersion is non-empty", "[sqlite]")
{
    Adapters::SQLiteAdapter adapter;
    CHECK_FALSE(adapter.AdapterVersion().empty());
}

// ============================================================
//  Schema navigation — GetCatalogs
// ============================================================

TEST_CASE("SQLiteAdapter: GetCatalogs returns the connection path", "[sqlite]")
{
    Adapters::SQLiteAdapter adapter;
    Adapters::ConnectionParams p;
    p.connectionString = ":memory:";
    REQUIRE(adapter.Connect(p).has_value());

    const auto cats = adapter.GetCatalogs();
    REQUIRE(cats.size() == 1);
    CHECK(cats[0] == ":memory:");
}

// ============================================================
//  Schema navigation — GetTables
// ============================================================

TEST_CASE("SQLiteAdapter: GetTables on empty DB returns no user tables", "[sqlite]")
{
    Adapters::SQLiteAdapter adapter;
    Adapters::ConnectionParams p;
    p.connectionString = ":memory:";
    REQUIRE(adapter.Connect(p).has_value());

    const auto tables = adapter.GetTables("");
    CHECK(tables.empty());
}

TEST_CASE("SQLiteAdapter: GetTables reflects created tables", "[sqlite]")
{
    Adapters::SQLiteAdapter adapter;
    Adapters::ConnectionParams p;
    p.connectionString = ":memory:";
    REQUIRE(adapter.Connect(p).has_value());

    adapter.Execute("CREATE TABLE t1 (id INTEGER)");
    adapter.Execute("CREATE TABLE t2 (id INTEGER)");

    const auto tables = adapter.GetTables("");
    REQUIRE(tables.size() == 2);

    // Names should be t1 and t2 (order may vary)
    bool foundT1 = false, foundT2 = false;
    for (const auto& t : tables) {
        if (t.name == "t1") foundT1 = true;
        if (t.name == "t2") foundT2 = true;
        CHECK(t.kind == "table");
    }
    CHECK(foundT1);
    CHECK(foundT2);
}

TEST_CASE("SQLiteAdapter: GetTables excludes sqlite_ system tables", "[sqlite]")
{
    Adapters::SQLiteAdapter adapter;
    Adapters::ConnectionParams p;
    p.connectionString = ":memory:";
    REQUIRE(adapter.Connect(p).has_value());

    adapter.Execute("CREATE TABLE real_table (x INTEGER)");

    const auto tables = adapter.GetTables("");
    for (const auto& t : tables)
        CHECK(t.name.find("sqlite_") == std::string::npos);
}

TEST_CASE("SQLiteAdapter: GetTables surfaces views", "[sqlite]")
{
    Adapters::SQLiteAdapter adapter;
    Adapters::ConnectionParams p;
    p.connectionString = ":memory:";
    REQUIRE(adapter.Connect(p).has_value());

    adapter.Execute("CREATE TABLE base (id INTEGER, val TEXT)");
    adapter.Execute("CREATE VIEW myview AS SELECT id FROM base WHERE id > 0");

    const auto tables = adapter.GetTables("");
    bool foundView = false;
    for (const auto& t : tables)
        if (t.name == "myview" && t.kind == "view") foundView = true;
    CHECK(foundView);
}

// ============================================================
//  Schema navigation — GetColumns
// ============================================================

TEST_CASE("SQLiteAdapter: GetColumns returns correct column metadata", "[sqlite]")
{
    auto adapter = MakeSeededAdapter();

    const auto cols = adapter.GetColumns("people");
    REQUIRE(cols.size() == 4);

    CHECK(cols[0].name       == "id");
    CHECK(cols[0].primaryKey == true);

    CHECK(cols[1].name     == "name");
    CHECK(cols[1].nullable == false);   // NOT NULL in DDL

    CHECK(cols[2].name == "age");
    CHECK(cols[3].name == "score");
}

TEST_CASE("SQLiteAdapter: GetColumns returns type names", "[sqlite]")
{
    auto adapter = MakeSeededAdapter();

    const auto cols = adapter.GetColumns("people");
    REQUIRE(cols.size() == 4);

    CHECK(cols[0].typeName == "INTEGER");
    CHECK(cols[1].typeName == "TEXT");
    CHECK(cols[2].typeName == "INTEGER");
    CHECK(cols[3].typeName == "REAL");
}

TEST_CASE("SQLiteAdapter: GetColumns on unknown table returns empty", "[sqlite]")
{
    auto adapter = MakeSeededAdapter();
    CHECK(adapter.GetColumns("no_such_table").empty());
}

// ============================================================
//  ExecuteQuery — basic fetch
// ============================================================

TEST_CASE("SQLiteAdapter: ExecuteQuery returns all rows", "[sqlite]")
{
    auto adapter = MakeSeededAdapter();

    Adapters::DataQuery q;
    q.table    = "people";
    q.pageSize = 100;

    const auto result = adapter.ExecuteQuery(q);
    REQUIRE(result.ok());
    REQUIRE(result.rows.size() == 5);
    CHECK(result.columns.size() == 4);
}

TEST_CASE("SQLiteAdapter: ExecuteQuery result columns match schema", "[sqlite]")
{
    auto adapter = MakeSeededAdapter();

    Adapters::DataQuery q;
    q.table    = "people";
    q.pageSize = 100;

    const auto result = adapter.ExecuteQuery(q);
    REQUIRE(result.ok());
    REQUIRE(result.columns.size() == 4);

    CHECK(result.columns[0].name == "id");
    CHECK(result.columns[1].name == "name");
    CHECK(result.columns[2].name == "age");
    CHECK(result.columns[3].name == "score");
}

TEST_CASE("SQLiteAdapter: ExecuteQuery result values are correct strings", "[sqlite]")
{
    auto adapter = MakeSeededAdapter();

    Adapters::DataQuery q;
    q.table         = "people";
    q.pageSize      = 100;
    q.sortColumn    = "id";
    q.sortAscending = true;

    const auto result = adapter.ExecuteQuery(q);
    REQUIRE(result.ok());
    REQUIRE(result.rows.size() == 5);

    CHECK(result.rows[0][0] == "1");      // id
    CHECK(result.rows[0][1] == "Alice");  // name
    CHECK(result.rows[0][2] == "30");     // age
}

// ============================================================
//  ExecuteQuery — pagination
// ============================================================

TEST_CASE("SQLiteAdapter: pagination page 0", "[sqlite]")
{
    auto adapter = MakeSeededAdapter();

    Adapters::DataQuery q;
    q.table         = "people";
    q.pageSize      = 2;
    q.page          = 0;
    q.sortColumn    = "id";
    q.sortAscending = true;

    const auto result = adapter.ExecuteQuery(q);
    REQUIRE(result.ok());
    REQUIRE(result.rows.size() == 2);
    CHECK(result.rows[0][1] == "Alice");
    CHECK(result.rows[1][1] == "Bob");
}

TEST_CASE("SQLiteAdapter: pagination page 1", "[sqlite]")
{
    auto adapter = MakeSeededAdapter();

    Adapters::DataQuery q;
    q.table         = "people";
    q.pageSize      = 2;
    q.page          = 1;
    q.sortColumn    = "id";
    q.sortAscending = true;

    const auto result = adapter.ExecuteQuery(q);
    REQUIRE(result.ok());
    REQUIRE(result.rows.size() == 2);
    CHECK(result.rows[0][1] == "Charlie");
    CHECK(result.rows[1][1] == "Diana");
}

TEST_CASE("SQLiteAdapter: last page may be partial", "[sqlite]")
{
    auto adapter = MakeSeededAdapter();

    Adapters::DataQuery q;
    q.table    = "people";
    q.pageSize = 3;
    q.page     = 1;   // rows 4–5

    const auto result = adapter.ExecuteQuery(q);
    REQUIRE(result.ok());
    CHECK(result.rows.size() == 2);
}

TEST_CASE("SQLiteAdapter: page beyond end returns empty rows", "[sqlite]")
{
    auto adapter = MakeSeededAdapter();

    Adapters::DataQuery q;
    q.table    = "people";
    q.pageSize = 5;
    q.page     = 99;

    const auto result = adapter.ExecuteQuery(q);
    REQUIRE(result.ok());
    CHECK(result.rows.empty());
}

// ============================================================
//  ExecuteQuery — sort
// ============================================================

TEST_CASE("SQLiteAdapter: sort ascending by name", "[sqlite]")
{
    auto adapter = MakeSeededAdapter();

    Adapters::DataQuery q;
    q.table         = "people";
    q.pageSize      = 100;
    q.sortColumn    = "name";
    q.sortAscending = true;

    const auto result = adapter.ExecuteQuery(q);
    REQUIRE(result.ok());
    REQUIRE(result.rows.size() == 5);

    CHECK(result.rows[0][1] == "Alice");
    CHECK(result.rows[1][1] == "Bob");
    CHECK(result.rows[2][1] == "Charlie");
}

TEST_CASE("SQLiteAdapter: sort descending by age", "[sqlite]")
{
    auto adapter = MakeSeededAdapter();

    Adapters::DataQuery q;
    q.table         = "people";
    q.pageSize      = 100;
    q.sortColumn    = "age";
    q.sortAscending = false;

    const auto result = adapter.ExecuteQuery(q);
    REQUIRE(result.ok());
    REQUIRE(result.rows.size() == 5);

    // Oldest first: Charlie (35) should be first
    CHECK(result.rows[0][1] == "Charlie");
}

// ============================================================
//  ExecuteQuery — exact-match filter
// ============================================================

TEST_CASE("SQLiteAdapter: whereExact filter", "[sqlite]")
{
    auto adapter = MakeSeededAdapter();

    Adapters::DataQuery q;
    q.table               = "people";
    q.pageSize            = 100;
    q.whereExact["name"]  = "Alice";

    const auto result = adapter.ExecuteQuery(q);
    REQUIRE(result.ok());
    REQUIRE(result.rows.size() == 1);
    CHECK(result.rows[0][1] == "Alice");
}

TEST_CASE("SQLiteAdapter: whereExact no match returns empty", "[sqlite]")
{
    auto adapter = MakeSeededAdapter();

    Adapters::DataQuery q;
    q.table               = "people";
    q.pageSize            = 100;
    q.whereExact["name"]  = "Zaphod";

    const auto result = adapter.ExecuteQuery(q);
    REQUIRE(result.ok());
    CHECK(result.rows.empty());
}

// ============================================================
//  ExecuteQuery — substring search
// ============================================================

TEST_CASE("SQLiteAdapter: searchColumn/searchValue LIKE filter", "[sqlite]")
{
    auto adapter = MakeSeededAdapter();

    Adapters::DataQuery q;
    q.table        = "people";
    q.pageSize     = 100;
    q.searchColumn = "name";
    q.searchValue  = "lice";   // matches 'Alice' only (not 'Charlie')

    const auto result = adapter.ExecuteQuery(q);
    REQUIRE(result.ok());
    REQUIRE(result.rows.size() == 1);
    CHECK(result.rows[0][1] == "Alice");
}

// ============================================================
//  CountQuery
// ============================================================

TEST_CASE("SQLiteAdapter: CountQuery returns total rows", "[sqlite]")
{
    auto adapter = MakeSeededAdapter();

    Adapters::DataQuery q;
    q.table = "people";
    CHECK(adapter.CountQuery(q) == 5);
}

TEST_CASE("SQLiteAdapter: CountQuery respects whereExact filter", "[sqlite]")
{
    auto adapter = MakeSeededAdapter();

    // Insert a second Alice to test count
    adapter.Execute("INSERT INTO people VALUES (6, 'Alice', 40, 70.0)");

    Adapters::DataQuery q;
    q.table              = "people";
    q.whereExact["name"] = "Alice";

    CHECK(adapter.CountQuery(q) == 2);
}

TEST_CASE("SQLiteAdapter: CountQuery on empty table is 0", "[sqlite]")
{
    Adapters::SQLiteAdapter adapter;
    Adapters::ConnectionParams p;
    p.connectionString = ":memory:";
    REQUIRE(adapter.Connect(p).has_value());

    adapter.Execute("CREATE TABLE empty_t (x INTEGER)");

    Adapters::DataQuery q;
    q.table = "empty_t";
    CHECK(adapter.CountQuery(q) == 0);
}

// ============================================================
//  Execute — raw SQL
// ============================================================

TEST_CASE("SQLiteAdapter: Execute SELECT returns rows", "[sqlite]")
{
    auto adapter = MakeSeededAdapter();

    const auto result = adapter.Execute("SELECT id, name FROM people ORDER BY id");
    REQUIRE(result.ok());
    REQUIRE(result.rows.size() == 5);
    CHECK(result.columns.size() == 2);
    CHECK(result.columns[0].name == "id");
    CHECK(result.columns[1].name == "name");
    CHECK(result.rows[0][1] == "Alice");
}

TEST_CASE("SQLiteAdapter: Execute CREATE TABLE returns ok with 0 rows", "[sqlite]")
{
    Adapters::SQLiteAdapter adapter;
    Adapters::ConnectionParams p;
    p.connectionString = ":memory:";
    REQUIRE(adapter.Connect(p).has_value());

    const auto result = adapter.Execute("CREATE TABLE foo (bar TEXT)");
    REQUIRE(result.ok());
    CHECK(result.rows.empty());
}

TEST_CASE("SQLiteAdapter: Execute INSERT reflects rowsAffected", "[sqlite]")
{
    Adapters::SQLiteAdapter adapter;
    Adapters::ConnectionParams p;
    p.connectionString = ":memory:";
    REQUIRE(adapter.Connect(p).has_value());

    adapter.Execute("CREATE TABLE t (v TEXT)");
    const auto result = adapter.Execute("INSERT INTO t VALUES ('hello')");
    REQUIRE(result.ok());
    CHECK(result.rowsAffected == 1);
}

TEST_CASE("SQLiteAdapter: Execute invalid SQL returns error result", "[sqlite]")
{
    Adapters::SQLiteAdapter adapter;
    Adapters::ConnectionParams p;
    p.connectionString = ":memory:";
    REQUIRE(adapter.Connect(p).has_value());

    const auto result = adapter.Execute("THIS IS NOT VALID SQL !!!!");
    CHECK_FALSE(result.ok());
    CHECK_FALSE(result.error.empty());
}

TEST_CASE("SQLiteAdapter: Execute on disconnected adapter returns error", "[sqlite]")
{
    Adapters::SQLiteAdapter adapter;   // never connected
    const auto result = adapter.Execute("SELECT 1");
    CHECK_FALSE(result.ok());
}

// ============================================================
//  NULL handling
// ============================================================

TEST_CASE("SQLiteAdapter: NULL column values become empty strings", "[sqlite]")
{
    Adapters::SQLiteAdapter adapter;
    Adapters::ConnectionParams p;
    p.connectionString = ":memory:";
    REQUIRE(adapter.Connect(p).has_value());

    adapter.Execute("CREATE TABLE nullable_t (id INTEGER, val TEXT)");
    adapter.Execute("INSERT INTO nullable_t VALUES (1, NULL)");
    adapter.Execute("INSERT INTO nullable_t VALUES (2, 'present')");

    Adapters::DataQuery q;
    q.table         = "nullable_t";
    q.pageSize      = 100;
    q.sortColumn    = "id";
    q.sortAscending = true;

    const auto result = adapter.ExecuteQuery(q);
    REQUIRE(result.ok());
    REQUIRE(result.rows.size() == 2);

    CHECK(result.rows[0][1] == "");        // NULL → empty string
    CHECK(result.rows[1][1] == "present");
}

// ============================================================
//  Execution timing
// ============================================================

TEST_CASE("SQLiteAdapter: executionMs is populated", "[sqlite]")
{
    auto adapter = MakeSeededAdapter();

    const auto result = adapter.Execute("SELECT * FROM people");
    REQUIRE(result.ok());
    CHECK(result.executionMs >= 0.0);
}
