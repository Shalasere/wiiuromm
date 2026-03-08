#include "romm/app_core.hpp"
#include "romm/config.hpp"
#include "romm/error.hpp"
#include "romm/persistence.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {
namespace fs = std::filesystem;

void requireTrue(bool cond, const char *msg) {
    if (!cond) {
        std::cerr << "FAILED: " << msg << "\n";
        std::exit(1);
    }
}

fs::path makeTempDir(const char *name) {
    const fs::path p = fs::temp_directory_path() / ("wiiuromm_" + std::string(name));
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p);
    return p;
}

void testConfigFileAndEnv() {
    const fs::path dir = makeTempDir("cfg");
    const fs::path cfgPath = dir / "config.json";
    {
        std::ofstream out(cfgPath);
        out << "{\n"
            << "  \"schema_version\": 1,\n"
            << "  \"server_url\": \"http://127.0.0.1:8080\",\n"
            << "  \"username\": \"dev\",\n"
            << "  \"password\": \"pw\",\n"
            << "  \"auth_token\": \"tok\",\n"
            << "  \"target_platform_id\": \"wii\",\n"
            << "  \"download_dir\": \"run/downloads\",\n"
            << "  \"fat32_safe\": true,\n"
            << "  \"max_concurrent_downloads\": 2\n"
            << "}\n";
    }

    romm::AppConfig cfg;
    std::string err;
    requireTrue(romm::loadConfigFromFile(cfgPath.string(), cfg, err), "loadConfigFromFile should pass");
    requireTrue(cfg.fat32Safe, "fat32_safe should parse true");
    requireTrue(cfg.maxConcurrentDownloads == 2, "max concurrency should parse");
    requireTrue(cfg.targetPlatformId == "wii", "target_platform_id should parse");

    setenv("SERVER_URL", "https://games.fortkickass.tech", 1);
    setenv("USERNAME", "root", 1);
    setenv("PASSWORD", "pw", 1);
    setenv("PLATFORM", "wii", 1);
    setenv("ROMM_MAX_CONCURRENT_DOWNLOADS", "3", 1);
    requireTrue(romm::applyEnvOverrides(cfg, err), "applyEnvOverrides should pass");
    requireTrue(cfg.serverUrl == "https://games.fortkickass.tech", "server url env override should apply");
    requireTrue(cfg.username == "root", "username env override should apply");
    requireTrue(cfg.password == "pw", "password env override should apply");
    requireTrue(cfg.targetPlatformId == "wii", "platform env override should apply");
    requireTrue(cfg.maxConcurrentDownloads == 3, "concurrency env override should apply");

    unsetenv("SERVER_URL");
    unsetenv("USERNAME");
    unsetenv("PASSWORD");
    unsetenv("PLATFORM");
    unsetenv("ROMM_MAX_CONCURRENT_DOWNLOADS");
}

void testQueueStateRoundTrip() {
    const fs::path dir = makeTempDir("queue");
    const fs::path statePath = dir / "queue_state.txt";

    romm::Status s = romm::makeDefaultStatus();
    romm::QueueItem q;
    q.rom = {"rom_1", "ROM 1", "Sub", "http://example.invalid/rom_1.bin", 100};
    q.state = romm::QueueState::Failed;
    q.progressPercent = 45;
    q.attempts = 2;
    q.error = "timeout";
    s.downloadQueue.push_back(q);

    std::string err;
    requireTrue(romm::saveQueueState(s, statePath.string(), err), "saveQueueState should pass");

    romm::Status loaded = romm::makeDefaultStatus();
    loaded.downloadQueue.clear();
    loaded.downloadHistory.clear();
    requireTrue(romm::loadQueueState(loaded, statePath.string(), err), "loadQueueState should pass");
    requireTrue(loaded.downloadQueue.size() == 1, "one queue item should load");
    requireTrue(loaded.downloadQueue[0].state == romm::QueueState::Failed, "queue state should roundtrip");
    requireTrue(loaded.downloadQueue[0].error == "timeout", "queue error should roundtrip");
}

void testManifestRoundTrip() {
    const fs::path dir = makeTempDir("manifest");
    romm::QueueItem item;
    item.rom = {"rom_2", "ROM 2", "Subtitle", "http://example.invalid/rom_2.bin", 222};
    item.state = romm::QueueState::Completed;
    item.progressPercent = 100;

    std::string outPath;
    std::string err;
    requireTrue(romm::writeCompletedManifest(item, dir.string(), outPath, err),
                "writeCompletedManifest should pass");
    requireTrue(fs::exists(outPath), "manifest file should exist");

    romm::Status s = romm::makeDefaultStatus();
    s.downloadHistory.clear();
    requireTrue(romm::loadCompletedManifests(s, dir.string(), err), "loadCompletedManifests should pass");
    requireTrue(s.downloadHistory.size() == 1, "manifest should load into history");
    requireTrue(s.downloadHistory[0].rom.id == "rom_2", "manifest id should roundtrip");
}

void testResumePlanner() {
    std::vector<romm::PartialChunk> chunks = {
        {0, 100},
        {100, 200},
        {300, 50},
    };
    const romm::ResumeDecision good = romm::planResume(1000, chunks);
    requireTrue(good.mode == romm::ResumeMode::ResumeFromPartial, "contiguous chunks should resume");
    requireTrue(good.resumeBytes == 350, "resume bytes should equal contiguous end");

    std::vector<romm::PartialChunk> gap = {
        {0, 100},
        {150, 20},
    };
    const romm::ResumeDecision bad = romm::planResume(1000, gap);
    requireTrue(bad.mode == romm::ResumeMode::RestartDueToGap, "gap should require restart");
}

void testPartDetection() {
    const fs::path dir = makeTempDir("parts");
    {
        std::ofstream a(dir / "game.part.0", std::ios::binary);
        a << "AAAA";
        std::ofstream b(dir / "game.part.4", std::ios::binary);
        b << "BBBBBB";
    }
    std::string err;
    const auto chunks = romm::detectPartChunks(dir.string(), "game", err);
    requireTrue(err.empty(), "detectPartChunks should not set error");
    requireTrue(chunks.size() == 2, "should find two part chunks");
}

void testErrorTaxonomy() {
    const auto auth = romm::classifyHttpStatus(401, "Unauthorized");
    requireTrue(auth.kind == romm::ErrorKind::Auth, "401 should classify as auth");

    const auto net = romm::classifyErrorText("connection timeout");
    requireTrue(net.kind == romm::ErrorKind::Network, "timeout should classify as network");

    const auto fsErr = romm::classifyErrorText("permission denied");
    requireTrue(fsErr.kind == romm::ErrorKind::Filesystem, "permission should classify as filesystem");
}

} // namespace

int main() {
    testConfigFileAndEnv();
    testQueueStateRoundTrip();
    testManifestRoundTrip();
    testResumePlanner();
    testPartDetection();
    testErrorTaxonomy();
    std::cout << "infra_test: all checks passed\n";
    return 0;
}
