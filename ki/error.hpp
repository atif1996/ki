#pragma once

#include <ki/status.hpp>

#include <expected>
#include <string>
#include <system_error>

namespace ki {

// Error is an expected failure. status and message are what the client receives, so message must be safe to
// show; cause carries internal detail for logs only.
struct Error {
    Status status = Status::internal_server_error;
    std::string message{};
    std::error_code cause{};
};

using Result = std::expected<void, Error>;

}  // namespace ki
