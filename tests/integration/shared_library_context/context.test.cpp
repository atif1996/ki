// T1.5: a context key declared in a header must be the same key on both sides of a shared-library boundary.

#include "probe.hpp"

#include <ki/context.hpp>
#include <ki/request.hpp>
#include <ki/test/recorder.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>

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
