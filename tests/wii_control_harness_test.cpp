#include "romm/app_core.hpp"
#include "romm/control_schema.hpp"

#include <cstdlib>
#include <iostream>

namespace {

void requireTrue(bool cond, const char *msg) {
    if (!cond) {
        std::cerr << "FAILED: " << msg << "\n";
        std::exit(1);
    }
}

void press(romm::Status &status, romm::ControlProfile profile, romm::LogicalButton button) {
    const romm::Action action = romm::mapButtonToAction(profile, button);
    const romm::ApplyResult res = romm::applyAction(status, action);
    requireTrue(res.keepRunning, "harness action unexpectedly requested quit");
}

void tickUntilIdle(romm::Status &status, int maxTicks = 1200) {
    for (int i = 0; i < maxTicks && status.downloadWorkerRunning; ++i) {
        (void)romm::tickDownload(status, 16);
    }
}

void testWiiRemoteNavQueueDownloadFlow() {
    romm::Status status = romm::makeDefaultStatus();
    requireTrue(status.currentView == romm::Status::View::PLATFORMS, "initial view should be platforms");

    press(status, romm::ControlProfile::WiiRemote, romm::LogicalButton::Confirm); // ROMS
    requireTrue(status.currentView == romm::Status::View::ROMS, "confirm should open roms");
    press(status, romm::ControlProfile::WiiRemote, romm::LogicalButton::Confirm); // DETAIL
    requireTrue(status.currentView == romm::Status::View::DETAIL, "confirm should open detail");
    press(status, romm::ControlProfile::WiiRemote, romm::LogicalButton::Confirm); // QUEUE + enqueue
    requireTrue(status.currentView == romm::Status::View::QUEUE, "confirm should open queue");
    requireTrue(status.downloadQueue.size() == 1, "queue should contain one rom");

    press(status, romm::ControlProfile::WiiRemote, romm::LogicalButton::StartWork); // start
    requireTrue(status.currentView == romm::Status::View::DOWNLOADING, "start should open downloading view");
    requireTrue(status.downloadWorkerRunning, "download worker should be running");
    tickUntilIdle(status);
    requireTrue(!status.downloadWorkerRunning, "download worker should finish");
    requireTrue(status.downloadHistory.size() == 1, "history should contain completed rom");
}

void testSearchAndViewsWithControllerProfiles() {
    romm::Status status = romm::makeDefaultStatus();
    press(status, romm::ControlProfile::WiiRemote, romm::LogicalButton::Confirm); // ROMS
    press(status, romm::ControlProfile::WiiRemote, romm::LogicalButton::Search);  // cycle query
    requireTrue(status.searchQuery == "Mario", "wii remote search should cycle query");

    press(status, romm::ControlProfile::GameCube, romm::LogicalButton::Back); // PLATFORMS
    requireTrue(status.currentView == romm::Status::View::PLATFORMS, "back should return to platforms");

    press(status, romm::ControlProfile::GameCube, romm::LogicalButton::Diagnostics);
    requireTrue(status.currentView == romm::Status::View::DIAGNOSTICS, "gamecube diagnostics should open diagnostics");
    press(status, romm::ControlProfile::GameCube, romm::LogicalButton::Back);
    requireTrue(status.currentView == romm::Status::View::PLATFORMS, "back should return to platforms");

    press(status, romm::ControlProfile::GameCube, romm::LogicalButton::Updater);
    requireTrue(status.currentView == romm::Status::View::UPDATER, "gamecube updater should open updater");
}

} // namespace

int main() {
    testWiiRemoteNavQueueDownloadFlow();
    testSearchAndViewsWithControllerProfiles();
    std::cout << "wii_control_harness_test: all checks passed\n";
    return 0;
}
