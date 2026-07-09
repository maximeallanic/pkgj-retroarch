#include "db.hpp"

#include "file.hpp"
#include "pkgi.hpp"
#include "utils.hpp"

#include <fmt/format.h>

#include <boost/scope_exit.hpp>

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include <stddef.h>

std::string pkgi_mode_to_string(Mode mode)
{
    switch (mode)
    {
    case ModeGB:      return "Game Boy";
    case ModeGBC:     return "Game Boy Color";
    case ModeGBA:     return "Game Boy Advance";
    case ModeSNES:    return "Super Nintendo";
    case ModeNES:     return "Nintendo NES";
    case ModeGenesis: return "Sega Mega Drive";
    case ModePS1:     return "PlayStation 1";
    case ModePSP:     return "PSP";
    }
    return "unknown system";
}

// Directory name used under ux0:roms/
std::string pkgi_mode_to_system_dir(Mode mode)
{
    switch (mode)
    {
    case ModeGB:      return "gb";
    case ModeGBC:     return "gbc";
    case ModeGBA:     return "gba";
    case ModeSNES:    return "snes";
    case ModeNES:     return "nes";
    case ModeGenesis: return "megadrive";
    case ModePS1:     return "psx";
    case ModePSP:     return "psp";
    }
    return "roms";
}

TitleDatabase::TitleDatabase(const std::string& dbPath) : _dbPath(dbPath)
{
}

// Cache filename per system (pipe-delimited text)
static const char* pkgi_mode_to_file_name(Mode mode)
{
    switch (mode)
    {
    case ModeGB:      return "roms_gb.dat";
    case ModeGBC:     return "roms_gbc.dat";
    case ModeGBA:     return "roms_gba.dat";
    case ModeSNES:    return "roms_snes.dat";
    case ModeNES:     return "roms_nes.dat";
    case ModeGenesis: return "roms_genesis.dat";
    case ModePS1:     return "roms_ps1.dat";
    case ModePSP:     return "roms_psp.dat";
    }
    throw formatEx<std::runtime_error>(
            "unknown mode {}", static_cast<int>(mode));
}

// ---------------------------------------------------------------------------
// Minimal JSON field extractor (no external dependency)
// Works on a null-terminated JSON substring.
// ---------------------------------------------------------------------------
namespace
{

// Return the value of a string field "key":"<value>" within [data, data+len)
static std::string json_str(const char* data, size_t len, const char* key)
{
    // Build the search pattern "key":"
    std::string pat = "\"";
    pat += key;
    pat += "\":\"";

    const char* found = nullptr;
    for (size_t i = 0; i + pat.size() <= len; ++i)
    {
        if (memcmp(data + i, pat.c_str(), pat.size()) == 0)
        {
            found = data + i + pat.size();
            break;
        }
    }
    if (!found)
        return "";

    const char* end = found;
    const char* limit = data + len;
    while (end < limit && *end != '"' && *end != '\0')
    {
        if (*end == '\\')
            end++; // skip escaped char
        end++;
    }
    return std::string(found, end);
}

// Return the value of a numeric field "key":<number> within [data, data+len)
static int64_t json_num(const char* data, size_t len, const char* key)
{
    std::string pat = "\"";
    pat += key;
    pat += "\":";

    const char* found = nullptr;
    for (size_t i = 0; i + pat.size() <= len; ++i)
    {
        if (memcmp(data + i, pat.c_str(), pat.size()) == 0)
        {
            found = data + i + pat.size();
            break;
        }
    }
    if (!found)
        return 0;

    // skip whitespace
    while (*found == ' ' || *found == '\t') found++;
    if (*found < '0' || *found > '9')
        return 0;

    int64_t result = 0;
    while (*found >= '0' && *found <= '9')
    {
        result = result * 10 + (*found - '0');
        found++;
    }
    return result;
}

// Find the closing '}' matching an opening '{' at ptr[0].
// Returns pointer to the '}', or nullptr on error.
static const char* find_object_end(const char* ptr, const char* limit)
{
    if (!ptr || *ptr != '{')
        return nullptr;
    int depth = 0;
    bool in_string = false;
    while (ptr < limit)
    {
        char c = *ptr;
        if (in_string)
        {
            if (c == '\\')
                ptr++; // skip escaped char
            else if (c == '"')
                in_string = false;
        }
        else
        {
            if (c == '"')
                in_string = true;
            else if (c == '{')
                depth++;
            else if (c == '}')
            {
                depth--;
                if (depth == 0)
                    return ptr;
            }
        }
        ptr++;
    }
    return nullptr;
}

// Split a string by delimiter (returns at most maxParts parts)
static std::vector<std::string> split_pipe(const std::string& s, char delim = '|')
{
    std::vector<std::string> parts;
    std::string cur;
    for (char c : s)
    {
        if (c == delim)
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

// Sorting helper
bool lower(const DbItem& a, const DbItem& b, DbSort sort, DbSortOrder order)
{
    int64_t cmp;
    if (sort == SortByTitle)
        cmp = a.titleid.compare(b.titleid);
    else if (sort == SortByName)
        cmp = pkgi_stricmp(a.name.c_str(), b.name.c_str());
    else if (sort == SortBySize)
        cmp = a.size - b.size;
    else if (sort == SortByDate)
        cmp = a.date.compare(b.date);
    else
        cmp = pkgi_stricmp(a.name.c_str(), b.name.c_str());

    if (cmp == 0)
        cmp = a.titleid.compare(b.titleid);

    if (order == SortDescending)
        cmp = -cmp;

    return cmp < 0;
}

} // namespace

// ---------------------------------------------------------------------------
// update() — Download Archive.org search results and cache locally
// URL format: https://archive.org/advancedsearch.php?q=collection:...&output=json&...
// ---------------------------------------------------------------------------
void TitleDatabase::update(Mode mode, Http* http, const std::string& update_url)
{
    db_total = 0;
    db_size = 0;

    LOGF("Fetching ROM list from Archive.org: {}", update_url);

    http->start(update_url, 0);

    const auto http_length = http->get_length();
    db_total = (http_length > 0) ? static_cast<uint32_t>(http_length) : 0;

    // Read the full JSON response into memory
    std::vector<char> json_buf;
    json_buf.reserve(256 * 1024);

    std::vector<uint8_t> chunk(32 * 1024);
    for (;;)
    {
        int read = http->read(chunk.data(), chunk.size());
        if (read <= 0)
            break;
        db_size += static_cast<uint32_t>(read);
        json_buf.insert(json_buf.end(),
                        reinterpret_cast<char*>(chunk.data()),
                        reinterpret_cast<char*>(chunk.data()) + read);
    }

    if (json_buf.empty())
        throw std::runtime_error("Archive.org returned empty response");

    json_buf.push_back('\0'); // null-terminate

    const char* json = json_buf.data();
    const size_t json_len = json_buf.size() - 1;

    // Locate the "docs":[ array
    const char* docs_key = "\"docs\":[";
    const char* docs_start = nullptr;
    for (size_t i = 0; i + strlen(docs_key) <= json_len; ++i)
    {
        if (memcmp(json + i, docs_key, strlen(docs_key)) == 0)
        {
            docs_start = json + i + strlen(docs_key);
            break;
        }
    }

    if (!docs_start)
        throw std::runtime_error("Archive.org JSON missing 'docs' array");

    const char* json_end = json + json_len;

    // Write cache file (pipe-delimited: identifier|title|date|size)
    const auto filepath =
            fmt::format("{}/{}", _dbPath, pkgi_mode_to_file_name(mode));
    const auto tmppath = _dbPath + "/dbtmp.dat";

    auto item_file = pkgi_create(tmppath);
    if (!item_file)
        throw formatEx<std::runtime_error>(
                "cannot create cache file {}", tmppath);

    BOOST_SCOPE_EXIT_ALL(&)
    {
        if (item_file)
            pkgi_close(item_file);
    };

    uint32_t count = 0;
    const char* p = docs_start;
    while (p < json_end && *p != ']')
    {
        if (*p == '{')
        {
            const char* obj_end = find_object_end(p, json_end);
            if (!obj_end)
                break;

            const size_t obj_len = static_cast<size_t>(obj_end - p + 1);

            const std::string identifier = json_str(p, obj_len, "identifier");
            const std::string title      = json_str(p, obj_len, "title");
            const std::string addeddate  = json_str(p, obj_len, "addeddate");
            const int64_t     item_size  = json_num(p, obj_len, "item_size");

            if (!identifier.empty())
            {
                const std::string line = identifier + "|" +
                                         title      + "|" +
                                         addeddate  + "|" +
                                         std::to_string(item_size) + "\n";
                pkgi_write(item_file, line.data(), static_cast<uint32_t>(line.size()));
                count++;
            }

            p = obj_end + 1;
        }
        else
        {
            p++;
        }

        if (count >= MAX_DB_ITEMS)
            break;
    }

    pkgi_close(item_file);
    item_file = nullptr;

    pkgi_rename(tmppath, filepath);

    LOGF("Archive.org database cached: {} items → {}", count, filepath);
}

// ---------------------------------------------------------------------------
// reload() — Read cached pipe-delimited file and populate db
// ---------------------------------------------------------------------------
void TitleDatabase::reload(
        Mode mode,
        uint32_t /*region_filter*/,
        DbSort sort_by,
        DbSortOrder sort_order,
        const std::string& search,
        const std::set<std::string>& /*installed_games*/)
{
    db.clear();
    _title_count = 0;

    const auto dbpath =
            fmt::format("{}/{}", _dbPath, pkgi_mode_to_file_name(mode));

    if (!pkgi_file_exists(dbpath))
        return;

    auto db_data = pkgi_load(dbpath);
    if (db_data.empty())
        return;

    const std::string sys_dir = pkgi_mode_to_system_dir(mode);

    auto ptr = reinterpret_cast<char*>(db_data.data());
    const auto data_end = reinterpret_cast<char*>(db_data.data() + db_data.size());

    unsigned line_num = 0;
    while (ptr < data_end)
    {
        ++line_num;

        // Find end of line
        char* line_end = ptr;
        while (line_end < data_end && *line_end != '\n' && *line_end != '\r')
            line_end++;

        if (line_end == ptr)
        {
            // skip empty line
            while (ptr < data_end && (*ptr == '\n' || *ptr == '\r'))
                ptr++;
            continue;
        }

        std::string line(ptr, line_end);

        // advance past newline(s)
        ptr = line_end;
        while (ptr < data_end && (*ptr == '\n' || *ptr == '\r'))
            ptr++;

        try
        {
            const auto fields = split_pipe(line);
            if (fields.size() < 2)
                continue;

            const std::string& identifier = fields[0];
            const std::string& title      = fields[1];
            const std::string  date  = (fields.size() > 2) ? fields[2] : "";
            int64_t size_val = 0;
            if (fields.size() > 3 && !fields[3].empty())
            {
                try { size_val = std::stoll(fields[3]); }
                catch (...) { size_val = 0; }
            }

            if (identifier.empty())
                continue;

            ++_title_count;

            // Apply search filter
            if (!search.empty() &&
                !pkgi_stricontains(title.c_str(), search.c_str()) &&
                !pkgi_stricontains(identifier.c_str(), search.c_str()))
                continue;

            // Construct Archive.org direct download URL
            // Pattern: https://archive.org/download/<id>/<id>.zip
            const std::string url = "https://archive.org/download/" +
                                    identifier + "/" + identifier + ".zip";

            if (db.size() >= MAX_DB_ITEMS)
                break;

            db.push_back(DbItem{
                    /*presence=*/PresenceUnknown,
                    /*titleid=*/identifier,
                    /*content=*/identifier,
                    /*flags=*/0,
                    /*name=*/title.empty() ? identifier : title,
                    /*name_org=*/"",
                    /*zrif=*/"",
                    /*url=*/url,
                    /*has_digest=*/false,
                    /*digest=*/{},
                    /*size=*/size_val,
                    /*date=*/date,
                    /*app_version=*/"",
                    /*fw_version=*/"",
                    /*selected=*/false,
                    /*system=*/sys_dir,
                    /*description=*/"",
                    /*user_flag=*/UserFlag::None,
                    /*user_comment=*/"",
            });
        }
        catch (const std::exception& e)
        {
            LOGFW("Failed to parse line {}: {}", line_num, e.what());
        }
    }

    std::sort(
            db.begin(),
            db.end(),
            [&](const auto& a, const auto& b)
            { return lower(a, b, sort_by, sort_order); });

    LOGF("Database loaded: {}/{} items", db.size(), _title_count);
}

void TitleDatabase::get_update_status(uint32_t* updated, uint32_t* total)
{
    *updated = db_size;
    *total   = db_total;
}

uint32_t TitleDatabase::count()
{
    return db.size();
}

uint32_t TitleDatabase::total()
{
    return _title_count;
}

DbItem* TitleDatabase::get(uint32_t index)
{
    return index < db.size() ? &db[index] : nullptr;
}

DbItem* TitleDatabase::get_by_content(const char* content)
{
    for (size_t i = 0; i < db.size(); ++i)
        if (db[i].content == content)
            return &db[i];
    return nullptr;
}

// Keep this for any remaining callers (e.g. browserview sort)
GameRegion pkgi_get_region(const std::string& /*titleid*/)
{
    return RegionUnknown;
}
