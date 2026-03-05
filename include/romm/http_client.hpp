#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace romm {

using HttpHeader = std::pair<std::string, std::string>;
using HttpHeaders = std::vector<HttpHeader>;

struct HttpResponse {
    int statusCode{0};
    std::string statusText;
    std::string body;
};

class IHttpClient {
public:
    virtual ~IHttpClient() = default;
    virtual bool get(const std::string &url, const HttpHeaders &headers,
                     HttpResponse &out, std::string &outError) = 0;

    // Implementations may stream directly or emulate streaming from buffered bytes.
    virtual bool streamGet(const std::string &url, const HttpHeaders &headers,
                           int &outStatusCode,
                           const std::function<bool(const uint8_t *, size_t)> &onChunk,
                           std::string &outError) = 0;
};

class NullHttpClient final : public IHttpClient {
public:
    bool get(const std::string &url, const HttpHeaders &headers,
             HttpResponse &out, std::string &outError) override;
    bool streamGet(const std::string &url, const HttpHeaders &headers,
                   int &outStatusCode,
                   const std::function<bool(const uint8_t *, size_t)> &onChunk,
                   std::string &outError) override;
};

} // namespace romm
