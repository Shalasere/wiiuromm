#include "romm/http_client.hpp"

namespace romm {

bool NullHttpClient::get(const std::string &url, const HttpHeaders &headers,
                         HttpResponse &out, std::string &outError) {
    (void)url;
    (void)headers;
    out = HttpResponse{};
    outError = "no HTTP client implementation configured";
    return false;
}

bool NullHttpClient::streamGet(const std::string &url, const HttpHeaders &headers,
                               int &outStatusCode,
                               const std::function<bool(const uint8_t *, size_t)> &onChunk,
                               std::string &outError) {
    (void)url;
    (void)headers;
    (void)onChunk;
    outStatusCode = 0;
    outError = "no HTTP client implementation configured";
    return false;
}

} // namespace romm
