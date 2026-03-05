#pragma once

#include <string>

#include "romm/config.hpp"
#include "romm/error.hpp"
#include "romm/http_client.hpp"

namespace romm {

struct AuthSession {
    bool tokenValid{false};
    std::string accountName;
    std::string lastCheckDetail;
};

bool validateTokenPreflight(const AppConfig &cfg, IHttpClient &client,
                            AuthSession &session, ErrorInfo &outInfo, std::string &outError);

} // namespace romm
