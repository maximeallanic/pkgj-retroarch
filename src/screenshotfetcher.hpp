#pragma once

#include "config.hpp"
#include "db.hpp"
#include "imagefetcher.hpp" // ImageFetchResult
#include "workerpool.hpp"

#ifndef PKGI_SIMULATOR
#include <vita2d.h>
#else
struct vita2d_texture;
#endif

#include <memory>
#include <string>

// Fetches up to MAX_SCREENSHOTS thumbnail screenshots for a game from the
// PlayStation Store chihiro API.  Shares the global WorkerSlot with
// ImageFetcher — downloads are queued sequentially.
//
// All public methods must be called from the MAIN thread.
class ScreenshotFetcher
{
public:
    static constexpr int    MAX_SCREENSHOTS = 4;
    static constexpr size_t MAX_SIZE_BYTES  = 200 * 1024; // 200 KB per image

    enum class Status
    {
        Pending,
        Downloading,
        Ready,
        Error,
    };

    ScreenshotFetcher(const Config* config, const DbItem* item);
    ~ScreenshotFetcher();

    // Drive the download and upload pipeline.  Must be called every frame.
    vita2d_texture* get_texture(int index);
    Status          get_status(int index);

private:
    std::string _paths[MAX_SCREENSHOTS];
    std::string _urls[MAX_SCREENSHOTS];

    bool   _submitted[MAX_SCREENSHOTS]{};
    Status _statuses[MAX_SCREENSHOTS];
    bool   _stopped{false}; // true after first 404 — don't try higher indices

    std::shared_ptr<ImageFetchResult> _results[MAX_SCREENSHOTS];
    vita2d_texture*                   _textures[MAX_SCREENSHOTS]{};
    bool                              _upload_pending[MAX_SCREENSHOTS]{};
    std::string                       _pending_paths[MAX_SCREENSHOTS];

    // Tries to hand screenshot[i] to WorkerSlot::image_worker().
    // Returns immediately if the slot is busy; caller retries next frame.
    void _try_submit(int i);
};
