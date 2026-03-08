#include "romm/api_client.hpp"
#include "romm/config.hpp"
#include "romm/downloader.hpp"
#include "romm/error.hpp"
#include "romm/http_client.hpp"
#include "romm/session.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/wait.h>
#include <vector>

namespace {
namespace fs = std::filesystem;

void requireTrue(bool cond, const char *msg) {
    if (!cond) {
        std::cerr << "FAILED: " << msg << "\n";
        std::exit(1);
    }
}

std::string shellEscape(const std::string &s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out.push_back(c);
    }
    out += "'";
    return out;
}

bool runCapture(const std::string &cmd, std::string &out, int &exitCode) {
    FILE *pipe = popen(cmd.c_str(), "r");
    if (pipe == nullptr) {
        out.clear();
        exitCode = 127;
        return false;
    }
    std::array<char, 4096> buf{};
    out.clear();
    while (true) {
        const size_t n = std::fread(buf.data(), 1, buf.size(), pipe);
        if (n > 0) out.append(buf.data(), n);
        if (n < buf.size()) {
            if (std::feof(pipe)) break;
            if (std::ferror(pipe)) break;
        }
    }
    const int rc = pclose(pipe);
    exitCode = WEXITSTATUS(rc);
    return rc == 0;
}

bool parseStatusSuffix(const std::string &payload, std::string &body, int &statusCode) {
    const std::string marker = "\n__STATUS__:";
    const size_t pos = payload.rfind(marker);
    if (pos == std::string::npos) return false;
    body = payload.substr(0, pos);
    const std::string sc = payload.substr(pos + marker.size());
    statusCode = std::atoi(sc.c_str());
    return statusCode > 0;
}

class CurlHttpClient final : public romm::IHttpClient {
public:
    bool get(const std::string &url, const romm::HttpHeaders &headers,
             romm::HttpResponse &out, std::string &outError) override {
        std::string cmd = "curl -sS -L";
        for (const auto &h : headers) {
            cmd += " -H " + shellEscape(h.first + ": " + h.second);
        }
        cmd += " -w " + shellEscape("\n__STATUS__:%{http_code}") + " " + shellEscape(url);

        std::string payload;
        int code = 0;
        if (!runCapture(cmd, payload, code)) {
            outError = "curl GET failed";
            return false;
        }

        int status = 0;
        std::string body;
        if (!parseStatusSuffix(payload, body, status)) {
            outError = "failed to parse curl status suffix";
            return false;
        }
        out.statusCode = status;
        out.statusText.clear();
        out.body = body;
        return true;
    }

    bool streamGet(const std::string &url, const romm::HttpHeaders &headers,
                   int &outStatusCode,
                   const std::function<bool(const uint8_t *, size_t)> &onChunk,
                   std::string &outError) override {
        romm::HttpResponse resp;
        if (!get(url, headers, resp, outError)) return false;
        outStatusCode = resp.statusCode;
        const size_t chunk = 1024;
        for (size_t i = 0; i < resp.body.size(); i += chunk) {
            const size_t n = std::min(chunk, resp.body.size() - i);
            const auto *ptr = reinterpret_cast<const uint8_t *>(resp.body.data() + i);
            if (!onChunk(ptr, n)) {
                outError = "stream callback aborted";
                return false;
            }
        }
        return true;
    }
};

class Observer final : public romm::IDownloadObserver {
public:
    int progressEvents{0};
    int completed{0};
    int failures{0};

    void onProgress(const romm::DownloadProgress &progress) override {
        (void)progress;
        progressEvents++;
    }
    void onComplete(const romm::DownloadRequest &request) override {
        (void)request;
        completed++;
    }
    void onFailure(const romm::DownloadRequest &request, const romm::ErrorInfo &error) override {
        (void)request;
        (void)error;
        failures++;
    }
};

} // namespace

int main() {
    const char *base = std::getenv("MOCK_BASE_URL");
    requireTrue(base != nullptr && std::string(base).size() > 0, "MOCK_BASE_URL must be set");
    const bool liveSmokeOnly = (std::getenv("LIVE_SMOKE_ONLY") != nullptr);

    const fs::path outDir = fs::temp_directory_path() / "wiiuromm_integration";
    std::error_code ec;
    fs::remove_all(outDir, ec);
    fs::create_directories(outDir, ec);
    requireTrue(!ec, "failed to create integration temp dir");

    romm::AppConfig cfg = romm::defaultConfig();
    cfg.serverUrl = base;
    cfg.username = "root";
    cfg.password = "pw";
    cfg.targetPlatformId = "wii";
    cfg.downloadDir = outDir.string();

    CurlHttpClient client;
    romm::AuthSession session;
    romm::ErrorInfo authInfo;
    std::string err;
    requireTrue(romm::validateTokenPreflight(cfg, client, session, authInfo, err),
                "token preflight should succeed");
    requireTrue(session.tokenValid, "token should be marked valid");

    if (!liveSmokeOnly) {
        romm::AppConfig badCfg = cfg;
        badCfg.authToken = "bad-token";
        romm::AuthSession badSession;
        romm::ErrorInfo badInfo;
        std::string badErr;
        requireTrue(!romm::validateTokenPreflight(badCfg, client, badSession, badInfo, badErr),
                    "bad token preflight should fail");
        requireTrue(badInfo.kind == romm::ErrorKind::Auth, "bad token should classify auth failure");
    }

    romm::Status status = romm::makeDefaultStatus();
    requireTrue(romm::syncCatalogFromApi(status, cfg, client, 2, err),
                "catalog sync should succeed");
    requireTrue(status.platforms.size() == 1, "platform list should be filtered to one item");
    requireTrue(status.platforms[0].slug == "wii", "platform should be Wii");
    requireTrue(!status.platforms[0].roms.empty(), "rom list should not be empty");
    if (!liveSmokeOnly) {
        requireTrue(!status.platforms[0].roms[0].downloadUrl.empty(), "rom download_url should be set");
    } else {
        std::cout << "integration_test: live smoke passed (preflight + Wii catalog sync)\n";
        return 0;
    }

    std::vector<romm::DownloadRequest> queue;
    cfg.maxDownloadRetries = 2;
    cfg.retryBackoffMs = 1;
    for (const auto &rom : status.platforms[0].roms) {
        if (rom.id == "wi_001" || rom.id == "wi_002") {
            romm::DownloadRequest req;
            req.id = rom.id;
            req.title = rom.title;
            req.url = rom.downloadUrl;
            req.outputPath = (outDir / (rom.id + ".bin")).string();
            queue.push_back(req);
        }
    }
    requireTrue(queue.size() == 2, "expected two queue items for integration download");

    Observer obs;
    requireTrue(romm::runDownloadQueue(cfg, client, queue, obs, err),
                "download queue should succeed");
    requireTrue(obs.failures == 0, "no download failures expected");
    requireTrue(obs.completed == 2, "two downloads should complete");
    requireTrue(obs.progressEvents > 0, "progress callback should fire");
    for (const auto &q : queue) {
        requireTrue(fs::exists(q.outputPath), "output file should exist");
        requireTrue(fs::file_size(q.outputPath) > 0, "output file should have content");
    }

    std::cout << "integration_test: all checks passed\n";
    return 0;
}
