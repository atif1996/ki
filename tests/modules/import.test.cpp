// The libraries used through import, with no ki header included. Asio's headers are: a module keeps only the
// parts of a header it names itself, and handler code instantiates Asio templates that need the rest.

#include <asio/awaitable.hpp>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <system_error>

import ki;
import ki.log;
import ki.test;

namespace {

struct User {
    std::string name;
};

inline constexpr ki::ContextKey<User> user_key{"test.user"};

}  // namespace

TEST_CASE("a router built through the module routes a request") {
    ki::Router router;
    router.get("/hello/{name}", [](ki::Request& req, ki::Response& res) -> asio::awaitable<ki::Result> {
        req.set(user_key, User{.name = std::string{req.path_value("name")}});
        co_return co_await res.text("hello " + req.get(user_key)->get().name);
    });
    auto request = ki::test::make_request("GET", "/hello/ada");
    const auto out = ki::test::Recorder::run(router, request);
    REQUIRE(out.status == ki::Status::ok);
    REQUIRE(out.body == "hello ada");
    REQUIRE(ki::reason_phrase(ki::Status::not_found) == "Not Found");
    REQUIRE_FALSE(ki::version().empty());
}

TEST_CASE("a logger built through the module writes a record") {
    std::string written;
    auto sink = std::make_shared<ki::log::TextSink>([&](std::string_view line) {
        written += line;
        return std::error_code{};
    });
    ki::log::Logger{sink}.info("started");
    REQUIRE(written.find("started") != std::string::npos);
}
