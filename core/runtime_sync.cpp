#include "romm/runtime_sync.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <vector>

#include "romm/api_client.hpp"
#include "romm/downloader.hpp"

namespace romm {
namespace {

std::vector<std::string> splitList(const std::string& raw) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= raw.size()) {
        const size_t comma = raw.find(',', start);
        const std::string token = raw.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        if (!token.empty())
            out.push_back(token);
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
    return out;
}

std::string toLower(const std::string& input) {
    std::string out = input;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

bool containsAny(const std::string& haystack, const std::vector<std::string>& needles) {
    for (const auto& needle : needles) {
        if (!needle.empty() && haystack.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

bool isPreWiiNintendoPlatform(const PlatformEntry& platform) {
    const std::string id = toLower(platform.id);
    const std::string slug = toLower(platform.slug);
    const std::string name = toLower(platform.name);
    const std::string familySlug = toLower(platform.familySlug);
    const std::string familyName = toLower(platform.familyName);
    const std::string merged = id + " " + slug + " " + name;
    const std::string familyMerged = familySlug + " " + familyName;

    // Prefer explicit family metadata when available. Some servers omit
    // family fields entirely; in that case we fall back to platform tokens.
    const bool hasFamilyMetadata = !familyMerged.empty();
    if (hasFamilyMetadata && !containsAny(familyMerged, {"nintendo"}))
        return false;

    // Exclude Wii U and newer generations (Wii itself is allowed).
    if (containsAny(merged, {"wiiu", "wii u", "switch", "3ds", "2ds"}))
        return false;

    // Canonical Nintendo pre-Wii tokens (plus DS line).
    const std::vector<std::string> allowTokens = {
        "nes", "famicom", "snes", "super nes", "super nintendo", "super famicom",
        "n64", "nintendo 64", "gamecube", "ngc", "wii",
        "game boy", "gameboy", "gb", "gbc", "gba",
        "nintendo ds", "nds",
        "virtual boy", "vb",
        "famicom disk", "fds",
        "satellaview",
        "pokemon mini",
        "game & watch", "game and watch"
    };
    return containsAny(merged, allowTokens);
}

void filterPreWiiNintendoPlatforms(std::vector<PlatformEntry>& platforms, std::string& outError) {
    std::vector<PlatformEntry> filtered;
    filtered.reserve(platforms.size());
    for (const auto& p : platforms) {
        if (isPreWiiNintendoPlatform(p))
            filtered.push_back(p);
    }
    if (filtered.empty()) {
        outError = "no Nintendo platforms (through Wii) found in API response";
        return;
    }
    platforms = std::move(filtered);
}

void filterTargetPlatforms(const std::string& target, std::vector<PlatformEntry>& platforms,
                           std::string& outError) {
    if (target.empty())
        return;
    const std::string lowerTarget = toLower(target);
    if (lowerTarget == "pre-wii" || lowerTarget == "pre_wii" || lowerTarget == "prewii") {
        filterPreWiiNintendoPlatforms(platforms, outError);
        return;
    }
    const std::vector<std::string> wanted = splitList(target);
    if (wanted.empty())
        return;

    std::set<std::string> wantSet(wanted.begin(), wanted.end());
    if (wantSet.count("gc"))
        wantSet.insert("ngc");

    std::vector<PlatformEntry> filtered;
    filtered.reserve(platforms.size());
    for (const auto& p : platforms) {
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

bool isTransientNetError(const std::string& err) {
    return err.find("socket connect failed") != std::string::npos || err.find("DNS lookup failed") != std::string::npos || err.find("socket write failed") != std::string::npos || err.find("socket read failed") != std::string::npos;
}

int clampPlatformIndex(const Status& status, int idx) {
    if (status.platforms.empty())
        return -1;
    if (idx < 0)
        return 0;
    const int maxIdx = static_cast<int>(status.platforms.size()) - 1;
    if (idx > maxIdx)
        return maxIdx;
    return idx;
}

int findPlatformIndexById(const Status& status, const std::string& platformId) {
    for (size_t i = 0; i < status.platforms.size(); ++i) {
        if (status.platforms[i].id == platformId)
            return static_cast<int>(i);
    }
    return -1;
}

struct QueueDownloadObserver final : public IDownloadObserver {
    explicit QueueDownloadObserver(QueueItem& itemRef)
        : item(itemRef) {
    }

    QueueItem& item;

    void onProgress(const DownloadProgress& progress) override {
        if (progress.expectedBytes > 0) {
            const uint64_t pct = (progress.downloadedBytes * 100ull) / progress.expectedBytes;
            item.progressPercent = static_cast<uint8_t>(std::min<uint64_t>(pct, 100ull));
        } else {
            item.progressPercent = std::min<uint8_t>(99, static_cast<uint8_t>(item.progressPercent + 1));
        }
    }

    void onComplete(const DownloadRequest& request) override {
        (void)request;
        item.progressPercent = 100;
        item.state = QueueState::Completed;
        item.error.clear();
    }

    void onFailure(const DownloadRequest& request, const ErrorInfo& error) override {
        (void)request;
        item.state = QueueState::Failed;
        item.error = error.userMessage;
        item.progressPercent = 0;
    }
};

} // namespace

void resetStartupSync(StartupSyncState& state) {
    state = StartupSyncState{};
}

bool startupSyncReady(const StartupSyncState& state) {
    return state.finished && state.stage != StartupSyncState::Stage::Failed && state.netReady;
}

void resetPlatformRomIndex(PlatformRomIndexState& state) {
    state = PlatformRomIndexState{};
}

bool isPlatformRomIndexActive(const PlatformRomIndexState& state) {
    return state.active;
}

bool beginSelectedPlatformRomIndex(Status& status, CatalogRuntimeState& runtime,
                                   bool openRomViewOnComplete,
                                   PlatformRomIndexState& state,
                                   std::string& outError) {
    outError.clear();
    const int idx = clampPlatformIndex(status, status.selectedPlatformIndex);
    if (idx < 0) {
        outError = "no platforms indexed";
        return false;
    }
    PlatformEntry& platform = status.platforms[static_cast<size_t>(idx)];

    if (runtime.indexedPlatformIds.count(platform.id)) {
        if (openRomViewOnComplete)
            status.selectedPlatformIndex = idx;
        status.selectedRomIndex = 0;
        if (openRomViewOnComplete)
            status.currentView = Status::View::ROMS;
        status.lastMessage = "Index cached: " + platform.name + " (" + std::to_string(platform.roms.size()) + " ROMs)";
        resetPlatformRomIndex(state);
        return true;
    }

    if (state.active && state.platformId == platform.id) {
        state.openRomViewOnComplete = state.openRomViewOnComplete || openRomViewOnComplete;
        status.lastMessage = "Indexing ROMs: " + platform.name + " (" + std::to_string(platform.roms.size()) + ")";
        return true;
    }

    resetPlatformRomIndex(state);
    state.active = true;
    state.openRomViewOnComplete = openRomViewOnComplete;
    state.platformIndex = idx;
    state.platformId = platform.id;
    state.romPage = 1;
    state.romGuard = 0;

    // Rebuild this platform's list on explicit selection.
    platform.roms.clear();
    status.selectedRomIndex = 0;
    status.lastMessage = "Indexing ROMs: " + platform.name + " (0)";
    return true;
}

bool stepPlatformRomIndex(Status& status, const AppConfig& cfg, IHttpClient& client,
                          CatalogRuntimeState& runtime,
                          PlatformRomIndexState& state, std::string& outError) {
    outError.clear();
    if (!state.active)
        return false;

    int idx = clampPlatformIndex(status, state.platformIndex);
    if (idx < 0 || status.platforms[static_cast<size_t>(idx)].id != state.platformId) {
        idx = findPlatformIndexById(status, state.platformId);
        if (idx < 0) {
            outError = "platform no longer available during index";
            state.active = false;
            status.lastMessage = "ROM index failed: " + outError;
            return true;
        }
        state.platformIndex = idx;
    }

    PlatformEntry& platform = status.platforms[static_cast<size_t>(idx)];
    std::vector<RomEntry> items;
    int nextPage = 0;
    if (!fetchRomsPage(cfg, client, platform.id, state.romPage, 64, items, nextPage, outError)) {
        state.active = false;
        status.lastMessage = "ROM index failed: " + outError;
        return true;
    }

    platform.roms.insert(platform.roms.end(), items.begin(), items.end());
    state.romPage = nextPage;
    state.romGuard++;
    status.lastMessage = "Indexing ROMs: " + platform.name + " (" + std::to_string(platform.roms.size()) + ")";

    if (state.romPage <= 0 || state.romGuard >= 256) {
        runtime.indexedPlatformIds.insert(platform.id);
        state.active = false;
        if (state.openRomViewOnComplete)
            status.selectedPlatformIndex = idx;
        status.selectedRomIndex = 0;
        if (state.openRomViewOnComplete)
            status.currentView = Status::View::ROMS;
        status.lastMessage = "Index complete: " + platform.name + " (" + std::to_string(platform.roms.size()) + " ROMs)";
    }

    return true;
}

bool stepStartupSync(Status& status, const AppConfig& cfg, IHttpClient& client,
                     StartupSyncState& startup, const NetworkInitCallback& initNetwork) {
    if (startup.finished)
        return false;
    if (startup.netRetryDelayFrames > 0) {
        startup.netRetryDelayFrames--;
        return false;
    }
    std::string err;

    switch (startup.stage) {
    case StartupSyncState::Stage::InitNetwork: {
        std::string netMsg;
        if (initNetwork) {
            startup.netReady = initNetwork(netMsg);
        } else {
            startup.netReady = true;
            netMsg = "Network ready.";
        }
        if (!startup.netReady) {
            startup.stage = StartupSyncState::Stage::Failed;
            status.currentView = Status::View::ERROR;
            status.lastMessage = netMsg.empty() ? "Network init failed." : netMsg;
            return true;
        }
        status.lastMessage = netMsg;
        startup.stage = StartupSyncState::Stage::FetchPlatforms;
        return true;
    }
    case StartupSyncState::Stage::FetchPlatforms: {
        std::vector<PlatformEntry> items;
        int nextPage = 0;
        if (!fetchPlatformsPage(cfg, client, startup.platformPage, 32, items, nextPage, err)) {
            if (isTransientNetError(err) && startup.netRetryCount < 3) {
                startup.netRetryCount++;
                startup.netRetryDelayFrames = 30 * startup.netRetryCount;
                status.lastMessage = "Network retry " + std::to_string(startup.netRetryCount) + "/3: " + err;
                return true;
            }
            startup.stage = StartupSyncState::Stage::Failed;
            status.currentView = Status::View::ERROR;
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
            status.currentView = Status::View::ERROR;
            status.lastMessage = "Catalog sync failed: API returned no platforms";
            return true;
        }
        filterTargetPlatforms(cfg.targetPlatformId, startup.platforms, err);
        if (!err.empty()) {
            startup.stage = StartupSyncState::Stage::Failed;
            status.currentView = Status::View::ERROR;
            status.lastMessage = "Catalog sync failed: " + err;
            return true;
        }
        startup.stage = StartupSyncState::Stage::Complete;
        return true;
    }
    case StartupSyncState::Stage::Complete:
        status.platforms = std::move(startup.platforms);
        status.selectedPlatformIndex = 0;
        status.selectedRomIndex = 0;
        status.currentView = Status::View::PLATFORMS;
        status.lastMessage = "Index complete: " + std::to_string(status.platforms.size()) + " platform(s). ROMs index on select.";
        startup.finished = true;
        return true;
    case StartupSyncState::Stage::Failed:
        startup.finished = true;
        return true;
    }
    return false;
}

bool indexSelectedPlatformRoms(Status& status, const AppConfig& cfg, IHttpClient& client,
                               CatalogRuntimeState& runtime, std::string& outError) {
    if (status.platforms.empty()) {
        outError = "no platforms indexed";
        return false;
    }
    int idx = status.selectedPlatformIndex;
    if (idx < 0)
        idx = 0;
    if (idx >= static_cast<int>(status.platforms.size())) {
        idx = static_cast<int>(status.platforms.size()) - 1;
    }
    PlatformEntry& platform = status.platforms[static_cast<size_t>(idx)];
    if (runtime.indexedPlatformIds.count(platform.id))
        return true;

    int romPage = 1;
    int romGuard = 0;
    platform.roms.clear();
    while (romPage > 0 && romGuard < 256) {
        std::vector<RomEntry> items;
        int nextPage = 0;
        if (!fetchRomsPage(cfg, client, platform.id, romPage, 64, items, nextPage, outError)) {
            return false;
        }
        platform.roms.insert(platform.roms.end(), items.begin(), items.end());
        romPage = nextPage;
        romGuard++;
        status.lastMessage = "Indexing ROMs: " + platform.name + " (" + std::to_string(platform.roms.size()) + ")";
    }
    runtime.indexedPlatformIds.insert(platform.id);
    status.selectedRomIndex = 0;
    status.lastMessage = "Index complete: " + platform.name + " (" + std::to_string(platform.roms.size()) + " ROMs)";
    return true;
}

bool runRealDownloads(Status& status, const AppConfig& cfg, IHttpClient& client) {
    if (!status.downloadWorkerRunning || status.downloadPaused)
        return false;
    if (status.downloadQueue.empty()) {
        status.downloadWorkerRunning = false;
        status.currentView = Status::View::QUEUE;
        status.lastMessage = "Queue empty.";
        return true;
    }

    bool changed = false;
    size_t idx = 0;
    while (idx < status.downloadQueue.size()) {
        QueueItem& item = status.downloadQueue[idx];
        if (item.state != QueueState::Pending) {
            idx++;
            continue;
        }
        item.state = QueueState::Downloading;
        item.progressPercent = 1;
        changed = true;

        DownloadRequest req;
        req.id = item.rom.id;
        req.title = item.rom.title;
        req.url = item.rom.downloadUrl;
        req.outputPath = cfg.downloadDir + "/" + item.rom.id + ".bin";

        QueueDownloadObserver observer(item);
        std::string dlErr;
        const bool ok = runDownloadQueue(cfg, client, {req}, observer, dlErr);
        if (ok) {
            status.downloadHistory.push_back(item);
            status.downloadQueue.erase(status.downloadQueue.begin() + static_cast<long>(idx));
            status.diagnostics.completedCount++;
            status.lastMessage = "Completed: " + req.title;
            continue;
        }

        item.state = QueueState::Failed;
        item.error = dlErr;
        item.progressPercent = 0;
        status.diagnostics.failedCount++;
        status.lastMessage = "Failed: " + item.rom.title + " (" + dlErr + ")";
        idx++;
    }

    status.downloadWorkerRunning = false;
    status.downloadPaused = false;
    status.activeDownloadIndex = -1;
    status.currentView = Status::View::QUEUE;
    if (status.selectedQueueIndex >= static_cast<int>(status.downloadQueue.size())) {
        status.selectedQueueIndex = status.downloadQueue.empty() ? 0 : static_cast<int>(status.downloadQueue.size()) - 1;
    }
    return changed;
}

} // namespace romm
