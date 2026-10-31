// T2.15, T2.22, T2.23

#include "router_support.hpp"

#include <ki/router.hpp>
#include <ki/test/recorder.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using ki::Handler;
using ki::Middleware;
using ki::Request;
using ki::Response;
using ki::Result;
using ki::Router;
using ki::Status;
using ki::testing::named;
using ki::testing::serve;

// T2.15
TEST_CASE("every verb helper routes its own method") {
    Router r;
    r.connect("/x", named("CONNECT"));
    r.del("/x", named("DELETE"));
    r.get("/x", named("GET"));
    r.head("/x", named("HEAD"));
    r.options("/x", named("OPTIONS"));
    r.patch("/x", named("PATCH"));
    r.post("/x", named("POST"));
    r.put("/x", named("PUT"));
    r.query("/x", named("QUERY"));
    r.trace("/x", named("TRACE"));
    REQUIRE(r.errors().empty());
    for (const auto* method : {"DELETE", "GET", "HEAD", "OPTIONS", "PATCH", "POST", "PUT", "QUERY", "TRACE"}) {
        INFO(method);
        REQUIRE(serve(r, method, "/x").name == method);
    }
    // CONNECT requests carry an authority-form target, so its route is checked with find().
    REQUIRE(r.find("CONNECT", "/x") == "/x");
}

// T2.15
TEST_CASE("handle matches every method the router knows, including registered ones") {
    Router r;
    r.register_method("LINK");
    r.handle("/any", named("any"));
    for (const auto* method : {"GET", "POST", "DELETE", "LINK"}) {
        INFO(method);
        REQUIRE(serve(r, method, "/any").name == "any");
    }
    REQUIRE(serve(r, "UNLINK", "/any").status == Status::method_not_allowed);
}

// T2.15
TEST_CASE("middlewares() reports the stack and chain() applies it outermost first") {
    std::string order;
    const auto tag = [&order](char c) -> Middleware {
        return [&order, c](Handler next) -> Handler {
            return [&order, c, next = std::move(next)](Request& req, Response& res) -> asio::awaitable<Result> {
                order.push_back(c);
                co_return co_await next(req, res);
            };
        };
    };
    Router r;
    r.use({tag('a'), tag('b')});
    REQUIRE(r.middlewares().size() == 2);

    const std::vector<Middleware> stack = {tag('x'), tag('y'), tag('z')};
    const auto handler = ki::chain(stack, named("end"));
    auto request = ki::test::make_request("GET", "/");
    REQUIRE(ki::test::Recorder::run(handler, request).body == "end");
    REQUIRE(order == "xyz");
}

// T2.15: chi's RoutePath, which middleware sets to change what the router matches.
TEST_CASE("middleware can rewrite the path the router matches") {
    Router r;
    r.use({[](Handler next) -> Handler {
        return [next = std::move(next)](Request& req, Response& res) -> asio::awaitable<Result> {
            if (req.path().starts_with("/v1/")) {
                req.route().route_path = std::string{req.path().substr(3)};
            }
            co_return co_await next(req, res);
        };
    }});
    r.get("/users/{id}", named("user"));
    const auto hit = serve(r, "GET", "/v1/users/9");
    REQUIRE(hit.name == "user");
    REQUIRE(hit.values == std::vector<std::string>{"9"});
}

// T2.22
TEST_CASE("a frozen router rejects registration and keeps serving") {
    Router sub;
    sub.get("/", named("sub"));
    Router r;
    r.get("/a", named("a"));
    r.mount("/sub", sub);
    r.freeze();

    r.get("/b", named("b"));
    r.use({[](Handler next) { return next; }});
    r.register_method("LINK");
    sub.get("/late", named("late"));

    REQUIRE(r.errors().size() == 4);
    REQUIRE(serve(r, "GET", "/a").name == "a");
    REQUIRE(serve(r, "GET", "/b").status == Status::not_found);
    REQUIRE(serve(r, "GET", "/sub/late").status == Status::not_found);
}

// T2.22: Go's TestServeMuxHandleFuncWithNilHandler
TEST_CASE("an empty handler is rejected at registration") {
    Router r;
    r.get("/x", Handler{});
    r.mount("/m", Handler{});
    REQUIRE(r.errors().size() == 2);
    REQUIRE(r.routes().empty());
}

// T2.23: Go's TestPathValueAndPattern, TestPathValueNoMatch, TestSetPathValue
TEST_CASE("path values and the matched pattern are available to handlers") {
    Router r;
    r.get("/a/{x}/b/{y}", [](Request& req, Response& res) -> asio::awaitable<Result> {
        co_return co_await res.write(std::string{req.path_value("x")} + "," + std::string{req.path_value("y")} + "," +
                                     std::string{req.path_value("missing")} + "," + req.route_pattern());
    });
    auto request = ki::test::make_request("GET", "/a/1/b/2");
    REQUIRE(ki::test::Recorder::run(r, request).body == "1,2,,/a/{x}/b/{y}");

    auto unmatched = ki::test::make_request("GET", "/nope");
    (void)ki::test::Recorder::run(r, unmatched);
    REQUIRE(unmatched.path_value("x").empty());
    REQUIRE(unmatched.route_pattern().empty());

    auto set = ki::test::make_request("GET", "/");
    set.set_path_value("a", "1");
    set.set_path_value("a", "2");
    set.set_path_value("b", "3");
    REQUIRE(set.path_value("a") == "2");
    REQUIRE(set.path_value("b") == "3");
}
