#include "screenshotfetcher.hpp"

#include "curlhttp.hpp"
#include "file.hpp"
#include "log.hpp"
#include "pkgi.hpp"

#include <chrono>
#include <fmt/format.h>
#include <vector>

#ifndef PKGI_SIMULATOR
#include <vita2d.h>
#else
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
extern SDL_Renderer* g_sdl_renderer;

static vita2d_texture* sim_ss_load_jpeg(const char* path)
{
    SDL_Surface* s = IMG_Load(path);
    if (!s)
        return nullptr;
    SDL_Texture* t = SDL_CreateTextureFromSurface(g_sdl_renderer, s);
    SDL_FreeSurface(s);
    return reinterpret_cast<vita2d_texture*>(t);
}
#define vita2d_load_JPEG_file(p) sim_ss_load_jpeg(p)
#define vita2d_wait_rendering_done() ((void)0)
#define vita2d_free_texture(t) \
    SDL_DestroyTexture(reinterpret_cast<SDL_Texture*>(t))
#endif

namespace
{
static std::string ss_get_country(const DbItem* item)
{
    switch (pkgi_get_region(item->titleid))
    {
    case RegionASA:
        if (item->name.find("CHN") != std::string::npos)
            return "CN";
        if (item->content.size() >= 6 && item->content.substr(0, 6) == "HP0507")
            return "KR";
        return "HK";
    case RegionJPN:
        return "JP";
    case RegionEUR:
        return "GB";
    default:
        return "US";
    }
}

static std::string ss_get_language(const DbItem* item)
{
    switch (pkgi_get_region(item->titleid))
    {
    case RegionASA:
        if (item->content.size() >= 6 && item->content.substr(0, 6) == "HP0507")
            return "ko";
        return "zh";
    case RegionJPN:
        return "ja";
    default:
        return "en";
    }
}
} // namespace

ScreenshotFetcher::ScreenshotFetcher(const Config* config, const DbItem* item)
{
    const auto country  = ss_get_country(item);
    const auto language = ss_get_language(item);

    const std::string folder =
            (config && !config->thumbnail_folder.empty())
            ? config->thumbnail_folder
            : "ux0:pkgj/cover";

    for (int i = 0; i < MAX_SCREENSHOTS; ++i)
    {
        _paths[i]    = fmt::format("{}/{}.ss{}.jpg", folder, item->titleid, i);
        _urls[i]     = fmt::format(
                "https://store.playstation.com/store/api/chihiro/"
                "00_09_000/container/{}/{}/19/{}/image"
                "?w=240&h=136&thumb=true&index={}",
                country,
                language,
                item->content,
                i + 1);
        _statuses[i] = Status::Pending;
    }
}

ScreenshotFetcher::~ScreenshotFetcher()
{
    vita2d_wait_rendering_done();
    for (int i = 0; i < MAX_SCREENSHOTS; ++i)
        if (_textures[i])
            vita2d_free_texture(_textures[i]);
}

void ScreenshotFetcher::_try_submit(int i)
{
    // Fast path: already cached on disc.
    if (pkgi_file_exists(_paths[i].c_str()))
    {
        _pending_paths[i]  = _paths[i];
        _upload_pending[i] = true;
        _submitted[i]      = true;
        return;
    }

    // All captures by VALUE — lambda must not reference 'this'.
    auto        result = std::make_shared<ImageFetchResult>();
    std::string path   = _paths[i];
    std::string url    = _urls[i];

    if (!WorkerSlot::image_worker().try_submit(
                path, // task_id = file path
                [result, path, url]()
                {
                    using namespace std::chrono;
                    const auto t0      = steady_clock::now();
                    const auto timeout = seconds(8);

                    auto done_error = [&]()
                    {
                        result->error = true;
                        result->ready.store(true, std::memory_order_release);
                    };
                    auto done_ok = [&](std::string p)
                    {
                        result->error = false;
                        result->path  = std::move(p);
                        result->ready.store(true, std::memory_order_release);
                    };

                    CurlHttp http;
                    try
                    {
                        http.start(url, 0);
                    }
                    catch (const std::exception& e)
                    {
                        LOGFW("[ScreenshotFetcher] HTTP start failed for {}: {}",
                              path,
                              e.what());
                        done_error();
                        return;
                    }

                    if (http.get_status() == 404)
                    {
                        done_error();
                        return;
                    }

                    std::vector<uint8_t> data;
                    data.reserve(16 * 1024);
                    size_t pos = 0;
                    while (true)
                    {
                        if (steady_clock::now() - t0 > timeout)
                        {
                            done_error();
                            return;
                        }
                        if (pos == data.size())
                            data.resize(pos + 4096);

                        int64_t n = 0;
                        try
                        {
                            n = http.read(
                                    data.data() + pos, data.size() - pos);
                        }
                        catch (...)
                        {
                            done_error();
                            return;
                        }
                        if (n == 0)
                            break;
                        pos += static_cast<size_t>(n);
                        if (pos > ScreenshotFetcher::MAX_SIZE_BYTES)
                        {
                            done_error();
                            return;
                        }
                    }

                    if (pos == 0)
                    {
                        done_error();
                        return;
                    }
                    data.resize(pos);

                    void* f = nullptr;
                    try
                    {
                        const std::string tmp = path + ".tmp";
                        f                     = pkgi_create(tmp);
                        pkgi_write(f, data.data(), data.size());
                        pkgi_close(f);
                        f = nullptr;
                        pkgi_rename(tmp, path);
                        done_ok(path);
                    }
                    catch (const std::exception& e)
                    {
                        if (f)
                            pkgi_close(f);
                        LOGFW("[ScreenshotFetcher] save failed for {}: {}",
                              path,
                              e.what());
                        done_error();
                    }
                }))
    {
        // Slot busy — keep _submitted[i] false and retry next frame.
        return;
    }

    _results[i]   = std::move(result);
    _submitted[i] = true;
    _statuses[i]  = Status::Downloading;
}

ScreenshotFetcher::Status ScreenshotFetcher::get_status(int index)
{
    if (index < 0 || index >= MAX_SCREENSHOTS)
        return Status::Error;

    // If a prior index returned 404, stop trying subsequent ones.
    if (_stopped && !_submitted[index])
    {
        _statuses[index] = Status::Error;
        return Status::Error;
    }

    if (!_submitted[index])
    {
        // Only start screenshot[i] after screenshot[i-1] has resolved,
        // so we load them sequentially and stop cleanly on 404.
        const bool prev_done = (index == 0) ||
                               (_submitted[index - 1] &&
                                _statuses[index - 1] != Status::Pending &&
                                _statuses[index - 1] != Status::Downloading);
        if (prev_done)
            _try_submit(index);
        return _statuses[index];
    }

    // Poll worker result.
    if (_results[index] &&
        _results[index]->ready.load(std::memory_order_acquire))
    {
        if (_results[index]->error || _results[index]->path.empty())
        {
            _statuses[index] = Status::Error;
            _stopped         = true; // don't try higher indices
        }
        else
        {
            _pending_paths[index]  = std::move(_results[index]->path);
            _upload_pending[index] = true;
        }
        _results[index].reset();
    }

    return _statuses[index];
}

vita2d_texture* ScreenshotFetcher::get_texture(int index)
{
    if (index < 0 || index >= MAX_SCREENSHOTS)
        return nullptr;

    get_status(index); // drive the pipeline

    if (!_upload_pending[index])
        return _textures[index];

    _upload_pending[index]  = false;
    const std::string path = std::move(_pending_paths[index]);

    vita2d_texture* tex = vita2d_load_JPEG_file(path.c_str());
    if (!tex)
    {
        LOGFW("[ScreenshotFetcher] vita2d_load_JPEG_file failed for {}, "
              "removing",
              path);
        pkgi_rm(path.c_str());
        _statuses[index] = Status::Error;
        _stopped         = true;
    }
    else
    {
        _statuses[index] = Status::Ready;
    }

    _textures[index] = tex;
    return tex;
}
