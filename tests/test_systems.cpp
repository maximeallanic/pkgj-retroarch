#include "doctest.h"
#include "systems.hpp"

#include <set>

TEST_CASE("historical modes keep their numeric positions")
{
    REQUIRE(pkgi_mode_count() >= 8);
    CHECK(pkgi_system(0).id == "gb");
    CHECK(pkgi_system(1).id == "gbc");
    CHECK(pkgi_system(2).id == "gba");
    CHECK(pkgi_system(3).id == "snes");
    CHECK(pkgi_system(4).id == "nes");
    CHECK(pkgi_system(5).id == "genesis");
    CHECK(pkgi_system(6).id == "ps1");
    CHECK(pkgi_system(7).id == "psp");
}

TEST_CASE("system directory and cache names match the legacy scheme")
{
    CHECK(pkgi_system_by_id("gb")->roms_dir == "gb");
    CHECK(pkgi_system_by_id("genesis")->roms_dir == "megadrive");
    CHECK(pkgi_system_by_id("ps1")->roms_dir == "psx");
    CHECK(pkgi_system_by_id("gb")->cache_file == "roms_gb.dat");
    CHECK(pkgi_system_by_id("ps1")->cache_file == "roms_ps1.dat");
}

TEST_CASE("extension matching accepts the system's own formats")
{
    const auto& gb = *pkgi_system_by_id("gb");
    CHECK(pkgi_matches_extension(gb, "tetris.gb"));
    CHECK_FALSE(pkgi_matches_extension(gb, "mario.nes"));
    CHECK(pkgi_matches_extension(gb, "tetris.zip"));   // archives universal
    CHECK(pkgi_matches_extension(gb, "tetris.7z"));
    CHECK_FALSE(pkgi_matches_extension(gb, "readme"));

    const auto& snes = *pkgi_system_by_id("snes");
    CHECK(pkgi_matches_extension(snes, "chrono.sfc"));
    CHECK(pkgi_matches_extension(snes, "chrono.smc"));
    CHECK(pkgi_matches_extension(snes, "CHRONO.SFC"));  // caller lowercases; test the given form
}

TEST_CASE("gbc also accepts .gb")
{
    const auto& gbc = *pkgi_system_by_id("gbc");
    CHECK(pkgi_matches_extension(gbc, "zelda.gbc"));
    CHECK(pkgi_matches_extension(gbc, "zelda.gb"));
}

TEST_CASE("exclusion rules drop collections and bookkeeping files")
{
    CHECK(pkgi_is_excluded_file("supergame collection.zip"));
    CHECK(pkgi_is_excluded_file("item_meta.xml"));
    CHECK(pkgi_is_excluded_file("item_files.xml"));
    CHECK(pkgi_is_excluded_file("item_reviews.xml"));
    CHECK(pkgi_is_excluded_file("thing.torrent"));
    CHECK(pkgi_is_excluded_file("index.sqlite"));
    CHECK(pkgi_is_excluded_file("anything.xml"));
    CHECK(pkgi_is_excluded_file("the romset pack.zip"));
    CHECK(pkgi_is_excluded_file("full_rom_pack.zip"));
    CHECK_FALSE(pkgi_is_excluded_file("tetris.gb"));
}

TEST_CASE("table integrity: unique ids, cache files, config keys; non-empty dirs")
{
    std::set<std::string> ids, caches, keys;
    for (const auto& s : pkgi_systems())
    {
        CHECK_FALSE(s.roms_dir.empty());
        CHECK(ids.insert(s.id).second);
        CHECK(caches.insert(s.cache_file).second);
        CHECK(keys.insert(s.config_key).second);
    }
}

TEST_CASE("new consoles are present and match their formats")
{
    CHECK(pkgi_mode_count() >= 20);

    struct Case { const char* id; const char* good; const char* bad; };
    const Case cases[] = {
        {"mastersystem", "sonic.sms",  "sonic.nes"},
        {"gamegear",     "sonic.gg",   "sonic.sms"},
        {"pcengine",     "bonk.pce",   "bonk.gb"},
        {"ngp",          "smt.ngp",    "smt.gb"},
        {"wonderswan",   "gunpey.ws",  "gunpey.gb"},
        {"atari2600",    "combat.a26", "combat.gb"},
        {"atari7800",    "dig.a78",    "dig.gb"},
        {"lynx",         "shadow.lnx", "shadow.gb"},
        {"msx",          "aleste.rom", "aleste.gb"},
        {"colecovision", "venture.col","venture.gb"},
        {"virtualboy",   "wario.vb",   "wario.gb"},
    };
    for (const auto& c : cases)
    {
        const SystemDef* s = pkgi_system_by_id(c.id);
        REQUIRE_MESSAGE(s != nullptr, c.id);
        CHECK(pkgi_matches_extension(*s, c.good));
        CHECK_FALSE(pkgi_matches_extension(*s, c.bad));
        CHECK(pkgi_matches_extension(*s, "anything.zip"));  // archives universal
    }
}

TEST_CASE("wonderswan color and ngp color extensions are accepted")
{
    CHECK(pkgi_matches_extension(*pkgi_system_by_id("wonderswan"), "game.wsc"));
    CHECK(pkgi_matches_extension(*pkgi_system_by_id("ngp"), "game.ngc"));
}
