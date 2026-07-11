#include "doctest.h"
#include "romcache.hpp"

TEST_CASE("format then parse round-trips a cache line")
{
    const std::string line =
        pkgi_format_cache_line("Tetris.gb", 131072, "https://host/dir/Tetris.gb");
    CHECK(line == "Tetris.gb|131072|https://host/dir/Tetris.gb");

    RomCacheLine out;
    REQUIRE(pkgi_parse_cache_line(line, out));
    CHECK(out.name == "Tetris.gb");
    CHECK(out.size == 131072);
    CHECK(out.url == "https://host/dir/Tetris.gb");
}

TEST_CASE("parse tolerates a missing size (defaults to 0) but keeps url")
{
    RomCacheLine out;
    REQUIRE(pkgi_parse_cache_line("Tetris.gb||https://host/Tetris.gb", out));
    CHECK(out.size == 0);
    CHECK(out.url == "https://host/Tetris.gb");
}

TEST_CASE("parse falls back to name as url when only two fields")
{
    RomCacheLine out;
    REQUIRE(pkgi_parse_cache_line("Tetris.gb|131072", out));
    CHECK(out.url == "Tetris.gb");
}

TEST_CASE("parse rejects a line with no fields")
{
    RomCacheLine out;
    CHECK_FALSE(pkgi_parse_cache_line("", out));
}

TEST_CASE("parse ignores a non-numeric size instead of throwing")
{
    RomCacheLine out;
    REQUIRE(pkgi_parse_cache_line("Tetris.gb|notanumber|https://h/x", out));
    CHECK(out.size == 0);
}

TEST_CASE("build_download_url uses the ia* host+dir, never /download/")
{
    const std::string url = pkgi_build_download_url(
        "ia600505.us.archive.org",
        "/12/items/coll",
        "Tetris%20%28World%29.gb");
    CHECK(url == "https://ia600505.us.archive.org/12/items/coll/Tetris%20%28World%29.gb");
    CHECK(url.find("/download/") == std::string::npos);
}
