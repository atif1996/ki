// T1.27

#include <ki/handler.hpp>
#include <ki/test/recorder.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace {

using Strings = std::vector<std::string_view>;

// form runs read_form through the recorder, so the body is read the way a handler would read it.
ki::ParsedForm form(std::string method, std::string target, std::string body,
                    std::string_view content_type = "application/x-www-form-urlencoded", ki::FormOptions options = {}) {
    ki::Headers headers;
    if (!content_type.empty()) {
        REQUIRE(headers.set("Content-Type", content_type));
    }
    auto request = ki::test::make_request(std::move(method), std::move(target), std::move(headers), std::move(body));
    ki::ParsedForm out;
    (void)ki::test::Recorder::run(
        [&](ki::Request& req, ki::Response&) -> asio::awaitable<ki::Result> {
            out = co_await req.read_form(options);
            co_return ki::Result{};
        },
        request);
    return out;
}

}  // namespace

// go: TestParseFormQuery
TEST_CASE("form holds body values before query values, post_form only body values") {
    const auto parsed =
        form("POST", "/search?q=foo&q=bar&both=x&prio=1&orphan=nope&empty=not",
             "z=post&both=y&prio=2&=nokey&orphan&empty=&", "application/x-www-form-urlencoded; param=value");
    REQUIRE_FALSE(parsed.error);
    REQUIRE(parsed.form.get("q") == "foo");
    REQUIRE(parsed.form.get("z") == "post");
    REQUIRE_FALSE(parsed.post_form.has("q"));
    REQUIRE(parsed.post_form.get("z") == "post");
    REQUIRE(parsed.form.get_all("q") == Strings{"foo", "bar"});
    REQUIRE(parsed.form.get_all("both") == Strings{"y", "x"});
    REQUIRE(parsed.form.get("prio") == "2");
    REQUIRE(parsed.form.get_all("orphan") == Strings{"", "nope"});
    REQUIRE(parsed.form.get_all("empty") == Strings{"", "not"});
}

// go: TestParseFormQueryMethods
TEST_CASE("only POST, PUT and PATCH bodies are parsed") {
    for (const auto* method : {"POST", "PATCH", "PUT"}) {
        INFO(method);
        REQUIRE(form(method, "/search", "foo=bar").form.get("foo") == "bar");
    }
    REQUIRE_FALSE(form("FOO", "/search", "foo=bar").form.has("foo"));
    REQUIRE_FALSE(form("GET", "/search", "foo=bar").form.has("foo"));
}

// go: TestParseFormUnknownContentType
TEST_CASE("other content types parse the query only, without error") {
    for (const std::string_view type : {"text/plain", "", "application/unknown"}) {
        INFO(type);
        const auto parsed = form("POST", "/?a=1", "body", type);
        REQUIRE_FALSE(parsed.error);
        REQUIRE(parsed.form.get("a") == "1");
        REQUIRE(parsed.post_form.empty());
    }
    const auto bad = form("POST", "/", "body", "text/plain; boundary=");
    REQUIRE(bad.error);
    REQUIRE(bad.error->status == ki::Status::bad_request);
}

// go: TestParseFormInitializeOnError
TEST_CASE("a parse error still yields initialised forms") {
    const auto parsed = form("POST", "/?q=foo", "a=%zz&b=2");
    REQUIRE(parsed.error);
    REQUIRE(parsed.post_form.get("b") == "2");
    REQUIRE(parsed.form.get("q") == "foo");
}

TEST_CASE("a form body over the limit is rejected") {
    const auto parsed =
        form("POST", "/", std::string(100, 'a'), "application/x-www-form-urlencoded", {.max_body = 10, .query = {}});
    REQUIRE(parsed.error);
    REQUIRE(parsed.error->status == ki::Status::content_too_large);
    REQUIRE(parsed.post_form.empty());
}

TEST_CASE("the content type is matched case-insensitively") {
    REQUIRE(form("POST", "/", "k=v", "Application/X-WWW-Form-URLEncoded").form.get("k") == "v");
}
