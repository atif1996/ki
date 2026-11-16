#pragma once

#include <ki/context.hpp>
#include <ki/error.hpp>
#include <ki/export.hpp>
#include <ki/headers.hpp>
#include <ki/query.hpp>
#include <ki/route_context.hpp>
#include <ki/transport.hpp>
#include <ki/url.hpp>

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

struct FormOptions {
    // max_body is Go's default limit for URL-encoded form bodies.
    std::size_t max_body = std::size_t{10} << 20U;
    QueryOptions query{};
};

struct ParsedForm {
    QueryValues form{};
    QueryValues post_form{};
    std::optional<Error> error{};
};

// Request is one HTTP request as seen by handlers and middleware.
class Request {
public:
    KI_EXPORT Request(std::string method, std::string target, Headers headers = {},
                      std::unique_ptr<detail::BodySource> body = {});

    [[nodiscard]] std::string_view method() const noexcept { return method_; }
    // target is the request-target exactly as received, including any query.
    [[nodiscard]] std::string_view target() const noexcept { return target_; }
    [[nodiscard]] url::TargetForm target_form() const noexcept { return form_; }
    // target_error is set when the request-target is malformed; the server answers such requests with 400
    // before any handler runs.
    [[nodiscard]] const std::optional<Error>& target_error() const noexcept { return target_error_; }

    // path is the decoded path; raw_path is the path as received, with escapes intact.
    [[nodiscard]] std::string_view path() const noexcept {
        return decoded_path_ ? std::string_view{*decoded_path_} : raw_path();
    }
    [[nodiscard]] std::string_view raw_path() const noexcept { return raw_path_; }
    // raw_query is the undecoded text after the first '?', without the '?'.
    [[nodiscard]] std::string_view raw_query() const noexcept { return raw_query_; }
    // authority is the host and port of an absolute-form or authority-form target, empty otherwise.
    [[nodiscard]] std::string_view authority() const noexcept { return authority_; }

    // query returns the decoded query parameters, parsing them on first use. query_error reports the first
    // malformed pair; the well-formed pairs are still available.
    [[nodiscard]] KI_EXPORT const QueryValues& query() const;
    [[nodiscard]] KI_EXPORT const std::optional<Error>& query_error() const;

    // route is the request's routing state, filled in by routers.
    [[nodiscard]] RouteContext& route() noexcept { return route_; }
    [[nodiscard]] const RouteContext& route() const noexcept { return route_; }
    // path_value returns the value of the route parameter named key, as Go's Request.PathValue does.
    [[nodiscard]] std::string_view path_value(std::string_view key) const noexcept { return route_.url_param(key); }
    KI_EXPORT void set_path_value(std::string_view key, std::string_view value);
    // route_pattern is the full pattern that matched, such as "/users/{id}", as Go's Request.Pattern.
    [[nodiscard]] std::string route_pattern() const { return route_.route_pattern(); }

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

    // read_form parses a URL-encoded form body as Go's Request.ParseForm does: only for POST, PUT and PATCH
    // with Content-Type application/x-www-form-urlencoded. form holds body values before query values;
    // post_form holds body values only.
    KI_EXPORT asio::awaitable<ParsedForm> read_form(FormOptions options = {});

private:
    [[nodiscard]] const ParsedQuery& parsed_query() const;

    std::string method_{};
    std::string target_{};
    url::TargetForm form_ = url::TargetForm::origin;
    std::string raw_path_{};
    std::string raw_query_{};
    std::string authority_{};
    std::optional<Error> target_error_{};
    std::optional<std::string> decoded_path_{};
    mutable std::optional<ParsedQuery> query_;
    Headers headers_{};
    Context context_{};
    RouteContext route_{};
    std::unique_ptr<detail::BodySource> body_{};
};

}  // namespace ki
