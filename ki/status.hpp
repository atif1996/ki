#pragma once

#include <ki/export.hpp>

#include <cstdint>
#include <string_view>

namespace ki {

// Status is an HTTP status code. Codes without a named enumerator are valid as static_cast<Status>(code).
enum class Status : std::uint16_t {
    continue_ = 100,
    switching_protocols = 101,
    ok = 200,
    created = 201,
    accepted = 202,
    no_content = 204,
    moved_permanently = 301,
    found = 302,
    see_other = 303,
    not_modified = 304,
    temporary_redirect = 307,
    permanent_redirect = 308,
    bad_request = 400,
    unauthorized = 401,
    forbidden = 403,
    not_found = 404,
    method_not_allowed = 405,
    request_timeout = 408,
    conflict = 409,
    content_too_large = 413,
    uri_too_long = 414,
    unsupported_media_type = 415,
    unprocessable_content = 422,
    too_many_requests = 429,
    request_header_fields_too_large = 431,
    internal_server_error = 500,
    not_implemented = 501,
    bad_gateway = 502,
    service_unavailable = 503,
    gateway_timeout = 504,
};

// reason_phrase returns the RFC 9110 reason phrase for status, or an empty view for unnamed codes.
KI_EXPORT std::string_view reason_phrase(Status status) noexcept;

}  // namespace ki
