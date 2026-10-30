#include <ki/ki.hpp>

#include <cstdio>
#include <print>
#include <version>

// T0.8: this project sets no C++ standard; ki's public cxx_std_23 requirement must supply it.
#if !defined(__cpp_lib_expected) || __cpp_lib_expected < 202202L
#error "consumer is not compiled as C++23: ki's cxx_std_23 requirement did not propagate"
#endif

int main() {
    if (ki::version() != KI_VERSION_STRING) {
        std::println(stderr, "header version {} does not match library version {}", KI_VERSION_STRING,
                     ki::version());
        return 1;
    }
    std::println("ki {}", ki::version());
    return 0;
}
