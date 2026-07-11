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
            {".gb"},                         InstallRetroArchRom, "theentiregameboycollection", "url_gb",
            SourceKind::ArchiveOrgRom},
        {"gbc",     "Game Boy Color",   "gbc",       "roms_gbc.dat",
            {".gbc", ".gb"},                 InstallRetroArchRom,
            "httpsarchive.orgdetailsnintendo-gameboy-color-full-rom-archive", "url_gbc",
            SourceKind::ArchiveOrgRom},
        {"gba",     "Game Boy Advance", "gba",       "roms_gba.dat",
            {".gba"},                        InstallRetroArchRom,
            "2DisneyGamesDisneySportsFootballDisneySportsSkateboardingEuropeEnFrDeEsIt", "url_gba",
            SourceKind::ArchiveOrgRom},
        {"snes",    "Super Nintendo",   "snes",      "roms_snes.dat",
            {".sfc", ".smc"},                InstallRetroArchRom, "FinalFantasyIII", "url_snes",
            SourceKind::ArchiveOrgRom},
        {"nes",     "Nintendo NES",     "nes",       "roms_nes.dat",
            {".nes"},                        InstallRetroArchRom, "FullNes", "url_nes",
            SourceKind::ArchiveOrgRom},
        {"genesis", "Sega Mega Drive",  "megadrive", "roms_genesis.dat",
            {".md", ".gen", ".smd", ".bin"}, InstallRetroArchRom,
            "cylums-sega-genesis-big-rom-collection-10-08-2025", "url_genesis",
            SourceKind::ArchiveOrgRom},
        {"ps1",     "PlayStation 1",    "psx",       "roms_ps1.dat",
            {".chd", ".pbp", ".cue", ".iso", ".img"}, InstallRetroArchRom,
            "cylums-playstation-rom-collection", "url_ps1",
            SourceKind::ArchiveOrgRom},
        {"psp",     "PSP",              "psp",       "roms_psp.dat",
            {".iso", ".cso", ".pbp"},        InstallRetroArchRom, "", "url_psp",
            SourceKind::ArchiveOrgRom},
        {"mastersystem", "Master System",     "mastersystem", "roms_mastersystem.dat",
            {".sms"},                 InstallRetroArchRom, "", "url_mastersystem",
            SourceKind::ArchiveOrgRom},
        {"gamegear",     "Game Gear",         "gamegear",     "roms_gamegear.dat",
            {".gg"},                  InstallRetroArchRom, "", "url_gamegear",
            SourceKind::ArchiveOrgRom},
        {"pcengine",     "PC Engine / TG-16", "pcengine",     "roms_pcengine.dat",
            {".pce"},                 InstallRetroArchRom, "", "url_pcengine",
            SourceKind::ArchiveOrgRom},
        {"ngp",          "Neo Geo Pocket",    "ngp",          "roms_ngp.dat",
            {".ngp", ".ngc"},         InstallRetroArchRom, "", "url_ngp",
            SourceKind::ArchiveOrgRom},
        {"wonderswan",   "WonderSwan",        "wswan",        "roms_wonderswan.dat",
            {".ws", ".wsc"},          InstallRetroArchRom, "", "url_wonderswan",
            SourceKind::ArchiveOrgRom},
        {"atari2600",    "Atari 2600",        "atari2600",    "roms_atari2600.dat",
            {".a26", ".bin"},         InstallRetroArchRom, "", "url_atari2600",
            SourceKind::ArchiveOrgRom},
        {"atari7800",    "Atari 7800",        "atari7800",    "roms_atari7800.dat",
            {".a78"},                 InstallRetroArchRom, "", "url_atari7800",
            SourceKind::ArchiveOrgRom},
        {"lynx",         "Atari Lynx",        "lynx",         "roms_lynx.dat",
            {".lnx"},                 InstallRetroArchRom, "", "url_lynx",
            SourceKind::ArchiveOrgRom},
        {"neogeo",       "Neo Geo",           "neogeo",       "roms_neogeo.dat",
            {},                       InstallRetroArchRom, "", "url_neogeo",
            SourceKind::ArchiveOrgRom},
        {"msx",          "MSX",               "msx",          "roms_msx.dat",
            {".rom", ".mx1", ".mx2", ".dsk"}, InstallRetroArchRom, "", "url_msx",
            SourceKind::ArchiveOrgRom},
        {"colecovision", "ColecoVision",      "colecovision", "roms_colecovision.dat",
            {".col"},                 InstallRetroArchRom, "", "url_colecovision",
            SourceKind::ArchiveOrgRom},
        {"virtualboy",   "Virtual Boy",       "virtualboy",   "roms_virtualboy.dat",
            {".vb"},                  InstallRetroArchRom, "", "url_virtualboy",
            SourceKind::ArchiveOrgRom},
        {"n64",          "Nintendo 64",       "n64",          "roms_n64.dat",
            {".n64", ".z64", ".v64"}, InstallRetroArchRom, "", "url_n64",
            SourceKind::ArchiveOrgRom},
        {"psvita", "PS Vita Games", "", "nps_psvita.dat",
            {}, InstallVitaNative, "http://nopaystation.com/tsv/PSV_GAMES.tsv",
            "url_psvita", SourceKind::NpsVita},
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
