#include <ki/helpers.hpp>

#include "detail/strings.hpp"
#include "detail/tree.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace ki {
namespace {

// clean_path is Go's path.Clean for rooted paths: it collapses repeated slashes and resolves "." and "..".
std::string clean_path(std::string_view path) {
    std::vector<std::string_view> parts;
    std::string_view rest = path;
    while (!rest.empty()) {
        const auto slash = rest.find('/');
        const auto part = detail::sub(rest, 0, slash);
        rest = slash == std::string_view::npos ? std::string_view{} : detail::sub(rest, slash + 1);
        if (part.empty() || part == ".") {
            continue;
        }
        if (part == "..") {
            if (!parts.empty()) {
                parts.pop_back();
            }
            continue;
        }
        parts.push_back(part);
    }
    std::string out;
    for (const auto part : parts) {
        out.push_back('/');
        out.append(part);
    }
    return out.empty() ? "/" : out;
}

bool has_scheme(std::string_view url) noexcept {
    const auto colon = url.find(':');
    if (colon == std::string_view::npos || colon == 0) {
        return false;
    }
    const auto scheme = detail::sub(url, 0, colon);
    const auto alpha = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); };
    return alpha(scheme.front()) && std::ranges::all_of(scheme, [&](char c) {
               return alpha(c) || (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.';
           }) && url.find_first_of("/?#") > colon;
}

// is_relative reports whether url has neither a scheme nor a host, as Go's url.Parse would see it; "///x"
// has an empty host and so counts as relative.
bool is_relative(std::string_view url) noexcept {
    if (has_scheme(url)) {
        return false;
    }
    return !(url.starts_with("//") && !url.starts_with("///"));
}

std::string hex_escape_non_ascii(std::string_view url) {
    constexpr std::string_view hex = "0123456789abcdef";
    std::string out;
    for (const char c : url) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 0x80) {
            out.push_back(c);
        } else {
            out.push_back('%');
            out.push_back(hex[byte >> 4U]);
            out.push_back(hex[byte & 0xFU]);
        }
    }
    return out;
}

std::string html_escape(std::string_view text) {
    std::string out;
    for (const char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&#34;"; break;
            case '\'': out += "&#39;"; break;
            default: out.push_back(c);
        }
    }
    return out;
}

}  // namespace

asio::awaitable<Result> redirect(Request& req, Response& res, std::string_view url, Status status) {
    std::string target{url};
    if (is_relative(url)) {
        const std::string old_path = req.raw_path().empty() ? std::string{"/"} : std::string{req.raw_path()};
        if (target.empty() || target.front() != '/') {
            target = std::string{detail::sub(old_path, 0, old_path.rfind('/') + 1)} + target;
        }
        std::string query;
        if (const auto question = target.find('?'); question != std::string::npos) {
            query = std::string{detail::sub(target, question)};
            target.resize(question);
        }
        const bool trailing = target.ends_with('/');
        target = clean_path(target);
        if (trailing && !target.ends_with('/')) {
            target.push_back('/');
        }
        target += query;
    }
    target = hex_escape_non_ascii(target);

    const bool had_content_type = res.headers().contains("Content-Type");
    if (auto location = res.set_header("Location", target); !location) {
        co_return location;
    }
    if (!had_content_type && (req.method() == "GET" || req.method() == "HEAD")) {
        (void)res.set_header("Content-Type", "text/html; charset=utf-8");
    }
    if (auto written = res.write_header(status); !written) {
        co_return written;
    }
    if (!had_content_type && req.method() == "GET") {
        co_return co_await res.write("<a href=\"" + html_escape(target) + "\">" + std::string{reason_phrase(status)} +
                                     "</a>.\n\n");
    }
    co_return Result{};
}

Handler strip_prefix(std::string_view prefix, Handler next) {
    return [prefix = detail::routing_path(prefix), next = std::move(next)](Request& req,
                                                                        Response& res) -> asio::awaitable<Result> {
        auto& context = req.route();
        const std::string path = context.route_path.empty() ? detail::routing_path(req.raw_path()) : context.route_path;
        if (!path.starts_with(prefix)) {
            (void)res.set_header("Content-Type", "text/plain; charset=utf-8");
            (void)res.set_header("X-Content-Type-Options", "nosniff");
            (void)res.write_header(Status::not_found);
            co_return co_await res.write("404 page not found\n");
        }
        const auto rest = detail::sub(path, prefix.size());
        context.route_path = rest.empty() ? std::string{"/"} : std::string{rest};
        co_return co_await next(req, res);
    };
}

}  // namespace ki
