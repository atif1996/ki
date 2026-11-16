// T1.21, T1.26

#include <ki/request.hpp>
#include <ki/url.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

using ki::url::Component;
using ki::url::TargetForm;

namespace {

struct EscapeCase {
    std::string_view in;
    std::string_view out;
};

}  // namespace

// go: TestUnescape (unescapeTests, which decode as QueryUnescape)
TEST_CASE("unescape decodes escapes and '+' in queries") {
    const std::vector<EscapeCase> ok = {
        {"", ""}, {"abc", "abc"}, {"1%41", "1A"}, {"1%41%42%43", "1ABC"}, {"%4a", "J"},
        {"%6F", "o"}, {"a+b", "a b"}, {"a%20b", "a b"},
    };
    for (const auto& c : ok) {
        INFO(c.in);
        REQUIRE(ki::url::unescape(c.in, Component::query) == c.out);
    }
    for (const std::string_view bad : {"%", "%a", "%1", "123%45%6", "%zzzzz"}) {
        INFO(bad);
        REQUIRE_FALSE(ki::url::unescape(bad, Component::query).has_value());
    }
}

// go: TestUnescape (PathUnescape keeps '+')
TEST_CASE("unescape keeps '+' in paths") {
    REQUIRE(ki::url::unescape("a+b", Component::path) == "a+b");
    REQUIRE(ki::url::unescape("a%2Fb", Component::path) == "a/b");
}

// go: TestQueryEscape (queryEscapeTests)
TEST_CASE("query_escape") {
    const std::vector<EscapeCase> cases = {
        {"", ""},
        {"abc", "abc"},
        {"one two", "one+two"},
        {"10%", "10%25"},
        {" ?&=#+%!<>#\"{}|\\^[]`\xE2\x98\xBA\t:/@$'()*,;",
         "+%3F%26%3D%23%2B%25%21%3C%3E%23%22%7B%7D%7C%5C%5E%5B%5D%60%E2%98%BA%09%3A%2F%40%24%27%28%29%2A%2C%3B"},
    };
    for (const auto& c : cases) {
        INFO(c.in);
        REQUIRE(ki::url::query_escape(c.in) == c.out);
        REQUIRE(ki::url::unescape(c.out, Component::query) == c.in);
    }
}

// go: TestPathEscape (pathEscapeTests)
TEST_CASE("path_escape") {
    const std::vector<EscapeCase> cases = {
        {"", ""},
        {"abc", "abc"},
        {"abc+def", "abc+def"},
        {"a/b", "a%2Fb"},
        {"one two", "one%20two"},
        {"10%", "10%25"},
        {" ?&=#+%!<>#\"{}|\\^[]`\xE2\x98\xBA\t:/@$'()*,;",
         "%20%3F&=%23+%25%21%3C%3E%23%22%7B%7D%7C%5C%5E%5B%5D%60%E2%98%BA%09:%2F@$%27%28%29%2A%2C%3B"},
    };
    for (const auto& c : cases) {
        INFO(c.in);
        REQUIRE(ki::url::path_escape(c.in) == c.out);
        REQUIRE(ki::url::unescape(c.out, Component::path) == c.in);
    }
}

TEST_CASE("escaping round-trips every byte value") {
    std::string all;
    for (int b = 0; b < 256; ++b) {
        all.push_back(static_cast<char>(b));
    }
    REQUIRE(ki::url::unescape(ki::url::query_escape(all), Component::query) == all);
    REQUIRE(ki::url::unescape(ki::url::path_escape(all), Component::path) == all);
}

// go: TestParseRequestURI (parseRequestURLTests)
TEST_CASE("request targets accepted and rejected as Go's ParseRequestURI does") {
    const std::vector<std::string_view> valid = {
        "http://foo.com",
        "http://foo.com/",
        "http://foo.com/path",
        "/",
        "//not.a.user@not.a.host/just/a/path",
        "//not.a.user@%66%6f%6f.com/just/a/path/also",
        "http://192.168.0.1/",
        "http://192.168.0.1:8080/",
        "http://[fe80::1]/",
        "http://[fe80::1]:8080/",
        "http://[fe80::1%25en0]/",
        "http://[fe80::1%25en0]:8080/",
        "http://[fe80::1%25%65%6e%301-._~]/",
        "http://[fe80::1%25%65%6e%301-._~]:8080/",
        "https://[1:2:3:4:5:6:7:8]",
        "https://[2001:db8::a:b:c:d]",
        "https://[fe80::1%25eth0]",
        "https://[fe80::abc:def%254]",
        "https://[2001:db8::1]/path",
        "https://[fe80::1%25eth0]/path?query=1",
        "https://[::ffff:192.0.2.1]",
    };
    const std::vector<std::string_view> invalid = {
        "foo.html",
        "../dir/",
        " http://foo.com",
        "http://192.168.0.%31/",
        "http://192.168.0.%31:8080/",
        "http://[fe80::%31]/",
        "http://[fe80::%31]:8080/",
        "http://[fe80::%31%25en0]/",
        "http://[fe80::%31%25en0]:8080/",
        "http://[fe80::1%en0]/",
        "http://[fe80::1%en0]:8080/",
        "https://[:1] ",
        "https://[1:2:3:4:5:6:7:8:9]",
        "https://[1::1::1]",
        "https://[1:2:3:]",
        "https://[ffff::127.0.0.4000]",
        "https://[0:0::test.com]:80",
        "https://[2001:db8::test.com]",
        "https://[test.com]",
        "https://1:2:3:4:5:6:7:8",
        "https://1:2:3:4:5:6:7:8:80",
        "https://example.com:80:",
    };
    for (const auto target : valid) {
        INFO(target);
        REQUIRE(ki::url::parse_target("GET", target).has_value());
    }
    for (const auto target : invalid) {
        INFO(target);
        REQUIRE_FALSE(ki::url::parse_target("GET", target).has_value());
    }
}

// go: TestRejectControlCharacters
TEST_CASE("control characters are rejected but non-ASCII bytes are not") {
    for (const std::string_view target : {"http://foo.com/?foo\nbar", "http\r://foo.com/", "http://foo\x7f.com/",
                                          "/a\tb", "/a b"}) {
        INFO(target);
        REQUIRE_FALSE(ki::url::parse_target("GET", target).has_value());
    }
    REQUIRE(ki::url::parse_target("GET", "http://foo.com/ctl\x80").has_value());
}

TEST_CASE("the four request-target forms") {
    const auto origin = ki::url::parse_target("GET", "/a/b?x=1");
    REQUIRE(origin->form == TargetForm::origin);
    REQUIRE(origin->path == "/a/b");
    REQUIRE(origin->query == "x=1");

    constexpr std::string_view absolute_target = "http://example.com:8080/p?q";
    const auto absolute = ki::url::parse_target("GET", absolute_target);
    REQUIRE(absolute->form == TargetForm::absolute);
    REQUIRE(absolute->scheme == "http");
    REQUIRE(absolute->authority == "example.com:8080");
    REQUIRE(absolute->path == "/p");
    REQUIRE(absolute->query == "q");

    REQUIRE(ki::url::parse_target("CONNECT", "example.com:443")->form == TargetForm::authority);
    REQUIRE_FALSE(ki::url::parse_target("CONNECT", "example.com").has_value());
    REQUIRE_FALSE(ki::url::parse_target("GET", "example.com:443").has_value());

    REQUIRE(ki::url::parse_target("OPTIONS", "*")->form == TargetForm::asterisk);
    REQUIRE_FALSE(ki::url::parse_target("GET", "*").has_value());
}

TEST_CASE("userinfo in an absolute-form authority is rejected") {
    REQUIRE_FALSE(ki::url::parse_target("GET", "http://user:pass@example.com/").has_value());
}

TEST_CASE("Request exposes the decoded and raw path, query and target errors") {
    const ki::Request request{"GET", "/caf%C3%A9/a%2Fb?x=1"};
    REQUIRE_FALSE(request.target_error());
    REQUIRE(request.path() == "/caf\xC3\xA9/a/b");
    REQUIRE(request.raw_path() == "/caf%C3%A9/a%2Fb");
    REQUIRE(request.raw_query() == "x=1");

    const ki::Request bad_escape{"GET", "/a%zz"};
    REQUIRE(bad_escape.target_error());
    REQUIRE(bad_escape.target_error()->status == ki::Status::bad_request);

    const ki::Request bad_target{"GET", "/a b"};
    REQUIRE(bad_target.target_error());

    const ki::Request proxy{"GET", "http://example.com/p?q=1"};
    REQUIRE(proxy.target_form() == TargetForm::absolute);
    REQUIRE(proxy.authority() == "example.com");
    REQUIRE(proxy.path() == "/p");
}
