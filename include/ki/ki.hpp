#pragma once

#include <ki/export.hpp>
#include <ki/version.hpp>

#include <string_view>

namespace ki {

// version returns the version of the linked ki library, which differs from KI_VERSION_STRING when
// the headers and the library come from different releases.
KI_EXPORT std::string_view version() noexcept;

}  // namespace ki
