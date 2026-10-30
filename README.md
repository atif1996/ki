# ki

A chi-style router and Go `net/http`-style HTTP server for C++23 coroutines, built on standalone Asio
and llhttp.

ki is in Phase 1: core request and response types and the test recorder; there is no router or server
yet. See [docs/PLAN.md](docs/PLAN.md) for the design, the phases and the tests that define when each
phase is done.

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

These examples are compiled and run as part of the test suite (`tests/readme_test.cpp`).

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
