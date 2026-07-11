#include "systems.hpp"

#include <algorithm>

namespace
{
// Case-insensitive suffix match. Callers are expected to pass an already
// lowercased basename, but pkgi_matches_extension is tolerant of mixed
// case input so a caller that forgets to lowercase still gets a correct
// answer instead of a silent false negative.
bool ends_with(const std::string& s, const std::string& suffix)
{
    if (s.size() < suffix.size())
        return false;
    return std::equal(
            suffix.rbegin(), suffix.rend(), s.rbegin(),
            [](char a, char b) {
                if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
                if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
                return a == b;
            });
}
}

const std::vector<SystemDef>& pkgi_systems()
{
    // NOTE: indices 0..7 are historical Mode values and must not move.
    static const std::vector<SystemDef> table = {
        {"gb",      "Game Boy",         "gb",        "roms_gb.dat",
            {".gb"},                         InstallRetroArchRom, "theentiregameboycollection", "url_gb"},
        {"gbc",     "Game Boy Color",   "gbc",       "roms_gbc.dat",
            {".gbc", ".gb"},                 InstallRetroArchRom,
            "httpsarchive.orgdetailsnintendo-gameboy-color-full-rom-archive", "url_gbc"},
        {"gba",     "Game Boy Advance", "gba",       "roms_gba.dat",
            {".gba"},                        InstallRetroArchRom,
            "2DisneyGamesDisneySportsFootballDisneySportsSkateboardingEuropeEnFrDeEsIt", "url_gba"},
        {"snes",    "Super Nintendo",   "snes",      "roms_snes.dat",
            {".sfc", ".smc"},                InstallRetroArchRom, "FinalFantasyIII", "url_snes"},
        {"nes",     "Nintendo NES",     "nes",       "roms_nes.dat",
            {".nes"},                        InstallRetroArchRom, "FullNes", "url_nes"},
        {"genesis", "Sega Mega Drive",  "megadrive", "roms_genesis.dat",
            {".md", ".gen", ".smd", ".bin"}, InstallRetroArchRom,
            "cylums-sega-genesis-big-rom-collection-10-08-2025", "url_genesis"},
        {"ps1",     "PlayStation 1",    "psx",       "roms_ps1.dat",
            {".chd", ".pbp", ".cue", ".iso", ".img"}, InstallRetroArchRom,
            "cylums-playstation-rom-collection", "url_ps1"},
        {"psp",     "PSP",              "psp",       "roms_psp.dat",
            {".iso", ".cso", ".pbp"},        InstallRetroArchRom, "", "url_psp"},
    };
    return table;
}

int pkgi_mode_count()
{
    return static_cast<int>(pkgi_systems().size());
}

const SystemDef& pkgi_system(int mode)
{
    const auto& t = pkgi_systems();
    if (mode < 0 || mode >= static_cast<int>(t.size()))
        return t.front();  // Mode out of range is a bug; clamp to keep UI alive.
    return t[static_cast<size_t>(mode)];
}

const SystemDef* pkgi_system_by_id(const std::string& id)
{
    for (const auto& s : pkgi_systems())
        if (s.id == id)
            return &s;
    return nullptr;
}

bool pkgi_matches_extension(const SystemDef& sys, const std::string& base_lower)
{
    if (ends_with(base_lower, ".zip") || ends_with(base_lower, ".7z"))
        return true;
    for (const auto& ext : sys.extensions)
        if (ends_with(base_lower, ext))
            return true;
    return false;
}

bool pkgi_is_excluded_file(const std::string& base_lower)
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
