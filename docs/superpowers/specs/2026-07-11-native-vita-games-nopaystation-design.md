# Design — Jeux PS Vita natifs via NoPayStation (Phase 3)

**Date:** 2026-07-11
**Statut:** proposé (Phase 3 de la feuille de route "toutes consoles compatibles PS Vita")
**Repo:** `pkgj-retroarch`
**Prérequis:** Phase 0+1 (table de systèmes data-driven + `jsonscan`/`romcache` + harnais de tests) — livrée et vérifiée sur device.

## Contexte

Le fork installe des ROMs Archive.org via une **table de systèmes data-driven** (`src/systems.{hpp,cpp}`) ; `Mode` est un index dans la table, et le pipeline `update()` (fetch + parse) → cache → `reload()` (→ `DbItem`) → download → install est piloté par la table.

La machinerie d'installation **PSN/PKG d'origine de pkgj est intacte** :
- `Downloader::do_download_package` (`downloader.cpp:147`) télécharge un `.pkg`, le déchiffre avec la `rif` (issue du zRIF) et l'extrait ; pour `Type::Game`/`Dlc` il appelle `pkgi_install`, pour `Type::Patch` `pkgi_install_update`.
- `pkgi_install` (`install.cpp`) fait le `promote` (installation système).
- `zrif.cpp` décode le zRIF base64 → `rif`.
- `PatchInfoFetcher`/`update.cpp`/`patchinfo.cpp` existent et **`gameview.cpp:143,534` utilise déjà `PatchInfoFetcher`** (requête XML Sony pour la dernière mise à jour d'un titre).
- `DownloadItem` porte `type`, `content` (content_id), `url`, `rif`, `digest`, `partition` ; `DbItem` porte encore `titleid`/`content`/`zrif`/`url`/`has_digest`/`digest`/région.

Ce qui a été **retiré** au commit `adb2ed7` ("replace PSN TSV with Archive.org API") :
- le parseur TSV NoPayStation dans `update()` (`pkgi_split_row`, colonnes, régions) ;
- les champs config `games_url`/`dlcs_url`/… et leurs défauts base64 (`default_psv_games_url`, `default_psv_dlcs_url`) ;
- le `reload()` du cache PKG (avec zRIF/content_id).

## Objectif

Rebrancher la source **NoPayStation** pour les **jeux PS Vita, leurs DLC et leurs mises à jour**, en réutilisant la chaîne d'install PKG intacte et en épousant la table data-driven de la Phase 0+1.

Décision utilisateur : périmètre = **Jeux + DLC + Updates** ; **URL TSV NoPS par défaut embarquée**.

## Non-objectifs

- PSP-Adrenaline (phase séparée), PSM, PSX, thèmes Vita.
- Réécriture de `do_download_package`/`pkgi_install`/zRIF (réutilisés tels quels).
- Abstraction "provider" polymorphe générique (YAGNI — 2 sources suffisent, on branche sur un `enum`).

## Décomposition en sous-phases (chacune = son propre plan d'implémentation)

- **3a — Jeux Vita (fondation)** : `SourceKind`, parseur `npstsv`, récupération config + défaut embarqué, cache PKG + `reload()`, download `Type::Game` + install, entrée UI "PS Vita Games", tests. **C'est le premier plan.**
- **3b — DLC Vita** : 2ᵉ TSV (`dlcs_url`), `Type::Dlc`, UI liste/multi-select DLC pour un titre (réactiver `MenuResultShowDlcs`).
- **3c — Mises à jour Vita** : brancher `PatchInfoFetcher` (déjà présent) → `Type::Patch` → `pkgi_install_update`, avec détection de version installée.

Le présent spec décrit l'architecture commune aux trois ; **3a est le périmètre du premier plan**, 3b/3c suivront (specs/plans incrémentaux réutilisant la même fondation).

## Architecture

### 1. `SourceKind` dans la table — `src/systems.{hpp,cpp}`

Ajouter un champ `source` à `SystemDef` :

```cpp
enum class SourceKind {
    ArchiveOrgRom,  // fetch = archive.org/metadata JSON ; install = copie ux0:roms/
    NpsVita,        // fetch = NoPayStation TSV ; install = PKG promote (Type::Game/Dlc/Patch)
};
```

Toutes les lignes RetroArch existantes prennent `source = ArchiveOrgRom` (aucun changement de comportement). Nouvelle ligne :

```
{"psvita", "PS Vita Games", /*roms_dir*/"", /*cache_file*/"nps_psvita.dat",
    /*extensions*/{}, InstallVitaNative, /*default_item*/"<TSV url>", "url_psvita",
    /*source*/SourceKind::NpsVita}
```

`InstallVitaNative` (déjà réservé en Phase 0+1) et `SourceKind::NpsVita` vont de pair. `roms_dir` inutilisé pour Vita (install via `promote`, pas de copie).

### 2. Parseur TSV NoPayStation — `src/npstsv.{hpp,cpp}` (STL-only, testé)

Unité pure, incluant uniquement la STL — récupère la logique de `pkgi_split_row` (git `adb2ed7^:src/db.cpp`) et l'accès par colonne.

```cpp
enum class NpsColumn { TitleId, Region, Name, PkgUrl, Zrif, ContentId, LastModified, Size /* … */ };

struct NpsRow {
    std::string titleid, region, name, pkg_url, zrif, content_id;
    long long size = 0;
};

// Parse une ligne TSV (séparateur '\t'). false si colonnes essentielles absentes,
// ou si pkg_url == "MISSING" / zrif == "MISSING" (jeu indisponible).
bool pkgi_parse_nps_row(const std::string& line, NpsRow& out);

// En-tête TSV : mappe les noms de colonnes -> indices (l'ordre NoPS varie selon la TSV).
// Appelé une fois sur la 1ère ligne ; les parses suivants utilisent ce mapping.
struct NpsColumnMap { /* index par NpsColumn, -1 si absent */ };
NpsColumnMap pkgi_parse_nps_header(const std::string& header_line);
bool pkgi_parse_nps_row(const std::string& line, const NpsColumnMap& cols, NpsRow& out);
```

> Le format NoPS a un **en-tête nommé** (`Title ID\tRegion\tName\tPKG direct link\tzRIF\t…`). On parse l'en-tête pour trouver les indices plutôt que de coder des positions en dur — plus robuste aux variantes GAMES/DLCS.

### 3. Data layer branché sur `source` — `src/db.cpp`

- `update(mode, http, url)` : `switch (pkgi_system(mode).source)` —
  - `ArchiveOrgRom` → chemin actuel (jsonscan + romcache), inchangé.
  - `NpsVita` → GET la TSV (URL = `pkgi_config_url(config, mode)`), parse l'en-tête puis chaque ligne via `npstsv`, saute `MISSING`, écrit un **cache PKG** : `titleid|region|name|pkg_url|zrif|content_id|size`.
- `reload(mode, …)` : si `source == NpsVita`, parse le cache PKG → `DbItem` avec `titleid`/`content=content_id`/`name`/`url=pkg_url`/`zrif`/région ; applique le filtre régions (déjà supporté : `DbFilter*`, `pkgi_get_region`). Sinon chemin ROM actuel.
- Le format de ligne du cache PKG est isolé dans `src/pkgcache.{hpp,cpp}` (STL-only, testé) — parse/format symétriques, comme `romcache`.

### 4. Download → install — `src/pkgi.cpp` / `downloader.cpp` (réutilisés)

- `pkgi_start_download` : si `source == NpsVita`, enfiler un `DownloadItem{ type=Type::Game, content=content_id, url=pkg_url, rif=pkgi_zrif_decode(zrif), digest, partition="ux0:" }` au lieu d'un `RomGame`. (3b utilisera `Type::Dlc`, 3c `Type::Patch`.)
- `do_download_package` + `pkgi_install` : inchangés — ils font download+déchiffrement+extract+promote.
- Lignes sans zRIF valide : non téléchargeables (déjà filtrées à l'`update`).

### 5. Config — `src/config.{hpp,cpp}`

- Restaurer `url_psvita` (jeux) via le mécanisme data-driven `system_urls` de la Phase 0+1 : la clé `url_psvita` = `config_key` de la ligne `psvita`, donc **aucun champ ad hoc** — juste une entrée de plus dans la map, et le défaut embarqué vit dans `SystemDef::default_item`.
- Défaut embarqué : récupérer la valeur base64 `default_psv_games_url` de `adb2ed7^:src/config.cpp` (URL NoPS PSV_GAMES) et la décoder en clair dans `default_item`. (3b fera de même pour `url_psvita_dlc` / PSV_DLCS.)

### 6. UI — `src/browserview.cpp` / `gameview.cpp`

- `build_tree` : la ligne `psvita` apparaît automatiquement (itération sur la table). MVP = liste plate cherchable ; le sous-groupement par lettre (`BrowseNode` + `group_filter`) est repoussé.
- `gameview` : pour un item Vita, afficher région + taille ; l'action download déclenche l'install PKG. (3c réactivera l'affichage "mise à jour disponible" via `PatchInfoFetcher`, déjà instancié en `gameview.cpp:143`.)

## Flux de données (Vita, 3a)

1. UI sélectionne le mode `psvita` (index table, `source=NpsVita`).
2. `update()` GET la TSV NoPS → parse en-tête + lignes → cache `nps_psvita.dat`.
3. `reload()` → `DbItem`s (titleid, content_id, pkg_url, zRIF, région) filtrés par région/recherche.
4. Sélection → `pkgi_start_download` enfile `Type::Game` (zRIF décodé → rif).
5. `do_download_package` télécharge le `.pkg`, déchiffre via rif, extrait ; `pkgi_install` promeut.

## Tests

Même stratégie que Phase 0+1 (doctest, g++ pur sans conan, cible `pkgj_tests` + CI) :
- `npstsv` : parse d'en-tête (mapping colonnes), parse de ligne réelle (fixture TSV NoPS tronquée), colonnes manquantes → `false`, `MISSING` (pkg/zrif) → `false`, ligne avec tabs en trop/manquants, dérivation de région depuis le titleid.
- `pkgcache` : aller-retour parse/format du cache PKG, ligne malformée → échec propre, champs avec caractères spéciaux.
- Intégrité table : la ligne `psvita` a `source=NpsVita`, `install=InstallVitaNative`, `cache_file` unique, `config_key=url_psvita` unique.
- zRIF : `pkgi_zrif_decode` est dans `zrif.cpp` (déjà lié au host `pkgj_cli`) ; ajouter un cas sur un zRIF connu → rif attendu si les dépendances (puff) le permettent, sinon repoussé.
- Fixture : `tests/fixtures/nps_psv_games.tsv` = en-tête NoPS réel + 3 lignes (1 jeu valide, 1 `MISSING` pkg, 1 zRIF `MISSING`).

## Gestion d'erreurs

- TSV vide / en-tête non reconnu → `update` lève (message clair), n'écrase pas le cache existant (écriture via fichier temporaire puis rename, comme l'actuel).
- Ligne TSV malformée / colonne essentielle absente → ignorée (log warn), n'interrompt pas le parse.
- zRIF `MISSING` ou pkg `MISSING` → item exclu du cache (non installable).
- Échec de déchiffrement/promote → remonté par `do_download_package`/`pkgi_install` (chemin d'erreur existant, inchangé).

## Migration / compatibilité

- `Mode`/index : la ligne `psvita` est **ajoutée en fin de table** (après les consoles RetroArch) — indices historiques 0..7 et les nouvelles consoles inchangés.
- `config.txt` : `url_psvita <tsv_url>` override le défaut ; absence → défaut embarqué. Les clés RetroArch existantes inchangées.
- Aucune migration de données : `nps_psvita.dat` est un nouveau cache.

## Risques / points ouverts

- **Légalité/robustesse de la source NoPS** : URLs et disponibilité varient ; le défaut embarqué peut devenir périmé — l'override config reste la soupape.
- **Variabilité du format TSV** : d'où le parse d'en-tête nommé plutôt que des indices en dur.
- **zRIF manquants** : beaucoup d'entrées NoPS n'ont pas de zRIF → non installables ; comportement attendu (filtré), à signaler clairement dans l'UI plutôt que d'échouer au download.
- **Compile device non testable en local** (pas de conan/VitaSDK) : `db.cpp`/`pkgi.cpp`/`downloader.cpp` vérifiés par lecture+grep, compile réel en CI (comme Phase 0+1).
- **Périmètre 3b/3c** : DLC (multi-select) et updates (PatchInfoFetcher) réactivent de l'UI dormante ; spec/plan dédiés après validation de 3a sur device.
