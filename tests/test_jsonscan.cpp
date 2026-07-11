#include "doctest.h"
#include "jsonscan.hpp"

#include <fstream>
#include <sstream>
#include <string>

static std::string load_fixture()
{
    std::ifstream f("tests/fixtures/archive_metadata.json");
    REQUIRE(f.good());
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

TEST_CASE("json_str extracts a top-level string field")
{
    const auto j = load_fixture();
    CHECK(pkgi_json_str(j.data(), j.size(), "d1") == "ia600505.us.archive.org");
    CHECK(pkgi_json_str(j.data(), j.size(), "dir") ==
          "/12/items/theentiregameboycollection");
    CHECK(pkgi_json_str(j.data(), j.size(), "nonexistent") == "");
}

TEST_CASE("find_object_end handles nested braces and strings")
{
    std::string s = R"({"a":{"b":"}"} ,"c":1})";
    const char* end = pkgi_find_object_end(s.data(), s.data() + s.size());
    REQUIRE(end != nullptr);
    CHECK(*end == '}');
    CHECK(end == s.data() + s.size() - 1);  // the LAST brace
}

TEST_CASE("find_object_end returns nullptr when unbalanced")
{
    std::string s = R"({"a":1)";
    CHECK(pkgi_find_object_end(s.data(), s.data() + s.size()) == nullptr);
}

TEST_CASE("json_unescape handles the escapes archive.org emits")
{
    CHECK(pkgi_json_unescape("a\\/b") == "a/b");
    CHECK(pkgi_json_unescape("a\\\\b") == "a\\b");
    CHECK(pkgi_json_unescape("a\\\"b") == "a\"b");
    CHECK(pkgi_json_unescape("plain") == "plain");
}

TEST_CASE("url_encode_path preserves slashes and unreserved chars")
{
    CHECK(pkgi_url_encode_path("Tetris (World).gb") == "Tetris%20%28World%29.gb");
    CHECK(pkgi_url_encode_path("a/b/c.gb") == "a/b/c.gb");
    CHECK(pkgi_url_encode_path("game-1_v2.0~.gb") == "game-1_v2.0~.gb");
}
