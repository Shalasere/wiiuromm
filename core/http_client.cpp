#include "romm/http_client.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#ifdef GEKKO
#include <network.h>
#else
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace romm {
namespace {

struct ParsedUrl {
    std::string host;
    std::string path;
    int port{80};
};

bool parseHttpUrl(const std::string& url, ParsedUrl& out, std::string& outError) {
    const std::string http = "http://";
    const std::string https = "https://";
    if (url.rfind(https, 0) == 0) {
        outError = "https is not supported by SocketHttpClient";
        return false;
    }
    if (url.rfind(http, 0) != 0) {
        outError = "URL must start with http://";
        return false;
    }

    const std::string rest = url.substr(http.size());
    const size_t slash = rest.find('/');
    const std::string hostPort = (slash == std::string::npos) ? rest : rest.substr(0, slash);
    out.path = (slash == std::string::npos) ? "/" : rest.substr(slash);
    if (hostPort.empty()) {
        outError = "missing host in URL";
        return false;
    }

    const size_t colon = hostPort.rfind(':');
    if (colon == std::string::npos) {
        out.host = hostPort;
        out.port = 80;
        return true;
    }

    out.host = hostPort.substr(0, colon);
    const std::string portStr = hostPort.substr(colon + 1);
    if (out.host.empty() || portStr.empty()) {
        outError = "invalid host:port in URL";
        return false;
    }
    char* end = nullptr;
    errno = 0;
    const long parsed = std::strtol(portStr.c_str(), &end, 10);
    if (errno != 0 || end == portStr.c_str() || *end != '\0' || parsed < 1 || parsed > 65535) {
        outError = "invalid port in URL";
        return false;
    }
    out.port = static_cast<int>(parsed);
    return true;
}

#ifdef GEKKO
int socketConnect(const ParsedUrl& url, std::string& outError) {
    const hostent* host = net_gethostbyname(url.host.c_str());
    if (host == nullptr || host->h_addr == nullptr) {
        outError = "DNS lookup failed: " + url.host;
        return -1;
    }

    const int s = net_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (s < 0) {
        outError = "socket create failed";
        return -1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(url.port));
    std::memcpy(&addr.sin_addr, host->h_addr, static_cast<size_t>(host->h_length));

    if (net_connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        net_close(s);
        outError = "socket connect failed";
        return -1;
    }
    return s;
}

int socketWrite(int s, const void* buf, size_t n) {
    return net_write(s, buf, static_cast<int>(n));
}

int socketRead(int s, void* buf, size_t n) {
    return net_read(s, buf, static_cast<int>(n));
}

void socketClose(int s) { net_close(s); }

#else
int socketConnect(const ParsedUrl& url, std::string& outError) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* res = nullptr;
    const std::string port = std::to_string(url.port);
    const int rc = getaddrinfo(url.host.c_str(), port.c_str(), &hints, &res);
    if (rc != 0 || res == nullptr) {
        outError = "DNS lookup failed: " + url.host;
        return -1;
    }

    int s = -1;
    for (addrinfo* it = res; it != nullptr; it = it->ai_next) {
        s = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (s < 0)
            continue;
        if (::connect(s, it->ai_addr, it->ai_addrlen) == 0)
            break;
        ::close(s);
        s = -1;
    }
    freeaddrinfo(res);

    if (s < 0) {
        outError = std::string("socket connect failed: ") + std::strerror(errno);
        return -1;
    }
    return s;
}

int socketWrite(int s, const void* buf, size_t n) {
    return static_cast<int>(::send(s, buf, n, 0));
}

int socketRead(int s, void* buf, size_t n) {
    return static_cast<int>(::recv(s, buf, n, 0));
}

void socketClose(int s) { ::close(s); }
#endif

bool writeAll(int s, const std::string& data, std::string& outError) {
    size_t off = 0;
    while (off < data.size()) {
        const int n = socketWrite(s, data.data() + off, data.size() - off);
        if (n <= 0) {
            outError = "socket write failed";
            return false;
        }
        off += static_cast<size_t>(n);
    }
    return true;
}

bool readAll(int s, std::string& out, std::string& outError) {
    out.clear();
    std::vector<char> buf(8192);
    while (true) {
        const int n = socketRead(s, buf.data(), buf.size());
        if (n == 0)
            return true;
        if (n < 0) {
            outError = "socket read failed";
            return false;
        }
        out.append(buf.data(), static_cast<size_t>(n));
    }
}

bool parseHttpResponse(const std::string& raw, HttpResponse& out, std::string& outError) {
    const size_t headerEnd = raw.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        outError = "invalid HTTP response (missing header terminator)";
        return false;
    }
    const std::string headerBlock = raw.substr(0, headerEnd);
    out.body = raw.substr(headerEnd + 4);

    const size_t lineEnd = headerBlock.find("\r\n");
    const std::string statusLine = (lineEnd == std::string::npos)
                                       ? headerBlock
                                       : headerBlock.substr(0, lineEnd);

    std::istringstream ss(statusLine);
    std::string http;
    ss >> http >> out.statusCode;
    std::getline(ss, out.statusText);
    if (out.statusCode <= 0) {
        outError = "invalid HTTP status line";
        return false;
    }
    if (!out.statusText.empty() && out.statusText[0] == ' ')
        out.statusText.erase(0, 1);
    return true;
}

bool parseHttpStatusLine(const std::string& headerBlock, int& outStatusCode, std::string& outError) {
    const size_t lineEnd = headerBlock.find("\r\n");
    const std::string statusLine = (lineEnd == std::string::npos)
                                       ? headerBlock
                                       : headerBlock.substr(0, lineEnd);

    std::istringstream ss(statusLine);
    std::string http;
    int code = 0;
    ss >> http >> code;
    if (code <= 0) {
        outError = "invalid HTTP status line";
        return false;
    }
    outStatusCode = code;
    return true;
}

bool httpGetRaw(const std::string& url, const HttpHeaders& headers,
                HttpResponse& out, std::string& outError) {
    ParsedUrl parsed;
    if (!parseHttpUrl(url, parsed, outError))
        return false;

    const int s = socketConnect(parsed, outError);
    if (s < 0)
        return false;

    std::string req = "GET " + parsed.path + " HTTP/1.0\r\n";
    req += "Host: " + parsed.host + "\r\n";
    req += "Connection: close\r\n";
    req += "Accept: */*\r\n";
    req += "Accept-Encoding: identity\r\n";
    req += "User-Agent: wiiuromm/0.1\r\n";
    for (const auto& h : headers) {
        req += h.first + ": " + h.second + "\r\n";
    }
    req += "\r\n";

    bool ok = writeAll(s, req, outError);
    std::string raw;
    if (ok)
        ok = readAll(s, raw, outError);
    socketClose(s);
    if (!ok)
        return false;

    return parseHttpResponse(raw, out, outError);
}

} // namespace

bool NullHttpClient::get(const std::string& url, const HttpHeaders& headers,
                         HttpResponse& out, std::string& outError) {
    (void)url;
    (void)headers;
    out = HttpResponse{};
    outError = "no HTTP client implementation configured";
    return false;
}

bool NullHttpClient::streamGet(const std::string& url, const HttpHeaders& headers,
                               int& outStatusCode,
                               const std::function<bool(const uint8_t*, size_t)>& onChunk,
                               std::string& outError) {
    (void)url;
    (void)headers;
    (void)onChunk;
    outStatusCode = 0;
    outError = "no HTTP client implementation configured";
    return false;
}

bool SocketHttpClient::get(const std::string& url, const HttpHeaders& headers,
                           HttpResponse& out, std::string& outError) {
    out = HttpResponse{};
    return httpGetRaw(url, headers, out, outError);
}

bool SocketHttpClient::streamGet(const std::string& url, const HttpHeaders& headers,
                                 int& outStatusCode,
                                 const std::function<bool(const uint8_t*, size_t)>& onChunk,
                                 std::string& outError) {
    ParsedUrl parsed;
    if (!parseHttpUrl(url, parsed, outError))
        return false;

    const int s = socketConnect(parsed, outError);
    if (s < 0)
        return false;

    std::string req = "GET " + parsed.path + " HTTP/1.0\r\n";
    req += "Host: " + parsed.host + "\r\n";
    req += "Connection: close\r\n";
    req += "Accept: */*\r\n";
    req += "Accept-Encoding: identity\r\n";
    req += "User-Agent: wiiuromm/0.1\r\n";
    for (const auto& h : headers) {
        req += h.first + ": " + h.second + "\r\n";
    }
    req += "\r\n";

    if (!writeAll(s, req, outError)) {
        socketClose(s);
        return false;
    }

    outStatusCode = 0;
    std::string buffered;
    buffered.reserve(8192);
    std::vector<char> buf(8192);
    bool headersParsed = false;

    while (true) {
        const int n = socketRead(s, buf.data(), buf.size());
        if (n == 0)
            break;
        if (n < 0) {
            socketClose(s);
            outError = "socket read failed";
            return false;
        }

        if (!headersParsed) {
            buffered.append(buf.data(), static_cast<size_t>(n));
            const size_t headerEnd = buffered.find("\r\n\r\n");
            if (headerEnd == std::string::npos)
                continue;

            const std::string headerBlock = buffered.substr(0, headerEnd);
            if (!parseHttpStatusLine(headerBlock, outStatusCode, outError)) {
                socketClose(s);
                return false;
            }
            headersParsed = true;

            const size_t bodyOffset = headerEnd + 4;
            if (buffered.size() > bodyOffset) {
                const uint8_t* data = reinterpret_cast<const uint8_t*>(buffered.data() + bodyOffset);
                const size_t bodySize = buffered.size() - bodyOffset;
                if (!onChunk(data, bodySize)) {
                    socketClose(s);
                    outError = "stream callback aborted";
                    return false;
                }
            }
            buffered.clear();
            continue;
        }

        if (!onChunk(reinterpret_cast<const uint8_t*>(buf.data()), static_cast<size_t>(n))) {
            socketClose(s);
            outError = "stream callback aborted";
            return false;
        }
    }

    socketClose(s);
    if (!headersParsed) {
        outError = "invalid HTTP response (missing header terminator)";
        return false;
    }
    return true;
}

} // namespace romm
