// T1.5

#include "probe.hpp"

#include <ki/context.hpp>
#include <ki/request.hpp>
#include <ki/test/recorder.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace {

struct User {
    int id = 0;
    std::string name;
};

inline constexpr ki::ContextKey<User> user_key{"test.user"};
inline constexpr ki::ContextKey<std::string> request_id_key{"test.request_id"};

}  // namespace

TEST_CASE("a stored value is read back with its type") {
    auto request = ki::test::make_request("GET", "/");
    request.set(user_key, User{.id = 42, .name = "ada"});

    auto user = request.get(user_key);
    REQUIRE(user.has_value());
    REQUIRE(user->get().id == 42);
    REQUIRE(user->get().name == "ada");
}

TEST_CASE("a missing key yields no value") {
    auto request = ki::test::make_request("GET", "/");
    REQUIRE_FALSE(request.get(user_key).has_value());
    REQUIRE(request.value_or(request_id_key, "none") == "none");

    bool called = false;
    REQUIRE_FALSE(request.with(user_key, [&](const User&) { called = true; }));
    REQUIRE_FALSE(called);
}

TEST_CASE("value_or and with read a present value") {
    auto request = ki::test::make_request("GET", "/");
    request.set(request_id_key, "abc");
    REQUIRE(request.value_or(request_id_key, "none") == "abc");

    std::string seen;
    REQUIRE(request.with(request_id_key, [&](const std::string& id) { seen = id; }));
    REQUIRE(seen == "abc");
}

TEST_CASE("setting a key again replaces its value") {
    ki::Context context;
    context.set(request_id_key, "first");
    context.set(request_id_key, "second");
    REQUIRE(context.size() == 1);
    REQUIRE(context.value_or(request_id_key, "") == "second");
    REQUIRE(context.erase(request_id_key));
    REQUIRE(context.size() == 0);
}

TEST_CASE("values do not leak between requests") {
    auto first = ki::test::make_request("GET", "/");
    auto second = ki::test::make_request("GET", "/");
    first.set(request_id_key, "only-first");
    REQUIRE_FALSE(second.get(request_id_key).has_value());
}

TEST_CASE("the same name with a different type reads as absent, never reinterpreted") {
    constexpr ki::ContextKey<std::vector<int>> as_vector{"test.request_id"};
    auto request = ki::test::make_request("GET", "/");
    request.set(request_id_key, "abc");
    REQUIRE_FALSE(request.get(as_vector).has_value());
    REQUIRE(request.get(request_id_key).has_value());
}

TEST_CASE("a key declared in a header is the same key in another shared library") {
    auto request = ki::test::make_request("GET", "/");

    request.set(probe_key, "set-by-tests");
    REQUIRE(probe_get(request) == "set-by-tests");

    probe_set(request, "set-by-library");
    REQUIRE(request.value_or(probe_key, "") == "set-by-library");
}

TEST_CASE("a name reused with another type across a shared library boundary reads as absent") {
    constexpr ki::ContextKey<std::string> as_string{"test.mismatch"};
    auto request = ki::test::make_request("GET", "/");
    probe_set_mismatch(request, 7);
    REQUIRE_FALSE(request.get(as_string).has_value());
    REQUIRE(request.get(probe_mismatch_key)->get() == 7);
}
