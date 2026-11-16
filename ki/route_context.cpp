#include <ki/route_context.hpp>

#include <cstddef>

namespace ki {

std::string_view RouteContext::url_param(std::string_view key) const noexcept {
    for (std::size_t i = url_params.keys.size(); i-- > 0;) {
        if (url_params.keys[i] == key && i < url_params.values.size()) {
            return url_params.values[i];
        }
    }
    return {};
}

std::string RouteContext::route_pattern() const {
    std::string pattern{};
    for (const auto& part : route_patterns) {
        pattern += part;
    }
    for (auto at = pattern.find("/*/"); at != std::string::npos; at = pattern.find("/*/")) {
        pattern.replace(at, 3, "/");
    }
    if (pattern != "/") {
        if (pattern.ends_with("//")) {
            pattern.resize(pattern.size() - 2);
        } else if (pattern.ends_with('/')) {
            pattern.pop_back();
        }
    }
    return pattern;
}

}  // namespace ki
