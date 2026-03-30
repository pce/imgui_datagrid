#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "adapters/utils/type_inferrer.hpp"

using namespace TypeInfer;

// ============================================================
//  Helpers
// ============================================================

static std::vector<std::string> repeat(const std::string& v, int n)
{
    return std::vector<std::string>(static_cast<size_t>(n), v);
}

// ============================================================
//  InferColumnType — Integer
// ============================================================

TEST_CASE("InferColumnType: all integers", "[inferrer][integer]")
{
    const auto r = InferColumnType({"1", "2", "42", "-7", "0", "1000"});
    CHECK(r.type == InferredType::Integer);
    CHECK(r.confidence >= 0.90f);
    CHECK(r.nullCount == 0);
}

TEST_CASE("InferColumnType: integers with sign", "[inferrer][integer]")
{
    const auto r = InferColumnType({"+1", "-99", "0", "42"});
    CHECK(r.type == InferredType::Integer);
}

TEST_CASE("InferColumnType: integers above 90% threshold", "[inferrer][integer]")
{
    // 9 integers + 1 non-integer → 90% → should still be Integer
    const auto r = InferColumnType({"1","2","3","4","5","6","7","8","9","hello"});
    CHECK(r.type == InferredType::Integer);
    CHECK(r.confidence >= 0.89f);
}

TEST_CASE("InferColumnType: integers below threshold → Text", "[inferrer][integer]")
{
    // Only 5/10 are integers → falls through to Text
    const auto r = InferColumnType({"1","2","3","4","5","a","b","c","d","e"});
    CHECK(r.type == InferredType::Text);
}

// ============================================================
//  InferColumnType — Real
// ============================================================

TEST_CASE("InferColumnType: floats", "[inferrer][real]")
{
    const auto r = InferColumnType({"1.5", "2.0", "3.14159", "-0.001", "99.9"});
    CHECK(r.type == InferredType::Real);
    CHECK(r.confidence >= 0.90f);
}

TEST_CASE("InferColumnType: mixed int and float → Real", "[inferrer][real]")
{
    // Integer is a subset of Real; mixed column → Real
    const auto r = InferColumnType({"1", "2.5", "3", "4.1"});
    CHECK(r.type == InferredType::Real);
}

TEST_CASE("InferColumnType: scientific notation → Real", "[inferrer][real]")
{
    const auto r = InferColumnType({"1e5", "2.3e-4", "0.0", "1E10"});
    CHECK(r.type == InferredType::Real);
}

// ============================================================
//  InferColumnType — Boolean
// ============================================================

TEST_CASE("InferColumnType: true/false strings → Boolean", "[inferrer][boolean]")
{
    const auto r = InferColumnType({"true", "false", "true", "false", "True", "False"});
    CHECK(r.type == InferredType::Boolean);
    CHECK(r.confidence >= 0.90f);
}

TEST_CASE("InferColumnType: yes/no strings → Boolean", "[inferrer][boolean]")
{
    const auto r = InferColumnType({"yes", "no", "YES", "No", "yes", "no"});
    CHECK(r.type == InferredType::Boolean);
}

TEST_CASE("InferColumnType: y/n/t/f → Boolean", "[inferrer][boolean]")
{
    const auto r = InferColumnType({"y", "n", "t", "f", "Y", "N"});
    CHECK(r.type == InferredType::Boolean);
}

TEST_CASE("InferColumnType: 0/1 only → Integer not Boolean", "[inferrer][boolean]")
{
    // 0 and 1 alone are ambiguous — prefer the more specific Integer
    const auto r = InferColumnType({"0", "1", "0", "1", "0", "1"});
    CHECK(r.type == InferredType::Integer);
}

TEST_CASE("InferColumnType: mixed word booleans and 0/1 → Boolean", "[inferrer][boolean]")
{
    const auto r = InferColumnType({"true", "false", "1", "0", "yes", "no"});
    CHECK(r.type == InferredType::Boolean);
}

// ============================================================
//  InferColumnType — Date
// ============================================================

TEST_CASE("InferColumnType: ISO dates YYYY-MM-DD", "[inferrer][date]")
{
    const auto r = InferColumnType({"2024-01-15", "2023-12-31", "2000-06-01"});
    CHECK(r.type == InferredType::Date);
    CHECK(r.confidence >= 0.90f);
}

TEST_CASE("InferColumnType: dates with slash separator", "[inferrer][date]")
{
    const auto r = InferColumnType({"2024/01/15", "2023/12/31", "2000/06/01"});
    CHECK(r.type == InferredType::Date);
}

TEST_CASE("InferColumnType: European DD-MM-YYYY dates", "[inferrer][date]")
{
    const auto r = InferColumnType({"15-01-2024", "31-12-2023", "01-06-2000"});
    CHECK(r.type == InferredType::Date);
}

// ============================================================
//  InferColumnType — DateTime
// ============================================================

TEST_CASE("InferColumnType: ISO-8601 datetimes with T", "[inferrer][datetime]")
{
    const auto r = InferColumnType({
        "2024-01-15T10:30",
        "2023-12-31T23:59",
        "2000-06-01T00:00"
    });
    CHECK(r.type == InferredType::DateTime);
    CHECK(r.confidence >= 0.90f);
}

TEST_CASE("InferColumnType: datetimes with space separator", "[inferrer][datetime]")
{
    const auto r = InferColumnType({
        "2024-01-15 10:30",
        "2023-12-31 23:59",
        "2000-06-01 00:00"
    });
    CHECK(r.type == InferredType::DateTime);
}

TEST_CASE("InferColumnType: DateTime preferred over Date", "[inferrer][datetime]")
{
    // All values have both date and time → DateTime wins
    const auto r = InferColumnType({
        "2024-01-15T10:30",
        "2024-01-16T11:00",
        "2024-01-17T12:00"
    });
    CHECK(r.type == InferredType::DateTime);
}

// ============================================================
//  InferColumnType — Text
// ============================================================

TEST_CASE("InferColumnType: plain text strings", "[inferrer][text]")
{
    const auto r = InferColumnType({"hello", "world", "foo", "bar", "baz"});
    CHECK(r.type == InferredType::Text);
}

TEST_CASE("InferColumnType: mixed types → Text", "[inferrer][text]")
{
    const auto r = InferColumnType({"1", "two", "3.0", "four", "5"});
    CHECK(r.type == InferredType::Text);
}

TEST_CASE("InferColumnType: empty sample list → Text confidence 0", "[inferrer][text]")
{
    const auto r = InferColumnType({});
    CHECK(r.type == InferredType::Text);
    CHECK(r.confidence == 0.0f);
    CHECK(r.total == 0);
}

TEST_CASE("InferColumnType: all nulls → Text confidence 0", "[inferrer][text]")
{
    const auto r = InferColumnType({"", "", "", ""});
    CHECK(r.type == InferredType::Text);
    CHECK(r.nullCount == 4);
    CHECK(r.confidence == 0.0f);
}

// ============================================================
//  InferColumnType — Nulls mixed with typed values
// ============================================================

TEST_CASE("InferColumnType: integers with nulls", "[inferrer][null]")
{
    const auto r = InferColumnType({"1", "", "3", "", "5", "", "7", "", "9", ""});
    // 5 non-null integers, 5 nulls → 100% of non-null are integers → Integer
    CHECK(r.type == InferredType::Integer);
    CHECK(r.nullCount == 5);
    CHECK(r.total == 10);
}

TEST_CASE("InferColumnType: min/max values populated", "[inferrer][minmax]")
{
    const auto r = InferColumnType({"banana", "apple", "cherry", "date"});
    CHECK(r.minValue == "apple");
    CHECK(r.maxValue == "date");
}

TEST_CASE("InferColumnType: min/max empty when all null", "[inferrer][minmax]")
{
    const auto r = InferColumnType({"", "", ""});
    CHECK(r.minValue.empty());
    CHECK(r.maxValue.empty());
}

// ============================================================
//  InferColumnType — caps at 200 samples
// ============================================================

TEST_CASE("InferColumnType: large sample capped at 200", "[inferrer][perf]")
{
    std::vector<std::string> many;
    many.reserve(500);
    for (int i = 0; i < 500; ++i) many.push_back(std::to_string(i));
    const auto r = InferColumnType(many);
    CHECK(r.total == 200);   // capped
    CHECK(r.type == InferredType::Integer);
}

// ============================================================
//  TypeName
// ============================================================

TEST_CASE("TypeName returns expected strings", "[inferrer][typename]")
{
    CHECK(std::string(TypeName(InferredType::Boolean))  == "boolean");
    CHECK(std::string(TypeName(InferredType::Integer))  == "integer");
    CHECK(std::string(TypeName(InferredType::Real))     == "real");
    CHECK(std::string(TypeName(InferredType::DateTime)) == "datetime");
    CHECK(std::string(TypeName(InferredType::Date))     == "date");
    CHECK(std::string(TypeName(InferredType::Text))     == "text");
}

// ============================================================
//  DetectDelimiter
// ============================================================

TEST_CASE("DetectDelimiter: comma-separated", "[delimiter]")
{
    const std::vector<std::string> lines = {
        "id,name,score",
        "1,Alice,95.5",
        "2,Bob,87.0",
        "3,Charlie,91.2"
    };
    const auto r = DetectDelimiter(lines);
    CHECK(r.delimiter == ',');
    CHECK(r.confidence > 0.4f);
}

TEST_CASE("DetectDelimiter: tab-separated", "[delimiter]")
{
    const std::vector<std::string> lines = {
        "id\tname\tscore",
        "1\tAlice\t95.5",
        "2\tBob\t87.0"
    };
    const auto r = DetectDelimiter(lines);
    CHECK(r.delimiter == '\t');
    CHECK(r.confidence > 0.4f);
}

TEST_CASE("DetectDelimiter: semicolon-separated", "[delimiter]")
{
    const std::vector<std::string> lines = {
        "id;name;score",
        "1;Alice;95.5",
        "2;Bob;87.0",
        "3;Charlie;91.2"
    };
    const auto r = DetectDelimiter(lines);
    CHECK(r.delimiter == ';');
}

TEST_CASE("DetectDelimiter: pipe-separated", "[delimiter]")
{
    const std::vector<std::string> lines = {
        "id|name|value|extra",
        "1|foo|bar|baz",
        "2|qux|quux|corge"
    };
    const auto r = DetectDelimiter(lines);
    CHECK(r.delimiter == '|');
}

TEST_CASE("DetectDelimiter: empty lines → comma with zero confidence", "[delimiter]")
{
    const auto r = DetectDelimiter({});
    CHECK(r.delimiter == ',');
    CHECK(r.confidence == 0.0f);
}

TEST_CASE("DetectDelimiter: single line, comma", "[delimiter]")
{
    const auto r = DetectDelimiter({"a,b,c,d"});
    CHECK(r.delimiter == ',');
}

TEST_CASE("DetectDelimiter: quoted fields with commas inside", "[delimiter]")
{
    // Commas inside double-quotes should NOT be counted as delimiters
    const std::vector<std::string> lines = {
        "id,name,note",
        "1,Alice,\"hello, world\"",
        "2,Bob,\"foo, bar, baz\""
    };
    const auto r = DetectDelimiter(lines);
    // Still comma (2 real delimiters per data line, not 4/5)
    CHECK(r.delimiter == ',');
}

// ============================================================
//  DetectHeader
// ============================================================

TEST_CASE("DetectHeader: classic header vs data row", "[header]")
{
    const std::vector<std::string> header = {"id", "name", "score", "active"};
    const std::vector<std::string> data   = {"1",  "Alice", "95.5", "true"};
    const auto r = DetectHeader(header, data);
    CHECK(r.hasHeader == true);
    CHECK(r.confidence > 0.50f);
}

TEST_CASE("DetectHeader: both rows numeric → not a header", "[header]")
{
    const std::vector<std::string> row1 = {"1",  "42",  "3.14", "0"};
    const std::vector<std::string> row2 = {"2",  "99",  "2.71", "1"};
    const auto r = DetectHeader(row1, row2);
    CHECK(r.hasHeader == false);
}

TEST_CASE("DetectHeader: all-text first row, all-numeric second → header", "[header]")
{
    const std::vector<std::string> first  = {"name", "value", "count", "rate"};
    const std::vector<std::string> second = {"7.2",  "100",   "3",     "0.5"};
    const auto r = DetectHeader(first, second);
    CHECK(r.hasHeader == true);
    CHECK(r.confidence > 0.60f);
    CHECK(!r.reason.empty());
}

TEST_CASE("DetectHeader: first row with all unique values → confidence boost", "[header]")
{
    const std::vector<std::string> first  = {"alpha", "beta", "gamma", "delta"};
    const std::vector<std::string> second = {"x",     "y",    "z",     "w"};
    const auto r = DetectHeader(first, second);
    CHECK(r.hasHeader == true);
}

TEST_CASE("DetectHeader: empty first row → not header, confidence 0", "[header]")
{
    const auto r = DetectHeader({}, {"1", "2", "3"});
    CHECK(r.hasHeader == false);
    CHECK(r.confidence == 0.0f);
}

TEST_CASE("DetectHeader: single row, no second row → low confidence", "[header]")
{
    const std::vector<std::string> first = {"col_a", "col_b", "col_c"};
    const auto r = DetectHeader(first, {});
    // Pulled towards neutral (0.5) because of single-row penalty
    CHECK(r.confidence > 0.30f);
    CHECK(r.confidence < 0.80f);
}

TEST_CASE("DetectHeader: column count mismatch → small confidence penalty", "[header]")
{
    const std::vector<std::string> first  = {"a", "b", "c"};
    const std::vector<std::string> second = {"1", "2"};          // one fewer column
    const auto r1 = DetectHeader(first, second);

    const std::vector<std::string> firstGood  = {"a", "b", "c"};
    const std::vector<std::string> secondGood = {"1", "2", "3"}; // matching
    const auto r2 = DetectHeader(firstGood, secondGood);

    // Mismatched should have lower or equal confidence
    CHECK(r1.confidence <= r2.confidence + 0.01f);
}

TEST_CASE("DetectHeader: reason string is non-empty", "[header]")
{
    const std::vector<std::string> first  = {"name", "age"};
    const std::vector<std::string> second = {"Alice", "30"};
    const auto r = DetectHeader(first, second);
    CHECK(!r.reason.empty());
}
