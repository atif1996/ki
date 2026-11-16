#pragma once

#include <ki/error.hpp>
#include <ki/request.hpp>
#include <ki/response.hpp>

#include <asio/awaitable.hpp>

#include <functional>

namespace ki {

// std::function rather than std::move_only_function, which libc++ does not ship yet; a handler that needs
// move-only state holds it through a std::shared_ptr.
using Handler = std::function<asio::awaitable<Result>(Request&, Response&)>;

// Middleware wraps a handler in another handler, as a chi middleware wraps an http.Handler.
using Middleware = std::function<Handler(Handler)>;

}  // namespace ki
