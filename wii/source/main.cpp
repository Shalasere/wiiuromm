#include <gccore.h>
#include <network.h>
#include <ogc/pad.h>
#include <wiiuse/wpad.h>

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <malloc.h>
#include <set>
#include <string>
#include <vector>

#include "romm/api_client.hpp"
#include "romm/app_core.hpp"
#include "romm/config.hpp"
#include "romm/control_schema.hpp"
#include "romm/downloader.hpp"
#include "romm/http_client.hpp"
#include "romm/logger.hpp"

namespace {

romm::LogicalButton mapWpadButton(u32 pressed) {
    if (pressed & WPAD_BUTTON_HOME) return romm::LogicalButton::Quit;
    if ((pressed & WPAD_BUTTON_1) && (pressed & WPAD_BUTTON_2)) return romm::LogicalButton::Diagnostics;
    if (pressed & WPAD_BUTTON_PLUS) return romm::LogicalButton::Updater;
    if (pressed & WPAD_BUTTON_UP) return romm::LogicalButton::Up;
    if (pressed & WPAD_BUTTON_DOWN) return romm::LogicalButton::Down;
    if (pressed & WPAD_BUTTON_LEFT) return romm::LogicalButton::Left;
    if (pressed & WPAD_BUTTON_RIGHT) return romm::LogicalButton::Right;
    if (pressed & WPAD_BUTTON_A) return romm::LogicalButton::Confirm;
    if (pressed & WPAD_BUTTON_B) return romm::LogicalButton::Back;
    if (pressed & WPAD_BUTTON_1) return romm::LogicalButton::Queue;
    if (pressed & WPAD_BUTTON_2) return romm::LogicalButton::StartWork;
    if (pressed & WPAD_BUTTON_MINUS) return romm::LogicalButton::Search;
    return romm::LogicalButton::None;
}

romm::LogicalButton mapGcButton(u16 pressed) {
    if (pressed & PAD_BUTTON_START) return romm::LogicalButton::Quit;
    if (pressed & PAD_BUTTON_UP) return romm::LogicalButton::Up;
    if (pressed & PAD_BUTTON_DOWN) return romm::LogicalButton::Down;
    if (pressed & PAD_BUTTON_LEFT) return romm::LogicalButton::Left;
    if (pressed & PAD_BUTTON_RIGHT) return romm::LogicalButton::Right;
    if (pressed & PAD_BUTTON_A) return romm::LogicalButton::Confirm;
    if (pressed & PAD_BUTTON_B) return romm::LogicalButton::Back;
    if (pressed & PAD_BUTTON_Y) return romm::LogicalButton::Queue;
    if (pressed & PAD_BUTTON_X) return romm::LogicalButton::StartWork;
    if (pressed & PAD_TRIGGER_R) return romm::LogicalButton::Search;
    if (pressed & PAD_TRIGGER_Z) return romm::LogicalButton::Diagnostics;
    if (pressed & PAD_TRIGGER_L) return romm::LogicalButton::Updater;
    return romm::LogicalButton::None;
}

constexpr u32 kGxFifoSize = (256 * 1024);

struct GxState {
    GXRModeObj *rmode{nullptr};
    void *xfb{nullptr};
    void *fifo{nullptr};
    bool initialized{false};
};

struct UiColor {
    u8 r;
    u8 g;
    u8 b;
    u8 a;
};

GxState gGx;

int clampIndexCount(int idx, int count) {
    if (count <= 0) return 0;
    if (idx < 0) return 0;
    if (idx >= count) return count - 1;
    return idx;
}

UiColor baseColorForView(romm::Status::View view) {
    switch (view) {
        case romm::Status::View::PLATFORMS: return {9, 36, 82, 255};
        case romm::Status::View::ROMS: return {8, 56, 74, 255};
        case romm::Status::View::DETAIL: return {26, 30, 85, 255};
        case romm::Status::View::QUEUE: return {46, 30, 75, 255};
        case romm::Status::View::DOWNLOADING: return {95, 69, 6, 255};
        case romm::Status::View::DIAGNOSTICS: return {21, 55, 33, 255};
        case romm::Status::View::UPDATER: return {24, 35, 86, 255};
        case romm::Status::View::ERROR: return {84, 9, 9, 255};
        default: return {16, 16, 16, 255};
    }
}

UiColor accentColorForView(romm::Status::View view) {
    switch (view) {
        case romm::Status::View::PLATFORMS: return {45, 115, 220, 255};
        case romm::Status::View::ROMS: return {26, 155, 190, 255};
        case romm::Status::View::DETAIL: return {96, 86, 220, 255};
        case romm::Status::View::QUEUE: return {170, 105, 228, 255};
        case romm::Status::View::DOWNLOADING: return {221, 173, 31, 255};
        case romm::Status::View::DIAGNOSTICS: return {68, 162, 80, 255};
        case romm::Status::View::UPDATER: return {85, 125, 230, 255};
        case romm::Status::View::ERROR: return {196, 46, 46, 255};
        default: return {200, 200, 200, 255};
    }
}

GXColor toGX(const UiColor &c) {
    GXColor out = {c.r, c.g, c.b, c.a};
    return out;
}

void gxDrawRect(f32 x, f32 y, f32 w, f32 h, const UiColor &color) {
    const GXColor c = toGX(color);
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position3f32(x, y, 0.0f);
    GX_Color4u8(c.r, c.g, c.b, c.a);
    GX_Position3f32(x + w, y, 0.0f);
    GX_Color4u8(c.r, c.g, c.b, c.a);
    GX_Position3f32(x + w, y + h, 0.0f);
    GX_Color4u8(c.r, c.g, c.b, c.a);
    GX_Position3f32(x, y + h, 0.0f);
    GX_Color4u8(c.r, c.g, c.b, c.a);
    GX_End();
}

void initVideoConsole() {
    VIDEO_Init();

    gGx.rmode = VIDEO_GetPreferredMode(nullptr);
    gGx.xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(gGx.rmode));
    gGx.fifo = MEM_K0_TO_K1(memalign(32, kGxFifoSize));
    if (gGx.fifo != nullptr) {
        std::memset(gGx.fifo, 0, kGxFifoSize);
    }

    console_init(gGx.xfb, 20, 20, gGx.rmode->fbWidth, gGx.rmode->xfbHeight,
                 gGx.rmode->fbWidth * VI_DISPLAY_PIX_SZ);

    VIDEO_Configure(gGx.rmode);
    VIDEO_SetNextFramebuffer(gGx.xfb);
    VIDEO_SetBlack(false);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (gGx.rmode->viTVMode & VI_NON_INTERLACE) {
        VIDEO_WaitVSync();
    }

    if (gGx.fifo == nullptr) {
        gGx.initialized = false;
        return;
    }

    GX_Init(gGx.fifo, kGxFifoSize);
    GX_SetCopyClear((GXColor){0, 0, 0, 255}, 0x00ffffff);
    GX_SetViewport(0, 0, gGx.rmode->fbWidth, gGx.rmode->efbHeight, 0, 1);
    GX_SetDispCopyYScale((f32)gGx.rmode->xfbHeight / (f32)gGx.rmode->efbHeight);
    GX_SetScissor(0, 0, gGx.rmode->fbWidth, gGx.rmode->efbHeight);
    GX_SetDispCopySrc(0, 0, gGx.rmode->fbWidth, gGx.rmode->efbHeight);
    GX_SetDispCopyDst(gGx.rmode->fbWidth, gGx.rmode->xfbHeight);
    GX_SetCopyFilter(gGx.rmode->aa, gGx.rmode->sample_pattern, GX_TRUE, gGx.rmode->vfilter);
    GX_SetFieldMode(gGx.rmode->field_rendering,
                    ((gGx.rmode->viHeight == 2 * gGx.rmode->xfbHeight) ? GX_ENABLE : GX_DISABLE));
    GX_SetCullMode(GX_CULL_NONE);
    GX_SetNumChans(1);
    GX_SetNumTexGens(0);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_CopyDisp(gGx.xfb, GX_TRUE);
    GX_SetDispCopyGamma(GX_GM_1_0);
    gGx.initialized = true;
}

void drawGxBackdrop(const romm::Status &status) {
    if (!gGx.initialized) return;

    Mtx44 proj;
    const f32 width = static_cast<f32>(gGx.rmode->fbWidth);
    const f32 height = static_cast<f32>(gGx.rmode->efbHeight);
    guOrtho(proj, 0, height, 0, width, 0, 1);
    GX_LoadProjectionMtx(proj, GX_ORTHOGRAPHIC);
    GX_SetViewport(0, 0, width, height, 0, 1);

    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_InvVtxCache();
    GX_InvalidateTexAll();
    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GX_SetColorUpdate(GX_TRUE);

    const UiColor base = baseColorForView(status.currentView);
    const UiColor accent = accentColorForView(status.currentView);
    const UiColor content = {20, 20, 28, 230};
    const UiColor footer = {15, 15, 20, 240};
    const UiColor selected = {245, 198, 52, 220};

    gxDrawRect(0, 0, width, height, base);
    gxDrawRect(0, 0, width, 54, accent);
    gxDrawRect(12, 66, width - 24, height - 126, content);
    gxDrawRect(0, height - 48, width, 48, footer);

    int selectedRow = -1;
    if (status.currentView == romm::Status::View::PLATFORMS) {
        const int sel = clampIndexCount(status.selectedPlatformIndex, static_cast<int>(status.platforms.size()));
        const int start = std::max(0, sel - 5);
        selectedRow = sel - start;
    } else if (status.currentView == romm::Status::View::ROMS) {
        int romCount = 0;
        if (!status.platforms.empty()) {
            const int psel = clampIndexCount(status.selectedPlatformIndex, static_cast<int>(status.platforms.size()));
            romCount = static_cast<int>(status.platforms[static_cast<size_t>(psel)].roms.size());
        }
        const int sel = clampIndexCount(status.selectedRomIndex, romCount);
        const int start = std::max(0, sel - 5);
        selectedRow = sel - start;
    } else if (status.currentView == romm::Status::View::QUEUE ||
               status.currentView == romm::Status::View::DOWNLOADING) {
        const int sel = clampIndexCount(status.selectedQueueIndex, static_cast<int>(status.downloadQueue.size()));
        const int start = std::max(0, sel - 5);
        selectedRow = sel - start;
    }

    if (selectedRow >= 0) {
        const f32 y = 150.0f + static_cast<f32>(selectedRow) * 24.0f;
        gxDrawRect(20, y, width - 40, 18, selected);
    }

    GX_DrawDone();
    GX_CopyDisp(gGx.xfb, GX_TRUE);
    GX_Flush();
    VIDEO_SetNextFramebuffer(gGx.xfb);
    VIDEO_Flush();
}

constexpr int kPanelWidth = 74;

const char *themeHeader(romm::Status::View view) {
    switch (view) {
        case romm::Status::View::PLATFORMS: return "\x1b[1;37;44m";
        case romm::Status::View::ROMS: return "\x1b[1;37;46m";
        case romm::Status::View::DETAIL: return "\x1b[1;37;45m";
        case romm::Status::View::QUEUE: return "\x1b[1;30;43m";
        case romm::Status::View::DOWNLOADING: return "\x1b[1;30;103m";
        case romm::Status::View::DIAGNOSTICS: return "\x1b[1;37;42m";
        case romm::Status::View::UPDATER: return "\x1b[1;37;104m";
        case romm::Status::View::ERROR: return "\x1b[1;37;41m";
        default: return "\x1b[1;37;40m";
    }
}

std::string clipText(const std::string &text, size_t width) {
    if (text.size() <= width) return text;
    if (width <= 3) return text.substr(0, width);
    return text.substr(0, width - 3) + "...";
}

void printRule() {
    std::printf("+");
    for (int i = 0; i < kPanelWidth + 2; ++i) std::printf("-");
    std::printf("+\n");
}

void printPanelLine(const std::string &text, const char *color = nullptr) {
    const std::string clipped = clipText(text, static_cast<size_t>(kPanelWidth));
    if (color != nullptr) std::printf("%s", color);
    std::printf("| %-*s |", kPanelWidth, clipped.c_str());
    if (color != nullptr) std::printf("\x1b[0m");
    std::printf("\n");
}

void printField(const std::string &label, const std::string &value, const char *color = nullptr) {
    std::string text = label;
    if (text.size() < 12) text.append(12 - text.size(), ' ');
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
    const char *key;
    const char *label;
};

void drawButtonStrip(const std::vector<SoftButton> &buttons) {
    std::string line;
    for (const auto &button : buttons) {
        std::string chip = "[";
        chip += button.key;
        chip += ":";
        chip += button.label;
        chip += "]";
        if (!line.empty()) chip = " " + chip;
        if (line.size() + chip.size() > static_cast<size_t>(kPanelWidth)) break;
        line += chip;
    }
    printPanelLine(line, "\x1b[1;30;47m");
}

template <typename T>
int clampSelectedIndex(int idx, const std::vector<T> &items) {
    if (items.empty()) return 0;
    if (idx < 0) return 0;
    const int hi = static_cast<int>(items.size()) - 1;
    if (idx > hi) return hi;
    return idx;
}

const romm::PlatformEntry *selectedPlatform(const romm::Status &status) {
    if (status.platforms.empty()) return nullptr;
    const int idx = clampSelectedIndex(status.selectedPlatformIndex, status.platforms);
    return &status.platforms[static_cast<size_t>(idx)];
}

const romm::RomEntry *selectedRomLoose(const romm::Status &status) {
    const romm::PlatformEntry *platform = selectedPlatform(status);
    if (platform == nullptr || platform->roms.empty()) return nullptr;
    const int idx = clampSelectedIndex(status.selectedRomIndex, platform->roms);
    return &platform->roms[static_cast<size_t>(idx)];
}

char spinnerChar(uint32_t frame) {
    static const char kSpin[] = {'|', '/', '-', '\\'};
    return kSpin[frame % 4];
}

void drawHeader(const romm::Status &status) {
    const bool busy = status.uiBusy || status.downloadWorkerRunning;
    const std::string busyState = busy
        ? std::string("busy ") + spinnerChar(status.uiFrameCounter)
        : "idle";
    const std::string title = "Wii RomM | " + std::string(romm::screenTitle(status.currentView)) +
                              " | " + std::string(romm::viewName(status.currentView)) +
                              " | " + busyState;
    printPanelLine(title, themeHeader(status.currentView));
    printPanelLine(std::string("Hint: ") + romm::screenHint(status.currentView), "\x1b[36m");
}

void drawPlatformsPane(const romm::Status &status) {
    if (status.platforms.empty()) {
        printPanelLine("No platforms loaded.");
        return;
    }
    printField("Platforms", std::to_string(status.platforms.size()));
    const int selected = clampSelectedIndex(status.selectedPlatformIndex, status.platforms);
    const romm::PlatformEntry &selectedPlatformItem = status.platforms[static_cast<size_t>(selected)];
    printField("Selected", selectedPlatformItem.name + " (" + selectedPlatformItem.id + ")", "\x1b[1;33m");
    const int start = std::max(0, selected - 5);
    const int end = std::min(static_cast<int>(status.platforms.size()), start + 10);
    const bool blink = ((status.uiFrameCounter / 10) % 2) == 0;
    for (int i = start; i < end; ++i) {
        const romm::PlatformEntry &platform = status.platforms[static_cast<size_t>(i)];
        const std::string line = std::string(i == selected ? (blink ? "> " : "* ") : "  ") +
            "[" + std::to_string(i + 1) + "/" + std::to_string(status.platforms.size()) + "] " +
            platform.name + " (" + platform.id + ") roms=" + std::to_string(platform.roms.size());
        printPanelLine(line, i == selected ? "\x1b[1;33m" : nullptr);
    }
}

void drawRomsPane(const romm::Status &status) {
    const romm::PlatformEntry *platform = selectedPlatform(status);
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
        const romm::RomEntry &rom = platform->roms[static_cast<size_t>(i)];
        const std::string line = std::string(i == selected ? (blink ? "> " : "* ") : "  ") +
            "[" + std::to_string(i + 1) + "/" + std::to_string(platform->roms.size()) + "] " +
            rom.title + " | " + std::to_string(rom.sizeMb) + " MB";
        printPanelLine(line, i == selected ? "\x1b[1;33m" : nullptr);
    }
}

void drawDetailPane(const romm::Status &status) {
    const romm::RomEntry *rom = selectedRomLoose(status);
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

void drawQueuePane(const romm::Status &status) {
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
        const romm::QueueItem &item = status.downloadQueue[static_cast<size_t>(i)];
        std::string line = std::string(i == selected ? "> " : "  ");
        line += "[" + std::string(romm::queueStateName(item.state)) + "] " + item.rom.title +
                " " + progressBar(item.progressPercent);
        if (item.state == romm::QueueState::Failed && !item.error.empty()) {
            line += " err=" + item.error;
        }
        const char *color = nullptr;
        if (i == selected) color = "\x1b[1;33m";
        if (item.state == romm::QueueState::Failed) color = "\x1b[1;31m";
        if (item.state == romm::QueueState::Completed) color = "\x1b[1;32m";
        if (item.state == romm::QueueState::Downloading) color = "\x1b[1;36m";
        printPanelLine(line, color);
    }
}

void drawDiagnosticsPane(const romm::Status &status) {
    printField("Input", std::to_string(status.diagnostics.inputCount));
    printField("Enqueue", std::to_string(status.diagnostics.enqueueCount));
    printField("Duplicates", std::to_string(status.diagnostics.duplicateBlockedCount));
    printField("Completed", std::to_string(status.diagnostics.completedCount));
    printField("Failed", std::to_string(status.diagnostics.failedCount));
    printField("Searches", std::to_string(status.diagnostics.searchCount));
    printField("Frame", std::to_string(status.uiFrameCounter));
    printField("Active Job", std::to_string(status.activeDownloadIndex));
}

void drawUpdaterPane(const romm::Status &status) {
    printField("Updater", std::string(romm::updaterStateName(status.updaterState)));
    printField("Current", status.currentVersion);
    printField("Available", status.availableVersion);
}

void drawErrorPane(const romm::Status &status) {
    printPanelLine("Error screen active.", "\x1b[1;31m");
    printPanelLine("Message: " + status.lastMessage);
}

void drawFooter(const romm::Status &status) {
    if (status.currentView == romm::Status::View::PLATFORMS) {
        drawButtonStrip({{"A", "Open"}, {"B", "Back"}, {"1/Y", "Queue"}, {"2/X", "Start"},
                         {"+/L", "Updater"}, {"1+2/Z", "Diag"}, {"HOME", "Quit"}});
    } else if (status.currentView == romm::Status::View::ROMS) {
        drawButtonStrip({{"A", "Detail"}, {"B", "Back"}, {"-", "Search"}, {"L/R", "Sort"},
                         {"1/Y", "Queue"}, {"HOME", "Quit"}});
    } else if (status.currentView == romm::Status::View::DETAIL) {
        drawButtonStrip({{"A", "Add Queue"}, {"B", "Back"}, {"1/Y", "Queue"}, {"HOME", "Quit"}});
    } else if (status.currentView == romm::Status::View::QUEUE ||
               status.currentView == romm::Status::View::DOWNLOADING) {
        drawButtonStrip({{"A", "Retry"}, {"B", "Back"}, {"Left", "Remove"}, {"Right", "Clear"},
                         {"2/X", "Start/Pause"}, {"HOME", "Quit"}});
    } else {
        drawButtonStrip({{"A", "Select"}, {"B", "Back"}, {"HOME", "Quit"}});
    }
    printField("Message", status.lastMessage, "\x1b[37m");
}

void drawUi(const romm::Status &status) {
    drawGxBackdrop(status);
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
}

bool initNetwork(std::string &outMessage) {
    if (net_init() < 0) {
        outMessage = "Network init failed.";
        return false;
    }
    char ip[16]{};
    char mask[16]{};
    char gw[16]{};
    if (if_config(ip, mask, gw, true, 20) < 0) {
        outMessage = "DHCP failed.";
        return false;
    }
    outMessage = std::string("Network up: ") + ip;
    return true;
}

std::vector<std::string> splitList(const std::string &raw) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= raw.size()) {
        const size_t comma = raw.find(',', start);
        const std::string token = raw.substr(start, comma == std::string::npos
            ? std::string::npos : comma - start);
        if (!token.empty()) out.push_back(token);
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return out;
}

void filterTargetPlatforms(const std::string &target, std::vector<romm::PlatformEntry> &platforms,
                           std::string &outError) {
    if (target.empty()) return;
    const std::vector<std::string> wanted = splitList(target);
    if (wanted.empty()) return;

    std::set<std::string> wantSet(wanted.begin(), wanted.end());
    if (wantSet.count("gc")) wantSet.insert("ngc");

    std::vector<romm::PlatformEntry> filtered;
    filtered.reserve(platforms.size());
    for (const auto &p : platforms) {
        if (wantSet.count(p.id) || wantSet.count(p.slug) || wantSet.count(p.name)) {
            filtered.push_back(p);
        }
    }
    if (filtered.empty()) {
        outError = "no requested target platforms found: " + target;
        return;
    }
    platforms = std::move(filtered);
}

struct StartupSyncState {
    enum class Stage {
        InitNetwork = 0,
        FetchPlatforms,
        FilterPlatforms,
        Complete,
        Failed
    };

    Stage stage{Stage::InitNetwork};
    bool finished{false};
    bool netReady{false};
    std::vector<romm::PlatformEntry> platforms;
    int platformPage{1};
    int platformGuard{0};
    int netRetryCount{0};
    int netRetryDelayFrames{0};
};

struct CatalogRuntimeState {
    std::set<std::string> indexedPlatformIds;
};

bool isTransientNetError(const std::string &err) {
    return err.find("socket connect failed") != std::string::npos ||
           err.find("DNS lookup failed") != std::string::npos ||
           err.find("socket write failed") != std::string::npos ||
           err.find("socket read failed") != std::string::npos;
}

bool stepStartupSync(romm::Status &status, const romm::AppConfig &cfg, romm::IHttpClient &client,
                     StartupSyncState &startup) {
    if (startup.finished) return false;
    if (startup.netRetryDelayFrames > 0) {
        startup.netRetryDelayFrames--;
        return false;
    }
    std::string err;

    switch (startup.stage) {
        case StartupSyncState::Stage::InitNetwork: {
            std::string netMsg;
            startup.netReady = initNetwork(netMsg);
            if (!startup.netReady) {
                startup.stage = StartupSyncState::Stage::Failed;
                status.currentView = romm::Status::View::ERROR;
                status.lastMessage = netMsg;
                return true;
            }
            status.lastMessage = netMsg;
            startup.stage = StartupSyncState::Stage::FetchPlatforms;
            return true;
        }
        case StartupSyncState::Stage::FetchPlatforms: {
            std::vector<romm::PlatformEntry> items;
            int nextPage = 0;
            if (!romm::fetchPlatformsPage(cfg, client, startup.platformPage, 32, items, nextPage, err)) {
                if (isTransientNetError(err) && startup.netRetryCount < 3) {
                    startup.netRetryCount++;
                    startup.netRetryDelayFrames = 30 * startup.netRetryCount;
                    status.lastMessage = "Network retry " + std::to_string(startup.netRetryCount) +
                                         "/3: " + err;
                    return true;
                }
                startup.stage = StartupSyncState::Stage::Failed;
                status.currentView = romm::Status::View::ERROR;
                status.lastMessage = "Catalog sync failed: " + err;
                return true;
            }
            startup.netRetryCount = 0;
            startup.platforms.insert(startup.platforms.end(), items.begin(), items.end());
            startup.platformPage = nextPage;
            startup.platformGuard++;
            status.lastMessage = "Indexing platforms: " + std::to_string(startup.platforms.size());
            if (startup.platformPage <= 0 || startup.platformGuard >= 64) {
                startup.stage = StartupSyncState::Stage::FilterPlatforms;
            }
            return true;
        }
        case StartupSyncState::Stage::FilterPlatforms: {
            if (startup.platforms.empty()) {
                startup.stage = StartupSyncState::Stage::Failed;
                status.currentView = romm::Status::View::ERROR;
                status.lastMessage = "Catalog sync failed: API returned no platforms";
                return true;
            }
            filterTargetPlatforms(cfg.targetPlatformId, startup.platforms, err);
            if (!err.empty()) {
                startup.stage = StartupSyncState::Stage::Failed;
                status.currentView = romm::Status::View::ERROR;
                status.lastMessage = "Catalog sync failed: " + err;
                return true;
            }
            startup.stage = StartupSyncState::Stage::Complete;
            return true;
        }
        case StartupSyncState::Stage::Complete:
            {
            status.platforms = std::move(startup.platforms);
            status.selectedPlatformIndex = 0;
            status.selectedRomIndex = 0;
            status.currentView = romm::Status::View::PLATFORMS;
            status.lastMessage = "Index complete: " + std::to_string(status.platforms.size()) +
                                 " platform(s). ROMs index on select.";
            startup.finished = true;
            return true;
            }
        case StartupSyncState::Stage::Failed:
            startup.finished = true;
            return true;
    }
    return false;
}

struct QueueDownloadObserver final : public romm::IDownloadObserver {
    explicit QueueDownloadObserver(romm::QueueItem &itemRef) : item(itemRef) {}
    romm::QueueItem &item;

    void onProgress(const romm::DownloadProgress &progress) override {
        if (progress.expectedBytes > 0) {
            const uint64_t pct = (progress.downloadedBytes * 100ull) / progress.expectedBytes;
            item.progressPercent = static_cast<uint8_t>(std::min<uint64_t>(pct, 100ull));
        } else {
            item.progressPercent = std::min<uint8_t>(99, static_cast<uint8_t>(item.progressPercent + 1));
        }
    }

    void onComplete(const romm::DownloadRequest &request) override {
        (void)request;
        item.progressPercent = 100;
        item.state = romm::QueueState::Completed;
        item.error.clear();
    }

    void onFailure(const romm::DownloadRequest &request, const romm::ErrorInfo &error) override {
        (void)request;
        item.state = romm::QueueState::Failed;
        item.error = error.userMessage;
        item.progressPercent = 0;
    }
};

bool runRealDownloads(romm::Status &status, const romm::AppConfig &cfg, romm::IHttpClient &client) {
    if (!status.downloadWorkerRunning || status.downloadPaused) return false;
    if (status.downloadQueue.empty()) {
        status.downloadWorkerRunning = false;
        status.currentView = romm::Status::View::QUEUE;
        status.lastMessage = "Queue empty.";
        return true;
    }

    bool changed = false;
    size_t idx = 0;
    while (idx < status.downloadQueue.size()) {
        romm::QueueItem &item = status.downloadQueue[idx];
        if (item.state != romm::QueueState::Pending) {
            idx++;
            continue;
        }
        item.state = romm::QueueState::Downloading;
        item.progressPercent = 1;
        changed = true;

        romm::DownloadRequest req;
        req.id = item.rom.id;
        req.title = item.rom.title;
        req.url = item.rom.downloadUrl;
        req.outputPath = cfg.downloadDir + "/" + item.rom.id + ".bin";

        QueueDownloadObserver observer(item);
        std::string dlErr;
        const bool ok = romm::runDownloadQueue(cfg, client, {req}, observer, dlErr);
        if (ok) {
            status.downloadHistory.push_back(item);
            status.downloadQueue.erase(status.downloadQueue.begin() + static_cast<long>(idx));
            status.diagnostics.completedCount++;
            status.lastMessage = "Completed: " + req.title;
            continue;
        }

        item.state = romm::QueueState::Failed;
        item.error = dlErr;
        item.progressPercent = 0;
        status.diagnostics.failedCount++;
        status.lastMessage = "Failed: " + item.rom.title + " (" + dlErr + ")";
        idx++;
    }

    status.downloadWorkerRunning = false;
    status.downloadPaused = false;
    status.activeDownloadIndex = -1;
    status.currentView = romm::Status::View::QUEUE;
    if (status.selectedQueueIndex >= static_cast<int>(status.downloadQueue.size())) {
        status.selectedQueueIndex = status.downloadQueue.empty()
            ? 0
            : static_cast<int>(status.downloadQueue.size()) - 1;
    }
    return changed;
}

bool indexSelectedPlatformRoms(romm::Status &status, const romm::AppConfig &cfg, romm::IHttpClient &client,
                               CatalogRuntimeState &runtime, std::string &outError) {
    if (status.platforms.empty()) {
        outError = "no platforms indexed";
        return false;
    }
    int idx = status.selectedPlatformIndex;
    if (idx < 0) idx = 0;
    if (idx >= static_cast<int>(status.platforms.size())) {
        idx = static_cast<int>(status.platforms.size()) - 1;
    }
    romm::PlatformEntry &platform = status.platforms[static_cast<size_t>(idx)];
    if (runtime.indexedPlatformIds.count(platform.id)) return true;

    int romPage = 1;
    int romGuard = 0;
    platform.roms.clear();
    while (romPage > 0 && romGuard < 256) {
        std::vector<romm::RomEntry> items;
        int nextPage = 0;
        if (!romm::fetchRomsPage(cfg, client, platform.id, romPage, 64, items, nextPage, outError)) {
            return false;
        }
        platform.roms.insert(platform.roms.end(), items.begin(), items.end());
        romPage = nextPage;
        romGuard++;
        status.lastMessage = "Indexing ROMs: " + platform.name + " (" + std::to_string(platform.roms.size()) + ")";
    }
    runtime.indexedPlatformIds.insert(platform.id);
    status.selectedRomIndex = 0;
    status.lastMessage = "Index complete: " + platform.name + " (" +
                         std::to_string(platform.roms.size()) + " ROMs)";
    return true;
}

} // namespace

int main() {
    initVideoConsole();
    WPAD_Init();
    PAD_Init();

    romm::Status status = romm::makeDefaultStatus();
    romm::AppConfig cfg = romm::defaultConfig();
    cfg.serverUrl = "http://games.fortkickass.tech";
    cfg.username = "root";
    cfg.password = "Quicksilver0917!";
    cfg.targetPlatformId = "wii,gc,gb,gbc,gba,nes,snes,n64";

    std::string cfgErr;
    (void)romm::applyEnvOverrides(cfg, cfgErr);

    status.platforms.clear();
    status.currentView = romm::Status::View::PLATFORMS;
    status.lastMessage = "Starting network bootstrap...";
    status.uiBusy = true;

    romm::SocketHttpClient httpClient;
    StartupSyncState startup;
    CatalogRuntimeState runtime;
    romm::initLogFile();
    romm::loadLogLevelFromEnv();
    romm::logInfo("wii app launched", "APP");
    bool netReady = false;
    drawUi(status);

    while (true) {
        status.uiFrameCounter++;
        WPAD_ScanPads();
        PAD_ScanPads();

        const u32 wpadPressed = WPAD_ButtonsDown(0);
        const u16 gcPressed = PAD_ButtonsDown(0);

        romm::Action action = romm::mapButtonToAction(
            romm::ControlProfile::WiiRemote, mapWpadButton(wpadPressed));
        if (action == romm::Action::None) {
            action = romm::mapButtonToAction(
                romm::ControlProfile::GameCube, mapGcButton(gcPressed));
        }
        if (!startup.finished && action != romm::Action::Quit) {
            action = romm::Action::None;
        }
        if (startup.finished && netReady &&
            action == romm::Action::Select &&
            status.currentView == romm::Status::View::PLATFORMS) {
            status.uiBusy = true;
            drawUi(status);
            std::string indexErr;
            if (!indexSelectedPlatformRoms(status, cfg, httpClient, runtime, indexErr)) {
                status.uiBusy = false;
                status.lastMessage = "ROM index failed: " + indexErr;
                romm::logWarn(status.lastMessage, "API");
                action = romm::Action::None;
                drawUi(status);
            } else {
                status.uiBusy = false;
                romm::logInfo(status.lastMessage, "API");
            }
        }

        const romm::ApplyResult result = romm::applyAction(status, action);
        if (action != romm::Action::None) {
            char wpadHex[16];
            char gcHex[16];
            std::snprintf(wpadHex, sizeof(wpadHex), "%08X", static_cast<unsigned int>(wpadPressed));
            std::snprintf(gcHex, sizeof(gcHex), "%04X", static_cast<unsigned int>(gcPressed));
            romm::logDebug(
                "frame=" + std::to_string(status.uiFrameCounter) +
                " action=" + std::string(romm::actionName(action)) +
                " view=" + std::string(romm::viewName(status.currentView)) +
                " stateChanged=" + std::string(result.stateChanged ? "1" : "0") +
                " keepRunning=" + std::string(result.keepRunning ? "1" : "0") +
                " wpad=0x" + std::string(wpadHex) +
                " gc=0x" + std::string(gcHex),
                "UI");
        }
        if (!result.keepRunning) {
            romm::logInfo("quit requested", "APP");
            break;
        }

        bool startupChanged = false;
        if (!startup.finished) {
            startupChanged = stepStartupSync(status, cfg, httpClient, startup);
            netReady = startup.netReady && startup.stage != StartupSyncState::Stage::Failed;
        }
        status.uiBusy = !startup.finished;

        bool tickChanged = false;
        if (startup.finished && netReady) {
            tickChanged = runRealDownloads(status, cfg, httpClient);
        } else {
            tickChanged = romm::tickDownload(status, 16);
        }
        const bool animateFrame = (status.uiBusy || status.downloadWorkerRunning) &&
                                  ((status.uiFrameCounter % 15u) == 0u);
        if (action != romm::Action::None || result.stateChanged || tickChanged || startupChanged || animateFrame) {
            drawUi(status);
        }
        VIDEO_WaitVSync();
    }

    romm::shutdownLogFile();
    if (gGx.fifo != nullptr) {
        std::free(gGx.fifo);
        gGx.fifo = nullptr;
    }
    return 0;
}
