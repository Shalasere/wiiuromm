#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <vpad/input.h>
#include <whb/log.h>
#include <whb/log_console.h>
#include <whb/proc.h>

#include <string>
#include <vector>

#include "romm/app_core.hpp"
#include "romm/control_schema.hpp"
#include "romm/logger.hpp"

namespace {

romm::LogicalButton mapVpadButton(uint32_t trigger) {
    if (trigger & VPAD_BUTTON_HOME) return romm::LogicalButton::Quit;
    if (trigger & VPAD_BUTTON_PLUS) return romm::LogicalButton::Quit;
    if (trigger & VPAD_BUTTON_UP) return romm::LogicalButton::Up;
    if (trigger & VPAD_BUTTON_DOWN) return romm::LogicalButton::Down;
    if (trigger & VPAD_BUTTON_LEFT) return romm::LogicalButton::Left;
    if (trigger & VPAD_BUTTON_RIGHT) return romm::LogicalButton::Right;
    if (trigger & VPAD_BUTTON_A) return romm::LogicalButton::Confirm;
    if (trigger & VPAD_BUTTON_B) return romm::LogicalButton::Back;
    if (trigger & VPAD_BUTTON_Y) return romm::LogicalButton::Queue;
    if (trigger & VPAD_BUTTON_X) return romm::LogicalButton::StartWork;
    if (trigger & VPAD_BUTTON_MINUS) return romm::LogicalButton::Search;
    if (trigger & VPAD_BUTTON_R) return romm::LogicalButton::Diagnostics;
    if (trigger & VPAD_BUTTON_L) return romm::LogicalButton::Updater;
    return romm::LogicalButton::None;
}

void logStatus(const romm::Status &status) {
    const std::vector<std::string> lines = romm::buildFramedStatusLines(status, true);
    for (const auto &line : lines) {
        WHBLogPrintf("%s", line.c_str());
    }
}

} // namespace

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    WHBProcInit();
    WHBLogConsoleInit();
    romm::initLogFile();
    romm::loadLogLevelFromEnv();
    romm::logInfo("wiiu app launched", "APP");

    WHBLogPrintf("wiiuromm (Wii U target) - shared core state prototype");
    WHBLogPrintf("A=Select B=Back Y=Queue X=Start/Pause -=Search L/R=Updater/Diag +=Quit");
    WHBLogPrintf("ROMS: Left/Right sort | QUEUE: Left remove Right clear A retry-failed");

    romm::Status status = romm::makeDefaultStatus();
    logStatus(status);

    while (WHBProcIsRunning()) {
        status.uiFrameCounter++;
        VPADStatus vpadStatus;
        VPADReadError readError;
        VPADRead(VPAD_CHAN_0, &vpadStatus, 1, &readError);
        if (readError == VPAD_READ_SUCCESS) {
            const romm::Action action = romm::mapButtonToAction(
                romm::ControlProfile::WiiU, mapVpadButton(vpadStatus.trigger));
            const romm::ApplyResult result = romm::applyAction(status, action);
            if (!result.keepRunning) {
                romm::logInfo("quit requested", "APP");
                break;
            }
            if (action != romm::Action::None) {
                romm::logDebug(
                    "frame=" + std::to_string(status.uiFrameCounter) +
                    " action=" + std::string(romm::actionName(action)) +
                    " view=" + std::string(romm::viewName(status.currentView)) +
                    " stateChanged=" + std::string(result.stateChanged ? "1" : "0"),
                    "UI");
            }
            if (action != romm::Action::None && result.stateChanged) {
                logStatus(status);
            }
        }

        if (romm::tickDownload(status, 16)) {
            logStatus(status);
        }

        OSSleepTicks(OSMillisecondsToTicks(16));
    }

    romm::shutdownLogFile();
    WHBLogConsoleFree();
    WHBProcShutdown();
    return 0;
}
