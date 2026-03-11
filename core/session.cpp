#include "romm/session.hpp"

#include <regex>

namespace romm {
namespace {

bool extractStringField(const std::string& src, const char* key, std::string& out) {
    const std::regex re(std::string("\"") + key + "\"\\s*:\\s*\"([^\"]*)\"");
    std::smatch m;
    if (!std::regex_search(src, m, re))
        return false;
    out = m[1].str();
    return true;
}

std::string base64Encode(const std::string& in) {
    static const char kTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((in.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i + 2 < in.size()) {
        const unsigned int n = (static_cast<unsigned int>(static_cast<unsigned char>(in[i])) << 16) | (static_cast<unsigned int>(static_cast<unsigned char>(in[i + 1])) << 8) | static_cast<unsigned int>(static_cast<unsigned char>(in[i + 2]));
        out.push_back(kTable[(n >> 18) & 63]);
        out.push_back(kTable[(n >> 12) & 63]);
        out.push_back(kTable[(n >> 6) & 63]);
        out.push_back(kTable[n & 63]);
        i += 3;
    }
    const size_t rem = in.size() - i;
    if (rem == 1) {
        const unsigned int n = (static_cast<unsigned int>(static_cast<unsigned char>(in[i])) << 16);
        out.push_back(kTable[(n >> 18) & 63]);
        out.push_back(kTable[(n >> 12) & 63]);
        out.push_back('=');
        out.push_back('=');
    } else if (rem == 2) {
        const unsigned int n = (static_cast<unsigned int>(static_cast<unsigned char>(in[i])) << 16) | (static_cast<unsigned int>(static_cast<unsigned char>(in[i + 1])) << 8);
        out.push_back(kTable[(n >> 18) & 63]);
        out.push_back(kTable[(n >> 12) & 63]);
        out.push_back(kTable[(n >> 6) & 63]);
        out.push_back('=');
    }
    return out;
}

} // namespace

bool validateTokenPreflight(const AppConfig& cfg, IHttpClient& client,
                            AuthSession& session, ErrorInfo& outInfo, std::string& outError) {
    HttpHeaders headers;
    if (!cfg.authToken.empty()) {
        headers.push_back({"Authorization", "Bearer " + cfg.authToken});
    } else if (!cfg.username.empty() || !cfg.password.empty()) {
        headers.push_back({"Authorization", "Basic " + base64Encode(cfg.username + ":" + cfg.password)});
    }

    HttpResponse resp;
    if (!client.get(cfg.serverUrl + "/api/platforms", headers, resp, outError)) {
        outInfo = classifyErrorText(outError);
        session.tokenValid = false;
        session.lastCheckDetail = outError;
        return false;
    }

    outInfo = classifyHttpStatus(resp.statusCode, resp.statusText);
    session.lastCheckDetail = outInfo.detail;
    session.tokenValid = (outInfo.kind == ErrorKind::None);

    std::string user;
    if (extractStringField(resp.body, "user", user))
        session.accountName = user;
    else
        session.accountName.clear();

    if (!session.tokenValid) {
        outError = outInfo.userMessage;
        return false;
    }
    return true;
}

} // namespace romm
