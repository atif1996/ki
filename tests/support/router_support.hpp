#pragma once

#include <ki/router.hpp>
#include <ki/test/recorder.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace ki::testing {

// Hit is what a named handler saw: its name and the route parameters it was given.
struct Hit {
    ki::Status status = ki::Status::ok;
    std::string name{};
    std::vector<std::string> keys{};
    std::vector<std::string> values{};
    std::string body{};
};

// named returns a handler that reports its name and the request's route parameters, so tests can tell
// which handler a request reached the way chi's tests compare handler identities.
inline ki::Handler named(std::string name) {
    return [name = std::move(name)](ki::Request& req, ki::Response& res) -> asio::awaitable<ki::Result> {
        std::string body = name;
        const auto& params = req.route().url_params;
        for (std::size_t i = 0; i < params.keys.size(); ++i) {
            body += '\x1f';
            body += params.keys[i];
            body += '=';
            body += i < params.values.size() ? params.values[i] : std::string{};
        }
        co_return co_await res.write(body);
    };
}

inline Hit serve(const ki::Router& router, std::string method, std::string target) {
    auto request = ki::test::make_request(std::move(method), std::move(target));
    const auto out = ki::test::Recorder::run(router, request);
    Hit hit{.status = out.status, .name = {}, .keys = {}, .values = {}, .body = out.body};
    if (out.status != ki::Status::ok) {
        return hit;
    }
    std::string_view body = out.body;
    const auto first = body.find('\x1f');
    hit.name = std::string{body.substr(0, first)};
    while (first != std::string_view::npos && !body.empty()) {
        const auto start = body.find('\x1f');
        if (start == std::string_view::npos) {
            break;
        }
        body.remove_prefix(start + 1);
        const auto end = std::min(body.find('\x1f'), body.size());
        const auto pair = body.substr(0, end);
        const auto equals = pair.find('=');
        hit.keys.emplace_back(pair.substr(0, equals));
        hit.values.emplace_back(pair.substr(equals + 1));
        body.remove_prefix(end);
    }
    return hit;
}

}  // namespace ki::testing
