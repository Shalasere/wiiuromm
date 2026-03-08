#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace romm {

enum class Action {
    None = 0,
    Up,
    Down,
    Left,
    Right,
    Select,
    OpenSearch,
    OpenDiagnostics,
    OpenUpdater,
    OpenQueue,
    Back,
    StartDownload,
    Quit
};

enum class QueueState {
    Pending = 0,
    Downloading,
    Completed,
    Failed
};

enum class RomSortMode {
    TitleAsc = 0,
    SizeAsc,
    SizeDesc
};

enum class UpdaterState {
    Idle = 0,
    Checking,
    Available,
    Applying,
    Applied
};

struct RomEntry {
    std::string id;
    std::string title;
    std::string subtitle;
    std::string downloadUrl;
    uint32_t sizeMb{0};
};

struct PlatformEntry {
    std::string id;
    std::string name;
    std::vector<RomEntry> roms;
    std::string slug;
};

struct QueueItem {
    RomEntry rom;
    QueueState state{QueueState::Pending};
    uint8_t progressPercent{0};
    uint8_t attempts{0};
    std::string error;
};

struct DiagnosticsCounters {
    uint32_t inputCount{0};
    uint32_t enqueueCount{0};
    uint32_t duplicateBlockedCount{0};
    uint32_t completedCount{0};
    uint32_t failedCount{0};
    uint32_t searchCount{0};
};

struct Status {
    enum class View {
        PLATFORMS = 0,
        ROMS,
        DETAIL,
        QUEUE,
        DOWNLOADING,
        DIAGNOSTICS,
        UPDATER,
        ERROR
    };

    View currentView{View::PLATFORMS};
    View prevQueueView{View::PLATFORMS};
    View prevDiagnosticsView{View::PLATFORMS};
    View prevUpdaterView{View::PLATFORMS};

    std::vector<PlatformEntry> platforms;
    int selectedPlatformIndex{0};
    int selectedRomIndex{0};
    int selectedQueueIndex{0};

    std::vector<QueueItem> downloadQueue;
    std::vector<QueueItem> downloadHistory;

    bool downloadWorkerRunning{false};
    bool downloadPaused{false};
    int activeDownloadIndex{-1};
    std::string searchQuery;
    RomSortMode romSort{RomSortMode::TitleAsc};

    DiagnosticsCounters diagnostics;
    bool uiBusy{false};
    uint32_t uiFrameCounter{0};

    UpdaterState updaterState{UpdaterState::Idle};
    std::string currentVersion{"0.1.0"};
    std::string availableVersion{"0.2.0"};

    std::string lastMessage;
};

struct ApplyResult {
    bool keepRunning{true};
    bool stateChanged{false};
    bool viewChanged{false};
};

Status makeDefaultStatus();
ApplyResult applyAction(Status &status, Action action);
bool tickDownload(Status &status, uint32_t elapsedMs);

const char *viewName(Status::View view);
const char *screenTitle(Status::View view);
const char *screenHint(Status::View view);
const char *actionName(Action action);
const char *queueStateName(QueueState state);
const char *romSortModeName(RomSortMode mode);
const char *updaterStateName(UpdaterState state);

std::vector<std::string> buildStatusLines(const Status &status, bool colorize = false);
std::vector<std::string> buildFramedStatusLines(const Status &status, bool colorize = false);

} // namespace romm
