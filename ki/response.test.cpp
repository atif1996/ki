#include <ki/handler.hpp>
#include <ki/test/recorder.hpp>

#include <asio/as_tuple.hpp>
#include <asio/error.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <stdexcept>
#include <string>
#include <utility>

using ki::Request;
using ki::Response;
using ki::Result;
using ki::Status;
using ki::test::Recorder;

namespace {

Result fail(Status status, std::string message) { return std::unexpected(ki::Error{status, std::move(message)}); }

}  // namespace

// T1.1
TEST_CASE("the first write sends status 200 when no status was chosen") {
    auto request = ki::test::make_request("GET", "/");
    auto out = Recorder::run(
        [](Request&, Response& res) -> asio::awaitable<Result> { co_return co_await res.write("hello"); }, request);

    REQUIRE(out.result);
    REQUIRE(out.status == Status::ok);
    REQUIRE(out.body == "hello");
    REQUIRE(out.head_writes == 1);
    REQUIRE(out.finished);
    REQUIRE(out.bytes_written == 5);
}

TEST_CASE("a handler that writes nothing still sends 200 when the response finishes") {
    auto request = ki::test::make_request("GET", "/");
    auto out = Recorder::run([](Request&, Response&) -> asio::awaitable<Result> { co_return Result{}; }, request);
    REQUIRE(out.status == Status::ok);
    REQUIRE(out.head_writes == 1);
    REQUIRE(out.body.empty());
    REQUIRE(out.finished);
}

// T1.2
TEST_CASE("headers cannot change after they are sent") {
    auto request = ki::test::make_request("GET", "/");
    Result late_set;
    Result late_remove;
    auto out = Recorder::run(
        [&](Request&, Response& res) -> asio::awaitable<Result> {
            REQUIRE(res.set_header("X-Early", "1"));
            co_await res.write("body");
            late_set = res.set_header("X-Late", "1");
            late_remove = res.remove_header("X-Early");
            co_return Result{};
        },
        request);

    REQUIRE_FALSE(late_set);
    REQUIRE_FALSE(late_remove);
    REQUIRE(out.headers.get("X-Early") == "1");
    REQUIRE_FALSE(out.headers.contains("X-Late"));
}

// T1.3
TEST_CASE("a second write_header is reported and changes nothing") {
    auto request = ki::test::make_request("GET", "/");
    Result second;
    auto out = Recorder::run(
        [&](Request&, Response& res) -> asio::awaitable<Result> {
            REQUIRE(res.write_header(Status::created));
            second = res.write_header(Status::not_found);
            co_return co_await res.write("made");
        },
        request);

    REQUIRE_FALSE(second);
    REQUIRE(out.status == Status::created);
    REQUIRE(out.head_writes == 1);
}

TEST_CASE("writing after the response finished fails and finishing again does nothing") {
    auto request = ki::test::make_request("GET", "/");
    Result late_write;
    Result second_finish;
    auto out = Recorder::run(
        [&](Request&, Response& res) -> asio::awaitable<Result> {
            co_await res.write("done");
            co_await res.finish();
            late_write = co_await res.write("more");
            second_finish = co_await res.finish();
            co_return Result{};
        },
        request);

    REQUIRE_FALSE(late_write);
    REQUIRE(second_finish);
    REQUIRE(out.body == "done");
}

// T1.6
TEST_CASE("an error returned by a handler reaches the outermost middleware unchanged") {
    std::string seen_by_outer;
    const ki::Middleware outer = [&](ki::Handler next) -> ki::Handler {
        return [&, next = std::move(next)](Request& req, Response& res) -> asio::awaitable<Result> {
            auto result = co_await next(req, res);
            if (!result) {
                seen_by_outer = result.error().message;
            }
            co_return result;
        };
    };
    const ki::Middleware inner = [](ki::Handler next) -> ki::Handler {
        return [next = std::move(next)](Request& req, Response& res) -> asio::awaitable<Result> {
            co_return co_await next(req, res);
        };
    };
    const ki::Handler handler = [](Request&, Response&) -> asio::awaitable<Result> {
        co_return fail(Status::unprocessable_content, "bad input");
    };

    auto request = ki::test::make_request("POST", "/");
    auto out = Recorder::run(outer(inner(handler)), request);

    REQUIRE_FALSE(out.result);
    REQUIRE(out.result.error().status == Status::unprocessable_content);
    REQUIRE(out.result.error().message == "bad input");
    REQUIRE(seen_by_outer == "bad input");
}

// T1.7
TEST_CASE("the recorder captures status, headers and body and supplies the request body") {
    ki::Headers headers;
    REQUIRE(headers.add("Content-Type", "application/json"));
    auto request = ki::test::make_request("POST", "/echo?x=1", std::move(headers), R"({"a":1})");

    auto out = Recorder::run(
        [](Request& req, Response& res) -> asio::awaitable<Result> {
            auto body = co_await req.read_all(1024);
            if (!body) {
                co_return std::unexpected(body.error());
            }
            REQUIRE(req.method() == "POST");
            REQUIRE(req.path() == "/echo");
            REQUIRE(req.raw_query() == "x=1");
            REQUIRE(req.headers().get("content-type") == "application/json");
            co_return co_await res.text(*body);
        },
        request);

    REQUIRE(out.result);
    REQUIRE(out.status == Status::ok);
    REQUIRE(out.headers.get("Content-Type") == "text/plain; charset=utf-8");
    REQUIRE(out.body == R"({"a":1})");
}

TEST_CASE("text after write_header leaves the locked headers alone") {
    auto request = ki::test::make_request("GET", "/");
    auto out = Recorder::run(
        [](Request&, Response& res) -> asio::awaitable<Result> {
            REQUIRE(res.write_header(Status::created));
            co_return co_await res.text("made");
        },
        request);
    REQUIRE(out.status == Status::created);
    REQUIRE_FALSE(out.headers.contains("Content-Type"));
    REQUIRE(out.body == "made");
}

TEST_CASE("read_all rejects a body over its limit") {
    auto request = ki::test::make_request("POST", "/", {}, std::string(100, 'x'));
    auto out = Recorder::run(
        [](Request& req, Response&) -> asio::awaitable<Result> {
            auto body = co_await req.read_all(10);
            if (!body) {
                co_return std::unexpected(body.error());
            }
            co_return Result{};
        },
        request);
    REQUIRE_FALSE(out.result);
    REQUIRE(out.result.error().status == Status::content_too_large);
}

// T1.8
TEST_CASE("cancellation reaches a waiting handler as operation_aborted") {
    std::error_code observed;
    auto request = ki::test::make_request("GET", "/slow");
    const auto started = std::chrono::steady_clock::now();
    auto out = Recorder::run(
        [&](Request&, Response&) -> asio::awaitable<Result> {
            asio::steady_timer timer{co_await asio::this_coro::executor, std::chrono::seconds{10}};
            auto [ec] = co_await timer.async_wait(asio::as_tuple(asio::use_awaitable));
            observed = ec;
            co_return fail(Status::service_unavailable, "cancelled");
        },
        request, std::chrono::milliseconds{10});

    REQUIRE(observed == asio::error::operation_aborted);
    REQUIRE(std::chrono::steady_clock::now() - started < std::chrono::seconds{2});
    REQUIRE_FALSE(out.exception);
    REQUIRE_FALSE(out.result);
    REQUIRE(out.result.error().message == "cancelled");
    REQUIRE(out.finished);
}

// T1.9
TEST_CASE("a handler that throws leaves the response valid and inspectable") {
    auto request = ki::test::make_request("GET", "/");
    auto out = Recorder::run(
        [](Request&, Response& res) -> asio::awaitable<Result> {
            REQUIRE(res.set_header("X-Before", "1"));
            co_await res.write("partial");
            throw std::runtime_error{"handler bug"};
        },
        request);

    REQUIRE(out.exception);
    REQUIRE_THROWS_AS(std::rethrow_exception(out.exception), std::runtime_error);
    REQUIRE(out.state == Response::State::headers_sent);
    REQUIRE(out.body == "partial");
    REQUIRE(out.bytes_written == 7);
    REQUIRE(out.headers.get("X-Before") == "1");
    REQUIRE_FALSE(out.finished);
}
