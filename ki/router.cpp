#include <ki/router.hpp>

#include <ki/redirect.hpp>

#include <ki/detail/strings.hpp>
#include <ki/detail/tree.hpp>

#include <algorithm>
#include <cstdint>
#include <ranges>
#include <utility>

namespace ki::detail {

// TreeState is shared by a router and the inline routers derived from it with with() and group().
// Claim records who registered a method on a pattern shape, so duplicates can be rejected. Implicit claims
// (a mount's trailing-slash stubs) may be overridden by an explicit route; explicit claims may not.
struct Claim {
    std::string pattern{};
    bool implicit = false;
};

struct TreeState {
    Tree tree{};
    // claims maps a pattern shape to its registered methods; "*" stands for a registration for every method.
    std::map<std::string, std::map<std::string, Claim, std::less<>>, std::less<>> claims{};
    std::vector<std::string> methods{"CONNECT", "DELETE", "GET", "HEAD", "OPTIONS",
                                     "PATCH", "POST", "PUT", "QUERY", "TRACE"};
    std::vector<Error> errors{};
    std::uint64_t next_id = 1;
    bool frozen = false;
};

struct RouterState {
    std::shared_ptr<TreeState> shared = std::make_shared<TreeState>();
    // handler is the middleware stack wrapped around routing; it is built when the first route is added,
    // after which use() is rejected.
    Handler handler{};
    Handler not_found{};
    Handler method_not_allowed{};
    std::weak_ptr<RouterState> parent{};
    std::vector<Middleware> middlewares{};
    bool is_inline = false;
};

}  // namespace ki::detail

namespace ki {
namespace {

using detail::RouterState;

std::string upper(std::string_view text) {
    std::string out{text};
    std::ranges::transform(out, out.begin(), [](char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c; });
    return out;
}

void record(RouterState& state, std::string message) {
    state.shared->errors.push_back(Error{.status = Status::internal_server_error, .message = std::move(message)});
}

asio::awaitable<Result> default_not_found(Request&, Response& res) {
    (void)res.set_header("Content-Type", "text/plain; charset=utf-8");
    (void)res.set_header("X-Content-Type-Options", "nosniff");
    (void)res.write_header(Status::not_found);
    co_return co_await res.write("404 page not found\n");
}

Handler default_method_not_allowed(std::vector<std::string> allowed) {
    return [allowed = std::move(allowed)](Request&, Response& res) -> asio::awaitable<Result> {
        for (const auto& method : allowed) {
            (void)res.add_header("Allow", method);
        }
        (void)res.write_header(Status::method_not_allowed);
        co_return Result{};
    };
}

Handler not_found_of(const RouterState& state) { return state.not_found ? state.not_found : Handler{default_not_found}; }

Handler method_not_allowed_of(const RouterState& state, std::vector<std::string> allowed) {
    return state.method_not_allowed ? state.method_not_allowed : default_method_not_allowed(std::move(allowed));
}

// next_route_path is the path a mounted router sees: what the parent's catch-all matched.
std::string next_route_path(const RouteContext& context) { return "/" + context.rest_path; }

void record_match(RouteContext& context, const detail::FindState& found) {
    context.route_params.keys = found.keys;
    context.route_params.values.clear();
    for (const auto value : found.values) {
        context.route_params.values.push_back(detail::decode_value(value));
    }
    context.rest_path.clear();
    if (!found.keys.empty() && found.keys.back() == "*" && found.values.size() >= found.keys.size()) {
        context.rest_path = std::string{found.values[found.keys.size() - 1]};
    }
}

// handler_for returns the endpoint serving method on node, if it has a handler.
std::optional<std::reference_wrapper<const detail::Endpoint>> handler_for(const detail::Node& node,
                                                                         std::string_view method) {
    if (!node.endpoints) {
        return std::nullopt;
    }
    const auto it = node.endpoints->find(method);
    if (it == node.endpoints->end() || !it->second.handler) {
        return std::nullopt;
    }
    return it->second;
}

asio::awaitable<Result> route_request(std::shared_ptr<RouterState> state, Request& req, Response& res) {
    auto& context = req.route();
    std::string path = context.route_path.empty() ? detail::routing_path(req.raw_path()) : context.route_path;
    if (path.empty()) {
        path = "/";
    }
    if (context.route_method.empty()) {
        context.route_method = std::string{req.method()};
    }
    const std::string method = context.route_method;
    if (!std::ranges::contains(state->shared->methods, method)) {
        co_return co_await method_not_allowed_of(*state, {})(req, res);
    }

    detail::FindState found{};
    const auto node = state->shared->tree.find(method, path, found);
    record_match(context, found);
    if (node) {
        if (const auto endpoint = handler_for(node->get(), method)) {
            const auto& params = context.route_params;
            for (std::size_t i = 0; i < params.keys.size() && i < params.values.size(); ++i) {
                context.url_params.add(params.keys[i], params.values[i]);
            }
            if (!endpoint->get().pattern.empty()) {
                context.route_patterns.push_back(endpoint->get().pattern);
            }
            const Handler handler = endpoint->get().handler;
            co_return co_await handler(req, res);
        }
    }
    if (found.method_not_allowed) {
        co_return co_await method_not_allowed_of(*state, found.methods_allowed)(req, res);
    }
    co_return co_await not_found_of(*state)(req, res);
}

// router_handler routes through state without owning it, so the state's own handler does not keep it alive.
Handler router_handler(const std::shared_ptr<RouterState>& state) {
    return [weak = std::weak_ptr<RouterState>{state}](Request& req, Response& res) -> asio::awaitable<Result> {
        auto locked = weak.lock();
        if (!locked) {
            co_return std::unexpected(Error{.message = "router destroyed while serving"});
        }
        co_return co_await route_request(std::move(locked), req, res);
    };
}

void build_route_handler(const std::shared_ptr<RouterState>& state) {
    state->handler = chain(state->middlewares, router_handler(state));
}

void for_each_subrouter(const detail::Node& node, const std::function<void(RouterState&)>& fn) {
    if (node.subroutes) {
        fn(*node.subroutes);
    }
    for (const auto& children : node.children) {
        for (const auto& child : children) {
            for_each_subrouter(*child, fn);
        }
    }
}

void set_not_found(RouterState& state, const Handler& handler) {
    state.not_found = handler;
    for_each_subrouter(state.shared->tree.root(), [&](RouterState& sub) {
        if (!sub.not_found) {
            set_not_found(sub, handler);
        }
    });
}

void set_method_not_allowed(RouterState& state, const Handler& handler) {
    state.method_not_allowed = handler;
    for_each_subrouter(state.shared->tree.root(), [&](RouterState& sub) {
        if (!sub.method_not_allowed) {
            set_method_not_allowed(sub, handler);
        }
    });
}

struct Registration {
    detail::Methods methods{};
    std::string_view pattern{};
    Handler handler{};
    std::uint64_t id = 0;
    bool implicit = false;
};

std::string describe(const detail::Methods& methods, std::string_view pattern) {
    return (methods.all ? std::string{} : methods.names.front() + " ") + std::string{pattern};
}

// check_claim decides which methods a registration sets on a shape, or reports the registration it
// conflicts with. An explicit registration for every method skips methods already registered explicitly,
// so the more specific route keeps them, as Go's ServeMux prefers a method-specific pattern.
std::expected<std::vector<std::string>, std::string> check_claim(const detail::TreeState& shared, const std::string& shape,
                                                                 const Registration& registration) {
    const auto existing = shared.claims.find(shape);
    const auto conflict = [&](const std::string& method, const detail::Claim& claim) {
        return std::unexpected("pattern '" + describe(registration.methods, registration.pattern) + "' conflicts with '" +
                               (method == "*" ? std::string{} : method + " ") + claim.pattern + "', registered earlier");
    };
    if (existing == shared.claims.end()) {
        return registration.methods.names;
    }
    if (registration.implicit) {
        const auto& [method, claim] = *existing->second.begin();
        return conflict(method, claim);
    }
    if (!registration.methods.all) {
        const auto& name = registration.methods.names.front();
        if (const auto it = existing->second.find(name); it != existing->second.end() && !it->second.implicit) {
            return conflict(name, it->second);
        }
        return registration.methods.names;
    }
    if (const auto it = existing->second.find("*"); it != existing->second.end() && !it->second.implicit) {
        return conflict("*", it->second);
    }
    std::vector<std::string> names;
    for (const auto& name : registration.methods.names) {
        const auto it = existing->second.find(name);
        if (it == existing->second.end() || it->second.implicit) {
            names.push_back(name);
        }
    }
    return names;
}

void commit_claim(detail::TreeState& shared, const std::string& shape, const Registration& registration) {
    auto& methods = shared.claims[shape];
    const detail::Claim claim{.pattern = std::string{registration.pattern}, .implicit = registration.implicit};
    if (registration.methods.all) {
        methods.insert_or_assign("*", claim);
    } else {
        methods.insert_or_assign(registration.methods.names.front(), claim);
    }
}

// add_route is chi's handle(): validate, build the router handler on first use, wrap inline middlewares,
// and insert. It returns the leaf node, or nothing when the registration was rejected and recorded.
std::optional<std::reference_wrapper<detail::Node>> add_route(const std::shared_ptr<RouterState>& state,
                                                              const Registration& registration) {
    auto& shared = *state->shared;
    if (shared.frozen) {
        record(*state, "routes cannot be added after the router is frozen");
        return std::nullopt;
    }
    if (!registration.handler) {
        record(*state, "nil handler for '" + std::string{registration.pattern} + "'");
        return std::nullopt;
    }
    if (registration.pattern.empty() || registration.pattern.front() != '/') {
        record(*state, "routing pattern must begin with '/' in '" + std::string{registration.pattern} + "'");
        return std::nullopt;
    }
    auto keys = detail::Tree::parse(registration.pattern);
    if (!keys) {
        shared.errors.push_back(std::move(keys.error()));
        return std::nullopt;
    }
    const auto routing = detail::routing_pattern(registration.pattern);
    const auto shape = detail::pattern_shape(routing);
    auto names = check_claim(shared, shape, registration);
    if (!names) {
        record(*state, std::move(names.error()));
        return std::nullopt;
    }

    if (!state->is_inline && !state->handler) {
        build_route_handler(state);
    }
    detail::Endpoint endpoint{.endpoint = registration.handler,
                              .id = registration.id != 0 ? registration.id : shared.next_id++,
                              .pattern = std::string{registration.pattern},
                              .param_keys = std::move(*keys)};
    if (state->is_inline) {
        state->handler = router_handler(state);
        endpoint.handler = chain(state->middlewares, registration.handler);
        endpoint.middlewares = state->middlewares;
    } else {
        endpoint.handler = registration.handler;
    }
    detail::Methods methods = registration.methods;
    methods.names = std::move(*names);
    commit_claim(shared, shape, registration);
    return shared.tree.insert(methods, routing, endpoint);
}

detail::Methods every_method(const RouterState& state, bool stub = false) {
    return {.names = state.shared->methods, .all = true, .stub = stub};
}

std::optional<std::string> find_in(const RouterState& state, RouteContext& context, std::string_view method,
                                   std::string_view path) {
    const auto& shared = *state.shared;
    if (!std::ranges::contains(shared.methods, method)) {
        return std::nullopt;
    }
    detail::FindState found{};
    const auto node = shared.tree.find(method, path, found);
    record_match(context, found);
    if (!node) {
        return std::nullopt;
    }
    const auto endpoint = handler_for(node->get(), method);
    if (!endpoint) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < context.route_params.keys.size() && i < context.route_params.values.size(); ++i) {
        context.url_params.add(context.route_params.keys[i], context.route_params.values[i]);
    }
    if (!node->get().subroutes) {
        return endpoint->get().pattern;
    }
    context.route_path = next_route_path(context);
    const std::string sub_path = context.route_path;
    auto sub_pattern = find_in(*node->get().subroutes, context, method, sub_path);
    if (!sub_pattern) {
        return std::nullopt;
    }
    std::string pattern = endpoint->get().pattern;
    if (pattern.ends_with("/*")) {
        pattern.resize(pattern.size() - 2);
    }
    return pattern + *sub_pattern;
}

void collect_errors(const RouterState& state, std::vector<Error>& out) {
    out.insert(out.end(), state.shared->errors.begin(), state.shared->errors.end());
    for_each_subrouter(state.shared->tree.root(), [&](RouterState& sub) { collect_errors(sub, out); });
}

void freeze_all(RouterState& state) {
    state.shared->frozen = true;
    for_each_subrouter(state.shared->tree.root(), [](RouterState& sub) { freeze_all(sub); });
}

Result walk_routes(const Router& router, const WalkFn& fn, const std::string& parent_route,
                   const std::vector<Middleware>& parent_middlewares) {
    for (const auto& route : router.routes()) {
        auto middlewares = parent_middlewares;
        const auto own = router.middlewares();
        middlewares.insert(middlewares.end(), own.begin(), own.end());

        if (route.subroutes) {
            if (const auto it = route.handlers.find("*"); it != route.handlers.end()) {
                middlewares.insert(middlewares.end(), it->second.middlewares.begin(), it->second.middlewares.end());
            }
            if (auto result = walk_routes(*route.subroutes, fn, parent_route + route.pattern, middlewares); !result) {
                return result;
            }
            continue;
        }
        for (const auto& [method, handler] : route.handlers) {
            if (method == "*") {
                continue;
            }
            std::string full = parent_route + route.pattern;
            for (auto at = full.find("/*/"); at != std::string::npos; at = full.find("/*/")) {
                full.replace(at, 3, "/");
            }
            auto applied = middlewares;
            applied.insert(applied.end(), handler.middlewares.begin(), handler.middlewares.end());
            if (auto result = fn(method, full, handler.endpoint, applied); !result) {
                return result;
            }
        }
    }
    return {};
}

}  // namespace

Router::Router() : state_{std::make_shared<RouterState>()} {}

void Router::use(std::initializer_list<Middleware> middlewares) {
    if (state_->shared->frozen) {
        record(*state_, "middlewares cannot be added after the router is frozen");
        return;
    }
    if (state_->handler) {
        record(*state_, "all middlewares must be defined before routes on a router");
        return;
    }
    state_->middlewares.insert(state_->middlewares.end(), middlewares.begin(), middlewares.end());
}

Router Router::with(std::initializer_list<Middleware> middlewares) {
    if (!state_->is_inline && !state_->handler) {
        build_route_handler(state_);
    }
    auto child = std::make_shared<RouterState>();
    child->shared = state_->shared;
    child->is_inline = true;
    child->parent = state_;
    if (state_->is_inline) {
        child->middlewares = state_->middlewares;
    }
    child->middlewares.insert(child->middlewares.end(), middlewares.begin(), middlewares.end());
    child->not_found = state_->not_found;
    child->method_not_allowed = state_->method_not_allowed;
    return Router{std::move(child)};
}

Router Router::group(const std::function<void(Router&)>& fn) {
    Router inline_router = with({});
    if (fn) {
        fn(inline_router);
    }
    return inline_router;
}

Router Router::route(std::string_view pattern, const std::function<void(Router&)>& fn) {
    Router sub{};
    if (!fn) {
        record(*state_, "attempting to route() a nil sub-router on '" + std::string{pattern} + "'");
        return sub;
    }
    fn(sub);
    mount(pattern, sub);
    return sub;
}

namespace {

void mount_impl(const std::shared_ptr<RouterState>& state, std::string_view pattern, Handler handler,
                const std::shared_ptr<RouterState>& sub) {
    auto& shared = *state->shared;
    if (!handler) {
        record(*state, "attempting to mount() a nil handler on '" + std::string{pattern} + "'");
        return;
    }
    const std::string base{pattern};
    if (base.empty() || base.front() != '/') {
        record(*state, "routing pattern must begin with '/' in '" + base + "'");
        return;
    }
    if (shared.tree.find_pattern(detail::routing_pattern(base + "*")) ||
        shared.tree.find_pattern(detail::routing_pattern(base + "/*"))) {
        record(*state, "attempting to mount() a handler on an existing path, '" + base + "'");
        return;
    }
    if (sub && sub->shared == state->shared) {
        record(*state, "attempting to mount() a router onto itself on '" + base + "'");
        return;
    }
    const std::string mount_pattern = base.ends_with('/') ? base : base + "/";
    const std::string wildcard = mount_pattern + "*";
    if (auto keys = detail::Tree::parse(wildcard); !keys) {
        shared.errors.push_back(std::move(keys.error()));
        return;
    }
    // Check every pattern the mount registers before registering any, so a rejected mount adds nothing.
    std::vector<Registration> registrations;
    if (!base.ends_with('/')) {
        registrations.push_back({.methods = every_method(*state, true), .pattern = base, .implicit = true});
        registrations.push_back({.methods = every_method(*state, true), .pattern = mount_pattern, .implicit = true});
    }
    registrations.push_back({.methods = every_method(*state, static_cast<bool>(sub)), .pattern = wildcard});
    for (const auto& registration : registrations) {
        const auto shape = detail::pattern_shape(detail::routing_pattern(registration.pattern));
        if (auto names = check_claim(shared, shape, registration); !names) {
            record(*state, std::move(names.error()));
            return;
        }
    }

    if (sub && !sub->not_found && state->not_found) {
        set_not_found(*sub, state->not_found);
    }
    if (sub && !sub->method_not_allowed && state->method_not_allowed) {
        set_method_not_allowed(*sub, state->method_not_allowed);
    }

    const Handler mount_handler = [handler = std::move(handler)](Request& req, Response& res) -> asio::awaitable<Result> {
        auto& context = req.route();
        context.route_path = next_route_path(context);
        // The catch-all that connects the sub-router is not one of the request's parameters.
        auto& params = context.url_params;
        if (!params.keys.empty() && params.keys.back() == "*" && params.values.size() >= params.keys.size()) {
            params.values[params.keys.size() - 1].clear();
        }
        co_return co_await handler(req, res);
    };

    const auto id = shared.next_id++;
    std::optional<std::reference_wrapper<detail::Node>> node;
    for (auto& registration : registrations) {
        registration.handler = mount_handler;
        registration.id = id;
        node = add_route(state, registration);
    }
    if (node && sub) {
        node->get().subroutes = sub;
    }
}

}  // namespace

void Router::mount(std::string_view pattern, Handler handler) { mount_impl(state_, pattern, std::move(handler), nullptr); }

void Router::mount(std::string_view pattern, const Router& router) {
    mount_impl(state_, pattern, Handler{router}, router.state_);
}

void Router::handle(std::string_view pattern, Handler handler) {
    if (const auto space = pattern.find_first_of(" \t"); space != std::string_view::npos) {
        auto rest = detail::sub(pattern, space + 1);
        while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t')) {
            rest = detail::sub(rest, 1);
        }
        method(detail::sub(pattern, 0, space), rest, std::move(handler));
        return;
    }
    (void)add_route(state_, {.methods = every_method(*state_), .pattern = pattern, .handler = std::move(handler)});
}

void Router::subtree(std::string_view pattern, Handler handler) {
    detail::Methods methods = every_method(*state_);
    std::string_view path = pattern;
    if (const auto space = pattern.find_first_of(" \t"); space != std::string_view::npos) {
        const auto name = upper(detail::sub(pattern, 0, space));
        if (!std::ranges::contains(state_->shared->methods, name)) {
            record(*state_, "'" + name + "' http method is not supported");
            return;
        }
        methods = {.names = {name}};
        path = detail::sub(pattern, space + 1);
        while (!path.empty() && (path.front() == ' ' || path.front() == '\t')) {
            path = detail::sub(path, 1);
        }
    }
    if (path.empty() || !path.ends_with('/')) {
        record(*state_, "subtree pattern must end in '/', got '" + std::string{path} + "'");
        return;
    }
    const std::string tree_pattern = std::string{path} + "*";
    if (!add_route(state_, {.methods = methods, .pattern = tree_pattern, .handler = std::move(handler)})) {
        return;
    }
    if (path == "/") {
        return;
    }
    const Registration redirect_route{
        .methods = methods,
        .pattern = detail::sub(path, 0, path.size() - 1),
        .handler = [](Request& req, Response& res) -> asio::awaitable<Result> {
            std::string location = std::string{req.raw_path()} + "/";
            if (!req.raw_query().empty()) {
                location += "?" + std::string{req.raw_query()};
            }
            co_return co_await redirect(req, res, location, Status::temporary_redirect);
        },
        .implicit = true};
    // An existing route on the bare path takes precedence, so the redirect is simply not added.
    const auto shape = detail::pattern_shape(detail::routing_pattern(redirect_route.pattern));
    if (check_claim(*state_->shared, shape, redirect_route)) {
        (void)add_route(state_, redirect_route);
    }
}

void Router::method(std::string_view method, std::string_view pattern, Handler handler) {
    const auto name = upper(method);
    if (!std::ranges::contains(state_->shared->methods, name)) {
        record(*state_, "'" + std::string{method} + "' http method is not supported");
        return;
    }
    (void)add_route(state_, {.methods = {.names = {name}}, .pattern = pattern, .handler = std::move(handler)});
}

void Router::register_method(std::string_view method) {
    if (state_->shared->frozen) {
        record(*state_, "methods cannot be registered after the router is frozen");
        return;
    }
    const auto name = upper(method);
    if (name.empty() || !valid_header_name(name)) {
        record(*state_, "invalid http method '" + std::string{method} + "'");
        return;
    }
    if (!std::ranges::contains(state_->shared->methods, name)) {
        state_->shared->methods.push_back(name);
    }
}

void Router::not_found(Handler handler) {
    auto target = state_;
    if (state_->is_inline) {
        if (auto parent = state_->parent.lock()) {
            handler = chain(state_->middlewares, std::move(handler));
            target = std::move(parent);
        }
    }
    set_not_found(*target, handler);
}

void Router::method_not_allowed(Handler handler) {
    auto target = state_;
    if (state_->is_inline) {
        if (auto parent = state_->parent.lock()) {
            handler = chain(state_->middlewares, std::move(handler));
            target = std::move(parent);
        }
    }
    set_method_not_allowed(*target, handler);
}

std::vector<Route> Router::routes() const {
    std::vector<Route> out{};
    const std::function<void(const detail::Node&)> visit = [&](const detail::Node& node) {
        if (node.endpoints || node.subroutes) {
            std::optional<std::uint64_t> stub_id{};
            std::map<std::string, std::vector<std::pair<std::string, std::reference_wrapper<const detail::Endpoint>>>>
                by_pattern;
            if (node.endpoints) {
                if (const auto it = node.endpoints->find(detail::stub_method); it != node.endpoints->end()) {
                    stub_id = it->second.id;
                }
                for (const auto& [method, endpoint] : *node.endpoints) {
                    if (!endpoint.pattern.empty()) {
                        by_pattern[endpoint.pattern].emplace_back(method, std::cref(endpoint));
                    }
                }
            }
            for (const auto& [pattern, endpoints] : by_pattern) {
                Route route{.pattern = pattern, .handlers = {}, .subroutes = nullptr};
                if (node.subroutes) {
                    route.subroutes = std::make_shared<const Router>(Router{node.subroutes});
                }
                for (const auto& [method, ref] : endpoints) {
                    const detail::Endpoint& endpoint = ref;
                    if (!endpoint.handler) {
                        continue;
                    }
                    const RouteEndpoint value{.handler = endpoint.handler,
                                              .endpoint = endpoint.endpoint,
                                              .middlewares = endpoint.middlewares};
                    if (method == detail::any_method) {
                        if (node.subroutes || endpoint.id != stub_id) {
                            route.handlers.emplace("*", value);
                        }
                    } else if (method != detail::stub_method && endpoint.id != stub_id) {
                        route.handlers.emplace(method, value);
                    }
                }
                if (route.handlers.empty() && !route.subroutes) {
                    continue;
                }
                out.push_back(std::move(route));
            }
        }
        for (const auto& children : node.children) {
            for (const auto& child : children) {
                visit(*child);
            }
        }
    };
    visit(state_->shared->tree.root());
    return out;
}

std::vector<Middleware> Router::middlewares() const { return state_->middlewares; }

std::optional<std::string> Router::find(std::string_view method, std::string_view path, RouteContext& context) const {
    return find_in(*state_, context, method, detail::routing_path(path));
}

std::vector<Error> Router::errors() const {
    std::vector<Error> out{};
    collect_errors(*state_, out);
    return out;
}

void Router::freeze() { freeze_all(*state_); }

asio::awaitable<Result> Router::operator()(Request& request, Response& response) const {
    const auto state = state_;
    if (!state->handler) {
        co_return co_await not_found_of(*state)(request, response);
    }
    request.route().active = true;
    const Handler handler = state->handler;
    co_return co_await handler(request, response);
}

Result walk(const Router& router, const WalkFn& fn) { return walk_routes(router, fn, {}, {}); }

Handler chain(std::span<const Middleware> middlewares, Handler endpoint) {
    for (const auto& middleware : std::views::reverse(middlewares)) {
        endpoint = middleware(std::move(endpoint));
    }
    return endpoint;
}

}  // namespace ki
