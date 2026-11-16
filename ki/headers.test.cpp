#include <ki/headers.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <string>
#include <string_view>

using namespace std::string_view_literals;

TEST_CASE("header names are matched case-insensitively") {
    ki::Headers headers;
    REQUIRE(headers.add("Content-Type", "text/plain"));
    REQUIRE(headers.get("content-type") == "text/plain");
    REQUIRE(headers.contains("CONTENT-TYPE"));
    REQUIRE_FALSE(headers.get("Content-Length").has_value());
}

TEST_CASE("add keeps every value and set replaces them") {
    ki::Headers headers;
    REQUIRE(headers.add("Accept", "a"));
    REQUIRE(headers.add("accept", "b"));
    REQUIRE(headers.get_all("ACCEPT") == std::vector{"a"sv, "b"sv});
    REQUIRE(headers.get("Accept") == "a");

    REQUIRE(headers.set("Accept", "c"));
    REQUIRE(headers.get_all("accept") == std::vector{"c"sv});
    REQUIRE(headers.remove("accept") == 1);
    REQUIRE(headers.empty());
}

// T1.4
TEST_CASE("invalid header names are rejected and leave the headers unchanged") {
    const std::string name =
        GENERATE(std::string{}, std::string{"Bad Name"}, std::string{"Bad:Name"}, std::string{"Bad\r\nName"},
                 std::string{"Bad\0Name", 8}, std::string{"Bad(Name)"}, std::string{"\xC3\xA9t\xC3\xA9"});
    ki::Headers headers;
    REQUIRE(headers.add("Kept", "1"));
    REQUIRE_FALSE(headers.add(name, "v"));
    REQUIRE_FALSE(headers.set(name, "v"));
    REQUIRE(headers.size() == 1);
}

// T1.4
TEST_CASE("header values with CR, LF, NUL or other control characters are rejected") {
    const std::string value = GENERATE(std::string{"a\r\nInjected: yes"}, std::string{"a\nb"}, std::string{"a\rb"},
                                       std::string{"a\0b", 3}, std::string{"a\x01z"}, std::string{"a\x7Fz"});
    ki::Headers headers;
    REQUIRE(headers.add("Kept", "1"));
    REQUIRE_FALSE(headers.add("X-Test", value));
    REQUIRE_FALSE(headers.set("Kept", value));
    REQUIRE(headers.get("Kept") == "1");
    REQUIRE(headers.size() == 1);
}

TEST_CASE("tabs, spaces and obs-text are valid in header values") {
    ki::Headers headers;
    REQUIRE(headers.add("X-Test", "a\tb c"));
    REQUIRE(headers.add("X-Test", "caf\xC3\xA9"));
    REQUIRE(headers.add("X-Empty", ""));
    REQUIRE(headers.size() == 3);
}
