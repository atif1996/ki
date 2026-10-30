#pragma once

#include <ki/error.hpp>
#include <ki/export.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ki {

// valid_header_name reports whether name is a non-empty RFC 9110 token.
KI_EXPORT bool valid_header_name(std::string_view name) noexcept;

// valid_header_value reports whether value holds only visible characters, spaces, tabs and obs-text. CR, LF,
// NUL and the other control characters are rejected because they allow header injection.
KI_EXPORT bool valid_header_value(std::string_view value) noexcept;

// Headers is an ordered list of header fields with case-insensitive names. Every mutation validates its input
// and leaves the headers unchanged when it fails.
class Headers {
public:
    struct Field {
        std::string name;
        std::string value;
    };

    [[nodiscard]] KI_EXPORT Result add(std::string_view name, std::string_view value);
    [[nodiscard]] KI_EXPORT Result set(std::string_view name, std::string_view value);
    KI_EXPORT std::size_t remove(std::string_view name) noexcept;

    [[nodiscard]] KI_EXPORT std::optional<std::string_view> get(std::string_view name) const noexcept;
    [[nodiscard]] KI_EXPORT std::vector<std::string_view> get_all(std::string_view name) const;
    [[nodiscard]] KI_EXPORT bool contains(std::string_view name) const noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return fields_.size(); }
    [[nodiscard]] bool empty() const noexcept { return fields_.empty(); }
    [[nodiscard]] auto begin() const noexcept { return fields_.begin(); }
    [[nodiscard]] auto end() const noexcept { return fields_.end(); }

private:
    std::vector<Field> fields_;
};

}  // namespace ki
