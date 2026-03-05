#pragma once

#include <string>

namespace romm {

enum class ErrorKind {
    None = 0,
    Network,
    Auth,
    Http,
    Filesystem,
    Parse,
    Unknown
};

struct ErrorInfo {
    ErrorKind kind{ErrorKind::None};
    std::string userMessage;
    std::string detail;
};

ErrorInfo classifyHttpStatus(int statusCode, const std::string &statusText = "");
ErrorInfo classifyErrorText(const std::string &detail);
const char *errorKindName(ErrorKind kind);

} // namespace romm
