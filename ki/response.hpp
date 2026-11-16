#pragma once

#include <ki/error.hpp>
#include <ki/export.hpp>
#include <ki/headers.hpp>
#include <ki/status.hpp>
#include <ki/transport.hpp>

#include <asio/awaitable.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace ki {

// Response is the reply to one request. It moves through three states: headers may change only while
// headers_pending; the first write sends status 200 unless write_header chose another; nothing may be
// written once finished. Status and bytes written are always tracked, so middleware reads them directly
// instead of wrapping the response.
class Response {
public:
    enum class State : std::uint8_t { headers_pending, headers_sent, finished };

    KI_EXPORT explicit Response(std::unique_ptr<detail::ResponseTransport> transport);

    [[nodiscard]] Status status() const noexcept { return status_; }
    [[nodiscard]] State state() const noexcept { return state_; }
    [[nodiscard]] bool headers_sent() const noexcept { return state_ != State::headers_pending; }
    [[nodiscard]] std::uint64_t bytes_written() const noexcept { return bytes_written_; }
    [[nodiscard]] const Headers& headers() const noexcept { return headers_; }

    [[nodiscard]] KI_EXPORT Result set_header(std::string_view name, std::string_view value);
    [[nodiscard]] KI_EXPORT Result add_header(std::string_view name, std::string_view value);
    [[nodiscard]] KI_EXPORT Result remove_header(std::string_view name);

    // write_header fixes the status and locks the headers; a second call fails and changes nothing.
    [[nodiscard]] KI_EXPORT Result write_header(Status status);

    KI_EXPORT asio::awaitable<Result> write(std::span<const std::byte> data);
    KI_EXPORT asio::awaitable<Result> write(std::string_view text);
    KI_EXPORT asio::awaitable<Result> flush();
    // finish ends the response; the server calls it after the handler returns, and later calls do nothing.
    KI_EXPORT asio::awaitable<Result> finish();

    // text writes body, first setting Content-Type to text/plain if no content type was chosen.
    KI_EXPORT asio::awaitable<Result> text(std::string_view body);

private:
    [[nodiscard]] Result require_pending(std::string_view action) const;
    asio::awaitable<Result> send_head();

    std::unique_ptr<detail::ResponseTransport> transport_;
    Headers headers_;
    Status status_ = Status::ok;
    State state_ = State::headers_pending;
    bool head_sent_ = false;
    std::uint64_t bytes_written_ = 0;
};

}  // namespace ki
