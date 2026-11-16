#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>

import ki_module_check;

int main() {
    asio::io_context io;
    int out = 0;
    asio::co_spawn(
        io, [&]() -> asio::awaitable<void> { out = (co_await ki_module_check::answer()).value_or(-1); },
        asio::detached);
    io.run();
    return out == 42 ? 0 : 1;
}
