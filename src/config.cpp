#include "config.hpp"

#include <cstdlib>

#include <algorithm>

#include <fmt/format.h>

#include "file.hpp"
#include "pkgi.hpp"
#include "systems.hpp"

// Default Archive.org item identifiers per system now live in the
// data-driven table in systems.cpp (SystemDef::default_item). update()
// fetches https://archive.org/metadata/<id> and lists the individual ROM
// files found in that item's "files" array. Override any of them from
// ux0:pkgj/config.txt with e.g.:  url_snes <item_id>
//
// PSP has no good per-file item on Archive.org at the moment, so its
// default_item is left empty (refresh disabled) until a suitable item
// identifier is configured.

// Comppack URL is unused for ROM mode but kept to avoid build errors
static constexpr char default_comppack_url[] = {0};

static char* skipnonws(char* text, char* end)
{
    while (text < end && *text != ' ' && *text != '\n' && *text != '\r')
        text++;
    return text;
}

static char* skipws(char* text, char* end)
{
    while (text < end && (*text == ' ' || *text == '\n' || *text == '\r'))
        text++;
    return text;
}

static DbSort parse_sort(const char* value, DbSort sort)
{
    if (pkgi_stricmp(value, "title") == 0)
        return SortByTitle;
    else if (pkgi_stricmp(value, "region") == 0)
        return SortByRegion;
    else if (pkgi_stricmp(value, "name") == 0)
        return SortByName;
    else if (pkgi_stricmp(value, "size") == 0)
        return SortBySize;
    else if (pkgi_stricmp(value, "date") == 0)
        return SortByDate;
    else
        return sort;
}

static DbSortOrder parse_order(const char* value, DbSortOrder order)
{
    if (pkgi_stricmp(value, "asc") == 0)
        return SortAscending;
    else if (pkgi_stricmp(value, "desc") == 0)
        return SortDescending;
    else
        return order;
}

static DbFilter parse_filter(char* value, uint32_t filter)
{
    uint32_t result = 0;

    char* start = value;
    for (;;)
    {
        char ch = *value;
        if (ch == 0 || ch == ',')
        {
            *value = 0;
            if (pkgi_stricmp(start, "ASA") == 0)
                result |= DbFilterRegionASA;
            else if (pkgi_stricmp(start, "EUR") == 0)
                result |= DbFilterRegionEUR;
            else if (pkgi_stricmp(start, "JPN") == 0)
                result |= DbFilterRegionJPN;
            else if (pkgi_stricmp(start, "USA") == 0)
                result |= DbFilterRegionUSA;
            else
                return static_cast<DbFilter>(filter);
            if (ch == 0)
                break;
            value++;
            start = value;
        }
        else
        {
            value++;
        }
    }
    return static_cast<DbFilter>(result);
}

static int parse_custom_index(const char* key)
{
    if (pkgi_stricmp(key, "custom1") == 0) return 0;
    if (pkgi_stricmp(key, "custom2") == 0) return 1;
    if (pkgi_stricmp(key, "custom3") == 0) return 2;
    if (pkgi_stricmp(key, "custom4") == 0) return 3;
    if (pkgi_stricmp(key, "custom5") == 0) return 4;
    return -1;
}

static char* skipline(char* text, char* end)
{
    while (text < end && *text != '\n' && *text != '\r')
        text++;
    while (text < end && (*text == '\n' || *text == '\r'))
        text++;
    return text;
}

static void parse_custom_entry(
        char* value,
        char* end,
        CustomConfigEntry* entry)
{
    entry->name.clear();
    entry->url.clear();

    value = skipws(value, end);
    if (value == end || *value != '"')
        return;

    ++value;
    char* name_start = value;
    while (value < end && *value != '"' && *value != '\n' && *value != '\r')
        value++;
    if (value == end || *value != '"')
        return;

    *value++ = 0;
    entry->name = name_start;

    value = skipws(value, end);
    if (value == end || *value == '\n' || *value == '\r')
    {
        entry->name.clear();
        return;
    }

    char* url_start = value;
    while (value < end && *value != '\n' && *value != '\r')
        value++;

    char* url_end = value;
    while (url_end > url_start && (url_end[-1] == ' ' || url_end[-1] == '\t'))
        --url_end;
    *url_end = 0;

    entry->url = url_start;
    if (entry->url.empty())
        entry->name.clear();
}

Config pkgi_load_config()
{
    try
    {
        Config config{};
        config.no_version_check = 0;

        // system_urls starts empty; pkgi_config_url() falls back to each
        // system's table default_item when no explicit override is set.
        config.comppack_url = "";
        config.thumbnail_url = "";
        config.thumbnail_folder = "";
        config.thumbnail_size = 2;
        config.sort   = SortByName;
        config.order  = SortAscending;
        config.filter = DbFilterAll;
        config.install_psp_psx_location = "ux0:";

        const auto path =
                fmt::format("{}/config.txt", pkgi_get_config_folder());
        LOGF("Config file path: {}", path);

        if (!pkgi_file_exists(path))
            return config;

        auto data = pkgi_load(path);
        data.push_back('\n');

        LOG("Parsing config.txt");
        auto text = reinterpret_cast<char*>(data.data());
        const auto end = text + data.size();

        while (text < end)
        {
            const auto key = text;

            text = skipnonws(text, end);
            if (text == end)
                break;

            *text++ = 0;

            text = skipws(text, end);
            if (text == end)
                break;

            const int custom_index = parse_custom_index(key);
            if (custom_index >= 0)
            {
                char* line_end = text;
                while (line_end < end && *line_end != '\n' && *line_end != '\r')
                    line_end++;
                parse_custom_entry(
                        text, line_end, &config.custom_entries[custom_index]);
                text = skipline(line_end, end);
                continue;
            }

            const auto value = text;

            text = skipnonws(text, end);
            if (text == end)
                break;

            *text++ = 0;

            text = skipws(text, end);

            // RetroArch system URL keys (table-driven; keeps legacy
            // url_gb / url_gbc / ... keys working since they are exactly
            // each SystemDef's config_key).
            bool handled_system_url = false;
            for (const auto& sys : pkgi_systems())
            {
                if (pkgi_stricmp(key, sys.config_key.c_str()) == 0)
                {
                    config.system_urls[sys.id] = value;
                    handled_system_url = true;
                    break;
                }
            }

            if (handled_system_url)
            {
                // handled above
            }
            else if (pkgi_stricmp(key, "url_comppack") == 0)
                config.comppack_url = value;
            else if (pkgi_stricmp(key, "thumbnail_url") == 0)
                config.thumbnail_url = value;
            else if (pkgi_stricmp(key, "thumbnail_folder") == 0)
                config.thumbnail_folder = value;
            else if (pkgi_stricmp(key, "thumbnail_size") == 0)
                config.thumbnail_size =
                        static_cast<int>(std::strtol(value, nullptr, 10));
            else if (pkgi_stricmp(key, "sort") == 0)
                config.sort = parse_sort(value, SortByName);
            else if (pkgi_stricmp(key, "order") == 0)
                config.order = parse_order(value, SortAscending);
            else if (pkgi_stricmp(key, "filter") == 0)
                config.filter = parse_filter(value, DbFilterAll);
            else if (pkgi_stricmp(key, "no_version_check") == 0)
                config.no_version_check = (pkgi_stricmp(value, "0") != 0);
            else if (pkgi_stricmp(key, "install_psp_psx_location") == 0)
                config.install_psp_psx_location = value;
        }
        return config;
    }
    catch (const std::exception& e)
    {
        throw formatEx<std::runtime_error>(
                "Failed to load config:\n{}", e.what());
    }
}

static const char* sort_str(DbSort sort)
{
    switch (sort)
    {
    case SortByTitle: return "title";
    case SortByRegion: return "region";
    case SortByName: return "name";
    case SortBySize: return "size";
    case SortByDate: return "date";
    }
    return "";
}

static const char* order_str(DbSortOrder order)
{
    switch (order)
    {
    case SortAscending:  return "asc";
    case SortDescending: return "desc";
    }
    return "";
}

void pkgi_save_config(const Config& config)
{
    char data[4096];
    int len = 0;

    for (const auto& sys : pkgi_systems())
    {
        auto it = config.system_urls.find(sys.id);
        if (it == config.system_urls.end() || it->second.empty())
            continue;
        if (it->second == sys.default_item)
            continue;  // don't persist a value identical to the default
        len += pkgi_snprintf(data + len, sizeof(data) - len,
                             "%s %s\n", sys.config_key.c_str(), it->second.c_str());
    }

    for (size_t i = 0; i < config.custom_entries.size(); i++)
    {
        const auto& entry = config.custom_entries[i];
        if (entry.name.empty() || entry.url.empty())
            continue;
        len += pkgi_snprintf(
                data + len,
                sizeof(data) - len,
                "custom%u \"%s\" %s\n",
                static_cast<unsigned>(i + 1),
                entry.name.c_str(),
                entry.url.c_str());
    }
    if (!config.thumbnail_url.empty())
        len += pkgi_snprintf(
                data + len,
                sizeof(data) - len,
                "thumbnail_url %s\n",
                config.thumbnail_url.c_str());
    if (!config.thumbnail_folder.empty())
        len += pkgi_snprintf(
                data + len,
                sizeof(data) - len,
                "thumbnail_folder %s\n",
                config.thumbnail_folder.c_str());
    len += pkgi_snprintf(
            data + len, sizeof(data) - len,
            "thumbnail_size %d\n", config.thumbnail_size);
    if (!config.install_psp_psx_location.empty() &&
        config.install_psp_psx_location != "ux0:")
        len += pkgi_snprintf(
                data + len,
                sizeof(data) - len,
                "install_psp_psx_location %s\n",
                config.install_psp_psx_location.c_str());
    len += pkgi_snprintf(
            data + len, sizeof(data) - len, "sort %s\n", sort_str(config.sort));
    len += pkgi_snprintf(
            data + len, sizeof(data) - len,
            "order %s\n", order_str(config.order));

    // Write filter
    len += pkgi_snprintf(data + len, sizeof(data) - len, "filter ");
    const char* sep = "";
    if (config.filter & DbFilterRegionASA)
    {
        len += pkgi_snprintf(data + len, sizeof(data) - len, "%sASA", sep);
        sep = ",";
    }
    if (config.filter & DbFilterRegionEUR)
    {
        len += pkgi_snprintf(data + len, sizeof(data) - len, "%sEUR", sep);
        sep = ",";
    }
    if (config.filter & DbFilterRegionJPN)
    {
        len += pkgi_snprintf(data + len, sizeof(data) - len, "%sJPN", sep);
        sep = ",";
    }
    if (config.filter & DbFilterRegionUSA)
    {
        len += pkgi_snprintf(data + len, sizeof(data) - len, "%sUSA", sep);
        sep = ",";
    }
    len += pkgi_snprintf(data + len, sizeof(data) - len, "\n");

    const auto path = fmt::format("{}/config.txt", pkgi_get_config_folder());
    pkgi_save(path, data, len);
}
