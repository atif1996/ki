#pragma once

#include <ki/error.hpp>
#include <ki/headers.hpp>
#include <ki/status.hpp>

#include <asio/awaitable.hpp>

#include <cstddef>
#include <expected>
#include <span>

namespace ki::detail {

// BodySource supplies a request body to Request. read returns 0 at the end of the body.
class BodySource {
public:
    BodySource() = default;
    BodySource(const BodySource&) = delete;
    BodySource& operator=(const BodySource&) = delete;
    BodySource(BodySource&&) = delete;
    BodySource& operator=(BodySource&&) = delete;
    virtual ~BodySource() = default;

    virtual asio::awaitable<std::expected<std::size_t, Error>> read(std::span<std::byte> buffer) = 0;
};

// ResponseTransport carries a Response to its destination: an HTTP/1.1 connection, the test recorder, or
// later an HTTP/2 stream. Response guarantees write_head is called once, before any body bytes.
class ResponseTransport {
public:
    ResponseTransport() = default;
    ResponseTransport(const ResponseTransport&) = delete;
    ResponseTransport& operator=(const ResponseTransport&) = delete;
    ResponseTransport(ResponseTransport&&) = delete;
    ResponseTransport& operator=(ResponseTransport&&) = delete;
    virtual ~ResponseTransport() = default;

    virtual asio::awaitable<Result> write_head(Status status, const Headers& headers) = 0;
    virtual asio::awaitable<Result> write_body(std::span<const std::byte> data) = 0;
    virtual asio::awaitable<Result> flush() = 0;
    virtual asio::awaitable<Result> finish() = 0;
};

}  // namespace ki::detail
