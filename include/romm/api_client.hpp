#pragma once

#include <string>
#include <vector>

#include "romm/app_core.hpp"
#include "romm/config.hpp"
#include "romm/http_client.hpp"

namespace romm {

bool fetchPlatformsPage(const AppConfig &cfg, IHttpClient &client, int page, int limit,
                        std::vector<PlatformEntry> &outItems, int &outNextPage,
                        std::string &outError);

bool fetchRomsPage(const AppConfig &cfg, IHttpClient &client, const std::string &platformId,
                   int page, int limit, std::vector<RomEntry> &outItems, int &outNextPage,
                   std::string &outError);

bool syncCatalogFromApi(Status &status, const AppConfig &cfg, IHttpClient &client,
                        int romPageLimit, std::string &outError);

} // namespace romm
