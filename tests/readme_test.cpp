// The request-values example from README.md, compiled and run so the documentation stays accurate.

#include <ki/ki.hpp>
#include <ki/test/recorder.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>

namespace {

// --- README example begins ---
struct User {
    int id;
    std::string name;
};

inline constexpr ki::ContextKey<User> user_key{"myapp.user"};

ki::Handler authenticate(ki::Handler next) {
    return [next = std::move(next)](ki::Request& req, ki::Response& res) -> asio::awaitable<ki::Result> {
        req.set(user_key, User{.id = 42, .name = "ada"});
        co_return co_await next(req, res);
    };
}

asio::awaitable<ki::Result> greet(ki::Request& req, ki::Response& res) {
    if (auto user = req.get(user_key)) {
        co_return co_await res.text("hello " + user->get().name);
    }
    co_return std::unexpected(ki::Error{ki::Status::unauthorized, "sign in first"});
}
// --- README example ends ---

}  // namespace

TEST_CASE("README: middleware stores a value and the handler reads it") {
    auto request = ki::test::make_request("GET", "/hello");
    auto out = ki::test::Recorder::run(authenticate(greet), request);
    REQUIRE(out.status == ki::Status::ok);
    REQUIRE(out.body == "hello ada");
}

TEST_CASE("README: without the middleware the value is absent") {
    auto request = ki::test::make_request("GET", "/hello");
    auto out = ki::test::Recorder::run(greet, request);
    REQUIRE_FALSE(out.result);
    REQUIRE(out.result.error().status == ki::Status::unauthorized);
}

TEST_CASE("README: value_or and with") {
    auto request = ki::test::make_request("GET", "/hello");
    REQUIRE(request.value_or(user_key, User{.id = 0, .name = "guest"}).name == "guest");
    request.set(user_key, User{.id = 7, .name = "lin"});
    std::string seen;
    REQUIRE(request.with(user_key, [&](const User& u) { seen = u.name; }));
    REQUIRE(seen == "lin");
}
