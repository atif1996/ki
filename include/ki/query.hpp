#pragma once

#include <ki/error.hpp>
#include <ki/export.hpp>

#include <charconv>
#include <concepts>
#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ki {

struct QueryOptions {
    // max_params bounds the number of '&'-separated pairs, as Go's default urlmaxqueryparams does; 0 disables
    // the limit.
    std::size_t max_params = 10'000;
};

struct ParsedQuery;

// QueryValues is an ordered list of decoded key/value pairs from a query string or URL-encoded form. Returned
// views stay valid until the QueryValues is modified or destroyed.
class QueryValues {
public:
    // parse decodes raw as Go's url.ParseQuery does: pairs are separated by '&', a pair containing ';' is
    // rejected, and a pair that fails to decode is skipped. Every valid pair is kept, and the first error is
    // reported alongside them.
    [[nodiscard]] KI_EXPORT static ParsedQuery parse(std::string_view raw, QueryOptions options = {});

    [[nodiscard]] KI_EXPORT std::optional<std::string_view> get(std::string_view key) const noexcept;
    [[nodiscard]] KI_EXPORT std::vector<std::string_view> get_all(std::string_view key) const;
    [[nodiscard]] KI_EXPORT bool has(std::string_view key) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
    [[nodiscard]] const std::vector<std::pair<std::string, std::string>>& items() const noexcept { return entries_; }

    // get_as parses the first value for key as an integer, failing with 400 when it is missing, not a number,
    // or out of range for T.
    template <std::integral T>
        requires(!std::same_as<T, bool>)
    [[nodiscard]] std::expected<T, Error> get_as(std::string_view key) const {
        const auto text = get(key);
        if (!text) {
            return std::unexpected(
                Error{.status = Status::bad_request, .message = "missing query parameter \"" + std::string{key} + '"'});
        }
        T value{};
        const char* const last = std::to_address(text->end());
        const auto [end, ec] = std::from_chars(text->data(), last, value);
        if (ec != std::errc{} || end != last) {
            return std::unexpected(
                Error{.status = Status::bad_request, .message = "invalid query parameter \"" + std::string{key} + '"'});
        }
        return value;
    }

    KI_EXPORT void add(std::string_view key, std::string_view value);
    KI_EXPORT void append(const QueryValues& other);

    // encode serializes the pairs sorted by key, keeping the order of values within a key, as Go's
    // url.Values.Encode does.
    [[nodiscard]] KI_EXPORT std::string encode() const;

private:
    std::vector<std::pair<std::string, std::string>> entries_;
};

struct ParsedQuery {
    QueryValues values;
    std::optional<Error> error;
};

}  // namespace ki
