#include <ki/version.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("the linked library version matches the headers") { REQUIRE(ki::version() == KI_VERSION_STRING); }
