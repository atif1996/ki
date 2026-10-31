#pragma once

#include <ki/export.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace ki {

struct RouteParams {
    std::vector<std::string> keys{};
    std::vector<std::string> values{};

    void add(std::string_view key, std::string_view value) {
        keys.emplace_back(key);
        values.emplace_back(value);
    }
};

// RouteContext is a request's routing state, chi's chi.Context. Routers fill it in as the request passes
// through them; middleware may set route_path or route_method to change what the next router matches.
struct RouteContext {
    // route_path is the path the next router matches against; empty means the request's own path.
    std::string route_path{};
    // route_method is the method routers match against; empty means the request's method.
    std::string route_method{};
    // url_params holds every parameter matched so far, across mounted routers.
    RouteParams url_params{};
    // route_patterns holds the pattern matched by each router, outermost first.
    std::vector<std::string> route_patterns{};
    // route_params holds the parameters of the most recent router's match.
    RouteParams route_params{};
    // active is set by the first router to handle the request; nested routers reuse the context.
    bool active = false;

    // url_param returns the value of the most recently matched parameter named key, or empty.
    [[nodiscard]] KI_EXPORT std::string_view url_param(std::string_view key) const noexcept;

    // route_pattern joins route_patterns into the full matched pattern, such as "/users/{id}".
    [[nodiscard]] KI_EXPORT std::string route_pattern() const;
};

}  // namespace ki
