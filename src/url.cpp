#include <ki/url.hpp>

#include "detail/strings.hpp"

#include <asio/ip/address_v6.hpp>

#include <algorithm>
#include <system_error>

namespace ki::url {
namespace {

int hex_value(char c) noexcept {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

bool is_alpha(char c) noexcept { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }
bool is_unreserved(char c) noexcept { return is_alpha(c) || is_digit(c) || c == '-' || c == '.' || c == '_' || c == '~'; }
bool is_sub_delim(char c) noexcept { return std::string_view{"!$&'()*+,;="}.find(c) != std::string_view::npos; }

std::unexpected<Error> invalid(std::string message) {
    return std::unexpected(Error{.status = Status::bad_request, .message = std::move(message)});
}

bool valid_port(std::string_view port) noexcept { return std::ranges::all_of(port, is_digit); }

// valid_zone checks an RFC 6874 zone identifier: unreserved characters and %XX escapes, at least one.
bool valid_zone(std::string_view zone) noexcept {
    if (zone.empty()) {
        return false;
    }
    for (std::size_t i = 0; i < zone.size(); ++i) {
        if (zone[i] == '%') {
            if (i + 2 >= zone.size() || hex_value(zone[i + 1]) < 0 || hex_value(zone[i + 2]) < 0) {
                return false;
            }
            i += 2;
        } else if (!is_unreserved(zone[i])) {
            return false;
        }
    }
    return true;
}

bool valid_ipv6_literal(std::string_view literal) {
    std::string_view address = literal;
    if (const auto percent = literal.find('%'); percent != std::string_view::npos) {
        if (detail::sub(literal, percent, 3) != "%25" || !valid_zone(detail::sub(literal, percent + 3))) {
            return false;
        }
        address = detail::sub(literal, 0, percent);
    }
    std::error_code ec;
    (void)asio::ip::make_address_v6(std::string{address}, ec);
    return !ec;
}

bool valid_authority(std::string_view authority, bool require_port) {
    if (authority.empty() || authority.find('@') != std::string_view::npos) {
        return false;
    }
    std::string_view port;
    bool has_port = false;
    if (authority.front() == '[') {
        const auto close = authority.find(']');
        if (close == std::string_view::npos || !valid_ipv6_literal(detail::sub(authority, 1, close - 1))) {
            return false;
        }
        const auto rest = detail::sub(authority, close + 1);
        if (!rest.empty()) {
            if (rest.front() != ':') {
                return false;
            }
            port = detail::sub(rest, 1);
            has_port = true;
        }
    } else {
        std::string_view host = authority;
        if (const auto colon = authority.rfind(':'); colon != std::string_view::npos) {
            host = detail::sub(authority, 0, colon);
            port = detail::sub(authority, colon + 1);
            has_port = true;
        }
        const bool host_ok = !host.empty() && std::ranges::all_of(host, [](char c) {
            return is_unreserved(c) || is_sub_delim(c);
        });
        if (!host_ok) {
            return false;
        }
    }
    return valid_port(port) && (!require_port || (has_port && !port.empty()));
}

// split_path_query fills target's path and query from rest.
void split_path_query(std::string_view rest, Target& target) {
    const auto question = std::min(rest.find('?'), rest.size());
    target.path = detail::sub(rest, 0, question);
    if (question < rest.size()) {
        target.query = detail::sub(rest, question + 1);
    }
}

std::size_t scheme_length(std::string_view target) noexcept {
    if (target.empty() || !is_alpha(target.front())) {
        return 0;
    }
    const auto end = std::ranges::find_if_not(target, [](char c) {
        return is_alpha(c) || is_digit(c) || c == '+' || c == '-' || c == '.';
    });
    return static_cast<std::size_t>(end - target.begin());
}

char upper_hex(unsigned value) noexcept { return "0123456789ABCDEF"[value & 0xFU]; }

template <typename KeepFn>
std::string escape(std::string_view text, KeepFn keep, bool space_as_plus) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        if (keep(c)) {
            out.push_back(c);
        } else if (c == ' ' && space_as_plus) {
            out.push_back('+');
        } else {
            const auto byte = static_cast<unsigned char>(c);
            out.push_back('%');
            out.push_back(upper_hex(byte >> 4U));
            out.push_back(upper_hex(byte));
        }
    }
    return out;
}

}  // namespace

std::expected<Target, Error> parse_target(std::string_view method, std::string_view target) {
    if (target.empty()) {
        return invalid("empty request target");
    }
    const bool has_bad_char = std::ranges::any_of(target, [](char c) {
        const auto u = static_cast<unsigned char>(c);
        return u <= 0x20 || u == 0x7F;
    });
    if (has_bad_char) {
        return invalid("invalid character in request target");
    }

    Target out;
    if (target == "*") {
        if (method != "OPTIONS") {
            return invalid("'*' request target is only valid for OPTIONS");
        }
        out.form = TargetForm::asterisk;
        out.path = target;
        return out;
    }
    if (target.front() == '/') {
        split_path_query(target, out);
        return out;
    }
    if (method == "CONNECT") {
        if (!valid_authority(target, true)) {
            return invalid("invalid CONNECT authority");
        }
        out.form = TargetForm::authority;
        out.authority = target;
        return out;
    }

    const auto scheme = scheme_length(target);
    if (scheme == 0 || detail::sub(target, scheme, 3) != "://") {
        return invalid("invalid request target");
    }
    const auto authority_begin = scheme + 3;
    const auto authority_end = std::min(target.find_first_of("/?", authority_begin), target.size());
    out.form = TargetForm::absolute;
    out.scheme = detail::sub(target, 0, scheme);
    out.authority = detail::sub(target, authority_begin, authority_end - authority_begin);
    if (!valid_authority(out.authority, false)) {
        return invalid("invalid host in request target");
    }
    split_path_query(detail::sub(target, authority_end), out);
    return out;
}

bool needs_unescape(std::string_view text, Component component) noexcept {
    return text.find_first_of(component == Component::query ? std::string_view{"%+"} : std::string_view{"%"}) !=
           std::string_view::npos;
}

std::expected<std::string, Error> unescape(std::string_view text, Component component) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '%') {
            if (i + 2 >= text.size()) {
                return invalid("invalid URL escape");
            }
            const int high = hex_value(text[i + 1]);
            const int low = hex_value(text[i + 2]);
            if (high < 0 || low < 0) {
                return invalid("invalid URL escape");
            }
            out.push_back(static_cast<char>((static_cast<unsigned>(high) << 4U) | static_cast<unsigned>(low)));
            i += 2;
        } else if (c == '+' && component == Component::query) {
            out.push_back(' ');
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::string query_escape(std::string_view text) { return escape(text, is_unreserved, true); }

std::string path_escape(std::string_view text) {
    return escape(
        text, [](char c) { return is_unreserved(c) || std::string_view{"$&+:=@"}.find(c) != std::string_view::npos; },
        false);
}

}  // namespace ki::url
