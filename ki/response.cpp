#include <ki/response.hpp>

#include <string>
#include <utility>

namespace ki {

Response::Response(std::unique_ptr<detail::ResponseTransport> transport) : transport_{std::move(transport)} {}

Result Response::require_pending(std::string_view action) const {
    if (state_ != State::headers_pending) {
        return std::unexpected(Error{.message = std::string{action} + " after the response headers were sent"});
    }
    return {};
}

Result Response::set_header(std::string_view name, std::string_view value) {
    if (auto pending = require_pending("set_header"); !pending) {
        return pending;
    }
    return headers_.set(name, value);
}

Result Response::add_header(std::string_view name, std::string_view value) {
    if (auto pending = require_pending("add_header"); !pending) {
        return pending;
    }
    return headers_.add(name, value);
}

Result Response::remove_header(std::string_view name) {
    if (auto pending = require_pending("remove_header"); !pending) {
        return pending;
    }
    headers_.remove(name);
    return {};
}

Result Response::write_header(Status status) {
    if (auto pending = require_pending("write_header"); !pending) {
        return pending;
    }
    status_ = status;
    state_ = State::headers_sent;
    return {};
}

asio::awaitable<Result> Response::send_head() {
    if (state_ == State::headers_pending) {
        state_ = State::headers_sent;
    }
    if (head_sent_) {
        co_return Result{};
    }
    head_sent_ = true;
    co_return co_await transport_->write_head(status_, headers_);
}

asio::awaitable<Result> Response::write(std::span<const std::byte> data) {
    if (state_ == State::finished) {
        co_return std::unexpected(Error{.message = "write after the response finished"});
    }
    if (auto head = co_await send_head(); !head) {
        co_return head;
    }
    auto written = co_await transport_->write_body(data);
    if (written) {
        bytes_written_ += data.size();
    }
    co_return written;
}

asio::awaitable<Result> Response::write(std::string_view text) {
    co_return co_await write(std::as_bytes(std::span{text.data(), text.size()}));
}

asio::awaitable<Result> Response::flush() {
    if (state_ == State::finished) {
        co_return Result{};
    }
    if (auto head = co_await send_head(); !head) {
        co_return head;
    }
    co_return co_await transport_->flush();
}

asio::awaitable<Result> Response::finish() {
    if (state_ == State::finished) {
        co_return Result{};
    }
    if (auto head = co_await send_head(); !head) {
        co_return head;
    }
    state_ = State::finished;
    co_return co_await transport_->finish();
}

asio::awaitable<Result> Response::text(std::string_view body) {
    if (!headers_sent() && !headers_.contains("Content-Type")) {
        if (auto set = headers_.set("Content-Type", "text/plain; charset=utf-8"); !set) {
            co_return set;
        }
    }
    co_return co_await write(body);
}

}  // namespace ki
