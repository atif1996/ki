// T2.3 - T2.13: chi's mux tests, ported. Each case names its chi test.

#include "router_support.hpp"

#include <ki/router.hpp>
#include <ki/test/recorder.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <map>
#include <span>
#include <string>
#include <thread>
#include <typeinfo>
#include <vector>

using ki::Handler;
using ki::Middleware;
using ki::Request;
using ki::Response;
using ki::Result;
using ki::Router;
using ki::Status;
using ki::testing::named;

namespace {

ki::test::Recorded fetch(const Router& router, std::string method, std::string target) {
    auto request = ki::test::make_request(std::move(method), std::move(target));
    return ki::test::Recorder::run(router, request);
}

std::string body(const Router& router, std::string method, std::string target) {
    return fetch(router, std::move(method), std::move(target)).body;
}

Handler text(std::string reply, Status status = Status::ok) {
    return [reply = std::move(reply), status](Request&, Response& res) -> asio::awaitable<Result> {
        (void)res.write_header(status);
        co_return co_await res.write(reply);
    };
}

// with_value returns middleware that stores value under key before calling the next handler.
Middleware with_value(const ki::ContextKey<std::string>& key, std::string value) {
    return [&key, value = std::move(value)](Handler next) -> Handler {
        return [&key, value, next = std::move(next)](Request& req, Response& res) -> asio::awaitable<Result> {
            req.set(key, value);
            co_return co_await next(req, res);
        };
    };
}

Middleware pass_through() {
    return [](Handler next) { return next; };
}

inline constexpr ki::ContextKey<std::string> user_key{"test.user"};
inline constexpr ki::ContextKey<std::string> mw_key{"test.mw"};
inline constexpr ki::ContextKey<std::string> with_key{"test.with"};
inline constexpr ki::ContextKey<std::string> mw2_key{"test.mw2"};

}  // namespace

// chi: TestMuxBasic
TEST_CASE("chi TestMuxBasic") {
    int count = 0;
    std::string log;
    Router m;
    m.use({
        [&count](Handler next) -> Handler {
            return [&count, next = std::move(next)](Request& req, Response& res) -> asio::awaitable<Result> {
                ++count;
                co_return co_await next(req, res);
            };
        },
        with_value(user_key, "peter"),
        [&log](Handler next) -> Handler {
            return [&log, next = std::move(next)](Request& req, Response& res) -> asio::awaitable<Result> {
                log += "logmw test";
                co_return co_await next(req, res);
            };
        },
    });
    m.get("/", [](Request& req, Response& res) -> asio::awaitable<Result> {
        co_return co_await res.write("hi " + req.value_or(user_key, ""));
    });
    m.method("GET", "/ping", text("."));
    m.method("GET", "/pingall", text("ping all"));
    m.method("get", "/ping/all", text("ping all"));
    m.get("/ping/all2", text("ping all2"));
    m.head("/ping", [](Request&, Response& res) -> asio::awaitable<Result> {
        (void)res.set_header("X-Ping", "1");
        co_return Result{};
    });
    m.post("/ping", text("", Status::created));
    // ki differs: chi first registers "/ping/{id}" with another handler and expects this one to replace it;
    // ki rejects duplicates (see "chi duplicate registrations are rejected").
    m.get("/ping/{id}", [](Request& req, Response& res) -> asio::awaitable<Result> {
        co_return co_await res.write("ping one id: " + std::string{req.path_value("id")});
    });
    m.get("/ping/{iidd}/woop", [](Request& req, Response& res) -> asio::awaitable<Result> {
        co_return co_await res.write("woop." + std::string{req.path_value("iidd")});
    });
    m.handle("/admin/*", text("catchall"));
    REQUIRE(m.errors().empty());

    REQUIRE(body(m, "GET", "/") == "hi peter");
    REQUIRE(log == "logmw test");
    REQUIRE(body(m, "GET", "/ping") == ".");
    REQUIRE(body(m, "GET", "/pingall") == "ping all");
    REQUIRE(body(m, "GET", "/ping/all") == "ping all");
    REQUIRE(body(m, "GET", "/ping/all2") == "ping all2");
    REQUIRE(body(m, "GET", "/ping/123") == "ping one id: 123");
    REQUIRE(body(m, "GET", "/ping/allan") == "ping one id: allan");
    REQUIRE(body(m, "GET", "/ping/1/woop") == "woop.1");

    const auto head = fetch(m, "HEAD", "/ping");
    REQUIRE(head.status == Status::ok);
    REQUIRE(head.headers.get("X-Ping") == "1");

    REQUIRE(body(m, "GET", "/admin/catch-thazzzzz") == "catchall");
    const auto post = fetch(m, "POST", "/admin/casdfsadfs");
    REQUIRE(post.status == Status::ok);
    REQUIRE(post.body == "catchall");

    const auto die = fetch(m, "DIE", "/ping/1/woop");
    REQUIRE(die.status == Status::method_not_allowed);
    REQUIRE(die.body.empty());
    REQUIRE(count == 12);  // the router middleware runs for every request, the 405 included
}

// chi: TestMuxMounts
TEST_CASE("chi TestMuxMounts") {
    Router r;
    r.get("/{hash}", [](Request& req, Response& res) -> asio::awaitable<Result> {
        co_return co_await res.write("/" + std::string{req.path_value("hash")});
    });
    r.route("/{hash}/share", [](Router& sub) {
        sub.get("/", [](Request& req, Response& res) -> asio::awaitable<Result> {
            co_return co_await res.write("/" + std::string{req.path_value("hash")} + "/share");
        });
        sub.get("/{network}", [](Request& req, Response& res) -> asio::awaitable<Result> {
            co_return co_await res.write("/" + std::string{req.path_value("hash")} + "/share/" +
                                         std::string{req.path_value("network")});
        });
    });
    Router m;
    m.mount("/sharing", r);
    REQUIRE(m.errors().empty());

    REQUIRE(body(m, "GET", "/sharing/aBc") == "/aBc");
    REQUIRE(body(m, "GET", "/sharing/aBc/share") == "/aBc/share");
    REQUIRE(body(m, "GET", "/sharing/aBc/share/twitter") == "/aBc/share/twitter");
}

// chi: TestMuxPlain
TEST_CASE("chi TestMuxPlain") {
    Router r;
    r.get("/hi", text("bye"));
    r.not_found(text("nothing here", Status::not_found));
    REQUIRE(body(r, "GET", "/hi") == "bye");
    REQUIRE(body(r, "GET", "/nothing-here") == "nothing here");
}

// chi: TestMuxEmptyRoutes
TEST_CASE("chi TestMuxEmptyRoutes") {
    Router mux;
    Router api;
    mux.handle("/api*", api);
    REQUIRE(body(mux, "GET", "/") == "404 page not found\n");
    REQUIRE(body(api, "GET", "/") == "404 page not found\n");
}

// chi: TestMuxTrailingSlash
TEST_CASE("chi TestMuxTrailingSlash") {
    Router r;
    r.not_found(text("nothing here", Status::not_found));
    const Handler index = [](Request& req, Response& res) -> asio::awaitable<Result> {
        co_return co_await res.write(req.path_value("accountID"));
    };
    Router sub;
    sub.get("/", index);
    r.mount("/accounts/{accountID}", sub);
    r.get("/accounts/{accountID}/", index);
    REQUIRE(r.errors().empty());

    REQUIRE(body(r, "GET", "/accounts/admin") == "admin");
    REQUIRE(body(r, "GET", "/accounts/admin/") == "admin");
    REQUIRE(body(r, "GET", "/nothing-here") == "nothing here");
}

// chi: TestMuxNestedNotFound
TEST_CASE("chi TestMuxNestedNotFound") {
    Router r;
    r.use({with_value(mw_key, "mw")});
    r.get("/hi", text("bye"));
    r.with({with_value(with_key, "with")}).not_found([](Request& req, Response& res) -> asio::awaitable<Result> {
        (void)res.write_header(Status::not_found);
        co_return co_await res.write("root 404 " + req.value_or(mw_key, "") + " " + req.value_or(with_key, ""));
    });

    Router sr1;
    sr1.get("/sub", text("sub"));
    sr1.group([](Router& group) {
        group.use({with_value(mw2_key, "mw2")});
        group.not_found([](Request& req, Response& res) -> asio::awaitable<Result> {
            (void)res.write_header(Status::not_found);
            co_return co_await res.write("sub 404 " + req.value_or(mw2_key, ""));
        });
    });
    Router sr2;
    sr2.get("/sub", text("sub2"));
    r.mount("/admin1", sr1);
    r.mount("/admin2", sr2);
    REQUIRE(r.errors().empty());

    REQUIRE(body(r, "GET", "/hi") == "bye");
    REQUIRE(body(r, "GET", "/nothing-here") == "root 404 mw with");
    REQUIRE(body(r, "GET", "/admin1/sub") == "sub");
    REQUIRE(body(r, "GET", "/admin1/nope") == "sub 404 mw2");
    REQUIRE(body(r, "GET", "/admin2/sub") == "sub2");
    REQUIRE(body(r, "GET", "/admin2/nope") == "root 404 mw with");
}

// chi: TestMethodNotAllowed
TEST_CASE("chi TestMethodNotAllowed") {
    Router r;
    r.get("/hi", text("hi, get"));
    r.head("/hi", text("hi, head"));
    r.get("/*", text("catch-all"));

    const auto registered = fetch(r, "GET", "/hi");
    REQUIRE(registered.status == Status::ok);
    REQUIRE_FALSE(registered.headers.contains("Allow"));

    const auto unregistered = fetch(r, "POST", "/hi");
    REQUIRE(unregistered.status == Status::method_not_allowed);
    auto allowed = unregistered.headers.get_all("Allow");
    std::ranges::sort(allowed);
    REQUIRE(allowed == std::vector<std::string_view>{"GET", "HEAD"});
}

// chi: TestMuxNestedMethodNotAllowed
TEST_CASE("chi TestMuxNestedMethodNotAllowed") {
    Router r;
    r.get("/root", text("root"));
    r.method_not_allowed(text("root 405", Status::method_not_allowed));
    Router sr1;
    sr1.get("/sub1", text("sub1"));
    sr1.method_not_allowed(text("sub1 405", Status::method_not_allowed));
    Router sr2;
    sr2.get("/sub2", text("sub2"));
    Router path_var;
    path_var.get("/{var}", text("pv"));
    path_var.method_not_allowed(text("pv 405", Status::method_not_allowed));
    r.mount("/prefix1", sr1);
    r.mount("/prefix2", sr2);
    r.mount("/pathVar", path_var);
    REQUIRE(r.errors().empty());

    REQUIRE(body(r, "GET", "/root") == "root");
    REQUIRE(body(r, "PUT", "/root") == "root 405");
    REQUIRE(body(r, "GET", "/prefix1/sub1") == "sub1");
    REQUIRE(body(r, "PUT", "/prefix1/sub1") == "sub1 405");
    REQUIRE(body(r, "GET", "/prefix2/sub2") == "sub2");
    REQUIRE(body(r, "PUT", "/prefix2/sub2") == "root 405");
    REQUIRE(body(r, "GET", "/pathVar/myvar") == "pv");
    REQUIRE(body(r, "DELETE", "/pathVar/myvar") == "pv 405");
}

// chi: TestMuxComplicatedNotFound
TEST_CASE("chi TestMuxComplicatedNotFound") {
    const auto decorate = [](Router& r) {
        r.get("/auth", text("auth get"));
        r.route("/public", [](Router& sub) { sub.get("/", text("public get")); });
        Router sub0;
        sub0.route("/resource", [](Router& sub) { sub.get("/", text("private get")); });
        r.mount("/private", sub0);
        Router sub1;
        sub1.route("/resource", [](Router& sub) { sub.get("/", text("private get")); });
        r.with({pass_through()}).mount("/private_mw", sub1);
    };
    const auto check = [](const Router& r) {
        REQUIRE(r.errors().empty());
        REQUIRE(body(r, "GET", "/auth") == "auth get");
        REQUIRE(body(r, "GET", "/public") == "public get");
        REQUIRE(body(r, "GET", "/public/") == "public get");
        REQUIRE(body(r, "GET", "/private/resource") == "private get");
        for (const auto* path : {"/nope", "/public/nope", "/private/nope", "/private/resource/nope", "/private_mw/nope",
                                 "/private_mw/resource/nope", "/auth/"}) {
            INFO(path);
            REQUIRE(body(r, "GET", path) == "custom not-found");
        }
    };

    SECTION("pre") {
        Router r;
        r.not_found(text("custom not-found"));
        decorate(r);
        check(r);
    }
    SECTION("post") {
        Router r;
        decorate(r);
        r.not_found(text("custom not-found"));
        check(r);
    }
}

namespace {

inline constexpr ki::ContextKey<std::string> inline1_key{"test.inline1"};
inline constexpr ki::ContextKey<std::string> inline2_key{"test.inline2"};
inline constexpr ki::ContextKey<std::string> request_id_key{"test.requestID"};
inline constexpr ki::ContextKey<std::string> session_key{"test.session.user"};
inline constexpr ki::ContextKey<bool> hook_key{"test.hook"};

// counting returns middleware that counts how often it is built and how often it runs.
Middleware counting(int& inits, int& runs) {
    return [&inits, &runs](Handler next) -> Handler {
        ++inits;
        return [&runs, next = std::move(next)](Request& req, Response& res) -> asio::awaitable<Result> {
            ++runs;
            co_return co_await next(req, res);
        };
    };
}

// reply writes format with {param} and {ctx} placeholders filled from the request.
Handler describe(std::string prefix, std::vector<std::string> params) {
    return [prefix = std::move(prefix), params = std::move(params)](Request& req,
                                                                    Response& res) -> asio::awaitable<Result> {
        std::string out = prefix;
        for (const auto& param : params) {
            out += '/';
            out += req.path_value(param);
        }
        co_return co_await res.write(out);
    };
}

Handler with_session(std::string text) {
    return [text = std::move(text)](Request& req, Response& res) -> asio::awaitable<Result> {
        co_return co_await res.write(text + " reqid:" + req.value_or(request_id_key, "") +
                                     " session:" + req.value_or(session_key, ""));
    };
}

Handler hub_reply(std::string suffix, std::string param = "hubID") {
    return
        [suffix = std::move(suffix), param = std::move(param)](Request& req, Response& res) -> asio::awaitable<Result> {
            std::string out = "/hubs/" + std::string{req.path_value(param)} + suffix;
            if (suffix.ends_with("/view/")) {
                out += req.path_value("*");
            }
            co_return co_await res.write(out + " reqid:" + req.value_or(request_id_key, "") +
                                         " session:" + req.value_or(session_key, ""));
        };
}

Router big_mux() {
    Router r;
    r.use({with_value(request_id_key, "1"), pass_through()});
    r.group([](Router& g) {
        g.use({with_value(session_key, "anonymous")});
        g.get("/favicon.ico", text("fav"));
        g.get("/hubs/{hubID}/view", hub_reply("/view"));
        g.get("/hubs/{hubID}/view/*", hub_reply("/view/"));
        g.post("/hubs/{hubSlug}/view/*", hub_reply("/view/", "hubSlug"));
    });
    r.group([](Router& g) {
        g.use({with_value(session_key, "elvis")});
        g.get("/", with_session("/"));
        g.get("/suggestions", with_session("/suggestions"));
        g.get("/woot/{wootID}/*", describe("/woot", {"wootID", "*"}));
        g.route("/hubs", [](Router& hubs) {
            hubs.route("/{hubID}", [](Router& hub) {
                hub.get("/", hub_reply(""));
                hub.get("/touch", hub_reply("/touch"));
                Router sr3;
                sr3.get("/", hub_reply("/webhooks"));
                sr3.route("/{webhookID}", [](Router& hook) {
                    hook.get("/", [](Request& req, Response& res) -> asio::awaitable<Result> {
                        co_return co_await res.write("/hubs/" + std::string{req.path_value("hubID")} + "/webhooks/" +
                                                     std::string{req.path_value("webhookID")});
                    });
                });
                const std::vector<Middleware> hook_mw = {[](Handler next) -> Handler {
                    return [next = std::move(next)](Request& req, Response& res) -> asio::awaitable<Result> {
                        req.set(hook_key, true);
                        co_return co_await next(req, res);
                    };
                }};
                hub.mount("/webhooks", ki::chain(hook_mw, sr3));
                hub.route("/posts", [](Router& posts) { posts.get("/", hub_reply("/posts")); });
            });
        });
        g.route("/folders/", [](Router& folders) {
            folders.get("/", with_session("/folders/"));
            folders.get("/public", with_session("/folders/public"));
        });
    });
    return r;
}

}  // namespace

// chi: TestMuxWith
TEST_CASE("chi TestMuxWith") {
    int init1 = 0;
    int run1 = 0;
    int init2 = 0;
    int run2 = 0;
    Router r;
    r.get("/hi", text("bye"));
    r.with({counting(init1, run1), with_value(inline1_key, "yes")})
        .with({counting(init2, run2), with_value(inline2_key, "yes")})
        .get("/inline", [](Request& req, Response& res) -> asio::awaitable<Result> {
            co_return co_await res.write("inline " + req.value_or(inline1_key, "") + " " +
                                         req.value_or(inline2_key, ""));
        });
    REQUIRE(body(r, "GET", "/hi") == "bye");
    REQUIRE(body(r, "GET", "/inline") == "inline yes yes");
    REQUIRE(init1 == 1);
    REQUIRE(run1 == 1);
    REQUIRE(init2 == 1);
    REQUIRE(run2 == 1);
}

// chi: TestMuxHandlePatternValidation (chi panics on the invalid patterns; ki records an error)
TEST_CASE("chi TestMuxHandlePatternValidation") {
    struct Valid {
        const char* pattern;
        const char* method;
        const char* path;
        const char* reply;
    };
    for (const auto& c : {Valid{"/user/{id}", "GET", "/user/123", "without-prefix GET"},
                          Valid{"POST /products/{id}", "POST", "/products/456", "with-prefix POST"},
                          Valid{"PATCH \t /", "PATCH", "/", "extended-whitespace PATCH"}}) {
        INFO(c.pattern);
        Router r;
        r.handle(c.pattern, text(c.reply));
        REQUIRE(r.errors().empty());
        const auto out = fetch(r, c.method, c.path);
        REQUIRE(out.status == Status::ok);
        REQUIRE(out.body == c.reply);
    }
    for (const auto* pattern : {"INVALID/user/{id}", "GET/user/{id}", "UNSUPPORTED /unsupported-method"}) {
        INFO(pattern);
        Router r;
        r.handle(pattern, text("never"));
        REQUIRE(r.errors().size() == 1);
        REQUIRE(r.routes().empty());
    }
}

// chi: TestRouterFromMuxWith
TEST_CASE("chi TestRouterFromMuxWith") {
    Router r;
    auto with = r.with({pass_through()});
    with.get("/with_middleware", text("ok"));
    REQUIRE(body(with, "GET", "/with_middleware") == "ok");
}

// chi: TestMuxMiddlewareStack
TEST_CASE("chi TestMuxMiddlewareStack") {
    int std_init = 0;
    int std_run = 0;
    int ctx_init = 0;
    int ctx_run = 0;
    int inline_init = 0;
    int inline_run = 0;
    static constexpr ki::ContextKey<int> count_key{"test.count.ctxmwHandler"};

    Router r;
    r.use({counting(std_init, std_run),
           [&ctx_init, &ctx_run](Handler next) -> Handler {
               ++ctx_init;
               return [&ctx_run, next = std::move(next)](Request& req, Response& res) -> asio::awaitable<Result> {
                   ++ctx_run;
                   req.set(count_key, ctx_run);
                   co_return co_await next(req, res);
               };
           },
           [](Handler next) -> Handler {
               return [next = std::move(next)](Request& req, Response& res) -> asio::awaitable<Result> {
                   if (req.path() == "/ping") {
                       co_return co_await res.write("pong");
                   }
                   co_return co_await next(req, res);
               };
           }});
    int handled = 0;
    r.with({counting(inline_init, inline_run)}).get("/", [&](Request& req, Response& res) -> asio::awaitable<Result> {
        ++handled;
        co_return co_await res.write("inits:" + std::to_string(ctx_init) + " reqs:" + std::to_string(handled) +
                                     " ctxValue:" + std::to_string(req.value_or(count_key, 0)));
    });
    r.get("/hi", text("wooot"));

    (void)body(r, "GET", "/");
    (void)body(r, "GET", "/");
    REQUIRE(body(r, "GET", "/") == "inits:1 reqs:3 ctxValue:3");
    REQUIRE(body(r, "GET", "/ping") == "pong");
}

// chi: TestMuxRouteGroups
TEST_CASE("chi TestMuxRouteGroups") {
    int init1 = 0;
    int run1 = 0;
    int init2 = 0;
    int run2 = 0;
    Router r;
    r.group([&](Router& g) {
        g.use({counting(init1, run1)});
        g.get("/group", text("root group"));
    });
    r.group([&](Router& g) {
        g.use({counting(init2, run2)});
        g.get("/group2", text("root group2"));
    });
    REQUIRE(body(r, "GET", "/group") == "root group");
    REQUIRE(init1 == 1);
    REQUIRE(run1 == 1);
    REQUIRE(body(r, "GET", "/group2") == "root group2");
    REQUIRE(init2 == 1);
    REQUIRE(run2 == 1);
}

// chi: TestMuxBig
TEST_CASE("chi TestMuxBig") {
    const auto r = big_mux();
    REQUIRE(r.errors().empty());
    REQUIRE(body(r, "GET", "/favicon.ico") == "fav");
    REQUIRE(body(r, "GET", "/hubs/4/view") == "/hubs/4/view reqid:1 session:anonymous");
    REQUIRE(body(r, "GET", "/hubs/4/view/index.html") == "/hubs/4/view/index.html reqid:1 session:anonymous");
    REQUIRE(body(r, "POST", "/hubs/ethereumhub/view/index.html") ==
            "/hubs/ethereumhub/view/index.html reqid:1 session:anonymous");
    REQUIRE(body(r, "GET", "/") == "/ reqid:1 session:elvis");
    REQUIRE(body(r, "GET", "/suggestions") == "/suggestions reqid:1 session:elvis");
    REQUIRE(body(r, "GET", "/woot/444/hiiii") == "/woot/444/hiiii");
    REQUIRE(body(r, "GET", "/hubs/123") == "/hubs/123 reqid:1 session:elvis");
    REQUIRE(body(r, "GET", "/hubs/123/touch") == "/hubs/123/touch reqid:1 session:elvis");
    REQUIRE(body(r, "GET", "/hubs/123/webhooks") == "/hubs/123/webhooks reqid:1 session:elvis");
    REQUIRE(body(r, "GET", "/hubs/123/posts") == "/hubs/123/posts reqid:1 session:elvis");
    REQUIRE(body(r, "GET", "/folders") == "404 page not found\n");
    REQUIRE(body(r, "GET", "/folders/") == "/folders/ reqid:1 session:elvis");
    REQUIRE(body(r, "GET", "/folders/public") == "/folders/public reqid:1 session:elvis");
    REQUIRE(body(r, "GET", "/folders/nothing") == "404 page not found\n");
}

namespace {

inline constexpr ki::ContextKey<std::string> test_ctx_key{"test.testCtx"};
inline constexpr ki::ContextKey<int> counter_key{"test.counter"};

Middleware increase_counter() {
    return [](Handler next) -> Handler {
        return [next = std::move(next)](Request& req, Response& res) -> asio::awaitable<Result> {
            req.set(counter_key, req.value_or(counter_key, 0) + 1);
            co_return co_await next(req, res);
        };
    };
}

Handler print_counter() {
    return [](Request& req, Response& res) -> asio::awaitable<Result> {
        co_return co_await res.write(std::to_string(req.value_or(counter_key, 0)));
    };
}

Handler param_reply(std::string prefix, std::string param) {
    return
        [prefix = std::move(prefix), param = std::move(param)](Request& req, Response& res) -> asio::awaitable<Result> {
            co_return co_await res.write(prefix + std::string{req.path_value(param)});
        };
}

}  // namespace

// chi: TestMuxSubroutesBasic
TEST_CASE("chi TestMuxSubroutesBasic") {
    Router r;
    r.get("/", text("index"));
    r.route("/articles", [](Router& articles) {
        articles.get("/", text("articles-list"));
        articles.get("/search", text("search-articles"));
        articles.route("/{id}", [](Router& article) {
            article.get("/", param_reply("get-article:", "id"));
            article.get("/sync", param_reply("sync-article:", "id"));
        });
    });
    REQUIRE(r.errors().empty());
    REQUIRE(body(r, "GET", "/") == "index");
    REQUIRE(body(r, "GET", "/articles") == "articles-list");
    REQUIRE(body(r, "GET", "/articles/search") == "search-articles");
    REQUIRE(body(r, "GET", "/articles/123") == "get-article:123");
    REQUIRE(body(r, "GET", "/articles/123/sync") == "sync-article:123");
}

// chi: TestMuxSubroutes
TEST_CASE("chi TestMuxSubroutes") {
    Router r;
    r.get("/hubs/{hubID}/view", text("hub1"));
    r.get("/hubs/{hubID}/view/*", text("hub2"));
    Router sr;
    sr.get("/", text("hub3"));
    r.mount("/hubs/{hubID}/users", sr);
    r.get("/hubs/{hubID}/users/", text("hub3 override"));
    Router sr3;
    sr3.get("/", text("account1"));
    sr3.get("/hi", text("account2"));
    r.route("/accounts/{accountID}", [&sr3](Router& sub) { sub.mount("/", sr3); });
    REQUIRE(r.errors().empty());

    REQUIRE(body(r, "GET", "/hubs/123/view") == "hub1");
    REQUIRE(body(r, "GET", "/hubs/123/view/index.html") == "hub2");
    REQUIRE(body(r, "GET", "/hubs/123/users") == "hub3");
    REQUIRE(body(r, "GET", "/hubs/123/users/") == "hub3 override");
    REQUIRE(body(r, "GET", "/accounts/44") == "account1");
    REQUIRE(body(r, "GET", "/accounts/44/hi") == "account2");

    // The routing patterns are recorded as the request passes through each router.
    auto request = ki::test::make_request("GET", "/accounts/44/hi");
    const auto out = ki::test::Recorder::run(r, request);
    REQUIRE(out.body == "account2");
    REQUIRE(request.route().route_patterns == std::vector<std::string>{"/accounts/{accountID}/*", "/*", "/hi"});
    REQUIRE(request.route_pattern() == "/accounts/{accountID}/hi");
}

// chi: TestSingleHandler
TEST_CASE("chi TestSingleHandler") {
    const Handler h = [](Request& req, Response& res) -> asio::awaitable<Result> {
        co_return co_await res.write("hi " + std::string{req.path_value("name")});
    };
    auto request = ki::test::make_request("GET", "/");
    request.route().url_params.add("name", "joe");
    REQUIRE(ki::test::Recorder::run(h, request).body == "hi joe");
}

// chi: TestServeHTTPExistingContext
TEST_CASE("chi TestServeHTTPExistingContext") {
    Router r;
    r.get("/hi", [](Request& req, Response& res) -> asio::awaitable<Result> {
        co_return co_await res.write(req.value_or(test_ctx_key, ""));
    });
    r.not_found([](Request& req, Response& res) -> asio::awaitable<Result> {
        (void)res.write_header(Status::not_found);
        co_return co_await res.write(req.value_or(test_ctx_key, ""));
    });

    auto hi = ki::test::make_request("GET", "/hi");
    hi.set(test_ctx_key, "hi ctx");
    const auto found = ki::test::Recorder::run(r, hi);
    REQUIRE(found.status == Status::ok);
    REQUIRE(found.body == "hi ctx");

    auto hello = ki::test::make_request("GET", "/hello");
    hello.set(test_ctx_key, "nothing here ctx");
    const auto missing = ki::test::Recorder::run(r, hello);
    REQUIRE(missing.status == Status::not_found);
    REQUIRE(missing.body == "nothing here ctx");
}

// chi: TestNestedGroups
TEST_CASE("chi TestNestedGroups") {
    Router r;
    r.get("/0", print_counter());
    r.group([](Router& g) {
        g.use({increase_counter()});
        g.get("/1", print_counter());
        g.with({increase_counter()}).get("/2", print_counter());
        g.group([](Router& inner) {
            inner.use({increase_counter(), increase_counter()});
            inner.get("/3", print_counter());
        });
        g.route("/", [](Router& sub) {
            sub.use({increase_counter(), increase_counter()});
            sub.with({increase_counter()}).get("/4", print_counter());
            sub.group([](Router& inner) {
                inner.use({increase_counter(), increase_counter()});
                inner.get("/5", print_counter());
                inner.with({increase_counter()}).get("/6", print_counter());
            });
        });
    });
    REQUIRE(r.errors().empty());
    for (const auto* route : {"0", "1", "2", "3", "4", "5", "6"}) {
        INFO(route);
        REQUIRE(body(r, "GET", std::string{"/"} + route) == route);
    }
}

// chi: TestMiddlewarePanicOnLateUse (chi panics; ki records an error and ignores the middleware)
TEST_CASE("chi TestMiddlewarePanicOnLateUse") {
    bool ran = false;
    Router r;
    r.get("/", text("hello\n"));
    r.use({[&ran](Handler next) -> Handler {
        ran = true;
        return next;
    }});
    REQUIRE(r.errors().size() == 1);
    REQUIRE(body(r, "GET", "/") == "hello\n");
    REQUIRE_FALSE(ran);
}

// chi: TestMountingExistingPath (chi panics; ki records an error and keeps the first mount)
TEST_CASE("chi TestMountingExistingPath") {
    Router r;
    r.get("/", text("root"));
    r.mount("/hi", text("first"));
    r.mount("/hi", text("second"));
    REQUIRE(r.errors().size() == 1);
    REQUIRE(body(r, "GET", "/hi") == "first");
}

// chi: TestMountingSimilarPattern
TEST_CASE("chi TestMountingSimilarPattern") {
    Router r;
    r.get("/hi", text("bye"));
    Router r2;
    r2.get("/", text("foobar"));
    Router r3;
    r3.get("/", text("foo"));
    r.mount("/foobar", r2);
    r.mount("/foo", r3);
    REQUIRE(r.errors().empty());
    REQUIRE(body(r, "GET", "/hi") == "bye");
    REQUIRE(body(r, "GET", "/foobar") == "foobar");
    REQUIRE(body(r, "GET", "/foo") == "foo");
}

// chi: TestMountingSelf (chi panics; ki records an error instead of creating a routing loop)
TEST_CASE("chi TestMountingSelf") {
    SECTION("direct self-mount") {
        Router r;
        r.get("/ping", text("pong"));
        r.mount("/", r);
        REQUIRE(r.errors().size() == 1);
        REQUIRE(body(r, "GET", "/nope") == "404 page not found\n");
    }
    SECTION("mount of an inline router sharing the same tree") {
        auto r = Router{}.with({pass_through()});
        r.mount("/", r.group([](Router& g) { g.get("/ping", text("pong")); }));
        REQUIRE(r.errors().size() == 1);
    }
}

// chi: TestMuxEmptyParams
TEST_CASE("chi TestMuxEmptyParams") {
    Router r;
    r.get("/users/{x}/{y}/{z}", [](Request& req, Response& res) -> asio::awaitable<Result> {
        co_return co_await res.write(std::string{req.path_value("x")} + "-" + std::string{req.path_value("y")} + "-" +
                                     std::string{req.path_value("z")});
    });
    REQUIRE(body(r, "GET", "/users/a/b/c") == "a-b-c");
    REQUIRE(body(r, "GET", "/users///c") == "--c");
}

// chi: TestMuxMissingParams
TEST_CASE("chi TestMuxMissingParams") {
    Router r;
    r.get(R"(/user/{userId:\d+})", [](Request& req, Response& res) -> asio::awaitable<Result> {
        co_return co_await res.write("userId = '" + std::string{req.path_value("userId")} + "'");
    });
    r.not_found(text("nothing here", Status::not_found));
    REQUIRE(r.errors().empty());
    REQUIRE(body(r, "GET", "/user/123") == "userId = '123'");
    REQUIRE(body(r, "GET", "/user/") == "nothing here");
}

// chi: TestMuxWildcardRoute, TestMuxWildcardRouteCheckTwo (chi panics; ki records an error)
TEST_CASE("chi TestMuxWildcardRoute") {
    for (const auto* pattern : {"/*/wildcard/must/be/at/end", "/*/wildcard/{must}/be/at/end"}) {
        INFO(pattern);
        Router r;
        r.get(pattern, text("never"));
        REQUIRE(r.errors().size() == 1);
    }
}

// chi: TestMuxRegexp
TEST_CASE("chi TestMuxRegexp") {
    Router r;
    r.route("/{param:[0-9]*}/test", [](Router& sub) { sub.get("/", param_reply("Hi: ", "param")); });
    REQUIRE(body(r, "GET", "//test") == "Hi: ");
}

// chi: TestMuxRegexp2
TEST_CASE("chi TestMuxRegexp2") {
    Router r;
    r.get("/foo-{suffix:[a-z]{2,3}}.json", param_reply("", "suffix"));
    // chi's backtracking matches an empty suffix here; chi's test checks only the empty body.
    REQUIRE(body(r, "GET", "/foo-.json").empty());
    REQUIRE(body(r, "GET", "/foo-abc.json") == "abc");
}

// chi: TestMuxRegexp3
TEST_CASE("chi TestMuxRegexp3") {
    Router r;
    r.get("/one/{firstId:[a-z0-9-]+}/{secondId:[a-z]+}/first", text("first"));
    r.get("/one/{firstId:[a-z0-9-_]+}/{secondId:[0-9]+}/second", text("second"));
    r.del("/one/{firstId:[a-z0-9-_]+}/{secondId:[0-9]+}/second", text("third"));
    r.route("/one", [](Router& sub) {
        sub.get("/{dns:[a-z-0-9_]+}", text("_"));
        sub.get("/{dns:[a-z-0-9_]+}/info", text("_"));
        sub.del("/{id:[0-9]+}", text("forth"));
    });
    REQUIRE(r.errors().empty());
    REQUIRE(body(r, "GET", "/one/hello/peter/first") == "first");
    REQUIRE(body(r, "GET", "/one/hithere/123/second") == "second");
    REQUIRE(body(r, "DELETE", "/one/hithere/123/second") == "third");
    REQUIRE(body(r, "DELETE", "/one/123") == "forth");
}

// chi: TestMuxSubrouterWildcardParam
TEST_CASE("chi TestMuxSubrouterWildcardParam") {
    const Handler h = [](Request& req, Response& res) -> asio::awaitable<Result> {
        co_return co_await res.write("param:" + std::string{req.path_value("param")} +
                                     " *:" + std::string{req.path_value("*")});
    };
    Router r;
    r.get("/bare/{param}", h);
    r.get("/bare/{param}/*", h);
    r.route("/case0", [&h](Router& sub) {
        sub.get("/{param}", h);
        sub.get("/{param}/*", h);
    });
    REQUIRE(body(r, "GET", "/bare/hi") == "param:hi *:");
    REQUIRE(body(r, "GET", "/bare/hi/yes") == "param:hi *:yes");
    REQUIRE(body(r, "GET", "/case0/hi") == "param:hi *:");
    REQUIRE(body(r, "GET", "/case0/hi/yes") == "param:hi *:yes");
}

// chi: TestMuxContextIsThreadSafe (scaled down; run under TSan in Phase 6)
TEST_CASE("chi TestMuxContextIsThreadSafe") {
    Router router;
    router.get("/{id}", param_reply("", "id"));
    router.freeze();
    std::atomic<int> wrong{0};
    {
        std::vector<std::jthread> workers;
        for (int t = 0; t < 8; ++t) {
            workers.emplace_back([&router, &wrong, t] {
                for (int i = 0; i < 250; ++i) {
                    const auto id = std::to_string(t * 1000 + i);
                    if (body(router, "GET", "/" + id) != id) {
                        ++wrong;
                    }
                }
            });
        }
    }
    REQUIRE(wrong == 0);
}

// chi: TestEscapedURLParams
TEST_CASE("chi TestEscapedURLParams") {
    Router m;
    m.get("/api/{identifier}/{region}/{size}/{rotation}/*", [](Request& req, Response& res) -> asio::awaitable<Result> {
        // ki differs: chi passes the parameter still encoded; ki decodes each segment, as Go's ServeMux does,
        // and %2F never splits a segment.
        const bool ok = req.path_value("identifier") == "http://example.com/image.png" &&
                        req.path_value("region") == "full" && req.path_value("size") == "max" &&
                        req.path_value("rotation") == "0";
        co_return co_await res.write(ok ? "success" : "wrong params");
    });
    REQUIRE(body(m, "GET", "/api/http:%2f%2fexample.com%2fimage.png/full/max/0/color.png") == "success");
}

// chi: TestCustomHTTPMethod (methods are registered per router rather than globally)
TEST_CASE("chi TestCustomHTTPMethod") {
    Router r;
    r.register_method("BOO");
    r.get("/", text("."));
    r.method("BOO", "/hi", text("custom method"));
    REQUIRE(r.errors().empty());
    REQUIRE(body(r, "GET", "/") == ".");
    REQUIRE(body(r, "BOO", "/hi") == "custom method");

    std::map<std::string, std::string> expected{{"GET", "/"}, {"BOO", "/hi"}};
    REQUIRE(ki::walk(
        r, [&](std::string_view method, std::string_view route, const Handler&, std::span<const Middleware>) -> Result {
            const auto it = expected.find(std::string{method});
            REQUIRE(it != expected.end());
            REQUIRE(it->second == route);
            expected.erase(it);
            return {};
        }));
    REQUIRE(expected.empty());

    Router other;
    other.method("BOO", "/hi", text("never"));
    REQUIRE(other.errors().size() == 1);
}

// chi: TestQueryHTTPMethod
TEST_CASE("chi TestQueryHTTPMethod") {
    Router r;
    r.get("/search", text("get"));
    r.query("/search", [](Request& req, Response& res) -> asio::awaitable<Result> {
        auto data = co_await req.read_all(1024);
        co_return co_await res.write("query: " + data.value_or(""));
    });
    r.method("QUERY", "/reports", text("reports"));
    r.handle("/any", text("any"));

    auto query = ki::test::make_request("QUERY", "/search", {}, "select 1");
    REQUIRE(ki::test::Recorder::run(r, query).body == "query: select 1");
    REQUIRE(body(r, "GET", "/search") == "get");
    REQUIRE(body(r, "QUERY", "/reports") == "reports");
    REQUIRE(body(r, "QUERY", "/any") == "any");

    const auto post = fetch(r, "POST", "/search");
    REQUIRE(post.status == Status::method_not_allowed);
    auto allowed = post.headers.get_all("Allow");
    std::ranges::sort(allowed);
    REQUIRE(allowed == std::vector<std::string_view>{"GET", "QUERY"});
}

// chi: TestMuxMatch, TestMuxMatch_HasBasePath, TestMuxMatch_DoesNotHaveBasePath
TEST_CASE("chi TestMuxMatch") {
    Router r;
    r.get("/hi", text("bye"));
    r.route("/articles", [](Router& sub) { sub.get("/{id}", text("article")); });
    r.route("/users", [](Router& sub) {
        sub.head("/{id}", text("user"));
        sub.get("/{id}", text("user"));
    });
    REQUIRE(r.match("GET", "/users/1"));
    REQUIRE_FALSE(r.match("HEAD", "/articles/10"));

    Router base;
    base.get("/", text(""));
    REQUIRE(base.match("GET", "/"));
    REQUIRE_FALSE(Router{}.match("GET", "/"));
}

// chi: TestMuxFind
TEST_CASE("chi TestMuxFind") {
    Router r;
    r.get("/", text(""));
    r.get("/hi", text("bye"));
    r.route("/yo", [](Router& sub) { sub.get("/sup", text("sup")); });
    r.route("/articles", [](Router& sub) { sub.get("/{id}", text("article")); });
    r.route("/users", [](Router& sub) {
        sub.head("/{id}", text("user"));
        sub.get("/{id}", text("user"));
    });
    r.route("/api", [](Router& api) {
        api.route("/groups", [](Router& groups) {
            groups.route("/v2", [](Router& v2) {
                v2.get("/", text("groups"));
                v2.post("/{id}", text("POST groups"));
            });
        });
    });
    REQUIRE(r.find("GET", "") != "/");
    REQUIRE_FALSE(r.find("GET", "/nope"));
    REQUIRE(r.find("GET", "/users/1") == "/users/{id}");
    REQUIRE_FALSE(r.find("HEAD", "/articles/10"));
    REQUIRE(r.find("GET", "/yo/sup") == "/yo/sup");
    REQUIRE(r.find("GET", "/api/groups/v2/") == "/api/groups/v2/");
    REQUIRE(r.find("POST", "/api/groups/v2/1") == "/api/groups/v2/{id}");

    ki::RouteContext context;
    REQUIRE(r.find("GET", "/users/7", context) == "/users/{id}");
    REQUIRE(context.url_param("id") == "7");
}

// chi: TestWalker
TEST_CASE("chi TestWalker") {
    std::vector<std::string> seen;
    REQUIRE(ki::walk(
        big_mux(),
        [&](std::string_view method, std::string_view route, const Handler&, std::span<const Middleware>) -> Result {
            seen.push_back(std::string{method} + " " + std::string{route});
            return {};
        }));
    std::ranges::sort(seen);
    for (const auto& route : seen) {
        UNSCOPED_INFO(route);
    }
    // /webhooks is mounted as a chained handler rather than a Router, so walk cannot descend into it.
    REQUIRE(std::ranges::contains(seen, "GET /hubs/{hubID}/webhooks/*"));
    REQUIRE(std::ranges::contains(seen, "POST /hubs/{hubSlug}/view/*"));
    REQUIRE(std::ranges::contains(seen, "GET /folders/public"));
}

// chi: TestWalkInlineMiddlewaresAcrossSubrouter
TEST_CASE("chi TestWalkInlineMiddlewaresAcrossSubrouter") {
    const auto count_middlewares = [](const Router& r) {
        std::size_t count = 0;
        REQUIRE(ki::walk(
            r,
            [&](std::string_view, std::string_view, const Handler&, std::span<const Middleware> middlewares) -> Result {
                count = middlewares.size();
                return {};
            }));
        return count;
    };
    SECTION("with + group wrapping route + with") {
        Router r;
        r.with({pass_through()}).group([](Router& g) {
            g.route("/foo", [](Router& foo) { foo.with({pass_through()}).post("/bar", text("")); });
        });
        REQUIRE(count_middlewares(r) == 2);
    }
    SECTION("use on a sub-router combined with an outer with") {
        Router r;
        r.with({pass_through()}).group([](Router& g) {
            g.route("/foo", [](Router& foo) {
                foo.use({pass_through()});
                foo.with({pass_through()}).post("/bar", text(""));
            });
        });
        REQUIRE(count_middlewares(r) == 3);
    }
}

// chi: TestWalkRouteWithHandlerAndSubrouter
TEST_CASE("chi TestWalkRouteWithHandlerAndSubrouter") {
    Router r;
    r.route("/foo", [](Router& foo) {
        foo.route("/bar", [](Router& bar) { bar.get("/{id}", text("")); });
        foo.get("/bar", text(""));
    });
    std::vector<std::string> routes;
    REQUIRE(ki::walk(
        r, [&](std::string_view method, std::string_view route, const Handler&, std::span<const Middleware>) -> Result {
            routes.push_back(std::string{method} + " " + std::string{route});
            return {};
        }));
    REQUIRE(std::ranges::contains(routes, "GET /foo/bar"));
    REQUIRE(std::ranges::contains(routes, "GET /foo/bar/{id}"));
}

// chi: TestRoutesHidesMountStub
TEST_CASE("chi TestRoutesHidesMountStub") {
    SECTION("a plain mount reports a single sub-router entry") {
        Router r;
        r.route("/api", [](Router& api) { api.get("/{id}", text("")); });
        const auto routes = r.routes();
        REQUIRE(routes.size() == 1);
        REQUIRE(routes[0].pattern == "/api/*");
        REQUIRE(routes[0].subroutes != nullptr);
    }
    SECTION("a handler colliding with a mount pattern hides the stub") {
        Router r;
        r.route("/bar", [](Router& bar) { bar.get("/{id}", text("")); });
        r.get("/bar", text(""));
        for (const auto& route : r.routes()) {
            if (route.pattern != "/bar") {
                continue;
            }
            REQUIRE(route.subroutes == nullptr);
            REQUIRE(route.handlers.size() == 1);
            REQUIRE(route.handlers.contains("GET"));
        }
    }
}

// chi: TestWalkMiddlewaresAcrossGroupAndRoute
TEST_CASE("chi TestWalkMiddlewaresAcrossGroupAndRoute") {
    const auto request_id = [](Handler next) { return next; };
    const auto timeout = [](Handler next) { return next; };
    const auto name_of = [&](const Middleware& m) -> std::string {
        if (m.target_type() == typeid(request_id)) {
            return "requestID";
        }
        if (m.target_type() == typeid(timeout)) {
            return "timeout";
        }
        return "unknown";
    };

    Router r;
    r.use({request_id});
    r.get("/A", text(""));
    r.group([&](Router& g) {
        g.use({timeout});
        g.get("/B", text(""));
        g.route("/C", [](Router& c) { c.get("/D", text("")); });
    });

    std::map<std::string, std::vector<std::string>> got;
    REQUIRE(ki::walk(r,
                     [&](std::string_view method, std::string_view route, const Handler&,
                         std::span<const Middleware> middlewares) -> Result {
                         auto& names = got[std::string{method} + " " + std::string{route}];
                         for (const auto& m : middlewares) {
                             names.push_back(name_of(m));
                         }
                         return {};
                     }));
    REQUIRE(got["GET /A"] == std::vector<std::string>{"requestID"});
    REQUIRE(got["GET /B"] == std::vector<std::string>{"requestID", "timeout"});
    REQUIRE(got["GET /C/D"] == std::vector<std::string>{"requestID", "timeout"});
}
