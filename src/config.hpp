#pragma once

#include "db.hpp"

#include <array>
#include <map>
#include <string>

struct CustomConfigEntry
{
    std::string name;
    std::string url;
};

typedef struct Config
{
    DbSort sort;
    DbSortOrder order;
    uint32_t filter;
    int no_version_check;
    std::string install_psp_psx_location; // kept for compatibility

    // Archive.org item identifier per system, keyed by SystemDef::id.
    // Empty/absent -> fall back to the table's default_item.
    std::map<std::string, std::string> system_urls;

    // Compatibility-pack URL (unused for ROMs, kept to avoid build breakage)
    std::string comppack_url;

    // Image panel settings
    std::string thumbnail_url;
    std::string thumbnail_folder;
    int thumbnail_size{2};

    std::array<CustomConfigEntry, 5> custom_entries;
} Config;

Config pkgi_load_config();
void pkgi_save_config(const Config& config);

// Resolve the configured Archive.org item for a mode (explicit override or default).
std::string pkgi_config_url(const Config& config, int mode);
