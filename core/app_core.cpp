#include "romm/app_core.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>

namespace romm {
namespace {

template <typename T>
int clampIndex(int idx, const std::vector<T> &items) {
    if (items.empty()) return 0;
    if (idx < 0) return 0;
    const int maxIdx = static_cast<int>(items.size()) - 1;
    if (idx > maxIdx) return maxIdx;
    return idx;
}

std::string toLowerCopy(const std::string &input) {
    std::string out = input;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

bool containsCaseInsensitive(const std::string &haystack, const std::string &needle) {
    if (needle.empty()) return true;
    const std::string h = toLowerCopy(haystack);
    const std::string n = toLowerCopy(needle);
    return h.find(n) != std::string::npos;
}

const PlatformEntry *selectedPlatform(const Status &status) {
    if (status.platforms.empty()) return nullptr;
    const int idx = clampIndex(status.selectedPlatformIndex, status.platforms);
    return &status.platforms[static_cast<size_t>(idx)];
}

std::vector<int> visibleRomIndices(const Status &status, const PlatformEntry &platform) {
    std::vector<int> indices;
    indices.reserve(platform.roms.size());
    for (size_t i = 0; i < platform.roms.size(); ++i) {
        const RomEntry &rom = platform.roms[i];
        if (containsCaseInsensitive(rom.title, status.searchQuery) ||
            containsCaseInsensitive(rom.subtitle, status.searchQuery) ||
            containsCaseInsensitive(platform.name, status.searchQuery)) {
            indices.push_back(static_cast<int>(i));
        }
    }

    auto cmpTitle = [&platform](int a, int b) {
        if (platform.roms[static_cast<size_t>(a)].title ==
            platform.roms[static_cast<size_t>(b)].title) {
            return a < b;
        }
        return platform.roms[static_cast<size_t>(a)].title <
               platform.roms[static_cast<size_t>(b)].title;
    };
    auto cmpSizeAsc = [&platform, &cmpTitle](int a, int b) {
        const uint32_t sa = platform.roms[static_cast<size_t>(a)].sizeMb;
        const uint32_t sb = platform.roms[static_cast<size_t>(b)].sizeMb;
        if (sa == sb) return cmpTitle(a, b);
        return sa < sb;
    };
    auto cmpSizeDesc = [&platform, &cmpTitle](int a, int b) {
        const uint32_t sa = platform.roms[static_cast<size_t>(a)].sizeMb;
        const uint32_t sb = platform.roms[static_cast<size_t>(b)].sizeMb;
        if (sa == sb) return cmpTitle(a, b);
        return sa > sb;
    };

    switch (status.romSort) {
        case RomSortMode::TitleAsc:
            std::sort(indices.begin(), indices.end(), cmpTitle);
            break;
        case RomSortMode::SizeAsc:
            std::sort(indices.begin(), indices.end(), cmpSizeAsc);
            break;
        case RomSortMode::SizeDesc:
            std::sort(indices.begin(), indices.end(), cmpSizeDesc);
            break;
        default:
            break;
    }
    return indices;
}

void clampRomSelection(Status &status) {
    const PlatformEntry *platform = selectedPlatform(status);
    if (platform == nullptr) {
        status.selectedRomIndex = 0;
        return;
    }
    const std::vector<int> visible = visibleRomIndices(status, *platform);
    status.selectedRomIndex = clampIndex(status.selectedRomIndex, visible);
}

const RomEntry *selectedRom(const Status &status) {
    const PlatformEntry *platform = selectedPlatform(status);
    if (platform == nullptr || platform->roms.empty()) return nullptr;
    const std::vector<int> visible = visibleRomIndices(status, *platform);
    if (visible.empty()) return nullptr;
    const int idx = clampIndex(status.selectedRomIndex, visible);
    const int romIdx = visible[static_cast<size_t>(idx)];
    return &platform->roms[static_cast<size_t>(romIdx)];
}

bool existsInQueueOrCompletedHistory(const Status &status, const std::string &romId) {
    for (const auto &item : status.downloadQueue) {
        if (item.rom.id == romId) return true;
    }
    for (const auto &item : status.downloadHistory) {
        if (item.rom.id == romId && item.state == QueueState::Completed) return true;
    }
    return false;
}

bool hasPendingItems(const Status &status) {
    for (const auto &item : status.downloadQueue) {
        if (item.state == QueueState::Pending) return true;
    }
    return false;
}

bool hasFailedItems(const Status &status) {
    for (const auto &item : status.downloadQueue) {
        if (item.state == QueueState::Failed) return true;
    }
    return false;
}

void beginDownload(Status &status, int queueIndex) {
    status.activeDownloadIndex = queueIndex;
    if (queueIndex < 0 || queueIndex >= static_cast<int>(status.downloadQueue.size())) {
        status.downloadWorkerRunning = false;
        status.activeDownloadIndex = -1;
        return;
    }

    QueueItem &item = status.downloadQueue[static_cast<size_t>(queueIndex)];
    item.state = QueueState::Downloading;
    item.error.clear();
    item.attempts = static_cast<uint8_t>(std::min<int>(255, static_cast<int>(item.attempts) + 1));
    if (item.progressPercent == 0) item.progressPercent = 1;
}

bool beginNextPendingDownload(Status &status) {
    for (size_t i = 0; i < status.downloadQueue.size(); ++i) {
        if (status.downloadQueue[i].state == QueueState::Pending) {
            beginDownload(status, static_cast<int>(i));
            return true;
        }
    }

    status.downloadWorkerRunning = false;
    status.downloadPaused = false;
    status.activeDownloadIndex = -1;
    status.currentView = Status::View::QUEUE;

    if (status.downloadQueue.empty()) {
        status.lastMessage = "All downloads complete.";
    } else if (hasFailedItems(status)) {
        status.lastMessage = "Pending complete; failed items remain.";
    } else {
        status.lastMessage = "Queue idle.";
    }
    return false;
}

void moveSelection(Status &status, int delta) {
    if (delta == 0) return;

    if (status.currentView == Status::View::PLATFORMS) {
        status.selectedPlatformIndex = clampIndex(
            status.selectedPlatformIndex + delta, status.platforms);
        status.selectedRomIndex = 0;
        return;
    }

    if (status.currentView == Status::View::ROMS) {
        const PlatformEntry *platform = selectedPlatform(status);
        if (platform == nullptr) return;
        const std::vector<int> visible = visibleRomIndices(status, *platform);
        status.selectedRomIndex = clampIndex(status.selectedRomIndex + delta, visible);
        return;
    }

    if (status.currentView == Status::View::QUEUE || status.currentView == Status::View::DOWNLOADING) {
        status.selectedQueueIndex = clampIndex(status.selectedQueueIndex + delta, status.downloadQueue);
    }
}

void cycleSort(Status &status, int delta) {
    int mode = static_cast<int>(status.romSort);
    mode = (mode + delta) % 3;
    if (mode < 0) mode += 3;
    status.romSort = static_cast<RomSortMode>(mode);
    clampRomSelection(status);
}

void cycleSearchQuery(Status &status) {
    static const std::vector<std::string> queries = {"", "Mario", "Zelda", "Xeno"};
    size_t idx = 0;
    for (size_t i = 0; i < queries.size(); ++i) {
        if (queries[i] == status.searchQuery) {
            idx = i;
            break;
        }
    }
    idx = (idx + 1) % queries.size();
    status.searchQuery = queries[idx];
    status.diagnostics.searchCount++;
    status.selectedRomIndex = 0;
    if (status.searchQuery.empty()) {
        status.lastMessage = "Search cleared.";
    } else {
        status.lastMessage = "Search filter: " + status.searchQuery;
    }
}

bool enqueueSelectedRom(Status &status) {
    const RomEntry *rom = selectedRom(status);
    if (rom == nullptr) {
        status.lastMessage = "No ROM selected.";
        return false;
    }

    if (existsInQueueOrCompletedHistory(status, rom->id)) {
        status.diagnostics.duplicateBlockedCount++;
        status.lastMessage = "ROM already queued or completed this session.";
        return false;
    }

    QueueItem item;
    item.rom = *rom;
    item.state = QueueState::Pending;
    status.downloadQueue.push_back(item);
    status.selectedQueueIndex = static_cast<int>(status.downloadQueue.size()) - 1;
    status.prevQueueView = Status::View::DETAIL;
    status.currentView = Status::View::QUEUE;
    status.diagnostics.enqueueCount++;
    status.lastMessage = "Added to queue: " + rom->title;
    return true;
}

bool removeSelectedQueueItem(Status &status) {
    if (status.downloadQueue.empty()) {
        status.lastMessage = "Queue is empty.";
        return false;
    }
    const int idx = clampIndex(status.selectedQueueIndex, status.downloadQueue);
    if (status.downloadWorkerRunning && idx == status.activeDownloadIndex) {
        status.lastMessage = "Cannot remove active download.";
        return false;
    }
    const std::string title = status.downloadQueue[static_cast<size_t>(idx)].rom.title;
    status.downloadQueue.erase(status.downloadQueue.begin() + idx);
    if (status.activeDownloadIndex > idx) status.activeDownloadIndex--;
    if (status.selectedQueueIndex >= static_cast<int>(status.downloadQueue.size())) {
        status.selectedQueueIndex = status.downloadQueue.empty()
            ? 0
            : static_cast<int>(status.downloadQueue.size()) - 1;
    }
    status.lastMessage = "Removed from queue: " + title;
    return true;
}

bool clearPendingAndFailed(Status &status) {
    if (status.downloadWorkerRunning) {
        status.lastMessage = "Pause/finish downloads before clearing queue.";
        return false;
    }
    const size_t before = status.downloadQueue.size();
    status.downloadQueue.erase(
        std::remove_if(status.downloadQueue.begin(), status.downloadQueue.end(), [](const QueueItem &item) {
            return item.state == QueueState::Pending || item.state == QueueState::Failed;
        }),
        status.downloadQueue.end());
    const size_t removed = before - status.downloadQueue.size();
    status.selectedQueueIndex = status.downloadQueue.empty()
        ? 0
        : clampIndex(status.selectedQueueIndex, status.downloadQueue);
    if (removed == 0) {
        status.lastMessage = "No pending/failed items to clear.";
        return false;
    }
    status.lastMessage = "Cleared " + std::to_string(removed) + " queue item(s).";
    return true;
}

bool retrySelectedFailed(Status &status) {
    if (status.downloadQueue.empty()) {
        status.lastMessage = "Queue is empty.";
        return false;
    }
    const int idx = clampIndex(status.selectedQueueIndex, status.downloadQueue);
    QueueItem &item = status.downloadQueue[static_cast<size_t>(idx)];
    if (item.state != QueueState::Failed) {
        status.lastMessage = "Selected item is not failed.";
        return false;
    }
    item.state = QueueState::Pending;
    item.progressPercent = 0;
    item.error.clear();
    status.lastMessage = "Retry queued: " + item.rom.title;
    return true;
}

void runUpdaterCheck(Status &status) {
    status.updaterState = UpdaterState::Checking;
    if (status.currentVersion == status.availableVersion) {
        status.updaterState = UpdaterState::Applied;
        status.lastMessage = "Already on latest version: " + status.currentVersion;
        return;
    }
    status.updaterState = UpdaterState::Available;
    status.lastMessage = "Update available: " + status.availableVersion;
}

bool startDownloads(Status &status) {
    if (status.downloadQueue.empty()) {
        status.lastMessage = "Queue is empty.";
        return false;
    }
    if (status.downloadWorkerRunning) {
        status.currentView = Status::View::DOWNLOADING;
        status.lastMessage = "Download already running.";
        return false;
    }

    for (auto &item : status.downloadQueue) {
        if (item.state == QueueState::Downloading) {
            item.state = QueueState::Pending;
        }
        if (item.progressPercent >= 100 && item.state != QueueState::Completed) {
            item.progressPercent = 0;
        }
    }

    if (!hasPendingItems(status)) {
        if (hasFailedItems(status)) {
            status.lastMessage = "No pending items. Retry failed item with Select.";
        } else {
            status.lastMessage = "Queue has no startable items.";
        }
        return false;
    }

    status.downloadWorkerRunning = true;
    status.downloadPaused = false;
    status.currentView = Status::View::DOWNLOADING;
    status.lastMessage = "Started download queue.";
    return beginNextPendingDownload(status);
}

} // namespace

Status makeDefaultStatus() {
    Status status;

    status.platforms = {
        {"wii", "Wii", {
            {"wi_001", "Super Mario Galaxy 2", "Nintendo EAD", "", 4447},
            {"wi_002", "Donkey Kong Country Returns", "Retro Studios", "", 3366},
            {"wi_003", "Metroid Prime Trilogy", "Retro Studios", "", 23100},
        }, "wii"},
        {"wiiu", "Wii U", {
            {"wu_001", "Mario Kart 8", "Nintendo EAD", "", 5870},
            {"wu_002", "The Legend of Zelda: Breath of the Wild", "Nintendo EPD", "", 13800},
            {"wu_003", "Xenoblade Chronicles X", "Monolith Soft", "", 22900},
        }, "wiiu"},
        {"switch", "Switch", {
            {"sw_001", "Super Mario Odyssey", "Nintendo EPD", "", 5800},
            {"sw_002", "Metroid Dread", "MercurySteam", "", 4500},
            {"sw_003", "Mario Kart 8 Deluxe", "Nintendo EPD", "", 11200},
        }, "switch"},
    };

    status.lastMessage = "Ready.";
    return status;
}

ApplyResult applyAction(Status &status, Action action) {
    ApplyResult result;
    const Status::View oldView = status.currentView;

    if (action != Action::None) status.diagnostics.inputCount++;

    switch (action) {
        case Action::None:
            return result;
        case Action::Quit:
            result.keepRunning = false;
            result.stateChanged = true;
            break;
        case Action::Up:
            moveSelection(status, -1);
            result.stateChanged = true;
            break;
        case Action::Down:
            moveSelection(status, 1);
            result.stateChanged = true;
            break;
        case Action::Left:
            if (status.currentView == Status::View::ROMS) {
                cycleSort(status, -1);
                status.lastMessage = std::string("Sort: ") + romSortModeName(status.romSort);
                result.stateChanged = true;
            } else if (status.currentView == Status::View::QUEUE) {
                result.stateChanged = removeSelectedQueueItem(status);
            }
            break;
        case Action::Right:
            if (status.currentView == Status::View::ROMS) {
                cycleSort(status, 1);
                status.lastMessage = std::string("Sort: ") + romSortModeName(status.romSort);
                result.stateChanged = true;
            } else if (status.currentView == Status::View::QUEUE) {
                result.stateChanged = clearPendingAndFailed(status);
            }
            break;
        case Action::Select:
            if (status.currentView == Status::View::PLATFORMS) {
                status.currentView = Status::View::ROMS;
                status.selectedRomIndex = 0;
                status.lastMessage = "Opened ROM list.";
                result.stateChanged = true;
            } else if (status.currentView == Status::View::ROMS) {
                if (selectedRom(status) == nullptr) {
                    status.lastMessage = "No ROM matches current search.";
                    result.stateChanged = true;
                } else {
                    status.currentView = Status::View::DETAIL;
                    status.lastMessage = "Opened ROM detail.";
                    result.stateChanged = true;
                }
            } else if (status.currentView == Status::View::DETAIL) {
                const bool queued = enqueueSelectedRom(status);
                result.stateChanged = true;
                if (!queued) status.currentView = Status::View::DETAIL;
            } else if (status.currentView == Status::View::QUEUE) {
                result.stateChanged = retrySelectedFailed(status);
            } else if (status.currentView == Status::View::DIAGNOSTICS) {
                status.lastMessage = "Diagnostics snapshot exported to log.";
                result.stateChanged = true;
            } else if (status.currentView == Status::View::UPDATER) {
                if (status.updaterState == UpdaterState::Idle ||
                    status.updaterState == UpdaterState::Checking) {
                    runUpdaterCheck(status);
                } else if (status.updaterState == UpdaterState::Available) {
                    status.updaterState = UpdaterState::Applying;
                    status.currentVersion = status.availableVersion;
                    status.updaterState = UpdaterState::Applied;
                    status.lastMessage = "Updated to version " + status.currentVersion;
                } else {
                    status.lastMessage = "Updater idle; already current.";
                }
                result.stateChanged = true;
            }
            break;
        case Action::OpenSearch:
            if (status.currentView == Status::View::ROMS) {
                cycleSearchQuery(status);
                result.stateChanged = true;
            }
            break;
        case Action::OpenDiagnostics:
            if (status.currentView == Status::View::PLATFORMS) {
                status.prevDiagnosticsView = status.currentView;
                status.currentView = Status::View::DIAGNOSTICS;
                status.lastMessage = "Opened diagnostics.";
                result.stateChanged = true;
            } else if (status.currentView == Status::View::DIAGNOSTICS) {
                status.lastMessage = "Diagnostics refresh complete.";
                result.stateChanged = true;
            }
            break;
        case Action::OpenUpdater:
            if (status.currentView == Status::View::PLATFORMS) {
                status.prevUpdaterView = status.currentView;
                status.currentView = Status::View::UPDATER;
                status.lastMessage = "Opened updater.";
                result.stateChanged = true;
            } else if (status.currentView == Status::View::UPDATER) {
                runUpdaterCheck(status);
                result.stateChanged = true;
            }
            break;
        case Action::OpenQueue:
            if (status.currentView != Status::View::QUEUE &&
                status.currentView != Status::View::DOWNLOADING) {
                status.prevQueueView = status.currentView;
            }
            status.currentView = Status::View::QUEUE;
            status.lastMessage = "Opened queue.";
            result.stateChanged = true;
            break;
        case Action::Back:
            if (status.currentView == Status::View::ROMS) {
                status.currentView = Status::View::PLATFORMS;
            } else if (status.currentView == Status::View::DETAIL) {
                status.currentView = Status::View::ROMS;
            } else if (status.currentView == Status::View::QUEUE) {
                status.currentView = status.prevQueueView;
            } else if (status.currentView == Status::View::DOWNLOADING) {
                status.currentView = Status::View::QUEUE;
            } else if (status.currentView == Status::View::DIAGNOSTICS) {
                status.currentView = status.prevDiagnosticsView;
            } else if (status.currentView == Status::View::UPDATER) {
                status.currentView = status.prevUpdaterView;
            } else if (status.currentView == Status::View::ERROR) {
                status.currentView = Status::View::PLATFORMS;
            }
            status.lastMessage = "Back.";
            result.stateChanged = true;
            break;
        case Action::StartDownload:
            if (status.currentView == Status::View::QUEUE) {
                (void)startDownloads(status);
                result.stateChanged = true;
            } else if (status.currentView == Status::View::DOWNLOADING &&
                       status.downloadWorkerRunning) {
                status.downloadPaused = !status.downloadPaused;
                status.lastMessage = status.downloadPaused ? "Downloads paused." : "Downloads resumed.";
                result.stateChanged = true;
            } else {
                status.lastMessage = "Open queue first to start downloads.";
                result.stateChanged = true;
            }
            break;
    }

    result.viewChanged = (oldView != status.currentView);
    return result;
}

bool tickDownload(Status &status, uint32_t elapsedMs) {
    if (!status.downloadWorkerRunning) return false;
    if (status.downloadPaused) return false;

    if (status.activeDownloadIndex < 0 ||
        status.activeDownloadIndex >= static_cast<int>(status.downloadQueue.size())) {
        return beginNextPendingDownload(status);
    }

    QueueItem &item = status.downloadQueue[static_cast<size_t>(status.activeDownloadIndex)];
    if (item.state != QueueState::Downloading) item.state = QueueState::Downloading;

    const uint8_t step = static_cast<uint8_t>(std::max<uint32_t>(1, elapsedMs / 80));
    const uint16_t nextProgress = static_cast<uint16_t>(item.progressPercent) + step;
    item.progressPercent = static_cast<uint8_t>(std::min<uint16_t>(100, nextProgress));

    if (item.rom.sizeMb >= 20000 && item.attempts == 1 && item.progressPercent >= 60) {
        item.state = QueueState::Failed;
        item.error = "Simulated timeout";
        status.diagnostics.failedCount++;
        status.lastMessage = "Failed: " + item.rom.title + " (retry with Select)";
        status.activeDownloadIndex = -1;
        (void)beginNextPendingDownload(status);
        return true;
    }

    if (item.progressPercent < 100) return true;

    item.state = QueueState::Completed;
    status.downloadHistory.push_back(item);
    status.diagnostics.completedCount++;
    status.lastMessage = "Completed: " + item.rom.title;
    status.downloadQueue.erase(status.downloadQueue.begin() + status.activeDownloadIndex);

    if (status.selectedQueueIndex >= static_cast<int>(status.downloadQueue.size())) {
        status.selectedQueueIndex = status.downloadQueue.empty()
            ? 0
            : static_cast<int>(status.downloadQueue.size()) - 1;
    }

    status.activeDownloadIndex = -1;
    if (status.downloadQueue.empty()) {
        status.downloadWorkerRunning = false;
        status.downloadPaused = false;
        status.currentView = Status::View::QUEUE;
        status.lastMessage = "All downloads complete.";
        return true;
    }
    (void)beginNextPendingDownload(status);
    return true;
}

const char *viewName(Status::View view) {
    switch (view) {
        case Status::View::PLATFORMS: return "PLATFORMS";
        case Status::View::ROMS: return "ROMS";
        case Status::View::DETAIL: return "DETAIL";
        case Status::View::QUEUE: return "QUEUE";
        case Status::View::DOWNLOADING: return "DOWNLOADING";
        case Status::View::DIAGNOSTICS: return "DIAGNOSTICS";
        case Status::View::UPDATER: return "UPDATER";
        case Status::View::ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

const char *screenTitle(Status::View view) {
    switch (view) {
        case Status::View::PLATFORMS: return "Platform Browser";
        case Status::View::ROMS: return "ROM Browser";
        case Status::View::DETAIL: return "ROM Detail";
        case Status::View::QUEUE: return "Queue";
        case Status::View::DOWNLOADING: return "Downloading";
        case Status::View::DIAGNOSTICS: return "Diagnostics";
        case Status::View::UPDATER: return "Updater";
        case Status::View::ERROR: return "Error";
        default: return "Unknown";
    }
}

const char *screenHint(Status::View view) {
    switch (view) {
        case Status::View::PLATFORMS: return "A: open platform, R/Z: diagnostics, L: updater";
        case Status::View::ROMS: return "A: detail, B: back, -/R: search, Left/Right: sort";
        case Status::View::DETAIL: return "A: add to queue, B: back, Y/1: queue";
        case Status::View::QUEUE: return "A: retry failed, Left: remove, Right: clear, X/2: start";
        case Status::View::DOWNLOADING: return "X/2: pause/resume, B: queue, HOME/+ or START: quit";
        case Status::View::DIAGNOSTICS: return "A: export summary, B: back";
        case Status::View::UPDATER: return "A: check/apply update, B: back";
        case Status::View::ERROR: return "B: return to platforms";
        default: return "";
    }
}

const char *actionName(Action action) {
    switch (action) {
        case Action::Up: return "Up";
        case Action::Down: return "Down";
        case Action::Left: return "Left";
        case Action::Right: return "Right";
        case Action::Select: return "Select";
        case Action::OpenSearch: return "OpenSearch";
        case Action::OpenDiagnostics: return "OpenDiagnostics";
        case Action::OpenUpdater: return "OpenUpdater";
        case Action::OpenQueue: return "OpenQueue";
        case Action::Back: return "Back";
        case Action::StartDownload: return "StartDownload";
        case Action::Quit: return "Quit";
        case Action::None:
        default: return "None";
    }
}

const char *queueStateName(QueueState state) {
    switch (state) {
        case QueueState::Pending: return "Pending";
        case QueueState::Downloading: return "Downloading";
        case QueueState::Completed: return "Completed";
        case QueueState::Failed: return "Failed";
        default: return "Unknown";
    }
}

const char *romSortModeName(RomSortMode mode) {
    switch (mode) {
        case RomSortMode::TitleAsc: return "TitleAsc";
        case RomSortMode::SizeAsc: return "SizeAsc";
        case RomSortMode::SizeDesc: return "SizeDesc";
        default: return "Unknown";
    }
}

const char *updaterStateName(UpdaterState state) {
    switch (state) {
        case UpdaterState::Idle: return "Idle";
        case UpdaterState::Checking: return "Checking";
        case UpdaterState::Available: return "Available";
        case UpdaterState::Applying: return "Applying";
        case UpdaterState::Applied: return "Applied";
        default: return "Unknown";
    }
}

namespace {

struct Rgb {
    int r;
    int g;
    int b;
};

struct ViewPalette {
    Rgb bg;
    Rgb header;
};

ViewPalette paletteForView(Status::View view) {
    switch (view) {
        case Status::View::PLATFORMS: return {{6, 46, 112}, {38, 108, 200}};
        case Status::View::ROMS: return {{0, 70, 96}, {20, 142, 186}};
        case Status::View::DETAIL: return {{12, 26, 72}, {54, 110, 210}};
        case Status::View::QUEUE: return {{52, 26, 88}, {120, 72, 180}};
        case Status::View::DOWNLOADING: return {{90, 60, 0}, {140, 100, 20}};
        case Status::View::DIAGNOSTICS: return {{30, 70, 40}, {40, 120, 70}};
        case Status::View::UPDATER: return {{16, 20, 70}, {50, 70, 170}};
        case Status::View::ERROR: return {{90, 0, 0}, {150, 20, 20}};
        default: return {{24, 24, 24}, {200, 200, 200}};
    }
}

std::string ansiFg(const Rgb &c, bool bold) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "\x1b[%s38;2;%d;%d;%dm", bold ? "1;" : "", c.r, c.g, c.b);
    return std::string(buf);
}

std::string ansiBg(const Rgb &c) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "\x1b[48;2;%d;%d;%dm", c.r, c.g, c.b);
    return std::string(buf);
}

std::string paint(const std::string &text, const Rgb &fg, const Rgb *bg, bool bold) {
    std::string out;
    out.reserve(text.size() + 48);
    out += ansiFg(fg, bold);
    if (bg != nullptr) out += ansiBg(*bg);
    out += text;
    out += "\x1b[0m";
    return out;
}

bool startsWith(const std::string &text, const char *prefix) {
    return text.rfind(prefix, 0) == 0;
}

char spinnerChar(uint32_t frame) {
    static const char kSpin[] = {'|', '/', '-', '\\'};
    return kSpin[frame % 4];
}

std::string stripAnsi(const std::string &src) {
    std::string out;
    out.reserve(src.size());
    bool esc = false;
    for (size_t i = 0; i < src.size(); ++i) {
        const char c = src[i];
        if (!esc && c == '\x1b') {
            esc = true;
            continue;
        }
        if (esc) {
            if ((c >= '@' && c <= '~') || c == '\n') esc = false;
            continue;
        }
        out.push_back(c);
    }
    return out;
}

} // namespace

std::vector<std::string> buildStatusLines(const Status &status, bool colorize) {
    std::vector<std::string> lines;
    lines.emplace_back("View         : " + std::string(viewName(status.currentView)));
    lines.emplace_back("Screen       : " + std::string(screenTitle(status.currentView)));
    lines.emplace_back("Hint         : " + std::string(screenHint(status.currentView)));
    lines.emplace_back("Search       : " + (status.searchQuery.empty() ? std::string("<none>") : status.searchQuery));
    lines.emplace_back("Sort         : " + std::string(romSortModeName(status.romSort)));

    const PlatformEntry *platform = selectedPlatform(status);
    if (platform != nullptr) {
        const std::vector<int> visible = visibleRomIndices(status, *platform);
        lines.emplace_back("Platform     : " + platform->name +
                           " (" + std::to_string(status.selectedPlatformIndex + 1) +
                           "/" + std::to_string(status.platforms.size()) + ")");
        lines.emplace_back("Visible ROMs : " + std::to_string(visible.size()) + "/" +
                           std::to_string(platform->roms.size()));

        if (!visible.empty()) {
            const int selectedVisibleIdx = clampIndex(status.selectedRomIndex, visible);
            const int romIdx = visible[static_cast<size_t>(selectedVisibleIdx)];
            const RomEntry &rom = platform->roms[static_cast<size_t>(romIdx)];
            lines.emplace_back("ROM          : " + rom.title +
                               " [" + std::to_string(selectedVisibleIdx + 1) + "/" +
                               std::to_string(visible.size()) + "]");
            lines.emplace_back("Size         : " + std::to_string(rom.sizeMb) + " MB");
        }
    }

    if (status.currentView == Status::View::DETAIL) {
        const RomEntry *rom = selectedRom(status);
        if (rom != nullptr) {
            lines.emplace_back("Detail ID    : " + rom->id);
            lines.emplace_back("Detail Title : " + rom->title);
            lines.emplace_back("Publisher    : " + (rom->subtitle.empty() ? std::string("<unknown>") : rom->subtitle));
            lines.emplace_back("Download URL : " + (rom->downloadUrl.empty() ? std::string("<missing>") : rom->downloadUrl));
            lines.emplace_back("Detail Size  : " + std::to_string(rom->sizeMb) + " MB");
        } else {
            lines.emplace_back("Detail       : <no ROM selected>");
        }
    }

    uint32_t pendingCount = 0;
    uint32_t failedCount = 0;
    for (const auto &item : status.downloadQueue) {
        if (item.state == QueueState::Pending) pendingCount++;
        if (item.state == QueueState::Failed) failedCount++;
    }
    lines.emplace_back("Queue        : " + std::to_string(status.downloadQueue.size()) + " items (" +
                       std::to_string(pendingCount) + " pending, " +
                       std::to_string(failedCount) + " failed)");
    lines.emplace_back("History      : " + std::to_string(status.downloadHistory.size()) + " completed");
    lines.emplace_back("Paused       : " + std::string(status.downloadPaused ? "yes" : "no"));

    if (status.downloadWorkerRunning && status.activeDownloadIndex >= 0 &&
        status.activeDownloadIndex < static_cast<int>(status.downloadQueue.size())) {
        const QueueItem &item = status.downloadQueue[static_cast<size_t>(status.activeDownloadIndex)];
        lines.emplace_back("Downloading  : " + item.rom.title + " (" +
                           std::to_string(item.progressPercent) + "%, try " +
                           std::to_string(item.attempts) + ")");
    }

    if (!status.downloadQueue.empty()) {
        const int start = std::max(0, status.selectedQueueIndex - 2);
        const int end = std::min(static_cast<int>(status.downloadQueue.size()), start + 5);
        for (int i = start; i < end; ++i) {
            const QueueItem &item = status.downloadQueue[static_cast<size_t>(i)];
            std::ostringstream oss;
            oss << (i == status.selectedQueueIndex ? "> " : "  ");
            oss << item.rom.title << " [" << queueStateName(item.state);
            if (item.state == QueueState::Downloading) {
                oss << " " << static_cast<int>(item.progressPercent) << "%";
            }
            if (item.state == QueueState::Failed && !item.error.empty()) {
                oss << " " << item.error;
            }
            oss << "]";
            lines.push_back(oss.str());
        }
    }

    lines.emplace_back("Updater      : " + std::string(updaterStateName(status.updaterState)) +
                       " (current " + status.currentVersion +
                       ", latest " + status.availableVersion + ")");
    lines.emplace_back("Diag         : input=" + std::to_string(status.diagnostics.inputCount) +
                       " enqueue=" + std::to_string(status.diagnostics.enqueueCount) +
                       " dup=" + std::to_string(status.diagnostics.duplicateBlockedCount) +
                       " done=" + std::to_string(status.diagnostics.completedCount) +
                       " fail=" + std::to_string(status.diagnostics.failedCount) +
                       " search=" + std::to_string(status.diagnostics.searchCount));
    lines.emplace_back("Message      : " + status.lastMessage);

    if (!colorize) return lines;

    const ViewPalette palette = paletteForView(status.currentView);
    const Rgb white{255, 255, 255};
    const Rgb hint{200, 220, 255};
    const Rgb ok{120, 220, 140};
    const Rgb warn{255, 210, 120};
    const Rgb bad{255, 120, 120};
    const Rgb dim{200, 200, 200};

    for (size_t i = 0; i < lines.size(); ++i) {
        std::string &line = lines[i];
        if (i <= 1) {
            line = paint(line, white, &palette.header, true);
            continue;
        }
        if (startsWith(line, "Hint         : ")) {
            line = paint(line, hint, &palette.bg, false);
            continue;
        }
        if (startsWith(line, "Message      : ")) {
            if (line.find("Failed") != std::string::npos || line.find("error") != std::string::npos) {
                line = paint(line, bad, nullptr, true);
            } else if (line.find("complete") != std::string::npos ||
                       line.find("Updated") != std::string::npos) {
                line = paint(line, ok, nullptr, true);
            } else {
                line = paint(line, white, nullptr, false);
            }
            continue;
        }
        if (startsWith(line, "> ") || startsWith(line, "  ")) {
            if (line.find("[Pending") != std::string::npos) {
                line = paint(line, dim, nullptr, false);
            } else if (line.find("[Downloading") != std::string::npos) {
                line = paint(line, warn, nullptr, true);
            } else if (line.find("[Completed") != std::string::npos) {
                line = paint(line, ok, nullptr, false);
            } else if (line.find("[Failed") != std::string::npos) {
                line = paint(line, bad, nullptr, true);
            } else {
                line = paint(line, white, nullptr, false);
            }
            continue;
        }
        line = paint(line, white, nullptr, false);
    }

    return lines;
}

std::vector<std::string> buildFramedStatusLines(const Status &status, bool colorize) {
    const std::vector<std::string> body = buildStatusLines(status, colorize);
    std::vector<std::string> out;
    out.reserve(body.size() + 8);

    const bool busy = status.uiBusy || status.downloadWorkerRunning;
    const std::string spin(1, spinnerChar(status.uiFrameCounter));
    const std::string busyLabel = busy ? ("busy " + spin) : "idle";

    out.emplace_back("======================================================================");
    out.emplace_back("wiiuromm | " + std::string(screenTitle(status.currentView)) +
                     " | " + std::string(viewName(status.currentView)) +
                     " | " + busyLabel);
    out.emplace_back(std::string("hint: ") + screenHint(status.currentView));
    out.emplace_back("----------------------------------------------------------------------");
    for (const auto &line : body) {
        out.emplace_back("| " + line);
    }
    out.emplace_back("----------------------------------------------------------------------");
    out.emplace_back("| footer: queue=" + std::to_string(status.downloadQueue.size()) +
                     " history=" + std::to_string(status.downloadHistory.size()) +
                     " msg=\"" + stripAnsi(status.lastMessage) + "\"");
    out.emplace_back("======================================================================");
    return out;
}

} // namespace romm
