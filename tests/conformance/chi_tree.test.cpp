// T2.1, T2.2: chi's tree tests, run through the router.

#include "router_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using ki::testing::named;
using ki::testing::serve;

namespace {

using Strings = std::vector<std::string>;

struct Case {
    std::string method;
    std::string path;
    std::string handler;  // empty: no handler matches
    Strings keys;
    Strings values;
};

void check(const ki::Router& router, const std::vector<Case>& cases) {
    REQUIRE(router.errors().empty());
    for (const auto& c : cases) {
        INFO(c.method << " " << c.path);
        const auto hit = serve(router, c.method, c.path);
        if (c.handler.empty()) {
            REQUIRE(hit.status != ki::Status::ok);
            continue;
        }
        REQUIRE(hit.status == ki::Status::ok);
        REQUIRE(hit.name == c.handler);
        REQUIRE(hit.keys == c.keys);
        REQUIRE(hit.values == c.values);
    }
}

}  // namespace

// chi: TestTree
TEST_CASE("chi TestTree") {
    ki::Router sub;
    sub.get("/users", named("hHubView3"));

    ki::Router r;
    r.get("/", named("hIndex"));
    r.get("/favicon.ico", named("hFavicon"));
    r.get("/pages/*", named("hStub"));
    r.get("/article", named("hArticleList"));
    r.get("/article/", named("hArticleList"));
    r.get("/article/near", named("hArticleNear"));
    // ki differs: chi registers some of these routes twice and the later registration wins; ki rejects
    // duplicates, so only the registration chi expects to take effect is kept (see "chi duplicate ...").
    r.get("/article/{id}", named("hArticleShow"));
    r.get("/article/@{user}", named("hArticleByUser"));
    r.get("/article/{id}/{opts}", named("hArticleShowOpts"));
    r.get("/article/{iffd}/edit", named("hStub"));
    r.get("/article/{id}//related", named("hArticleShowRelated"));
    r.get("/article/slug/{month}/-/{day}/{year}", named("hArticleSlug"));
    r.get("/admin/user", named("hUserList"));
    r.get("/admin/user/", named("hUserList"));
    r.get("/admin/user//{id}", named("hUserShow"));
    r.get("/admin/user/{id}", named("hUserShow"));
    r.get("/admin/apps/{id}", named("hAdminAppShow"));
    r.get("/admin/apps/{id}/*", named("hAdminAppShowCatchall"));
    r.get("/admin/*", named("hAdminCatchall"));
    r.get("/users/{userID}/profile", named("hUserProfile"));
    r.get("/users/super/*", named("hUserSuper"));
    r.get("/users/*", named("hUserAll"));
    r.get("/hubs/{hubID}/view", named("hHubView1"));
    r.get("/hubs/{hubID}/view/*", named("hHubView2"));
    r.get("/hubs/{hubID}/*", sub);
    r.get("/hubs/{hubID}/users", named("hHubView3"));

    check(r,
          {
              {"GET", "/", "hIndex", {}, {}},
              {"GET", "/favicon.ico", "hFavicon", {}, {}},
              {"GET", "/pages", "", {}, {}},
              {"GET", "/pages/", "hStub", {"*"}, {""}},
              {"GET", "/pages/yes", "hStub", {"*"}, {"yes"}},
              {"GET", "/article", "hArticleList", {}, {}},
              {"GET", "/article/", "hArticleList", {}, {}},
              {"GET", "/article/near", "hArticleNear", {}, {}},
              {"GET", "/article/neard", "hArticleShow", {"id"}, {"neard"}},
              {"GET", "/article/123", "hArticleShow", {"id"}, {"123"}},
              {"GET", "/article/123/456", "hArticleShowOpts", {"id", "opts"}, {"123", "456"}},
              {"GET", "/article/@peter", "hArticleByUser", {"user"}, {"peter"}},
              {"GET", "/article/22//related", "hArticleShowRelated", {"id"}, {"22"}},
              {"GET", "/article/111/edit", "hStub", {"iffd"}, {"111"}},
              {"GET", "/article/slug/sept/-/4/2015", "hArticleSlug", {"month", "day", "year"}, {"sept", "4", "2015"}},
              {"GET", "/article/:id", "hArticleShow", {"id"}, {":id"}},
              {"GET", "/admin/user", "hUserList", {}, {}},
              {"GET", "/admin/user/", "hUserList", {}, {}},
              {"GET", "/admin/user/1", "hUserShow", {"id"}, {"1"}},
              {"GET", "/admin/user//1", "hUserShow", {"id"}, {"1"}},
              {"GET", "/admin/hi", "hAdminCatchall", {"*"}, {"hi"}},
              {"GET", "/admin/lots/of/:fun", "hAdminCatchall", {"*"}, {"lots/of/:fun"}},
              {"GET", "/admin/apps/333", "hAdminAppShow", {"id"}, {"333"}},
              {"GET", "/admin/apps/333/woot", "hAdminAppShowCatchall", {"id", "*"}, {"333", "woot"}},
              {"GET", "/hubs/123/view", "hHubView1", {"hubID"}, {"123"}},
              {"GET", "/hubs/123/view/index.html", "hHubView2", {"hubID", "*"}, {"123", "index.html"}},
              {"GET", "/hubs/123/users", "hHubView3", {"hubID"}, {"123"}},
              {"GET", "/users/123/profile", "hUserProfile", {"userID"}, {"123"}},
              {"GET", "/users/super/123/okay/yes", "hUserSuper", {"*"}, {"123/okay/yes"}},
              {"GET", "/users/123/okay/yes", "hUserAll", {"*"}, {"123/okay/yes"}},
          });
}

// chi: TestTreeMoar
TEST_CASE("chi TestTreeMoar") {
    ki::Router r;
    r.get("/articlefun", named("hStub5"));
    r.get("/articles/{id}", named("hStub"));
    r.del("/articles/{slug}", named("hStub8"));
    r.get("/articles/search", named("hStub1"));
    r.get("/articles/{id}:delete", named("hStub8"));
    r.get("/articles/{iidd}!sup", named("hStub4"));
    // ki differs: chi registers this and "/pages/*" twice, the later winning; see "chi duplicate ...".
    r.get("/articles/{id}:{op}", named("hStub2"));
    r.get("/articles/{slug:^[a-z]+}/posts", named("hStub"));
    r.get("/articles/{id}/posts/{pid}", named("hStub6"));
    r.get("/articles/{id}/posts/{month}/{day}/{year}/{slug}", named("hStub7"));
    r.get("/articles/{id}.json", named("hStub10"));
    r.get("/articles/{id}/data.json", named("hStub11"));
    r.get("/articles/files/{file}.{ext}", named("hStub12"));
    r.put("/articles/me", named("hStub13"));
    r.get("/pages/*", named("hStub9"));
    r.get("/users/{id}", named("hStub14"));
    r.get("/users/{id}/settings/{key}", named("hStub15"));
    r.get("/users/{id}/settings/*", named("hStub16"));

    check(r, {
                 {"GET", "/articles/search", "hStub1", {}, {}},
                 {"GET", "/articlefun", "hStub5", {}, {}},
                 {"GET", "/articles/123", "hStub", {"id"}, {"123"}},
                 {"DELETE", "/articles/123mm", "hStub8", {"slug"}, {"123mm"}},
                 {"GET", "/articles/789:delete", "hStub8", {"id"}, {"789"}},
                 {"GET", "/articles/789!sup", "hStub4", {"iidd"}, {"789"}},
                 {"GET", "/articles/123:sync", "hStub2", {"id", "op"}, {"123", "sync"}},
                 {"GET", "/articles/456/posts/1", "hStub6", {"id", "pid"}, {"456", "1"}},
                 {"GET",
                  "/articles/456/posts/09/04/1984/juice",
                  "hStub7",
                  {"id", "month", "day", "year", "slug"},
                  {"456", "09", "04", "1984", "juice"}},
                 {"GET", "/articles/456.json", "hStub10", {"id"}, {"456"}},
                 {"GET", "/articles/456/data.json", "hStub11", {"id"}, {"456"}},
                 {"GET", "/articles/files/file.zip", "hStub12", {"file", "ext"}, {"file", "zip"}},
                 {"GET", "/articles/files/photos.tar.gz", "hStub12", {"file", "ext"}, {"photos", "tar.gz"}},
                 {"PUT", "/articles/me", "hStub13", {}, {}},
                 {"GET", "/articles/me", "hStub", {"id"}, {"me"}},
                 {"GET", "/pages", "", {}, {}},
                 {"GET", "/pages/", "hStub9", {"*"}, {""}},
                 {"GET", "/pages/yes", "hStub9", {"*"}, {"yes"}},
                 {"GET", "/users/1", "hStub14", {"id"}, {"1"}},
                 {"GET", "/users/", "", {}, {}},
                 {"GET", "/users/2/settings/password", "hStub15", {"id", "key"}, {"2", "password"}},
                 {"GET", "/users/2/settings/", "hStub16", {"id", "*"}, {"2", ""}},
             });
}

// chi: TestTreeRegexp
TEST_CASE("chi TestTreeRegexp") {
    ki::Router r;
    r.get("/articles/{rid:^[0-9]{5,6}}", named("hStub7"));
    r.get("/articles/{zid:^0[0-9]+}", named("hStub3"));
    r.get("/articles/{name:^@[a-z]+}/posts", named("hStub4"));
    r.get("/articles/{op:^[0-9]+}/run", named("hStub5"));
    r.get("/articles/{id:^[0-9]+}", named("hStub1"));
    r.get("/articles/{id:^[1-9]+}-{aux}", named("hStub6"));
    r.get("/articles/{slug}", named("hStub2"));

    check(r, {
                 {"GET", "/articles", "", {}, {}},
                 {"GET", "/articles/12345", "hStub7", {"rid"}, {"12345"}},
                 {"GET", "/articles/123", "hStub1", {"id"}, {"123"}},
                 {"GET", "/articles/how-to-build-a-router", "hStub2", {"slug"}, {"how-to-build-a-router"}},
                 {"GET", "/articles/0456", "hStub3", {"zid"}, {"0456"}},
                 {"GET", "/articles/@pk/posts", "hStub4", {"name"}, {"@pk"}},
                 {"GET", "/articles/1/run", "hStub5", {"op"}, {"1"}},
                 {"GET", "/articles/1122", "hStub1", {"id"}, {"1122"}},
                 {"GET", "/articles/1122-yes", "hStub6", {"id", "aux"}, {"1122", "yes"}},
             });
}

// chi: TestTreeRegexpRecursive
TEST_CASE("chi TestTreeRegexpRecursive") {
    ki::Router r;
    r.get("/one/{firstId:[a-z0-9-]+}/{secondId:[a-z0-9-]+}/first", named("hStub1"));
    r.get("/one/{firstId:[a-z0-9-_]+}/{secondId:[a-z0-9-_]+}/second", named("hStub2"));

    check(r, {
                 {"GET", "/one/hello/world/first", "hStub1", {"firstId", "secondId"}, {"hello", "world"}},
                 {"GET", "/one/hi_there/ok/second", "hStub2", {"firstId", "secondId"}, {"hi_there", "ok"}},
                 {"GET", "/one///first", "", {}, {}},
                 {"GET", "/one/hi/123/second", "hStub2", {"firstId", "secondId"}, {"hi", "123"}},
             });
}

// chi: TestTreeRegexMatchWholeParam
TEST_CASE("chi TestTreeRegexMatchWholeParam") {
    ki::Router r;
    r.get("/{id:[0-9]+}", named("hStub1"));
    r.get("/{x:.+}/foo", named("hStub1"));
    r.get("/{param:[0-9]*}/test", named("hStub1"));
    REQUIRE(r.errors().empty());

    for (const auto* path : {"/13", "/a/foo", "//test"}) {
        INFO(path);
        REQUIRE(serve(r, "GET", path).name == "hStub1");
    }
    for (const auto* path : {"/a13", "/13.jpg", "/a13.jpg", "//foo"}) {
        INFO(path);
        REQUIRE(serve(r, "GET", path).status == ki::Status::not_found);
    }
}

// chi: TestTreeFindPattern, through mount(), which refuses a pattern the tree already has.
TEST_CASE("chi TestTreeFindPattern") {
    const auto with_routes = [] {
        ki::Router r;
        r.get("/pages/*", named("hStub1"));
        r.get("/articles/{id}/*", named("hStub2"));
        r.get("/articles/{slug}/{uid}/*", named("hStub3"));
        return r;
    };
    for (const auto* existing :
         {"/pages", "/pages/", "/articles/{id}", "/articles/{something}", "/articles/{slug}/{uid}"}) {
        INFO(existing);
        auto r = with_routes();
        r.mount(existing, named("mounted"));
        REQUIRE(r.errors().size() == 1);
    }
    auto r = with_routes();
    r.mount("/page", named("mounted"));
    REQUIRE(r.errors().empty());
}

TEST_CASE("invalid patterns are rejected and leave the router unchanged") {
    for (const auto* pattern : {"/a/{id", "/a/*/b", "/*/{id}", "/a/{x:[}", "/a/{id}/{id}", "no-slash", ""}) {
        INFO(pattern);
        ki::Router r;
        r.get("/ok", named("ok"));
        r.get(pattern, named("bad"));
        REQUIRE(r.errors().size() == 1);
        REQUIRE(r.routes().size() == 1);
        REQUIRE(serve(r, "GET", "/ok").name == "ok");
    }
}

// chi: TestTree, TestTreeMoar, TestMuxBasic register these routes twice and expect the later registration to
// win. ki differs, as Go's ServeMux does: a duplicate is rejected, recorded with both patterns, and the first
// registration keeps serving.
TEST_CASE("chi duplicate registrations are rejected") {
    struct Pair {
        const char* first;
        const char* second;
        const char* path;
    };
    for (const auto& c :
         {Pair{"/article/{id}", "/article/{id}", "/article/1"},
          Pair{"/article/{sup}/{opts}", "/article/{id}/{opts}", "/article/1/2"},
          Pair{"/admin/user/", "/admin/user/", "/admin/user/"}, Pair{"/admin/*", "/admin/*", "/admin/x"},
          Pair{"/articles/{id}:{op}", "/articles/{id}:{op}", "/articles/1:sync"},
          Pair{"/ping/{id}", "/ping/{iidd}", "/ping/7"}}) {
        INFO(c.second);
        ki::Router r;
        r.get(c.first, named("first"));
        r.get(c.second, named("second"));
        const auto errors = r.errors();
        REQUIRE(errors.size() == 1);
        REQUIRE(errors[0].message.find(c.first) != std::string::npos);
        REQUIRE(errors[0].message.find(c.second) != std::string::npos);
        REQUIRE(serve(r, "GET", c.path).name == "first");
    }
}
