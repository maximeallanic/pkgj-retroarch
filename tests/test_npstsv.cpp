#include "doctest.h"
#include "npstsv.hpp"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static std::vector<std::string> fixture_lines()
{
    std::ifstream f("tests/fixtures/nps_psv_games.tsv");
    REQUIRE(f.good());
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(f, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

TEST_CASE("split_tsv_row splits on tabs")
{
    const auto f = pkgi_split_tsv_row("a\tb\tc");
    REQUIRE(f.size() == 3);
    CHECK(f[0] == "a");
    CHECK(f[1] == "b");
    CHECK(f[2] == "c");
}

TEST_CASE("a valid NoPS game row parses with titleid derived from content id")
{
    const auto lines = fixture_lines();
    REQUIRE(lines.size() == 4);   // header + 3 rows
    NpsRow row;
    REQUIRE(pkgi_parse_nps_game_row(lines[1], row));
    CHECK(row.content_id == "UP0000-PCSA00000_00-TESTGAME00000000");
    CHECK(row.titleid == "PCSA00000");     // content_id.substr(7,9)
    CHECK(row.region == "USA");
    CHECK(row.name == "Test Game");
    CHECK(row.pkg_url == "http://ia.example/Test.pkg");
    CHECK(row.zrif == "KO5ifQhpU9wAAAAAAAAAAA");
    CHECK(row.name_org == "Test Game Org");
    CHECK(row.size == 123456);
}

TEST_CASE("a row with MISSING pkg url is rejected")
{
    const auto lines = fixture_lines();
    NpsRow row;
    CHECK_FALSE(pkgi_parse_nps_game_row(lines[2], row));
}

TEST_CASE("a row with MISSING zrif is rejected")
{
    const auto lines = fixture_lines();
    NpsRow row;
    CHECK_FALSE(pkgi_parse_nps_game_row(lines[3], row));
}

TEST_CASE("CART ONLY and empty urls are rejected")
{
    NpsRow row;
    CHECK_FALSE(pkgi_parse_nps_game_row(
        "CID\tUSA\tName\tCART ONLY\tZRIF\tCID\td\torg\t1", row));
    CHECK_FALSE(pkgi_parse_nps_game_row(
        "CID\tUSA\tName\t\tZRIF\tCID\td\torg\t1", row));
}

TEST_CASE("a short content id yields an empty titleid but still parses")
{
    NpsRow row;
    REQUIRE(pkgi_parse_nps_game_row(
        "SHORT\tUSA\tName\thttp://x/y.pkg\tZRIF\tSHORT\td\torg\t42", row));
    CHECK(row.titleid == "");
    CHECK(row.size == 42);
}
