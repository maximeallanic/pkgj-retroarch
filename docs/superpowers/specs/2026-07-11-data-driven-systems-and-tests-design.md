# Design — Table de systèmes data-driven + harnais de tests + consoles RetroArch

**Date:** 2026-07-11
**Statut:** proposé (Phase 0+1 de la feuille de route "toutes consoles compatibles PS Vita")
**Repo:** `pkgj-retroarch`

## Contexte

Fork de PKGj repurposé pour installer des ROMs hébergées sur Archive.org dans
`ux0:roms/<system>/` pour RetroArch. Le fork supporte aujourd'hui **8 systèmes**
codés en dur via un `enum Mode` (GB, GBC, GBA, SNES, NES, Genesis, PS1, PSP).

Cet enum figé se propage en `switch` dupliqués dans tout l'arbre :
- `db.cpp` — ~41 occurrences (`pkgi_mode_to_string`, `pkgi_mode_to_system_dir`,
  `mode_to_cache`, `is_rom_file`)
- `pkgi.cpp` — ~32 occurrences (mode → url, masque `allow_refresh`, itération UI)
- `config.cpp` / `config.hpp` — 8 champs `gb_url…psp_url` + 8 `if` de parse + 8 `SAVE_URL`
- `browserview.cpp` (~16), `gameview.cpp` (~5), `cli.cpp` (~9), `vita.cpp` (~2)

**Objectif de l'utilisateur :** ajouter toutes les consoles compatibles PS Vita
(consoles RetroArch, PSP via Adrenaline, jeux Vita natifs) + des tests vérifiant le
bon fonctionnement depuis les sources.

Ce spec couvre uniquement **Phase 0 + Phase 1**. Les phases 2 et 3 auront leurs
propres specs (voir Roadmap).

## Objectifs (Phase 0+1)

1. Remplacer l'`enum Mode` figé par une **table de systèmes pilotée par données**, de
   sorte qu'ajouter une console = ajouter une ligne.
2. Extraire la logique pure (matching d'extensions, exclusions, scanner JSON, format de
   cache, construction d'URL) dans des **unités sans dépendance externe**, testables
   isolément.
3. Mettre en place un **harnais de tests** (doctest, host g++ + CI dédié) couvrant cette
   logique.
4. Ajouter les **consoles RetroArch** réellement supportées par les cores Vita.

**Hors périmètre (phases ultérieures) :** installation PSP-Adrenaline, installation Vita
native (.vpk/.pkg). Le design *prépare* ces chemins via un champ `install` mais ne les
implémente pas.

## Non-objectifs

- Refonte de l'UI ImGui au-delà de l'itération data-driven de la liste de systèmes.
- Réécriture du téléchargeur / installeur ROM (inchangé, il copie déjà vers
  `ux0:roms/<dir>/`).
- Support d'un vrai parseur JSON tiers (on garde le scanner maison, on le teste).

## Architecture

### 1. Table de systèmes — `src/systems.{hpp,cpp}`

Nouvelle unité, **dépendance STL uniquement** (pas de fmt/http/sqlite).

```cpp
enum class InstallKind {
    RetroArchRom,   // copie vers ux0:roms/<roms_dir>/   (seul implémenté en Phase 0+1)
    PspAdrenaline,  // Phase 2  (réservé, non utilisé)
    VitaNative,     // Phase 3  (réservé, non utilisé)
};

struct SystemDef {
    std::string id;            // clé stable, ex. "gb", "snes", "pcengine"
    std::string display_name;  // "Game Boy"
    std::string roms_dir;      // dossier ux0:roms/<x>/ ; souvent == id
    std::string cache_file;    // "roms_gb.dat"
    std::vector<std::string> extensions;  // {".gb"} — .zip/.7z toujours acceptés en plus
    InstallKind install;
    std::string default_item;  // identifiant Archive.org par défaut ("" si aucun)
    std::string config_key;    // "url_gb"
};

const std::vector<SystemDef>& pkgi_systems();          // la table (ordre = ordre UI)
const SystemDef& pkgi_system(Mode mode);               // Mode == index dans la table
const SystemDef* pkgi_system_by_id(const std::string& id);

bool pkgi_matches_extension(const SystemDef& sys, const std::string& base_lower);
bool pkgi_is_excluded_file(const std::string& base_lower);
```

`Mode` reste défini (`enum Mode : int`) mais devient **un simple index** dans la table.
`ModeCount` = `pkgi_systems().size()`. Les valeurs historiques (`ModeGB=0…ModePSP=7`)
restent aux mêmes positions pour ne pas invalider d'état persistant.

`pkgi_mode_to_string`, `pkgi_mode_to_system_dir`, `mode_to_cache`, `is_rom_file`
délèguent à la table. `db.cpp` inclut `systems.hpp` et supprime ses `switch`.

### 2. JSON scanner — `src/jsonscan.{hpp,cpp}`

Extraction des helpers maison actuellement `static` dans `db.cpp` :
`json_str`, `find_object_end`, `json_unescape`, plus l'extraction `d1`/`dir` et la
construction d'URL de téléchargement. **STL uniquement.** `db.cpp` inclut le header.

```cpp
std::string pkgi_json_str(const std::string& json, const std::string& key, size_t from = 0);
size_t      pkgi_find_object_end(const std::string& json, size_t obj_start);
std::string pkgi_json_unescape(const std::string& s);
std::string pkgi_url_encode_path(const std::string& path);
```

### 3. Format de cache ROM — `src/romcache.{hpp,cpp}`

La ligne de cache est `file_name|size|download_url` (pipe-délimité). On extrait
parse/format dans une unité pure, avec la construction d'URL `https://<d1>/<dir>/<file>`
(fleet ia*, pas le redirect `/download/` — contrainte SSL Vita, cf. CLAUDE.md).

```cpp
struct RomCacheLine { std::string name; int64_t size; std::string url; };
bool        pkgi_parse_cache_line(const std::string& line, RomCacheLine& out);
std::string pkgi_format_cache_line(const RomCacheLine& in);
std::string pkgi_build_download_url(const std::string& d1,
                                    const std::string& dir,
                                    const std::string& file);
```

### 4. Config data-driven — `src/config.{hpp,cpp}`

- Remplacer les 8 champs `gb_url…psp_url` par
  `std::map<std::string,std::string> system_urls;` (clé = `SystemDef::id`).
- `pkgi_config_url(Mode)` : renvoie `system_urls[id]` si présent, sinon
  `default_item` de la table.
- Parse `config.txt` : pour chaque ligne `url_<key> <value>`, retrouver le système par
  `config_key` et stocker. **Rétrocompat :** les anciennes clés (`url_gb`…) restent
  valides car ce sont exactement les `config_key` de la table.
- Save : boucle sur la table, écrit `url_<key> <value>` uniquement si ≠ défaut.
- `install_psp_psx_location`, `comppack_url`, `thumbnail_*`, `custom_entries` : inchangés.

### 5. Consommateurs UI

- `pkgi.cpp` : `mode_to_url` → `pkgi_config_url`. Masque `allow_refresh` construit par
  boucle sur la table (`pkgi_config_url(m).empty()`). Itération de systèmes par index
  `0..ModeCount`.
- `browserview.cpp`, `gameview.cpp`, `cli.cpp`, `menu.cpp` : remplacer les `switch Mode`
  et libellés en dur par `pkgi_system(mode).display_name` / `.roms_dir`.

## Flux de données (inchangé sur le fond)

1. UI sélectionne un `Mode` (index table).
2. `update(mode, http, pkgi_config_url(mode))` GET `archive.org/metadata/<item>`.
3. Scanner JSON (`jsonscan`) → walk `files[]`, garde ceux qui passent
   `pkgi_matches_extension` et pas `pkgi_is_excluded_file`, construit l'URL via
   `romcache`/`jsonscan`.
4. Écrit le cache `roms_<id>.dat`.
5. `reload` parse le cache (`pkgi_parse_cache_line`) → `DbItem`s.
6. Download → `pkgi_install_rom` copie vers `ux0:roms/<roms_dir>/`.

## Consoles ajoutées (Phase 1)

Uniquement des systèmes dont un core RetroArch tourne réellement sur PS Vita.
`default_item` renseigné seulement si un identifiant Archive.org plausible existe ;
sinon vide (comme PSP aujourd'hui) — l'utilisateur le fournit via `config.txt`.

| id | Nom | roms_dir | extensions | core Vita |
|----|-----|----------|-----------|-----------|
| mastersystem | Master System | mastersystem | .sms .zip .7z | Genesis Plus GX |
| gamegear | Game Gear | gamegear | .gg .zip .7z | Genesis Plus GX |
| pcengine | PC Engine / TG-16 | pcengine | .pce .zip .7z | Beetle PCE Fast |
| ngp | Neo Geo Pocket | ngp | .ngp .ngc .zip .7z | Beetle NeoPop |
| wonderswan | WonderSwan (Color) | wswan | .ws .wsc .zip .7z | Beetle Cygne |
| atari2600 | Atari 2600 | atari2600 | .a26 .bin .zip .7z | Stella |
| atari7800 | Atari 7800 | atari7800 | .a78 .zip .7z | ProSystem |
| lynx | Atari Lynx | lynx | .lnx .zip .7z | Beetle Handy |
| neogeo | Neo Geo (arcade) | neogeo | .zip .7z | FBNeo |
| msx | MSX / MSX2 | msx | .rom .mx1 .mx2 .dsk .zip .7z | blueMSX/fMSX |
| colecovision | ColecoVision | colecovision | .col .zip .7z | blueMSX |
| virtualboy | Virtual Boy | virtualboy | .vb .zip .7z | Beetle VB |
| n64 | Nintendo 64 (best-effort) | n64 | .n64 .z64 .v64 .zip .7z | Mupen64Plus |

> La liste exacte des `default_item` sera fixée à l'implémentation ; l'absence
> d'`default_item` est un état supporté (pas une régression).

## Tests

**Framework :** doctest (header unique `tests/doctest.h`, vendoré, zéro dépendance).

**Cible CMake `pkgj_tests`** dans `host.cmake` (ou `tests/CMakeLists.txt`), compilée avec
g++ + STL **sans conan** (ne link que `systems.cpp`, `jsonscan.cpp`, `romcache.cpp` +
les `.cpp` de test). Objectif : `ctest` / binaire exécutable localement et en CI.

**Job CI `.github/workflows/test.yml`** : `ubuntu-latest`, install g++/cmake/ninja,
build `pkgj_tests`, run. Pas de conan → rapide, sépare la vérif logique du build VPK.

**Cas de test :**

1. `matches_extension` — pour chaque système : extensions valides (positif), extension
   d'un autre système (négatif), casse mixte (`.SFC`), `.zip`/`.7z` universels acceptés,
   nom sans extension rejeté.
2. `is_excluded_file` — `collection.zip`, `_meta.xml`, `_files.xml`, `_reviews.xml`,
   `.torrent`, `.sqlite`, `.xml`, `romset`, `rom collection`, `full_rom_pack` exclus ;
   ROM normale non exclue.
3. `parse_cache_line` / `format_cache_line` — aller-retour, ligne malformée (pas de `|`,
   size non numérique) → échec propre, URL avec `|` improbable géré.
4. `jsonscan` — sur fixture `tests/fixtures/archive_metadata.json` (échantillon réel
   d'`archive.org/metadata/<id>`) : extraction `server`/`d1`/`dir`, itération `files[]`,
   `find_object_end` sur objets imbriqués, `json_unescape` (`\/`, `\\`, `\"`).
5. `build_download_url` — assemble `https://<d1>/<dir>/<file>` avec encodage de chemin
   correct (espaces, unicode) ; **ne doit pas** produire une URL `/download/` (ECDSA).
6. Intégrité de la table — `id` uniques, `cache_file` uniques, `roms_dir` non vide,
   `config_key` uniques, `Mode` historiques (0..7) toujours aux mêmes id.

**Fixtures :** un vrai JSON de metadata Archive.org tronqué à quelques `files[]`
(inclut un fichier ROM valide, un `_meta.xml`, un `collection.zip`) pour exercer le
pipeline complet extension+exclusion+URL.

## Gestion d'erreurs

- Table : accès `pkgi_system(mode)` hors bornes → assert/clamp (Mode invalide = bug).
- `parse_cache_line` : ligne invalide → `false`, ignorée par `reload` (log warn),
  n'interrompt pas le chargement des autres lignes.
- Config : clé `url_*` inconnue → ignorée silencieusement (rétrocompat forward).
- `jsonscan` : clé absente → chaîne vide ; `find_object_end` sans fermeture → `npos`.

## Plan de migration / ordre d'implémentation

1. Créer `systems.{hpp,cpp}` avec les 8 systèmes existants (aucun changement de
   comportement) ; `Mode` = index ; router `db.cpp` dessus. **Build host + tests verts.**
2. Extraire `jsonscan` et `romcache` depuis `db.cpp` ; `db.cpp` inclut. Tests.
3. Migrer `config` vers `system_urls` (rétrocompat clés). Tests.
4. Router `pkgi.cpp` / `browserview` / `gameview` / `cli` / `menu` sur la table.
5. Ajouter le harnais doctest + `test.yml` CI.
6. Ajouter les nouvelles consoles (lignes de table). Étendre les tests de matching.
7. Build VPK via CI (`gh workflow run build.yml`) et vérifier sur device (déploiement FTP
   décrit dans CLAUDE.md).

Chaque étape garde l'arbre compilable et les tests verts (refactor incrémental).

## Roadmap (specs séparés, hors de ce document)

- **Phase 2 — PSP via Adrenaline** : `InstallKind::PspAdrenaline`, install vers
  `ux0:pspemu/ISO/` (ou EBOOT), formats `.iso/.cso/.pbp`. Nouveau handler d'install.
- **Phase 3 — Jeux Vita natifs** : `InstallKind::VitaNative`, réactive la machinerie
  zRIF/RIF/bgdl/promote dormante pour `.vpk`/`.pkg`. Chemin d'install radicalement
  différent (pas une copie de fichier) ; source non-Archive.org probable.

## Risques / points ouverts

- **Persistance de `Mode` :** si un état sur device encode `Mode` en entier, insérer des
  systèmes au milieu décalerait les index. Mitigation : **ajouter les nouveaux en fin de
  table**, garder 0..7 stables.
- **`default_item` des nouvelles consoles :** peut rester vide au départ ; ce n'est pas un
  échec de test (état supporté).
- **N64 sur Vita :** core lourd/instable ; inclus en "best-effort", peut être retiré si
  jugé trompeur.
- **Compilation des unités extraites sans conan :** exige qu'elles n'incluent ni fmt ni
  http ni sqlite. Contrainte de design assumée (c'est aussi ce qui les rend testables).
