#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <atomic>
#include <unistd.h>

#include "adapters/filesystem/fluent_query.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace Adapters::Fs;
using namespace Adapters::Fs::literals;

//
// Creates:
//   <tmpdir>/dg_fq_<N>/
//       main.cpp        400 bytes
//       utils.hpp       800 bytes
//       readme.txt     1200 bytes
//       image.png     50000 bytes
//       .hidden_file     10 bytes
//       subdir/
//           nested.cpp  300 bytes
//           deep.hpp    100 bytes
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
        dir = fs::temp_directory_path() / ("dg_fq_" + std::to_string(getpid()) + "_" + std::to_string(++s_id));
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

static bool hasName(const std::vector<Adapters::FilesystemEntry>& v, const std::string& name)
{
    for (const auto& e : v)
        if (e.name == name)
            return true;
    return false;
}

TEST_CASE("FluentQuery: basic listing counts entries", "[fluent_query]")
{
    TempDir tmp;
    auto    ents = FluentQuery::from(tmp.dir).entries();
    // 5 visible: main.cpp utils.hpp readme.txt image.png subdir
    CHECK(ents.size() == 5);
}

TEST_CASE("FluentQuery: show_hidden includes dot files", "[fluent_query]")
{
    TempDir tmp;
    auto    ents = FluentQuery::from(tmp.dir).show_hidden().entries();
    CHECK(ents.size() == 6);
    CHECK(hasName(ents, ".hidden_file"));
}

TEST_CASE("FluentQuery: where(ext_is) filters by extension", "[fluent_query]")
{
    TempDir tmp;
    // Non-recursive: only main.cpp at root, not subdir/nested.cpp
    auto ents = FluentQuery::from(tmp.dir).where(ext_is(".cpp")).entries();
    REQUIRE(ents.size() == 1);
    CHECK(ents[0].name == "main.cpp");
}

TEST_CASE("FluentQuery: recursive() descends into subdirs", "[fluent_query]")
{
    TempDir tmp;
    int     n = FluentQuery::from(tmp.dir).recursive().where(ext_is(".cpp")).count();
    CHECK(n == 2); // main.cpp + subdir/nested.cpp
}

TEST_CASE("FluentQuery: recursive maxDepth=0 stays at root", "[fluent_query]")
{
    TempDir tmp;
    // depth 0 = only root-level items; subdir itself is included but not entered
    int n = FluentQuery::from(tmp.dir).recursive(true, 0).count();
    CHECK(n == 5); // main.cpp utils.hpp readme.txt image.png subdir (hidden excluded)
}

TEST_CASE("FluentQuery: where(size_gt) filters by size", "[fluent_query]")
{
    TempDir tmp;
    auto    ents = FluentQuery::from(tmp.dir).where(size_gt(1000)).entries();
    // readme.txt (1200) + image.png (50000); dirs have sizeBytes=0
    REQUIRE(ents.size() == 2);
    CHECK(hasName(ents, "readme.txt"));
    CHECK(hasName(ents, "image.png"));
}

TEST_CASE("FluentQuery: where(is_dir) returns only dirs", "[fluent_query]")
{
    TempDir tmp;
    auto    ents = FluentQuery::from(tmp.dir).where(is_dir()).entries();
    REQUIRE(ents.size() == 1);
    CHECK(ents[0].name == "subdir");
}

TEST_CASE("FluentQuery: where composed && predicate", "[fluent_query]")
{
    TempDir tmp;
    // .hpp files larger than 500 bytes → only utils.hpp (800 bytes)
    auto ents = FluentQuery::from(tmp.dir).where(ext_is(".hpp") && size_gt(500)).entries();
    REQUIRE(ents.size() == 1);
    CHECK(ents[0].name == "utils.hpp");
}

TEST_CASE("FluentQuery: order_by size descending", "[fluent_query]")
{
    TempDir tmp;
    auto    ents = FluentQuery::from(tmp.dir).order_by("size", false).entries();
    REQUIRE_FALSE(ents.empty());
    // image.png (50000 bytes) is largest — dirs have 0 bytes so go last
    CHECK(ents[0].name == "image.png");
}

TEST_CASE("FluentQuery: limit and offset pagination", "[fluent_query]")
{
    TempDir tmp;
    // Default sort: name asc, dirs first → subdir, image.png, main.cpp, readme.txt, utils.hpp
    // offset(1) skips subdir; limit(2) → image.png, main.cpp
    auto ents = FluentQuery::from(tmp.dir).order_by("name").limit(2).offset(1).entries();
    REQUIRE(ents.size() == 2);
    CHECK(hasName(ents, "image.png"));
    CHECK(hasName(ents, "main.cpp"));
}

TEST_CASE("FluentQuery: select projection", "[fluent_query]")
{
    TempDir tmp;
    auto    result = FluentQuery::from(tmp.dir).select({"name", "size"}).execute();
    CHECK(result.columns.size() == 2);
    CHECK(result.columns[0].name == "name");
    CHECK(result.columns[1].name == "size");
}

TEST_CASE("FluentQuery: dirs_first groups directories", "[fluent_query]")
{
    TempDir tmp;
    auto    ents = FluentQuery::from(tmp.dir).dirs_first(true).entries();
    REQUIRE_FALSE(ents.empty());
    CHECK(ents[0].name == "subdir");
    CHECK(ents[0].kind == "dir");
}

TEST_CASE("FluentQuery: from_sql basic SELECT *", "[fluent_query]")
{
    TempDir     tmp;
    std::string sql = "SELECT * FROM '" + tmp.dir.string() + "'";
    auto        q   = FluentQuery::from_sql(sql);
    REQUIRE(q.has_value());
    CHECK(q->count() == 5); // 5 visible entries
}

TEST_CASE("FluentQuery: from_sql WHERE extension filter", "[fluent_query]")
{
    TempDir     tmp;
    std::string sql = "SELECT * FROM '" + tmp.dir.string() + "' WHERE extension = '.hpp'";
    auto        q   = FluentQuery::from_sql(sql);
    REQUIRE(q.has_value());
    // Only utils.hpp at root (non-recursive)
    CHECK(q->count() == 1);
}

TEST_CASE("FluentQuery: from_sql WHERE size > numeric", "[fluent_query]")
{
    TempDir     tmp;
    std::string sql = "SELECT * FROM '" + tmp.dir.string() + "' WHERE size > 1000";
    auto        q   = FluentQuery::from_sql(sql);
    REQUIRE(q.has_value());
    // readme.txt (1200) + image.png (50000)
    CHECK(q->count() == 2);
}

TEST_CASE("FluentQuery: from_sql RECURSIVE", "[fluent_query]")
{
    TempDir     tmp;
    std::string sql = "SELECT * FROM '" + tmp.dir.string() + "' RECURSIVE WHERE extension = '.cpp'";
    auto        q   = FluentQuery::from_sql(sql);
    REQUIRE(q.has_value());
    CHECK(q->count() == 2); // main.cpp + subdir/nested.cpp
}

TEST_CASE("FluentQuery: from_sql ORDER BY size DESC LIMIT 1", "[fluent_query]")
{
    TempDir     tmp;
    std::string sql = "SELECT * FROM '" + tmp.dir.string() + "' ORDER BY size DESC LIMIT 1";
    auto        q   = FluentQuery::from_sql(sql);
    REQUIRE(q.has_value());
    auto ents = q->entries();
    REQUIRE(ents.size() == 1);
    CHECK(ents[0].name == "image.png");
}

TEST_CASE("FluentQuery: from_sql invalid SQL returns error", "[fluent_query]")
{
    auto q = FluentQuery::from_sql("GARBAGE SQL");
    CHECK_FALSE(q.has_value());
    CHECK_FALSE(q.error().empty());
}

TEST_CASE("FluentQuery: name_contains case-insensitive", "[fluent_query]")
{
    TempDir tmp;
    // "MAIN" should match "main.cpp" (case-insensitive)
    auto ents = FluentQuery::from(tmp.dir).where(name_contains("MAIN")).entries();
    REQUIRE(ents.size() == 1);
    CHECK(ents[0].name == "main.cpp");
}

TEST_CASE("FluentQuery: name_glob pattern", "[fluent_query]")
{
    TempDir tmp;
    // Non-recursive: only utils.hpp at root level
    auto ents = FluentQuery::from(tmp.dir).where(name_glob("*.hpp")).entries();
    REQUIRE(ents.size() == 1);
    CHECK(ents[0].name == "utils.hpp");
}
