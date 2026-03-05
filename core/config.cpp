#include "romm/config.hpp"

#include <cctype>
#include <cstdlib>
#include <cerrno>
#include <fstream>
#include <regex>
#include <sstream>

namespace romm {
namespace {

std::string trim(const std::string &s) {
    size_t begin = 0;
    while (begin < s.size() && std::isspace(static_cast<unsigned char>(s[begin]))) begin++;
    size_t end = s.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) end--;
    return s.substr(begin, end - begin);
}

bool extractStringField(const std::string &src, const char *key, std::string &out) {
    const std::regex re(std::string("\"") + key + "\"\\s*:\\s*\"([^\"]*)\"");
    std::smatch m;
    if (!std::regex_search(src, m, re)) return false;
    out = m[1].str();
    return true;
}

bool extractIntField(const std::string &src, const char *key, int &out) {
    const std::regex re(std::string("\"") + key + "\"\\s*:\\s*(-?[0-9]+)");
    std::smatch m;
    if (!std::regex_search(src, m, re)) return false;
    out = std::stoi(m[1].str());
    return true;
}

bool extractBoolField(const std::string &src, const char *key, bool &out) {
    const std::regex re(std::string("\"") + key + "\"\\s*:\\s*(true|false)");
    std::smatch m;
    if (!std::regex_search(src, m, re)) return false;
    out = (m[1].str() == "true");
    return true;
}

bool parseBoolString(const std::string &raw, bool &out) {
    const std::string v = trim(raw);
    if (v == "1" || v == "true" || v == "TRUE" || v == "yes" || v == "on") {
        out = true;
        return true;
    }
    if (v == "0" || v == "false" || v == "FALSE" || v == "no" || v == "off") {
        out = false;
        return true;
    }
    return false;
}

bool parseIntStrict(const char *text, int &out) {
    if (text == nullptr || *text == '\0') return false;
    errno = 0;
    char *end = nullptr;
    long v = std::strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') return false;
    if (v < -2147483647L - 1L || v > 2147483647L) return false;
    out = static_cast<int>(v);
    return true;
}

} // namespace

AppConfig defaultConfig() {
    return AppConfig{};
}

bool validateConfig(const AppConfig &cfg, std::string &outError) {
    if (cfg.schemaVersion <= 0) {
        outError = "schema_version must be positive";
        return false;
    }
    if (!(cfg.serverUrl.rfind("http://", 0) == 0 || cfg.serverUrl.rfind("https://", 0) == 0)) {
        outError = "server_url must start with http:// or https://";
        return false;
    }
    if (cfg.downloadDir.empty()) {
        outError = "download_dir must not be empty";
        return false;
    }
    if (cfg.maxConcurrentDownloads < 1 || cfg.maxConcurrentDownloads > 8) {
        outError = "max_concurrent_downloads must be in [1,8]";
        return false;
    }
    if (cfg.maxDownloadRetries < 0 || cfg.maxDownloadRetries > 8) {
        outError = "max_download_retries must be in [0,8]";
        return false;
    }
    if (cfg.retryBackoffMs < 0 || cfg.retryBackoffMs > 10000) {
        outError = "retry_backoff_ms must be in [0,10000]";
        return false;
    }
    outError.clear();
    return true;
}

bool loadConfigFromFile(const std::string &path, AppConfig &out, std::string &outError) {
    out = defaultConfig();

    std::ifstream in(path);
    if (!in.is_open()) {
        outError = "failed to open config file: " + path;
        return false;
    }

    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string body = ss.str();

    int schema = out.schemaVersion;
    if (!extractIntField(body, "schema_version", schema)) {
        outError = "missing required key: schema_version";
        return false;
    }
    out.schemaVersion = schema;

    (void)extractStringField(body, "server_url", out.serverUrl);
    (void)extractStringField(body, "auth_token", out.authToken);
    (void)extractStringField(body, "download_dir", out.downloadDir);
    (void)extractBoolField(body, "fat32_safe", out.fat32Safe);
    (void)extractIntField(body, "max_concurrent_downloads", out.maxConcurrentDownloads);
    (void)extractIntField(body, "max_download_retries", out.maxDownloadRetries);
    (void)extractIntField(body, "retry_backoff_ms", out.retryBackoffMs);

    if (!validateConfig(out, outError)) return false;
    return true;
}

bool applyEnvOverrides(AppConfig &cfg, std::string &outError) {
    if (const char *v = std::getenv("ROMM_SERVER_URL")) cfg.serverUrl = v;
    if (const char *v = std::getenv("ROMM_AUTH_TOKEN")) cfg.authToken = v;
    if (const char *v = std::getenv("ROMM_DOWNLOAD_DIR")) cfg.downloadDir = v;

    if (const char *v = std::getenv("ROMM_MAX_CONCURRENT_DOWNLOADS")) {
        int parsed = 0;
        if (!parseIntStrict(v, parsed)) {
            outError = "invalid ROMM_MAX_CONCURRENT_DOWNLOADS";
            return false;
        }
        cfg.maxConcurrentDownloads = parsed;
    }

    if (const char *v = std::getenv("ROMM_MAX_DOWNLOAD_RETRIES")) {
        int parsed = 0;
        if (!parseIntStrict(v, parsed)) {
            outError = "invalid ROMM_MAX_DOWNLOAD_RETRIES";
            return false;
        }
        cfg.maxDownloadRetries = parsed;
    }

    if (const char *v = std::getenv("ROMM_RETRY_BACKOFF_MS")) {
        int parsed = 0;
        if (!parseIntStrict(v, parsed)) {
            outError = "invalid ROMM_RETRY_BACKOFF_MS";
            return false;
        }
        cfg.retryBackoffMs = parsed;
    }

    if (const char *v = std::getenv("ROMM_FAT32_SAFE")) {
        bool b = false;
        if (!parseBoolString(v, b)) {
            outError = "invalid ROMM_FAT32_SAFE";
            return false;
        }
        cfg.fat32Safe = b;
    }

    if (!validateConfig(cfg, outError)) return false;
    return true;
}

} // namespace romm
