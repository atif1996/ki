#pragma once

#include <ki/error.hpp>
#include <ki/export.hpp>
#include <ki/handler.hpp>
#include <ki/request.hpp>
#include <ki/response.hpp>
#include <ki/route_context.hpp>

#include <asio/awaitable.hpp>

#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ki {

namespace detail {
struct RouterState;
}

class Router;

// RouteEndpoint is one method's handler on a route: handler with inline middlewares applied, and the
// endpoint and middlewares it was built from.
struct RouteEndpoint {
    Handler handler{};
    Handler endpoint{};
    std::vector<Middleware> middlewares{};
};

struct Route {
    std::string pattern{};
    // handlers maps method names, and "*" for a route registered for every method, to endpoints.
    std::map<std::string, RouteEndpoint, std::less<>> handlers{};
    // subroutes is the router mounted on this route, if any.
    std::shared_ptr<const Router> subroutes{};
};

// Router is chi's Mux: a radix-tree router with middleware stacks, inline groups and mounted sub-routers.
// A Router is a handle to shared state, so copies refer to the same routes, and a Router is itself a
// Handler.
//
// Registration never fails silently and never throws: a rejected registration (bad pattern, unsupported
// method, middleware after routes, mounting on an existing path, a router frozen by the server) leaves the
// router unchanged and is recorded in errors(). A server refuses to start a router with errors.
class Router {
public:
    KI_EXPORT Router();

    // use appends middlewares to this router's stack; they must come before any route.
    KI_EXPORT void use(std::initializer_list<Middleware> middlewares);

    // with returns an inline router that adds middlewares to the routes registered through it.
    [[nodiscard]] KI_EXPORT Router with(std::initializer_list<Middleware> middlewares);
    // group calls fn with an inline router that has its own copy of the middleware stack.
    KI_EXPORT Router group(const std::function<void(Router&)>& fn);
    // route builds a sub-router with fn and mounts it at pattern.
    KI_EXPORT Router route(std::string_view pattern, const std::function<void(Router&)>& fn);
    // mount attaches a handler or router at pattern; requests under it continue routing there.
    KI_EXPORT void mount(std::string_view pattern, Handler handler);
    KI_EXPORT void mount(std::string_view pattern, const Router& router);

    // subtree routes pattern, which must end in '/', and every path below it to handler, as a Go ServeMux
    // pattern ending in '/' does. The same path without the slash redirects to it with 307, keeping the
    // query, unless that path has a route of its own. A "POST /files/" style pattern limits it to one method.
    KI_EXPORT void subtree(std::string_view pattern, Handler handler);

    // handle registers handler for every method, or for one method with a "GET /path" style pattern.
    KI_EXPORT void handle(std::string_view pattern, Handler handler);
    KI_EXPORT void method(std::string_view method, std::string_view pattern, Handler handler);
    void connect(std::string_view pattern, Handler handler) { method("CONNECT", pattern, std::move(handler)); }
    void del(std::string_view pattern, Handler handler) { method("DELETE", pattern, std::move(handler)); }
    void get(std::string_view pattern, Handler handler) { method("GET", pattern, std::move(handler)); }
    void head(std::string_view pattern, Handler handler) { method("HEAD", pattern, std::move(handler)); }
    void options(std::string_view pattern, Handler handler) { method("OPTIONS", pattern, std::move(handler)); }
    void patch(std::string_view pattern, Handler handler) { method("PATCH", pattern, std::move(handler)); }
    void post(std::string_view pattern, Handler handler) { method("POST", pattern, std::move(handler)); }
    void put(std::string_view pattern, Handler handler) { method("PUT", pattern, std::move(handler)); }
    void query(std::string_view pattern, Handler handler) { method("QUERY", pattern, std::move(handler)); }
    void trace(std::string_view pattern, Handler handler) { method("TRACE", pattern, std::move(handler)); }

    // register_method adds a custom method this router can route, such as "LINK".
    KI_EXPORT void register_method(std::string_view method);

    // not_found and method_not_allowed replace the default 404 and 405 responses, here and in mounted
    // routers that have not set their own.
    KI_EXPORT void not_found(Handler handler);
    KI_EXPORT void method_not_allowed(Handler handler);

    [[nodiscard]] KI_EXPORT std::vector<Route> routes() const;
    [[nodiscard]] KI_EXPORT std::vector<Middleware> middlewares() const;

    // find returns the full pattern that would serve method and path, following mounted routers, and
    // records the matched parameters in context; match reports whether there is one.
    [[nodiscard]] KI_EXPORT std::optional<std::string> find(std::string_view method, std::string_view path,
                                                            RouteContext& context) const;
    [[nodiscard]] std::optional<std::string> find(std::string_view method, std::string_view path) const {
        RouteContext context{};
        return find(method, path, context);
    }
    [[nodiscard]] bool match(std::string_view method, std::string_view path) const {
        return find(method, path).has_value();
    }

    // errors returns the registration errors of this router and every router mounted in it.
    [[nodiscard]] KI_EXPORT std::vector<Error> errors() const;

    // freeze stops further registration here and in mounted routers; the server calls it before serving,
    // so routing never races with registration.
    KI_EXPORT void freeze();

    KI_EXPORT asio::awaitable<Result> operator()(Request& request, Response& response) const;

private:
    explicit Router(std::shared_ptr<detail::RouterState> state) : state_{std::move(state)} {}

    std::shared_ptr<detail::RouterState> state_{};
};

// chain wraps endpoint in middlewares, the first outermost, as chi's Chain(...).Handler(endpoint) does.
[[nodiscard]] KI_EXPORT Handler chain(std::span<const Middleware> middlewares, Handler endpoint);

// HostRouter sends each request to the handler registered for its host, as go-chi/hostrouter does. Hosts
// are compared case-insensitively without the port; "*.example.com" matches any subdomain of example.com
// (the longest matching suffix wins), and fallback serves hosts with no route, otherwise 404. Like Router,
// it is a handle to shared state and records rejected registrations in errors().
class HostRouter {
public:
    KI_EXPORT HostRouter();

    KI_EXPORT void route(std::string_view host, Handler handler);
    KI_EXPORT void fallback(Handler handler);
    [[nodiscard]] KI_EXPORT std::vector<Error> errors() const;

    KI_EXPORT asio::awaitable<Result> operator()(Request& request, Response& response) const;

private:
    struct State;
    std::shared_ptr<State> state_;
};

// WalkFn receives each route's method, full pattern, endpoint handler and the middlewares that apply to it.
using WalkFn = std::function<Result(std::string_view method, std::string_view route, const Handler& handler,
                                    std::span<const Middleware> middlewares)>;

// walk visits every route of router and the routers mounted in it, stopping at the first error fn returns.
KI_EXPORT Result walk(const Router& router, const WalkFn& fn);

}  // namespace ki
