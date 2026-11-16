#include <ki/status.hpp>

namespace ki {

std::string_view reason_phrase(Status status) noexcept {
    switch (status) {
        case Status::continue_: return "Continue";
        case Status::switching_protocols: return "Switching Protocols";
        case Status::ok: return "OK";
        case Status::created: return "Created";
        case Status::accepted: return "Accepted";
        case Status::no_content: return "No Content";
        case Status::moved_permanently: return "Moved Permanently";
        case Status::found: return "Found";
        case Status::see_other: return "See Other";
        case Status::not_modified: return "Not Modified";
        case Status::temporary_redirect: return "Temporary Redirect";
        case Status::permanent_redirect: return "Permanent Redirect";
        case Status::bad_request: return "Bad Request";
        case Status::unauthorized: return "Unauthorized";
        case Status::forbidden: return "Forbidden";
        case Status::not_found: return "Not Found";
        case Status::method_not_allowed: return "Method Not Allowed";
        case Status::request_timeout: return "Request Timeout";
        case Status::conflict: return "Conflict";
        case Status::content_too_large: return "Content Too Large";
        case Status::uri_too_long: return "URI Too Long";
        case Status::unsupported_media_type: return "Unsupported Media Type";
        case Status::unprocessable_content: return "Unprocessable Content";
        case Status::too_many_requests: return "Too Many Requests";
        case Status::request_header_fields_too_large: return "Request Header Fields Too Large";
        case Status::internal_server_error: return "Internal Server Error";
        case Status::not_implemented: return "Not Implemented";
        case Status::bad_gateway: return "Bad Gateway";
        case Status::service_unavailable: return "Service Unavailable";
        case Status::gateway_timeout: return "Gateway Timeout";
    }
    return {};
}

}  // namespace ki
