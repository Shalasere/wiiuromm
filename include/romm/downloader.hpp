#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "romm/config.hpp"
#include "romm/error.hpp"
#include "romm/http_client.hpp"

namespace romm {

struct DownloadRequest {
    std::string id;
    std::string title;
    std::string url;
    uint64_t expectedBytes{0};
    std::string outputPath;
};

struct DownloadProgress {
    std::string id;
    uint64_t downloadedBytes{0};
    uint64_t expectedBytes{0};
};

class IDownloadObserver {
public:
    virtual ~IDownloadObserver() = default;
    virtual void onProgress(const DownloadProgress &progress) = 0;
    virtual void onComplete(const DownloadRequest &request) = 0;
    virtual void onFailure(const DownloadRequest &request, const ErrorInfo &error) = 0;
};

bool runDownloadQueue(const AppConfig &cfg, IHttpClient &client,
                      const std::vector<DownloadRequest> &queue,
                      IDownloadObserver &observer, std::string &outError);

} // namespace romm
