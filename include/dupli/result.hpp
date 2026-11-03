#pragma once

#include <variant>
#include <string>
#include <optional>

namespace dupli {

enum class ErrorCode {
    Ok,
    NotFound,
    PermissionDenied,
    IoError,
    InvalidArgument,
    Unsupported,
    Stale,
    Conflict,
    Cancelled,
    Unknown
};

struct Error {
    ErrorCode code = ErrorCode::Unknown;
    std::string message;
    std::string path;
    std::string operation;
};

/// Stable, human-readable error code names for reports and CLI output.
inline const char* to_string(ErrorCode code) {
    switch (code) {
    case ErrorCode::Ok: return "ok";
    case ErrorCode::NotFound: return "not-found";
    case ErrorCode::PermissionDenied: return "permission-denied";
    case ErrorCode::IoError: return "io-error";
    case ErrorCode::InvalidArgument: return "invalid-argument";
    case ErrorCode::Unsupported: return "unsupported";
    case ErrorCode::Stale: return "stale";
    case ErrorCode::Conflict: return "conflict";
    case ErrorCode::Cancelled: return "cancelled";
    case ErrorCode::Unknown: return "unknown";
    }
    return "unknown";
}

template<typename T>
class Result {
public:
    Result(T value) : m_data(std::move(value)) {}
    Result(Error error) : m_data(std::move(error)) {}
    
    bool ok() const { return std::holds_alternative<T>(m_data); }
    bool has_error() const { return std::holds_alternative<Error>(m_data); }
    
    T& value() { return std::get<T>(m_data); }
    const T& value() const { return std::get<T>(m_data); }
    
    Error& error() { return std::get<Error>(m_data); }
    const Error& error() const { return std::get<Error>(m_data); }
    
    operator bool() const { return ok(); }
    
private:
    std::variant<T, Error> m_data;
};

// Specialization for void
template<>
class Result<void> {
public:
    Result() = default;
    Result(Error error) : m_error(std::move(error)) {}
    
    bool ok() const { return !m_error.has_value(); }
    bool has_error() const { return m_error.has_value(); }
    
    Error& error() { return m_error.value(); }
    const Error& error() const { return m_error.value(); }
    
    operator bool() const { return ok(); }
    
private:
    std::optional<Error> m_error;
};

} // namespace dupli

// Result<T, Error> pattern: provides consistent error handling without exceptions
// for normal control flow. Used throughout the codebase to make failures explicit.
