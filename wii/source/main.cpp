#include <gccore.h>
#include <ogc/pad.h>
#include <wiiuse/wpad.h>

#include <cstdio>
#include <string>
#include <vector>

#include "romm/app_core.hpp"
#include "romm/control_schema.hpp"

namespace {

romm::LogicalButton mapWpadButton(u32 pressed) {
    if (pressed & WPAD_BUTTON_HOME) return romm::LogicalButton::Quit;
    if (pressed & WPAD_BUTTON_PLUS) return romm::LogicalButton::Quit;
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

void initVideoConsole() {
    VIDEO_Init();

    GXRModeObj *rmode = VIDEO_GetPreferredMode(nullptr);
    void *xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));

    console_init(xfb, 20, 20, rmode->fbWidth, rmode->xfbHeight,
                 rmode->fbWidth * VI_DISPLAY_PIX_SZ);

    VIDEO_Configure(rmode);
    VIDEO_SetNextFramebuffer(xfb);
    VIDEO_SetBlack(false);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (rmode->viTVMode & VI_NON_INTERLACE) {
        VIDEO_WaitVSync();
    }
}

void drawUi(const romm::Status &status) {
    std::printf("\x1b[2J\x1b[H");
    std::printf("wiiuromm (Wii/vWii) - shared core state prototype\n");
    std::printf("Wii Remote: A=Select B=Back 1=Queue 2=Start/Pause -=Search +=Quit HOME=Quit\n");
    std::printf("GameCube  : A=Select B=Back Y=Queue X=Start/Pause L=Updater Z=Diag R=Search START=Quit\n");
    std::printf("ROMS: Left/Right sort | QUEUE: Left remove Right clear A retry-failed\n\n");

    const std::vector<std::string> lines = romm::buildStatusLines(status, true);
    for (const auto &line : lines) {
        std::printf("%s\n", line.c_str());
    }
}

} // namespace

int main() {
    initVideoConsole();
    WPAD_Init();
    PAD_Init();

    romm::Status status = romm::makeDefaultStatus();
    drawUi(status);

    while (true) {
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

        const romm::ApplyResult result = romm::applyAction(status, action);
        if (!result.keepRunning) {
            break;
        }

        const bool tickChanged = romm::tickDownload(status, 16);
        if (action != romm::Action::None || result.stateChanged || tickChanged) {
            drawUi(status);
        }
        VIDEO_WaitVSync();
    }

    return 0;
}
