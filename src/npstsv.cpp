#include "npstsv.hpp"

std::vector<std::string> pkgi_split_tsv_row(const std::string& line)
{
    std::vector<std::string> fields;
    std::string cur;
    for (char c : line)
    {
        if (c == '\t')
        {
            fields.push_back(cur);
            cur.clear();
        }
        else
        {
            cur += c;
        }
    }
    fields.push_back(cur);
    return fields;
}

bool pkgi_parse_nps_game_row(const std::string& line, NpsRow& out)
{
    const auto f = pkgi_split_tsv_row(line);
    if (f.size() < 5)             // need at least through zrif (index 4)
        return false;

    const std::string& pkg_url = f[3];
    const std::string& zrif = f[4];
    if (pkg_url.empty() || pkg_url == "MISSING" || pkg_url == "CART ONLY" ||
        zrif == "MISSING")
        return false;

    out.content_id = f[0];
    out.region = f[1];
    out.name = f[2];
    out.pkg_url = pkg_url;
    out.zrif = zrif;
    out.name_org = f.size() > 7 ? f[7] : "";
    out.titleid = out.content_id.size() >= 16 ? out.content_id.substr(7, 9) : "";

    out.size = 0;
    if (f.size() > 8 && !f[8].empty())
    {
        try { out.size = std::stoll(f[8]); }
        catch (...) { out.size = 0; }
    }
    return true;
}
