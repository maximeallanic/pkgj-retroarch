#include "gameview.hpp"

#include <fmt/format.h>

#include "dialog.hpp"
#include "file.hpp"
#include "imgui.hpp"
#include "pkgi.hpp"
extern "C"
{
#include "style.h"
}

#ifndef PKGI_SIMULATOR
#include <vita2d.h>
// vita2d_texture_get_width / _height are declared in vita2d.h
#else
#include <SDL2/SDL.h>
// On Linux/simulator, vita2d_texture is opaque (reinterpreted as SDL_Texture).
// Provide thin wrappers so the rest of gameview.cpp compiles without ifdefs.
static inline float vita2d_texture_get_width(vita2d_texture* t)
{
    int w = 0;
    if (t) SDL_QueryTexture(reinterpret_cast<SDL_Texture*>(t), nullptr, nullptr, &w, nullptr);
    return static_cast<float>(w);
}
static inline float vita2d_texture_get_height(vita2d_texture* t)
{
    int h = 0;
    if (t) SDL_QueryTexture(reinterpret_cast<SDL_Texture*>(t), nullptr, nullptr, nullptr, &h);
    return static_cast<float>(h);
}
#endif

namespace
{
constexpr unsigned GameViewWidth  = VITA_WIDTH  * 0.95;
constexpr unsigned GameViewHeight = VITA_HEIGHT * 0.82;

// Thumbnail panel size presets indexed by config.thumbnail_size
// 0=off, 1=small, 2=medium, 3=large
struct ThumbSize { float w, h; };
constexpr ThumbSize kThumbSizes[] = {
    {  0.f,   0.f}, // 0 off
    {203.f, 203.f}, // 1 small   (square, 90% of previous width)
    {284.f, 284.f}, // 2 medium
    {365.f, 365.f}, // 3 large
};
constexpr int kThumbSizeCount = 4;

const char* presence_label(DbPresence presence)
{
    switch (presence)
    {
    case PresenceUnknown:
        return "Unknown";
    case PresenceIncomplete:
        return "Incomplete";
    case PresenceInstalling:
        return "Installing";
    case PresenceInstalled:
        return "Installed";
    case PresenceMissing:
        return "Missing";
    case PresenceGamePresent:
        return "Base game present";
    }
    return "Unknown";
}

std::string friendly_size(int64_t size)
{
    if (size <= 0)
        return "unknown";
    if (size < 1000LL)
        return fmt::format("{} B", size);
    if (size < 1000LL * 1000)
        return fmt::format("{:.1f} kB", static_cast<double>(size) / 1000.0);
    if (size < 1000LL * 1000 * 1000)
        return fmt::format("{:.1f} MB", static_cast<double>(size) / 1000.0 / 1000.0);
    return fmt::format("{:.2f} GB", static_cast<double>(size) / 1000.0 / 1000.0 / 1000.0);
}

void draw_centered_status_text(
        ImDrawList* dl,
        ImVec2 panel_min,
        float panel_w,
        float panel_h,
        const char* line1,
        const char* line2,
        ImU32 color)
{
    ImVec2 s1 = ImGui::CalcTextSize(line1);
    ImVec2 s2 = line2 ? ImGui::CalcTextSize(line2) : ImVec2(0.f, 0.f);
    const float gap = line2 ? 2.f : 0.f;
    const float total_h = s1.y + (line2 ? gap + s2.y : 0.f);

    dl->AddText(
            ImVec2(panel_min.x + (panel_w - s1.x) * 0.5f,
                   panel_min.y + (panel_h - total_h) * 0.5f),
            color,
            line1);

    if (line2)
    {
        dl->AddText(
                ImVec2(panel_min.x + (panel_w - s2.x) * 0.5f,
                       panel_min.y + (panel_h - total_h) * 0.5f + s1.y + gap),
                color,
                line2);
    }
}
}

GameView::GameView(
        Mode mode,
        const Config* config,
        Downloader* downloader,
        DbItem* item,
        std::optional<CompPackDatabase::Item> base_comppack,
        std::optional<CompPackDatabase::Item> patch_comppack,
        AnnotationDatabase* annotationDb)
    : _mode(mode)
    , _config(config)
    , _downloader(downloader)
    , _item(item)
    , _base_comppack(base_comppack)
    , _patch_comppack(patch_comppack)
    , _image_fetcher(config, item)
    , _annotationDb(annotationDb)
    , _annotation(annotationDb ? annotationDb->get(item->titleid) : UserAnnotation{})
{
    // Populate the text buffer from the saved annotation
    std::strncpy(_comment_buf, _annotation.comment.c_str(),
                 sizeof(_comment_buf) - 1);
    _comment_buf[sizeof(_comment_buf) - 1] = '\0';

    if (is_vita_mode())
        _patch_info_fetcher = std::make_unique<PatchInfoFetcher>(item->titleid);

    refresh();
}

void GameView::render()
{
    ImGui::SetNextWindowPos(
            ImVec2((VITA_WIDTH - GameViewWidth) / 2,
                   (VITA_HEIGHT - GameViewHeight) / 2));
    ImGui::SetNextWindowSize(ImVec2(GameViewWidth, GameViewHeight), 0);

    ImGui::Begin(
            fmt::format("{} ({})###gameview", _item->name, _item->titleid)
                    .c_str(),
            nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoScrollbar |
                    ImGuiWindowFlags_NoScrollWithMouse |
                    ImGuiWindowFlags_NoCollapse |
                    ImGuiWindowFlags_NoSavedSettings);

    {
        const int tsz = std::max(0, std::min(
                _config->thumbnail_size, kThumbSizeCount - 1));
        const float kImagePanelW = kThumbSizes[tsz].w;
        const float kImagePanelH = kThumbSizes[tsz].h;
        auto* thumb_tex = _image_fetcher.get_texture();
        const auto image_status = _image_fetcher.get_status();

        if (kImagePanelW > 0.f)
        {
            const ImGuiStyle& style = ImGui::GetStyle();
            ImVec2 win_pos = ImGui::GetWindowPos();
            const float title_bar_h = ImGui::GetFrameHeight();
            ImVec2 panel_min(
                    win_pos.x + (float)GameViewWidth
                            - style.WindowPadding.x - kImagePanelW,
                    win_pos.y + title_bar_h + style.WindowPadding.y);
            ImVec2 panel_max(
                    panel_min.x + kImagePanelW,
                    panel_min.y + kImagePanelH);

            ImDrawList* dl = ImGui::GetForegroundDrawList();
            dl->AddRectFilled(panel_min, panel_max, IM_COL32(20, 20, 20, 230), 3.f);
            dl->AddRect(panel_min, panel_max, IM_COL32(110, 110, 110, 255), 3.f);

            if (thumb_tex)
            {
                float tw = static_cast<float>(vita2d_texture_get_width(thumb_tex));
                float th = static_cast<float>(vita2d_texture_get_height(thumb_tex));
                const float inner_w = kImagePanelW - 6.f;
                const float inner_h = kImagePanelH - 6.f;
                if (tw > inner_w) { th = th * inner_w / tw; tw = inner_w; }
                if (th > inner_h) { tw = tw * inner_h / th; th = inner_h; }
                // Top-align: centre horizontally, pin to top of inner area
                ImVec2 img_min(
                        panel_min.x + (kImagePanelW - tw) * 0.5f,
                        panel_min.y + 3.f);
                ImVec2 img_max(img_min.x + tw, img_min.y + th);
                dl->AddImage((ImTextureID)thumb_tex, img_min, img_max);
            }
            else
            {
                if (image_status == ImageFetcher::Status::Downloading)
                {
                    draw_centered_status_text(
                            dl,
                            panel_min,
                            kImagePanelW,
                            kImagePanelH,
                            "Downloading",
                            "cover",
                            IM_COL32(180, 190, 220, 220));
                }
                else if (image_status == ImageFetcher::Status::Error)
                {
                    draw_centered_status_text(
                            dl,
                            panel_min,
                            kImagePanelW,
                            kImagePanelH,
                            "Download error",
                            "no image available",
                            IM_COL32(210, 150, 150, 220));
                }
            }
        }
    }

    {
        const int tsz = std::max(0, std::min(
                _config->thumbnail_size, kThumbSizeCount - 1));
        const float panelW = kThumbSizes[tsz].w;
        ImGui::PushTextWrapPos(
                panelW > 0.f
                ? (float)GameViewWidth
                        - ImGui::GetStyle().WindowPadding.x
                        - panelW
                        - ImGui::GetStyle().ItemSpacing.x
                : 0.f);
    }

    if (is_vita_mode())
    {
        ImGui::Text(fmt::format("Firmware version: {}", pkgi_get_system_version()).c_str());
        ImGui::Text(fmt::format("Required firmware version: {}", get_min_system_version()).c_str());
        ImGui::Text(" ");
        ImGui::Text(fmt::format(
                            "Installed game version: {}",
                            _game_version.empty() ? "not installed" : _game_version)
                            .c_str());
        if (_comppack_versions.present && _comppack_versions.base.empty() &&
            _comppack_versions.patch.empty())
        {
            ImGui::Text("Installed compatibility pack: unknown version");
        }
        else
        {
            ImGui::Text(fmt::format(
                                "Installed base compatibility pack: {}",
                                _comppack_versions.base.empty() ? "no" : "yes")
                                .c_str());
            ImGui::Text(fmt::format(
                                "Installed patch compatibility pack version: {}",
                                _comppack_versions.patch.empty() ? "none"
                                                                 : _comppack_versions.patch)
                                .c_str());
        }
        ImGui::Text(" ");
        printDiagnostic();
        ImGui::Text(" ");
    }
    else
    {        
        ImGui::Text(fmt::format("Content ID: {}",
                                _item->content.empty() ? "unknown" : _item->content).c_str());
        ImGui::Text(fmt::format("Package size: {}", friendly_size(_item->size)).c_str());
        ImGui::Text(fmt::format("Last update: {}",
                                _item->date.empty() ? "unknown" : _item->date).c_str());
        ImGui::Text(" ");
        ImGui::Text("Diagnostic:");
        ImGui::Text(fmt::format("- Status: {}", presence_label(_item->presence)).c_str());
        ImGui::Text(fmt::format("- NoPspEmuDrm kernel plugin: {}",
                                _nopspemudrm_present ? "present" : "not detected").c_str());
        ImGui::Text("- Install as ISO: available");
        if (_nopspemudrm_present)
            ImGui::Text("- LiveArea PBP queue: available");
        else
            ImGui::Text("- LiveArea PBP queue: unavailable without plugin");
        ImGui::Text(" ");
    }

    ImGui::PopTextWrapPos();

    if (is_vita_mode() && _patch_info_fetcher &&
        _patch_info_fetcher->get_status() == PatchInfoFetcher::Status::Found)
    {
        if (ImGui::Button("Install game and patch###installgame"))
            start_download_package();
        ImGui::SetItemDefaultFocus();
        if (ImGui::IsItemFocused())
            ImGui::SetScrollY(0.0f);
    }
    else if (is_vita_mode())
    {
        if (ImGui::Button("Install game###installgame"))
            start_download_package();
        ImGui::SetItemDefaultFocus();
        if (ImGui::IsItemFocused())
            ImGui::SetScrollY(0.0f);
    }
    else
    {
        if (ImGui::Button("Install as ISO###installpspiso"))
            start_download_package(PspInstallMode::Iso);
        ImGui::SetItemDefaultFocus();
        if (ImGui::IsItemFocused())
            ImGui::SetScrollY(0.0f);

        if (_nopspemudrm_present)
        {
            if (ImGui::Button("Queue as PBP in LiveArea###installpsppbp"))
                start_download_package(PspInstallMode::LiveAreaPbp);
        }
    }

    if (is_vita_mode() && _base_comppack)
    {
        if (!_downloader->is_in_queue(CompPackBase, _item->titleid))
        {
            if (ImGui::Button("Install base compatibility pack###installbasecomppack"))
                start_download_comppack(false);
        }
        else
        {
            if (ImGui::Button("Cancel base compatibility pack installation###installbasecomppack"))
                cancel_download_comppacks(false);
        }
    }
    if (is_vita_mode() && _patch_comppack)
    {
        if (!_downloader->is_in_queue(CompPackPatch, _item->titleid))
        {
            if (ImGui::Button(fmt::format(
                                      "Install compatibility pack {}###installpatchcommppack",
                                      _patch_comppack->app_version)
                                      .c_str()))
                start_download_comppack(true);
        }
        else
        {
            if (ImGui::Button("Cancel patch compatibility pack installation###installpatchcommppack"))
                cancel_download_comppacks(true);
        }
    }

    if (_annotationDb)
    {
        if (_ime_active && pkgi_dialog_input_update())
        {
            pkgi_dialog_input_get_text(_comment_buf, sizeof(_comment_buf));
            _annotation.comment = _comment_buf;
            _annotationDb->set(_item->titleid, _annotation);
            _item->user_comment = _annotation.comment;
            _ime_active = false;
        }

        ImGui::Separator();
        ImGui::Text("Personal Notes");
        ImGui::Spacing();

        // ── Flag — compact cycling selector  [ < ]  [label]  [ > ] ──────────
        {
            auto cycle = [&](int delta)
            {
                int fi = (static_cast<int>(_annotation.flag) + delta +
                          UserFlagCount) %
                         UserFlagCount;
                _annotation.flag = static_cast<UserFlag>(fi);
                _annotationDb->set(_item->titleid, _annotation);
                _item->user_flag = _annotation.flag;
            };

            ImGui::Text("Flag:");
            ImGui::SameLine();

            if (ImGui::Button("< ##flagprev"))
                cycle(-1);

            ImGui::SameLine();

            // Centre label button — clicking also cycles forward
            const bool active = (_annotation.flag != UserFlag::None);
            if (active)
                ImGui::PushStyleColor(
                        ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
            // Fixed width so the window doesn't shift as text changes
            if (ImGui::Button(
                        fmt::format(
                                "{:<18}###flaglabel",
                                user_flag_label(_annotation.flag))
                                .c_str(),
                        ImVec2(180.f, 0)))
                cycle(+1);
            if (active)
                ImGui::PopStyleColor();

            ImGui::SameLine();
            if (ImGui::Button("> ##flagnext"))
                cycle(+1);
        }

        ImGui::Spacing();
        ImGui::Text("Comment:");
        ImGui::TextWrapped(
                "%s",
                _annotation.comment.empty() ? "(no comment)"
                                            : _annotation.comment.c_str());
        ImGui::Spacing();
        if (ImGui::Button("Edit Comment"))
        {
            pkgi_dialog_input_text("Comment", _comment_buf);
            _ime_active = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear Notes"))
        {
            _annotationDb->remove(_item->titleid);
            _annotation = {};
            _comment_buf[0] = '\0';
            _item->user_flag = UserFlag::None;
            _item->user_comment.clear();
            _ime_active = false;
        }
    }

    ImGui::End();
}

static const auto Red = ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
static const auto Yellow = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
static const auto Green = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);

void GameView::printDiagnostic()
{
    bool ok = true;
    auto const printError = [&](auto const& str)
    {
        ok = false;
        ImGui::TextColored(Red, str);
    };

    auto const systemVersion = pkgi_get_system_version();
    auto const minSystemVersion = get_min_system_version();

    ImGui::Text("Diagnostic:");

    if (systemVersion < minSystemVersion)
    {
        if (!_comppack_versions.present)
        {
            if (_refood_present)
                ImGui::Text("- This game will work thanks to reF00D");
            else if (_0syscall6_present)
                ImGui::Text("- This game will work thanks to 0syscall6");
            else
                printError(
                        "- Your firmware is too old to play this game, you "
                        "must install reF00D or 0syscall6");
        }
    }
    else
    {
        ImGui::Text("- Your firmware is recent enough");
    }

    if (_comppack_versions.present && _comppack_versions.base.empty() &&
        _comppack_versions.patch.empty())
    {
        ImGui::TextColored(
                Yellow,
                "- A compatibility pack is installed but not by PKGj, please "
                "make sure it matches the installed version or reinstall it "
                "with PKGj");
        ok = false;
    }

    if (_comppack_versions.base.empty() && !_comppack_versions.patch.empty())
        printError(
                "- You have installed an update compatibility pack without "
                "installing the base pack, install the base pack first and "
                "reinstall the update compatibility pack.");

    std::string comppack_version;
    if (!_comppack_versions.patch.empty())
        comppack_version = _comppack_versions.patch;
    else if (!_comppack_versions.base.empty())
        comppack_version = _comppack_versions.base;

    if (_item->presence == PresenceInstalled && !comppack_version.empty() &&
        comppack_version < _game_version)
        printError(
                "- The version of the game does not match the installed "
                "compatibility pack. If you have updated the game, also "
                "install the update compatibility pack.");

    if (_item->presence == PresenceInstalled &&
        comppack_version > _game_version)
        printError(
                "- The version of the game does not match the installed "
                "compatibility pack. Downgrade to the base compatibility "
                "pack or update the game through the Live Area.");

    if (_item->presence != PresenceInstalled)
    {
        ImGui::Text("- Game not installed");
        ok = false;
    }

    if (ok)
        ImGui::TextColored(Green, "All green");
}

std::string GameView::get_min_system_version()
{
    if (!_patch_info_fetcher)
        return _item->fw_version;

    auto const patchInfo = _patch_info_fetcher->get_patch_info();
    if (patchInfo)
        return patchInfo->fw_version;
    else
        return _item->fw_version;
}

bool GameView::is_vita_mode() const
{
    return _mode == ModeGames;
}

void GameView::refresh()
{
    LOGF("Refreshing game view");
    if (is_vita_mode())
    {
        _refood_present = pkgi_is_module_present("ref00d");
        _0syscall6_present = pkgi_is_module_present("0syscall6");
        _game_version = pkgi_get_game_version(_item->titleid);
        _comppack_versions = pkgi_get_comppack_versions(_item->titleid);
    }
    else
    {
        _refood_present = false;
        _0syscall6_present = false;
        _nopspemudrm_present = pkgi_is_module_present("NoPspEmuDrm_kern");
        _game_version.clear();
        _comppack_versions = {};
    }
}


void GameView::do_download(PspInstallMode psp_install_mode) {
    pkgi_start_download(*_downloader, *_item, psp_install_mode);
    _item->presence = PresenceUnknown;
}

void GameView::start_download_package(PspInstallMode psp_install_mode)
{
    if (_item->presence == PresenceInstalled)
    {
        LOGF("[{}] {} - already installed", _item->titleid, _item->name);
        pkgi_dialog_question(
        fmt::format(
                "{} is already installed."
                "Would you like to redownload it?",
                _item->name)
                .c_str(),
        {{"Redownload.", [this, psp_install_mode] { this->do_download(psp_install_mode); }},
         {"Dont Redownload.", [] {} }});
        return;
    }
    this->do_download(psp_install_mode);
}

void GameView::cancel_download_package()
{
    _downloader->remove_from_queue(Game, _item->content);
    _item->presence = PresenceUnknown;
}

void GameView::start_download_comppack(bool patch)
{
    const auto& entry = patch ? _patch_comppack : _base_comppack;

    _downloader->add(DownloadItem{
            patch ? CompPackPatch : CompPackBase,
            _item->name,
            _item->titleid,
            _config->comppack_url + entry->path,
            std::vector<uint8_t>{},
            std::vector<uint8_t>{},
            false,
            "ux0:",
            entry->app_version});
}

void GameView::cancel_download_comppacks(bool patch)
{
    _downloader->remove_from_queue(
            patch ? CompPackPatch : CompPackBase, _item->titleid);
}
