#include <ki/strip_prefix.hpp>

#include <ki/detail/strings.hpp>
#include <ki/request.hpp>
#include <ki/response.hpp>
#include <ki/detail/tree.hpp>

#include <string>
#include <utility>

namespace ki {

Handler strip_prefix(std::string_view prefix, Handler next) {
    return [prefix = detail::routing_path(prefix), next = std::move(next)](Request& req,
                                                                        Response& res) -> asio::awaitable<Result> {
        auto& context = req.route();
        const std::string path = context.route_path.empty() ? detail::routing_path(req.raw_path()) : context.route_path;
        if (!path.starts_with(prefix)) {
            (void)res.set_header("Content-Type", "text/plain; charset=utf-8");
            (void)res.set_header("X-Content-Type-Options", "nosniff");
            (void)res.write_header(Status::not_found);
            co_return co_await res.write("404 page not found\n");
        }
        const auto rest = detail::sub(path, prefix.size());
        context.route_path = rest.empty() ? std::string{"/"} : std::string{rest};
        co_return co_await next(req, res);
    };
}

}  // namespace ki
