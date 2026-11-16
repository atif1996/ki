// T1.22 - T1.25

#include <ki/query.hpp>
#include <ki/request.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Values = std::map<std::string, std::vector<std::string>>;

Values as_map(const ki::QueryValues& values) {
    Values out;
    for (const auto& [key, value] : values.items()) {
        out[std::string{key}].emplace_back(value);
    }
    return out;
}

struct ParseCase {
    std::string_view query;
    Values out;
    bool ok;
};

}  // namespace

// go: TestParseQuery (parseTests)
TEST_CASE("parse matches Go's ParseQuery, keeping valid pairs alongside the first error") {
    const std::vector<ParseCase> cases = {
        {"a=1", {{"a", {"1"}}}, true},
        {"a=1&b=2", {{"a", {"1"}}, {"b", {"2"}}}, true},
        {"a=1&a=2&a=banana", {{"a", {"1", "2", "banana"}}}, true},
        {"ascii=%3Ckey%3A+0x90%3E", {{"ascii", {"<key: 0x90>"}}}, true},
        {"a=1;b=2", {}, false},
        {"a;b=1", {}, false},
        {"a=%3B", {{"a", {";"}}}, true},
        {"a%3Bb=1", {{"a;b", {"1"}}}, true},
        {"a=1&a=2;a=banana", {{"a", {"1"}}}, false},
        {"a;b&c=1", {{"c", {"1"}}}, false},
        {"a=1&b=2;a=3&c=4", {{"a", {"1"}}, {"c", {"4"}}}, false},
        {"a=1&b=2;c=3", {{"a", {"1"}}}, false},
        {";", {}, false},
        {"a=1;", {}, false},
        {"a=1&;", {{"a", {"1"}}}, false},
        {";a=1&b=2", {{"b", {"2"}}}, false},
        {"a=1&b=2;", {{"a", {"1"}}}, false},
    };
    for (const auto& c : cases) {
        INFO(c.query);
        const auto parsed = ki::QueryValues::parse(c.query);
        REQUIRE(parsed.error.has_value() == !c.ok);
        REQUIRE(as_map(parsed.values) == c.out);
    }
}

// T1.22
TEST_CASE("repeated keys keep their order and keys without '=' get an empty value") {
    const auto parsed = ki::QueryValues::parse("a=1&a=2&b=&c");
    REQUIRE_FALSE(parsed.error);
    const auto& values = parsed.values;
    REQUIRE(values.get_all("a") == std::vector<std::string_view>{"1", "2"});
    REQUIRE(values.get("b") == "");
    REQUIRE(values.get("c") == "");
    REQUIRE(values.has("c"));
    REQUIRE_FALSE(values.has("d"));
    REQUIRE(ki::QueryValues::parse("").values.empty());
}

// T1.23
TEST_CASE("an invalid escape skips only its pair") {
    const auto parsed = ki::QueryValues::parse("a=1&b=%zz&c=3");
    REQUIRE(parsed.error);
    REQUIRE(parsed.error->status == ki::Status::bad_request);
    REQUIRE(parsed.values.get("a") == "1");
    REQUIRE_FALSE(parsed.values.has("b"));
    REQUIRE(parsed.values.get("c") == "3");
}

// go: TestParseQueryLimits (T1.24)
TEST_CASE("the number of parameters is limited") {
    const auto query_with = [](int params) {
        std::string query;
        for (int i = 0; i < params; ++i) {
            if (i > 0) {
                query += '&';
            }
            query += 'p' + std::to_string(i);
        }
        return query;
    };
    REQUIRE(ki::QueryValues::parse(query_with(10)).values.size() == 10);
    REQUIRE(ki::QueryValues::parse(query_with(10'000)).values.size() == 10'000);

    const auto over = ki::QueryValues::parse(query_with(10'001));
    REQUIRE(over.error);
    REQUIRE(over.values.empty());

    REQUIRE(ki::QueryValues::parse(query_with(10), {.max_params = 9}).error);
    REQUIRE(ki::QueryValues::parse(query_with(10'001), {.max_params = 0}).values.size() == 10'001);
}

// T1.25
TEST_CASE("typed access reports missing, malformed and out-of-range values") {
    const auto values = ki::QueryValues::parse("page=3&big=300&neg=-2&word=abc&partial=12x").values;
    REQUIRE(values.get_as<int>("page") == 3);
    REQUIRE(values.get_as<std::int64_t>("neg") == -2);

    const auto missing = values.get_as<int>("absent");
    REQUIRE_FALSE(missing);
    REQUIRE(missing.error().status == ki::Status::bad_request);
    REQUIRE_FALSE(values.get_as<int>("word"));
    REQUIRE_FALSE(values.get_as<int>("partial"));
    REQUIRE_FALSE(values.get_as<std::uint8_t>("big"));
    REQUIRE_FALSE(values.get_as<unsigned>("neg"));
}

TEST_CASE("values survive moving and copying the container") {
    auto parsed = ki::QueryValues::parse("short=a&decoded=%41%42");
    ki::QueryValues moved = std::move(parsed.values);
    const ki::QueryValues copied = moved;
    REQUIRE(moved.get("short") == "a");
    REQUIRE(copied.get("decoded") == "AB");
}

// go: TestEncodeQuery (encodeQueryTests)
TEST_CASE("encode sorts keys and keeps value order") {
    REQUIRE(ki::QueryValues{}.encode().empty());

    ki::QueryValues values;
    values.add("q", "puppies");
    values.add("oe", "utf8");
    REQUIRE(values.encode() == "oe=utf8&q=puppies");

    ki::QueryValues repeated;
    repeated.add("q", "dogs");
    repeated.add("q", "&");
    repeated.add("q", "7");
    REQUIRE(repeated.encode() == "q=dogs&q=%26&q=7");

    ki::QueryValues interleaved;
    for (const auto* value : {"c1", "a1", "b1", "c2", "a2", "b2", "c3", "a3", "b3"}) {
        interleaved.add(std::string_view{value, 1}, value);
    }
    REQUIRE(interleaved.encode() == "a=a1&a=a2&a=a3&b=b1&b=b2&b=b3&c=c1&c=c2&c=c3");
}

TEST_CASE("Request parses its query on first use") {
    const ki::Request request{"GET", "/search?q=foo&q=bar&x=%zz"};
    REQUIRE(request.query().get("q") == "foo");
    REQUIRE(request.query().get_all("q").size() == 2);
    REQUIRE(request.query_error());
}
