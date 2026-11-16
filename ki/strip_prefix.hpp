#pragma once

#include <ki/export.hpp>
#include <ki/handler.hpp>

#include <string_view>

namespace ki {

// strip_prefix returns a handler that removes prefix from the path the next router matches and calls next,
// or answers 404 when the path does not start with prefix, as Go's http.StripPrefix does. The prefix is
// compared after decoding, so "/foo%2Fbar" does not match the prefix "/foo/bar". The request itself is not
// changed; the remaining path is in req.route().route_path.
[[nodiscard]] KI_EXPORT Handler strip_prefix(std::string_view prefix, Handler next);

}  // namespace ki
