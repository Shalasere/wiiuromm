#include "romm/api_client.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <regex>

#include "romm/error.hpp"

namespace romm {
namespace {

bool parseIntStrict(const std::string &s, int &out) {
    if (s.empty()) return false;
    errno = 0;
    char *end = nullptr;
    long v = std::strtol(s.c_str(), &end, 10);
    if (errno != 0 || end == s.c_str() || *end != '\0') return false;
    if (v < -2147483647L - 1L || v > 2147483647L) return false;
    out = static_cast<int>(v);
    return true;
}

bool parseU32Strict(const std::string &s, uint32_t &out) {
    if (s.empty()) return false;
    errno = 0;
    char *end = nullptr;
    unsigned long v = std::strtoul(s.c_str(), &end, 10);
    if (errno != 0 || end == s.c_str() || *end != '\0') return false;
    if (v > 0xFFFFFFFFul) return false;
    out = static_cast<uint32_t>(v);
    return true;
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
    return parseIntStrict(m[1].str(), out);
}

bool parseItemObjects(const std::string &body, std::vector<std::string> &objects, std::string &outError) {
    const std::string key = "\"items\"";
    const size_t keyPos = body.find(key);
    if (keyPos == std::string::npos) {
        outError = "missing items array";
        return false;
    }
    const size_t arrBegin = body.find('[', keyPos);
    if (arrBegin == std::string::npos) {
        outError = "missing items array begin";
        return false;
    }

    int depth = 0;
    bool inString = false;
    bool escape = false;
    size_t arrEnd = std::string::npos;
    for (size_t i = arrBegin; i < body.size(); ++i) {
        char c = body[i];
        if (inString) {
            if (escape) {
                escape = false;
            } else if (c == '\\') {
                escape = true;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (c == '"') {
            inString = true;
            continue;
        }
        if (c == '[') depth++;
        else if (c == ']') {
            depth--;
            if (depth == 0) {
                arrEnd = i;
                break;
            }
        }
    }
    if (arrEnd == std::string::npos) {
        outError = "unterminated items array";
        return false;
    }

    const std::string arr = body.substr(arrBegin + 1, arrEnd - arrBegin - 1);
    depth = 0;
    inString = false;
    escape = false;
    size_t objStart = std::string::npos;
    for (size_t i = 0; i < arr.size(); ++i) {
        char c = arr[i];
        if (inString) {
            if (escape) {
                escape = false;
            } else if (c == '\\') {
                escape = true;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (c == '"') {
            inString = true;
            continue;
        }
        if (c == '{') {
            if (depth == 0) objStart = i;
            depth++;
        } else if (c == '}') {
            depth--;
            if (depth == 0 && objStart != std::string::npos) {
                objects.push_back(arr.substr(objStart, i - objStart + 1));
                objStart = std::string::npos;
            }
        }
    }
    return true;
}

bool isSuccessStatus(const HttpResponse &resp) {
    return resp.statusCode >= 200 && resp.statusCode < 300;
}

HttpHeaders buildAuthHeaders(const AppConfig &cfg) {
    HttpHeaders headers;
    if (!cfg.authToken.empty()) headers.push_back({"Authorization", "Bearer " + cfg.authToken});
    return headers;
}

} // namespace

bool fetchPlatformsPage(const AppConfig &cfg, IHttpClient &client, int page, int limit,
                        std::vector<PlatformEntry> &outItems, int &outNextPage,
                        std::string &outError) {
    outItems.clear();
    outNextPage = 0;

    HttpResponse resp;
    const std::string url = cfg.serverUrl + "/api/v1/platforms?page=" +
                            std::to_string(page) + "&limit=" + std::to_string(limit);
    if (!client.get(url, buildAuthHeaders(cfg), resp, outError)) return false;
    if (!isSuccessStatus(resp)) {
        outError = classifyHttpStatus(resp.statusCode, resp.statusText).userMessage;
        return false;
    }

    std::vector<std::string> objects;
    if (!parseItemObjects(resp.body, objects, outError)) return false;

    for (const auto &obj : objects) {
        PlatformEntry p;
        if (!extractStringField(obj, "id", p.id)) continue;
        if (!extractStringField(obj, "name", p.name)) p.name = p.id;
        outItems.push_back(p);
    }

    (void)extractIntField(resp.body, "next_page", outNextPage);
    return true;
}

bool fetchRomsPage(const AppConfig &cfg, IHttpClient &client, const std::string &platformId,
                   int page, int limit, std::vector<RomEntry> &outItems, int &outNextPage,
                   std::string &outError) {
    outItems.clear();
    outNextPage = 0;

    HttpResponse resp;
    const std::string url = cfg.serverUrl + "/api/v1/platforms/" + platformId + "/roms?page=" +
                            std::to_string(page) + "&limit=" + std::to_string(limit);
    if (!client.get(url, buildAuthHeaders(cfg), resp, outError)) return false;
    if (!isSuccessStatus(resp)) {
        outError = classifyHttpStatus(resp.statusCode, resp.statusText).userMessage;
        return false;
    }

    std::vector<std::string> objects;
    if (!parseItemObjects(resp.body, objects, outError)) return false;

    for (const auto &obj : objects) {
        RomEntry r;
        std::string sizeMbRaw;
        if (!extractStringField(obj, "id", r.id)) continue;
        if (!extractStringField(obj, "title", r.title)) r.title = r.id;
        (void)extractStringField(obj, "subtitle", r.subtitle);
        if (extractStringField(obj, "download_url", r.downloadUrl)) {
            // filled
        }

        int sizeFromInt = 0;
        if (extractIntField(obj, "size_mb", sizeFromInt)) {
            r.sizeMb = static_cast<uint32_t>(std::max(0, sizeFromInt));
        } else if (extractStringField(obj, "size_mb", sizeMbRaw)) {
            uint32_t sizeParsed = 0;
            if (parseU32Strict(sizeMbRaw, sizeParsed)) r.sizeMb = sizeParsed;
        }
        outItems.push_back(r);
    }

    (void)extractIntField(resp.body, "next_page", outNextPage);
    return true;
}

bool syncCatalogFromApi(Status &status, const AppConfig &cfg, IHttpClient &client,
                        int romPageLimit, std::string &outError) {
    std::vector<PlatformEntry> platforms;

    int page = 1;
    int guard = 0;
    while (page > 0 && guard < 64) {
        std::vector<PlatformEntry> items;
        int nextPage = 0;
        if (!fetchPlatformsPage(cfg, client, page, 32, items, nextPage, outError)) return false;
        platforms.insert(platforms.end(), items.begin(), items.end());
        page = nextPage;
        guard++;
    }

    if (platforms.empty()) {
        outError = "API returned no platforms";
        return false;
    }

    for (auto &p : platforms) {
        int romPage = 1;
        int romGuard = 0;
        while (romPage > 0 && romGuard < 256) {
            std::vector<RomEntry> items;
            int nextPage = 0;
            if (!fetchRomsPage(cfg, client, p.id, romPage, romPageLimit, items, nextPage, outError)) {
                return false;
            }
            p.roms.insert(p.roms.end(), items.begin(), items.end());
            romPage = nextPage;
            romGuard++;
        }
    }

    status.platforms = platforms;
    status.selectedPlatformIndex = 0;
    status.selectedRomIndex = 0;
    status.lastMessage = "Catalog synced from API.";
    return true;
}

} // namespace romm
