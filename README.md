# ki

A chi-style router and Go `net/http`-style HTTP server for C++23 coroutines, built on standalone Asio
and llhttp.

ki is at Phase 0: build and packaging only. See [docs/PLAN.md](docs/PLAN.md) for the design, the
phases and the tests that define when each phase is done.

## Requirements

- A C++23 compiler: GCC 14+, Clang 18+, Apple Clang 17+, MSVC 17.10+
- CMake 3.25+
- Conan 2 (primary) or vcpkg (manifest mode)

## Build and test with Conan

```sh
conan profile detect                      # once per machine
conan build . -s compiler.cppstd=23 -o "&:shared=True" --build=missing
```

`conan build` configures, builds and runs the test suite. Development builds are shared to prove the
library's exports and transitive linking; consumers get a static library by default. Add
`-c user.ki:werror=True` to treat warnings as errors, as CI does.

To work with CMake directly after `conan install`, use the presets Conan generates:

```sh
conan install . -s compiler.cppstd=23 -o "&:shared=True" --build=missing
cmake --preset conan-release
cmake --build --preset conan-release
ctest --preset conan-release
```

## Build and test with vcpkg

```sh
export VCPKG_ROOT=/path/to/vcpkg
cmake --preset vcpkg-release
cmake --build --preset vcpkg-release
ctest --preset vcpkg-release
```

## Use ki from another project

```cmake
find_package(ki CONFIG REQUIRED)
target_link_libraries(app PRIVATE ki::ki)
```

ki requires C++23 and passes that requirement to every target that links it.

## Repository checks

```sh
python3 scripts/ci_checks.py policy       # no throw in library code
python3 scripts/ci_checks.py versions     # CMakeLists.txt, vcpkg.json and the Conan recipe agree
python3 scripts/ci_checks.py deps         # no forbidden dependencies
python3 scripts/ci_checks.py consumers --deps build/Release/generators --shared
```

## License

To be decided.
