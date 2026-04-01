#pragma once

#include "annotationdb.hpp"
#include "comppackdb.hpp"
#include "config.hpp"
#include "db.hpp"
#include "downloader.hpp"
#include "imagefetcher.hpp"
#include "install.hpp"
#include "patchinfofetcher.hpp"

#include <memory>

#include <optional>

class GameView
{
public:
    GameView(
            Mode mode,
            const Config* config,
            Downloader* downloader,
            DbItem* item,
            std::optional<CompPackDatabase::Item> base_comppack,
            std::optional<CompPackDatabase::Item> patch_comppack,
            AnnotationDatabase* annotationDb);

    const DbItem* get_item() const
    {
        return _item;
    }

    void render();
    void refresh();

    bool is_closed() const
    {
        return _closed;
    }

    void close()
    {
        _closed = true;
    }

private:
    Mode _mode;
    const Config* _config;
    Downloader* _downloader;

    DbItem* _item;
    std::optional<CompPackDatabase::Item> _base_comppack;
    std::optional<CompPackDatabase::Item> _patch_comppack;

    bool _refood_present{false};
    bool _0syscall6_present{false};
    bool _nopspemudrm_present{false};
    std::string _game_version;
    CompPackVersion _comppack_versions;

    bool _closed{false};

    std::unique_ptr<PatchInfoFetcher> _patch_info_fetcher;
    ImageFetcher _image_fetcher;

    // --- Annotation state ---
    AnnotationDatabase* _annotationDb;
    UserAnnotation      _annotation;       // working copy
    char                _comment_buf[512]; // buffer for IME result
    bool                _ime_active{false}; // true while virtual keyboard is open
    // ------------------------

    std::string get_min_system_version();
    bool is_vita_mode() const;
    void printDiagnostic();
    void do_download(PspInstallMode psp_install_mode = PspInstallMode::Auto);
    void start_download_package(
            PspInstallMode psp_install_mode = PspInstallMode::Auto);
    void cancel_download_package();
    void start_download_comppack(bool patch);
    void cancel_download_comppacks(bool patch);
};
