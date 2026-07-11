#pragma once

#include <string>
#include <vector>

enum InstallKind
{
    InstallRetroArchRom,   // copy into ux0:roms/<roms_dir>/  (Phase 0+1)
    InstallPspAdrenaline,  // reserved — Phase 2
    InstallVitaNative,     // reserved — Phase 3
};

struct SystemDef
{
    std::string id;            // stable key, e.g. "gb", "snes"
    std::string display_name;  // "Game Boy"
    std::string roms_dir;      // dir under ux0:roms/
    std::string cache_file;    // "roms_gb.dat"
    std::vector<std::string> extensions;  // e.g. {".gb"}; .zip/.7z always accepted too
    InstallKind install;
    std::string default_item;  // default Archive.org identifier ("" if none)
    std::string config_key;    // "url_gb"
};

// The system table. Index order == UI order == Mode value.
const std::vector<SystemDef>& pkgi_systems();
int pkgi_mode_count();
const SystemDef& pkgi_system(int mode);
const SystemDef* pkgi_system_by_id(const std::string& id);

// base_lower is a lowercased basename (no directory).
bool pkgi_matches_extension(const SystemDef& sys, const std::string& base_lower);
bool pkgi_is_excluded_file(const std::string& base_lower);
