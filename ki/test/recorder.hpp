#pragma once

#include <ki/error.hpp>
#include <ki/test/export.hpp>
#include <ki/handler.hpp>
#include <ki/headers.hpp>
#include <ki/request.hpp>
#include <ki/response.hpp>
#include <ki/status.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <string>

namespace ki::test {

// Recorded is what a client would have received from a handler run by Recorder, and how the handler ended.
struct Recorded {
    Status status = Status::ok;
    Headers headers{};
    std::string body{};
    std::size_t head_writes = 0;
    std::size_t flushes = 0;
    bool finished = false;
    Response::State state = Response::State::headers_pending;
    std::uint64_t bytes_written = 0;
    // result is what the handler returned; it is meaningful only when exception is empty.
    Result result{};
    std::exception_ptr exception{};
};

// make_request builds a request with an in-memory body for use with Recorder.
KI_TEST_EXPORT Request make_request(std::string method, std::string target, Headers headers = {}, std::string body = {});

// Recorder runs a handler without a socket, as Go's httptest.ResponseRecorder does. It finishes the response
// after the handler returns, as the server will.
class Recorder {
public:
    KI_TEST_EXPORT static Recorded run(const Handler& handler, Request& request);

    // run with cancel_after cancels the handler's pending operation once the delay passes; the handler sees
    // asio::error::operation_aborted, as it would when a client disconnects.
    KI_TEST_EXPORT static Recorded run(const Handler& handler, Request& request, std::chrono::milliseconds cancel_after);

private:
    static Recorded run_impl(const Handler& handler, Request& request,
                             std::optional<std::chrono::milliseconds> cancel_after);
};

}  // namespace ki::test
