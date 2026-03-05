#include "romm/downloader.hpp"

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>

#include "romm/error.hpp"

namespace romm {
namespace {
namespace fs = std::filesystem;

bool isSuccessStatus(int code) {
    return code >= 200 && code < 300;
}

bool isRetryableHttpStatus(int code) {
    return code == 408 || code == 425 || code == 429 || (code >= 500 && code <= 599);
}

HttpHeaders buildHeaders(const AppConfig &cfg) {
    HttpHeaders headers;
    if (!cfg.authToken.empty()) headers.push_back({"Authorization", "Bearer " + cfg.authToken});
    return headers;
}

void backoffWaitMs(int ms) {
    if (ms <= 0) return;
    const clock_t start = std::clock();
    while (true) {
        const clock_t now = std::clock();
        if (now <= start) continue;
        const long elapsedMs = static_cast<long>((now - start) * 1000 / CLOCKS_PER_SEC);
        if (elapsedMs >= ms) break;
    }
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

        const int attemptsMax = std::max(1, cfg.maxDownloadRetries + 1);
        int attempt = 0;
        ErrorInfo lastError;
        bool completed = false;

        while (attempt < attemptsMax && !completed) {
            attempt++;
            std::ofstream out(req.outputPath, std::ios::binary | std::ios::trunc);
            if (!out.is_open()) {
                lastError.kind = ErrorKind::Filesystem;
                lastError.userMessage = "Failed to open output file.";
                lastError.detail = req.outputPath;
                break;
            }

            uint64_t downloaded = 0;
            int statusCode = 0;
            std::string streamError;
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
                streamError);

            out.flush();
            out.close();

            if (!ok) {
                std::error_code ec;
                fs::remove(outPath, ec);
                lastError = classifyErrorText(streamError);
                const bool retryable = (lastError.kind == ErrorKind::Network ||
                                        lastError.kind == ErrorKind::Http ||
                                        lastError.kind == ErrorKind::Unknown);
                if (retryable && attempt < attemptsMax) {
                    backoffWaitMs(cfg.retryBackoffMs * attempt);
                    continue;
                }
                break;
            }

            if (!isSuccessStatus(statusCode)) {
                std::error_code ec;
                fs::remove(outPath, ec);
                lastError = classifyHttpStatus(statusCode);
                if (isRetryableHttpStatus(statusCode) && attempt < attemptsMax) {
                    backoffWaitMs(cfg.retryBackoffMs * attempt);
                    continue;
                }
                break;
            }

            if (req.expectedBytes > 0 && downloaded > req.expectedBytes) {
                std::error_code ec;
                fs::remove(outPath, ec);
                lastError.kind = ErrorKind::Parse;
                lastError.userMessage = "Download exceeded expected size.";
                lastError.detail = req.id;
                break;
            }

            completed = true;
            observer.onComplete(req);
        }

        if (!completed) {
            if (lastError.kind == ErrorKind::None) {
                lastError.kind = ErrorKind::Unknown;
                lastError.userMessage = "Download failed.";
                lastError.detail = req.id;
            }
            lastError.detail += " (attempts=" + std::to_string(attempt) + ")";
            observer.onFailure(req, lastError);
            outError = lastError.userMessage;
            return false;
        }
    }
    return true;
}

} // namespace romm
