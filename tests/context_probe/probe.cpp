#include "probe.hpp"

#include <utility>

void probe_set(ki::Request& request, std::string value) { request.set(probe_key, std::move(value)); }

std::optional<std::string> probe_get(const ki::Request& request) {
    if (auto value = request.get(probe_key)) {
        return value->get();
    }
    return std::nullopt;
}

void probe_set_mismatch(ki::Request& request, int value) { request.set(probe_mismatch_key, value); }
