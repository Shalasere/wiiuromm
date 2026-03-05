#pragma once

#include <string>

namespace romm {

struct AppConfig {
    int schemaVersion{1};
    std::string serverUrl{"http://localhost:8080"};
    std::string authToken;
    std::string downloadDir{"run/downloads"};
    bool fat32Safe{false};
    int maxConcurrentDownloads{1};
    int maxDownloadRetries{2};
    int retryBackoffMs{25};
};

AppConfig defaultConfig();
bool loadConfigFromFile(const std::string &path, AppConfig &out, std::string &outError);
bool applyEnvOverrides(AppConfig &cfg, std::string &outError);
bool validateConfig(const AppConfig &cfg, std::string &outError);

} // namespace romm
