#include <ki/detail/strings.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string_view>

using ki::detail::sub;

static_assert(sub("hello world", 6) == "world");
static_assert(sub("hello world", 0, 5) == "hello");
static_assert(sub("hello", 5).empty());
static_assert(sub("hello", 100).empty());
static_assert(sub("hello", 2, 100) == "llo");
static_assert(sub("", 0).empty());
static_assert(noexcept(sub(std::string_view{}, 1)));

TEST_CASE("sub clamps like views::drop and views::take instead of throwing") {
    constexpr std::string_view text = "hello";
    REQUIRE(sub(text, 1, 3) == text.substr(1, 3));
    REQUIRE(sub(text, 99, 1).empty());
}
