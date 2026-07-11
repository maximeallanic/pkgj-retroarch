#pragma once

#include <string>
#include <vector>

struct NpsRow
{
    std::string content_id;
    std::string titleid;
    std::string region;
    std::string name;
    std::string name_org;
    std::string pkg_url;
    std::string zrif;
    long long size = 0;
};

// Split one line on '\t'. A trailing '\r' should already be stripped by the caller.
std::vector<std::string> pkgi_split_tsv_row(const std::string& line);

// Parse one NoPayStation PSV_GAMES data row (NOT the header).
// Column layout: content_id=0, region=1, name=2, pkg_url=3, zrif=4, name_org=7, size=8.
// Returns false (row excluded) when pkg_url is empty / "MISSING" / "CART ONLY",
// or zrif == "MISSING", or there are too few columns to reach zrif.
bool pkgi_parse_nps_game_row(const std::string& line, NpsRow& out);
