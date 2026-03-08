#include "romm/api_client.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <set>
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

std::vector<std::string> splitList(const std::string &raw) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : raw) {
        if (c == ',') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
            continue;
        }
        if (!std::isspace(static_cast<unsigned char>(c))) cur.push_back(c);
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
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

bool extractStringOrNumberField(const std::string &src, const char *key, std::string &out) {
    if (extractStringField(src, key, out)) return true;
    int n = 0;
    if (!extractIntField(src, key, n)) return false;
    out = std::to_string(n);
    return true;
}

bool parseRootArrayObjects(const std::string &body, std::vector<std::string> &objects, std::string &outError) {
    const size_t arrBegin = body.find('[');
    if (arrBegin == std::string::npos) {
        outError = "missing array begin";
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
        outError = "unterminated array";
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

std::string base64Encode(const std::string &in) {
    static const char kTable[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((in.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i + 2 < in.size()) {
        const unsigned int n =
            (static_cast<unsigned int>(static_cast<unsigned char>(in[i])) << 16) |
            (static_cast<unsigned int>(static_cast<unsigned char>(in[i + 1])) << 8) |
            static_cast<unsigned int>(static_cast<unsigned char>(in[i + 2]));
        out.push_back(kTable[(n >> 18) & 63]);
        out.push_back(kTable[(n >> 12) & 63]);
        out.push_back(kTable[(n >> 6) & 63]);
        out.push_back(kTable[n & 63]);
        i += 3;
    }
    const size_t rem = in.size() - i;
    if (rem == 1) {
        const unsigned int n =
            (static_cast<unsigned int>(static_cast<unsigned char>(in[i])) << 16);
        out.push_back(kTable[(n >> 18) & 63]);
        out.push_back(kTable[(n >> 12) & 63]);
        out.push_back('=');
        out.push_back('=');
    } else if (rem == 2) {
        const unsigned int n =
            (static_cast<unsigned int>(static_cast<unsigned char>(in[i])) << 16) |
            (static_cast<unsigned int>(static_cast<unsigned char>(in[i + 1])) << 8);
        out.push_back(kTable[(n >> 18) & 63]);
        out.push_back(kTable[(n >> 12) & 63]);
        out.push_back(kTable[(n >> 6) & 63]);
        out.push_back('=');
    }
    return out;
}

HttpHeaders buildAuthHeaders(const AppConfig &cfg) {
    HttpHeaders headers;
    if (!cfg.authToken.empty()) {
        headers.push_back({"Authorization", "Bearer " + cfg.authToken});
    } else if (!cfg.username.empty() || !cfg.password.empty()) {
        headers.push_back({"Authorization", "Basic " + base64Encode(cfg.username + ":" + cfg.password)});
    }
    return headers;
}

} // namespace

bool fetchPlatformsPage(const AppConfig &cfg, IHttpClient &client, int page, int limit,
                        std::vector<PlatformEntry> &outItems, int &outNextPage,
                        std::string &outError) {
    outItems.clear();
    outNextPage = 0;

    HttpResponse resp;
    const std::string url = cfg.serverUrl + "/api/platforms?page=" +
                            std::to_string(page) + "&limit=" + std::to_string(limit);
    if (!client.get(url, buildAuthHeaders(cfg), resp, outError)) return false;
    if (!isSuccessStatus(resp)) {
        outError = classifyHttpStatus(resp.statusCode, resp.statusText).userMessage;
        return false;
    }

    std::vector<std::string> objects;
    if (!parseItemObjects(resp.body, objects, outError)) {
        objects.clear();
        std::string fallbackErr;
        if (!parseRootArrayObjects(resp.body, objects, fallbackErr)) return false;
        outError.clear();
    }

    for (const auto &obj : objects) {
        PlatformEntry p;
        if (!extractStringOrNumberField(obj, "id", p.id)) continue;
        if (!extractStringField(obj, "slug", p.slug)) p.slug = p.id;
        if (!extractStringField(obj, "display_name", p.name) &&
            !extractStringField(obj, "name", p.name)) {
            p.name = p.slug.empty() ? p.id : p.slug;
        }
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
    const int offset = std::max(0, (page - 1) * limit);
    const std::string url = cfg.serverUrl + "/api/roms?platform_ids=" + platformId +
                            "&limit=" + std::to_string(limit) +
                            "&offset=" + std::to_string(offset);
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
        if (!extractStringOrNumberField(obj, "id", r.id)) continue;
        if (!extractStringField(obj, "title", r.title) &&
            !extractStringField(obj, "name", r.title)) {
            r.title = r.id;
        }
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
        } else if (extractIntField(obj, "file_size_bytes", sizeFromInt)) {
            r.sizeMb = static_cast<uint32_t>(std::max(0, sizeFromInt) / (1024 * 1024));
        }
        outItems.push_back(r);
    }

    if (!extractIntField(resp.body, "next_page", outNextPage)) {
        outNextPage = (objects.size() == static_cast<size_t>(std::max(0, limit)))
            ? (page + 1)
            : 0;
    }
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

    if (!cfg.targetPlatformId.empty()) {
        const std::vector<std::string> wanted = splitList(cfg.targetPlatformId);
        std::vector<PlatformEntry> filtered;
        if (wanted.size() <= 1) {
            const auto it = std::find_if(platforms.begin(), platforms.end(), [&](const PlatformEntry &p) {
                return p.id == cfg.targetPlatformId ||
                       p.slug == cfg.targetPlatformId ||
                       p.name == cfg.targetPlatformId;
            });
            if (it == platforms.end()) {
                outError = "target platform not found: " + cfg.targetPlatformId;
                return false;
            }
            filtered.push_back(*it);
        } else {
            std::set<std::string> wantSet(wanted.begin(), wanted.end());
            if (wantSet.count("gc")) wantSet.insert("ngc");
            for (const auto &p : platforms) {
                if (wantSet.count(p.id) || wantSet.count(p.slug) || wantSet.count(p.name)) {
                    filtered.push_back(p);
                }
            }
            if (filtered.empty()) {
                outError = "no requested target platforms found: " + cfg.targetPlatformId;
                return false;
            }
        }
        platforms = std::move(filtered);
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
