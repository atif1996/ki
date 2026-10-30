#pragma once

#include <ki/context.hpp>
#include <ki/error.hpp>
#include <ki/export.hpp>
#include <ki/handler.hpp>
#include <ki/headers.hpp>
#include <ki/request.hpp>
#include <ki/response.hpp>
#include <ki/status.hpp>
#include <ki/version.hpp>

#include <string_view>

namespace ki {

// version returns the version of the linked ki library, which differs from KI_VERSION_STRING when
// the headers and the library come from different releases.
KI_EXPORT std::string_view version() noexcept;

}  // namespace ki
