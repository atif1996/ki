#pragma once

#include <ki/request.hpp>
#include <ki_probe_export.hpp>

#include <optional>
#include <string>

// A shared library on the other side of a DLL boundary from the tests, for T1.5.

inline constexpr ki::ContextKey<std::string> probe_key{"test.probe"};
inline constexpr ki::ContextKey<int> probe_mismatch_key{"test.mismatch"};

KI_PROBE_EXPORT void probe_set(ki::Request& request, std::string value);
KI_PROBE_EXPORT std::optional<std::string> probe_get(const ki::Request& request);
KI_PROBE_EXPORT void probe_set_mismatch(ki::Request& request, int value);
