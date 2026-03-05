#include "romm/error.hpp"

#include <algorithm>
#include <cctype>

namespace romm {
namespace {

std::string lowerCopy(const std::string &s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

bool contains(const std::string &haystack, const std::string &needle) {
    return haystack.find(needle) != std::string::npos;
}

} // namespace

ErrorInfo classifyHttpStatus(int statusCode, const std::string &statusText) {
    ErrorInfo info;
    info.detail = "HTTP " + std::to_string(statusCode) +
                  (statusText.empty() ? "" : (" " + statusText));

    if (statusCode >= 200 && statusCode < 300) {
        info.kind = ErrorKind::None;
        info.userMessage = "OK";
        return info;
    }
    if (statusCode == 401 || statusCode == 403) {
        info.kind = ErrorKind::Auth;
        info.userMessage = "Authentication failed.";
        return info;
    }
    if (statusCode == 404) {
        info.kind = ErrorKind::Http;
        info.userMessage = "Requested content not found.";
        return info;
    }
    if (statusCode == 429) {
        info.kind = ErrorKind::Http;
        info.userMessage = "Rate limited by server.";
        return info;
    }
    if (statusCode >= 500) {
        info.kind = ErrorKind::Network;
        info.userMessage = "Server unavailable.";
        return info;
    }
    info.kind = ErrorKind::Http;
    info.userMessage = "Unexpected server response.";
    return info;
}

ErrorInfo classifyErrorText(const std::string &detail) {
    const std::string lower = lowerCopy(detail);
    ErrorInfo info;
    info.detail = detail;

    if (contains(lower, "timeout") || contains(lower, "connection") ||
        contains(lower, "dns") || contains(lower, "refused")) {
        info.kind = ErrorKind::Network;
        info.userMessage = "Network error. Check connectivity.";
        return info;
    }
    if (contains(lower, "auth") || contains(lower, "unauthorized") ||
        contains(lower, "forbidden") || contains(lower, "token")) {
        info.kind = ErrorKind::Auth;
        info.userMessage = "Authentication error.";
        return info;
    }
    if (contains(lower, "permission") || contains(lower, "read-only") ||
        contains(lower, "no space") || contains(lower, "disk")) {
        info.kind = ErrorKind::Filesystem;
        info.userMessage = "Storage/filesystem error.";
        return info;
    }
    if (contains(lower, "json") || contains(lower, "parse") || contains(lower, "malformed")) {
        info.kind = ErrorKind::Parse;
        info.userMessage = "Data parse error.";
        return info;
    }
    info.kind = ErrorKind::Unknown;
    info.userMessage = "Unknown error.";
    return info;
}

const char *errorKindName(ErrorKind kind) {
    switch (kind) {
        case ErrorKind::None: return "None";
        case ErrorKind::Network: return "Network";
        case ErrorKind::Auth: return "Auth";
        case ErrorKind::Http: return "Http";
        case ErrorKind::Filesystem: return "Filesystem";
        case ErrorKind::Parse: return "Parse";
        case ErrorKind::Unknown:
        default:
            return "Unknown";
    }
}

} // namespace romm
