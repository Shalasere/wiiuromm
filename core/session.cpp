#include "romm/session.hpp"

#include <regex>

namespace romm {
namespace {

bool extractStringField(const std::string &src, const char *key, std::string &out) {
    const std::regex re(std::string("\"") + key + "\"\\s*:\\s*\"([^\"]*)\"");
    std::smatch m;
    if (!std::regex_search(src, m, re)) return false;
    out = m[1].str();
    return true;
}

} // namespace

bool validateTokenPreflight(const AppConfig &cfg, IHttpClient &client,
                            AuthSession &session, ErrorInfo &outInfo, std::string &outError) {
    HttpHeaders headers;
    if (!cfg.authToken.empty()) {
        headers.push_back({"Authorization", "Bearer " + cfg.authToken});
    }

    HttpResponse resp;
    if (!client.get(cfg.serverUrl + "/api/v1/preflight", headers, resp, outError)) {
        outInfo = classifyErrorText(outError);
        session.tokenValid = false;
        session.lastCheckDetail = outError;
        return false;
    }

    outInfo = classifyHttpStatus(resp.statusCode, resp.statusText);
    session.lastCheckDetail = outInfo.detail;
    session.tokenValid = (outInfo.kind == ErrorKind::None);

    std::string user;
    if (extractStringField(resp.body, "user", user)) {
        session.accountName = user;
    } else {
        session.accountName.clear();
    }

    if (!session.tokenValid) {
        outError = outInfo.userMessage;
        return false;
    }
    return true;
}

} // namespace romm
