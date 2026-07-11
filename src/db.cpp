#include "db.hpp"

#include "file.hpp"
#include "jsonscan.hpp"
#include "pkgi.hpp"
#include "systems.hpp"
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
    return pkgi_system(mode).display_name;
}

// Directory name used under ux0:roms/
std::string pkgi_mode_to_system_dir(Mode mode)
{
    return pkgi_system(mode).roms_dir;
}

TitleDatabase::TitleDatabase(const std::string& dbPath) : _dbPath(dbPath)
{
}

// Cache filename per system (pipe-delimited text)
static const char* pkgi_mode_to_file_name(Mode mode)
{
    return pkgi_system(mode).cache_file.c_str();
}

// ---------------------------------------------------------------------------
// Minimal JSON field extractor (no external dependency)
// Works on a null-terminated JSON substring.
// ---------------------------------------------------------------------------
namespace
{

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

    // Resolve the download host+path. We MUST use the top-level "d1" server plus
    // the top-level "dir" (the classic ia*.archive.org fleet, GoDaddy RSA certs
    // the Vita's SSL stack can handle). The generic /download/ URL 302-redirects
    // to newer dn*.archive.org nodes that serve ECDSA-only TLS, which the Vita
    // cannot negotiate. alternate_locations also carries a server/dir but for
    // those dn* nodes — so grab the "dir" that appears at/after "d1", not the
    // first one in the document.
    const std::string d1 = pkgi_json_str(json, json_len, "d1");
    std::string dir;
    {
        const char* d1pat = "\"d1\":\"";
        const size_t d1len = strlen(d1pat);
        const char* pos = nullptr;
        for (size_t i = 0; i + d1len <= json_len; ++i)
            if (memcmp(json + i, d1pat, d1len) == 0)
            {
                pos = json + i;
                break;
            }
        const char* from = pos ? pos : json;
        dir = pkgi_json_str(from, json_len - static_cast<size_t>(from - json), "dir");
    }

    std::string base_url;
    if (!d1.empty() && !dir.empty())
        base_url = "https://" + d1 + dir; // dir already begins with '/'
    else
        // Fallback (may fail on Vita SSL if it lands on a dn* node).
        base_url = "https://archive.org/download/" + item_id;

    LOGF("Archive.org download base: {}", base_url);

    // Write cache file (pipe-delimited: file_name|size|download_url)
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
            const char* obj_end = pkgi_find_object_end(p, json_end);
            if (!obj_end)
                break;

            const size_t obj_len = static_cast<size_t>(obj_end - p + 1);

            const std::string name = pkgi_json_unescape(pkgi_json_str(p, obj_len, "name"));
            // "size" is a JSON string in item metadata, e.g. "size":"552536"
            const std::string size = pkgi_json_str(p, obj_len, "size");

            const std::string base_lower = to_lower(basename_of(name));

            if (!name.empty() && pkgi_matches_extension(pkgi_system(mode), base_lower) &&
                !pkgi_is_excluded_file(base_lower))
            {
                const std::string url = base_url + "/" + pkgi_url_encode_path(name);
                const std::string line = name + "|" +
                                         (size.empty() ? "0" : size) + "|" +
                                         url + "\n";
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
            // Cache line format: file_name|size|download_url
            const auto fields = split_pipe(line);
            if (fields.size() < 2)
                continue;

            const std::string& file_name = fields[0];
            int64_t size_val = 0;
            if (fields.size() > 1 && !fields[1].empty())
            {
                try { size_val = std::stoll(fields[1]); }
                catch (...) { size_val = 0; }
            }
            const std::string& url = (fields.size() > 2) ? fields[2] : fields[0];

            if (file_name.empty() || url.empty())
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
