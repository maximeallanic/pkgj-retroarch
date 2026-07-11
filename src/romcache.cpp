#include "romcache.hpp"

#include <vector>

namespace
{
std::vector<std::string> split_pipe(const std::string& s)
{
    std::vector<std::string> parts;
    std::string cur;
    for (char c : s)
    {
        if (c == '|')
        {
            parts.push_back(cur);
            cur.clear();
        }
        else
        {
            cur += c;
        }
    }
    parts.push_back(cur);
    return parts;
}
}

bool pkgi_parse_cache_line(const std::string& line, RomCacheLine& out)
{
    const auto fields = split_pipe(line);
    if (fields.empty() || fields[0].empty())
        return false;

    out.name = fields[0];
    out.size = 0;
    if (fields.size() > 1 && !fields[1].empty())
    {
        try { out.size = std::stoll(fields[1]); }
        catch (...) { out.size = 0; }
    }
    out.url = (fields.size() > 2 && !fields[2].empty()) ? fields[2] : fields[0];
    return true;
}

std::string pkgi_format_cache_line(const std::string& name, long long size,
                                   const std::string& url)
{
    return name + "|" + std::to_string(size) + "|" + url;
}

std::string pkgi_build_download_url(const std::string& d1, const std::string& dir,
                                    const std::string& file_encoded)
{
    // dir already begins with '/'.
    return "https://" + d1 + dir + "/" + file_encoded;
}
