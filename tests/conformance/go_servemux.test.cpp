// T2.16, T2.17, T2.19: Go ServeMux behaviour that ki adopts on top of chi's router.

#include "router_support.hpp"

#include <ki/router.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using ki::Router;
using ki::Status;
using ki::testing::named;
using ki::testing::serve;

namespace {

std::string only_error(const Router& r) {
    const auto errors = r.errors();
    REQUIRE(errors.size() == 1);
    return errors[0].message;
}

}  // namespace

// T2.16 (go: TestParsePattern)
TEST_CASE("{name...} is a named catch-all") {
    Router r;
    r.get("/{a}/foo/{rest...}", named("rest"));
    REQUIRE(r.errors().empty());
    const auto hit = serve(r, "GET", "/x/foo/a/b/c");
    REQUIRE(hit.name == "rest");
    REQUIRE(hit.keys == std::vector<std::string>{"a", "rest"});
    REQUIRE(hit.values == std::vector<std::string>{"x", "a/b/c"});
    REQUIRE(r.find("GET", "/x/foo/") == "/{a}/foo/{rest...}");
}

// T2.16 (go: TestParsePatternError, the cases that apply to ki's grammar). chi's grammar allows a
// parameter to share a segment with literals ("/{w}x"), so Go's "bad wildcard segment" cases for that do
// not apply; "{$}" is not supported, so it is a bad name.
TEST_CASE("pattern errors report the offset and reason") {
    struct Case {
        const char* pattern;
        const char* offset;
        const char* reason;
    };
    for (const auto& c : {Case{"/{wx", "offset 1", "'}' is missing"},
                          Case{"/a/{/}/c", "offset 3", "bad wildcard name"},
                          Case{"/a/{%61}/c", "offset 3", "bad wildcard name"},
                          Case{"/{a$}", "offset 1", "bad wildcard name"},
                          Case{"/{}", "offset 1", "empty wildcard"},
                          Case{"/{...}", "offset 1", "empty wildcard"},
                          Case{"/{$...}", "offset 1", "bad wildcard name"},
                          Case{"/{$}", "offset 1", "bad wildcard name"},
                          Case{"/{a...}/", "offset 1", "not at end"},
                          Case{"/{a...}/x", "offset 1", "not at end"},
                          Case{"/a/{x}/b/{x...}", "offset 9", "duplicate"}}) {
        INFO(c.pattern);
        Router r;
        r.get(c.pattern, named("never"));
        const auto message = only_error(r);
        REQUIRE(message.find(c.offset) != std::string::npos);
        REQUIRE(message.find(c.reason) != std::string::npos);
    }
    for (const auto* pattern : {"", "{a}/b", "A=B /"}) {
        INFO(pattern);
        Router r;
        r.handle(pattern, named("never"));
        REQUIRE(r.errors().size() == 1);
    }
}

// T2.17 (go: TestRegisterConflict, TestConflictsWith, the decidable cases)
TEST_CASE("duplicate and same-shape registrations are rejected with both patterns named") {
    Router r;
    r.get("/a/{x}", named("first"));
    r.get("/a/{y}", named("second"));
    r.handle("GET /a/{z}", named("third"));
    const auto errors = r.errors();
    REQUIRE(errors.size() == 2);
    REQUIRE(errors[0].message.find("/a/{y}") != std::string::npos);
    REQUIRE(errors[0].message.find("/a/{x}") != std::string::npos);
    REQUIRE(serve(r, "GET", "/a/1").name == "first");

    Router any;
    any.handle("/b", named("any"));
    any.handle("/b", named("again"));
    REQUIRE(any.errors().size() == 1);
}

// T2.17: a method-specific pattern is more specific than one for every method, as in Go's ServeMux.
TEST_CASE("a method-specific route coexists with an every-method route on the same pattern") {
    Router before;
    before.handle("/c", named("any"));
    before.get("/c", named("get"));
    Router after;
    after.get("/c", named("get"));
    after.handle("/c", named("any"));
    for (const auto* r : {&before, &after}) {
        REQUIRE(r->errors().empty());
        REQUIRE(serve(*r, "GET", "/c").name == "get");
        REQUIRE(serve(*r, "POST", "/c").name == "any");
    }
}

TEST_CASE("different methods and different regexes on one shape do not conflict") {
    Router r;
    r.get("/d/{id}", named("get"));
    r.del("/d/{slug}", named("delete"));
    r.get("/e/{n:[0-9]+}", named("number"));
    r.get("/e/{w:[a-z]+}", named("word"));
    REQUIRE(r.errors().empty());
    REQUIRE(serve(r, "DELETE", "/d/x").values == std::vector<std::string>{"x"});
    REQUIRE(serve(r, "GET", "/e/42").name == "number");
    REQUIRE(serve(r, "GET", "/e/abc").name == "word");
}

TEST_CASE("an explicit route may override a mount's trailing-slash stub but a mount may not override a route") {
    Router sub;
    sub.get("/", named("sub"));
    Router r;
    r.mount("/m", sub);
    r.get("/m/", named("override"));
    REQUIRE(r.errors().empty());
    REQUIRE(serve(r, "GET", "/m/").name == "override");
    REQUIRE(serve(r, "GET", "/m").name == "sub");

    Router taken;
    taken.get("/n", named("route"));
    taken.mount("/n", sub);
    REQUIRE(taken.errors().size() == 1);
    REQUIRE(serve(taken, "GET", "/n").name == "route");
    REQUIRE(serve(taken, "GET", "/n/").status == Status::not_found);
}

// T2.19 (go: TestEscapedPathsAndPatterns)
TEST_CASE("paths and patterns match after decoding each segment") {
    Router r;
    r.get("/a", named("a"));
    r.get("/%62", named("b"));
    r.get("/%7B/%7D", named("braces"));
    r.get("/%x", named("bad-escape"));
    REQUIRE(r.errors().empty());
    for (const auto* path : {"/a", "/%61"}) {
        INFO(path);
        REQUIRE(serve(r, "GET", path).name == "a");
    }
    for (const auto* path : {"/b", "/%62"}) {
        INFO(path);
        REQUIRE(serve(r, "GET", path).name == "b");
    }
    for (const auto* path : {"/{/}", "/%7b/}", "/{/%7d", "/%7B/%7D"}) {
        INFO(path);
        REQUIRE(serve(r, "GET", path).name == "braces");
    }
    REQUIRE(serve(r, "GET", "/%25x").name == "bad-escape");
}

TEST_CASE("an encoded slash stays inside its segment and parameters are decoded") {
    Router r;
    r.get("/files/{name}", named("file"));
    r.get("/files/{dir}/{name}", named("nested"));
    r.get("/id/{id:[a-z]+}", named("regex"));
    const auto hit = serve(r, "GET", "/files/a%2Fb.txt");
    REQUIRE(hit.name == "file");
    REQUIRE(hit.values == std::vector<std::string>{"a/b.txt"});
    REQUIRE(serve(r, "GET", "/files/a/b.txt").name == "nested");
    REQUIRE(serve(r, "GET", "/files/%25").values == std::vector<std::string>{"%"});
    // A regex sees the decoded value.
    REQUIRE(serve(r, "GET", "/id/%61bc").values == std::vector<std::string>{"abc"});
}

TEST_CASE("truncated escapes at the end of a path are routed safely") {
    Router r;
    r.get("/{x}", named("x"));
    for (const auto* path : {"/%", "/%4", "/a%", "/a%4"}) {
        INFO(path);
        const auto hit = serve(r, "GET", path);
        REQUIRE(hit.status != Status::internal_server_error);
    }
    REQUIRE(r.find("GET", "/%4") == "/{x}");
}

#include <ki/redirect.hpp>
#include <ki/strip_prefix.hpp>
#include <ki/test/recorder.hpp>

namespace {

using ki::Handler;
using ki::Request;
using ki::Response;
using ki::Result;

Handler write_query() {
    return [](Request& req, Response& res) -> asio::awaitable<Result> { co_return co_await res.write(req.raw_query()); };
}

ki::test::Recorded fetch(const ki::Handler& handler, std::string method, std::string target) {
    auto request = ki::test::make_request(std::move(method), std::move(target));
    return ki::test::Recorder::run(handler, request);
}

// follow makes a request and follows at most one 307 redirect, as Go's client does in these tests.
ki::test::Recorded follow(const ki::Handler& handler, const std::string& method, const std::string& target) {
    auto out = fetch(handler, method, target);
    if (out.status == Status::temporary_redirect) {
        out = fetch(handler, method, std::string{out.headers.get("Location").value_or("")});
    }
    return out;
}

}  // namespace

// T2.18 (go: TestServeWithSlashRedirectKeepsQueryString). Go's cases 8 and 9 rely on ServeMux cleaning
// "/foo/.." paths; ki leaves that to the CleanPath middleware (Phase 3).
TEST_CASE("subtree redirects the bare path with 307 and keeps the query") {
    Router r;
    r.handle("/testOne", write_query());
    r.subtree("/testTwo/", write_query());
    r.handle("/testThree", write_query());
    r.subtree("/testThree/", [](Request& req, Response& res) -> asio::awaitable<Result> {
        co_return co_await res.write(std::string{req.raw_query()} + ":bar");
    });
    REQUIRE(r.errors().empty());

    struct Case {
        const char* path;
        const char* method;
        const char* want;
    };
    for (const auto& c : {Case{"/testOne?this=that", "GET", "this=that"}, Case{"/testTwo?foo=bar", "GET", "foo=bar"},
                          Case{"/testTwo?a=1&b=2&a=3", "GET", "a=1&b=2&a=3"}, Case{"/testTwo?", "GET", ""},
                          Case{"/testThree?foo", "GET", "foo"}, Case{"/testThree/?foo", "GET", "foo:bar"},
                          Case{"/testThree?foo", "CONNECT", "foo"}, Case{"/testThree/?foo", "CONNECT", "foo:bar"}}) {
        INFO(c.method << " " << c.path);
        const auto out = follow(r, c.method, c.path);
        REQUIRE(out.status == Status::ok);
        REQUIRE(out.body == c.want);
    }

    const auto redirected = fetch(r, "GET", "/testTwo?x=1");
    REQUIRE(redirected.status == Status::temporary_redirect);
    REQUIRE(redirected.headers.get("Location") == "/testTwo/?x=1");
    REQUIRE(fetch(r, "GET", "/testTwo/deeper/still").status == Status::ok);
}

// T2.18 (go: TestServeMuxHandlerRedirectPost)
TEST_CASE("a method-specific subtree redirects that method and keeps it") {
    Router r;
    r.subtree("POST /test/", named("post"));
    const auto out = follow(r, "POST", "/test");
    REQUIRE(out.status == Status::ok);
    REQUIRE(out.body.starts_with("post"));
}

// T2.18 (go: TestMuxNoSlash405WithTrailingSlash, TestMuxNoSlashRedirectWithTrailingSlash)
TEST_CASE("a subtree with a parameter does not match the root") {
    Router get;
    get.subtree("GET /{x}/", named("ok"));
    REQUIRE(fetch(get, "GET", "/").status == Status::not_found);
    Router any;
    any.subtree("/{x}/", named("ok"));
    REQUIRE(fetch(any, "GET", "/").status == Status::not_found);
    REQUIRE(fetch(any, "GET", "/a/b").status == Status::ok);
}

TEST_CASE("a subtree pattern must end in a slash, and a later route replaces its redirect") {
    Router r;
    r.subtree("/files", named("files"));
    REQUIRE(r.errors().size() == 1);

    Router later;
    later.subtree("/docs/", named("tree"));
    later.get("/docs", named("exact"));
    REQUIRE(later.errors().empty());
    REQUIRE(fetch(later, "GET", "/docs").body == "exact");
    REQUIRE(fetch(later, "POST", "/docs").status == Status::temporary_redirect);
}

// T2.20 (go: TestHostHandlers). ki differs from Go: a host route serves its whole host, rather than Go's
// single ServeMux that also falls back to host-less patterns for that host.
TEST_CASE("HostRouter routes by host, falling back for other hosts") {
    const auto reply = [](std::string text) -> Handler {
        return [text = std::move(text)](Request&, Response& res) -> asio::awaitable<Result> {
            co_return co_await res.write(text);
        };
    };
    Router defaults;
    defaults.subtree("/", reply("Default"));
    defaults.subtree("/someDir/", reply("someDir"));
    defaults.subtree("/#/", reply("hash"));
    Router some_host;
    some_host.subtree("/someDir/", reply("someHost.com/someDir"));
    ki::HostRouter hosts;
    hosts.route("someHost.com", some_host);
    hosts.fallback(defaults);
    REQUIRE(defaults.errors().empty());
    REQUIRE(hosts.errors().empty());

    struct Case {
        const char* url;
        const char* expected;
    };
    for (const auto& c : {Case{"http://localhost/someDir/apage", "someDir"}, Case{"http://localhost/%23/apage", "hash"},
                          Case{"http://localhost/otherDir/apage", "Default"},
                          Case{"http://someHost.com/someDir/apage", "someHost.com/someDir"},
                          Case{"http://otherHost.com/someDir/apage", "someDir"},
                          Case{"http://otherHost.com/aDir/apage", "Default"}}) {
        INFO(c.url);
        REQUIRE(fetch(hosts, "GET", c.url).body == c.expected);
    }
    for (const auto& c : {Case{"http://localhost/someDir", "/someDir/"}, Case{"http://localhost/%23", "/%23/"},
                          Case{"http://someHost.com/someDir", "/someDir/"}}) {
        INFO(c.url);
        const auto out = fetch(hosts, "GET", c.url);
        REQUIRE(out.status == Status::temporary_redirect);
        REQUIRE(out.headers.get("Location") == c.expected);
    }
}

// T2.20 (go: TestStripPortFromHost). ki rejects a host with a port at registration instead of keeping a
// pattern that can never match.
TEST_CASE("HostRouter ignores the port and the case of the host") {
    ki::HostRouter hosts;
    hosts.route("example.com", named("ok"));
    hosts.route("example.com:9000", named("uh-oh"));
    REQUIRE(hosts.errors().size() == 1);
    REQUIRE(fetch(hosts, "GET", "http://example.com:9000/").body == "ok");
    REQUIRE(fetch(hosts, "GET", "http://EXAMPLE.com/").body == "ok");

    ki::Headers headers;
    REQUIRE(headers.set("Host", "example.com:8080"));
    auto request = ki::test::make_request("GET", "/", std::move(headers));
    REQUIRE(ki::test::Recorder::run(hosts, request).body == "ok");
}

TEST_CASE("HostRouter wildcards, duplicates and unknown hosts") {
    ki::HostRouter hosts;
    hosts.route("*.example.com", named("any-sub"));
    hosts.route("*.api.example.com", named("api-sub"));
    hosts.route("www.example.com", named("www"));
    hosts.route("www.example.com", named("again"));
    REQUIRE(hosts.errors().size() == 1);
    REQUIRE(fetch(hosts, "GET", "http://www.example.com/").body == "www");
    REQUIRE(fetch(hosts, "GET", "http://a.example.com/").body == "any-sub");
    REQUIRE(fetch(hosts, "GET", "http://v1.api.example.com/").body == "api-sub");
    REQUIRE(fetch(hosts, "GET", "http://example.com/").status == Status::not_found);
    REQUIRE(fetch(hosts, "GET", "http://other.org/").status == Status::not_found);
}

// T2.21 (go: TestStripPrefix, TestStripPrefixNotModifyRequest)
TEST_CASE("strip_prefix removes a decoded prefix and leaves the request unchanged") {
    const Handler show = [](Request& req, Response& res) -> asio::awaitable<Result> {
        co_return co_await res.write(req.route().route_path);
    };
    const auto h = ki::strip_prefix("/foo/bar", show);
    REQUIRE(fetch(h, "GET", "/foo/bar/qux").body == "/qux");
    // The rest keeps the encoded slash in routing form; Go reports Path "/qux", RawPath "%2Fqux".
    REQUIRE(fetch(h, "GET", "/foo/bar%2Fqux").body == "%2Fqux");
    REQUIRE(fetch(h, "GET", "/foo%2Fbar/qux").status == Status::not_found);
    REQUIRE(fetch(h, "GET", "/bar").status == Status::not_found);

    auto request = ki::test::make_request("GET", "/foo/bar");
    (void)ki::test::Recorder::run(ki::strip_prefix("/foo", show), request);
    REQUIRE(request.path() == "/foo/bar");

    Router api;
    api.get("/users/{id}", named("user"));
    REQUIRE(serve(api, "GET", "/users/1").name == "user");
    REQUIRE(fetch(ki::strip_prefix("/v1", api), "GET", "/v1/users/1").body.starts_with("user"));
}

// T2.21 (go: TestRedirect)
TEST_CASE("redirect resolves and cleans relative urls as Go does") {
    struct Case {
        const char* in;
        const char* want;
    };
    for (const auto& c :
         {Case{"http://foobar.com/baz", "http://foobar.com/baz"}, Case{"https://foobar.com/baz", "https://foobar.com/baz"},
          Case{"test://foobar.com/baz", "test://foobar.com/baz"}, Case{"//foobar.com/baz", "//foobar.com/baz"},
          Case{"/foobar.com/baz", "/foobar.com/baz"}, Case{"foobar.com/baz", "/qux/foobar.com/baz"},
          Case{"../quux/foobar.com/baz", "/quux/foobar.com/baz"}, Case{"///foobar.com/baz", "/foobar.com/baz"},
          Case{"/foo?next=http://bar.com/", "/foo?next=http://bar.com/"},
          Case{"http://localhost:8080/_ah/login?continue=http://localhost:8080/",
               "http://localhost:8080/_ah/login?continue=http://localhost:8080/"},
          Case{"/\xD1\x84\xD1\x83\xD0\xB1\xD0\xB0\xD1\x80", "/%d1%84%d1%83%d0%b1%d0%b0%d1%80"},
          Case{"http://foo.com/\xD1\x84\xD1\x83\xD0\xB1\xD0\xB0\xD1\x80",
               "http://foo.com/%d1%84%d1%83%d0%b1%d0%b0%d1%80"}}) {
        INFO(c.in);
        const std::string url = c.in;
        const auto out = fetch(
            [url](Request& req, Response& res) -> asio::awaitable<Result> {
                co_return co_await ki::redirect(req, res, url, Status::found);
            },
            "GET", "http://example.com/qux/");
        REQUIRE(out.status == Status::found);
        REQUIRE(out.headers.get("Location") == c.want);
    }
}

// T2.21 (go: TestRedirectContentTypeAndBody, TestRedirectEscapedPath, TestRedirectBadPath). Go's cases with
// an empty Content-Type value do not apply: ki headers cannot hold a name without a value list.
TEST_CASE("redirect bodies, escaped paths and header injection") {
    const auto redirect_to = [](std::string url, Status status, bool preset_type = false) -> Handler {
        return [url = std::move(url), status, preset_type](Request& req, Response& res) -> asio::awaitable<Result> {
            if (preset_type) {
                (void)res.set_header("Content-Type", "application/test");
            }
            co_return co_await ki::redirect(req, res, url, status);
        };
    };
    struct Case {
        const char* method;
        bool preset;
        const char* type;
        const char* body;
    };
    for (const auto& c : {Case{"GET", false, "text/html; charset=utf-8", "<a href=\"/foo\">Found</a>.\n\n"},
                          Case{"HEAD", false, "text/html; charset=utf-8", ""}, Case{"POST", false, "", ""},
                          Case{"DELETE", false, "", ""}, Case{"GET", true, "application/test", ""}}) {
        INFO(c.method << " preset=" << c.preset);
        const auto out = fetch(redirect_to("/foo", Status::found, c.preset), c.method, "http://example.com/qux/");
        REQUIRE(out.status == Status::found);
        REQUIRE(out.headers.get("Content-Type").value_or("") == c.type);
        REQUIRE(out.body == c.body);
    }

    REQUIRE(fetch(redirect_to("qux%2Fbaz", Status::moved_permanently), "GET", "http://example.com/foo%2Fbar/")
                .headers.get("Location") == "/foo%2Fbar/qux%2Fbaz");
    REQUIRE(fetch(redirect_to("", Status::not_modified), "GET", "http://example.com").status == Status::not_modified);

    const auto injected = fetch(redirect_to("/a\r\nSet-Cookie: x=1", Status::found), "GET", "/");
    REQUIRE_FALSE(injected.result);
    REQUIRE_FALSE(injected.headers.contains("Set-Cookie"));
    REQUIRE_FALSE(injected.headers.contains("Location"));
}
