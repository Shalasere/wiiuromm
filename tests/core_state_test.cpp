#include "romm/app_core.hpp"
#include "romm/control_schema.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

void requireTrue(bool cond, const char *msg) {
    if (!cond) {
        std::cerr << "FAILED: " << msg << "\n";
        std::exit(1);
    }
}

void requireActionEq(romm::Action actual, romm::Action expected, const char *msg) {
    if (actual != expected) {
        std::cerr << "FAILED: " << msg
                  << " expected=" << romm::actionName(expected)
                  << " actual=" << romm::actionName(actual) << "\n";
        std::exit(1);
    }
}

bool hasLineWithPrefix(const std::vector<std::string> &lines, const std::string &prefix) {
    for (const auto &line : lines) {
        if (line.rfind(prefix, 0) == 0) return true;
    }
    return false;
}

void goToRoms(romm::Status &status) {
    auto res = romm::applyAction(status, romm::Action::Select);
    requireTrue(res.keepRunning, "Select PLATFORMS should keep running");
    requireTrue(status.currentView == romm::Status::View::ROMS, "Select PLATFORMS should open ROMS");
}

void goToDetailAndQueueFirst(romm::Status &status) {
    goToRoms(status);
    auto res = romm::applyAction(status, romm::Action::Select);
    requireTrue(res.keepRunning, "Select ROMS should keep running");
    requireTrue(status.currentView == romm::Status::View::DETAIL, "Select ROMS should open DETAIL");
    res = romm::applyAction(status, romm::Action::Select);
    requireTrue(res.keepRunning, "Select DETAIL should keep running");
    requireTrue(status.currentView == romm::Status::View::QUEUE, "Select DETAIL should open QUEUE");
}

void tickUntilIdle(romm::Status &status, int maxTicks = 600) {
    for (int i = 0; i < maxTicks && status.downloadWorkerRunning; ++i) {
        (void)romm::tickDownload(status, 16);
    }
}

void testQueueDedupe() {
    romm::Status status = romm::makeDefaultStatus();
    goToDetailAndQueueFirst(status);
    requireTrue(status.downloadQueue.size() == 1, "first enqueue should add one item");
    auto res = romm::applyAction(status, romm::Action::Back);
    requireTrue(res.keepRunning, "Back from queue should keep running");
    requireTrue(status.currentView == romm::Status::View::DETAIL, "Back from queue should return DETAIL");
    res = romm::applyAction(status, romm::Action::Select);
    requireTrue(res.keepRunning, "duplicate enqueue attempt should keep running");
    requireTrue(status.downloadQueue.size() == 1, "duplicate enqueue should not grow queue");
    requireTrue(status.diagnostics.duplicateBlockedCount == 1, "duplicate counter should increment");
}

void testDownloadCompletesAndMovesToHistory() {
    romm::Status status = romm::makeDefaultStatus();
    goToDetailAndQueueFirst(status);
    auto res = romm::applyAction(status, romm::Action::StartDownload);
    requireTrue(res.keepRunning, "start download should keep running");
    requireTrue(status.currentView == romm::Status::View::DOWNLOADING, "start download should open DOWNLOADING");
    requireTrue(status.downloadWorkerRunning, "worker should be running");
    tickUntilIdle(status);
    requireTrue(!status.downloadWorkerRunning, "worker should stop after completion");
    requireTrue(status.downloadQueue.empty(), "queue should be empty after completion");
    requireTrue(status.downloadHistory.size() == 1, "history should contain completed item");
    requireTrue(status.diagnostics.completedCount == 1, "completed counter should increment");
}

void testCompletedHistoryBlocksRequeue() {
    romm::Status status = romm::makeDefaultStatus();
    goToDetailAndQueueFirst(status);
    (void)romm::applyAction(status, romm::Action::StartDownload);
    tickUntilIdle(status);
    requireTrue(status.downloadHistory.size() == 1, "history should contain one completed item");
    auto res = romm::applyAction(status, romm::Action::Back);
    requireTrue(res.keepRunning, "back from queue should keep running");
    requireTrue(status.currentView == romm::Status::View::DETAIL, "back from queue should return detail");
    res = romm::applyAction(status, romm::Action::Select);
    requireTrue(res.keepRunning, "requeue attempt should keep running");
    requireTrue(status.downloadQueue.empty(), "completed game should not requeue");
}

void testQuitAction() {
    romm::Status status = romm::makeDefaultStatus();
    const auto res = romm::applyAction(status, romm::Action::Quit);
    requireTrue(!res.keepRunning, "quit action should request app exit");
}

void testStandardControlSchemas() {
    requireActionEq(
        romm::mapButtonToAction(romm::ControlProfile::WiiU, romm::LogicalButton::Confirm),
        romm::Action::Select,
        "Wii U Confirm should map to Select");
    requireActionEq(
        romm::mapButtonToAction(romm::ControlProfile::WiiU, romm::LogicalButton::Back),
        romm::Action::Back,
        "Wii U Back should map to Back");
    requireActionEq(
        romm::mapButtonToAction(romm::ControlProfile::WiiRemote, romm::LogicalButton::Confirm),
        romm::Action::Select,
        "Wii Remote Confirm should map to Select");
    requireActionEq(
        romm::mapButtonToAction(romm::ControlProfile::WiiRemote, romm::LogicalButton::Back),
        romm::Action::Back,
        "Wii Remote Back should map to Back");
    requireActionEq(
        romm::mapButtonToAction(romm::ControlProfile::GameCube, romm::LogicalButton::Confirm),
        romm::Action::Select,
        "GameCube Confirm should map to Select");
    requireActionEq(
        romm::mapButtonToAction(romm::ControlProfile::GameCube, romm::LogicalButton::Back),
        romm::Action::Back,
        "GameCube Back should map to Back");
}

void testSearchAndSortCycle() {
    romm::Status status = romm::makeDefaultStatus();
    goToRoms(status);
    (void)romm::applyAction(status, romm::Action::OpenSearch);
    requireTrue(status.searchQuery == "Mario", "search should cycle to Mario");
    (void)romm::applyAction(status, romm::Action::Right);
    requireTrue(status.romSort == romm::RomSortMode::SizeAsc, "Right should cycle sort forward");
    (void)romm::applyAction(status, romm::Action::Left);
    requireTrue(status.romSort == romm::RomSortMode::TitleAsc, "Left should cycle sort backward");
    requireTrue(status.diagnostics.searchCount == 1, "search counter should increment");
}

void testNoSearchResultBlocksDetail() {
    romm::Status status = romm::makeDefaultStatus();
    goToRoms(status);
    (void)romm::applyAction(status, romm::Action::OpenSearch); // Mario
    (void)romm::applyAction(status, romm::Action::OpenSearch); // Zelda
    (void)romm::applyAction(status, romm::Action::OpenSearch); // Xeno
    auto res = romm::applyAction(status, romm::Action::Select);
    requireTrue(res.keepRunning, "select should keep running");
    requireTrue(status.currentView == romm::Status::View::ROMS,
                "select with no visible ROM should remain in ROMS");
}

void testQueueRemoveAndClear() {
    romm::Status status = romm::makeDefaultStatus();
    goToDetailAndQueueFirst(status);
    auto res = romm::applyAction(status, romm::Action::Back); // detail
    requireTrue(res.keepRunning, "back should keep running");
    res = romm::applyAction(status, romm::Action::Back); // roms
    requireTrue(res.keepRunning, "back should keep running");
    (void)romm::applyAction(status, romm::Action::Down); // next rom
    (void)romm::applyAction(status, romm::Action::Select); // detail
    (void)romm::applyAction(status, romm::Action::Select); // enqueue
    requireTrue(status.downloadQueue.size() == 2, "queue should have two items");
    res = romm::applyAction(status, romm::Action::Left);
    requireTrue(res.keepRunning, "remove should keep running");
    requireTrue(status.downloadQueue.size() == 1, "left in queue should remove selected");
    status.downloadQueue[0].state = romm::QueueState::Failed;
    res = romm::applyAction(status, romm::Action::Right);
    requireTrue(res.keepRunning, "clear should keep running");
    requireTrue(status.downloadQueue.empty(), "right in queue should clear pending/failed");
}

void testPauseResumeAndFailureRetry() {
    romm::Status status = romm::makeDefaultStatus();
    goToRoms(status);
    (void)romm::applyAction(status, romm::Action::Right); // SizeAsc
    (void)romm::applyAction(status, romm::Action::Right); // SizeDesc (largest first)
    (void)romm::applyAction(status, romm::Action::Select); // detail
    (void)romm::applyAction(status, romm::Action::Select); // enqueue
    (void)romm::applyAction(status, romm::Action::StartDownload);
    requireTrue(status.downloadWorkerRunning, "worker should start");
    const uint8_t p0 = status.downloadQueue[0].progressPercent;
    (void)romm::tickDownload(status, 160);
    const uint8_t p1 = status.downloadQueue[0].progressPercent;
    requireTrue(p1 > p0, "download should progress while running");
    (void)romm::applyAction(status, romm::Action::StartDownload); // pause
    requireTrue(status.downloadPaused, "start action in downloading should pause");
    const uint8_t pausedProgress = status.downloadQueue[0].progressPercent;
    (void)romm::tickDownload(status, 1000);
    requireTrue(status.downloadQueue[0].progressPercent == pausedProgress,
                "progress should not change while paused");
    (void)romm::applyAction(status, romm::Action::StartDownload); // resume
    requireTrue(!status.downloadPaused, "second start action should resume");
    for (int i = 0; i < 500 && status.downloadWorkerRunning; ++i) {
        (void)romm::tickDownload(status, 16);
    }
    requireTrue(!status.downloadWorkerRunning, "worker should stop after failure-only queue");
    requireTrue(status.downloadQueue.size() == 1, "failed item should remain in queue");
    requireTrue(status.downloadQueue[0].state == romm::QueueState::Failed,
                "first attempt for large ROM should fail");
    requireTrue(status.diagnostics.failedCount == 1, "failed counter should increment");
    (void)romm::applyAction(status, romm::Action::Select); // retry failed
    requireTrue(status.downloadQueue[0].state == romm::QueueState::Pending, "retry should reset to pending");
    (void)romm::applyAction(status, romm::Action::StartDownload);
    tickUntilIdle(status);
    requireTrue(status.downloadQueue.empty(), "retry path should complete and clear queue");
    requireTrue(status.downloadHistory.size() == 1, "retry completion should move to history");
}

void testUpdaterFlow() {
    romm::Status status = romm::makeDefaultStatus();
    (void)romm::applyAction(status, romm::Action::OpenUpdater);
    requireTrue(status.currentView == romm::Status::View::UPDATER, "open updater should switch view");
    (void)romm::applyAction(status, romm::Action::Select); // check
    requireTrue(status.updaterState == romm::UpdaterState::Available, "check should mark update available");
    (void)romm::applyAction(status, romm::Action::Select); // apply
    requireTrue(status.updaterState == romm::UpdaterState::Applied, "apply should mark updater applied");
    requireTrue(status.currentVersion == status.availableVersion, "current version should update");
}

void testStatusMetadataLines() {
    romm::Status status = romm::makeDefaultStatus();
    const std::vector<std::string> lines = romm::buildStatusLines(status);
    requireTrue(hasLineWithPrefix(lines, "Screen       : Platform Browser"), "screen title line missing");
    requireTrue(hasLineWithPrefix(lines, "Search       : "), "search line missing");
    requireTrue(hasLineWithPrefix(lines, "Sort         : "), "sort line missing");
    requireTrue(hasLineWithPrefix(lines, "Updater      : "), "updater line missing");
    requireTrue(hasLineWithPrefix(lines, "Diag         : "), "diagnostics line missing");
}

} // namespace

int main() {
    testQueueDedupe();
    testDownloadCompletesAndMovesToHistory();
    testCompletedHistoryBlocksRequeue();
    testQuitAction();
    testStandardControlSchemas();
    testSearchAndSortCycle();
    testNoSearchResultBlocksDetail();
    testQueueRemoveAndClear();
    testPauseResumeAndFailureRetry();
    testUpdaterFlow();
    testStatusMetadataLines();
    std::cout << "core_state_test: all checks passed\n";
    return 0;
}
