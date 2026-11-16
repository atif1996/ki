#include <ki/test/recorder.hpp>

#include <ki/detail/strings.hpp>

#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>

#include <algorithm>
#include <iterator>
#include <memory>
#include <utility>

namespace ki::test {
namespace {

class StringBody final : public detail::BodySource {
public:
    explicit StringBody(std::string data) : data_{std::move(data)} {}

    asio::awaitable<std::expected<std::size_t, Error>> read(std::span<std::byte> buffer) override {
        const std::size_t n = std::min(buffer.size(), data_.size() - offset_);
        std::ranges::transform(detail::sub(data_, offset_, n), buffer.begin(),
                               [](char c) { return static_cast<std::byte>(c); });
        offset_ += n;
        co_return n;
    }

private:
    std::string data_;
    std::size_t offset_ = 0;
};

class RecordingTransport final : public detail::ResponseTransport {
public:
    explicit RecordingTransport(Recorded& out) : out_{out} {}

    asio::awaitable<Result> write_head(Status status, const Headers& headers) override {
        out_.status = status;
        out_.headers = headers;
        ++out_.head_writes;
        co_return Result{};
    }

    asio::awaitable<Result> write_body(std::span<const std::byte> data) override {
        std::ranges::transform(data, std::back_inserter(out_.body), [](std::byte b) { return static_cast<char>(b); });
        co_return Result{};
    }

    asio::awaitable<Result> flush() override {
        ++out_.flushes;
        co_return Result{};
    }

    asio::awaitable<Result> finish() override {
        out_.finished = true;
        co_return Result{};
    }

private:
    Recorded& out_;
};

asio::awaitable<Result> serve(const Handler& handler, Request& request, Response& response) {
    // Asio's default makes every co_await after a cancellation throw; ki reports cancellation as error codes.
    co_await asio::this_coro::throw_if_cancelled(false);
    auto result = co_await handler(request, response);
    if (auto finished = co_await response.finish(); !finished && result) {
        result = std::move(finished);
    }
    co_return result;
}

}  // namespace

Request make_request(std::string method, std::string target, Headers headers, std::string body) {
    return Request{std::move(method), std::move(target), std::move(headers),
                   std::make_unique<StringBody>(std::move(body))};
}

Recorded Recorder::run(const Handler& handler, Request& request) { return run_impl(handler, request, std::nullopt); }

Recorded Recorder::run(const Handler& handler, Request& request, std::chrono::milliseconds cancel_after) {
    return run_impl(handler, request, cancel_after);
}

Recorded Recorder::run_impl(const Handler& handler, Request& request,
                            std::optional<std::chrono::milliseconds> cancel_after) {
    Recorded out;
    asio::io_context io;
    Response response{std::make_unique<RecordingTransport>(out)};
    asio::cancellation_signal cancel;
    asio::steady_timer deadline{io};

    asio::co_spawn(io, serve(handler, request, response),
                   asio::bind_cancellation_slot(cancel.slot(), [&](std::exception_ptr error, Result result) {
                       out.exception = std::move(error);
                       out.result = std::move(result);
                       deadline.cancel();
                   }));
    if (cancel_after) {
        deadline.expires_after(*cancel_after);
        deadline.async_wait([&cancel](std::error_code ec) {
            if (!ec) {
                cancel.emit(asio::cancellation_type::terminal);
            }
        });
    }
    io.run();

    out.state = response.state();
    out.bytes_written = response.bytes_written();
    return out;
}

}  // namespace ki::test
