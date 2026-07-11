# Data-Driven System Table + Test Harness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the frozen `enum Mode` with a data-driven system table, extract the pure ROM logic into dependency-free units covered by a doctest harness, then add the RetroArch consoles the PS Vita cores actually support.

**Architecture:** A single `std::vector<SystemDef>` table (`systems.cpp`) is the source of truth; `Mode` becomes an index into it. The pure logic that used to live inside `db.cpp` next to fmt/sqlite/http — extension matching, exclusion rules, the hand-rolled JSON scanner, the cache-line format, and download-URL construction — moves into three STL-only translation units (`systems`, `jsonscan`, `romcache`) so a tiny `pkgj_tests` binary compiles them with plain g++ (no conan). `db.cpp`, `config.cpp`, `pkgi.cpp`, and `browserview.cpp` then read the table instead of switching on the enum.

**Tech Stack:** C++17, CMake, doctest (single-header, vendored), GitHub Actions.

## Global Constraints

- `systems.{hpp,cpp}`, `jsonscan.{hpp,cpp}`, `romcache.{hpp,cpp}` MUST include **only** the C++ standard library — no `fmt`, no `http.hpp`, no `sqlite`, no `pkgi.hpp`. This is what keeps them unit-testable without conan.
- `Mode` is an **index** into `pkgi_systems()`. The eight historical values keep their numeric positions: `ModeGB=0, ModeGBC=1, ModeGBA=2, ModeSNES=3, ModeNES=4, ModeGenesis=5, ModePS1=6, ModePSP=7`. New systems are appended **after** index 7 — never inserted in the middle (on-device state may encode `Mode` as an int).
- Download URLs are built as `https://<d1><dir>/<url_encoded_file>` (the ia* fleet). Never emit an `https://archive.org/download/...` URL as the primary path (Vita SSL cannot negotiate the ECDSA dn* nodes).
- Preserve backward compatibility of `config.txt`: the existing keys `url_gb … url_psp` must keep working (they are exactly the new table's `config_key`s).
- Language standard: C++17 (matches the existing tree). Tests build with `g++-12 -std=c++17`.
- TDD: write the failing test first, watch it fail, implement, watch it pass, commit. One logical change per commit.
- Do not build the `.vpk` locally (no VitaSDK/conan on this machine). Device build happens via `gh workflow run build.yml` at the end.

---

## File Structure

**New files:**
- `tests/doctest.h` — vendored single-header test framework (v2.4.11).
- `tests/CMakeLists.txt` — builds the `pkgj_tests` executable from pure units only.
- `tests/test_main.cpp` — doctest entrypoint (`#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`).
- `tests/test_systems.cpp` — extension matching, exclusion rules, table integrity.
- `tests/test_jsonscan.cpp` — JSON scanner + URL-encode + unescape, over a fixture.
- `tests/test_romcache.cpp` — cache-line parse/format + download-URL builder.
- `tests/fixtures/archive_metadata.json` — trimmed real Archive.org metadata sample.
- `tests/run.sh` — one-line g++ compile+run (works with no conan).
- `src/systems.hpp` / `src/systems.cpp` — the system table + lookups + matching/exclusion.
- `src/jsonscan.hpp` / `src/jsonscan.cpp` — `json_str`, `find_object_end`, `json_unescape`, `url_encode_path`.
- `src/romcache.hpp` / `src/romcache.cpp` — cache-line parse/format + `pkgi_build_download_url`.
- `.github/workflows/test.yml` — CI job that builds+runs `pkgj_tests` (no conan).

**Modified files:**
- `src/db.hpp` — `Mode` becomes a plain `enum` index; add `pkgi_mode_count()`; declare table-backed helpers.
- `src/db.cpp` — delete the four `switch(mode)` helpers and the extracted static logic; delegate to the new units.
- `src/config.hpp` — replace the eight `*_url` fields with `std::map<std::string,std::string> system_urls`.
- `src/config.cpp` — data-driven defaults / parse / save; keep legacy keys working.
- `src/pkgi.cpp` — `pkgi_get_url_from_mode` and the `allow_refresh` bitmask read the table; `ModeCount` → `pkgi_mode_count()`.
- `src/browserview.cpp` — `build_tree` iterates the table instead of the hardcoded 8 rows.
- `host.cmake` — add the extracted `.cpp`s to `pkgj_cli`/`pkgj_sim`; add `add_subdirectory(tests)` guarded by an option.
- `CMakeLists.txt` — add the extracted `.cpp`s to the Vita target source list.

---

## Task 1: Test harness (doctest) that builds with plain g++

**Files:**
- Create: `tests/doctest.h`, `tests/test_main.cpp`, `tests/test_smoke.cpp`, `tests/run.sh`, `tests/CMakeLists.txt`
- Modify: `host.cmake` (append `add_subdirectory` guarded by option)

**Interfaces:**
- Produces: a runnable `pkgj_tests` binary and a `tests/run.sh` that compiles+runs with `g++-12 -std=c++17` and **no conan**. Later tasks add `test_*.cpp` files and list them in `tests/CMakeLists.txt` + `tests/run.sh`.

- [ ] **Step 1: Vendor doctest**

Download the single header (pinned version) into `tests/doctest.h`:

```bash
curl -fsSL https://raw.githubusercontent.com/doctest/doctest/v2.4.11/doctest/doctest.h \
  -o tests/doctest.h
test -s tests/doctest.h && head -5 tests/doctest.h
```

Expected: prints the doctest license header. If offline, obtain `doctest.h` v2.4.11 by any means and place it at that path (it is a single self-contained header).

- [ ] **Step 2: Write the doctest entrypoint**

`tests/test_main.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
```

- [ ] **Step 3: Write a smoke test (the failing test)**

`tests/test_smoke.cpp`:

```cpp
#include "doctest.h"

TEST_CASE("doctest harness is wired up")
{
    CHECK(1 + 1 == 2);
}
```

- [ ] **Step 4: Write the no-conan run script**

`tests/run.sh`:

```bash
#!/usr/bin/env bash
# Compile+run the pure-logic unit tests with plain g++ (no conan, no VitaSDK).
# Add each new tests/test_*.cpp and the src/*.cpp unit(s) it exercises here.
set -euo pipefail
cd "$(dirname "$0")/.."
CXX="${CXX:-g++-12}"
"$CXX" -std=c++17 -I src -I tests -o /tmp/pkgj_tests \
    tests/test_main.cpp \
    tests/test_smoke.cpp
/tmp/pkgj_tests "$@"
```

Make it executable:

```bash
chmod +x tests/run.sh
```

- [ ] **Step 5: Run it to verify it passes**

Run: `./tests/run.sh`
Expected: doctest prints `[doctest] test cases: 1 | 1 passed` and exits 0.

- [ ] **Step 6: Add a CMake target (for CI + host build parity)**

`tests/CMakeLists.txt`:

```cmake
# Pure-logic unit tests. Depends only on the C++ standard library, so this
# target builds without conan. Sources are added as units are extracted.
add_executable(pkgj_tests
  test_main.cpp
  test_smoke.cpp
)
target_include_directories(pkgj_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/src
)
target_compile_features(pkgj_tests PRIVATE cxx_std_17)
enable_testing()
add_test(NAME pkgj_tests COMMAND pkgj_tests)
```

Append to the end of `host.cmake`:

```cmake
option(BUILD_TESTS "Build pkgj_tests unit tests" OFF)
if(BUILD_TESTS)
  add_subdirectory(${CMAKE_SOURCE_DIR}/tests ${CMAKE_BINARY_DIR}/tests)
endif()
```

- [ ] **Step 7: Commit**

```bash
git add tests/ host.cmake
git commit -m "test: add doctest harness that builds with plain g++ (no conan)"
```

---

## Task 2: System table (`systems.{hpp,cpp}`) + route db.cpp

**Files:**
- Create: `src/systems.hpp`, `src/systems.cpp`, `tests/test_systems.cpp`
- Modify: `src/db.hpp` (Mode enum + declarations), `src/db.cpp` (delegate), `host.cmake`, `CMakeLists.txt`, `tests/run.sh`, `tests/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `enum InstallKind { RetroArchRom, PspAdrenaline, VitaNative };`
  - `struct SystemDef { std::string id, display_name, roms_dir, cache_file; std::vector<std::string> extensions; InstallKind install; std::string default_item, config_key; };`
  - `const std::vector<SystemDef>& pkgi_systems();`
  - `int pkgi_mode_count();`
  - `const SystemDef& pkgi_system(int mode);`
  - `const SystemDef* pkgi_system_by_id(const std::string& id);`
  - `bool pkgi_matches_extension(const SystemDef&, const std::string& base_lower);`
  - `bool pkgi_is_excluded_file(const std::string& base_lower);`
- Consumes (from Task 1): the doctest harness.

- [ ] **Step 1: Write the failing test**

`tests/test_systems.cpp`:

```cpp
#include "doctest.h"
#include "systems.hpp"

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
```

Add `#include <set>` at the top of the test file (used by the last case).

- [ ] **Step 2: Add the two test files to the runners**

In `tests/run.sh`, extend the compile line to:

```bash
"$CXX" -std=c++17 -I src -I tests -o /tmp/pkgj_tests \
    tests/test_main.cpp \
    tests/test_smoke.cpp \
    tests/test_systems.cpp \
    src/systems.cpp
```

In `tests/CMakeLists.txt`, add `test_systems.cpp` and `${CMAKE_SOURCE_DIR}/src/systems.cpp` to the `add_executable(pkgj_tests ...)` list.

- [ ] **Step 3: Run to verify it fails**

Run: `./tests/run.sh`
Expected: FAIL — `fatal error: systems.hpp: No such file or directory`.

- [ ] **Step 4: Write `src/systems.hpp`**

```cpp
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
```

- [ ] **Step 5: Write `src/systems.cpp`**

```cpp
#include "systems.hpp"

namespace
{
bool ends_with(const std::string& s, const std::string& suffix)
{
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}
}

const std::vector<SystemDef>& pkgi_systems()
{
    // NOTE: indices 0..7 are historical Mode values and must not move.
    static const std::vector<SystemDef> table = {
        {"gb",      "Game Boy",         "gb",        "roms_gb.dat",
            {".gb"},                         InstallRetroArchRom, "theentiregameboycollection", "url_gb"},
        {"gbc",     "Game Boy Color",   "gbc",       "roms_gbc.dat",
            {".gbc", ".gb"},                 InstallRetroArchRom, "", "url_gbc"},
        {"gba",     "Game Boy Advance", "gba",       "roms_gba.dat",
            {".gba"},                        InstallRetroArchRom, "", "url_gba"},
        {"snes",    "Super Nintendo",   "snes",      "roms_snes.dat",
            {".sfc", ".smc"},                InstallRetroArchRom, "FinalFantasyIII", "url_snes"},
        {"nes",     "Nintendo NES",     "nes",       "roms_nes.dat",
            {".nes"},                        InstallRetroArchRom, "FullNes", "url_nes"},
        {"genesis", "Sega Mega Drive",  "megadrive", "roms_genesis.dat",
            {".md", ".gen", ".smd", ".bin"}, InstallRetroArchRom, "", "url_genesis"},
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
```

- [ ] **Step 6: Run to verify it passes**

Run: `./tests/run.sh`
Expected: PASS — all `test_systems.cpp` cases green (`test cases: 7 | 7 passed` including smoke).

- [ ] **Step 7: Route `db.cpp` through the table**

In `src/db.hpp`: replace the `enum Mode { ... }` + `static constexpr auto ModeCount = 8;` block with:

```cpp
// Mode is an index into pkgi_systems() (see systems.hpp).
enum Mode
{
    ModeGB = 0, ModeGBC, ModeGBA, ModeSNES,
    ModeNES, ModeGenesis, ModePS1, ModePSP,
};
```

Delete the line `static constexpr auto ModeCount = 8;`. Add near the top of `db.hpp` (after the includes): `#include "systems.hpp"`. Keep the existing `pkgi_mode_to_string` / `pkgi_mode_to_system_dir` declarations.

In `src/db.cpp`:
- Add `#include "systems.hpp"`.
- Replace the bodies of `pkgi_mode_to_string`, `pkgi_mode_to_system_dir`, and `pkgi_mode_to_file_name` with table lookups:

```cpp
std::string pkgi_mode_to_string(Mode mode)
{
    return pkgi_system(mode).display_name;
}

std::string pkgi_mode_to_system_dir(Mode mode)
{
    return pkgi_system(mode).roms_dir;
}

static const char* pkgi_mode_to_file_name(Mode mode)
{
    return pkgi_system(mode).cache_file.c_str();
}
```

- Delete the static `is_rom_file(Mode, ...)` and static `is_excluded_file(...)` and static `ends_with(...)` definitions in `db.cpp`, and replace their call sites inside `update()`:

```cpp
if (!name.empty() && pkgi_matches_extension(pkgi_system(mode), base_lower) &&
    !pkgi_is_excluded_file(base_lower))
```

(`to_lower`, `basename_of`, `json_unescape`, `url_encode_path`, `json_str`, `find_object_end`, `split_pipe` stay in `db.cpp` for now — later tasks move some of them.)

- [ ] **Step 8: Wire `systems.cpp` into the device + host builds**

In `CMakeLists.txt`, add `src/systems.cpp` to the Vita target's source list (find the `add_executable`/source list for the `pkgj` target and add the line next to `src/db.cpp`).

In `host.cmake`, add `src/systems.cpp` to **both** `pkgj_cli` and `pkgj_sim` source lists (next to `src/db.cpp`).

- [ ] **Step 9: Commit**

```bash
git add src/systems.hpp src/systems.cpp src/db.hpp src/db.cpp \
        CMakeLists.txt host.cmake tests/
git commit -m "feat: data-driven system table; route db.cpp mode helpers through it"
```

---

## Task 3: Extract the JSON scanner (`jsonscan.{hpp,cpp}`)

**Files:**
- Create: `src/jsonscan.hpp`, `src/jsonscan.cpp`, `tests/test_jsonscan.cpp`, `tests/fixtures/archive_metadata.json`
- Modify: `src/db.cpp` (include + delete moved statics), `CMakeLists.txt`, `host.cmake`, `tests/run.sh`, `tests/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `std::string pkgi_json_str(const char* data, size_t len, const char* key);`
  - `const char* pkgi_find_object_end(const char* ptr, const char* limit);`
  - `std::string pkgi_json_unescape(const std::string& s);`
  - `std::string pkgi_url_encode_path(const std::string& path);`
- These keep the exact behavior of the current `db.cpp` statics (same pointer-based signatures) so `update()` keeps working unchanged apart from the `pkgi_` prefix.

- [ ] **Step 1: Create the fixture**

`tests/fixtures/archive_metadata.json` — a trimmed but real-shaped Archive.org metadata document. It MUST contain a top-level `d1`, a `dir` appearing after `d1`, and a `files` array with one valid ROM, one `_meta.xml`, and one `collection.zip`:

```json
{
  "created": 1700000000,
  "d1": "ia600505.us.archive.org",
  "d2": "ia800505.us.archive.org",
  "dir": "/12/items/theentiregameboycollection",
  "files": [
    {"name": "Tetris (World).gb", "source": "original", "size": "131072"},
    {"name": "Super Mario Land (World).gb", "source": "original", "size": "65536"},
    {"name": "theentiregameboycollection_meta.xml", "source": "metadata", "size": "512"},
    {"name": "Game Boy Collection.zip", "source": "original", "size": "999999"}
  ],
  "server": "ia600505.us.archive.org",
  "uniq": 123456
}
```

- [ ] **Step 2: Write the failing test**

`tests/test_jsonscan.cpp`:

```cpp
#include "doctest.h"
#include "jsonscan.hpp"

#include <fstream>
#include <sstream>
#include <string>

static std::string load_fixture()
{
    std::ifstream f("tests/fixtures/archive_metadata.json");
    REQUIRE(f.good());
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

TEST_CASE("json_str extracts a top-level string field")
{
    const auto j = load_fixture();
    CHECK(pkgi_json_str(j.data(), j.size(), "d1") == "ia600505.us.archive.org");
    CHECK(pkgi_json_str(j.data(), j.size(), "dir") ==
          "/12/items/theentiregameboycollection");
    CHECK(pkgi_json_str(j.data(), j.size(), "nonexistent") == "");
}

TEST_CASE("find_object_end handles nested braces and strings")
{
    std::string s = R"({"a":{"b":"}"} ,"c":1})";
    const char* end = pkgi_find_object_end(s.data(), s.data() + s.size());
    REQUIRE(end != nullptr);
    CHECK(*end == '}');
    CHECK(end == s.data() + s.size() - 1);  // the LAST brace
}

TEST_CASE("find_object_end returns nullptr when unbalanced")
{
    std::string s = R"({"a":1)";
    CHECK(pkgi_find_object_end(s.data(), s.data() + s.size()) == nullptr);
}

TEST_CASE("json_unescape handles the escapes archive.org emits")
{
    CHECK(pkgi_json_unescape("a\\/b") == "a/b");
    CHECK(pkgi_json_unescape("a\\\\b") == "a\\b");
    CHECK(pkgi_json_unescape("a\\\"b") == "a\"b");
    CHECK(pkgi_json_unescape("plain") == "plain");
}

TEST_CASE("url_encode_path preserves slashes and unreserved chars")
{
    CHECK(pkgi_url_encode_path("Tetris (World).gb") == "Tetris%20%28World%29.gb");
    CHECK(pkgi_url_encode_path("a/b/c.gb") == "a/b/c.gb");
    CHECK(pkgi_url_encode_path("game-1_v2.0~.gb") == "game-1_v2.0~.gb");
}
```

- [ ] **Step 3: Add to runners and run to verify it fails**

Extend `tests/run.sh` compile line with `tests/test_jsonscan.cpp` and `src/jsonscan.cpp`; add the same to `tests/CMakeLists.txt`.

Run: `./tests/run.sh`
Expected: FAIL — `fatal error: jsonscan.hpp: No such file or directory`.

- [ ] **Step 4: Write `src/jsonscan.hpp`**

```cpp
#pragma once

#include <cstddef>
#include <string>

// Value of a string field "key":"<value>" within [data, data+len). Empty if absent.
std::string pkgi_json_str(const char* data, size_t len, const char* key);

// Closing '}' matching an opening '{' at ptr[0]; nullptr on error/unbalanced.
const char* pkgi_find_object_end(const char* ptr, const char* limit);

// Minimal unescape for the escapes archive.org emits: \/  \\  \"
std::string pkgi_json_unescape(const std::string& s);

// Percent-encode a path; '/' kept; unreserved chars pass through.
std::string pkgi_url_encode_path(const std::string& path);
```

- [ ] **Step 5: Write `src/jsonscan.cpp` (move bodies verbatim from db.cpp)**

```cpp
#include "jsonscan.hpp"

#include <cstring>

std::string pkgi_json_str(const char* data, size_t len, const char* key)
{
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
            end++;
        end++;
    }
    return std::string(found, end);
}

const char* pkgi_find_object_end(const char* ptr, const char* limit)
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
                ptr++;
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

std::string pkgi_json_unescape(const std::string& s)
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

std::string pkgi_url_encode_path(const std::string& path)
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
```

- [ ] **Step 6: Delete the moved statics from `db.cpp` and call the new API**

In `src/db.cpp`: add `#include "jsonscan.hpp"`. Delete the `static std::string json_str(...)`, `static const char* find_object_end(...)`, `static std::string json_unescape(...)`, and `static std::string url_encode_path(...)` definitions. Update their call sites in `update()`:
- `json_str(json, json_len, "d1")` → `pkgi_json_str(json, json_len, "d1")`
- `json_str(from, ..., "dir")` → `pkgi_json_str(from, ..., "dir")`
- `find_object_end(p, json_end)` → `pkgi_find_object_end(p, json_end)`
- `json_unescape(json_str(p, obj_len, "name"))` → `pkgi_json_unescape(pkgi_json_str(p, obj_len, "name"))`
- `json_str(p, obj_len, "size")` → `pkgi_json_str(p, obj_len, "size")`
- `url_encode_path(name)` → `pkgi_url_encode_path(name)`

- [ ] **Step 7: Run to verify it passes**

Run: `./tests/run.sh`
Expected: PASS — jsonscan cases green.

- [ ] **Step 8: Wire into builds**

Add `src/jsonscan.cpp` to the Vita target in `CMakeLists.txt` and to `pkgj_cli` + `pkgj_sim` in `host.cmake`.

- [ ] **Step 9: Commit**

```bash
git add src/jsonscan.hpp src/jsonscan.cpp src/db.cpp CMakeLists.txt host.cmake tests/
git commit -m "refactor: extract JSON scanner into dependency-free jsonscan unit + tests"
```

---

## Task 4: Extract cache-line + URL builder (`romcache.{hpp,cpp}`)

**Files:**
- Create: `src/romcache.hpp`, `src/romcache.cpp`, `tests/test_romcache.cpp`
- Modify: `src/db.cpp` (use in `update()` + `reload()`), `CMakeLists.txt`, `host.cmake`, `tests/run.sh`, `tests/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `struct RomCacheLine { std::string name; long long size; std::string url; };`
  - `bool pkgi_parse_cache_line(const std::string& line, RomCacheLine& out);`
  - `std::string pkgi_format_cache_line(const std::string& name, long long size, const std::string& url);`
  - `std::string pkgi_build_download_url(const std::string& d1, const std::string& dir, const std::string& file_encoded);`
- Consumes (from Task 3): `pkgi_url_encode_path` for the caller-side encoding (builder takes an already-encoded file to stay single-responsibility).

- [ ] **Step 1: Write the failing test**

`tests/test_romcache.cpp`:

```cpp
#include "doctest.h"
#include "romcache.hpp"

TEST_CASE("format then parse round-trips a cache line")
{
    const std::string line =
        pkgi_format_cache_line("Tetris.gb", 131072, "https://host/dir/Tetris.gb");
    CHECK(line == "Tetris.gb|131072|https://host/dir/Tetris.gb");

    RomCacheLine out;
    REQUIRE(pkgi_parse_cache_line(line, out));
    CHECK(out.name == "Tetris.gb");
    CHECK(out.size == 131072);
    CHECK(out.url == "https://host/dir/Tetris.gb");
}

TEST_CASE("parse tolerates a missing size (defaults to 0) but keeps url")
{
    RomCacheLine out;
    REQUIRE(pkgi_parse_cache_line("Tetris.gb||https://host/Tetris.gb", out));
    CHECK(out.size == 0);
    CHECK(out.url == "https://host/Tetris.gb");
}

TEST_CASE("parse falls back to name as url when only two fields")
{
    RomCacheLine out;
    REQUIRE(pkgi_parse_cache_line("Tetris.gb|131072", out));
    CHECK(out.url == "Tetris.gb");
}

TEST_CASE("parse rejects a line with no fields")
{
    RomCacheLine out;
    CHECK_FALSE(pkgi_parse_cache_line("", out));
}

TEST_CASE("parse ignores a non-numeric size instead of throwing")
{
    RomCacheLine out;
    REQUIRE(pkgi_parse_cache_line("Tetris.gb|notanumber|https://h/x", out));
    CHECK(out.size == 0);
}

TEST_CASE("build_download_url uses the ia* host+dir, never /download/")
{
    const std::string url = pkgi_build_download_url(
        "ia600505.us.archive.org",
        "/12/items/coll",
        "Tetris%20%28World%29.gb");
    CHECK(url == "https://ia600505.us.archive.org/12/items/coll/Tetris%20%28World%29.gb");
    CHECK(url.find("/download/") == std::string::npos);
}
```

- [ ] **Step 2: Add to runners; run to verify it fails**

Extend `tests/run.sh` and `tests/CMakeLists.txt` with `tests/test_romcache.cpp` + `src/romcache.cpp`.

Run: `./tests/run.sh`
Expected: FAIL — `fatal error: romcache.hpp: No such file or directory`.

- [ ] **Step 3: Write `src/romcache.hpp`**

```cpp
#pragma once

#include <string>

struct RomCacheLine
{
    std::string name;
    long long size = 0;
    std::string url;
};

// Parse one pipe-delimited cache line "name|size|url".
//  - <1 field -> false. size non-numeric/empty -> 0. url defaults to name if absent.
bool pkgi_parse_cache_line(const std::string& line, RomCacheLine& out);

// Serialize a cache line (no trailing newline).
std::string pkgi_format_cache_line(const std::string& name, long long size,
                                   const std::string& url);

// Assemble the ia* download URL. `file_encoded` must already be url-encoded.
std::string pkgi_build_download_url(const std::string& d1, const std::string& dir,
                                    const std::string& file_encoded);
```

- [ ] **Step 4: Write `src/romcache.cpp`**

```cpp
#include "romcache.hpp"

#include <vector>

namespace
{
std::vector<std::string> split_pipe(const std::string& s)
{
    std::vector<std::string> parts;
    std::string cur;
    for (char c : s)
    {
        if (c == '|')
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
}

bool pkgi_parse_cache_line(const std::string& line, RomCacheLine& out)
{
    const auto fields = split_pipe(line);
    if (fields.empty() || fields[0].empty())
        return false;

    out.name = fields[0];
    out.size = 0;
    if (fields.size() > 1 && !fields[1].empty())
    {
        try { out.size = std::stoll(fields[1]); }
        catch (...) { out.size = 0; }
    }
    out.url = (fields.size() > 2 && !fields[2].empty()) ? fields[2] : fields[0];
    return true;
}

std::string pkgi_format_cache_line(const std::string& name, long long size,
                                   const std::string& url)
{
    return name + "|" + std::to_string(size) + "|" + url;
}

std::string pkgi_build_download_url(const std::string& d1, const std::string& dir,
                                    const std::string& file_encoded)
{
    // dir already begins with '/'.
    return "https://" + d1 + dir + "/" + file_encoded;
}
```

- [ ] **Step 5: Use the new API in `db.cpp`**

Add `#include "romcache.hpp"` to `db.cpp`.

In `update()`, replace the base-URL + line-writing block. Where it currently builds `base_url` from `d1`/`dir` and writes `name + "|" + size + "|" + url + "\n"`, keep the `d1`/`dir` extraction (via `pkgi_json_str`) but build each line with the helpers:

```cpp
const std::string enc = pkgi_url_encode_path(name);
std::string url;
if (!d1.empty() && !dir.empty())
    url = pkgi_build_download_url(d1, dir, enc);
else
    url = "https://archive.org/download/" + item_id + "/" + enc;  // SSL fallback

const long long sz = size.empty() ? 0 : [&]{
    try { return std::stoll(size); } catch (...) { return 0LL; }
}();
const std::string line = pkgi_format_cache_line(name, sz, url) + "\n";
pkgi_write(item_file, line.data(), static_cast<uint32_t>(line.size()));
```

In `reload()`, replace the inline field-splitting (the `split_pipe(line)` block and the `fields[...]` access) with:

```cpp
RomCacheLine parsed;
if (!pkgi_parse_cache_line(line, parsed))
    continue;
const std::string& file_name = parsed.name;
const int64_t size_val = parsed.size;
const std::string& url = parsed.url;
```

Then delete the now-unused `static ... split_pipe(...)` definition from `db.cpp` (it lives in `romcache.cpp` now). Leave the rest of `reload()` (title derivation, search filter, `db.push_back`) unchanged.

- [ ] **Step 6: Run to verify it passes**

Run: `./tests/run.sh`
Expected: PASS — romcache cases green.

- [ ] **Step 7: Wire into builds**

Add `src/romcache.cpp` to the Vita target in `CMakeLists.txt` and to `pkgj_cli` + `pkgj_sim` in `host.cmake`.

- [ ] **Step 8: Commit**

```bash
git add src/romcache.hpp src/romcache.cpp src/db.cpp CMakeLists.txt host.cmake tests/
git commit -m "refactor: extract cache-line format + ia* URL builder into romcache unit + tests"
```

---

## Task 5: Data-driven config (`system_urls` map)

**Files:**
- Modify: `src/config.hpp`, `src/config.cpp`
- Create: `tests/test_config_url.cpp`
- Modify: `tests/run.sh`, `tests/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `Config::system_urls` is `std::map<std::string,std::string>` keyed by `SystemDef::id`.
  - `std::string pkgi_config_url(const Config&, int mode);` (declared in `config.hpp`) — returns `system_urls[id]` if set, else the table's `default_item`.
- Consumes: `pkgi_system`, `pkgi_systems`, `pkgi_system_by_id` (Task 2).

> `config.cpp` includes `fmt`, so its file I/O is not unit-tested directly. We test the **pure** resolution helper `pkgi_config_url`, which only needs `systems.cpp` + a `Config` value.

- [ ] **Step 1: Write the failing test**

`tests/test_config_url.cpp`:

```cpp
#include "doctest.h"
#include "config.hpp"
#include "systems.hpp"

TEST_CASE("config url falls back to the table default when unset")
{
    Config c{};
    // gb has a default item; gbc does not.
    CHECK(pkgi_config_url(c, 0) == pkgi_system_by_id("gb")->default_item);
    CHECK(pkgi_config_url(c, 1) == "");
}

TEST_CASE("an explicit system_urls entry overrides the default")
{
    Config c{};
    c.system_urls["gb"] = "my-custom-item";
    CHECK(pkgi_config_url(c, 0) == "my-custom-item");
}
```

- [ ] **Step 2: Add to runners; run to verify it fails**

Add `tests/test_config_url.cpp` + `src/config.cpp` to `tests/run.sh` and `tests/CMakeLists.txt`.

> `config.cpp` pulls in `fmt`. For the **no-conan** `run.sh`, compile only `pkgi_config_url` by isolating it: put `pkgi_config_url` in a **new** tiny file `src/config_url.cpp` (STL + systems only) instead of `config.cpp`. Update the interface accordingly: create `src/config_url.cpp`, and have `run.sh`/`CMakeLists.txt` reference `src/config_url.cpp` (not `config.cpp`). `config.hpp` still declares `pkgi_config_url`.

Run: `./tests/run.sh`
Expected: FAIL — `pkgi_config_url` undefined / `system_urls` not a member.

- [ ] **Step 3: Update `config.hpp`**

Replace the eight `std::string gb_url … psp_url;` fields with:

```cpp
#include <map>
// ...
    // Archive.org item identifier per system, keyed by SystemDef::id.
    // Empty/absent -> fall back to the table's default_item.
    std::map<std::string, std::string> system_urls;
```

Add the declaration (after `Config pkgi_load_config();`):

```cpp
// Resolve the configured Archive.org item for a mode (explicit override or default).
std::string pkgi_config_url(const Config& config, int mode);
```

- [ ] **Step 4: Implement `pkgi_config_url` in `src/config_url.cpp`**

```cpp
#include "config.hpp"
#include "systems.hpp"

std::string pkgi_config_url(const Config& config, int mode)
{
    const SystemDef& sys = pkgi_system(mode);
    auto it = config.system_urls.find(sys.id);
    if (it != config.system_urls.end() && !it->second.empty())
        return it->second;
    return sys.default_item;
}
```

- [ ] **Step 5: Rewrite defaults/parse/save in `config.cpp` to loop the table**

Replace the eight `config.field = default_*` assignments in `pkgi_load_config` with a loop that seeds from the table (defaults live in the table now, so seeding is optional — leave `system_urls` empty and rely on `pkgi_config_url` fallback). Remove the eight `default_*_url` constants.

Replace the eight `if (pkgi_stricmp(key, "url_gb") == 0) config.gb_url = value; else if ...` blocks with a table-driven lookup:

```cpp
else
{
    bool handled = false;
    for (const auto& sys : pkgi_systems())
    {
        if (pkgi_stricmp(key, sys.config_key.c_str()) == 0)
        {
            config.system_urls[sys.id] = value;
            handled = true;
            break;
        }
    }
    // (fall through to the remaining non-url keys if !handled)
}
```

Integrate this so the remaining `else if` keys (`url_comppack`, `thumbnail_url`, custom entries, sort/filter) still work — put the table loop as one branch among the existing key checks, not swallowing unknown keys before them.

Replace the `SAVE_URL(...)` block in `pkgi_save_config` with a loop:

```cpp
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
```

- [ ] **Step 6: Run to verify it passes**

Run: `./tests/run.sh`
Expected: PASS — config-url cases green.

- [ ] **Step 7: Wire `config_url.cpp` into builds**

Add `src/config_url.cpp` to the Vita target in `CMakeLists.txt` and to `pkgj_cli` + `pkgj_sim` in `host.cmake` (next to `config.cpp`; note `pkgj_cli` currently has no `config.cpp` — add `config_url.cpp` there since `systems.cpp` is already present and `config_url.cpp` only needs systems).

- [ ] **Step 8: Commit**

```bash
git add src/config.hpp src/config.cpp src/config_url.cpp CMakeLists.txt host.cmake tests/
git commit -m "feat: data-driven config (system_urls map) with legacy url_* keys preserved"
```

---

## Task 6: Route the UI consumers through the table

**Files:**
- Modify: `src/pkgi.cpp` (`pkgi_get_url_from_mode`, `allow_refresh`, `ModeCount` loop), `src/browserview.cpp` (`build_tree`)

**Interfaces:**
- Consumes: `pkgi_config_url` (Task 5), `pkgi_mode_count`, `pkgi_systems`, `pkgi_system` (Task 2).
- No new produced interface (internal wiring). No unit test — verified by host build compiling and by the device smoke test at the end.

- [ ] **Step 1: Replace `pkgi_get_url_from_mode` in `pkgi.cpp`**

```cpp
std::string const& pkgi_get_url_from_mode(Mode mode)
{
    // Resolve against config overrides + table defaults. Static storage so we
    // can keep returning a reference (callers expect `std::string const&`).
    static thread_local std::string resolved;
    resolved = pkgi_config_url(config, mode);
    return resolved;
}
```

Add `#include "config.hpp"` if not already included in `pkgi.cpp` (it is, via `pkgi.hpp`/config — verify; add if the build complains).

- [ ] **Step 2: Replace the `allow_refresh` bitmask (pkgi.cpp ~line 878)**

```cpp
int allow_refresh = 0;
for (int i = 0; i < pkgi_mode_count() && i < 32; ++i)
    if (!pkgi_get_url_from_mode(static_cast<Mode>(i)).empty())
        allow_refresh |= (1 << i);
```

- [ ] **Step 3: Replace `ModeCount` usages (pkgi.cpp ~lines 334-337)**

```cpp
const auto mode_count = pkgi_mode_count();
// ...
for (int i = 0; i < pkgi_mode_count(); ++i)
```

- [ ] **Step 4: Make `build_tree` iterate the table (browserview.cpp ~line 42)**

```cpp
static std::vector<BrowseNode> build_tree(const Config& config)
{
    std::vector<BrowseNode> root;
    for (int i = 0; i < pkgi_mode_count(); ++i)
        root.push_back({ pkgi_system(i).display_name,
                         static_cast<Mode>(i), {}, "", "" });

    int custom_number = 1;
    for (const auto& entry : config.custom_entries)
    {
        if (entry.name.empty() || entry.url.empty())
            continue;
        root.push_back({ fmt::format("Custom {}", custom_number++),
                         std::nullopt, {}, "", entry.url });
    }
    return root;
}
```

Add `#include "systems.hpp"` to `browserview.cpp` if not transitively available (it is, via `db.hpp`).

- [ ] **Step 5: Verify the host simulator still links**

Since the machine has no conan, this step is best-effort: check the edited files compile in isolation for obvious errors by grepping for leftover references:

```bash
grep -rn "config\.gb_url\|config\.snes_url\|config\.ps1_url\|config\.psp_url\|ModeCount" src/
```

Expected: **no matches** (all removed). Any match is a leftover to fix.

- [ ] **Step 6: Commit**

```bash
git add src/pkgi.cpp src/browserview.cpp
git commit -m "refactor: route UI (url resolution, refresh mask, system list) through the table"
```

---

## Task 7: Add the RetroArch consoles

**Files:**
- Modify: `src/systems.cpp` (append rows), `tests/test_systems.cpp` (extend matching coverage)

**Interfaces:**
- Consumes/Produces: extends `pkgi_systems()` with new rows. All new rows use `InstallRetroArchRom`. Appended after index 7 (constraint).

- [ ] **Step 1: Write the failing test (new-console matching + growth)**

Append to `tests/test_systems.cpp`:

```cpp
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
```

- [ ] **Step 2: Run to verify it fails**

Run: `./tests/run.sh`
Expected: FAIL — `pkgi_system_by_id("mastersystem")` returns nullptr, `REQUIRE_MESSAGE` aborts.

- [ ] **Step 3: Append the new rows to `pkgi_systems()` in `src/systems.cpp`**

Insert **after** the `psp` row (indices ≥ 8), before the closing `};`:

```cpp
        {"mastersystem", "Master System",     "mastersystem", "roms_mastersystem.dat",
            {".sms"},                 InstallRetroArchRom, "", "url_mastersystem"},
        {"gamegear",     "Game Gear",         "gamegear",     "roms_gamegear.dat",
            {".gg"},                  InstallRetroArchRom, "", "url_gamegear"},
        {"pcengine",     "PC Engine / TG-16", "pcengine",     "roms_pcengine.dat",
            {".pce"},                 InstallRetroArchRom, "", "url_pcengine"},
        {"ngp",          "Neo Geo Pocket",    "ngp",          "roms_ngp.dat",
            {".ngp", ".ngc"},         InstallRetroArchRom, "", "url_ngp"},
        {"wonderswan",   "WonderSwan",        "wswan",        "roms_wonderswan.dat",
            {".ws", ".wsc"},          InstallRetroArchRom, "", "url_wonderswan"},
        {"atari2600",    "Atari 2600",        "atari2600",    "roms_atari2600.dat",
            {".a26", ".bin"},         InstallRetroArchRom, "", "url_atari2600"},
        {"atari7800",    "Atari 7800",        "atari7800",    "roms_atari7800.dat",
            {".a78"},                 InstallRetroArchRom, "", "url_atari7800"},
        {"lynx",         "Atari Lynx",        "lynx",         "roms_lynx.dat",
            {".lnx"},                 InstallRetroArchRom, "", "url_lynx"},
        {"neogeo",       "Neo Geo",           "neogeo",       "roms_neogeo.dat",
            {},                       InstallRetroArchRom, "", "url_neogeo"},
        {"msx",          "MSX",               "msx",          "roms_msx.dat",
            {".rom", ".mx1", ".mx2", ".dsk"}, InstallRetroArchRom, "", "url_msx"},
        {"colecovision", "ColecoVision",      "colecovision", "roms_colecovision.dat",
            {".col"},                 InstallRetroArchRom, "", "url_colecovision"},
        {"virtualboy",   "Virtual Boy",       "virtualboy",   "roms_virtualboy.dat",
            {".vb"},                  InstallRetroArchRom, "", "url_virtualboy"},
        {"n64",          "Nintendo 64",       "n64",          "roms_n64.dat",
            {".n64", ".z64", ".v64"}, InstallRetroArchRom, "", "url_n64"},
```

> `neogeo` has empty `extensions` (arcade sets ship as `.zip`, already universal). `atari2600` includes `.bin`, which `genesis` also uses — that is fine, extensions are per-system and never cross-checked.

- [ ] **Step 4: Run to verify it passes**

Run: `./tests/run.sh`
Expected: PASS — new-console cases green; table-integrity case still green (ids/caches/keys unique).

- [ ] **Step 5: Commit**

```bash
git add src/systems.cpp tests/test_systems.cpp
git commit -m "feat: add RetroArch consoles (SMS, GG, PCE, NGP, WS, Atari, Neo Geo, MSX, CV, VB, N64)"
```

---

## Task 8: CI job that builds+runs the tests (no conan)

**Files:**
- Create: `.github/workflows/test.yml`

**Interfaces:**
- Consumes: `tests/run.sh` (Task 1, kept current by later tasks).

- [ ] **Step 1: Write the workflow**

`.github/workflows/test.yml`:

```yaml
name: unit-tests

on:
  push:
  pull_request:
  workflow_dispatch:

jobs:
  tests:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Install toolchain
        run: sudo apt-get update && sudo apt-get install -y g++-12
      - name: Build and run pure-logic tests
        env:
          CXX: g++-12
        run: ./tests/run.sh
```

- [ ] **Step 2: Verify the script is CI-safe locally**

Run: `CXX=g++-12 ./tests/run.sh`
Expected: PASS — same output CI will produce; exit 0.

- [ ] **Step 3: Commit**

```bash
git add .github/workflows/test.yml
git commit -m "ci: run pure-logic unit tests on push/PR (no conan required)"
```

---

## Task 9: Device build + on-device smoke check

**Files:** none (build + deploy only).

- [ ] **Step 1: Trigger the CI VPK build**

```bash
gh workflow run build.yml --ref "$(git rev-parse --abbrev-ref HEAD)"
gh run watch "$(gh run list --workflow build.yml -L1 --json databaseId -q '.[0].databaseId')" --exit-status
```

Expected: build succeeds (~6–10 min cold). If it fails to compile, fix the reported file and re-run — the host refactor may surface a device-only include.

- [ ] **Step 2: Deploy the eboot (requires the user's Vita FTP awake)**

```bash
RUN_ID="$(gh run list --workflow build.yml -L1 --json databaseId -q '.[0].databaseId')"
gh run download "$RUN_ID" -n pkgj-retroarch -D /tmp/pkgj_out
( cd /tmp/pkgj_out && unzip -o pkgj.vpk )
curl -T /tmp/pkgj_out/eboot.bin "ftp://192.168.0.166:1337/ux0:/app/PKGJ00000/eboot.bin"
```

Expected: `curl` completes. Exit 7 / "not reachable" → ask the user to re-enable VitaShell FTP (SELECT), then retry.

- [ ] **Step 3: Manual smoke check (ask the user)**

Ask the user to fully close + relaunch PKGj+ and confirm: the system list now shows the new consoles (Master System … N64), and refreshing a system with a configured `url_<id>` still downloads and installs into `ux0:roms/<dir>/`. Report their result.

- [ ] **Step 4: No commit** (verification only).

---

## Self-Review

**Spec coverage:**
- Table data-driven → Task 2. ✓
- Config data-driven (`system_urls`, legacy keys) → Task 5. ✓
- Extract `jsonscan` → Task 3; `romcache` → Task 4. ✓
- Test harness (doctest, host g++, CI) → Tasks 1 + 8. ✓
- Test cases (matching, exclusions, cache round-trip, JSON scan, URL build, table integrity) → Tasks 2/3/4/7. ✓
- New RetroArch consoles → Task 7. ✓
- UI routing (mode→url, allow_refresh, build_tree, ModeCount) → Task 6. ✓
- Roadmap Phases 2/3 reserved via `InstallKind` → Task 2 (enum) — implementation deferred, as specified. ✓
- Mode index stability / append-only → enforced in Task 2 (Global Constraints) + Task 7 (append after 7). ✓
- ia*-only URL constraint → Task 4 test asserts no `/download/`. ✓

**Placeholder scan:** No "TBD"/"handle edge cases"/"write tests for the above" — every code/test step shows full content. The only deferred item (`default_item` for new consoles) is intentionally empty and is a supported state (matches PSP today). ✓

**Type consistency:** `pkgi_system(int)`, `pkgi_mode_count()`, `pkgi_matches_extension(const SystemDef&, const std::string&)`, `pkgi_is_excluded_file(const std::string&)`, `pkgi_json_str(const char*, size_t, const char*)`, `pkgi_find_object_end(const char*, const char*)`, `RomCacheLine{name,size,url}`, `pkgi_parse_cache_line`, `pkgi_format_cache_line`, `pkgi_build_download_url`, `pkgi_config_url(const Config&, int)` — used identically across producing and consuming tasks. `Mode` used as an int index throughout; `pkgi_system` accepts `int` so `Mode`/`int` interchange is safe. ✓

**Note on `pkgj_cli`:** it did not previously compile `config.cpp`; Task 5 adds only the dependency-free `config_url.cpp` there, not `config.cpp`, so no new heavy dependency is pulled into the CLI. ✓
