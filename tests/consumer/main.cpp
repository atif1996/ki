#include <ki/ki.hpp>

#include <cstdio>
#include <print>
#include <version>

// This project sets no C++ standard; ki's public cxx_std_23 requirement must supply it.
#if !defined(__cpp_lib_expected) || __cpp_lib_expected < 202202L
#error "consumer is not compiled as C++23: ki's cxx_std_23 requirement did not propagate"
#endif

int main() {
    if (ki::version() != KI_VERSION_STRING) {
        std::println(stderr, "header version {} does not match library version {}", KI_VERSION_STRING, ki::version());
        return 1;
    }
    // A regex route exercises the router library and its private RE2 dependency through the package.
    ki::Router router;
    router.get("/items/{id:[0-9]+}",
               [](ki::Request&, ki::Response&) -> asio::awaitable<ki::Result> { co_return ki::Result{}; });
    if (!router.errors().empty() || !router.match("GET", "/items/42") || router.match("GET", "/items/x")) {
        std::println(stderr, "the router did not match as expected");
        return 1;
    }
    std::println("ki {}", ki::version());
    return 0;
}
