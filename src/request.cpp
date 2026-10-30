#include <ki/request.hpp>

#include <algorithm>
#include <array>

namespace ki {

Request::Request(std::string method, std::string target, Headers headers, std::unique_ptr<detail::BodySource> body)
    : method_{std::move(method)},
      target_{std::move(target)},
      path_end_{std::min(target_.find('?'), target_.size())},
      headers_{std::move(headers)},
      body_{std::move(body)} {}

std::string_view Request::raw_query() const noexcept {
    if (path_end_ == target_.size()) {
        return {};
    }
    std::string_view query{target_};
    query.remove_prefix(path_end_ + 1);
    return query;
}

asio::awaitable<std::expected<std::size_t, Error>> Request::read(std::span<std::byte> buffer) {
    if (!body_) {
        co_return std::size_t{0};
    }
    co_return co_await body_->read(buffer);
}

asio::awaitable<std::expected<std::string, Error>> Request::read_all(std::size_t limit) {
    std::string body;
    std::array<std::byte, std::size_t{16} * 1024> chunk{};
    for (;;) {
        auto n = co_await read(chunk);
        if (!n) {
            co_return std::unexpected(std::move(n.error()));
        }
        if (*n == 0) {
            co_return body;
        }
        if (body.size() + *n > limit) {
            co_return std::unexpected(Error{.status = Status::content_too_large, .message = "request body too large"});
        }
        const auto bytes = std::span{chunk}.first(*n);
        for (const std::byte b : bytes) {
            body.push_back(static_cast<char>(b));
        }
    }
}

}  // namespace ki
