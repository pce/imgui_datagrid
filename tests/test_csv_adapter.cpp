#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>

#include "adapters/csv/csv_adapter.hpp"
#include "adapters/data_source.hpp"

#include <filesystem>
#include <fstream>
#include <string>

using namespace datagrid::adapters;

namespace fs = std::filesystem;

static fs::path WriteTempCsv(const std::string& name, const std::string& content)
{
    const fs::path p = fs::temp_directory_path() / name;
    std::ofstream  f(p, std::ios::out | std::ios::trunc);
    f << content;
    return p;
}

// RAII temp-file guard
struct TempFile
{
    fs::path    path;
    std::string pathStr; // cached so c_str() doesn't point into a temporary
    explicit TempFile(const std::string& name, const std::string& content)
        : path(WriteTempCsv(name, content)), pathStr(path.string())
    {}
    ~TempFile()
    {
        std::error_code ec;
        fs::remove(path, ec);
    }
    const char* c_str() const { return pathStr.c_str(); }
    std::string str() const { return pathStr; }
};

TEST_CASE("CsvAdapter: connect to valid file", "[csv]")
{
    TempFile tmp("dg_test_basic.csv",
                 "id,name,score\n"
                 "1,Alice,95.5\n"
                 "2,Bob,87.0\n"
                 "3,Charlie,91.2\n");

    CsvAdapter       adapter;
    ConnectionParams p;
    p.connectionString = tmp.str();

    REQUIRE(adapter.Connect(p).has_value());
    REQUIRE(adapter.IsConnected());
    REQUIRE(adapter.LastError().empty());
}

TEST_CASE("CsvAdapter: connect to non-existent file", "[csv]")
{
    CsvAdapter       adapter;
    ConnectionParams p;
    p.connectionString = "/no/such/path/missing_file_xyz.csv";

    REQUIRE_FALSE(adapter.Connect(p).has_value());
    REQUIRE_FALSE(adapter.IsConnected());
    REQUIRE_FALSE(adapter.LastError().empty());
}

TEST_CASE("CsvAdapter: disconnect clears state", "[csv]")
{
    TempFile                   tmp("dg_test_disconnect.csv", "a,b\n1,2\n");
    CsvAdapter       adapter;
    ConnectionParams p;
    p.connectionString = tmp.str();

    REQUIRE(adapter.Connect(p).has_value());
    adapter.Disconnect();
    REQUIRE_FALSE(adapter.IsConnected());
}

TEST_CASE("CsvAdapter: GetTables returns stem name", "[csv]")
{
    TempFile                   tmp("my_records.csv", "x,y\n1,2\n");
    CsvAdapter       adapter;
    ConnectionParams p;
    p.connectionString = tmp.str();
    REQUIRE(adapter.Connect(p).has_value());

    const auto tables = adapter.GetTables("");
    REQUIRE(tables.size() == 1);
    CHECK(tables[0].name == "my_records");
    CHECK(tables[0].kind == "csv");
}

TEST_CASE("CsvAdapter: GetColumns returns header row", "[csv]")
{
    TempFile tmp("dg_test_cols.csv",
                 "id,name,score\n"
                 "1,Alice,95.5\n");

    CsvAdapter       adapter;
    ConnectionParams p;
    p.connectionString = tmp.str();
    REQUIRE(adapter.Connect(p).has_value());

    const auto cols = adapter.GetColumns("dg_test_cols");
    REQUIRE(cols.size() == 3);
    CHECK(cols[0].name == "id");
    CHECK(cols[1].name == "name");
    CHECK(cols[2].name == "score");
}

TEST_CASE("CsvAdapter: GetColumns on unknown table returns empty", "[csv]")
{
    TempFile                   tmp("dg_test_unknowntbl.csv", "a,b\n1,2\n");
    CsvAdapter       adapter;
    ConnectionParams p;
    p.connectionString = tmp.str();
    REQUIRE(adapter.Connect(p).has_value());

    CHECK(adapter.GetColumns("no_such_table").empty());
}

TEST_CASE("CsvAdapter: GetCatalogs returns file path", "[csv]")
{
    TempFile                   tmp("dg_test_catalogs.csv", "a\n1\n");
    CsvAdapter       adapter;
    ConnectionParams p;
    p.connectionString = tmp.str();
    REQUIRE(adapter.Connect(p).has_value());

    const auto cats = adapter.GetCatalogs();
    REQUIRE(cats.size() == 1);
    CHECK(cats[0] == tmp.str());
}

TEST_CASE("CsvAdapter: AdapterLabel reflects file and row count", "[csv]")
{
    TempFile tmp("dg_test_label.csv",
                 "col\n"
                 "a\n"
                 "b\n"
                 "c\n");

    CsvAdapter       adapter;
    ConnectionParams p;
    p.connectionString = tmp.str();
    REQUIRE(adapter.Connect(p).has_value());

    const std::string label = adapter.AdapterLabel();
    CHECK_THAT(label, Catch::Matchers::ContainsSubstring("3 rows"));
}

TEST_CASE("CsvAdapter: ExecuteQuery returns all rows unpaged", "[csv]")
{
    TempFile tmp("dg_test_query.csv",
                 "id,name\n"
                 "1,Alice\n"
                 "2,Bob\n"
                 "3,Charlie\n");

    CsvAdapter       adapter;
    ConnectionParams p;
    p.connectionString = tmp.str();
    REQUIRE(adapter.Connect(p).has_value());

    DataQuery q;
    q.table    = "dg_test_query";
    q.pageSize = 100;
    q.page     = 0;

    const auto result = adapter.ExecuteQuery(q);
    REQUIRE(result.ok());
    REQUIRE(result.rows.size() == 3);
    CHECK(result.columns.size() == 2);
}

TEST_CASE("CsvAdapter: ExecuteQuery row values match CSV content", "[csv]")
{
    TempFile tmp("dg_test_values.csv",
                 "name,score\n"
                 "Alice,95\n"
                 "Bob,87\n");

    CsvAdapter       adapter;
    ConnectionParams p;
    p.connectionString = tmp.str();
    REQUIRE(adapter.Connect(p));

    DataQuery q;
    q.table    = "dg_test_values";
    q.pageSize = 100;

    const auto result = adapter.ExecuteQuery(q);
    REQUIRE(result.ok());
    REQUIRE(result.rows.size() == 2);

    CHECK(result.rows[0][0] == "Alice");
    CHECK(result.rows[0][1] == "95");
    CHECK(result.rows[1][0] == "Bob");
    CHECK(result.rows[1][1] == "87");
}

TEST_CASE("CsvAdapter: pagination returns correct page slice", "[csv]")
{
    TempFile tmp("dg_test_page.csv",
                 "n\n"
                 "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n");

    CsvAdapter       adapter;
    ConnectionParams p;
    p.connectionString = tmp.str();
    REQUIRE(adapter.Connect(p));

    DataQuery q;
    q.table    = "dg_test_page";
    q.pageSize = 3;

    // Page 0 → rows 1,2,3
    q.page  = 0;
    auto r0 = adapter.ExecuteQuery(q);
    REQUIRE(r0.ok());
    REQUIRE(r0.rows.size() == 3);
    CHECK(r0.rows[0][0] == "1");
    CHECK(r0.rows[2][0] == "3");

    // Page 1 → rows 4,5,6
    q.page  = 1;
    auto r1 = adapter.ExecuteQuery(q);
    REQUIRE(r1.ok());
    REQUIRE(r1.rows.size() == 3);
    CHECK(r1.rows[0][0] == "4");

    // Page 3 → row 10 only
    q.page  = 3;
    auto r3 = adapter.ExecuteQuery(q);
    REQUIRE(r3.ok());
    REQUIRE(r3.rows.size() == 1);
    CHECK(r3.rows[0][0] == "10");

    // Page 4 → beyond the data
    q.page  = 4;
    auto r4 = adapter.ExecuteQuery(q);
    REQUIRE(r4.ok());
    CHECK(r4.rows.empty());
}

TEST_CASE("CsvAdapter: whereExact filter returns matching rows only", "[csv]")
{
    TempFile tmp("dg_test_filter.csv",
                 "city,pop\n"
                 "Berlin,3.7\n"
                 "Paris,2.1\n"
                 "Berlin,3.7\n"
                 "Rome,4.3\n");

    CsvAdapter       adapter;
    ConnectionParams p;
    p.connectionString = tmp.str();
    REQUIRE(adapter.Connect(p));

    DataQuery q;
    q.table              = "dg_test_filter";
    q.pageSize           = 100;
    q.whereExact["city"] = "Berlin";

    const auto result = adapter.ExecuteQuery(q);
    REQUIRE(result.ok());
    REQUIRE(result.rows.size() == 2);
    for (const auto& row : result.rows)
        CHECK(row[0] == "Berlin");
}

TEST_CASE("CsvAdapter: whereExact with no matching rows returns empty", "[csv]")
{
    TempFile tmp("dg_test_nomatch.csv", "city\nBerlin\nParis\n");

    CsvAdapter       adapter;
    ConnectionParams p;
    p.connectionString = tmp.str();
    REQUIRE(adapter.Connect(p));

    DataQuery q;
    q.table              = "dg_test_nomatch";
    q.pageSize           = 100;
    q.whereExact["city"] = "Tokyo";

    const auto result = adapter.ExecuteQuery(q);
    REQUIRE(result.ok());
    CHECK(result.rows.empty());
}

TEST_CASE("CsvAdapter: searchColumn/searchValue does case-insensitive LIKE", "[csv]")
{
    TempFile tmp("dg_test_search.csv",
                 "name\n"
                 "Alice Smith\n"
                 "Bob Jones\n"
                 "Alice Cooper\n"
                 "Dave\n");

    CsvAdapter       adapter;
    ConnectionParams p;
    p.connectionString = tmp.str();
    REQUIRE(adapter.Connect(p));

    DataQuery q;
    q.table        = "dg_test_search";
    q.pageSize     = 100;
    q.searchColumn = "name";
    q.searchValue  = "alice"; // lower-case — must match case-insensitively

    const auto result = adapter.ExecuteQuery(q);
    REQUIRE(result.ok());
    REQUIRE(result.rows.size() == 2);
    CHECK(result.rows[0][0] == "Alice Smith");
    CHECK(result.rows[1][0] == "Alice Cooper");
}

TEST_CASE("CsvAdapter: sort ascending", "[csv]")
{
    TempFile tmp("dg_test_sort_asc.csv", "n\n30\n10\n20\n");

    CsvAdapter       adapter;
    ConnectionParams p;
    p.connectionString = tmp.str();
    REQUIRE(adapter.Connect(p));

    DataQuery q;
    q.table         = "dg_test_sort_asc";
    q.pageSize      = 100;
    q.sortColumn    = "n";
    q.sortAscending = true;

    const auto result = adapter.ExecuteQuery(q);
    REQUIRE(result.ok());
    REQUIRE(result.rows.size() == 3);
    // Numeric sort: 10 < 20 < 30
    CHECK(result.rows[0][0] == "10");
    CHECK(result.rows[1][0] == "20");
    CHECK(result.rows[2][0] == "30");
}

TEST_CASE("CsvAdapter: sort descending", "[csv]")
{
    TempFile tmp("dg_test_sort_desc.csv", "n\n30\n10\n20\n");

    CsvAdapter       adapter;
    ConnectionParams p;
    p.connectionString = tmp.str();
    REQUIRE(adapter.Connect(p));

    DataQuery q;
    q.table         = "dg_test_sort_desc";
    q.pageSize      = 100;
    q.sortColumn    = "n";
    q.sortAscending = false;

    const auto result = adapter.ExecuteQuery(q);
    REQUIRE(result.ok());
    REQUIRE(result.rows.size() == 3);
    CHECK(result.rows[0][0] == "30");
    CHECK(result.rows[2][0] == "10");
}

TEST_CASE("CsvAdapter: lexicographic sort on text column", "[csv]")
{
    TempFile tmp("dg_test_sort_lex.csv", "name\nCharlie\nAlice\nBob\n");

    CsvAdapter       adapter;
    ConnectionParams p;
    p.connectionString = tmp.str();
    REQUIRE(adapter.Connect(p));

    DataQuery q;
    q.table         = "dg_test_sort_lex";
    q.pageSize      = 100;
    q.sortColumn    = "name";
    q.sortAscending = true;

    const auto result = adapter.ExecuteQuery(q);
    REQUIRE(result.ok());
    REQUIRE(result.rows.size() == 3);
    CHECK(result.rows[0][0] == "Alice");
    CHECK(result.rows[1][0] == "Bob");
    CHECK(result.rows[2][0] == "Charlie");
}

TEST_CASE("CsvAdapter: CountQuery matches total row count", "[csv]")
{
    TempFile tmp("dg_test_count.csv", "x\n1\n2\n3\n4\n5\n");

    CsvAdapter       adapter;
    ConnectionParams p;
    p.connectionString = tmp.str();
    REQUIRE(adapter.Connect(p));

    DataQuery q;
    q.table = "dg_test_count";

    CHECK(adapter.CountQuery(q) == 5);
}

TEST_CASE("CsvAdapter: CountQuery respects whereExact filter", "[csv]")
{
    TempFile tmp("dg_test_count_filter.csv", "kind\napple\nbanana\napple\norange\napple\n");

    CsvAdapter       adapter;
    ConnectionParams p;
    p.connectionString = tmp.str();
    REQUIRE(adapter.Connect(p));

    DataQuery q;
    q.table              = "dg_test_count_filter";
    q.whereExact["kind"] = "apple";

    CHECK(adapter.CountQuery(q) == 3);
}

TEST_CASE("CsvAdapter: Execute(sql) returns an error result", "[csv]")
{
    TempFile tmp("dg_test_nosql.csv", "a\n1\n");

    CsvAdapter       adapter;
    ConnectionParams p;
    p.connectionString = tmp.str();
    REQUIRE(adapter.Connect(p));

    const auto result = adapter.Execute("SELECT * FROM anything");
    CHECK_FALSE(result.ok());
    CHECK_FALSE(result.error.empty());
}

TEST_CASE("CsvAdapter: quoted fields with embedded commas are parsed correctly", "[csv]")
{
    TempFile tmp("dg_test_quoted.csv",
                 "name,address\n"
                 "Alice,\"123 Main St, Apt 4\"\n"
                 "Bob,\"456 Oak Ave\"\n");

    CsvAdapter       adapter;
    ConnectionParams p;
    p.connectionString = tmp.str();
    REQUIRE(adapter.Connect(p));

    DataQuery q;
    q.table    = "dg_test_quoted";
    q.pageSize = 100;

    const auto result = adapter.ExecuteQuery(q);
    REQUIRE(result.ok());
    REQUIRE(result.rows.size() == 2);
    CHECK(result.rows[0][1] == "123 Main St, Apt 4");
    CHECK(result.rows[1][1] == "456 Oak Ave");
}

TEST_CASE("CsvAdapter: embedded escaped quotes (double-quote pairs)", "[csv]")
{
    TempFile tmp("dg_test_escaped_quotes.csv",
                 "quote\n"
                 "\"say \"\"hello\"\"\"\n");

    CsvAdapter       adapter;
    ConnectionParams p;
    p.connectionString = tmp.str();
    REQUIRE(adapter.Connect(p));

    DataQuery q;
    q.table    = "dg_test_escaped_quotes";
    q.pageSize = 100;

    const auto result = adapter.ExecuteQuery(q);
    REQUIRE(result.ok());
    REQUIRE(result.rows.size() == 1);
    CHECK(result.rows[0][0] == "say \"hello\"");
}

TEST_CASE("CsvAdapter: semicolon delimiter via ConnectionParams", "[csv]")
{
    TempFile tmp("dg_test_semicolon.csv",
                 "a;b;c\n"
                 "1;2;3\n"
                 "4;5;6\n");

    CsvAdapter       adapter;
    ConnectionParams p;
    p.connectionString = tmp.str();
    p.csvSeparator     = ';';
    REQUIRE(adapter.Connect(p));

    const auto cols = adapter.GetColumns("dg_test_semicolon");
    REQUIRE(cols.size() == 3);
    CHECK(cols[0].name == "a");
    CHECK(cols[1].name == "b");
    CHECK(cols[2].name == "c");

    DataQuery q;
    q.table           = "dg_test_semicolon";
    q.pageSize        = 100;
    const auto result = adapter.ExecuteQuery(q);
    REQUIRE(result.ok());
    REQUIRE(result.rows.size() == 2);
    CHECK(result.rows[0][0] == "1");
    CHECK(result.rows[0][1] == "2");
}

TEST_CASE("CsvAdapter: empty file (header only, no data rows)", "[csv]")
{
    TempFile tmp("dg_test_headeronly.csv", "a,b,c\n");

    CsvAdapter       adapter;
    ConnectionParams p;
    p.connectionString = tmp.str();
    REQUIRE(adapter.Connect(p));

    DataQuery q;
    q.table    = "dg_test_headeronly";
    q.pageSize = 100;

    const auto result = adapter.ExecuteQuery(q);
    REQUIRE(result.ok());
    CHECK(result.rows.empty());
    CHECK(result.columns.size() == 3);
    CHECK(adapter.CountQuery(q) == 0);
}

TEST_CASE("CsvAdapter: re-connect replaces previous data", "[csv]")
{
    TempFile tmpA("dg_test_reconnect_a.csv", "x\n1\n2\n");
    TempFile tmpB("dg_test_reconnect_b.csv", "y\n10\n20\n30\n");

    CsvAdapter adapter;

    ConnectionParams pA;
    pA.connectionString = tmpA.str();
    REQUIRE(adapter.Connect(pA));
    CHECK(adapter.GetColumns("dg_test_reconnect_a").size() == 1);

    ConnectionParams pB;
    pB.connectionString = tmpB.str();
    REQUIRE(adapter.Connect(pB));

    // Old table is gone, new table is available
    CHECK(adapter.GetColumns("dg_test_reconnect_a").empty());
    CHECK(adapter.GetColumns("dg_test_reconnect_b").size() == 1);

    DataQuery q;
    q.table    = "dg_test_reconnect_b";
    q.pageSize = 100;
    CHECK(adapter.CountQuery(q) == 3);
}
