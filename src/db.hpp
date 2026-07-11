#pragma once

#include "annotationdb.hpp"
#include "http.hpp"
#include "systems.hpp"

#include <array>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <cstdint>

enum DbPresence
{
    PresenceUnknown,
    PresenceIncomplete,
    PresenceInstalling,
    PresenceInstalled,
    PresenceMissing,
    PresenceGamePresent,
};

enum DbSort
{
    SortByTitle,
    SortByRegion,
    SortByName,
    SortBySize,
    SortByDate,
};

enum DbSortOrder
{
    SortAscending,
    SortDescending,
};

enum DbFilter
{
    DbFilterRegionASA = 0x01,
    DbFilterRegionEUR = 0x02,
    DbFilterRegionJPN = 0x04,
    DbFilterRegionUSA = 0x08,

    DbFilterInstalled = 0x10,

    DbFilterAllRegions = DbFilterRegionUSA | DbFilterRegionEUR |
                         DbFilterRegionJPN | DbFilterRegionASA,
    DbFilterAll = DbFilterAllRegions,
};

struct DbItem
{
    DbPresence presence;
    std::string titleid;   // = Archive.org identifier
    std::string content;   // = Archive.org identifier
    uint32_t flags;
    std::string name;
    std::string name_org;
    std::string zrif;      // always empty for ROMs
    std::string url;       // Archive.org download URL
    bool has_digest;       // always false for ROMs
    std::array<uint8_t, 32> digest; // always empty for ROMs
    int64_t size;
    std::string date;
    std::string app_version; // unused for ROMs
    std::string fw_version;  // unused for ROMs
    bool selected;

    // System string (e.g. "gb", "gba", "snes")
    std::string system;

    // Fetched live (not persisted to DB)
    std::string description;

    // Personal annotation (loaded from AnnotationDatabase after reload)
    UserFlag    user_flag    = UserFlag::None;
    std::string user_comment;
};

enum GameRegion
{
    RegionASA,
    RegionEUR,
    RegionJPN,
    RegionUSA,
    RegionINT,
    RegionUnknown,
};

// Mode is an index into pkgi_systems() (see systems.hpp).
enum Mode
{
    ModeGB = 0, ModeGBC, ModeGBA, ModeSNES,
    ModeNES, ModeGenesis, ModePS1, ModePSP,
};

std::string pkgi_mode_to_string(Mode mode);
std::string pkgi_mode_to_system_dir(Mode mode);

class TitleDatabase
{
public:
    TitleDatabase(const std::string& dbPath);

    void reload(
            Mode mode,
            uint32_t region_filter,
            DbSort sort_by,
            DbSortOrder sort_order,
            const std::string& search,
            const std::set<std::string>& installed_games);

    void update(Mode mode, Http* http, const std::string& update_url);
    void get_update_status(uint32_t* updated, uint32_t* total);

    uint32_t count();
    uint32_t total();
    DbItem* get(uint32_t index);
    DbItem* get_by_content(const char* content);

private:
    static constexpr auto MAX_DB_ITEMS = 8192;

    void update_nps_tsv(Mode mode, Http* http, const std::string& update_url);
    void reload_nps_tsv(
            Mode mode,
            uint32_t region_filter,
            const std::string& search,
            const std::set<std::string>& installed_games,
            const std::string& data);

    std::string _dbPath;
    uint32_t db_total;
    uint32_t db_size;
    uint32_t _title_count;

    std::vector<DbItem> db;
};

GameRegion pkgi_get_region(const std::string& titleid);
