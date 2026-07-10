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

static bool ends_with(const std::string& s, const char* suffix)
{
    const size_t n = strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

// Is this basename (lowercased) a ROM playable for the given system?
// Zipped/7z ROMs are loadable by RetroArch cores for every cartridge system.
static bool is_rom_file(Mode mode, const std::string& base_lower)
{
    if (ends_with(base_lower, ".zip") || ends_with(base_lower, ".7z"))
        return true;
    switch (mode)
    {
    case ModeGB:  return ends_with(base_lower, ".gb");
    case ModeGBC: return ends_with(base_lower, ".gbc") ||
                         ends_with(base_lower, ".gb");
    case ModeGBA: return ends_with(base_lower, ".gba");
    case ModeSNES: return ends_with(base_lower, ".sfc") ||
                          ends_with(base_lower, ".smc");
    case ModeNES: return ends_with(base_lower, ".nes");
    case ModeGenesis: return ends_with(base_lower, ".md") ||
                             ends_with(base_lower, ".gen") ||
                             ends_with(base_lower, ".smd") ||
                             ends_with(base_lower, ".bin");
    case ModePS1: return ends_with(base_lower, ".chd") ||
                         ends_with(base_lower, ".pbp") ||
                         ends_with(base_lower, ".cue") ||
                         ends_with(base_lower, ".iso") ||
                         ends_with(base_lower, ".img");
    case ModePSP: return ends_with(base_lower, ".iso") ||
                         ends_with(base_lower, ".cso") ||
                         ends_with(base_lower, ".pbp");
    }
    return false;
}

// Files to skip: the single combined archive some items ship alongside the
// individual games, plus Archive.org bookkeeping files.
static bool is_excluded_file(const std::string& base_lower)
{
    if (base_lower.find("rom collection") != std::string::npos ||
        base_lower.find("full_rom_pack") != std::string::npos ||
        base_lower.find("romset") != std::string::npos)
        return true;
    return ends_with(base_lower, "collection.zip") ||
           ends_with(base_lower, "_meta.xml") ||
           ends_with(base_lower, "_files.xml") ||
           ends_with(base_lower, "_reviews.xml") ||
           ends_with(base_lower, ".torrent") ||
           ends_with(base_lower, ".sqlite") ||
           ends_with(base_lower, ".xml");
}

static std::string to_lower(std::string s)
{
    for (char& c : s)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return s;
}

static std::string basename_of(const std::string& path)
{
    const auto pos = path.rfind('/');
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

// Minimal JSON string unescape for the escapes Archive.org actually emits.
static std::string json_unescape(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '\\' && i + 1 < s.size())
        {
            const char n = s[i + 1];
            if (n == '/' || n == '\\' || n == '"')
            {
                out += n;
                ++i;
                continue;
            }
        }
        out += s[i];
    }
    return out;
}

// Percent-encode a path for use in an Archive.org download URL. '/' separators
// are preserved; unreserved characters pass through; everything else is escaped.
static std::string url_encode_path(const std::string& path)
{
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(path.size() * 2);
    for (unsigned char c : path)
    {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
            c == '~' || c == '/')
        {
            out += static_cast<char>(c);
        }
        else
        {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0x0F];
        }
    }
    return out;
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
// update() — Fetch an Archive.org item's metadata and cache its ROM file list
//
// update_url is an Archive.org *item identifier* (the first one if a
// comma-separated list is given). We GET https://archive.org/metadata/<id>
// and walk its "files" array, keeping the entries whose extension matches the
// system. Each ROM file becomes one cache line:  item_id|file_name|size
// ---------------------------------------------------------------------------
void TitleDatabase::update(Mode mode, Http* http, const std::string& update_url)
{
    db_total = 0;
    db_size = 0;

    // Take the first item identifier if several are configured.
    std::string item_id = update_url;
    const auto comma = item_id.find(',');
    if (comma != std::string::npos)
        item_id = item_id.substr(0, comma);
    // Trim surrounding whitespace.
    while (!item_id.empty() && (item_id.front() == ' ' || item_id.front() == '\t'))
        item_id.erase(item_id.begin());
    while (!item_id.empty() && (item_id.back() == ' ' || item_id.back() == '\t' ||
                                item_id.back() == '\r' || item_id.back() == '\n'))
        item_id.pop_back();

    if (item_id.empty())
        throw std::runtime_error(
                "no Archive.org item configured for this system");

    const std::string meta_url = "https://archive.org/metadata/" + item_id;
    LOGF("Fetching ROM metadata from Archive.org: {}", meta_url);

    http->start(meta_url, 0);

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

    // Locate the "files":[ array
    const char* files_key = "\"files\":[";
    const char* files_start = nullptr;
    for (size_t i = 0; i + strlen(files_key) <= json_len; ++i)
    {
        if (memcmp(json + i, files_key, strlen(files_key)) == 0)
        {
            files_start = json + i + strlen(files_key);
            break;
        }
    }

    if (!files_start)
        throw std::runtime_error("Archive.org metadata missing 'files' array");

    const char* json_end = json + json_len;

    // Write cache file (pipe-delimited: item_id|file_name|size)
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
    const char* p = files_start;
    while (p < json_end && *p != ']')
    {
        if (*p == '{')
        {
            const char* obj_end = find_object_end(p, json_end);
            if (!obj_end)
                break;

            const size_t obj_len = static_cast<size_t>(obj_end - p + 1);

            const std::string name = json_unescape(json_str(p, obj_len, "name"));
            // "size" is a JSON string in item metadata, e.g. "size":"552536"
            const std::string size = json_str(p, obj_len, "size");

            const std::string base_lower = to_lower(basename_of(name));

            if (!name.empty() && is_rom_file(mode, base_lower) &&
                !is_excluded_file(base_lower))
            {
                const std::string line = item_id + "|" + name + "|" +
                                         (size.empty() ? "0" : size) + "\n";
                pkgi_write(
                        item_file, line.data(),
                        static_cast<uint32_t>(line.size()));
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

    LOGF("Archive.org database cached: {} ROMs → {}", count, filepath);
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
            // Cache line format: item_id|file_name|size
            const auto fields = split_pipe(line);
            if (fields.size() < 2)
                continue;

            const std::string& item_id   = fields[0];
            const std::string& file_name = fields[1];
            int64_t size_val = 0;
            if (fields.size() > 2 && !fields[2].empty())
            {
                try { size_val = std::stoll(fields[2]); }
                catch (...) { size_val = 0; }
            }

            if (item_id.empty() || file_name.empty())
                continue;

            // Display name: file base name without its extension.
            std::string title = basename_of(file_name);
            const auto dot = title.rfind('.');
            if (dot != std::string::npos && dot != 0)
                title = title.substr(0, dot);

            ++_title_count;

            // Apply search filter
            if (!search.empty() &&
                !pkgi_stricontains(title.c_str(), search.c_str()) &&
                !pkgi_stricontains(file_name.c_str(), search.c_str()))
                continue;

            // Archive.org direct download URL for this individual ROM file.
            const std::string url = "https://archive.org/download/" + item_id +
                                    "/" + url_encode_path(file_name);

            // Unique key within the system list (used for temp path + presence).
            const std::string content = basename_of(file_name);

            if (db.size() >= MAX_DB_ITEMS)
                break;

            db.push_back(DbItem{
                    /*presence=*/PresenceUnknown,
                    /*titleid=*/content,
                    /*content=*/content,
                    /*flags=*/0,
                    /*name=*/title.empty() ? content : title,
                    /*name_org=*/"",
                    /*zrif=*/"",
                    /*url=*/url,
                    /*has_digest=*/false,
                    /*digest=*/{},
                    /*size=*/size_val,
                    /*date=*/"",
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
