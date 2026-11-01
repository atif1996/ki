#pragma once

#include <ki/error.hpp>
#include <ki/export.hpp>
#include <ki/handler.hpp>
#include <ki/request.hpp>
#include <ki/response.hpp>
#include <ki/status.hpp>

#include <asio/awaitable.hpp>

#include <string_view>

namespace ki {

// redirect replies with a redirect to url, as Go's http.Redirect does: a relative url is resolved against the
// request path and cleaned (keeping a trailing slash), non-ASCII bytes are percent-encoded, and GET requests
// get a short HTML body unless a Content-Type was already set. A url containing control characters fails,
// so it can never inject a header.
KI_EXPORT asio::awaitable<Result> redirect(Request& req, Response& res, std::string_view url, Status status);

// strip_prefix returns a handler that removes prefix from the path the next router matches and calls next,
// or answers 404 when the path does not start with prefix, as Go's http.StripPrefix does. The prefix is
// compared after decoding, so "/foo%2Fbar" does not match the prefix "/foo/bar". The request itself is not
// changed; the remaining path is in req.route().route_path.
[[nodiscard]] KI_EXPORT Handler strip_prefix(std::string_view prefix, Handler next);

}  // namespace ki
