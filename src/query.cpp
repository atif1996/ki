#include <ki/query.hpp>
#include <ki/url.hpp>

#include "detail/strings.hpp"

#include <algorithm>
#include <numeric>

namespace ki {

ParsedQuery QueryValues::parse(std::string_view raw, QueryOptions options) {
    ParsedQuery out;
    const auto pairs = static_cast<std::size_t>(std::ranges::count(raw, '&')) + 1;
    if (options.max_params != 0 && pairs > options.max_params) {
        out.error = Error{.status = Status::bad_request, .message = "too many query parameters"};
        return out;
    }
    const auto fail = [&out](Error error) {
        if (!out.error) {
            out.error = std::move(error);
        }
    };

    out.values.entries_.reserve(pairs);
    std::string_view rest = raw;
    for (bool more = true; more;) {
        const auto amp = rest.find('&');
        more = amp != std::string_view::npos;
        const auto pair = detail::sub(rest, 0, std::min(amp, rest.size()));
        rest = more ? detail::sub(rest, amp + 1) : std::string_view{};

        if (pair.find(';') != std::string_view::npos) {
            fail(Error{.status = Status::bad_request, .message = "invalid semicolon separator in query"});
            continue;
        }
        if (pair.empty()) {
            continue;
        }
        const auto equals = std::min(pair.find('='), pair.size());
        auto key = url::unescape(detail::sub(pair, 0, equals), url::Component::query);
        if (!key) {
            fail(std::move(key.error()));
            continue;
        }
        auto value = url::unescape(detail::sub(pair, std::min(equals + 1, pair.size())), url::Component::query);
        if (!value) {
            fail(std::move(value.error()));
            continue;
        }
        out.values.entries_.emplace_back(std::move(*key), std::move(*value));
    }
    return out;
}

std::optional<std::string_view> QueryValues::get(std::string_view key) const noexcept {
    const auto it = std::ranges::find(entries_, key, [](const auto& entry) { return std::string_view{entry.first}; });
    if (it == entries_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<std::string_view> QueryValues::get_all(std::string_view key) const {
    std::vector<std::string_view> out;
    for (const auto& [k, v] : entries_) {
        if (k == key) {
            out.emplace_back(v);
        }
    }
    return out;
}

bool QueryValues::has(std::string_view key) const noexcept { return get(key).has_value(); }

void QueryValues::add(std::string_view key, std::string_view value) { entries_.emplace_back(key, value); }

void QueryValues::append(const QueryValues& other) {
    entries_.insert(entries_.end(), other.entries_.begin(), other.entries_.end());
}

std::string QueryValues::encode() const {
    std::vector<std::size_t> order(entries_.size());
    std::ranges::iota(order, std::size_t{0});
    std::ranges::stable_sort(order, {}, [this](std::size_t i) { return std::string_view{entries_[i].first}; });

    std::string out;
    for (const auto i : order) {
        if (!out.empty()) {
            out.push_back('&');
        }
        out += url::query_escape(entries_[i].first);
        out.push_back('=');
        out += url::query_escape(entries_[i].second);
    }
    return out;
}

}  // namespace ki
