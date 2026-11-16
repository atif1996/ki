#include <ki/headers.hpp>

#include <algorithm>

namespace ki {
namespace {

bool is_token_char(unsigned char c) noexcept {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
        return true;
    }
    return std::string_view{"!#$%&'*+-.^_`|~"}.find(static_cast<char>(c)) != std::string_view::npos;
}

unsigned char ascii_lower(char c) noexcept {
    const auto u = static_cast<unsigned char>(c);
    return (u >= 'A' && u <= 'Z') ? static_cast<unsigned char>(u - 'A' + 'a') : u;
}

bool equals_ignore_case(std::string_view a, std::string_view b) noexcept {
    return std::ranges::equal(a, b, [](char x, char y) { return ascii_lower(x) == ascii_lower(y); });
}

Result validate(std::string_view name, std::string_view value) {
    if (!valid_header_name(name)) {
        return std::unexpected(Error{.message = "invalid header name"});
    }
    if (!valid_header_value(value)) {
        return std::unexpected(Error{.message = "invalid header value"});
    }
    return {};
}

}  // namespace

bool valid_header_name(std::string_view name) noexcept {
    return !name.empty() &&
           std::ranges::all_of(name, [](char c) { return is_token_char(static_cast<unsigned char>(c)); });
}

bool valid_header_value(std::string_view value) noexcept {
    return std::ranges::all_of(value, [](char c) {
        const auto u = static_cast<unsigned char>(c);
        return u == '\t' || (u >= 0x20 && u != 0x7F);
    });
}

Result Headers::add(std::string_view name, std::string_view value) {
    if (auto valid = validate(name, value); !valid) {
        return valid;
    }
    fields_.push_back({.name = std::string{name}, .value = std::string{value}});
    return {};
}

Result Headers::set(std::string_view name, std::string_view value) {
    if (auto valid = validate(name, value); !valid) {
        return valid;
    }
    remove(name);
    fields_.push_back({.name = std::string{name}, .value = std::string{value}});
    return {};
}

std::size_t Headers::remove(std::string_view name) noexcept {
    return std::erase_if(fields_, [name](const Field& f) { return equals_ignore_case(f.name, name); });
}

std::optional<std::string_view> Headers::get(std::string_view name) const noexcept {
    const auto it = std::ranges::find_if(fields_, [name](const Field& f) { return equals_ignore_case(f.name, name); });
    if (it == fields_.end()) {
        return std::nullopt;
    }
    return it->value;
}

std::vector<std::string_view> Headers::get_all(std::string_view name) const {
    std::vector<std::string_view> values;
    for (const auto& field : fields_) {
        if (equals_ignore_case(field.name, name)) {
            values.emplace_back(field.value);
        }
    }
    return values;
}

bool Headers::contains(std::string_view name) const noexcept { return get(name).has_value(); }

}  // namespace ki
