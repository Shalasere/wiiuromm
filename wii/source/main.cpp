#include <gccore.h>
#include <network.h>
#include <ogc/lwp.h>
#include <ogc/mutex.h>
#include <ogc/pad.h>
#include <ogc/wiilaunch.h>
#include <wiiuse/wpad.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <malloc.h>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>

#include "FreeTypeGX.h"
#include "filebrowser.h"
#include "font_ttf.h"
#include "gettext.h"
#include "libwiigui/gui.h"
#include "romm/api_client.hpp"
#include "romm/app_core.hpp"
#include "romm/config.hpp"
#include "romm/control_schema.hpp"
#include "romm/http_client.hpp"
#include "romm/logger.hpp"
#include "romm/runtime_sync.hpp"
#include "romm/ui_theme.hpp"
#include "scrollbar_png.h"
#include "sgm_browser_options_entry_bg_png.h"
#include "sgm_browser_options_png.h"
#include "sgm_browser_separator_png.h"
#include "sgm_button_png.h"
#include "sgm_dialogue_box_png.h"
#include "sgm_icon_brows_folder_png.h"
#include "sgm_keyboard_textbox_png.h"
#include "sgm_menu_wbackground_png.h"
#include "menu_button_home_over_png.h"
#include "menu_button_home_png.h"
#include "menu_button_settings_over_png.h"
#include "menu_button_settings_png.h"
#include "menu_button_switch_over_png.h"
#include "menu_button_switch_png.h"
#include "menu_button_wii_over_png.h"
#include "menu_button_wii_png.h"
#include "sgm_scrollbar_arrowdown_png.h"
#include "sgm_scrollbar_arrowup_png.h"
#include "sgm_scrollbar_box_png.h"
#include "sgm_scrollbar_custom_bottom_png.h"
#include "sgm_scrollbar_custom_tile_png.h"
#include "sgm_scrollbar_custom_top_png.h"
#include "sgm_scrollbar_device_bottom_png.h"
#include "sgm_scrollbar_device_tile_png.h"
#include "sgm_scrollbar_device_top_png.h"
#include "sgm_taskbar_arrange_list_png.h"
#include "sgm_taskbar_arrange_list_gray_png.h"
#include "sgm_taskbar_emu_png.h"
#include "sgm_taskbar_emu_gray_png.h"
#include "sgm_taskbar_locked_png.h"
#include "sgm_taskbar_locked_gray_png.h"
#include "sgm_taskbar_sd_png.h"
#include "sgm_taskbar_search_png.h"
#include "sgm_taskbar_search_gray_png.h"
#include "sgm_taskbar_usb_png.h"
#include "sgm_taskbar_wii_png.h"
#include "sgm_taskbar_wii_gray_png.h"
#include "player1_point_png.h"
#include "video.h"

BROWSERINFO browser = {};
BROWSERENTRY* browserList = nullptr;
char rootdir[10] = "sd:/";
int rumbleRequest[4] = {0, 0, 0, 0};

namespace {

romm::LogicalButton mapWpadButton(u32 pressed) {
    if (pressed & WPAD_BUTTON_HOME)
        return romm::LogicalButton::Quit;
    if ((pressed & WPAD_BUTTON_1) && (pressed & WPAD_BUTTON_2))
        return romm::LogicalButton::Diagnostics;
    if (pressed & WPAD_BUTTON_PLUS)
        return romm::LogicalButton::Updater;
    if (pressed & WPAD_BUTTON_UP)
        return romm::LogicalButton::Up;
    if (pressed & WPAD_BUTTON_DOWN)
        return romm::LogicalButton::Down;
    if (pressed & WPAD_BUTTON_LEFT)
        return romm::LogicalButton::Left;
    if (pressed & WPAD_BUTTON_RIGHT)
        return romm::LogicalButton::Right;
    if (pressed & WPAD_BUTTON_A)
        return romm::LogicalButton::Confirm;
    if (pressed & WPAD_BUTTON_B)
        return romm::LogicalButton::Back;
    if (pressed & WPAD_BUTTON_1)
        return romm::LogicalButton::Queue;
    if (pressed & WPAD_BUTTON_2)
        return romm::LogicalButton::StartWork;
    if (pressed & WPAD_BUTTON_MINUS)
        return romm::LogicalButton::Search;
    return romm::LogicalButton::None;
}

romm::LogicalButton mapGcButton(u16 pressed) {
    if (pressed & PAD_BUTTON_START)
        return romm::LogicalButton::Quit;
    if (pressed & PAD_BUTTON_UP)
        return romm::LogicalButton::Up;
    if (pressed & PAD_BUTTON_DOWN)
        return romm::LogicalButton::Down;
    if (pressed & PAD_BUTTON_LEFT)
        return romm::LogicalButton::Left;
    if (pressed & PAD_BUTTON_RIGHT)
        return romm::LogicalButton::Right;
    if (pressed & PAD_BUTTON_A)
        return romm::LogicalButton::Confirm;
    if (pressed & PAD_BUTTON_B)
        return romm::LogicalButton::Back;
    if (pressed & PAD_BUTTON_Y)
        return romm::LogicalButton::Queue;
    if (pressed & PAD_BUTTON_X)
        return romm::LogicalButton::StartWork;
    if (pressed & PAD_TRIGGER_R)
        return romm::LogicalButton::Search;
    if (pressed & PAD_TRIGGER_Z)
        return romm::LogicalButton::Diagnostics;
    if (pressed & PAD_TRIGGER_L)
        return romm::LogicalButton::Updater;
    return romm::LogicalButton::None;
}

u32 resolveWpadPressed(u32 down, u32 held, u32& previousHeld) {
    if (down != 0)
        return down;
    const u32 synthesized = held & ~previousHeld;
    previousHeld = held;
    return synthesized;
}

u16 resolveGcPressed(u16 down, u16 held, u16& previousHeld) {
    if (down != 0)
        return down;
    const u16 synthesized = held & static_cast<u16>(~previousHeld);
    previousHeld = held;
    return synthesized;
}

struct GxState {
    GXRModeObj* rmode{nullptr};
    void* xfb{nullptr};
    void* fifo{nullptr};
    bool initialized{false};
};

struct UiColor {
    u8 r;
    u8 g;
    u8 b;
    u8 a;
};

UiColor toUi(const romm::UiColorRgba& c) {
    return UiColor{c.r, c.g, c.b, c.a};
}

GxState gGx;
bool gFontReady = false;
constexpr bool kUseGxBackdrop = true;
constexpr bool kUseAnsiColors = false;
constexpr int kBottomLeftSmallX = 6;
constexpr int kBottomLeftSmallY = 374;
constexpr int kBottomLeftSmallW = 48;
constexpr int kBottomLeftSmallH = 48;
constexpr int kBottomLeftBigX = 58;
constexpr int kBottomLeftBigY = 368;
constexpr int kBottomLeftBigW = 92;
constexpr int kBottomLeftBigH = 92;
constexpr int kBottomRightBigX = 490;
constexpr int kBottomRightBigY = 368;
constexpr int kBottomRightBigW = 92;
constexpr int kBottomRightBigH = 92;
constexpr int kBottomRightSmallX = 586;
constexpr int kBottomRightSmallY = 374;
constexpr int kBottomRightSmallW = 48;
constexpr int kBottomRightSmallH = 48;
struct TemplateSkin {
    GuiImageData* menuBackground{nullptr};
    GuiImageData* dialogueBox{nullptr};
    GuiImageData* bgOptions{nullptr};
    GuiImageData* bgOptionsEntry{nullptr};
    GuiImageData* separator{nullptr};
    GuiImageData* scrollbar{nullptr};
    GuiImageData* scrollbarArrowUp{nullptr};
    GuiImageData* scrollbarArrowDown{nullptr};
    GuiImageData* scrollbarThumb{nullptr};
    GuiImageData* button{nullptr};
    GuiImageData* keyboardTextBox{nullptr};
    GuiImageData* folder{nullptr};
    GuiImageData* scrollbarCustomTop{nullptr};
    GuiImageData* scrollbarCustomTile{nullptr};
    GuiImageData* scrollbarCustomBottom{nullptr};
    GuiImageData* scrollbarDeviceTop{nullptr};
    GuiImageData* scrollbarDeviceTile{nullptr};
    GuiImageData* scrollbarDeviceBottom{nullptr};
    GuiImageData* taskbarEmu{nullptr};
    GuiImageData* taskbarEmuGray{nullptr};
    GuiImageData* taskbarArrangeList{nullptr};
    GuiImageData* taskbarArrangeListGray{nullptr};
    GuiImageData* taskbarLocked{nullptr};
    GuiImageData* taskbarLockedGray{nullptr};
    GuiImageData* taskbarWii{nullptr};
    GuiImageData* taskbarWiiGray{nullptr};
    GuiImageData* taskbarSearch{nullptr};
    GuiImageData* taskbarSearchGray{nullptr};
    GuiImageData* taskbarSd{nullptr};
    GuiImageData* taskbarUsb{nullptr};
    GuiImageData* menuButtonWii{nullptr};
    GuiImageData* menuButtonWiiOver{nullptr};
    GuiImageData* menuButtonSettings{nullptr};
    GuiImageData* menuButtonSettingsOver{nullptr};
    GuiImageData* menuButtonSwitch{nullptr};
    GuiImageData* menuButtonSwitchOver{nullptr};
    GuiImageData* menuButtonHome{nullptr};
    GuiImageData* menuButtonHomeOver{nullptr};
    GuiImageData* player1Point{nullptr};
    bool initialized{false};
};
TemplateSkin gSkin;
GuiFileBrowser* gPlatformBrowser = nullptr;
std::vector<BROWSERENTRY> gPlatformBrowserRows;
struct PointerState {
    bool valid{false};
    int x{320};
    int y{240};
    float angle{0.0f};
};
PointerState gPointerState;
u32 gLastWpadHeldMask = 0;
s32 gLastWpadProbeErr = -1;
int gActiveWpadChannel = 0;
void gxDrawRect(f32 x, f32 y, f32 w, f32 h, const UiColor& color);

struct AsyncRomIndexRequest {
    int platformIndex{-1};
    std::string platformId;
    std::string platformName;
    bool openRomViewOnComplete{true};
};

struct AsyncRomIndexResult {
    bool ready{false};
    bool success{false};
    std::string platformId;
    std::string platformName;
    std::vector<romm::RomEntry> roms;
    std::string error;
    bool openRomViewOnComplete{true};
};

struct AsyncRomIndexWorker {
    lwp_t thread{LWP_THREAD_NULL};
    mutex_t mutex{LWP_MUTEX_NULL};
    bool threadStarted{false};
    bool stopRequested{false};
    bool busy{false};
    bool jobQueued{false};
    std::string activePlatformId;
    std::string activePlatformName;
    AsyncRomIndexRequest request{};
    AsyncRomIndexResult result{};
    romm::AppConfig cfg{};
};

AsyncRomIndexWorker gAsyncRomIndex;

int findPlatformIndexById(const romm::Status& status, const std::string& platformId) {
    for (size_t i = 0; i < status.platforms.size(); ++i) {
        if (status.platforms[i].id == platformId)
            return static_cast<int>(i);
    }
    return -1;
}

void* asyncRomIndexThreadMain(void*) {
    for (;;) {
        bool stop = false;
        bool hasJob = false;
        AsyncRomIndexRequest request{};
        romm::AppConfig cfg{};

        LWP_MutexLock(gAsyncRomIndex.mutex);
        stop = gAsyncRomIndex.stopRequested;
        if (!stop && gAsyncRomIndex.jobQueued) {
            request = gAsyncRomIndex.request;
            cfg = gAsyncRomIndex.cfg;
            gAsyncRomIndex.jobQueued = false;
            gAsyncRomIndex.busy = true;
            gAsyncRomIndex.activePlatformId = request.platformId;
            gAsyncRomIndex.activePlatformName = request.platformName;
            hasJob = true;
        }
        LWP_MutexUnlock(gAsyncRomIndex.mutex);

        if (stop)
            break;
        if (!hasJob) {
            usleep(4000);
            continue;
        }

        romm::SocketHttpClient client;
        std::vector<romm::RomEntry> roms;
        std::string error;
        bool success = true;
        int page = 1;
        const int pageLimit = 16;
        int guard = 0;
        while (page > 0 && guard < 256) {
            std::vector<romm::RomEntry> items;
            int nextPage = 0;
            if (!romm::fetchRomsPage(cfg, client, request.platformId, page, pageLimit, items, nextPage, error)) {
                success = false;
                break;
            }
            roms.insert(roms.end(), items.begin(), items.end());
            page = nextPage;
            guard++;
            // Keep indexing responsive but avoid long CPU bursts that hitch UI.
            usleep(4000);
        }
        if (success && page > 0 && guard >= 256) {
            success = false;
            error = "rom index pagination guard reached";
        }

        LWP_MutexLock(gAsyncRomIndex.mutex);
        gAsyncRomIndex.busy = false;
        gAsyncRomIndex.activePlatformId.clear();
        gAsyncRomIndex.activePlatformName.clear();
        gAsyncRomIndex.result.ready = true;
        gAsyncRomIndex.result.success = success;
        gAsyncRomIndex.result.platformId = request.platformId;
        gAsyncRomIndex.result.platformName = request.platformName;
        gAsyncRomIndex.result.roms = std::move(roms);
        gAsyncRomIndex.result.error = std::move(error);
        gAsyncRomIndex.result.openRomViewOnComplete = request.openRomViewOnComplete;
        LWP_MutexUnlock(gAsyncRomIndex.mutex);
    }
    return nullptr;
}

bool initAsyncRomIndexWorker(const romm::AppConfig& cfg) {
    if (gAsyncRomIndex.threadStarted) {
        LWP_MutexLock(gAsyncRomIndex.mutex);
        gAsyncRomIndex.cfg = cfg;
        LWP_MutexUnlock(gAsyncRomIndex.mutex);
        return true;
    }

    gAsyncRomIndex = AsyncRomIndexWorker{};
    gAsyncRomIndex.cfg = cfg;
    if (LWP_MutexInit(&gAsyncRomIndex.mutex, false) < 0) {
        gAsyncRomIndex = AsyncRomIndexWorker{};
        return false;
    }
    // Regex-heavy parsing and socket I/O need more headroom than 64 KiB on Wii.
    if (LWP_CreateThread(&gAsyncRomIndex.thread, asyncRomIndexThreadMain, nullptr, nullptr, 256 * 1024, 96) < 0) {
        LWP_MutexDestroy(gAsyncRomIndex.mutex);
        gAsyncRomIndex = AsyncRomIndexWorker{};
        return false;
    }
    gAsyncRomIndex.threadStarted = true;
    return true;
}

void shutdownAsyncRomIndexWorker() {
    if (!gAsyncRomIndex.threadStarted)
        return;

    LWP_MutexLock(gAsyncRomIndex.mutex);
    gAsyncRomIndex.stopRequested = true;
    LWP_MutexUnlock(gAsyncRomIndex.mutex);
    LWP_JoinThread(gAsyncRomIndex.thread, nullptr);
    LWP_MutexDestroy(gAsyncRomIndex.mutex);
    gAsyncRomIndex = AsyncRomIndexWorker{};
}

bool queueSelectedPlatformRomIndexAsync(romm::Status& status, romm::CatalogRuntimeState& runtime,
                                        bool openRomViewOnComplete, std::string& outError) {
    outError.clear();
    if (status.platforms.empty()) {
        outError = "no platforms indexed";
        return false;
    }
    int idx = status.selectedPlatformIndex;
    if (idx < 0)
        idx = 0;
    if (idx >= static_cast<int>(status.platforms.size()))
        idx = static_cast<int>(status.platforms.size()) - 1;
    auto& platform = status.platforms[static_cast<size_t>(idx)];

    if (runtime.indexedPlatformIds.count(platform.id)) {
        if (openRomViewOnComplete)
            status.currentView = romm::Status::View::ROMS;
        status.selectedRomIndex = 0;
        status.lastMessage = "Index cached: " + platform.name + " (" + std::to_string(platform.roms.size()) + " ROMs)";
        return true;
    }

    LWP_MutexLock(gAsyncRomIndex.mutex);
    const bool busy = gAsyncRomIndex.busy;
    const bool queued = gAsyncRomIndex.jobQueued;
    const std::string activeName = gAsyncRomIndex.activePlatformName;
    const std::string activeId = gAsyncRomIndex.activePlatformId;
    const std::string queuedId = gAsyncRomIndex.request.platformId;

    if (busy && activeId == platform.id) {
        status.lastMessage = "Indexing ROMs: " + platform.name + " (" + std::to_string(platform.roms.size()) + ")";
        LWP_MutexUnlock(gAsyncRomIndex.mutex);
        return true;
    }

    if (queued && queuedId == platform.id) {
        status.lastMessage = "Index queued: " + platform.name;
        LWP_MutexUnlock(gAsyncRomIndex.mutex);
        return true;
    }

    gAsyncRomIndex.jobQueued = true;
    gAsyncRomIndex.request.platformIndex = idx;
    gAsyncRomIndex.request.platformId = platform.id;
    gAsyncRomIndex.request.platformName = platform.name;
    gAsyncRomIndex.request.openRomViewOnComplete = openRomViewOnComplete;
    gAsyncRomIndex.result.ready = false;
    gAsyncRomIndex.result.roms.clear();
    LWP_MutexUnlock(gAsyncRomIndex.mutex);

    status.selectedRomIndex = 0;
    if (busy && !activeName.empty()) {
        status.lastMessage = "Index queued: " + platform.name + " (after " + activeName + ")";
    } else {
        status.lastMessage = "Indexing ROMs: " + platform.name + " (queued)";
    }
    return true;
}

bool pollPlatformRomIndexAsyncResult(romm::Status& status, romm::CatalogRuntimeState& runtime) {
    AsyncRomIndexResult result{};
    LWP_MutexLock(gAsyncRomIndex.mutex);
    if (!gAsyncRomIndex.result.ready) {
        LWP_MutexUnlock(gAsyncRomIndex.mutex);
        return false;
    }
    result = std::move(gAsyncRomIndex.result);
    gAsyncRomIndex.result = AsyncRomIndexResult{};
    LWP_MutexUnlock(gAsyncRomIndex.mutex);

    if (!result.success) {
        status.lastMessage = "ROM index failed: " + result.error;
        return true;
    }

    const int idx = findPlatformIndexById(status, result.platformId);
    if (idx < 0) {
        status.lastMessage = "ROM index dropped: platform removed";
        return true;
    }

    auto& platform = status.platforms[static_cast<size_t>(idx)];
    platform.roms = std::move(result.roms);
    runtime.indexedPlatformIds.insert(platform.id);
    status.selectedRomIndex = 0;

    bool selectionStillOnPlatform = false;
    if (status.selectedPlatformIndex >= 0 &&
        status.selectedPlatformIndex < static_cast<int>(status.platforms.size())) {
        const auto& selected = status.platforms[static_cast<size_t>(status.selectedPlatformIndex)];
        selectionStillOnPlatform = (selected.id == platform.id);
    }
    if (result.openRomViewOnComplete && selectionStillOnPlatform) {
        status.selectedPlatformIndex = idx;
        status.currentView = romm::Status::View::ROMS;
    }
    status.lastMessage = "Index complete: " + platform.name + " (" + std::to_string(platform.roms.size()) + " ROMs)";
    return true;
}

bool isPlatformRomIndexBusyAsync() {
    bool busy = false;
    LWP_MutexLock(gAsyncRomIndex.mutex);
    busy = gAsyncRomIndex.busy || gAsyncRomIndex.jobQueued;
    LWP_MutexUnlock(gAsyncRomIndex.mutex);
    return busy;
}

void copyBrowserText(char* dst, size_t dstSize, const std::string& value) {
    if (dst == nullptr || dstSize == 0)
        return;
    const size_t n = std::min(dstSize - 1, value.size());
    std::memcpy(dst, value.data(), n);
    dst[n] = '\0';
}

std::string toLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string platformDisplayName(const romm::PlatformEntry& platform) {
    const std::string lowerName = toLowerAscii(platform.name);
    const std::string lowerSlug = toLowerAscii(platform.slug);

    // Check SNES first because its full name also contains
    // "nintendo entertainment system" as a substring.
    if (lowerName.find("super nintendo entertainment system") != std::string::npos ||
        lowerName == "snes" || lowerSlug == "snes")
        return "SNES";
    if (lowerName.find("nintendo entertainment system") != std::string::npos ||
        lowerName == "nes" || lowerSlug == "nes")
        return "NES";
    return platform.name;
}

void syncPlatformBrowser(const romm::Status& status, int selectedPlatform) {
    gPlatformBrowserRows.clear();
    gPlatformBrowserRows.reserve(std::max<int>(1, static_cast<int>(status.platforms.size())));

    if (status.platforms.empty()) {
        BROWSERENTRY entry{};
        entry.isdir = 1;
        copyBrowserText(entry.filename, sizeof(entry.filename), "no-platforms");
        copyBrowserText(entry.displayname, sizeof(entry.displayname), "No platforms indexed");
        gPlatformBrowserRows.push_back(entry);
    } else {
        for (const auto& platform : status.platforms) {
            BROWSERENTRY entry{};
            entry.isdir = 1;
            const std::string displayName = platformDisplayName(platform);
            copyBrowserText(entry.filename, sizeof(entry.filename), displayName);
            std::string label = displayName + " (" + std::to_string(platform.roms.size()) + ")";
            copyBrowserText(entry.displayname, sizeof(entry.displayname), label);
            gPlatformBrowserRows.push_back(entry);
        }
    }

    browserList = gPlatformBrowserRows.data();
    browser.numEntries = static_cast<int>(gPlatformBrowserRows.size());
    if (browser.numEntries <= 0) {
        browser.selIndex = 0;
    } else if (selectedPlatform < 0) {
        browser.selIndex = 0;
    } else if (selectedPlatform >= browser.numEntries) {
        browser.selIndex = browser.numEntries - 1;
    } else {
        browser.selIndex = selectedPlatform;
    }
    if (browser.numEntries <= FILE_PAGESIZE) {
        browser.pageIndex = 0;
    } else {
        int page = browser.selIndex - (FILE_PAGESIZE / 2);
        const int maxPage = browser.numEntries - FILE_PAGESIZE;
        if (page < 0)
            page = 0;
        if (page > maxPage)
            page = maxPage;
        browser.pageIndex = page;
    }
}

void shutdownTemplateSkin() {
    delete gPlatformBrowser;
    gPlatformBrowser = nullptr;
    gPlatformBrowserRows.clear();
    browserList = nullptr;
    browser = BROWSERINFO{};

    delete gSkin.menuBackground;
    delete gSkin.dialogueBox;
    delete gSkin.bgOptions;
    delete gSkin.bgOptionsEntry;
    delete gSkin.separator;
    delete gSkin.scrollbar;
    delete gSkin.scrollbarArrowUp;
    delete gSkin.scrollbarArrowDown;
    delete gSkin.scrollbarThumb;
    delete gSkin.button;
    delete gSkin.keyboardTextBox;
    delete gSkin.folder;
    delete gSkin.scrollbarCustomTop;
    delete gSkin.scrollbarCustomTile;
    delete gSkin.scrollbarCustomBottom;
    delete gSkin.scrollbarDeviceTop;
    delete gSkin.scrollbarDeviceTile;
    delete gSkin.scrollbarDeviceBottom;
    delete gSkin.taskbarEmu;
    delete gSkin.taskbarEmuGray;
    delete gSkin.taskbarArrangeList;
    delete gSkin.taskbarArrangeListGray;
    delete gSkin.taskbarLocked;
    delete gSkin.taskbarLockedGray;
    delete gSkin.taskbarWii;
    delete gSkin.taskbarWiiGray;
    delete gSkin.taskbarSearch;
    delete gSkin.taskbarSearchGray;
    delete gSkin.taskbarSd;
    delete gSkin.taskbarUsb;
    delete gSkin.menuButtonWii;
    delete gSkin.menuButtonWiiOver;
    delete gSkin.menuButtonSettings;
    delete gSkin.menuButtonSettingsOver;
    delete gSkin.menuButtonSwitch;
    delete gSkin.menuButtonSwitchOver;
    delete gSkin.menuButtonHome;
    delete gSkin.menuButtonHomeOver;
    delete gSkin.player1Point;
    gSkin = TemplateSkin{};
}

bool initTemplateSkin() {
    auto imageReady = [](GuiImageData* img) -> bool {
        return img != nullptr && img->GetImage() != nullptr &&
               img->GetWidth() > 0 && img->GetHeight() > 0;
    };

    if (gSkin.initialized) {
        const bool stillValid = imageReady(gSkin.menuBackground) &&
                                imageReady(gSkin.dialogueBox) &&
                                imageReady(gSkin.bgOptionsEntry) &&
                                imageReady(gSkin.scrollbarArrowUp) &&
                                imageReady(gSkin.scrollbarArrowDown) &&
                                imageReady(gSkin.scrollbarThumb) &&
                                imageReady(gSkin.keyboardTextBox);
        if (stillValid)
            return true;
        shutdownTemplateSkin();
    }

    gSkin.menuBackground = new GuiImageData(sgm_menu_wbackground_png);
    gSkin.dialogueBox = new GuiImageData(sgm_dialogue_box_png);
    gSkin.bgOptions = new GuiImageData(sgm_browser_options_png);
    gSkin.bgOptionsEntry = new GuiImageData(sgm_browser_options_entry_bg_png);
    gSkin.separator = new GuiImageData(sgm_browser_separator_png);
    gSkin.scrollbar = new GuiImageData(scrollbar_png);
    gSkin.scrollbarArrowUp = new GuiImageData(sgm_scrollbar_arrowup_png);
    gSkin.scrollbarArrowDown = new GuiImageData(sgm_scrollbar_arrowdown_png);
    gSkin.scrollbarThumb = new GuiImageData(sgm_scrollbar_box_png);
    gSkin.button = new GuiImageData(sgm_button_png);
    gSkin.keyboardTextBox = new GuiImageData(sgm_keyboard_textbox_png);
    gSkin.folder = new GuiImageData(sgm_icon_brows_folder_png);
    gSkin.scrollbarCustomTop = new GuiImageData(sgm_scrollbar_custom_top_png);
    gSkin.scrollbarCustomTile = new GuiImageData(sgm_scrollbar_custom_tile_png);
    gSkin.scrollbarCustomBottom = new GuiImageData(sgm_scrollbar_custom_bottom_png);
    gSkin.scrollbarDeviceTop = new GuiImageData(sgm_scrollbar_device_top_png);
    gSkin.scrollbarDeviceTile = new GuiImageData(sgm_scrollbar_device_tile_png);
    gSkin.scrollbarDeviceBottom = new GuiImageData(sgm_scrollbar_device_bottom_png);
    gSkin.taskbarEmu = new GuiImageData(sgm_taskbar_emu_png);
    gSkin.taskbarEmuGray = new GuiImageData(sgm_taskbar_emu_gray_png);
    gSkin.taskbarArrangeList = new GuiImageData(sgm_taskbar_arrange_list_png);
    gSkin.taskbarArrangeListGray = new GuiImageData(sgm_taskbar_arrange_list_gray_png);
    gSkin.taskbarLocked = new GuiImageData(sgm_taskbar_locked_png);
    gSkin.taskbarLockedGray = new GuiImageData(sgm_taskbar_locked_gray_png);
    gSkin.taskbarWii = new GuiImageData(sgm_taskbar_wii_png);
    gSkin.taskbarWiiGray = new GuiImageData(sgm_taskbar_wii_gray_png);
    gSkin.taskbarSearch = new GuiImageData(sgm_taskbar_search_png);
    gSkin.taskbarSearchGray = new GuiImageData(sgm_taskbar_search_gray_png);
    gSkin.taskbarSd = new GuiImageData(sgm_taskbar_sd_png);
    gSkin.taskbarUsb = new GuiImageData(sgm_taskbar_usb_png);
    gSkin.menuButtonWii = new GuiImageData(menu_button_wii_png);
    gSkin.menuButtonWiiOver = new GuiImageData(menu_button_wii_over_png);
    gSkin.menuButtonSettings = new GuiImageData(menu_button_settings_png);
    gSkin.menuButtonSettingsOver = new GuiImageData(menu_button_settings_over_png);
    gSkin.menuButtonSwitch = new GuiImageData(menu_button_switch_png);
    gSkin.menuButtonSwitchOver = new GuiImageData(menu_button_switch_over_png);
    gSkin.menuButtonHome = new GuiImageData(menu_button_home_png);
    gSkin.menuButtonHomeOver = new GuiImageData(menu_button_home_over_png);
    gSkin.player1Point = new GuiImageData(player1_point_png);

    const bool ok = imageReady(gSkin.menuBackground) && imageReady(gSkin.dialogueBox) && imageReady(gSkin.bgOptions) &&
                    imageReady(gSkin.bgOptionsEntry) && imageReady(gSkin.separator) && imageReady(gSkin.scrollbar) &&
                    imageReady(gSkin.scrollbarArrowUp) && imageReady(gSkin.scrollbarArrowDown) && imageReady(gSkin.scrollbarThumb) &&
                    imageReady(gSkin.button) && imageReady(gSkin.keyboardTextBox) && imageReady(gSkin.folder) &&
                    imageReady(gSkin.scrollbarCustomTop) && imageReady(gSkin.scrollbarCustomTile) &&
                    imageReady(gSkin.scrollbarCustomBottom) && imageReady(gSkin.scrollbarDeviceTop) &&
                    imageReady(gSkin.scrollbarDeviceTile) && imageReady(gSkin.scrollbarDeviceBottom) &&
                    imageReady(gSkin.taskbarEmu) && imageReady(gSkin.taskbarEmuGray) &&
                    imageReady(gSkin.taskbarArrangeList) && imageReady(gSkin.taskbarArrangeListGray) &&
                    imageReady(gSkin.taskbarLocked) && imageReady(gSkin.taskbarLockedGray) &&
                    imageReady(gSkin.taskbarWii) && imageReady(gSkin.taskbarWiiGray) &&
                    imageReady(gSkin.taskbarSearch) && imageReady(gSkin.taskbarSearchGray) &&
                    imageReady(gSkin.taskbarSd) && imageReady(gSkin.taskbarUsb) &&
                    imageReady(gSkin.menuButtonWii) && imageReady(gSkin.menuButtonWiiOver) &&
                    imageReady(gSkin.menuButtonSettings) && imageReady(gSkin.menuButtonSettingsOver) &&
                    imageReady(gSkin.menuButtonSwitch) && imageReady(gSkin.menuButtonSwitchOver) &&
                    imageReady(gSkin.menuButtonHome) && imageReady(gSkin.menuButtonHomeOver) &&
                    imageReady(gSkin.player1Point);
    if (!ok) {
        shutdownTemplateSkin();
        return false;
    }
    gSkin.initialized = true;
    return true;
}

int clampIndexCount(int idx, int count) {
    if (count <= 0)
        return 0;
    if (idx < 0)
        return 0;
    if (idx >= count)
        return count - 1;
    return idx;
}

int computeListStartIndex(int selected, int totalCount, int pageSize) {
    if (totalCount <= pageSize)
        return 0;
    int start = selected - (pageSize - 1);
    if (start < 0)
        start = 0;
    const int maxStart = totalCount - pageSize;
    if (start > maxStart)
        start = maxStart;
    return start;
}

bool pointInRect(int px, int py, int x, int y, int w, int h) {
    return px >= x && py >= y && px < (x + w) && py < (y + h);
}

bool pointInEllipse(int px, int py, int x, int y, int w, int h) {
    if (w <= 0 || h <= 0)
        return false;
    const double rx = static_cast<double>(w) * 0.5;
    const double ry = static_cast<double>(h) * 0.5;
    const double cx = static_cast<double>(x) + rx;
    const double cy = static_cast<double>(y) + ry;
    const double dx = (static_cast<double>(px) - cx) / rx;
    const double dy = (static_cast<double>(py) - cy) / ry;
    return (dx * dx + dy * dy) <= 1.0;
}

bool updatePointerState(int wpadChannel) {
    const PointerState previous = gPointerState;
    gPointerState.valid = false;

    WPADData* data = WPAD_Data(wpadChannel);
    if (data != nullptr && data->ir.valid) {
        const int maxX = std::max(1, static_cast<int>(screenwidth)) - 1;
        const int maxY = std::max(1, static_cast<int>(screenheight)) - 1;
        int x = static_cast<int>(std::lround(data->ir.x));
        int y = static_cast<int>(std::lround(data->ir.y));
        if (x < 0)
            x = 0;
        if (x > maxX)
            x = maxX;
        if (y < 0)
            y = 0;
        if (y > maxY)
            y = maxY;
        gPointerState.valid = true;
        gPointerState.x = x;
        gPointerState.y = y;
        gPointerState.angle = data->ir.angle;
    }

    if (gPointerState.valid != previous.valid)
        return true;
    if (!gPointerState.valid)
        return false;
    const int dx = std::abs(gPointerState.x - previous.x);
    const int dy = std::abs(gPointerState.y - previous.y);
    const float da = std::fabs(gPointerState.angle - previous.angle);
    return dx >= 3 || dy >= 3 || da >= 2.0f;
}

int resolveActiveWpadChannel() {
    u32 type = 0;
    s32 err = WPAD_Probe(0, &type);
    if (err == WPAD_ERR_NONE) {
        gLastWpadProbeErr = err;
        return 0;
    }

    for (int chan = 1; chan < 4; ++chan) {
        err = WPAD_Probe(chan, &type);
        if (err == WPAD_ERR_NONE) {
            gLastWpadProbeErr = err;
            return chan;
        }
    }

    gLastWpadProbeErr = err;
    return 0;
}

void drawPointerOverlay() {
    if (!gPointerState.valid)
        return;

    if (gSkin.player1Point != nullptr && gSkin.player1Point->GetImage() != nullptr) {
        Menu_DrawImg(
            static_cast<f32>(gPointerState.x - 48),
            static_cast<f32>(gPointerState.y - 48),
            96.0f,
            96.0f,
            gSkin.player1Point->GetImage(),
            gPointerState.angle,
            1.0f,
            1.0f,
            255);
        return;
    }

    // Fallback marker if pointer image failed to load.
    gxDrawRect(static_cast<f32>(gPointerState.x - 6), static_cast<f32>(gPointerState.y - 1), 13.0f, 3.0f, UiColor{255, 255, 255, 220});
    gxDrawRect(static_cast<f32>(gPointerState.x - 1), static_cast<f32>(gPointerState.y - 6), 3.0f, 13.0f, UiColor{255, 255, 255, 220});
}

struct PointerActionResult {
    romm::Action action{romm::Action::None};
    bool selectionChanged{false};
    bool consumedA{false};
    bool requestUiReset{false};
    bool requestReturnToMenu{false};
};

PointerActionResult applyPointerNavigation(romm::Status& status, bool aPressed, bool allowActions) {
    PointerActionResult result;
    if (!gPointerState.valid)
        return result;

    const int px = gPointerState.x;
    const int py = gPointerState.y;
    const int platformCount = static_cast<int>(status.platforms.size());
    const int leftPage = 4;
    const int leftPaneX = 34;
    const int leftPaneY = 62;
    const int leftPaneW = 238;
    const int leftRowH = 56;
    const int leftVisibleCount = std::max(1, platformCount);
    int leftSelected = clampIndexCount(status.selectedPlatformIndex, leftVisibleCount);
    int leftStart = computeListStartIndex(leftSelected, leftVisibleCount, leftPage);
    const bool platformPaneSelectable = status.currentView == romm::Status::View::PLATFORMS;

    if (platformPaneSelectable &&
        pointInRect(px, py, leftPaneX, leftPaneY, leftPaneW - 26, leftPage * leftRowH)) {
        const int row = (py - leftPaneY) / leftRowH;
        const int idx = leftStart + row;
        if (idx >= 0 && idx < platformCount && status.selectedPlatformIndex != idx) {
            status.selectedPlatformIndex = idx;
            status.selectedRomIndex = 0;
            result.selectionChanged = true;
        }
        if (aPressed) {
            result.consumedA = true;
            if (allowActions && status.currentView == romm::Status::View::PLATFORMS)
                result.action = romm::Action::Select;
        }
    }

    const int leftBarX = leftPaneX + leftPaneW - 22;
    const int leftBarY = leftPaneY + 2;
    const int leftBarH = (leftPage * leftRowH) - 4;
    if (platformPaneSelectable && pointInRect(px, py, leftBarX, leftBarY, 20, 20) && aPressed) {
        result.consumedA = true;
        if (platformCount > 0) {
            const int next = clampIndexCount(status.selectedPlatformIndex - 1, platformCount);
            if (next != status.selectedPlatformIndex) {
                status.selectedPlatformIndex = next;
                status.selectedRomIndex = 0;
                result.selectionChanged = true;
            }
        }
    } else if (platformPaneSelectable &&
               pointInRect(px, py, leftBarX, leftBarY + leftBarH - 20, 20, 20) &&
               aPressed) {
        result.consumedA = true;
        if (platformCount > 0) {
            const int next = clampIndexCount(status.selectedPlatformIndex + 1, platformCount);
            if (next != status.selectedPlatformIndex) {
                status.selectedPlatformIndex = next;
                status.selectedRomIndex = 0;
                result.selectionChanged = true;
            }
        }
    }

    const int platformIndex = clampIndexCount(status.selectedPlatformIndex, platformCount);
    const romm::PlatformEntry* platform = platformCount > 0 ? &status.platforms[static_cast<size_t>(platformIndex)] : nullptr;

    const bool rightShowsQueue = status.currentView == romm::Status::View::QUEUE ||
                                 status.currentView == romm::Status::View::DOWNLOADING;
    const bool rightHasPlaceholder = !rightShowsQueue &&
                                     (platform == nullptr || platform->roms.empty());

    int rightCount = 0;
    int rightSelected = 0;
    if (rightShowsQueue) {
        rightCount = status.downloadQueue.empty() ? 1 : static_cast<int>(status.downloadQueue.size());
        if (!status.downloadQueue.empty())
            rightSelected = clampIndexCount(status.selectedQueueIndex, rightCount);
    } else if (rightHasPlaceholder) {
        rightCount = 1;
    } else if (platform != nullptr) {
        rightCount = static_cast<int>(platform->roms.size());
        rightSelected = clampIndexCount(status.selectedRomIndex, rightCount);
    }

    const int rightPage = 7;
    const int rightPaneX = 304;
    const int rightPaneY = 64;
    const int rightPaneW = 288;
    const int rightRowH = 32;
    int rightStart = computeListStartIndex(rightSelected, rightCount, rightPage);

    if (pointInRect(px, py, rightPaneX, rightPaneY, rightPaneW - 28, rightPage * rightRowH)) {
        const int row = (py - rightPaneY) / rightRowH;
        const int idx = rightStart + row;
        if (idx >= 0 && idx < rightCount) {
            if (rightShowsQueue && !status.downloadQueue.empty()) {
                if (status.selectedQueueIndex != idx) {
                    status.selectedQueueIndex = idx;
                    result.selectionChanged = true;
                }
            } else if (!rightHasPlaceholder && platform != nullptr) {
                if (status.selectedRomIndex != idx) {
                    status.selectedRomIndex = idx;
                    result.selectionChanged = true;
                }
            }
        }
        if (aPressed) {
            result.consumedA = true;
            if (allowActions && status.currentView == romm::Status::View::ROMS &&
                !rightShowsQueue && !rightHasPlaceholder && platform != nullptr) {
                result.action = romm::Action::Select;
            } else if (allowActions && rightShowsQueue && !status.downloadQueue.empty()) {
                result.action = romm::Action::Select;
            }
        }
    }

    const int rightBarX = rightPaneX + rightPaneW - 22;
    const int rightBarY = rightPaneY + 2;
    const int rightBarH = (rightPage * rightRowH) - 4;
    if (pointInRect(px, py, rightBarX, rightBarY, 20, 20) && aPressed) {
        result.consumedA = true;
        if (rightShowsQueue && !status.downloadQueue.empty()) {
            const int next = clampIndexCount(status.selectedQueueIndex - 1,
                                             static_cast<int>(status.downloadQueue.size()));
            if (next != status.selectedQueueIndex) {
                status.selectedQueueIndex = next;
                result.selectionChanged = true;
            }
        } else if (!rightHasPlaceholder && platform != nullptr && !platform->roms.empty()) {
            const int next = clampIndexCount(status.selectedRomIndex - 1,
                                             static_cast<int>(platform->roms.size()));
            if (next != status.selectedRomIndex) {
                status.selectedRomIndex = next;
                result.selectionChanged = true;
            }
        }
    } else if (pointInRect(px, py, rightBarX, rightBarY + rightBarH - 20, 20, 20) && aPressed) {
        result.consumedA = true;
        if (rightShowsQueue && !status.downloadQueue.empty()) {
            const int next = clampIndexCount(status.selectedQueueIndex + 1,
                                             static_cast<int>(status.downloadQueue.size()));
            if (next != status.selectedQueueIndex) {
                status.selectedQueueIndex = next;
                result.selectionChanged = true;
            }
        } else if (!rightHasPlaceholder && platform != nullptr && !platform->roms.empty()) {
            const int next = clampIndexCount(status.selectedRomIndex + 1,
                                             static_cast<int>(platform->roms.size()));
            if (next != status.selectedRomIndex) {
                status.selectedRomIndex = next;
                result.selectionChanged = true;
            }
        }
    }

    // Bottom circular cutouts and icon strip.
    if (allowActions && aPressed) {
        if (pointInEllipse(px, py, kBottomLeftSmallX, kBottomLeftSmallY, kBottomLeftSmallW, kBottomLeftSmallH)) {
            result.consumedA = true;
            result.requestUiReset = true;
        } else if (pointInEllipse(px, py, kBottomLeftBigX, kBottomLeftBigY, kBottomLeftBigW, kBottomLeftBigH)) {
            result.consumedA = true;
            result.requestReturnToMenu = true;
        } else if (pointInEllipse(px, py, kBottomRightSmallX, kBottomRightSmallY, kBottomRightSmallW, kBottomRightSmallH)) {
            result.consumedA = true;
            result.action = romm::Action::Quit;
        } else if (pointInEllipse(px, py, kBottomRightBigX, kBottomRightBigY, kBottomRightBigW, kBottomRightBigH)) {
            // Settings gear is intentionally unbound for now.
            result.consumedA = true;
        } else if (pointInRect(px, py, 238, 442, 40, 32)) {
            result.consumedA = true;
            result.action = romm::Action::OpenQueue;
        } else if (pointInRect(px, py, 278, 442, 40, 32)) {
            result.consumedA = true;
            result.action = romm::Action::Right;
        } else if (pointInRect(px, py, 318, 442, 40, 32)) {
            result.consumedA = true;
            result.action = romm::Action::OpenSearch;
        } else if (pointInRect(px, py, 358, 442, 40, 32)) {
            result.consumedA = true;
            result.action = romm::Action::OpenQueue;
        }
    }

    return result;
}

GXColor toGX(const UiColor& c) {
    GXColor out = {c.r, c.g, c.b, c.a};
    return out;
}

void gxDrawRect(f32 x, f32 y, f32 w, f32 h, const UiColor& color) {
    Menu_DrawRectangle(x, y, w, h, toGX(color), 1);
}

struct Glyph5x7 {
    u8 rows[7];
};

const Glyph5x7 kGlyphTable[37] = {
    {{0, 0, 0, 0, 0, 0, 0}}, // space
    {{0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}}, // 0
    {{0x04, 0x0C, 0x14, 0x04, 0x04, 0x04, 0x1F}}, // 1
    {{0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}}, // 2
    {{0x0E, 0x11, 0x01, 0x06, 0x01, 0x11, 0x0E}}, // 3
    {{0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}}, // 4
    {{0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}}, // 5
    {{0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}}, // 6
    {{0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}}, // 7
    {{0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}}, // 8
    {{0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}}, // 9
    {{0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}}, // A
    {{0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}}, // B
    {{0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}}, // C
    {{0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C}}, // D
    {{0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}}, // E
    {{0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}}, // F
    {{0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E}}, // G
    {{0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}}, // H
    {{0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}}, // I
    {{0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C}}, // J
    {{0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}}, // K
    {{0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}}, // L
    {{0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}}, // M
    {{0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}}, // N
    {{0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}}, // O
    {{0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}}, // P
    {{0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}}, // Q
    {{0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}}, // R
    {{0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}}, // S
    {{0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}}, // T
    {{0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}}, // U
    {{0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}}, // V
    {{0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11}}, // W
    {{0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}}, // X
    {{0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}}, // Y
    {{0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}} // Z
};

const Glyph5x7 kDotGlyph = {{0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}};
const Glyph5x7 kColonGlyph = {{0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}};
const Glyph5x7 kDashGlyph = {{0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}};
const Glyph5x7 kSlashGlyph = {{0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x00}};
const Glyph5x7 kPipeGlyph = {{0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}};
const Glyph5x7 kLBrGlyph = {{0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E}};
const Glyph5x7 kRBrGlyph = {{0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E}};
const Glyph5x7 kLParGlyph = {{0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}};
const Glyph5x7 kRParGlyph = {{0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}};
const Glyph5x7 kEqGlyph = {{0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00}};
const Glyph5x7 kPctGlyph = {{0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03}};
const Glyph5x7 kBangGlyph = {{0x04, 0x04, 0x04, 0x04, 0x00, 0x00, 0x04}};
const Glyph5x7 kQGlyph = {{0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}};
const Glyph5x7 kQuoteGlyph = {{0x0A, 0x0A, 0x04, 0x00, 0x00, 0x00, 0x00}};
const Glyph5x7 kPlusGlyph = {{0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}};

const Glyph5x7& glyphFor(char c) {
    if (c == ' ')
        return kGlyphTable[0];
    if (c >= '0' && c <= '9')
        return kGlyphTable[1 + (c - '0')];
    if (c >= 'A' && c <= 'Z')
        return kGlyphTable[11 + (c - 'A')];
    if (c >= 'a' && c <= 'z')
        return kGlyphTable[11 + (c - 'a')];
    switch (c) {
    case '.':
        return kDotGlyph;
    case ':':
        return kColonGlyph;
    case '-':
    case '_':
        return kDashGlyph;
    case '/':
        return kSlashGlyph;
    case '|':
        return kPipeGlyph;
    case '[':
        return kLBrGlyph;
    case ']':
        return kRBrGlyph;
    case '(':
        return kLParGlyph;
    case ')':
        return kRParGlyph;
    case '=':
        return kEqGlyph;
    case '%':
        return kPctGlyph;
    case '!':
        return kBangGlyph;
    case '?':
        return kQGlyph;
    case '"':
    case '\'':
        return kQuoteGlyph;
    case '+':
        return kPlusGlyph;
    default:
        return kGlyphTable[0];
    }
}

void gxDrawGlyph(f32 x, f32 y, f32 scale, const UiColor& color, char c) {
    const Glyph5x7& g = glyphFor(c);
    for (int row = 0; row < 7; ++row) {
        const u8 bits = g.rows[row];
        for (int col = 0; col < 5; ++col) {
            if ((bits & (1u << (4 - col))) == 0)
                continue;
            gxDrawRect(x + static_cast<f32>(col) * scale,
                       y + static_cast<f32>(row) * scale,
                       scale, scale, color);
        }
    }
}

void gxDrawTextLineBitmap(f32 x, f32 y, f32 scale, const UiColor& color,
                          const std::string& text, int maxChars) {
    const int count = std::min<int>(static_cast<int>(text.size()), maxChars);
    const f32 advance = (5.0f * scale) + scale;
    for (int i = 0; i < count; ++i) {
        char c = text[static_cast<size_t>(i)];
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        gxDrawGlyph(x + static_cast<f32>(i) * advance, y, scale, color, c);
    }
}

void gxDrawTextLine(f32 x, f32 y, f32 scale, const UiColor& color,
                    const std::string& text, int maxChars) {
    if (!gFontReady) {
        gxDrawTextLineBitmap(x, y, scale, color, text, maxChars);
        return;
    }

    const int count = std::min<int>(static_cast<int>(text.size()), maxChars);
    if (count <= 0)
        return;

    const std::string clipped = text.substr(0, static_cast<size_t>(count));
    const int pointSize = std::max(10, static_cast<int>(std::lround(scale * 8.0f)));

    GuiText line(clipped.c_str(), pointSize, toGX(color));
    line.SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
    line.SetPosition(static_cast<int>(std::lround(x)),
                     static_cast<int>(std::lround(y)));
    line.Draw();
}

void initVideoConsole() {
    InitVideo();
    gGx.initialized = true;
}

struct ShopTile {
    std::string title;
    std::string subtitle;
};

std::string toUpperAscii(const std::string& in) {
    std::string out = in;
    for (char& c : out)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

char uiSpinnerGlyph(uint32_t frame) {
    static const char kSpin[] = {'|', '/', '-', '\\'};
    return kSpin[(frame / 6u) % 4u];
}

void drawPanel(f32 x, f32 y, f32 w, f32 h, const UiColor& fill, const UiColor& border) {
    gxDrawRect(x, y, w, h, border);
    gxDrawRect(x + 2.0f, y + 2.0f, w - 4.0f, h - 4.0f, fill);
}

[[maybe_unused]] void drawDottedRule(f32 x, f32 y, f32 w, const UiColor& color) {
    for (f32 cx = x; cx < x + w; cx += 8.0f)
        gxDrawRect(cx, y, 3.0f, 2.0f, color);
}

[[maybe_unused]] void drawCard(f32 x, f32 y, f32 w, f32 h, const ShopTile& tile, bool selected) {
    const auto& theme = romm::kWiiShopTheme;
    const UiColor border = selected ? UiColor{22, 176, 236, 255} : toUi(theme.border);
    const UiColor fill = selected ? UiColor{212, 243, 255, 255} : toUi(theme.primarySoft);
    const UiColor icon = toUi(theme.primary);
    const UiColor text = toUi(theme.text);
    const UiColor sub = toUi(theme.subtext);

    drawPanel(x, y, w, h, fill, border);
    drawPanel(x + 10.0f, y + 10.0f, 58.0f, h - 20.0f, icon, UiColor{230, 250, 255, 255});
    gxDrawTextLine(x + 22.0f, y + 26.0f, 2.4f, UiColor{255, 255, 255, 255}, "#", 1);

    gxDrawTextLine(x + 80.0f, y + 14.0f, 2.3f, text, toUpperAscii(tile.title), 18);
    gxDrawRect(x + 80.0f, y + 46.0f, w - 96.0f, 2.0f, UiColor{126, 210, 245, 255});
    gxDrawTextLine(x + 80.0f, y + h - 26.0f, 1.8f, sub, toUpperAscii(tile.subtitle), 24);
}

[[maybe_unused]] void collectShopTiles(const romm::Status& status, std::vector<ShopTile>& outTiles, std::string& outTitle) {
    outTiles.clear();
    outTitle = "WII ROMM";

    switch (status.currentView) {
    case romm::Status::View::PLATFORMS:
        outTitle = "Search by Platform";
        if (status.platforms.empty()) {
            outTiles.push_back({"No Platforms", "Check API Connection"});
            return;
        }
        for (const auto& p : status.platforms) {
            outTiles.push_back({p.name, "Titles: " + std::to_string(p.roms.size())});
        }
        return;
    case romm::Status::View::ROMS: {
        outTitle = "Browse ROMs";
        if (status.platforms.empty()) {
            outTiles.push_back({"No Platform Selected", "Press B to return"});
            return;
        }
        const int psel = clampIndexCount(status.selectedPlatformIndex, static_cast<int>(status.platforms.size()));
        const romm::PlatformEntry& platform = status.platforms[static_cast<size_t>(psel)];
        if (platform.roms.empty()) {
            outTiles.push_back({platform.name, "No ROMs Indexed Yet"});
            return;
        }
        for (const auto& rom : platform.roms) {
            outTiles.push_back({rom.title, "Size: " + std::to_string(rom.sizeMb) + " MB"});
        }
        return;
    }
    case romm::Status::View::DETAIL: {
        outTitle = "ROM Detail";
        if (status.platforms.empty()) {
            outTiles.push_back({"No ROM Selected", "Press B to return"});
            return;
        }
        const int psel = clampIndexCount(status.selectedPlatformIndex, static_cast<int>(status.platforms.size()));
        const romm::PlatformEntry& platform = status.platforms[static_cast<size_t>(psel)];
        if (platform.roms.empty()) {
            outTiles.push_back({"No ROM Selected", "Press B to return"});
            return;
        }
        const int rsel = clampIndexCount(status.selectedRomIndex, static_cast<int>(platform.roms.size()));
        const romm::RomEntry& rom = platform.roms[static_cast<size_t>(rsel)];
        outTiles.push_back({"Title", rom.title});
        outTiles.push_back({"ID", rom.id});
        outTiles.push_back({"Publisher", rom.subtitle.empty() ? std::string("<unknown>") : rom.subtitle});
        outTiles.push_back({"Download", rom.downloadUrl.empty() ? std::string("<missing>") : rom.downloadUrl});
        return;
    }
    case romm::Status::View::QUEUE:
    case romm::Status::View::DOWNLOADING:
        outTitle = "Download Queue";
        if (status.downloadQueue.empty()) {
            outTiles.push_back({"Queue Empty", "Press 2 or X to start work"});
            return;
        }
        for (const auto& q : status.downloadQueue) {
            outTiles.push_back({q.rom.title, std::string(romm::queueStateName(q.state)) + " " + std::to_string(q.progressPercent) + "%"});
        }
        return;
    case romm::Status::View::DIAGNOSTICS:
        outTitle = "Diagnostics";
        outTiles.push_back({"Input", std::to_string(status.diagnostics.inputCount)});
        outTiles.push_back({"Enqueue", std::to_string(status.diagnostics.enqueueCount)});
        outTiles.push_back({"Completed", std::to_string(status.diagnostics.completedCount)});
        outTiles.push_back({"Failed", std::to_string(status.diagnostics.failedCount)});
        outTiles.push_back({"Searches", std::to_string(status.diagnostics.searchCount)});
        outTiles.push_back({"Frame", std::to_string(status.uiFrameCounter)});
        return;
    case romm::Status::View::UPDATER:
        outTitle = "Updater";
        outTiles.push_back({"State", std::string(romm::updaterStateName(status.updaterState))});
        outTiles.push_back({"Current", status.currentVersion});
        outTiles.push_back({"Available", status.availableVersion});
        return;
    case romm::Status::View::ERROR:
        outTitle = "Error";
        outTiles.push_back({"Connection Error", status.lastMessage});
        outTiles.push_back({"Hint", "Press B to return to platforms"});
        return;
    default:
        break;
    }
}

[[maybe_unused]] int selectedTileIndex(const romm::Status& status, int itemCount) {
    if (itemCount <= 0)
        return 0;
    if (status.currentView == romm::Status::View::PLATFORMS)
        return clampIndexCount(status.selectedPlatformIndex, itemCount);
    if (status.currentView == romm::Status::View::ROMS)
        return clampIndexCount(status.selectedRomIndex, itemCount);
    if (status.currentView == romm::Status::View::QUEUE || status.currentView == romm::Status::View::DOWNLOADING)
        return clampIndexCount(status.selectedQueueIndex, itemCount);
    return 0;
}

void drawGxBackdrop(const romm::Status& status) {
    if (!gGx.initialized || screenwidth <= 0 || screenheight <= 0)
        return;
    const GXColor clearColor = {236, 238, 242, 255};
    GX_SetCopyClear(clearColor, 0x00ffffff);

    if (!initTemplateSkin()) {
        gxDrawRect(0.0f, 0.0f, static_cast<f32>(screenwidth), static_cast<f32>(screenheight),
                   UiColor{18, 22, 30, 255});
        gxDrawTextLine(24.0f, 24.0f, 2.0f, UiColor{255, 255, 255, 255},
                       "libwiigui skin asset init failed", 40);
        GX_Flush();
        return;
    }

    const int width = screenwidth;
    const int height = screenheight;
    GuiWindow scene(width, height);
    std::vector<std::unique_ptr<GuiImage>> images;
    std::vector<std::unique_ptr<GuiText>> texts;
    images.reserve(128);
    texts.reserve(128);

    auto clipTextLocal = [](const std::string& in, size_t maxLen) -> std::string {
        if (in.size() <= maxLen)
            return in;
        if (maxLen <= 3)
            return in.substr(0, maxLen);
        return in.substr(0, maxLen - 3) + "...";
    };
    auto addImage = [&](GuiImageData* data, int x, int y, float sx, float sy, int alpha = 255) -> GuiImage* {
        auto img = std::make_unique<GuiImage>(data);
        img->SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
        img->SetPosition(x, y);
        img->SetScaleX(sx);
        img->SetScaleY(sy);
        img->SetAlpha(alpha);
        scene.Append(img.get());
        images.push_back(std::move(img));
        return images.back().get();
    };
    auto addText = [&](const std::string& value, int size, GXColor color, int x, int y) -> GuiText* {
        auto textObj = std::make_unique<GuiText>(value.c_str(), size, color);
        textObj->SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
        textObj->SetPosition(x, y);
        scene.Append(textObj.get());
        texts.push_back(std::move(textObj));
        return texts.back().get();
    };
    auto scaleToW = [](GuiImageData* d, float w) -> float {
        return (d == nullptr || d->GetWidth() <= 0) ? 1.0f : (w / static_cast<float>(d->GetWidth()));
    };
    auto scaleToH = [](GuiImageData* d, float h) -> float {
        return (d == nullptr || d->GetHeight() <= 0) ? 1.0f : (h / static_cast<float>(d->GetHeight()));
    };
    auto drawTemplateScrollbar = [&](bool deviceStyle, int x, int y, int h, int totalCount, int selectedIndex) {
        GuiImageData* trackTop = deviceStyle ? gSkin.scrollbarDeviceTop : gSkin.scrollbarCustomTop;
        GuiImageData* trackTile = deviceStyle ? gSkin.scrollbarDeviceTile : gSkin.scrollbarCustomTile;
        GuiImageData* trackBottom = deviceStyle ? gSkin.scrollbarDeviceBottom : gSkin.scrollbarCustomBottom;
        const int positionY = deviceStyle ? 10 : 12;
        const int trackX = x + (deviceStyle ? 2 : 0);
        const int arrowH = 20;
        const int topH = 40;
        const int bottomH = 40;
        const int tileH = 4;
        const int thumbH = 28;

        addImage(gSkin.scrollbarArrowUp, x, y,
                 scaleToW(gSkin.scrollbarArrowUp, 20.0f), scaleToH(gSkin.scrollbarArrowUp, 20.0f), 232);
        addImage(gSkin.scrollbarArrowDown, x, y + h - arrowH,
                 scaleToW(gSkin.scrollbarArrowDown, 20.0f), scaleToH(gSkin.scrollbarArrowDown, 20.0f), 232);

        int trackY = y + positionY;
        int tiles = (h - (positionY * 2) - topH - bottomH) / tileH;
        if (tiles < 1)
            tiles = 1;
        addImage(trackTop, trackX, trackY, scaleToW(trackTop, 20.0f), scaleToH(trackTop, 40.0f), 230);
        trackY += topH;
        addImage(trackTile, trackX, trackY, scaleToW(trackTile, 20.0f),
                 scaleToH(trackTile, static_cast<float>(tiles * tileH)), 230);
        trackY += tiles * tileH;
        addImage(trackBottom, trackX, trackY, scaleToW(trackBottom, 20.0f), scaleToH(trackBottom, 40.0f), 230);

        const int total = std::max(1, totalCount);
        const int selected = clampIndexCount(selectedIndex, total);
        const int minY = y + 23;
        const int maxY = y + h - 23 - thumbH;
        const float ratio = (total <= 1) ? 0.0f :
                            static_cast<float>(selected) / static_cast<float>(total - 1);
        int thumbY = minY + static_cast<int>(std::round((maxY - minY) * ratio));
        if (thumbY < minY)
            thumbY = minY;
        if (thumbY > maxY)
            thumbY = maxY;
        addImage(gSkin.scrollbarThumb, x, thumbY,
                 scaleToW(gSkin.scrollbarThumb, 20.0f), scaleToH(gSkin.scrollbarThumb, 28.0f), 236);
    };

    const int platformCount = static_cast<int>(status.platforms.size());
    const int selectedPlatform = clampIndexCount(status.selectedPlatformIndex, platformCount);
    const romm::PlatformEntry* platform = (platformCount > 0)
                                              ? &status.platforms[static_cast<size_t>(selectedPlatform)]
                                              : nullptr;
    const int romCount = platform ? static_cast<int>(platform->roms.size()) : 0;
    const int selectedRom = clampIndexCount(status.selectedRomIndex, romCount);
    const romm::RomEntry* rom = (platform && romCount > 0)
                                    ? &platform->roms[static_cast<size_t>(selectedRom)]
                                    : nullptr;

    const GXColor cTitle = {24, 42, 58, 255};
    const GXColor cBody = {18, 32, 45, 255};
    const GXColor cAccent = {57, 177, 228, 255};
    const GXColor cClock = {138, 138, 138, 240};

    // Base clear to avoid black showing through transparent skin regions.
    gxDrawRect(0.0f, 0.0f, static_cast<f32>(width), static_cast<f32>(height),
               UiColor{236, 238, 242, 255});

    // Wii-style shell and panes using template images.
    addImage(gSkin.menuBackground, 0, 0, scaleToW(gSkin.menuBackground, static_cast<float>(width)),
             scaleToH(gSkin.menuBackground, static_cast<float>(height)), 255);
    addImage(gSkin.dialogueBox, 28, 28, scaleToW(gSkin.dialogueBox, 256.0f), scaleToH(gSkin.dialogueBox, 274.0f), 212);
    addImage(gSkin.dialogueBox, 292, 28, scaleToW(gSkin.dialogueBox, 324.0f), scaleToH(gSkin.dialogueBox, 274.0f), 212);
    addImage(gSkin.separator, 286, 42, scaleToW(gSkin.separator, 4.0f), scaleToH(gSkin.separator, 248.0f), 170);

    const int leftPaneX = 34;
    const int leftPaneY = 62;
    const int leftPaneW = 238;
    const int leftBarX = leftPaneX + leftPaneW - 22;
    const int leftBarY = leftPaneY + 2;
    const int leftBarH = (4 * 56) - 4;

    addText("Platforms", 19, cTitle, 36, 36);
    const std::string platformTitle = platform ? platformDisplayName(*platform) : std::string("ROMs");
    addText(clipTextLocal(platformTitle, 23), 19, cTitle, 304, 36);
    if (status.uiBusy) {
        const std::string throbber = std::string("Indexing ") + uiSpinnerGlyph(status.uiFrameCounter);
        addText(throbber, 15, cAccent, 486, 38);
    }

    syncPlatformBrowser(status, selectedPlatform);

    const int leftCount = browser.numEntries;
    const int leftSelected = clampIndexCount(browser.selIndex, leftCount);
    const int leftPage = 4;
    int leftStart = computeListStartIndex(leftSelected, leftCount, leftPage);

    for (int i = 0; i < leftPage; ++i) {
        const int idx = leftStart + i;
        if (idx >= leftCount)
            break;
        const int y = leftPaneY + i * 56;
        if (idx == leftSelected && leftCount > 1) {
            addImage(gSkin.bgOptionsEntry, leftPaneX + 6, y + 7,
                     scaleToW(gSkin.bgOptionsEntry, static_cast<float>(leftPaneW - 30)),
                     scaleToH(gSkin.bgOptionsEntry, 40.0f), 144);
        } else {
            addImage(gSkin.bgOptionsEntry, leftPaneX + 6, y + 7,
                     scaleToW(gSkin.bgOptionsEntry, static_cast<float>(leftPaneW - 30)),
                     scaleToH(gSkin.bgOptionsEntry, 40.0f), 92);
        }
        addImage(gSkin.folder, leftPaneX + 12, y + 7,
                 scaleToW(gSkin.folder, 46.0f), scaleToH(gSkin.folder, 34.0f), 208);
        const std::string rawLabel = gPlatformBrowserRows[static_cast<size_t>(idx)].filename;
        const int maxLabelPixels = leftPaneW - 92;
        int rowFont = 18;
        while (rowFont > 11 &&
               static_cast<int>(std::lround(static_cast<float>(rawLabel.size()) * static_cast<float>(rowFont) * 0.46f)) > maxLabelPixels) {
            --rowFont;
        }
        const int pxPerChar = std::max(1, static_cast<int>(std::lround(static_cast<float>(rowFont) * 0.46f)));
        const size_t charCap = static_cast<size_t>(std::max(8, (maxLabelPixels / pxPerChar) + 1));
        const std::string rowLabel = clipTextLocal(rawLabel, charCap);
        addText(rowLabel, rowFont, idx == leftSelected ? cAccent : cBody, leftPaneX + 62, y + 15);
    }
    drawTemplateScrollbar(true, leftBarX, leftBarY, leftBarH, leftCount, leftSelected);

    const bool rightShowsQueue = status.currentView == romm::Status::View::QUEUE ||
                                 status.currentView == romm::Status::View::DOWNLOADING;
    const bool rightHasPlaceholder = !rightShowsQueue &&
                                     (platform == nullptr || platform->roms.empty());
    int rightCount = 0;
    int rightSelected = 0;
    if (rightShowsQueue) {
        rightCount = status.downloadQueue.empty() ? 1 : static_cast<int>(status.downloadQueue.size());
        if (!status.downloadQueue.empty()) {
            rightSelected = clampIndexCount(status.selectedQueueIndex, rightCount);
        }
    } else if (rightHasPlaceholder) {
        rightCount = 1;
    } else if (platform != nullptr) {
        rightCount = static_cast<int>(platform->roms.size());
        rightSelected = clampIndexCount(status.selectedRomIndex, rightCount);
    }

    auto rightRowText = [&](int idx) -> std::string {
        if (rightShowsQueue) {
            if (status.downloadQueue.empty()) {
                return "Queue empty.";
            }
            const auto& item = status.downloadQueue[static_cast<size_t>(idx)];
            return std::string(romm::queueStateName(item.state)) + "  " +
                   item.rom.title + "  " +
                   std::to_string(item.progressPercent) + "%";
        }
        if (rightHasPlaceholder) {
            if (status.uiBusy)
                return "Indexing ROMs...";
            return platform == nullptr
                       ? "Select a platform from the left panel."
                       : "No ROMs indexed for this platform.";
        }
        return platform->roms[static_cast<size_t>(idx)].title;
    };

    const int rightPaneX = 304;
    const int rightPaneY = 64;
    const int rightPaneW = 288;
    const int rightBarX = rightPaneX + rightPaneW - 22;
    const int rightBarY = rightPaneY + 2;
    const int rightBarH = (7 * 32) - 4;
    const int rightPage = 7;
    int rightStart = computeListStartIndex(rightSelected, rightCount, rightPage);

    for (int i = 0; i < rightPage; ++i) {
        const int idx = rightStart + i;
        if (idx >= rightCount)
            break;
        const int y = rightPaneY + i * 32;
        if (idx == rightSelected && rightCount > 1) {
            addImage(gSkin.bgOptionsEntry, rightPaneX + 8, y - 1,
                     scaleToW(gSkin.bgOptionsEntry, static_cast<float>(rightPaneW - 50)),
                     scaleToH(gSkin.bgOptionsEntry, 25.0f), 112);
        }
        const int rowFont = 16;
        const int pxPerChar = std::max(1, static_cast<int>(std::lround(static_cast<float>(rowFont) * 0.46f)));
        const int maxTextPixels = rightPaneW - 72;
        const size_t rowCap = static_cast<size_t>(std::max(8, (maxTextPixels / pxPerChar) + 1));
        addText("o", 13, cAccent, rightPaneX + 12, y + 7);
        addText(clipTextLocal(rightRowText(idx), rowCap), rowFont,
                idx == rightSelected ? cAccent : cBody, rightPaneX + 34, y + 2);
    }

    drawTemplateScrollbar(false, rightBarX, rightBarY, rightBarH, rightCount, rightSelected);

    // Bottom dock: SGM-style clock/title and centered taskbar strip.
    addImage(gSkin.keyboardTextBox, 194, 392,
             scaleToW(gSkin.keyboardTextBox, 252.0f), scaleToH(gSkin.keyboardTextBox, 34.0f), 240);

    unsigned int hour = 0;
    unsigned int minute = 0;
    const std::time_t now = std::time(nullptr);
    if (now > 0) {
        const std::tm* localNow = std::localtime(&now);
        if (localNow != nullptr) {
            hour = static_cast<unsigned int>(localNow->tm_hour);
            minute = static_cast<unsigned int>(localNow->tm_min);
        }
    }
    char clockBuf[6];
    std::snprintf(clockBuf, sizeof(clockBuf), "%02u:%02u", hour, minute);
    addText(clockBuf, 40, cClock, 275, 340);

    const std::string focusTitle = rom ? rom->title : (platform ? platformDisplayName(*platform) : std::string("No selection"));
    addText(clipTextLocal(focusTitle, 31), 19, cBody, 210, 400);

    const int taskbarX = 238;
    const int taskbarY = 442;
    const bool onPlatformView = status.currentView == romm::Status::View::PLATFORMS;
    const bool onQueue = status.currentView == romm::Status::View::QUEUE ||
                         status.currentView == romm::Status::View::DOWNLOADING;
    GuiImageData* deviceTaskIcon = onPlatformView ? gSkin.taskbarWii : gSkin.taskbarEmu;
    GuiImageData* modeTaskIcon = gSkin.taskbarArrangeList;
    GuiImageData* searchTaskIcon = (status.currentView == romm::Status::View::ROMS)
                                       ? gSkin.taskbarSearch
                                       : gSkin.taskbarSearchGray;
    GuiImageData* lockTaskIcon = onQueue ? gSkin.taskbarLocked : gSkin.taskbarLockedGray;
    addImage(deviceTaskIcon, taskbarX, taskbarY,
             scaleToW(deviceTaskIcon, 40.0f), scaleToH(deviceTaskIcon, 32.0f), 232);
    addImage(modeTaskIcon, taskbarX + 40, taskbarY,
             scaleToW(modeTaskIcon, 40.0f), scaleToH(modeTaskIcon, 32.0f), 232);
    addImage(searchTaskIcon, taskbarX + 80, taskbarY,
             scaleToW(searchTaskIcon, 40.0f), scaleToH(searchTaskIcon, 32.0f), 232);
    addImage(lockTaskIcon, taskbarX + 120, taskbarY,
             scaleToW(lockTaskIcon, 40.0f), scaleToH(lockTaskIcon, 32.0f), 232);

    // Left/right circular dock buttons with hover variants.
    const bool pointerValid = gPointerState.valid;
    const bool hoverBackSmall = pointerValid &&
                                pointInEllipse(gPointerState.x, gPointerState.y,
                                               kBottomLeftSmallX, kBottomLeftSmallY,
                                               kBottomLeftSmallW, kBottomLeftSmallH);
    const bool hoverBackLarge = pointerValid &&
                                pointInEllipse(gPointerState.x, gPointerState.y,
                                               kBottomLeftBigX, kBottomLeftBigY,
                                               kBottomLeftBigW, kBottomLeftBigH);
    const bool hoverQuitSmall = pointerValid &&
                                pointInEllipse(gPointerState.x, gPointerState.y,
                                               kBottomRightSmallX, kBottomRightSmallY,
                                               kBottomRightSmallW, kBottomRightSmallH);
    const bool hoverQuitLarge = pointerValid &&
                                pointInEllipse(gPointerState.x, gPointerState.y,
                                               kBottomRightBigX, kBottomRightBigY,
                                               kBottomRightBigW, kBottomRightBigH);

    GuiImageData* leftSmallButton = hoverBackSmall ? gSkin.menuButtonSwitchOver : gSkin.menuButtonSwitch;
    GuiImageData* leftBigButton = hoverBackLarge ? gSkin.menuButtonWiiOver : gSkin.menuButtonWii;
    GuiImageData* rightSmallButton = hoverQuitSmall ? gSkin.menuButtonSwitchOver : gSkin.menuButtonSwitch;
    GuiImageData* rightBigButton = hoverQuitLarge ? gSkin.menuButtonSettingsOver : gSkin.menuButtonSettings;

    addImage(leftSmallButton, kBottomLeftSmallX, kBottomLeftSmallY,
             scaleToW(leftSmallButton, static_cast<float>(kBottomLeftSmallW)),
             scaleToH(leftSmallButton, static_cast<float>(kBottomLeftSmallH)), 232);
    addImage(leftBigButton, kBottomLeftBigX, kBottomLeftBigY,
             scaleToW(leftBigButton, static_cast<float>(kBottomLeftBigW)),
             scaleToH(leftBigButton, static_cast<float>(kBottomLeftBigH)), 236);
    addImage(rightSmallButton, kBottomRightSmallX, kBottomRightSmallY,
             scaleToW(rightSmallButton, static_cast<float>(kBottomRightSmallW)),
             scaleToH(rightSmallButton, static_cast<float>(kBottomRightSmallH)), 232);
    addImage(rightBigButton, kBottomRightBigX, kBottomRightBigY,
             scaleToW(rightBigButton, static_cast<float>(kBottomRightBigW)),
             scaleToH(rightBigButton, static_cast<float>(kBottomRightBigH)), 236);

    const char* inputDiagEnv = std::getenv("ROMM_INPUT_DIAG");
    if (inputDiagEnv != nullptr && inputDiagEnv[0] == '1') {
        char inputDiag[112];
        std::snprintf(
            inputDiag,
            sizeof(inputDiag),
            "C:%d E:%ld IR:%s %03d,%03d W:%08X",
            gActiveWpadChannel + 1,
            static_cast<long>(gLastWpadProbeErr),
            gPointerState.valid ? "on" : "off",
            gPointerState.x,
            gPointerState.y,
            static_cast<unsigned int>(gLastWpadHeldMask));
        addText(
            clipTextLocal(inputDiag, 26),
            13,
            gPointerState.valid ? cAccent : cBody,
            width - 232,
            30);
    }

    scene.Draw();
    drawPointerOverlay();
    GX_Flush();
}

constexpr int kPanelWidth = 74;

const char* themeHeader(romm::Status::View view) {
    switch (view) {
    case romm::Status::View::PLATFORMS:
        return "\x1b[1;37;44m";
    case romm::Status::View::ROMS:
        return "\x1b[1;37;46m";
    case romm::Status::View::DETAIL:
        return "\x1b[1;37;45m";
    case romm::Status::View::QUEUE:
        return "\x1b[1;30;43m";
    case romm::Status::View::DOWNLOADING:
        return "\x1b[1;30;103m";
    case romm::Status::View::DIAGNOSTICS:
        return "\x1b[1;37;42m";
    case romm::Status::View::UPDATER:
        return "\x1b[1;37;104m";
    case romm::Status::View::ERROR:
        return "\x1b[1;37;41m";
    default:
        return "\x1b[1;37;40m";
    }
}

std::string clipText(const std::string& text, size_t width) {
    if (text.size() <= width)
        return text;
    if (width <= 3)
        return text.substr(0, width);
    return text.substr(0, width - 3) + "...";
}

void printRule() {
    std::printf("+");
    for (int i = 0; i < kPanelWidth + 2; ++i)
        std::printf("-");
    std::printf("+\n");
}

void printPanelLine(const std::string& text, const char* color = nullptr) {
    const std::string clipped = clipText(text, static_cast<size_t>(kPanelWidth));
    if (kUseAnsiColors && color != nullptr)
        std::printf("%s", color);
    std::printf("| %-*s |", kPanelWidth, clipped.c_str());
    if (kUseAnsiColors && color != nullptr)
        std::printf("\x1b[0m");
    std::printf("\n");
}

void printField(const std::string& label, const std::string& value, const char* color = nullptr) {
    std::string text = label;
    if (text.size() < 12)
        text.append(12 - text.size(), ' ');
    text += ": " + value;
    printPanelLine(text, color);
}

std::string progressBar(uint8_t percent, int width = 18) {
    const int fill = std::min<int>(width, (static_cast<int>(percent) * width) / 100);
    std::string bar = "[";
    bar.append(fill, '#');
    bar.append(width - fill, '.');
    bar += "] ";
    bar += std::to_string(percent);
    bar += "%";
    return bar;
}

struct SoftButton {
    const char* key;
    const char* label;
};

void drawButtonStrip(const std::vector<SoftButton>& buttons) {
    std::string line;
    for (const auto& button : buttons) {
        std::string chip = "[";
        chip += button.key;
        chip += ":";
        chip += button.label;
        chip += "]";
        if (!line.empty())
            chip = " " + chip;
        if (line.size() + chip.size() > static_cast<size_t>(kPanelWidth))
            break;
        line += chip;
    }
    printPanelLine(line, "\x1b[1;30;47m");
}

template <typename T>
int clampSelectedIndex(int idx, const std::vector<T>& items) {
    if (items.empty())
        return 0;
    if (idx < 0)
        return 0;
    const int hi = static_cast<int>(items.size()) - 1;
    if (idx > hi)
        return hi;
    return idx;
}

const romm::PlatformEntry* selectedPlatform(const romm::Status& status) {
    if (status.platforms.empty())
        return nullptr;
    const int idx = clampSelectedIndex(status.selectedPlatformIndex, status.platforms);
    return &status.platforms[static_cast<size_t>(idx)];
}

const romm::RomEntry* selectedRomLoose(const romm::Status& status) {
    const romm::PlatformEntry* platform = selectedPlatform(status);
    if (platform == nullptr || platform->roms.empty())
        return nullptr;
    const int idx = clampSelectedIndex(status.selectedRomIndex, platform->roms);
    return &platform->roms[static_cast<size_t>(idx)];
}

char spinnerChar(uint32_t frame) {
    static const char kSpin[] = {'|', '/', '-', '\\'};
    return kSpin[frame % 4];
}

void drawHeader(const romm::Status& status) {
    const bool busy = status.uiBusy || status.downloadWorkerRunning;
    const std::string busyState = busy
                                      ? std::string("busy ") + spinnerChar(status.uiFrameCounter)
                                      : "idle";
    const std::string title = "Wii RomM | " + std::string(romm::screenTitle(status.currentView)) + " | " + std::string(romm::viewName(status.currentView)) + " | " + busyState;
    printPanelLine(title, themeHeader(status.currentView));
    printPanelLine(std::string("Hint: ") + romm::screenHint(status.currentView), "\x1b[36m");
}

void drawPlatformsPane(const romm::Status& status) {
    if (status.platforms.empty()) {
        printPanelLine("No platforms loaded.");
        return;
    }
    printField("Platforms", std::to_string(status.platforms.size()));
    const int selected = clampSelectedIndex(status.selectedPlatformIndex, status.platforms);
    const romm::PlatformEntry& selectedPlatformItem = status.platforms[static_cast<size_t>(selected)];
    printField("Selected", selectedPlatformItem.name + " (" + selectedPlatformItem.id + ")", "\x1b[1;33m");
    const int start = std::max(0, selected - 5);
    const int end = std::min(static_cast<int>(status.platforms.size()), start + 10);
    const bool blink = ((status.uiFrameCounter / 10) % 2) == 0;
    for (int i = start; i < end; ++i) {
        const romm::PlatformEntry& platform = status.platforms[static_cast<size_t>(i)];
        const std::string line = std::string(i == selected ? (blink ? "> " : "* ") : "  ") + "[" + std::to_string(i + 1) + "/" + std::to_string(status.platforms.size()) + "] " + platform.name + " (" + platform.id + ") roms=" + std::to_string(platform.roms.size());
        printPanelLine(line, i == selected ? "\x1b[1;33m" : nullptr);
    }
}

void drawRomsPane(const romm::Status& status) {
    const romm::PlatformEntry* platform = selectedPlatform(status);
    if (platform == nullptr) {
        printPanelLine("No selected platform.");
        return;
    }
    printField("Platform", platform->name + " (" + platform->id + ")");
    printField("Sort", std::string(romm::romSortModeName(status.romSort)));
    printField("Search", status.searchQuery.empty() ? std::string("<none>") : status.searchQuery);
    if (platform->roms.empty()) {
        printPanelLine("No ROMs indexed for this platform yet.");
        return;
    }
    const int selected = clampSelectedIndex(status.selectedRomIndex, platform->roms);
    printField("ROM Count", std::to_string(platform->roms.size()));
    const int start = std::max(0, selected - 5);
    const int end = std::min(static_cast<int>(platform->roms.size()), start + 10);
    const bool blink = ((status.uiFrameCounter / 10) % 2) == 0;
    for (int i = start; i < end; ++i) {
        const romm::RomEntry& rom = platform->roms[static_cast<size_t>(i)];
        const std::string line = std::string(i == selected ? (blink ? "> " : "* ") : "  ") + "[" + std::to_string(i + 1) + "/" + std::to_string(platform->roms.size()) + "] " + rom.title + " | " + std::to_string(rom.sizeMb) + " MB";
        printPanelLine(line, i == selected ? "\x1b[1;33m" : nullptr);
    }
}

void drawDetailPane(const romm::Status& status) {
    const romm::RomEntry* rom = selectedRomLoose(status);
    if (rom == nullptr) {
        printPanelLine("No ROM selected.");
        return;
    }
    printField("Title", rom->title, "\x1b[1;33m");
    printField("ID", rom->id);
    printField("Publisher", rom->subtitle.empty() ? std::string("<unknown>") : rom->subtitle);
    printField("Size", std::to_string(rom->sizeMb) + " MB");
    printField("Download", rom->downloadUrl.empty() ? std::string("<missing>") : rom->downloadUrl);
}

void drawQueuePane(const romm::Status& status) {
    printField("Queue", std::to_string(status.downloadQueue.size()));
    printField("History", std::to_string(status.downloadHistory.size()));
    printField("Paused", status.downloadPaused ? "yes" : "no");
    if (status.downloadQueue.empty()) {
        printPanelLine("Queue is empty.");
        return;
    }
    const int selected = clampSelectedIndex(status.selectedQueueIndex, status.downloadQueue);
    const int start = std::max(0, selected - 5);
    const int end = std::min(static_cast<int>(status.downloadQueue.size()), start + 10);
    for (int i = start; i < end; ++i) {
        const romm::QueueItem& item = status.downloadQueue[static_cast<size_t>(i)];
        std::string line = std::string(i == selected ? "> " : "  ");
        line += "[" + std::string(romm::queueStateName(item.state)) + "] " + item.rom.title + " " + progressBar(item.progressPercent);
        if (item.state == romm::QueueState::Failed && !item.error.empty()) {
            line += " err=" + item.error;
        }
        const char* color = nullptr;
        if (i == selected)
            color = "\x1b[1;33m";
        if (item.state == romm::QueueState::Failed)
            color = "\x1b[1;31m";
        if (item.state == romm::QueueState::Completed)
            color = "\x1b[1;32m";
        if (item.state == romm::QueueState::Downloading)
            color = "\x1b[1;36m";
        printPanelLine(line, color);
    }
}

void drawDiagnosticsPane(const romm::Status& status) {
    printField("Input", std::to_string(status.diagnostics.inputCount));
    printField("Enqueue", std::to_string(status.diagnostics.enqueueCount));
    printField("Duplicates", std::to_string(status.diagnostics.duplicateBlockedCount));
    printField("Completed", std::to_string(status.diagnostics.completedCount));
    printField("Failed", std::to_string(status.diagnostics.failedCount));
    printField("Searches", std::to_string(status.diagnostics.searchCount));
    printField("Frame", std::to_string(status.uiFrameCounter));
    printField("Active Job", std::to_string(status.activeDownloadIndex));
}

void drawUpdaterPane(const romm::Status& status) {
    printField("Updater", std::string(romm::updaterStateName(status.updaterState)));
    printField("Current", status.currentVersion);
    printField("Available", status.availableVersion);
}

void drawErrorPane(const romm::Status& status) {
    printPanelLine("Error screen active.", "\x1b[1;31m");
    printPanelLine("Message: " + status.lastMessage);
}

void drawFooter(const romm::Status& status) {
    if (status.currentView == romm::Status::View::PLATFORMS) {
        drawButtonStrip({{"A", "Open"}, {"B", "Back"}, {"1/Y", "Queue"}, {"2/X", "Start"}, {"+/L", "Updater"}, {"1+2/Z", "Diag"}, {"HOME", "Quit"}});
    } else if (status.currentView == romm::Status::View::ROMS) {
        drawButtonStrip({{"A", "Detail"}, {"B", "Back"}, {"-", "Search"}, {"L/R", "Sort"}, {"1/Y", "Queue"}, {"HOME", "Quit"}});
    } else if (status.currentView == romm::Status::View::DETAIL) {
        drawButtonStrip({{"A", "Add Queue"}, {"B", "Back"}, {"1/Y", "Queue"}, {"HOME", "Quit"}});
    } else if (status.currentView == romm::Status::View::QUEUE || status.currentView == romm::Status::View::DOWNLOADING) {
        drawButtonStrip({{"A", "Retry"}, {"B", "Back"}, {"Left", "Remove"}, {"Right", "Clear"}, {"2/X", "Start/Pause"}, {"HOME", "Quit"}});
    } else {
        drawButtonStrip({{"A", "Select"}, {"B", "Back"}, {"HOME", "Quit"}});
    }
    printField("Message", status.lastMessage, "\x1b[37m");
}

void drawUi(const romm::Status& status) {
    if (kUseGxBackdrop) {
        drawGxBackdrop(status);
        Menu_Render();
        return;
    }

    VIDEO_ClearFrameBuffer(gGx.rmode, gGx.xfb, COLOR_BLACK);
    std::printf("\x1b[2J\x1b[H");
    printRule();
    drawHeader(status);
    printRule();
    switch (status.currentView) {
    case romm::Status::View::PLATFORMS:
        drawPlatformsPane(status);
        break;
    case romm::Status::View::ROMS:
        drawRomsPane(status);
        break;
    case romm::Status::View::DETAIL:
        drawDetailPane(status);
        break;
    case romm::Status::View::QUEUE:
    case romm::Status::View::DOWNLOADING:
        drawQueuePane(status);
        break;
    case romm::Status::View::DIAGNOSTICS:
        drawDiagnosticsPane(status);
        break;
    case romm::Status::View::UPDATER:
        drawUpdaterPane(status);
        break;
    case romm::Status::View::ERROR:
        drawErrorPane(status);
        break;
    default:
        printPanelLine("Unknown view.");
        break;
    }
    printRule();
    drawFooter(status);
    printRule();
    std::fflush(stdout);
    VIDEO_SetNextFramebuffer(gGx.xfb);
    VIDEO_Flush();
}

bool initNetwork(std::string& outMessage) {
    if (net_init() < 0) {
        outMessage = "Network init failed.";
        return false;
    }
    char ip[16]{};
    char mask[16]{};
    char gw[16]{};
    // Keep startup responsive in emulator/bridge mode: avoid long DHCP stalls.
    if (if_config(ip, mask, gw, true, 1) < 0) {
        // Dolphin can fail DHCP while still allowing socket traffic in
        // localhost bridge mode. Continue and let HTTP sync decide readiness.
        outMessage = "DHCP unavailable; trying direct sockets.";
        return true;
    }
    outMessage = std::string("Network up: ") + ip;
    return true;
}

bool tryLaunchHomebrewChannel() {
    static constexpr u64 kHbcIds[] = {
        0x000100014C554C5ALL, // LULZ
        0x00010001AF1BF516LL, // 1.0.7
        0x000100014A4F4449LL, // JODI
        0x0001000148415858LL  // HAXX
    };

    const s32 initRc = WII_Initialize();
    if (initRc < 0)
        return false;

    for (u64 titleId : kHbcIds) {
        const s32 rc = WII_LaunchTitle(titleId);
        if (rc >= 0) {
            return true;
        }
    }
    return false;
}

bool runningUnderDolphin() {
    const char* emu = std::getenv("ROMM_EMULATOR");
    if (emu == nullptr || *emu == '\0')
        return false;
    std::string value(emu);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value.find("dolphin") != std::string::npos;
}

} // namespace

int main() {
    initVideoConsole();
    LoadLanguage();
    InitFreeType(const_cast<u8*>(font_ttf),
                 static_cast<FT_Long>(font_ttf_size));
    gFontReady = true;
    WPAD_Init();
    WPAD_SetDataFormat(WPAD_CHAN_ALL, WPAD_FMT_BTNS_ACC_IR);
    WPAD_SetVRes(WPAD_CHAN_ALL, screenwidth, screenheight);
    PAD_Init();

    romm::Status status = romm::makeDefaultStatus();
    romm::AppConfig cfg = romm::defaultConfig();
    std::string cfgErr;
    (void)romm::applyEnvOverrides(cfg, cfgErr);
    cfg.targetPlatformId = "pre-wii";

    status.currentView = romm::Status::View::PLATFORMS;
    status.lastMessage = "Starting network bootstrap...";
    // Keep controls responsive while startup indexing runs in the background.
    status.uiBusy = false;

    romm::SocketHttpClient httpClient;
    romm::StartupSyncState startup;
    romm::CatalogRuntimeState runtime;
    romm::PlatformRomIndexState romIndex;
    const bool asyncIndexReady = initAsyncRomIndexWorker(cfg);
    romm::initLogFile();
    romm::loadLogLevelFromEnv();
    romm::logInfo("wii app launched", "APP");
    if (!asyncIndexReady) {
        romm::logWarn("async ROM index worker unavailable; using synchronous fallback", "APP");
    }
    if (!cfgErr.empty()) {
        romm::logWarn("config env override error: " + cfgErr, "CFG");
    }
    bool netReady = false;
    u32 prevWpadHeld = 0;
    u16 prevGcHeld = 0;
    bool lastPointerValidLogged = false;
    int lastPointerLogX = -1;
    int lastPointerLogY = -1;
    drawUi(status);

    while (true) {
        status.uiFrameCounter++;
        WPAD_ScanPads();
        PAD_ScanPads();

        gActiveWpadChannel = resolveActiveWpadChannel();
        const u32 wpadDown = WPAD_ButtonsDown(gActiveWpadChannel);
        const u32 wpadHeld = WPAD_ButtonsHeld(gActiveWpadChannel);
        gLastWpadHeldMask = wpadHeld;
        const u16 gcDown = PAD_ButtonsDown(0);
        const u16 gcHeld = PAD_ButtonsHeld(0);

        const u32 wpadPressed = resolveWpadPressed(wpadDown, wpadHeld, prevWpadHeld);
        const u16 gcPressed = resolveGcPressed(gcDown, gcHeld, prevGcHeld);
        prevWpadHeld = wpadHeld;
        prevGcHeld = gcHeld;

        const bool pointerVisualChanged = updatePointerState(gActiveWpadChannel);
        if (gPointerState.valid != lastPointerValidLogged ||
            (gPointerState.valid &&
             (std::abs(gPointerState.x - lastPointerLogX) > 24 ||
              std::abs(gPointerState.y - lastPointerLogY) > 24))) {
            romm::logInfo(
                "pointer valid=" + std::string(gPointerState.valid ? "1" : "0") +
                " x=" + std::to_string(gPointerState.x) +
                " y=" + std::to_string(gPointerState.y),
                "INPUT");
            lastPointerValidLogged = gPointerState.valid;
            lastPointerLogX = gPointerState.x;
            lastPointerLogY = gPointerState.y;
        }
        const romm::LogicalButton wiiButton = mapWpadButton(wpadPressed);
        const bool wiiSelectFromA = (wiiButton == romm::LogicalButton::Confirm);

        romm::Action action = romm::mapButtonToAction(
            romm::ControlProfile::WiiRemote, wiiButton);
        if (action == romm::Action::None) {
            action = romm::mapButtonToAction(
                romm::ControlProfile::GameCube, mapGcButton(gcPressed));
        }
        const PointerActionResult pointerResult = applyPointerNavigation(
            status, (wpadPressed & WPAD_BUTTON_A) != 0, true);
        if (pointerResult.consumedA && wiiSelectFromA && action == romm::Action::Select) {
            action = romm::Action::None;
        }
        if (pointerResult.action != romm::Action::None &&
            (action == romm::Action::None || (pointerResult.consumedA && wiiSelectFromA))) {
            action = pointerResult.action;
        }
        if (pointerResult.requestReturnToMenu) {
            romm::logInfo("homebrew-menu requested from Wii dock button", "UI");
            if (runningUnderDolphin()) {
                romm::logWarn("dolphin runtime detected; using app quit fallback for Wii button", "UI");
                action = romm::Action::Quit;
            } else if (tryLaunchHomebrewChannel()) {
                shutdownAsyncRomIndexWorker();
                romm::shutdownLogFile();
                return 0;
            } else {
                romm::logWarn("homebrew channel not available; falling back to app quit", "UI");
                action = romm::Action::Quit;
            }
        }
        bool uiResetChanged = false;
        if (pointerResult.requestUiReset) {
            status.currentView = romm::Status::View::PLATFORMS;
            status.prevQueueView = romm::Status::View::PLATFORMS;
            status.prevDiagnosticsView = romm::Status::View::PLATFORMS;
            status.prevUpdaterView = romm::Status::View::PLATFORMS;
            status.selectedPlatformIndex = 0;
            status.selectedRomIndex = 0;
            status.selectedQueueIndex = 0;
            status.searchQuery.clear();
            status.romSort = romm::RomSortMode::TitleAsc;
            status.lastMessage = "UI reset.";
            action = romm::Action::None;
            uiResetChanged = true;
        }
        bool romIndexBeginChanged = false;
        if (startup.finished && netReady && action == romm::Action::Select &&
            status.currentView == romm::Status::View::PLATFORMS) {
            std::string indexErr;
            const bool queued = asyncIndexReady
                                    ? queueSelectedPlatformRomIndexAsync(status, runtime, true, indexErr)
                                    : romm::beginSelectedPlatformRomIndex(status, runtime, true, romIndex, indexErr);
            if (!queued) {
                status.lastMessage = "ROM index failed: " + indexErr;
                romm::logWarn(status.lastMessage, "API");
            } else {
                romm::logInfo(status.lastMessage, "API");
            }
            romIndexBeginChanged = true;
            // Opening ROM view is deferred until index completion.
            action = romm::Action::None;
        }

        const romm::ApplyResult result = romm::applyAction(status, action);
        if (action != romm::Action::None) {
            char wpadHex[16];
            char gcHex[16];
            std::snprintf(wpadHex, sizeof(wpadHex), "%08X", static_cast<unsigned int>(wpadPressed));
            std::snprintf(gcHex, sizeof(gcHex), "%04X", static_cast<unsigned int>(gcPressed));
            romm::logDebug(
                "frame=" + std::to_string(status.uiFrameCounter) + " action=" + std::string(romm::actionName(action)) + " view=" + std::string(romm::viewName(status.currentView)) + " stateChanged=" + std::string(result.stateChanged ? "1" : "0") + " keepRunning=" + std::string(result.keepRunning ? "1" : "0") + " wpad=0x" + std::string(wpadHex) + " gc=0x" + std::string(gcHex),
                "UI");
        }
        if (!result.keepRunning) {
            romm::logInfo("quit requested", "APP");
            break;
        }

        bool startupChanged = false;
        if (!startup.finished) {
            startupChanged = romm::stepStartupSync(status, cfg, httpClient, startup, initNetwork);
            netReady = romm::startupSyncReady(startup);
        }
        bool romIndexChanged = false;
        if (startup.finished && netReady) {
            if (asyncIndexReady) {
                romIndexChanged = pollPlatformRomIndexAsyncResult(status, runtime);
            } else {
                std::string indexErr;
                romIndexChanged = romm::stepPlatformRomIndex(
                    status, cfg, httpClient, runtime, romIndex, indexErr);
                if (!indexErr.empty()) {
                    romm::logWarn("ROM index failed: " + indexErr, "API");
                }
            }
        }

        status.uiBusy = asyncIndexReady ? isPlatformRomIndexBusyAsync()
                                        : romm::isPlatformRomIndexActive(romIndex);

        bool tickChanged = false;
        if (startup.finished && netReady) {
            tickChanged = romm::runRealDownloads(status, cfg, httpClient);
        } else {
            tickChanged = romm::tickDownload(status, 16);
        }
        const bool animateFrame = (status.uiBusy || status.downloadWorkerRunning) && ((status.uiFrameCounter % 15u) == 0u);
        if (action != romm::Action::None || result.stateChanged || uiResetChanged || tickChanged ||
            startupChanged || animateFrame || pointerVisualChanged ||
            pointerResult.selectionChanged || romIndexChanged || romIndexBeginChanged) {
            drawUi(status);
        }
        VIDEO_WaitVSync();
    }

    // Keep exit path conservative on hardware/emulators: avoid teardown-time faults.
    shutdownAsyncRomIndexWorker();
    romm::shutdownLogFile();
    return 0;
}
