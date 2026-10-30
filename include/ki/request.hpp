#pragma once

#include <ki/context.hpp>
#include <ki/error.hpp>
#include <ki/export.hpp>
#include <ki/headers.hpp>
#include <ki/transport.hpp>

#include <asio/awaitable.hpp>

#include <cstddef>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace ki {

// Request is one HTTP request as seen by handlers and middleware.
class Request {
public:
    KI_EXPORT Request(std::string method, std::string target, Headers headers = {},
                      std::unique_ptr<detail::BodySource> body = {});

    [[nodiscard]] std::string_view method() const noexcept { return method_; }
    // target is the request-target exactly as received, including any query.
    [[nodiscard]] std::string_view target() const noexcept { return target_; }
    [[nodiscard]] std::string_view path() const noexcept {
        std::string_view path{target_};
        path.remove_suffix(target_.size() - path_end_);
        return path;
    }
    // raw_query is the undecoded text after the first '?', without the '?'.
    [[nodiscard]] KI_EXPORT std::string_view raw_query() const noexcept;

    [[nodiscard]] Headers& headers() noexcept { return headers_; }
    [[nodiscard]] const Headers& headers() const noexcept { return headers_; }

    [[nodiscard]] Context& context() noexcept { return context_; }
    [[nodiscard]] const Context& context() const noexcept { return context_; }

    template <typename T, typename U = T>
        requires std::constructible_from<T, U&&>
    void set(const ContextKey<T>& key, U&& value) {
        context_.set(key, std::forward<U>(value));
    }

    template <typename T>
    [[nodiscard]] std::optional<std::reference_wrapper<const T>> get(const ContextKey<T>& key) const noexcept {
        return context_.get(key);
    }

    template <typename T>
    [[nodiscard]] T value_or(const ContextKey<T>& key, std::type_identity_t<T> fallback) const {
        return context_.value_or(key, std::move(fallback));
    }

    template <typename T, std::invocable<const T&> F>
    bool with(const ContextKey<T>& key, F&& f) const {
        return context_.with(key, std::forward<F>(f));
    }

    // read fills buffer with the next bytes of the body and returns how many it wrote; 0 means the body ended.
    KI_EXPORT asio::awaitable<std::expected<std::size_t, Error>> read(std::span<std::byte> buffer);

    // read_all returns the whole body, or Status::content_too_large once it exceeds limit bytes.
    KI_EXPORT asio::awaitable<std::expected<std::string, Error>> read_all(std::size_t limit);

private:
    std::string method_;
    std::string target_;
    std::size_t path_end_;
    Headers headers_;
    Context context_;
    std::unique_ptr<detail::BodySource> body_;
};

}  // namespace ki
