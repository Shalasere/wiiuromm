#include <coreinit/screen.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <vpad/input.h>
#include <whb/proc.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <string>
#include <vector>

#include "romm/app_core.hpp"
#include "romm/config.hpp"
#include "romm/control_schema.hpp"
#include "romm/http_client.hpp"
#include "romm/logger.hpp"
#include "romm/runtime_sync.hpp"
#include "romm/ui_theme.hpp"

namespace {

romm::LogicalButton mapVpadButton(uint32_t trigger) {
    if (trigger & VPAD_BUTTON_HOME)
        return romm::LogicalButton::Quit;
    if (trigger & VPAD_BUTTON_PLUS)
        return romm::LogicalButton::Quit;
    if (trigger & VPAD_BUTTON_UP)
        return romm::LogicalButton::Up;
    if (trigger & VPAD_BUTTON_DOWN)
        return romm::LogicalButton::Down;
    if (trigger & VPAD_BUTTON_LEFT)
        return romm::LogicalButton::Left;
    if (trigger & VPAD_BUTTON_RIGHT)
        return romm::LogicalButton::Right;
    if (trigger & VPAD_BUTTON_A)
        return romm::LogicalButton::Confirm;
    if (trigger & VPAD_BUTTON_B)
        return romm::LogicalButton::Back;
    if (trigger & VPAD_BUTTON_Y)
        return romm::LogicalButton::Queue;
    if (trigger & VPAD_BUTTON_X)
        return romm::LogicalButton::StartWork;
    if (trigger & VPAD_BUTTON_MINUS)
        return romm::LogicalButton::Search;
    if (trigger & VPAD_BUTTON_R)
        return romm::LogicalButton::Diagnostics;
    if (trigger & VPAD_BUTTON_L)
        return romm::LogicalButton::Updater;
    return romm::LogicalButton::None;
}

struct HybridTile {
    std::string title;
    std::string subtitle;
    bool selected{false};
};

int selectedIndex(const romm::Status& status, int total) {
    if (total <= 0)
        return 0;
    switch (status.currentView) {
    case romm::Status::View::PLATFORMS:
        return std::clamp(status.selectedPlatformIndex, 0, total - 1);
    case romm::Status::View::ROMS:
    case romm::Status::View::DETAIL:
        return std::clamp(status.selectedRomIndex, 0, total - 1);
    case romm::Status::View::QUEUE:
    case romm::Status::View::DOWNLOADING:
        return std::clamp(status.selectedQueueIndex, 0, total - 1);
    default:
        return 0;
    }
}

void collectHybridTiles(const romm::Status& status, std::vector<HybridTile>& outTiles) {
    outTiles.clear();
    switch (status.currentView) {
    case romm::Status::View::PLATFORMS:
        for (const auto& p : status.platforms)
            outTiles.push_back({p.name, "roms " + std::to_string(p.roms.size()), false});
        break;
    case romm::Status::View::ROMS:
    case romm::Status::View::DETAIL:
        if (!status.platforms.empty()) {
            const int psel = std::clamp(status.selectedPlatformIndex, 0, static_cast<int>(status.platforms.size()) - 1);
            const auto& platform = status.platforms[static_cast<size_t>(psel)];
            for (const auto& r : platform.roms)
                outTiles.push_back({r.title, std::to_string(r.sizeMb) + " MB", false});
        }
        break;
    case romm::Status::View::QUEUE:
    case romm::Status::View::DOWNLOADING:
        for (const auto& q : status.downloadQueue)
            outTiles.push_back({q.rom.title, std::string(romm::queueStateName(q.state)) + " " + std::to_string(q.progressPercent) + "%", false});
        break;
    case romm::Status::View::DIAGNOSTICS:
        outTiles.push_back({"INPUT", std::to_string(status.diagnostics.inputCount), false});
        outTiles.push_back({"ENQUEUE", std::to_string(status.diagnostics.enqueueCount), false});
        outTiles.push_back({"DONE", std::to_string(status.diagnostics.completedCount), false});
        outTiles.push_back({"FAILED", std::to_string(status.diagnostics.failedCount), false});
        outTiles.push_back({"SEARCH", std::to_string(status.diagnostics.searchCount), false});
        break;
    case romm::Status::View::UPDATER:
        outTiles.push_back({"UPDATER", std::string(romm::updaterStateName(status.updaterState)), false});
        outTiles.push_back({"CURRENT", status.currentVersion, false});
        outTiles.push_back({"AVAILABLE", status.availableVersion, false});
        break;
    case romm::Status::View::ERROR:
        outTiles.push_back({"CONNECTION", "ERROR", false});
        outTiles.push_back({"DETAIL", status.lastMessage, false});
        break;
    default:
        break;
    }

    if (outTiles.empty())
        outTiles.push_back({"NO DATA", "WAITING FOR SYNC", false});

    const int sel = selectedIndex(status, static_cast<int>(outTiles.size()));
    outTiles[static_cast<size_t>(sel)].selected = true;
}

uint32_t toRgbx(const romm::UiColorRgba& c) {
    return (static_cast<uint32_t>(c.r) << 24) |
           (static_cast<uint32_t>(c.g) << 16) |
           (static_cast<uint32_t>(c.b) << 8);
}

struct ScreenState {
    void* tvBuffer{nullptr};
    void* drcBuffer{nullptr};
    uint32_t tvSize{0};
    uint32_t drcSize{0};
};

bool initScreens(ScreenState& s) {
    OSScreenInit();
    s.tvSize = OSScreenGetBufferSizeEx(SCREEN_TV);
    s.drcSize = OSScreenGetBufferSizeEx(SCREEN_DRC);
    s.tvBuffer = memalign(0x100, ((s.tvSize + 0xFFu) & ~0xFFu));
    s.drcBuffer = memalign(0x100, ((s.drcSize + 0xFFu) & ~0xFFu));
    if (s.tvBuffer == nullptr || s.drcBuffer == nullptr)
        return false;

    std::memset(s.tvBuffer, 0, s.tvSize);
    std::memset(s.drcBuffer, 0, s.drcSize);
    OSScreenSetBufferEx(SCREEN_TV, s.tvBuffer);
    OSScreenSetBufferEx(SCREEN_DRC, s.drcBuffer);
    OSScreenEnableEx(SCREEN_TV, TRUE);
    OSScreenEnableEx(SCREEN_DRC, TRUE);
    return true;
}

void shutdownScreens(ScreenState& s) {
    OSScreenEnableEx(SCREEN_TV, FALSE);
    OSScreenEnableEx(SCREEN_DRC, FALSE);
    OSScreenShutdown();
    std::free(s.tvBuffer);
    std::free(s.drcBuffer);
    s.tvBuffer = nullptr;
    s.drcBuffer = nullptr;
}

void fillRect(OSScreenID screen, int sw, int sh, int x, int y, int w, int h, uint32_t color) {
    if (w <= 0 || h <= 0)
        return;
    const int x0 = std::max(0, x);
    const int y0 = std::max(0, y);
    const int x1 = std::min(sw, x + w);
    const int y1 = std::min(sh, y + h);
    for (int yy = y0; yy < y1; ++yy) {
        for (int xx = x0; xx < x1; ++xx) {
            OSScreenPutPixelEx(screen, static_cast<uint32_t>(xx), static_cast<uint32_t>(yy), color);
        }
    }
}

void drawFrame(OSScreenID screen, int sw, int sh, int x, int y, int w, int h, int borderPx,
               uint32_t fill, uint32_t border) {
    fillRect(screen, sw, sh, x, y, w, h, border);
    fillRect(screen, sw, sh, x + borderPx, y + borderPx, w - (borderPx * 2), h - (borderPx * 2), fill);
}

void drawSurface(OSScreenID screen, int sw, int sh, const romm::Status& status) {
    const auto& t = romm::kWiiUHybridTheme;
    const uint32_t bg = toRgbx(t.bg);
    const uint32_t shell = toRgbx(t.shell);
    const uint32_t border = toRgbx(t.border);
    const uint32_t tile = toRgbx(t.tile);
    const uint32_t tileSelected = toRgbx(t.selectedTile);
    const uint32_t accent = toRgbx(t.accent);
    const uint32_t muted = toRgbx(t.muted);

    OSScreenClearBufferEx(screen, bg);

    const int margin = std::max(10, std::min(sw, sh) / 30);
    const int topInset = std::max(20, sh / 16);
    const int shellX = margin;
    const int shellY = topInset;
    const int shellW = sw - (margin * 2);
    const int shellH = sh - topInset - margin;
    drawFrame(screen, sw, sh, shellX, shellY, shellW, shellH, 2, shell, border);

    const int headerH = std::max(38, shellH / 7);
    drawFrame(screen, sw, sh, shellX, shellY, shellW, headerH, 1, shell, muted);
    drawFrame(screen, sw, sh, shellX + t.spacing.sm, shellY + t.spacing.sm, headerH - (t.spacing.sm * 2), headerH - (t.spacing.sm * 2), 1, accent, accent);

    const int dotsY = shellY + (headerH / 3);
    const int dotW = std::max(4, sw / 180);
    const int dotGap = dotW + 5;
    const int dotStart = shellX + (shellW / 2) - ((6 * dotGap) / 2);
    std::vector<HybridTile> tiles;
    collectHybridTiles(status, tiles);
    const int total = static_cast<int>(tiles.size());
    const int selected = selectedIndex(status, total);
    const int perPage = 15;
    const int page = selected / perPage;
    for (int i = 0; i < 6; ++i) {
        fillRect(screen, sw, sh, dotStart + (i * dotGap), dotsY, dotW, dotW, i == page ? accent : muted);
    }

    const int footerH = std::max(40, shellH / 8);
    const int gridX = shellX + t.spacing.md;
    const int gridY = shellY + headerH + t.spacing.sm;
    const int gridW = shellW - (t.spacing.md * 2);
    const int gridH = shellH - headerH - footerH - (t.spacing.sm * 2);
    drawFrame(screen, sw, sh, gridX, gridY, gridW, gridH, 1, shell, border);

    const int cols = 5;
    const int rows = 3;
    const int gap = t.spacing.xs;
    const int tileW = std::max(12, (gridW - ((cols + 1) * gap)) / cols);
    const int tileH = std::max(12, (gridH - ((rows + 1) * gap)) / rows);
    const int start = page * perPage;
    const int end = std::min(total, start + perPage);
    const bool pulse = ((status.uiFrameCounter / 10u) % 2u) == 0u;

    for (int slot = 0; slot < perPage; ++slot) {
        const int col = slot % cols;
        const int row = slot / cols;
        const int x = gridX + gap + (col * (tileW + gap));
        const int y = gridY + gap + (row * (tileH + gap));
        const int idx = start + slot;
        const bool hasTile = idx < end;
        const bool sel = hasTile && tiles[static_cast<size_t>(idx)].selected;
        drawFrame(screen, sw, sh, x, y, tileW, tileH, 1, sel ? tileSelected : tile, border);
        fillRect(screen, sw, sh, x + 1, y + 1, tileW - 2, std::max(1, tileH / 10), sel ? accent : muted);
        if (sel && pulse) {
            fillRect(screen, sw, sh, x + tileW - 6, y + 2, 3, 3, accent);
        }
    }

    const int footerY = shellY + shellH - footerH;
    drawFrame(screen, sw, sh, shellX, footerY, shellW, footerH, 1, shell, muted);
    const int iconCount = 6;
    const int iconGap = std::max(10, shellW / 40);
    const int iconSize = std::max(12, footerH / 2);
    const int iconTotalW = (iconCount * iconSize) + ((iconCount - 1) * iconGap);
    int iconX = shellX + (shellW - iconTotalW) / 2;
    const int iconY = footerY + (footerH - iconSize) / 2;
    for (int i = 0; i < iconCount; ++i) {
        drawFrame(screen, sw, sh, iconX, iconY, iconSize, iconSize, 1, shell, i == 4 ? accent : muted);
        iconX += iconSize + iconGap;
    }

    OSScreenPutFontEx(screen, 2, 1, "WiiURomM GX2 Hybrid");
    OSScreenPutFontEx(screen, 2, 3, status.lastMessage.c_str());
    OSScreenFlipBuffersEx(screen);
}

void renderFrame(const romm::Status& status) {
    drawSurface(SCREEN_TV, 1280, 720, status);
    drawSurface(SCREEN_DRC, 854, 480, status);
}

} // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    WHBProcInit();
    ScreenState screens;
    if (!initScreens(screens)) {
        return 1;
    }

    romm::initLogFile();
    romm::loadLogLevelFromEnv();
    romm::logInfo("wiiu app launched", "APP");

    romm::Status status = romm::makeDefaultStatus();
    romm::AppConfig cfg = romm::defaultConfig();
    std::string cfgErr;
    (void)romm::applyEnvOverrides(cfg, cfgErr);
    if (!cfgErr.empty()) {
        romm::logWarn("config env override error: " + cfgErr, "CFG");
    }
    status.currentView = romm::Status::View::PLATFORMS;
    status.lastMessage = "Starting network bootstrap...";
    status.uiBusy = true;

    romm::SocketHttpClient httpClient;
    romm::StartupSyncState startup;
    romm::CatalogRuntimeState runtime;
    bool netReady = false;

    while (WHBProcIsRunning()) {
        status.uiFrameCounter++;
        romm::Action action = romm::Action::None;
        romm::ApplyResult result;

        VPADStatus vpadStatus;
        VPADReadError readError;
        VPADRead(VPAD_CHAN_0, &vpadStatus, 1, &readError);
        if (readError == VPAD_READ_SUCCESS) {
            action = romm::mapButtonToAction(
                romm::ControlProfile::WiiU, mapVpadButton(vpadStatus.trigger));
            if (!startup.finished && action != romm::Action::Quit) {
                action = romm::Action::None;
            }
            if (startup.finished && netReady && action == romm::Action::Select &&
                status.currentView == romm::Status::View::PLATFORMS) {
                std::string indexErr;
                if (!romm::indexSelectedPlatformRoms(status, cfg, httpClient, runtime, indexErr)) {
                    status.lastMessage = "ROM index failed: " + indexErr;
                    romm::logWarn(status.lastMessage, "API");
                    action = romm::Action::None;
                } else {
                    romm::logInfo(status.lastMessage, "API");
                }
            }

            result = romm::applyAction(status, action);
            if (!result.keepRunning) {
                romm::logInfo("quit requested", "APP");
                break;
            }
            if (action != romm::Action::None) {
                romm::logDebug(
                    "frame=" + std::to_string(status.uiFrameCounter) + " action=" + std::string(romm::actionName(action)) + " view=" + std::string(romm::viewName(status.currentView)) + " stateChanged=" + std::string(result.stateChanged ? "1" : "0"),
                    "UI");
            }
        }

        if (!startup.finished) {
            (void)romm::stepStartupSync(status, cfg, httpClient, startup);
            netReady = romm::startupSyncReady(startup);
        }
        status.uiBusy = !startup.finished;

        if (startup.finished && netReady) {
            (void)romm::runRealDownloads(status, cfg, httpClient);
        } else {
            (void)romm::tickDownload(status, 16);
        }

        renderFrame(status);
        OSSleepTicks(OSMillisecondsToTicks(16));
    }

    romm::shutdownLogFile();
    shutdownScreens(screens);
    WHBProcShutdown();
    return 0;
}
