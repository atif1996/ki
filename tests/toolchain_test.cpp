// T0.2: the C++23, Asio and llhttp baseline that the rest of ki depends on, checked on every platform.

#include <asio/as_tuple.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <catch2/catch_test_macros.hpp>
#include <llhttp.h>

#include <chrono>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace {

std::expected<int, std::string> half(int n) {
    if (n % 2 != 0) {
        return std::unexpected("odd");
    }
    return n / 2;
}

asio::awaitable<void> wait_once(std::optional<std::error_code>& result) {
    asio::steady_timer timer{co_await asio::this_coro::executor, std::chrono::milliseconds{1}};
    auto [ec] = co_await timer.async_wait(asio::as_tuple(asio::use_awaitable));
    result = ec;
}

}  // namespace

TEST_CASE("std::expected carries values and errors") {
    REQUIRE(half(4) == 2);
    REQUIRE_FALSE(half(3).has_value());
    REQUIRE(half(3).error() == "odd");
}

TEST_CASE("an Asio coroutine receives an error code instead of an exception") {
    asio::io_context io;
    std::optional<std::error_code> result;
    asio::co_spawn(io, wait_once(result), asio::detached);
    io.run();
    REQUIRE(result.has_value());
    REQUIRE_FALSE(*result);
}

TEST_CASE("llhttp parses pipelined requests from one buffer") {
    llhttp_settings_t settings{};
    llhttp_settings_init(&settings);
    settings.on_message_complete = [](llhttp_t* parser) -> int {
        ++*static_cast<int*>(parser->data);
        return 0;
    };

    llhttp_t parser{};
    llhttp_init(&parser, HTTP_REQUEST, &settings);
    int messages = 0;
    parser.data = &messages;

    constexpr std::string_view input =
        "GET /a HTTP/1.1\r\nHost: x\r\n\r\n"
        "GET /b HTTP/1.1\r\nHost: x\r\n\r\n";
    REQUIRE(llhttp_execute(&parser, input.data(), input.size()) == HPE_OK);
    REQUIRE(messages == 2);
}
