#include <ki/request.hpp>

#include <ki/detail/strings.hpp>

#include <algorithm>
#include <array>

namespace ki {
namespace {

bool is_token_char(char c) noexcept {
    const auto u = static_cast<unsigned char>(c);
    return (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') ||
           std::string_view{"!#$%&'*+-.^_`|~"}.find(c) != std::string_view::npos;
}

std::size_t token_length(std::string_view text) noexcept {
    return static_cast<std::size_t>(std::ranges::find_if_not(text, is_token_char) - text.begin());
}

void skip_whitespace(std::string_view& text) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
}

// media_type returns the lowercased type/subtype of a Content-Type value after checking its parameters, as
// Go's mime.ParseMediaType does; an empty parameter value is an error.
std::expected<std::string, Error> media_type(std::string_view value) {
    const auto bad = std::unexpected(Error{.status = Status::bad_request, .message = "invalid Content-Type"});
    skip_whitespace(value);
    const auto type = token_length(value);
    if (type == 0 || type >= value.size() || value[type] != '/') {
        return bad;
    }
    const auto subtype = token_length(detail::sub(value, type + 1));
    if (subtype == 0) {
        return bad;
    }
    std::string essence{detail::sub(value, 0, type + 1 + subtype)};
    std::ranges::transform(essence, essence.begin(),
                           [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; });

    auto rest = detail::sub(value, type + 1 + subtype);
    for (;;) {
        skip_whitespace(rest);
        if (rest.empty()) {
            return essence;
        }
        if (rest.front() != ';') {
            return bad;
        }
        rest.remove_prefix(1);
        skip_whitespace(rest);
        if (rest.empty()) {
            return essence;
        }
        const auto name = token_length(rest);
        if (name == 0 || name >= rest.size() || rest[name] != '=') {
            return bad;
        }
        rest.remove_prefix(name + 1);
        if (!rest.empty() && rest.front() == '"') {
            std::size_t i = 1;
            while (i < rest.size() && rest[i] != '"') {
                i += rest[i] == '\\' ? 2 : 1;
            }
            if (i >= rest.size()) {
                return bad;
            }
            rest.remove_prefix(i + 1);
        } else {
            const auto length = token_length(rest);
            if (length == 0) {
                return bad;
            }
            rest.remove_prefix(length);
        }
    }
}

}  // namespace

Request::Request(std::string method, std::string target, Headers headers, std::unique_ptr<detail::BodySource> body)
    : method_{std::move(method)}, target_{std::move(target)}, headers_{std::move(headers)}, body_{std::move(body)} {
    if (auto parsed = url::parse_target(method_, target_)) {
        form_ = parsed->form;
        raw_path_ = parsed->path;
        raw_query_ = parsed->query;
        authority_ = parsed->authority;
    } else {
        target_error_ = std::move(parsed.error());
        const std::string_view whole{target_};
        const auto question = std::min(whole.find('?'), whole.size());
        raw_path_ = detail::sub(whole, 0, question);
        if (question < whole.size()) {
            raw_query_ = detail::sub(whole, question + 1);
        }
    }
    if (url::needs_unescape(raw_path(), url::Component::path)) {
        if (auto decoded = url::unescape(raw_path(), url::Component::path)) {
            decoded_path_ = std::move(*decoded);
        } else if (!target_error_) {
            target_error_ = std::move(decoded.error());
        }
    }
}

void Request::set_path_value(std::string_view key, std::string_view value) {
    auto& params = route_.url_params;
    for (std::size_t i = params.keys.size(); i-- > 0;) {
        if (params.keys[i] == key) {
            params.values[i] = std::string{value};
            return;
        }
    }
    params.add(key, value);
}

const ParsedQuery& Request::parsed_query() const {
    if (query_) {
        return *query_;
    }
    return query_.emplace(QueryValues::parse(raw_query()));
}

const QueryValues& Request::query() const { return parsed_query().values; }

const std::optional<Error>& Request::query_error() const { return parsed_query().error; }

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

asio::awaitable<ParsedForm> Request::read_form(FormOptions options) {
    ParsedForm out;
    const auto record = [&out](std::optional<Error> error) {
        if (error && !out.error) {
            out.error = std::move(error);
        }
    };

    if (method_ == "POST" || method_ == "PUT" || method_ == "PATCH") {
        if (const auto content_type = headers_.get("Content-Type")) {
            auto type = media_type(*content_type);
            if (!type) {
                record(std::move(type.error()));
            } else if (*type == "application/x-www-form-urlencoded") {
                auto body = co_await read_all(options.max_body);
                if (!body) {
                    record(std::move(body.error()));
                } else {
                    auto parsed = QueryValues::parse(*body, options.query);
                    out.post_form = std::move(parsed.values);
                    record(std::move(parsed.error));
                }
            }
        }
    }

    out.form.append(out.post_form);
    out.form.append(query());
    record(query_error());
    co_return out;
}

}  // namespace ki
