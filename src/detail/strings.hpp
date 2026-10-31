#pragma once

#include <algorithm>
#include <cstddef>
#include <string_view>

namespace ki::detail {

// sub is std::string_view::substr without exceptions: a position or count past the end is clamped, as
// std::views::drop and std::views::take do. Those would serve, but they are not noexcept in libc++ or
// libstdc++: the standard defines them through std::min and the string_view(first, last) constructor,
// and declares neither noexcept. Revisit if the standard libraries strengthen them; the libc++ request is
// https://github.com/llvm/llvm-project/issues/237594.
[[nodiscard]] constexpr std::string_view sub(std::string_view text, std::size_t pos,
                                             std::size_t count = std::string_view::npos) noexcept {
    text.remove_prefix(std::min(pos, text.size()));
    text.remove_suffix(text.size() - std::min(count, text.size()));
    return text;
}

}  // namespace ki::detail
