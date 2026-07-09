#pragma once

#include "db.hpp"

#include <array>
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

    // Archive.org collection search URLs per RetroArch system
    std::string gb_url;       // Game Boy
    std::string gbc_url;      // Game Boy Color
    std::string gba_url;      // Game Boy Advance
    std::string snes_url;     // Super Nintendo
    std::string nes_url;      // Nintendo NES
    std::string genesis_url;  // Sega Mega Drive/Genesis
    std::string ps1_url;      // PlayStation 1
    std::string psp_url;      // PlayStation Portable

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
