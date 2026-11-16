#pragma once

#include <ki/error.hpp>
#include <ki/export.hpp>
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

}  // namespace ki
