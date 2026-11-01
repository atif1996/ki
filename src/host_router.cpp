#include <ki/router.hpp>

#include "detail/strings.hpp"

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace ki {

struct HostRouter::State {
    std::map<std::string, Handler, std::less<>> exact{};
    // wildcards maps a suffix such as ".example.com" to its handler.
    std::map<std::string, Handler, std::less<>> wildcards{};
    Handler fallback{};
    std::vector<Error> errors{};
};

namespace {

std::string lower(std::string_view text) {
    std::string out{text};
    std::ranges::transform(out, out.begin(), [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; });
    return out;
}

bool valid_host_name(std::string_view host) noexcept {
    return !host.empty() && std::ranges::all_of(host, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_';
    });
}

// host_without_port lowercases the request's host and removes any port; a bracketed IPv6 literal keeps
// its brackets.
std::string host_without_port(const Request& req) {
    std::string_view host = req.authority();
    if (host.empty()) {
        host = req.headers().get("Host").value_or("");
    }
    if (host.starts_with('[')) {
        return lower(detail::sub(host, 0, host.find(']') + 1));
    }
    return lower(detail::sub(host, 0, host.rfind(':')));
}

asio::awaitable<Result> not_found(Request&, Response& res) {
    (void)res.set_header("Content-Type", "text/plain; charset=utf-8");
    (void)res.set_header("X-Content-Type-Options", "nosniff");
    (void)res.write_header(Status::not_found);
    co_return co_await res.write("404 page not found\n");
}

}  // namespace

HostRouter::HostRouter() : state_{std::make_shared<State>()} {}

void HostRouter::route(std::string_view host, Handler handler) {
    const auto name = lower(host);
    const auto fail = [&](std::string reason) {
        state_->errors.push_back(Error{.status = Status::internal_server_error,
                                       .message = "host '" + std::string{host} + "': " + std::move(reason)});
    };
    if (!handler) {
        return fail("nil handler");
    }
    const bool wildcard = name.starts_with("*.");
    const std::string_view bare = wildcard ? detail::sub(name, 1) : std::string_view{name};
    if (!valid_host_name(wildcard ? detail::sub(bare, 1) : bare)) {
        return fail("not a host name without a port");
    }
    auto& table = wildcard ? state_->wildcards : state_->exact;
    if (!table.emplace(std::string{bare}, std::move(handler)).second) {
        return fail("already routed");
    }
}

void HostRouter::fallback(Handler handler) { state_->fallback = std::move(handler); }

std::vector<Error> HostRouter::errors() const { return state_->errors; }

asio::awaitable<Result> HostRouter::operator()(Request& request, Response& response) const {
    const auto state = state_;
    const auto host = host_without_port(request);
    Handler handler;
    if (const auto it = state->exact.find(host); it != state->exact.end()) {
        handler = it->second;
    } else {
        std::size_t longest = 0;
        for (const auto& [suffix, candidate] : state->wildcards) {
            if (host.ends_with(suffix) && host.size() > suffix.size() && suffix.size() > longest) {
                longest = suffix.size();
                handler = candidate;
            }
        }
    }
    if (!handler) {
        handler = state->fallback ? state->fallback : Handler{not_found};
    }
    co_return co_await handler(request, response);
}

}  // namespace ki
