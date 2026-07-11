#pragma once

#include <string>

struct RomCacheLine
{
    std::string name;
    long long size = 0;
    std::string url;
};

// Parse one pipe-delimited cache line "name|size|url".
//  - <1 field -> false. size non-numeric/empty -> 0. url defaults to name if absent.
bool pkgi_parse_cache_line(const std::string& line, RomCacheLine& out);

// Serialize a cache line (no trailing newline).
std::string pkgi_format_cache_line(const std::string& name, long long size,
                                   const std::string& url);

// Assemble the ia* download URL. `file_encoded` must already be url-encoded.
std::string pkgi_build_download_url(const std::string& d1, const std::string& dir,
                                    const std::string& file_encoded);
