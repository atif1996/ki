#pragma once

#include <ki/error.hpp>
#include <ki/export.hpp>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace ki::url {

// TargetForm is the RFC 9112 form of a request-target.
enum class TargetForm : std::uint8_t {
    origin,     // "/path?query"
    absolute,   // "http://host/path?query", sent to proxies
    authority,  // "host:port", CONNECT only
    asterisk,   // "*", OPTIONS only
};

// Target is a validated request-target. Its views point into the string passed to parse_target.
struct Target {
    TargetForm form = TargetForm::origin;
    std::string_view scheme;
    std::string_view authority;
    std::string_view path;
    std::string_view query;
};

// parse_target validates a request-target for method. It rejects control characters and spaces, '*' outside
// OPTIONS, authority form outside CONNECT, userinfo in an authority, and hosts that are neither a valid
// bracketed IPv6 literal (with an RFC 6874 zone) nor a plain ASCII name or IPv4 address.
[[nodiscard]] KI_EXPORT std::expected<Target, Error> parse_target(std::string_view method, std::string_view target);

// Component selects the decoding rules: in a query, '+' means a space.
enum class Component : std::uint8_t { path, query };

[[nodiscard]] KI_EXPORT bool needs_unescape(std::string_view text, Component component) noexcept;

// unescape decodes %XX escapes, failing on a '%' not followed by two hex digits.
[[nodiscard]] KI_EXPORT std::expected<std::string, Error> unescape(std::string_view text, Component component);

// query_escape escapes text for a query key or value; spaces become '+'.
[[nodiscard]] KI_EXPORT std::string query_escape(std::string_view text);

// path_escape escapes text for one path segment; '/' is escaped, spaces become %20.
[[nodiscard]] KI_EXPORT std::string path_escape(std::string_view text);

}  // namespace ki::url
