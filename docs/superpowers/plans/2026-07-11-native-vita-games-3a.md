# Native PS Vita Games (Phase 3a — NoPayStation) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a "PS Vita Games" source that fetches the NoPayStation PSV_GAMES TSV, lists the games (excluding entries with no usable zRIF), and installs a selected game as a native `.pkg` via the fork's intact PKG install path.

**Architecture:** Extend the Phase 0+1 data-driven system table with a `SourceKind` field. A new STL-only `npstsv` unit parses NoPayStation TSV rows (fixed pkgj column layout). `db.cpp`'s `update()`/`reload()` branch on `source`: for `NpsVita` they cache the raw TSV and parse it into `DbItem`s carrying `content_id`/`url`/`zRIF`/region. Download enqueues `Type::Game` with the decoded zRIF; the untouched `do_download_package`/`pkgi_install` chain downloads, decrypts, extracts and promotes.

**Tech Stack:** C++17, CMake, doctest (already vendored), the existing PKG/zRIF machinery (`downloader.cpp`, `install.cpp`, `zrif.cpp`).

## Global Constraints

- New STL-only unit `src/npstsv.{hpp,cpp}` includes ONLY the C++ standard library — no fmt/http/sqlite/pkgi.hpp (so `./tests/run.sh` builds it with plain g++, no conan).
- `Mode` is an index into `pkgi_systems()`; the `psvita` row is APPENDED after all existing rows (historical indices and the Phase-1 consoles must not move).
- NoPayStation Vita GAMES TSV column layout (0-indexed, pkgj's proven layout): `content_id=0, region=1, name=2, pkg_url=3, zrif=4, name_org=7, size=8`. `titleid = content_id.substr(7, 9)` when `content_id.size() >= 16`, else "".
- Exclude a row (do not list / not installable) when `pkg_url` is empty, `"MISSING"`, or `"CART ONLY"`, OR `zrif == "MISSING"`. (User decision: zRIF-missing entries are excluded, not shown greyed.)
- The TSV's first line is a header — skip it when parsing the cache.
- Embedded default for `psvita`: `http://nopaystation.com/tsv/PSV_GAMES.tsv` (decoded from the old `default_psv_games_url`). Overridable via `config.txt` key `url_psvita` — this works automatically through Phase 0+1's data-driven `system_urls`; NO config.cpp change is needed.
- MVP install path is DIRECT download (`downloader.add(DownloadItem{Game,…})` → `do_download_package` → `pkgi_install`). Do NOT wire the bgdl/LiveArea path.
- zRIF decode: `int pkgi_zrif_decode(const char* str, uint8_t* rif, char* error, uint32_t error_size)` (from `zrif.hpp`); decode into a `uint8_t rif[PKGI_PSM_RIF_SIZE]` buffer (1024, safe), build the DownloadItem rif as `std::vector<uint8_t>(rif, rif + PKGI_RIF_SIZE)` (512 = `PKGI_RIF_SIZE`, the Vita RIF size). `PKGI_RIF_SIZE`/`PKGI_PSM_RIF_SIZE` are in `download.hpp`.
- `DownloadItem` field order: `{ Type type; std::string name; std::string content; std::string url; std::vector<uint8_t> rif; std::vector<uint8_t> digest; bool save_as_iso; std::string partition; std::string version; std::string system; }`.
- `db.cpp`, `pkgi.cpp`, `downloader.cpp` CANNOT be compiled locally (no conan/VitaSDK). Verify those edits by reading + grep; the real compile is the CI device build (final task). The pure `npstsv` logic IS unit-tested.
- TDD: failing test → watch fail → implement → watch pass → commit. One logical change per commit.

---

## File Structure

**New files:**
- `src/npstsv.hpp` / `src/npstsv.cpp` — NoPayStation TSV row parser (STL-only).
- `tests/test_npstsv.cpp` — unit tests for the parser.
- `tests/fixtures/nps_psv_games.tsv` — trimmed real-shaped NoPS GAMES TSV (header + valid + 2 excluded rows).

**Modified files:**
- `src/systems.hpp` — add `enum class SourceKind`; add `SourceKind source;` to `SystemDef`.
- `src/systems.cpp` — set `source` on every existing row (`SourceKind::ArchiveOrgRom`); append the `psvita` row (`SourceKind::NpsVita`).
- `tests/test_systems.cpp` — assert the `psvita` row's fields and that all existing rows are `ArchiveOrgRom`.
- `src/db.cpp` — branch `update()` and `reload()` on `pkgi_system(mode).source`.
- `src/pkgi.cpp` — `mode_to_type` branches on source; `pkgi_start_download` builds the Vita `DownloadItem` (zRIF decode) for `NpsVita`.
- `tests/run.sh`, `tests/CMakeLists.txt` — add `npstsv.cpp` + `test_npstsv.cpp`.

---

## Task 1: NoPayStation TSV parser (`npstsv.{hpp,cpp}`)

**Files:**
- Create: `src/npstsv.hpp`, `src/npstsv.cpp`, `tests/test_npstsv.cpp`, `tests/fixtures/nps_psv_games.tsv`
- Modify: `tests/run.sh`, `tests/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `struct NpsRow { std::string content_id, titleid, region, name, name_org, pkg_url, zrif; long long size; };`
  - `std::vector<std::string> pkgi_split_tsv_row(const std::string& line);`
  - `bool pkgi_parse_nps_game_row(const std::string& line, NpsRow& out);`

- [ ] **Step 1: Create the fixture**

`tests/fixtures/nps_psv_games.tsv` — a header line then three data rows (tab-separated). Row 1 valid; row 2 has `MISSING` pkg url; row 3 has `MISSING` zrif. Columns: `content_id, region, name, pkg_url, zrif, contentid2, date, name_org, size`.

Create it with this exact command (guarantees real tab characters):

```bash
printf 'Content ID\tRegion\tName\tPKG direct link\tzRIF\tContent ID\tLast Modification Date\tOriginal Name\tFile Size\n' > tests/fixtures/nps_psv_games.tsv
printf 'UP0000-PCSA00000_00-TESTGAME00000000\tUSA\tTest Game\thttp://ia.example/Test.pkg\tKO5ifQhpU9wAAAAAAAAAAA\tUP0000-PCSA00000_00-TESTGAME00000000\t2020-01-01\tTest Game Org\t123456\n' >> tests/fixtures/nps_psv_games.tsv
printf 'UP0000-PCSA00001_00-NOPKG00000000000\tEUR\tNo Pkg Game\tMISSING\tKO5ifQhpU9wAAAAAAAAAAA\tUP0000-PCSA00001_00-NOPKG00000000000\t2020-01-01\tNo Pkg Org\t0\n' >> tests/fixtures/nps_psv_games.tsv
printf 'UP0000-PCSA00002_00-NOZRIF0000000000\tJPN\tNo Zrif Game\thttp://ia.example/NoZrif.pkg\tMISSING\tUP0000-PCSA00002_00-NOZRIF0000000000\t2020-01-01\tNo Zrif Org\t654321\n' >> tests/fixtures/nps_psv_games.tsv
cat -A tests/fixtures/nps_psv_games.tsv | head -1
```

Expected: the header prints with `^I` between column names (confirms tabs).

- [ ] **Step 2: Write the failing test**

`tests/test_npstsv.cpp`:

```cpp
#include "doctest.h"
#include "npstsv.hpp"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static std::vector<std::string> fixture_lines()
{
    std::ifstream f("tests/fixtures/nps_psv_games.tsv");
    REQUIRE(f.good());
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(f, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

TEST_CASE("split_tsv_row splits on tabs")
{
    const auto f = pkgi_split_tsv_row("a\tb\tc");
    REQUIRE(f.size() == 3);
    CHECK(f[0] == "a");
    CHECK(f[1] == "b");
    CHECK(f[2] == "c");
}

TEST_CASE("a valid NoPS game row parses with titleid derived from content id")
{
    const auto lines = fixture_lines();
    REQUIRE(lines.size() == 4);   // header + 3 rows
    NpsRow row;
    REQUIRE(pkgi_parse_nps_game_row(lines[1], row));
    CHECK(row.content_id == "UP0000-PCSA00000_00-TESTGAME00000000");
    CHECK(row.titleid == "PCSA00000");     // content_id.substr(7,9)
    CHECK(row.region == "USA");
    CHECK(row.name == "Test Game");
    CHECK(row.pkg_url == "http://ia.example/Test.pkg");
    CHECK(row.zrif == "KO5ifQhpU9wAAAAAAAAAAA");
    CHECK(row.name_org == "Test Game Org");
    CHECK(row.size == 123456);
}

TEST_CASE("a row with MISSING pkg url is rejected")
{
    const auto lines = fixture_lines();
    NpsRow row;
    CHECK_FALSE(pkgi_parse_nps_game_row(lines[2], row));
}

TEST_CASE("a row with MISSING zrif is rejected")
{
    const auto lines = fixture_lines();
    NpsRow row;
    CHECK_FALSE(pkgi_parse_nps_game_row(lines[3], row));
}

TEST_CASE("CART ONLY and empty urls are rejected")
{
    NpsRow row;
    CHECK_FALSE(pkgi_parse_nps_game_row(
        "CID\tUSA\tName\tCART ONLY\tZRIF\tCID\td\torg\t1", row));
    CHECK_FALSE(pkgi_parse_nps_game_row(
        "CID\tUSA\tName\t\tZRIF\tCID\td\torg\t1", row));
}

TEST_CASE("a short content id yields an empty titleid but still parses")
{
    NpsRow row;
    REQUIRE(pkgi_parse_nps_game_row(
        "SHORT\tUSA\tName\thttp://x/y.pkg\tZRIF\tSHORT\td\torg\t42", row));
    CHECK(row.titleid == "");
    CHECK(row.size == 42);
}
```

- [ ] **Step 3: Add to runners; run to verify it fails**

Extend `tests/run.sh`'s compile line with `tests/test_npstsv.cpp` and `src/npstsv.cpp`. Add the same two files to `add_executable(pkgj_tests ...)` in `tests/CMakeLists.txt` (the test file plus `${CMAKE_SOURCE_DIR}/src/npstsv.cpp`).

Run: `./tests/run.sh`
Expected: FAIL — `fatal error: npstsv.hpp: No such file or directory`.

- [ ] **Step 4: Write `src/npstsv.hpp`**

```cpp
#pragma once

#include <string>
#include <vector>

struct NpsRow
{
    std::string content_id;
    std::string titleid;
    std::string region;
    std::string name;
    std::string name_org;
    std::string pkg_url;
    std::string zrif;
    long long size = 0;
};

// Split one line on '\t'. A trailing '\r' should already be stripped by the caller.
std::vector<std::string> pkgi_split_tsv_row(const std::string& line);

// Parse one NoPayStation PSV_GAMES data row (NOT the header).
// Column layout: content_id=0, region=1, name=2, pkg_url=3, zrif=4, name_org=7, size=8.
// Returns false (row excluded) when pkg_url is empty / "MISSING" / "CART ONLY",
// or zrif == "MISSING", or there are too few columns to reach zrif.
bool pkgi_parse_nps_game_row(const std::string& line, NpsRow& out);
```

- [ ] **Step 5: Write `src/npstsv.cpp`**

```cpp
#include "npstsv.hpp"

std::vector<std::string> pkgi_split_tsv_row(const std::string& line)
{
    std::vector<std::string> fields;
    std::string cur;
    for (char c : line)
    {
        if (c == '\t')
        {
            fields.push_back(cur);
            cur.clear();
        }
        else
        {
            cur += c;
        }
    }
    fields.push_back(cur);
    return fields;
}

bool pkgi_parse_nps_game_row(const std::string& line, NpsRow& out)
{
    const auto f = pkgi_split_tsv_row(line);
    if (f.size() < 5)             // need at least through zrif (index 4)
        return false;

    const std::string& pkg_url = f[3];
    const std::string& zrif = f[4];
    if (pkg_url.empty() || pkg_url == "MISSING" || pkg_url == "CART ONLY" ||
        zrif == "MISSING")
        return false;

    out.content_id = f[0];
    out.region = f[1];
    out.name = f[2];
    out.pkg_url = pkg_url;
    out.zrif = zrif;
    out.name_org = f.size() > 7 ? f[7] : "";
    out.titleid = out.content_id.size() >= 16 ? out.content_id.substr(7, 9) : "";

    out.size = 0;
    if (f.size() > 8 && !f[8].empty())
    {
        try { out.size = std::stoll(f[8]); }
        catch (...) { out.size = 0; }
    }
    return true;
}
```

- [ ] **Step 6: Run to verify it passes**

Run: `./tests/run.sh`
Expected: PASS — the npstsv cases green, existing suite still green.

- [ ] **Step 7: Commit**

```bash
git add src/npstsv.hpp src/npstsv.cpp tests/test_npstsv.cpp tests/fixtures/nps_psv_games.tsv tests/run.sh tests/CMakeLists.txt
git commit -m "feat: NoPayStation TSV row parser (npstsv) + tests"
```

---

## Task 2: `SourceKind` + the `psvita` table row

**Files:**
- Modify: `src/systems.hpp`, `src/systems.cpp`, `tests/test_systems.cpp`

**Interfaces:**
- Consumes: `SystemDef`, `pkgi_systems()`, `pkgi_system_by_id()` (Phase 0+1).
- Produces: `enum class SourceKind { ArchiveOrgRom, NpsVita };` and `SourceKind SystemDef::source`. A `psvita` row with `install == InstallVitaNative`, `source == SourceKind::NpsVita`, `cache_file == "nps_psvita.dat"`, `config_key == "url_psvita"`, `default_item == "http://nopaystation.com/tsv/PSV_GAMES.tsv"`.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_systems.cpp`:

```cpp
TEST_CASE("all cartridge/disc systems use the Archive.org source")
{
    for (const auto& s : pkgi_systems())
        if (s.id != "psvita")
            CHECK(s.source == SourceKind::ArchiveOrgRom);
}

TEST_CASE("the psvita row is a NoPayStation native-install source")
{
    const SystemDef* v = pkgi_system_by_id("psvita");
    REQUIRE(v != nullptr);
    CHECK(v->source == SourceKind::NpsVita);
    CHECK(v->install == InstallVitaNative);
    CHECK(v->cache_file == "nps_psvita.dat");
    CHECK(v->config_key == "url_psvita");
    CHECK(v->default_item == "http://nopaystation.com/tsv/PSV_GAMES.tsv");
    CHECK(v->display_name == "PS Vita Games");
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `./tests/run.sh`
Expected: FAIL — `SourceKind` undeclared / `source` not a member of `SystemDef`.

- [ ] **Step 3: Add `SourceKind` + field to `src/systems.hpp`**

Above `struct SystemDef`, add:

```cpp
enum class SourceKind
{
    ArchiveOrgRom,  // fetch = archive.org/metadata JSON; install = copy to ux0:roms/
    NpsVita,        // fetch = NoPayStation TSV; install = PKG promote (Type::Game)
};
```

Add a field to `SystemDef` (after `config_key`):

```cpp
    SourceKind source;
```

- [ ] **Step 4: Set `source` on every row + append `psvita` in `src/systems.cpp`**

Add `, SourceKind::ArchiveOrgRom` as the final field of EACH existing row (the 8 base systems + the 13 Phase-1 consoles). Then append, immediately before the table's closing `};`, after the `n64` row:

```cpp
        {"psvita", "PS Vita Games", "", "nps_psvita.dat",
            {}, InstallVitaNative, "http://nopaystation.com/tsv/PSV_GAMES.tsv",
            "url_psvita", SourceKind::NpsVita},
```

- [ ] **Step 5: Run to verify it passes**

Run: `./tests/run.sh`
Expected: PASS — the new psvita cases and the "all others are ArchiveOrgRom" case green; the existing table-integrity case (unique id/cache_file/config_key) still green.

- [ ] **Step 6: Commit**

```bash
git add src/systems.hpp src/systems.cpp tests/test_systems.cpp
git commit -m "feat: add SourceKind + PS Vita Games (NoPayStation) table row"
```

---

## Task 3: Branch `db.cpp` update()/reload() on source

**Files:**
- Modify: `src/db.cpp`

**Interfaces:**
- Consumes: `pkgi_system(mode).source` (Task 2), `pkgi_parse_nps_game_row` / `NpsRow` (Task 1), existing `pkgi_get_region`, region-filter helpers, `DbItem`.
- Produces: no new external interface; wires the NpsVita path into the existing `update()`/`reload()`.

> Not locally compilable (fmt/sqlite). Verify by reading + grep. The parse correctness is covered by Task 1's unit tests; this task is the wiring.

- [ ] **Step 1: Add the include and branch `update()`**

At the top of `src/db.cpp`, add `#include "npstsv.hpp"`.

In `TitleDatabase::update(Mode mode, Http* http, const std::string& update_url)`, at the very start (after `db_total = 0; db_size = 0;`), branch to a TSV path for NpsVita:

```cpp
    if (pkgi_system(mode).source == SourceKind::NpsVita)
    {
        update_nps_tsv(mode, http, update_url);
        return;
    }
```

Add a new private helper `update_nps_tsv` (declare it in `db.hpp` under `private:`), implemented in `db.cpp`. It reuses the existing HTTP-read + temp-file-then-rename pattern already in `update()` — fetch the TSV body and write it VERBATIM to the system's cache file:

```cpp
void TitleDatabase::update_nps_tsv(
        Mode mode, Http* http, const std::string& update_url)
{
    if (update_url.empty())
        throw std::runtime_error("no NoPayStation TSV configured for this system");

    LOGF("Fetching NoPayStation TSV: {}", update_url);
    http->start(update_url, 0);

    const auto http_length = http->get_length();
    db_total = (http_length > 0) ? static_cast<uint32_t>(http_length) : 0;

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

    std::vector<uint8_t> chunk(64 * 1024);
    for (;;)
    {
        const int read = http->read(chunk.data(), chunk.size());
        if (read <= 0)
            break;
        db_size += static_cast<uint32_t>(read);
        pkgi_write(item_file, chunk.data(), static_cast<uint32_t>(read));
    }

    pkgi_close(item_file);
    item_file = nullptr;
    pkgi_rename(tmppath, filepath);
    LOGF("NoPayStation TSV cached → {}", filepath);
}
```

- [ ] **Step 2: Branch `reload()`**

In `TitleDatabase::reload(...)`, after loading the cache bytes but before the ROM line loop, branch on source. Extract the existing ROM loop into the `else`; add the NpsVita branch that skips the header line and parses each subsequent line with `pkgi_parse_nps_game_row`, applying the same region + search filters the ROM path uses:

```cpp
    if (pkgi_system(mode).source == SourceKind::NpsVita)
    {
        reload_nps_tsv(mode, region_filter, search, installed_games);
        // (sort + annotation handled by the shared tail below — see Step 3)
    }
    else
    {
        // ... existing ROM cache parsing loop, unchanged ...
    }
```

Implement `reload_nps_tsv` (declare in `db.hpp` `private:`) — it reads the same `db_data` buffer already loaded in `reload()`. To avoid duplicating the buffer-loading, keep the `pkgi_load(dbpath)` / empty-checks in `reload()` before the branch, and pass the loaded string to the helper. Concretely, change the helper to take the buffer:

```cpp
void TitleDatabase::reload_nps_tsv(
        Mode mode,
        uint32_t region_filter,
        const std::string& search,
        const std::set<std::string>& installed_games,
        const std::string& data)
{
    const bool filter_by_region =
            (region_filter & DbFilterAllRegions) != DbFilterAllRegions;

    std::vector<std::string> lines;
    {
        std::string cur;
        for (char c : data)
        {
            if (c == '\n')
            {
                if (!cur.empty() && cur.back() == '\r')
                    cur.pop_back();
                lines.push_back(cur);
                cur.clear();
            }
            else
                cur += c;
        }
        if (!cur.empty())
            lines.push_back(cur);
    }

    for (size_t i = 1; i < lines.size(); ++i)   // i = 1 skips the header
    {
        NpsRow row;
        if (!pkgi_parse_nps_game_row(lines[i], row))
            continue;

        ++_title_count;

        if (filter_by_region)
        {
            const GameRegion gr = pkgi_get_region(row.titleid);
            if (!region_matches(region_filter, gr))
                continue;
        }

        if (!search.empty() &&
            !pkgi_stricontains(row.name.c_str(), search.c_str()) &&
            !pkgi_stricontains(row.titleid.c_str(), search.c_str()))
            continue;

        if ((region_filter & DbFilterInstalled) &&
            installed_games.find(row.titleid) == installed_games.end())
            continue;

        if (db.size() >= MAX_DB_ITEMS)
            break;

        db.push_back(DbItem{
                PresenceUnknown,
                row.titleid,
                row.content_id,
                0,
                row.name,
                row.name_org,
                row.zrif,
                row.pkg_url,
                false,
                {},
                row.size,
                "",
                "",
                "",
                false,
                "",              // system (unused for Vita)
                "",
                UserFlag::None,
                "",
        });
    }
}
```

Add a small local helper `region_matches(uint32_t filter, GameRegion r)` in `db.cpp` (anonymous namespace) mapping `RegionUSA→DbFilterRegionUSA`, `RegionEUR→DbFilterRegionEUR`, `RegionJPN→DbFilterRegionJPN`, `RegionASA→DbFilterRegionASA`, anything else → treated as allowed:

```cpp
static bool region_matches(uint32_t filter, GameRegion r)
{
    switch (r)
    {
    case RegionUSA: return filter & DbFilterRegionUSA;
    case RegionEUR: return filter & DbFilterRegionEUR;
    case RegionJPN: return filter & DbFilterRegionJPN;
    case RegionASA: return filter & DbFilterRegionASA;
    default:        return true;
    }
}
```

> The `DbItem` initializer above MUST match the current field order/count in `db.hpp`. Before writing it, read the existing `db.push_back(DbItem{...})` in the ROM `reload()` path and mirror its exact field order — copy that braced-init shape and only change the values. If the struct has more/fewer fields than shown, match the real struct.

- [ ] **Step 3: Keep the shared sort/annotation tail running for both paths**

Ensure the `std::sort(...)` and any annotation/count finalization at the end of `reload()` run for BOTH branches (they operate on `db`). If they currently sit inside the ROM loop's scope, move them below the `if/else` so the NpsVita branch is sorted too. Read the tail of `reload()` and confirm.

- [ ] **Step 4: Declare the two helpers in `db.hpp`**

Under `private:` in `class TitleDatabase`:

```cpp
    void update_nps_tsv(Mode mode, Http* http, const std::string& update_url);
    void reload_nps_tsv(
            Mode mode,
            uint32_t region_filter,
            const std::string& search,
            const std::set<std::string>& installed_games,
            const std::string& data);
```

- [ ] **Step 5: Verify (no local compile)**

Run the unit suite to confirm nothing pure broke:

Run: `./tests/run.sh`
Expected: PASS (unchanged — this task doesn't touch pure units).

Then grep to confirm the wiring is self-consistent:

```bash
grep -n "update_nps_tsv\|reload_nps_tsv\|SourceKind::NpsVita\|region_matches\|npstsv.hpp" src/db.cpp src/db.hpp
```

Expected: `update_nps_tsv` and `reload_nps_tsv` each declared once in db.hpp and defined once in db.cpp and each called once; `#include "npstsv.hpp"` present. Report that device compile is deferred to CI.

- [ ] **Step 6: Commit**

```bash
git add src/db.cpp src/db.hpp
git commit -m "feat: branch db update/reload on source; parse NoPayStation TSV for PS Vita"
```

---

## Task 4: Download wiring for `Type::Game`

**Files:**
- Modify: `src/pkgi.cpp`

**Interfaces:**
- Consumes: `pkgi_system(mode).source` (Task 2); `pkgi_zrif_decode` (`zrif.hpp`); `PKGI_RIF_SIZE`/`PKGI_PSM_RIF_SIZE` (`download.hpp`); `Downloader::add`, `DownloadItem`, `Type::Game`.
- Produces: `mode_to_type` returns `Game` for NpsVita systems, `RomGame` otherwise; `pkgi_start_download` enqueues a Vita PKG download for NpsVita.

> Not locally compilable. Verify by reading + grep.

- [ ] **Step 1: Add includes**

Ensure `src/pkgi.cpp` includes `"zrif.hpp"` and `"download.hpp"` (for `pkgi_zrif_decode` and the RIF-size macros). Check the current includes first; add only what's missing.

- [ ] **Step 2: Branch `mode_to_type`**

Replace the body of `mode_to_type` (currently returns `RomGame` unconditionally, ~pkgi.cpp:123-126):

```cpp
Type mode_to_type(Mode mode)
{
    return pkgi_system(mode).source == SourceKind::NpsVita ? Game : RomGame;
}
```

- [ ] **Step 3: Branch `pkgi_start_download`**

At the start of the `try` block in `pkgi_start_download`, before the current RomGame `downloader.add`, handle the Vita path and return; leave the existing RomGame `downloader.add` as the `else`:

```cpp
        if (item.system.empty() && !item.zrif.empty() &&
            mode_to_type(mode) == Game)
        {
            uint8_t rif[PKGI_PSM_RIF_SIZE];
            char message[256];
            if (pkgi_zrif_decode(item.zrif.c_str(), rif, message, sizeof(message)))
                throw std::runtime_error(
                        fmt::format("invalid zRIF for {}: {}", item.name, message));

            downloader.add(DownloadItem{
                    Game,
                    item.name,
                    item.content,                 // content_id
                    item.url,                     // .pkg url
                    std::vector<uint8_t>(rif, rif + PKGI_RIF_SIZE),
                    item.has_digest
                            ? std::vector<uint8_t>(item.digest.begin(),
                                                   item.digest.end())
                            : std::vector<uint8_t>{},
                    false,                        // save_as_iso
                    pkgi_get_mode_partition(),    // "ux0:"
                    "",                           // version
                    "",                           // system (unused)
            });
            return;
        }
```

> `mode` is the file-scope current mode used elsewhere in `pkgi.cpp` (e.g. the RomGame `is_in_queue(RomGame, …)` sites). Confirm `mode` is in scope in `pkgi_start_download`; it is a file-scope `Mode mode = ModeGB;` (db.hpp/pkgi.cpp). If `pkgi_zrif_decode` returns non-zero on SUCCESS in this codebase, invert the check — read `zrif.cpp`'s return convention first and match it (the original caller treated `pkgi_zrif_decode(...) == 0` as the OK-to-proceed / no-error branch; mirror that exactly).

- [ ] **Step 4: Verify (no local compile)**

Run: `./tests/run.sh`
Expected: PASS (unchanged).

Grep for consistency:

```bash
grep -n "mode_to_type\|SourceKind::NpsVita\|pkgi_zrif_decode\|PKGI_RIF_SIZE\|Type::Game\|\bGame\b" src/pkgi.cpp | head
```

Expected: `mode_to_type` branches on source; the Vita enqueue uses `Game`, decodes zRIF, uses `PKGI_RIF_SIZE`. Confirm the zRIF return-convention matches `zrif.cpp`. Report device compile deferred to CI.

- [ ] **Step 5: Commit**

```bash
git add src/pkgi.cpp
git commit -m "feat: enqueue native PS Vita .pkg (Type::Game) with decoded zRIF"
```

---

## Task 5: Device build + on-device smoke check

**Files:** none (build + deploy only).

- [ ] **Step 1: Push and run CI (test lane + device VPK build)**

```bash
git push origin feat/archive-org-metadata
gh workflow run test.yml  --ref feat/archive-org-metadata
gh workflow run build.yml --ref feat/archive-org-metadata
BUILD_ID="$(gh run list --workflow build.yml -L1 --json databaseId -q '.[0].databaseId')"
gh run watch "$BUILD_ID" --exit-status --interval 20
```

Expected: both succeed. The device VPK build is the real compile check for `db.cpp`/`pkgi.cpp`/`downloader.cpp` (not compilable locally). If it fails, fix the reported file and re-push.

- [ ] **Step 2: Deploy the VPK (requires the user's Vita FTP awake)**

The Vita currently has no PKGj install, so upload the full VPK for a VitaShell install (FTP alone can't install a VPK). Use the Vita's current IP (ask the user if it changed):

```bash
BUILD_ID="$(gh run list --workflow build.yml -L1 --json databaseId -q '.[0].databaseId')"
rm -rf /tmp/pkgj_v3 && mkdir -p /tmp/pkgj_v3
gh run download "$BUILD_ID" -n pkgj-retroarch -D /tmp/pkgj_v3
( cd /tmp/pkgj_v3 && unzip -o pkgj.vpk )
curl -sS --connect-timeout 10 -T /tmp/pkgj_v3/pkgj.vpk "ftp://<VITA_IP>:1337/ux0:/pkgj.vpk"
```

Expected: upload completes. Exit 7 → ask the user to re-enable VitaShell FTP (SELECT), then retry.

- [ ] **Step 3: Manual smoke check (ask the user)**

Ask the user to install `ux0:/pkgj.vpk` via VitaShell, launch PKGj+, and confirm:
1. A "PS Vita Games" entry appears in the system list.
2. Opening it lists games (entries without a zRIF are absent).
3. Selecting a game with a valid zRIF downloads and installs it (appears on the LiveArea).

Report the user's result. If the TSV fetch fails (the embedded `http://nopaystation.com/...` default may be unreachable/stale), have them set `url_psvita <working_tsv_url>` in `ux0:/pkgj/config.txt` and retry.

- [ ] **Step 4: No commit** (verification only).

---

## Self-Review

**Spec coverage (3a scope):**
- `SourceKind` on the table → Task 2. ✓
- `npstsv` STL-only parser + tests → Task 1. ✓
- Data layer branches update()/reload() on source; raw-TSV cache; DbItem carries content_id/url/zRIF/region → Task 3. ✓
- Download enqueues `Type::Game` with decoded zRIF; reuse `do_download_package`/`pkgi_install` → Task 4. ✓
- Config `url_psvita` + embedded default → free via Phase 0+1 data-driven config (default_item on the row, Task 2); no config.cpp change (stated in Global Constraints). ✓
- Exclude zRIF-missing / MISSING / CART ONLY entries → Task 1 parser + Task 3 reload. ✓
- Region + search filtering → Task 3. ✓
- Tests (header skip, valid row, MISSING pkg, MISSING zrif, CART ONLY, short content id) → Task 1. ✓
- Device compile verification → Task 5. ✓
- 3b (DLC) / 3c (updates) → out of this plan by design (spec decomposition). ✓

**Deviation from spec (noted intentionally):** the spec proposed an invented pipe-delimited "cache PKG" format via a `pkgcache` unit. This plan instead caches the raw TSV and parses it at reload with `npstsv` — the original pkgj approach, DRY and proven, and it drops a whole invented unit. Same observable behavior; zRIF-missing rows excluded at reload (never listed), honoring the user's decision.

**Placeholder scan:** No TBD/"handle errors"/"write tests for the above". Every code and test step is complete. The two device-only tasks (3,4) explicitly cannot compile locally and say so; their correctness rests on Task 1's unit tests plus the CI build in Task 5 — not a placeholder, a stated verification boundary.

**Type consistency:** `NpsRow{content_id,titleid,region,name,name_org,pkg_url,zrif,size}`, `pkgi_split_tsv_row`, `pkgi_parse_nps_game_row`, `SourceKind{ArchiveOrgRom,NpsVita}`, `SystemDef::source`, `update_nps_tsv`, `reload_nps_tsv(...,const std::string& data)`, `region_matches`, `mode_to_type`→`Game`/`RomGame`, `DownloadItem` field order — used identically across tasks. The `DbItem{...}` init in Task 3 is explicitly gated on matching the real struct field order (read-before-write instruction).
