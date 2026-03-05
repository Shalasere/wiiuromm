#include "romm/downloader.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "romm/error.hpp"

namespace romm {
namespace {
namespace fs = std::filesystem;

bool isSuccessStatus(int code) {
    return code >= 200 && code < 300;
}

HttpHeaders buildHeaders(const AppConfig &cfg) {
    HttpHeaders headers;
    if (!cfg.authToken.empty()) headers.push_back({"Authorization", "Bearer " + cfg.authToken});
    return headers;
}

} // namespace

bool runDownloadQueue(const AppConfig &cfg, IHttpClient &client,
                      const std::vector<DownloadRequest> &queue,
                      IDownloadObserver &observer, std::string &outError) {
    for (const auto &req : queue) {
        if (req.url.empty()) {
            ErrorInfo info;
            info.kind = ErrorKind::Parse;
            info.userMessage = "Missing download URL.";
            info.detail = "request " + req.id + " has empty URL";
            observer.onFailure(req, info);
            outError = info.userMessage;
            return false;
        }

        const fs::path outPath(req.outputPath);
        const fs::path parent = outPath.parent_path();
        if (!parent.empty()) {
            std::error_code ec;
            fs::create_directories(parent, ec);
            if (ec) {
                ErrorInfo info;
                info.kind = ErrorKind::Filesystem;
                info.userMessage = "Failed to create output directory.";
                info.detail = ec.message();
                observer.onFailure(req, info);
                outError = info.userMessage;
                return false;
            }
        }

        std::ofstream out(req.outputPath, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            ErrorInfo info;
            info.kind = ErrorKind::Filesystem;
            info.userMessage = "Failed to open output file.";
            info.detail = req.outputPath;
            observer.onFailure(req, info);
            outError = info.userMessage;
            return false;
        }

        uint64_t downloaded = 0;
        int statusCode = 0;
        const bool ok = client.streamGet(
            req.url, buildHeaders(cfg), statusCode,
            [&](const uint8_t *data, size_t size) {
                if (size == 0) return true;
                out.write(reinterpret_cast<const char *>(data), static_cast<std::streamsize>(size));
                if (!out.good()) return false;
                downloaded += static_cast<uint64_t>(size);
                DownloadProgress p;
                p.id = req.id;
                p.downloadedBytes = downloaded;
                p.expectedBytes = req.expectedBytes;
                observer.onProgress(p);
                return true;
            },
            outError);

        out.flush();
        out.close();

        if (!ok) {
            std::error_code ec;
            fs::remove(outPath, ec);
            ErrorInfo info = classifyErrorText(outError);
            observer.onFailure(req, info);
            return false;
        }

        if (!isSuccessStatus(statusCode)) {
            std::error_code ec;
            fs::remove(outPath, ec);
            ErrorInfo info = classifyHttpStatus(statusCode);
            observer.onFailure(req, info);
            outError = info.userMessage;
            return false;
        }

        if (req.expectedBytes > 0 && downloaded > req.expectedBytes) {
            std::error_code ec;
            fs::remove(outPath, ec);
            ErrorInfo info;
            info.kind = ErrorKind::Parse;
            info.userMessage = "Download exceeded expected size.";
            info.detail = req.id;
            observer.onFailure(req, info);
            outError = info.userMessage;
            return false;
        }

        observer.onComplete(req);
    }
    return true;
}

} // namespace romm
