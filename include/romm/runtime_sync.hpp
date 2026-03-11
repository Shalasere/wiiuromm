#pragma once

#include <functional>
#include <set>
#include <string>
#include <vector>

#include "romm/app_core.hpp"
#include "romm/config.hpp"
#include "romm/http_client.hpp"

namespace romm {

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
    std::vector<PlatformEntry> platforms;
    int platformPage{1};
    int platformGuard{0};
    int netRetryCount{0};
    int netRetryDelayFrames{0};
};

struct CatalogRuntimeState {
    std::set<std::string> indexedPlatformIds;
};

struct PlatformRomIndexState {
    bool active{false};
    bool openRomViewOnComplete{false};
    int platformIndex{-1};
    std::string platformId;
    int romPage{1};
    int romGuard{0};
};

using NetworkInitCallback = std::function<bool(std::string&)>;

void resetStartupSync(StartupSyncState& state);
bool startupSyncReady(const StartupSyncState& state);
void resetPlatformRomIndex(PlatformRomIndexState& state);
bool isPlatformRomIndexActive(const PlatformRomIndexState& state);

bool stepStartupSync(Status& status, const AppConfig& cfg, IHttpClient& client,
                     StartupSyncState& startup,
                     const NetworkInitCallback& initNetwork = NetworkInitCallback{});

bool beginSelectedPlatformRomIndex(Status& status, CatalogRuntimeState& runtime,
                                   bool openRomViewOnComplete,
                                   PlatformRomIndexState& state,
                                   std::string& outError);

bool stepPlatformRomIndex(Status& status, const AppConfig& cfg, IHttpClient& client,
                          CatalogRuntimeState& runtime,
                          PlatformRomIndexState& state, std::string& outError);

bool indexSelectedPlatformRoms(Status& status, const AppConfig& cfg, IHttpClient& client,
                               CatalogRuntimeState& runtime, std::string& outError);

bool runRealDownloads(Status& status, const AppConfig& cfg, IHttpClient& client);

} // namespace romm
