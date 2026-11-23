# ki

A chi-style router and Go `net/http`-style HTTP server for C++23 coroutines, built on standalone Asio
and llhttp.

ki has its core request and response types, structured logging, and a chi-compatible router; there is no
server or middleware yet. See [docs/PLAN.md](docs/PLAN.md) for the design, the phases and the tests that
define when each phase is done.

## Passing values through a request

Middleware often needs to hand a value to the code after it: an authenticated user, a request ID, a
resolved client address. ki attaches typed values to the request, the way Go code uses
`context.WithValue`, except that the compiler checks the type.

Declare a key once, usually in a header. The key's type is the value's type, and its name identifies it,
so prefix the name with your project to keep it apart from other libraries' keys:

```cpp
struct User {
    int id;
    std::string name;
};

inline constexpr ki::ContextKey<User> user_key{"myapp.user"};
```

Middleware stores the value, and later handlers read it back:

```cpp
ki::Handler authenticate(ki::Handler next) {
    return [next = std::move(next)](ki::Request& req, ki::Response& res) -> asio::awaitable<ki::Result> {
        req.set(user_key, User{.id = 42, .name = "ada"});
        co_return co_await next(req, res);
    };
}

asio::awaitable<ki::Result> greet(ki::Request& req, ki::Response& res) {
    if (auto user = req.get(user_key)) {
        co_return co_await res.text("hello " + user->get().name);
    }
    co_return std::unexpected(ki::Error{ki::Status::unauthorized, "sign in first"});
}
```

Ways to read a value:

| Call | Returns |
|---|---|
| `req.get(key)` | `std::optional<std::reference_wrapper<const T>>`: empty when absent, otherwise `->get()` gives the value |
| `req.value_or(key, fallback)` | a copy of the value, or `fallback` when absent |
| `req.with(key, [](const T& v) { ... })` | runs the function only when the value is present, and reports whether it ran |

- `req.set(user_key, 42)` does not compile: the value must be a `User`.
- A value that was never set reads as absent, as with a missing middleware in Go.
- Values live as long as the request and are never shared between requests.
- If two libraries use the same name with different types, reading with the wrong type returns empty;
  the value is never misread.

Handlers run without a socket in tests through `ki::test::Recorder`, ki's equivalent of Go's
`httptest.ResponseRecorder`:

```cpp
auto request = ki::test::make_request("GET", "/hello");
auto out = ki::test::Recorder::run(authenticate(greet), request);
// out.status == ki::Status::ok, out.body == "hello ada"
```

These examples are compiled and run as part of the test suite (`tests/integration/readme.test.cpp`).

## Requirements

- A C++23 compiler: GCC 14+, Clang 19+, Apple Clang 17+, MSVC 17.10+ (Clang 18 lacks `std::expected`
  with GCC's standard library)
- CMake 3.25+
- Conan 2 (primary) or vcpkg (manifest mode)

## Build and test with Conan

```sh
conan profile detect                      # once per machine
conan build . -s compiler.cppstd=23 -o "&:shared=True" --build=missing
```

`conan build` configures, builds and runs the test suite. Development builds are shared to prove the
library's exports and transitive linking; consumers get a static library by default. Add
`-c user.project:werror=True` to treat warnings as errors, as CI does.

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
target_link_libraries(app PRIVATE ki::ki ki::log)     # the HTTP types and router; structured logging
target_link_libraries(app_tests PRIVATE ki::test)     # the Recorder, for tests only
```

ki requires C++23 and passes that requirement to every target that links it.

## C++ modules

Each library can also be imported as a module that re-exports its headers:

```cpp
import ki;        // link ki::ki_module
import ki.log;    // link ki::log_module
import ki.test;   // link ki::test_module
```

The build decides whether to make them. With `KI_MODULES=AUTO`, the default, CMake compiles a module that
exports an Asio coroutine and a file that imports it, and builds ki's modules only if that works; otherwise it
prints why and carries on with headers. `KI_MODULES=ON` makes a failed check an error, and `OFF` skips it.
With Conan, pass `-c user.project:modules=ON`.

Modules need CMake 3.28 or newer and the Ninja or Visual Studio generator; Conan uses Makefiles unless you add
`-c tools.cmake.cmaketoolchain:generator=Ninja`. A project that imports an installed ki needs CMake 3.29 or
newer and `set(CMAKE_CXX_EXTENSIONS OFF)`: the module is compiled as standard C++23, and Clang rejects a module
built with different language options from the file importing it. In November 2026 the check passes on Clang 19 (with
`clang-scan-deps`) and Clang 23, and fails on Apple Clang 21, GCC 14 and GCC 16.2; MSVC is untested. Conan's
generated CMake files do not describe modules, so the module targets come only from a plain CMake install or
`add_subdirectory`. Macros such as `KI_VERSION_STRING` are never part of a module and still need
`<ki/version.hpp>`.

A file that writes handlers still includes the Asio headers it uses, such as `<asio/awaitable.hpp>`. A module
keeps only the parts of a header it names itself, and coroutine code instantiates Asio templates, and needs
`std::coroutine_traits`, from the rest.

## Layout

ki follows the [moderncpp template](https://github.com/atif1996/moderncpp): headers and sources sit together
in a directory named after the package, and there is no `include/` or `src/` split. Installing puts the
public headers under `<prefix>/include/ki/`.

| Directory | Library | Include as |
|---|---|---|
| `ki/` | `ki::ki`: the HTTP types and the router | `<ki/router.hpp>`, or `<ki/ki.hpp>` for everything |
| `ki/log/` | `ki::log`: structured logging | `<ki/log/log.hpp>` |
| `ki/test/` | `ki::test`: the Recorder for testing handlers | `<ki/test/recorder.hpp>` |

- Unit tests sit next to the code they test, as `*.test.cpp`.
- `tests/conformance` holds the ported chi and Go router suites, and `tests/integration` the tests that use
  several libraries together.
- `tests/consumer` is a separate project built against the installed package; `conan create` runs it.
- `ki/detail/` holds private headers and sources, which are not installed.

For release builds, `-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON` lets the linker inline across source files
and across ki's static libraries.

## Linux builds on a Mac

`scripts/linux_check.py` runs the CI Linux steps (GCC 14 and Clang 19 on Ubuntu 24.04) in a local
container. It uses Apple's [container](https://github.com/apple/container) (`brew install container`, then
`container system start`) when its service is running, and otherwise any Docker engine, such as Colima:

```sh
python3 scripts/linux_check.py                  # both compilers
python3 scripts/linux_check.py --compiler gcc-14
python3 scripts/linux_check.py --engine docker  # skip Apple's container
```

The two engines keep separate images and Conan cache volumes, so the first run on each builds the
dependencies.

## Repository checks

```sh
git ls-files '*.cpp' '*.cppm' '*.hpp' | xargs clang-format --dry-run --Werror   # CI pins clang-format 22.1.8
python3 scripts/ci_checks.py policy       # no throw or substr in library code
python3 scripts/ci_checks.py versions     # CMakeLists.txt, vcpkg.json and the Conan recipe agree
python3 scripts/ci_checks.py deps         # no forbidden dependencies
python3 scripts/ci_checks.py consumers --deps build/Release/generators --shared
```

## License

ki is licensed under the [Mozilla Public License 2.0](LICENSE): changes to ki's own files must be
published under the same license, while programs that use ki may carry any license. Parts of ki port code
and tests from chi, go-radix and Go; their notices are in [LICENSE-THIRD-PARTY.md](LICENSE-THIRD-PARTY.md).
