#include "romm/logger.hpp"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>

namespace romm {
namespace {
namespace fs = std::filesystem;

constexpr size_t kMaxLogBytes = 512 * 1024;
const char *kDefaultPath = "run/logs/wiiuromm.log";

std::mutex gLogMutex;
std::ofstream gLogFile;
std::string gLogPath;
size_t gLogBytes = 0;
bool gLogReady = false;
LogLevel gMinLevel = LogLevel::Info;

const char *levelName(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO";
        case LogLevel::Warn: return "WARN";
        case LogLevel::Error: return "ERROR";
        default: return "INFO";
    }
}

void openFreshLogLocked(const std::string &path) {
    if (gLogFile.is_open()) gLogFile.close();
    std::error_code ec;
    const fs::path p(path);
    const fs::path parent = p.parent_path();
    if (!parent.empty()) fs::create_directories(parent, ec);
    gLogFile.open(path, std::ios::trunc);
    gLogBytes = 0;
    gLogReady = gLogFile.good();
    if (gLogReady) {
        gLogFile << "wiiuromm log start\n";
        gLogFile.flush();
        gLogBytes = static_cast<size_t>(gLogFile.tellp());
    }
}

void rotateLocked() {
    if (gLogPath.empty()) return;
    if (gLogFile.is_open()) gLogFile.close();
    std::error_code ec;
    fs::path p(gLogPath);
    fs::path rotated = p;
    rotated += ".1";
    fs::remove(rotated, ec);
    ec.clear();
    fs::rename(p, rotated, ec);
    openFreshLogLocked(gLogPath);
}

void logInternal(LogLevel level, const std::string &tag, const std::string &msg) {
    if (level < gMinLevel) return;
    const std::string line = "[" + tag + "] [" + levelName(level) + "] " + msg;
    std::cout << line << std::endl;

    std::lock_guard<std::mutex> lock(gLogMutex);
    if (!gLogReady) return;
    const size_t writeBytes = line.size() + 1;
    if (gLogBytes + writeBytes > kMaxLogBytes) rotateLocked();
    if (!gLogReady) return;
    gLogFile << line << "\n";
    gLogFile.flush();
    gLogBytes += writeBytes;
}

} // namespace

void initLogFile() {
    std::lock_guard<std::mutex> lock(gLogMutex);
    const char *envPath = std::getenv("ROMM_LOG_PATH");
    gLogPath = (envPath != nullptr && *envPath != '\0') ? envPath : kDefaultPath;
    openFreshLogLocked(gLogPath);
}

void shutdownLogFile() {
    std::lock_guard<std::mutex> lock(gLogMutex);
    gLogReady = false;
    gLogBytes = 0;
    if (gLogFile.is_open()) {
        gLogFile.flush();
        gLogFile.close();
    }
}

void setLogLevel(LogLevel level) { gMinLevel = level; }

void setLogLevelFromString(const std::string &level) {
    std::string l;
    l.reserve(level.size());
    for (char c : level) l.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    if (l == "debug") gMinLevel = LogLevel::Debug;
    else if (l == "warn") gMinLevel = LogLevel::Warn;
    else if (l == "error") gMinLevel = LogLevel::Error;
    else gMinLevel = LogLevel::Info;
}

void loadLogLevelFromEnv() {
    const char *envLevel = std::getenv("ROMM_LOG_LEVEL");
    if (envLevel != nullptr && *envLevel != '\0') setLogLevelFromString(envLevel);
}

void logLine(const std::string &msg) { logInternal(LogLevel::Info, "APP", msg); }
void logDebug(const std::string &msg, const std::string &tag) { logInternal(LogLevel::Debug, tag, msg); }
void logInfo(const std::string &msg, const std::string &tag) { logInternal(LogLevel::Info, tag, msg); }
void logWarn(const std::string &msg, const std::string &tag) { logInternal(LogLevel::Warn, tag, msg); }
void logError(const std::string &msg, const std::string &tag) { logInternal(LogLevel::Error, tag, msg); }

} // namespace romm

