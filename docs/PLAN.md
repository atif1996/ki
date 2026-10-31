# ki — plan

ki is a chi-style router and Go `net/http`-style HTTP server for C++23 coroutines, built on
standalone Asio and llhttp.

Every phase below ends with tests. A phase is done only when all of its tests pass on the platforms
listed for it; "works on my machine" is not an exit criterion.

## Goals and non-goals

Goals:

- chi's routing and middleware model: one handler type, `use` / `with` / `group` / `route` / `mount`,
  radix-tree matching, sub-routers with their own middleware stacks.
- Go `net/http`'s server behaviour: per-phase timeouts, graceful shutdown, cancellation on client
  disconnect, a response state machine, a socket-free test recorder.
- Throughput and tail latency at least on par with Go `net/http` + chi, published with reproducible
  harnesses (smallnest go-web-framework-benchmark fork, TechEmpower).
- Consumable through Conan 2 and vcpkg; builds on Linux, macOS and Windows.

Non-goals for the core library:

- SQL or any database access. A private, non-installed SQL layer exists only for the TechEmpower
  database tests (see Phase 8).
- HTTP/2, TLS and an HTTP client — planned later, but the core types must not block them.

## Principles

| Principle | Rule | Enforcement |
|---|---|---|
| No throw for expected errors | Expected failures return `std::expected<T, ki::Error>` or `std::error_code`. Asio calls use non-throwing completion tokens by default. | CI gate: no `throw` in `include/` or `src/`. clang-tidy `bugprone-exception-escape` on `noexcept` functions. |
| Exception safety | The library is exception-neutral toward user code. Router setup has the strong guarantee. RAII for every resource. A last-resort catch in each connection coroutine keeps one throwing handler from taking down the server. | Phase 1 and Phase 5 tests that throw from handlers. |
| C++23 only | `target_compile_features(ki PUBLIC cxx_std_23)`. No C++20 fallbacks. | T0.8. |
| No bare pointers | Ownership via `std::unique_ptr` / `std::shared_ptr`. Non-owning access via references, `std::span`, `std::string_view`. C APIs that need `void*` (llhttp, later libpq / OpenSSL / nghttp2) are wrapped in RAII types under `src/detail/` and nowhere else. | clang-tidy `cppcoreguidelines-owning-memory`, `-no-malloc`, `-pro-bounds-*`, `-pro-type-reinterpret-cast`; `src/detail/` exemptions carry a `NOLINT` with a reason. |
| Memory safety | No raw buffer arithmetic outside the parser wrapper. | ASan + UBSan and TSan jobs, hardened standard library, fuzzing (Phase 6). |
| Injection prevention | CR/LF and invalid token characters rejected in header names and values. llhttp strict mode, no lenient flags. Size limits on headers, header count and body. Control characters escaped in log output. HTML escaping is the default in templates. | Phase 6 security tests. |
| Prepared statements only | Applies wherever SQL appears (today: only `bench/sql/`). SQL text is a `consteval`-constructed literal; values are bound as typed parameters. | Compile-fail test T8.4. |

The library is compiled with exceptions enabled: `asio::awaitable` uses `std::exception_ptr`
internally. The guarantee is that ki never *throws* for expected conditions, not that it builds with
`-fno-exceptions`.

## Decisions

| Area | Decision |
|---|---|
| I/O | Standalone Asio 1.38.2, C++20 coroutines, one `io_context` per core. |
| HTTP/1.1 parsing | llhttp (handles pipelined input). Not Boost.Beast. |
| Copying | Minimal copying: pass `std::string_view`, move rather than copy, avoid copies that serve no purpose. There is no zero-copy goal: where avoiding a copy would make an API harder to use or open the door to dangling views, ki copies. Performance work is driven by the Phase 7 benchmarks, not done speculatively. |
| JSON / BSON | Glaze, optional (`ki::json`), behind a Conan option and vcpkg feature. Exception-free; also covers BSON, CBOR, MessagePack. Pin the same version in both package managers (Conan Center has 8.4.0, vcpkg has 9.0.0 — use a vcpkg `overrides` entry). |
| Pattern grammar | chi's grammar is the base, so chi route tables port unchanged: `{name}`, params sharing a segment with literals (`{id}.json`, `{file}.{ext}`, `{id}-{aux}`), regex constraints (`{id:[0-9]+}`), trailing `*`, and an optional method prefix (`"GET /path"`). Go's additions: `{name...}` as a named catch-all, and `{$}` on opt-in subtree routes. Host matching lives in a separate `HostRouter`, not in the pattern. |
| Regex params | RE2 2025-11-05 in v1 (linear time, no exceptions, syntax close to Go's `regexp`, which chi uses). Never `std::regex` (throws, ReDoS). RE2 brings abseil as a transitive dependency. See [ADR 0001](adr/0001-re2-for-route-regex.md). |
| URL, query and form encoding | ki's own percent-encoding and decoding in v1, Go-strict on invalid escapes; decoded values are owned strings, used for query strings, URL-encoded form bodies, path values and redirect URLs. Hosts are matched as ASCII only (no IDNA). Ada (WHATWG URL parser) is deferred to later. |
| Precedence and conflicts | chi's order per segment: static → regex → param → catch-all, with backtracking. Registration rejects what is decidable (identical patterns, the same shape with different param names, invalid regex) with an error naming both patterns. Overlap between two regexes is not decidable, so ki does not promise Go's "no ambiguous routes" guarantee. |
| Trailing slash | chi semantics: `/a` and `/a/` are distinct routes. Go's subtree semantics (`/files/` matches everything below, `/files` redirects with the query kept) are opt-in per route. |
| HEAD and path cleaning | Opt-in middleware as in chi (GetHead, CleanPath), enabled in the example server's default stack. |
| Router lifetime | Routes are registered before serving; the router is frozen when the server starts and later registration returns an error. Matching is lock-free. |
| Build | CMake ≥ 3.25, presets, Ninja, target-based, `FILE_SET HEADERS`, installed `ki-config.cmake`. No C++ modules yet. |
| Packaging | Conan 2 primary (verified locally), vcpkg manifest secondary (verified in CI). CMake uses only `find_package`. |
| Linkage | Dev presets and CI build **shared** to prove exports and transitive linking; CI also builds static. Consumers default to **static**. Internal unit tests link a non-installed static `ki_internal` target because the shared library hides internal symbols. |
| Request and Response | Concrete classes, not interfaces. The transport (HTTP/1.1 connection, test recorder, later HTTP/2) sits behind `ki::detail::ResponseTransport` / `BodySource`. Response always tracks status, bytes written and state, so middleware never wraps it. |
| Handler type | `std::function<asio::awaitable<Result>(Request&, Response&)>`. `std::move_only_function` was planned but libc++ does not ship it yet; handlers needing move-only state hold it through `std::shared_ptr`. Revisit when libc++ ships it. |
| Request values | `ki::ContextKey<T>` identified by name (works across shared libraries, may be declared `inline` in a header), with a compile-time type fingerprint so a name reused with another type reads as absent. Accessed through `req.set` / `get` / `value_or` / `with`. |
| Cancellation | Every root coroutine ki spawns disables Asio's `throw_if_cancelled`, which otherwise makes each `co_await` after a cancellation throw. Cancellation reaches handlers only as `operation_aborted` error codes. |
| Version source | `project(ki VERSION …)` in `CMakeLists.txt`. The Conan recipe reads it; `vcpkg.json` duplicates it and T0.9 catches drift. |
| Tests | Catch2 v3. chi's test cases ported as the conformance suite; each ported case carries a `// chi: TestName` provenance comment. Where chi panics, ki returns an error and the ported test asserts on it. |
| First middleware | RequestID, ClientIP (trusted-proxy, rightmost-untrusted — chi's replacement for the deprecated RealIP), Logger, Metrics (RED), Recoverer, Timeout, Heartbeat, StripSlashes, CleanPath, GetHead. |
| Logging | `ki::log`, modelled on Go's `log/slog`: a `Sink` interface receiving structured records, a `Logger` value type with `with(attrs...)`, built-in `null` / `text` / `json` sinks, asynchronous by default. No logging-library dependency in core. Adapters for Quill or spdlog are optional extras, later. |
| Observability hooks | Core `ki` exposes an observer interface (connection accepted/closed with reason, request started/finished, request rejected with reason). Logging and metrics build on it; core has no metrics dependency. |
| Metrics | Optional `ki::prometheus` component on prometheus-cpp **core only** (no `pull`, no `push`, no civetweb). ki serves `/metrics` itself through `prometheus::TextSerializer`; Prometheus scrapes it. Middleware records through a ki-owned recorder interface rather than prometheus-cpp types, so a ki-native metrics data model can replace prometheus-cpp later without changing the middleware. |
| Authentication | One `ki::auth::Principal` (subject, roles, scopes) stored per request by every authentication middleware; `require_roles` / `require_scopes` middleware attached with `use`/`with`/`group` give 401 or 403. HTTP Basic (RFC 7617) in core on libsodium (HMAC-SHA256, constant-time compare). Bearer JWT in the separate library `ki::jwt` (jwt-cpp, OpenSSL 3.x), Ed25519 by default and ES256/RSA opted into per key. ki is a resource server: no token issuing or login flows. See [ADR 0002](adr/0002-authentication-middleware.md). |
| TLS | Terminated by a proxy in v1 (ingress, load balancer, nginx/Envoy, mesh sidecar); ki serves plain HTTP/1.1 and trusts forwarded headers only from configured proxies. In-process TLS is deferred. See [ADR 0003](adr/0003-tls-at-proxy.md). |

## Layout

```
ki/
├── CMakeLists.txt
├── CMakePresets.json         # dev-shared (default), dev-static, asan, tsan, vcpkg-*
├── cmake/                    # ki-config.cmake.in, version.hpp.in
├── include/ki/               # public headers only
├── src/                      # implementation; src/detail/ holds the C-API wrappers
├── tests/
│   ├── unit/                 # links ki_internal (static)
│   ├── conformance/          # ported chi + net/http behaviour; links ki (shared in dev)
│   ├── security/
│   ├── compile_fail/         # try_compile checks that must fail
│   └── fuzz/                 # libFuzzer targets + seed corpora
├── test_package/             # consumer project: Conan test_package and plain find_package check
├── bench/                    # KI_BUILD_BENCH=OFF by default; never installed
│   ├── go-reference/         # net/http + chi with identical routes/middleware
│   ├── smallnest/            # entry for the go-web-framework-benchmark fork
│   ├── techempower/
│   └── sql/                  # private libpq layer (ki_bench_sql), not exported
├── conanfile.py
├── vcpkg.json
├── .clang-tidy
├── .github/workflows/ci.yml
└── docs/PLAN.md
```

## Phases

Platform key: **L** Linux (GCC 14, Clang 19+; Clang 18 lacks `std::expected` with libstdc++), **M** macOS (Apple Clang), **W** Windows (MSVC 17.10+).
"Local" means verified on the development Mac before the phase is reported done; everything else is
verified by CI once a remote exists.

### Phase 0 — build and packaging

Deliverables: CMake target and install/export, presets, Conan recipe and `test_package`,
`vcpkg.json`, `.clang-tidy`, CI workflow, toolchain smoke tests.

| ID | Test | Passes when | Where |
|---|---|---|---|
| T0.1 | Dev build: `conan install . -s compiler.cppstd=23 --build=missing -o "ki/*:shared=True"`, `cmake --preset conan-debug`, build, `ctest` (Debug and Release) | Builds warning-free, all tests pass | Local, L M W |
| T0.2 | Toolchain smoke tests | (a) `std::expected` usable; (b) an Asio coroutine awaits a timer with `as_tuple` and receives an error code, no exception; (c) llhttp parses two pipelined requests from one buffer and reports 2 completed messages | Local, L M W |
| T0.3 | `conan create . -s compiler.cppstd=23 --build=missing` | Package builds, its tests run, `test_package` links `ki::ki` and prints a version equal to `KI_VERSION_STRING` | Local, L M W |
| T0.4 | T0.3 with `-o "ki/*:shared=True"` | Consumer calls `ki::version()` from the shared library (on Windows: through the DLL export) | Local, L M W |
| T0.5 | `cmake --install` to a temporary prefix, then build `test_package` with only `CMAKE_PREFIX_PATH` | `find_package(ki)` resolves asio and llhttp transitively; consumer builds and runs | Local, L |
| T0.6 | vcpkg: `cmake --preset vcpkg-release`, build, `ctest` | Dependencies resolve from the pinned baseline; tests pass | L |
| T0.7 | Subproject: a consumer uses `add_subdirectory(ki)` | `ki::ki` works; ki's tests are not built | Local |
| T0.8 | C++ standard enforcement | (a) A consumer with no `CMAKE_CXX_STANDARD` compiles as C++23; (b) `conan create` with `cppstd=20` stops in `validate()` with a "requires C++23" message | Local |
| T0.9 | Version consistency | `CMakeLists.txt`, `conan inspect .`, `vcpkg.json` and runtime `ki::version()` agree; any mismatch fails CI | Local, L |
| T0.10 | Warnings | Zero warnings with `-Wall -Wextra -Wpedantic -Werror` / `/W4 /WX /permissive-`; flags apply to ki's own CI only, never to consumers | Local, L M W |
| T0.11 | Header self-sufficiency | Each public header compiles alone in its own translation unit | Local |
| T0.12 | No private dependencies leak | Installed `ki-config.cmake`, the Conan package and `vcpkg.json` reference no libpq/SQL dependency; Glaze and prometheus-cpp appear only when their options are on; civetweb and `prometheus-cpp::pull` never appear | Local, L |
| T0.13 | Policy gates | The `throw` grep gate and clang-tidy run clean | Local, L |
| T0.14 | Clean tree | `git status` is clean after every build above | Local |
| T0.15 | CI matrix | Green on L, M, W in shared and static | L M W |

Done when: T0.1–T0.5, T0.7–T0.14 pass locally, and T0.6, T0.15 pass in CI (or are recorded as
blocked on a missing remote).

### Phase 1 — core types and the test recorder

Deliverables: `ki::Error`, `ki::Result`, `ki::Request` (method, target, headers, query, per-request
context map, streaming body), `ki::Response` (status, headers, streaming writes, flush, state
machine), `ki::Handler`, `ki::Middleware`, `ki::test::Recorder` (the `httptest.ResponseRecorder`
analogue), `ki::log` (Logger, Sink, null/text/json sinks, AsyncSink). The observer interface moved to
Phase 5 (T5.18), where the server fires its connection events.

| ID | Test | Passes when |
|---|---|---|
| T1.1 | Implicit 200 | First body write with no explicit status sends 200 |
| T1.2 | Headers lock | `set_header` after headers are sent returns an error; headers unchanged |
| T1.3 | Duplicate status | A second `write_header` is a no-op and is reported, not thrown |
| T1.4 | Header validation | A name with a non-token character, or a value containing CR, LF or NUL, is rejected with an error |
| T1.5 | Context map | Typed set/get on a request; a missing key yields no value; values do not leak between requests |
| T1.6 | Result propagation | An `unexpected` returned by a handler reaches the outermost middleware unchanged |
| T1.7 | Recorder | Captures status, headers and body; runs a handler to completion without a socket |
| T1.8 | Cancellation | The recorder can cancel a running handler; the handler observes `operation_aborted` and returns |
| T1.9 | Exception neutrality | A handler that throws through the recorder leaves the recorder and response in a valid, inspectable state |
| T1.10 | Log level filtering | A call below the active level performs no formatting (a counting formatter is never invoked) |
| T1.11 | Structured log output | The JSON sink emits one valid JSON object per record that parses back with matching fields; the text sink emits quoted `key=value` pairs |
| T1.12 | Log escaping | CR, LF, other control characters and quotes in messages or attributes are escaped in both sinks |
| T1.13 | Bound attributes | `logger.with(...)` attributes appear on every record and do not leak to the parent logger |
| T1.14 | Sink failure | A failing sink (closed file descriptor) reports an error; the request still succeeds; nothing throws |
| T1.15 | Async sink does not block | With the sink stalled for 1 s, handler latency is unchanged; overflow drops records and increments a counter |
| T1.16 | Attribute lifetime | The async sink copies borrowed attributes; ASan stays clean when the caller's strings are destroyed right after the call |
| T1.17 | Flush on shutdown | Every record logged before shutdown is present in the output afterwards |
| T1.18 | Concurrent logging | Many threads logging at once produce no torn records; TSan clean |
| T1.19 | Allocation count | Allocations per access-log line are measured and tracked; a regression against the previous baseline is flagged. Measured once the counting allocator lands with T2.14 |

#### Request target, query and form parsing

Ported from Go's `net/url` and `net/http` tests (Go 1.26), each with a `// go: TestName` comment.
Parsing is lazy (on first access) and done once per request; parse errors are returned, never thrown.

| ID | Test | Passes when | Go sources |
|---|---|---|---|
| T1.21 | Request-target forms | Origin form splits path and query at the first `?`; absolute form is accepted (proxy requests); authority form only for CONNECT; `*` only for OPTIONS; the raw path is kept alongside the decoded path; invalid percent-escapes, control characters and NUL are rejected with 400 | `TestParseRequestURI`, `TestRequestURI`, `TestRejectControlCharacters`, `TestReadRequest`, `TestReadRequest_Bad`, `TestReadRequestErrors` |
| T1.22 | Query values | `?a=1&a=2&b=&c` gives `a=[1,2]`, `b=[""]`, `c=[""]`; repeated values keep their order; `+` and `%20` decode to space and `%2B` to `+`; keys are decoded too; an empty or absent query yields no values | `TestParseQuery`, `TestQueryValues`, `TestQuery`, `TestUnescape` |
| T1.23 | Malformed query | An invalid escape (`%zz`) returns an error naming the bad pair, and the valid pairs remain readable; `;` is not a separator and returns an error, as in Go since 1.17, unless an opt-in AllowQuerySemicolons middleware is used | `TestParseQuery` (error cases), `TestQuerySemicolon`, `TestParseFormSemicolonSeparator` |
| T1.24 | Query and target limits | More than 10,000 parameters returns an error (Go's default `urlmaxqueryparams`), configurable; a request target over the configured length gets 414 | `TestParseQueryLimits` |
| T1.25 | Query API | `get` returns the first value, `get_all` every value, `has` presence; returned views stay valid while the values live, including after the container is moved or copied; typed access (`get_as<int>("page")`, and `path_value<int>("id")` with the router in T2.23) returns an error for missing, non-numeric or out-of-range input | — |
| T1.26 | Escaping round trip | Query and path escaping used by redirects and URL building round-trips every byte value; encoding sorts keys deterministically | `TestEncodeQuery`, `TestQueryEscape`, `TestPathEscape` |
| T1.27 | URL-encoded form bodies | For POST, PUT and PATCH with `application/x-www-form-urlencoded`, `form()` merges body and query with body values first and `post_form()` holds the body only; other methods and unknown content types parse the query only, without error; the form body is capped (Go's 10 MB default, configurable); a parse error leaves the form empty but initialised | `TestParseFormQuery`, `TestParseFormQueryMethods`, `TestPostQuery`, `TestParseFormUnknownContentType`, `TestParseFormInitializeOnError` |

Done when: all T1 tests pass on L M W under the dev-shared preset.

### Phase 2 — router (chi conformance)

Deliverables: radix tree, `Router` with `use` / `with` / `group` / `route` / `mount` / `handle` /
`method`, `not_found` / `method_not_allowed`, `match` / `find` / `walk`, route-pattern reporting,
`path_value`.

Ported from chi `tree_test.go`, `mux_test.go`, `pattern_test.go`, `path_value_test.go`,
`context_test.go`, and from Go's `net/http` `pattern_test.go`, `routing_tree_test.go` and the mux
tests in `serve_test.go` (Go 1.26). Each ported case carries a `// chi: TestName` or
`// go: TestName` provenance comment.

| ID | Area | chi sources |
|---|---|---|
| T2.1 | Tree matching: static, param, wildcard, precedence | `TestTree`, `TestTreeMoar`, `TestTreeFindPattern` |
| T2.2 | Regex params on RE2 and params sharing a segment with literals (`{id}.json`, `{file}.{ext}`, `{id}-{aux}`, `/foo-{suffix:[a-z]{2,3}}.json`), ported unchanged | `TestTreeRegexp`, `TestTreeRegexpRecursive`, `TestTreeRegexMatchWholeParam`, `TestMuxRegexp`, `TestMuxRegexp2`, `TestMuxRegexp3` |
| T2.3 | Basic routing, empty and plain routers, single handler | `TestMuxBasic`, `TestMuxPlain`, `TestMuxEmptyRoutes`, `TestSingleHandler` |
| T2.4 | Params: empty, missing, escaped, wildcard | `TestMuxEmptyParams`, `TestMuxMissingParams`, `TestEscapedURLParams`, `TestMuxWildcardRoute*`, `TestMuxSubrouterWildcardParam`, `TestPathValue` |
| T2.5 | Trailing slashes | `TestMuxTrailingSlash` |
| T2.6 | 404 and 405 with `Allow`, including nested | `TestMuxNestedNotFound`, `TestMethodNotAllowed`, `TestMuxNestedMethodNotAllowed`, `TestMuxComplicatedNotFound` |
| T2.7 | Middleware composition | `TestMuxWith`, `TestRouterFromMuxWith`, `TestMuxMiddlewareStack`, `TestMuxRouteGroups`, `TestNestedGroups` |
| T2.8 | Mounts and sub-routers | `TestMuxMounts`, `TestMuxSubroutesBasic`, `TestMuxSubroutes`, `TestMountingSimilarPattern` |
| T2.9 | Registration errors (chi panics; ki returns errors, router unchanged) | `TestMuxHandlePatternValidation`, `TestMiddlewarePanicOnLateUse`, `TestMountingExistingPath`, `TestMountingSelf` |
| T2.10 | Methods: custom and `QUERY` | `TestCustomHTTPMethod`, `TestQueryHTTPMethod` |
| T2.11 | Introspection | `TestMuxMatch*`, `TestMuxFind`, `TestWalker`, `TestWalk*`, `TestRoutesHidesMountStub`, `TestRoutePattern`, `TestReplaceWildcardsConsecutive` |
| T2.12 | Large route table | `TestMuxBig` |
| T2.13 | Concurrent matching | `TestMuxContextIsThreadSafe`; must also pass under TSan |
| T2.14 | Allocation count | Allocations per route match are measured with a counting allocator and tracked; a regression against the previous baseline is flagged |
| T2.15 | chi API completeness: `Handle` matches every method; `Handle("GET /path")` method prefix; every verb helper including CONNECT, TRACE, OPTIONS; `Middlewares()`; `Chain()`; route-path rewriting by middleware (chi's `RoutePath`); existing and base request context | `TestServeHTTPExistingContext`, `TestServerBaseContext`, `TestContext_Clone`, plus new cases |
| T2.16 | Go grammar additions: `{name...}` named catch-all; `{$}` on subtree routes; pattern parse errors reported with offset and reason | go: `TestParsePattern`, `TestParsePatternError` (cases that apply to ki's grammar) |
| T2.17 | Conflicts and precedence: decidable conflicts return an error naming both patterns and leave the router unchanged; mixed static/regex/param/catch-all routes resolve in chi's documented order | go: `TestConflictsWith`, `TestRegisterConflict`, `TestDescribeConflict`, `TestComparePaths` (decidable cases); chi: `TestTreeMoar` ordering cases |
| T2.18 | Opt-in subtree routes: `/files/` matches everything below; `/files` redirects to `/files/` keeping the query string; correct redirect status for non-GET; 405 with `Allow` on trailing-slash routes | go: `TestServeMuxHandlerRedirects`, `TestServeWithSlashRedirectKeepsQueryString`, `TestServeMuxHandlerRedirectPost`, `TestMuxNoSlash405WithTrailingSlash`, `TestMuxNoSlashRedirectWithTrailingSlash`, `TestServeMuxHandlerTrailingSlash` |
| T2.19 | Escaped path segments: `%2F` inside a param is matched per segment and decoded once in the param value | go: `TestEscapedPathsAndPatterns`; chi: `TestEscapedURLParams` |
| T2.20 | `HostRouter`: routes by host, host-specific routes win over host-less ones, port stripped before matching | go: `TestHostHandlers`, `TestStripPortFromHost`, `TestServeWithSlashRedirectForHostPatterns` |
| T2.21 | `StripPrefix` and `Redirect` helpers: prefix removed without modifying the original request; relative targets resolved, escaped, correct body and Content-Type, bad paths rejected | go: `TestStripPrefix`, `TestStripPrefixNotModifyRequest`, `TestRedirect`, `TestRedirectBadPath`, `TestRedirectContentTypeAndBody`, `TestRedirectEscapedPath`, `TestMuxRedirectRelative` |
| T2.22 | Router lifetime: registration after the server starts returns an error; an empty handler is rejected at registration; an empty router answers 404 | go: `TestServeMuxHandleFuncWithNilHandler`, `TestEmptyServeMux`, `TestShouldRedirectConcurrency` (adapted) |
| T2.23 | Path values API: set, read, copy with the request, missing name yields no value, matched pattern available to handlers | go: `TestPathValueAndPattern`, `TestPathValueNoMatch`, `TestSetPathValue`, `TestRequestClonePathValue` |

Done when: every chi case listed above and every Go routing test named above is ported or explicitly
recorded as not applicable with a reason, and all pass on L M W.

Deferred: chi's `TestServerBaseContext` needs a running server and moves to Phase 5. The allocation counts
(T1.19, T2.14) are measured with the Phase 7 benchmark tooling, which brings the counting allocator.

### Phase 3 — first middleware set

| ID | Middleware | Passes when | chi sources |
|---|---|---|---|
| T3.1 | RequestID | Reuses an incoming `X-Request-Id`, otherwise generates a unique one; stored in the request context | `TestRequestID` |
| T3.2 | ClientIP | All chi client-IP cases, including the four GHSA advisory tests, IPv4-mapped IPv6 and zone-ID bypasses, fail-closed on unparseable input; misconfiguration returns an error at setup instead of panicking | `client_ip_test.go` (23 tests) |
| T3.3 | Logger | Writes an access-log record through `ki::log` with the request-scoped logger: method, path, route pattern, status, bytes, duration, request ID; control characters escaped | `TestRequestLogger` |
| T3.11 | Metrics (RED) | See the RED table below | — |
| T3.4 | Recoverer | A throwing handler becomes a 500 through the router's error handler; an abort-style failure is not swallowed | `TestRecoverer*` |
| T3.5 | Timeout | A handler exceeding the deadline is cancelled and a 503 is produced; a handler under the deadline is unaffected | — |
| T3.6 | Heartbeat | Answers the configured path before routing | — |
| T3.7 | StripSlashes / RedirectSlashes / CleanPath | chi cases pass, including the backslash open-redirect case; CleanPath also passes Go's path-cleaning cases (`//`, `/./`, `/../`, leading slashes) and leaves CONNECT requests untouched | chi: `strip_test.go`, `TestCleanPath`; go: `TestCleanPath`, `TestMuxRedirectLeadingSlashes` |
| T3.8 | GetHead | HEAD routed to GET handlers with no body written; matches Go's automatic HEAD-for-GET behaviour | chi: `TestGetHead`; go: `TestMatchingMethods` (HEAD cases) |
| T3.9 | Response wrapper | Status and byte counts captured; status correct when flushed | `wrap_writer_test.go` |
| T3.10 | Ordering | RequestID → ClientIP → Metrics → Logger → Recoverer → Timeout produces the expected log line and metric samples for a 500 and for a timeout | — |
| T3.12 | BasicAuth | No header, malformed header (wrong scheme, bad base64, no `:`) and wrong credentials give 401 with `WWW-Authenticate: Basic realm="...", charset="UTF-8"`; valid credentials reach the handler with a Principal; `static_credentials` keeps only keyed HMAC-SHA256 digests and compares the submitted user and password against every entry with no early exit (checked by counting comparisons: the same for a known user, an unknown user and a near-miss); a realm containing `"` or control characters is rejected when the middleware is created | `TestBasicAuth` |
| T3.14 | Crypto wrappers | ki's wrappers over libsodium produce HMAC-SHA256 values matching the RFC 4231 test cases; the constant-time comparison is correct for equal, different and different-in-the-last-byte inputs; libsodium is initialized once before first use | — |
| T3.15 | require_https | Passes a request when a trusted proxy reports `X-Forwarded-Proto: https`; rejects it with 403 when the header says `http`, is missing, or comes from an untrusted peer | — |
| T3.13 | Authorization | `require_roles` (all), `require_any_role` and `require_scopes`: 401 without a principal, 403 with insufficient roles or scopes, the handler runs otherwise; the same restrictions work after Basic and Bearer authentication; `walk()` reports them on each guarded route | — |

#### Metrics middleware (RED)

Records rate, errors and duration for every request through the ki recorder interface. The route
pattern is only known after routing, so the middleware reads it after `next` returns, as chi's
`RoutePattern()` does. It sits outside Recoverer and Timeout so it records their 500 and 503.

| Metric | Type | Labels |
|---|---|---|
| `http_server_requests_total` | counter | `method`, `route`, `status` |
| `http_server_request_errors_total` | counter | `method`, `route`, `reason` (`5xx`, `exception`, `timeout`, `aborted`) |
| `http_server_request_duration_seconds` | histogram | `method`, `route`, `status` |
| `http_server_active_requests` | gauge | `method` |

Label rules, because these values are attacker-controlled: `route` is the matched pattern
(`/users/{id}`), never the raw path, and `unmatched` when nothing matched; `method` outside the
known set becomes `OTHER`; no client IP, user agent or query string in any label. A request whose
client disconnects before the response is complete is counted with `status="aborted"`.

| ID | Test | Passes when |
|---|---|---|
| T3.11a | Rate | N requests to a route increase `http_server_requests_total` for that method/route/status by exactly N |
| T3.11b | Errors | A 500, a recovered exception, a timeout (503) and a client abort each increment `http_server_request_errors_total` with the matching `reason`; 4xx does not count as an error |
| T3.11c | Duration | A handler sleeping 50 ms lands in the bucket containing 0.05 s; `_sum` and `_count` match the requests made |
| T3.11d | Route pattern | Requests to `/users/1` and `/users/2` share one `route="/users/{id}"` series; a mounted sub-router reports the full pattern |
| T3.11e | Cardinality bound | 100,000 requests with random paths and methods create at most routes × known methods × observed statuses series |
| T3.11f | Active requests | The gauge rises while handlers are in flight and returns to 0 after they finish, including after cancellation |
| T3.11g | Hot-path cost | Allocations per recorded request are measured and tracked; a regression against the previous baseline is flagged |
| T3.11h | Per-request cost (micro) | Google Benchmark of the middleware wrapping a no-op handler versus the bare handler, at 1 and N threads. Tracked, not gated: CI flags a regression of more than 20 % against the previous baseline as an early warning. The gate is T7.7 |

Done when: all T3 tests pass on L M W.

### Phase 4 — optional components and libraries: JSON, Prometheus and `ki::jwt`

Each component is behind a Conan option and a vcpkg feature; with all options off, ki installs with
none of their dependencies.

| ID | Test | Passes when |
|---|---|---|
| T4.1 | Round trip | A plain aggregate serializes to and parses from JSON with no annotations |
| T4.2 | Malformed input | Invalid JSON returns an error with position information; no exception |
| T4.3 | Request helper | `co_await req.json<T>()` honours the body size limit and maps parse errors to 400 |
| T4.4 | Response helper | `res.json(value)` sets `Content-Type: application/json` and `Content-Length` |
| T4.5 | Optionality | With the JSON option off, ki builds and installs without Glaze; T0.12 confirms |
| T4.6 | Exposition format | `/metrics` output served by ki passes `promtool check metrics` |
| T4.7 | Histogram correctness | Known observations land in the expected buckets; `_sum` and `_count` are exact |
| T4.8 | Concurrent recording | Concurrent increments from N `io_context` threads sum exactly; TSan clean |
| T4.9 | Contention | T7.7 passes with the prometheus-cpp backend; if its per-histogram mutex causes a miss, per-thread histograms merged at scrape time fix it |
| T4.10 | No throw at the boundary | Invalid bucket boundaries or metric names at registration return an error from ki's API; prometheus-cpp exceptions never escape |
| T4.11 | Optionality | With the metrics option off, ki builds and installs without prometheus-cpp |
| T4.12 | No embedded server | The dependency graph (`conan graph info`, vcpkg manifest) contains no civetweb or `prometheus-cpp::pull`; at runtime the process listens only on ports ki opened |
| T4.13 | Internal listener | `/metrics` mounted on an internal listener is not reachable through the public listener |
| T4.14 | Valid tokens | EdDSA (Ed25519) tokens signed by a configured key are accepted by default; ES256, RS256 and PS256 tokens are accepted only for keys with that algorithm enabled and rejected otherwise; accepted tokens map to a Principal: `sub`, roles from a configurable claim path (`roles`, `realm_access.roles`), scopes from `scope` or `scp` |
| T4.15 | Algorithm attacks | `alg: none`, an algorithm different from the key's pinned algorithm, and an HS256 token signed with an RSA public key as the secret are all rejected |
| T4.16 | Claims | Expired, not-yet-valid (each just inside and just outside the leeway), wrong issuer, wrong audience and missing required claims are rejected |
| T4.17 | Keys | An unknown `kid` is rejected; with several keys, `kid` selects the right one; a JWKS document that fails to parse is a configuration error at startup, not a throw |
| T4.18 | Malformed tokens | Wrong segment count, bad base64url, invalid JSON and oversize tokens (over 8 KiB) give 401; jwt-cpp exceptions never escape (fuzzed in T6.8) |
| T4.19 | Transport | Tokens are read only from `Authorization: Bearer`; a token in the query string is ignored |
| T4.20 | Responses | Failures give 401 with `WWW-Authenticate: Bearer error="invalid_token"`; neither the response nor the logs contain the token |
| T4.21 | Separate library | `ki::jwt` is its own target, found with `find_package(ki COMPONENTS jwt)`; core ki builds and installs without jwt-cpp or OpenSSL, and only applications linking `ki::jwt` get them (extends T0.12) |

### Phase 5 — server

Deliverables: listener, connection coroutine, llhttp wrapper, per-connection arena, timeouts,
graceful shutdown, `ConnState` hooks. Tests are black-box over loopback.

| ID | Test | Passes when |
|---|---|---|
| T5.1 | Keep-alive | 1,000 sequential requests on one connection all succeed |
| T5.2 | Pipelining | 100 pipelined requests sent in one write return 100 responses in order |
| T5.3 | `Expect: 100-continue` | `100 Continue` is sent only when the handler first reads the body |
| T5.4 | Chunked | Chunked request bodies are decoded; streamed responses are chunked when length is unknown |
| T5.5 | HEAD | No body bytes on the wire |
| T5.6 | Limits | Headers over the limit → 431; header count over the limit → 431; body over the limit → 413; connection closed afterwards |
| T5.7 | Read-header timeout | A client that stops mid-headers is disconnected within timeout + 100 ms |
| T5.8 | Idle timeout | An idle keep-alive connection is closed within timeout + 100 ms |
| T5.9 | Write timeout | A client that stops reading is disconnected within timeout + 100 ms |
| T5.10 | Graceful shutdown | With one 200 ms request in flight: new connections are refused, the in-flight response is delivered, idle connections close, shutdown returns before its deadline |
| T5.11 | Forced shutdown | A handler that never finishes is cancelled when the shutdown deadline expires |
| T5.12 | Disconnect cancels | Closing the client socket mid-handler makes the handler observe `operation_aborted` within 50 ms |
| T5.13 | Throw isolation | A throwing handler with no Recoverer closes only its own connection; a concurrent connection is still served; the process stays up |
| T5.14 | Multi-core | With N `io_context` threads, 10,000 concurrent connections are served without errors |
| T5.15 | Allocation count | Allocations per steady-state plaintext request are measured and reported next to the Go reference's |
| T5.16 | Close and reject reasons | Each timeout, limit and smuggling case in Phases 5 and 6 fires the observer with the matching reason, and the Prometheus close/reject counters increment accordingly |
| T5.17 | Host header | An HTTP/1.1 request with a missing, duplicated or invalid `Host` gets 400 and the connection is closed; CONNECT targets are accepted in authority form (go: `TestServerValidatesHostHeader`, `TestRequestBadHostHeader`) |
| T5.18 | Observer hooks | Each connection and request lifecycle event (connection accepted, closed with reason, request started, finished, rejected with reason) fires exactly once, in order, with correct fields, over a loopback server and through the recorder. Moved from Phase 1, where no connection events exist yet |

Done when: all T5 tests pass on L M W, timing tests pass 100 consecutive runs without flaking.

### Phase 6 — security and memory safety

| ID | Test | Passes when |
|---|---|---|
| T6.1 | Request smuggling | `Content-Length` + `Transfer-Encoding`, duplicate differing `Content-Length`, and obsolete line folding are rejected with 400 and the connection closed |
| T6.2 | CRLF injection | No public API can emit a header containing CR or LF; compile-fail tests where the type system enforces it |
| T6.3 | Path traversal | `..`, `%2e%2e`, `%2f` and backslash variants never escape a mounted prefix |
| T6.4 | Open redirect | Redirect middleware never produces a scheme-relative or backslash-relative `Location` |
| T6.5 | Log injection | CR, LF and other control characters in request fields appear escaped in logs |
| T6.6 | HTML escaping | Template output escapes `<script>`, quotes and ampersands by default; UTF-8 preserved |
| T6.7 | Sanitizers | All test suites pass under ASan + UBSan (L M, ASan only on W) and TSan (L M) |
| T6.8 | Fuzzing | libFuzzer targets for the HTTP parser, route patterns, path matching, query strings and header values run 60 s each per CI run and 1 h nightly with zero findings; crashing inputs are added to the seed corpus as regression tests |
| T6.9 | Hardened standard library | Test suites pass with `_LIBCPP_HARDENING_MODE` / `_GLIBCXX_ASSERTIONS` enabled |

Done when: all T6 tests pass, and any finding has a regression test.

### Phase 7 — performance and published benchmarks

Deliverables: Go reference server (`net/http` + chi, identical routes and middleware), smallnest
harness fork with a ki entry, TechEmpower Plaintext and JSON entries.

| ID | Test | Passes when |
|---|---|---|
| T7.1 | Routing micro-benchmark | GitHub API route set (from go-http-routing-benchmark): ns/op and allocs/op reported next to chi on the same machine |
| T7.2 | smallnest harness | All its cases (0/10/100/500 ms processing, 100/1000/5000 clients, CPU-bound, keep-alive, non-keep-alive, pipelining) run against ki with the same workload as the Go entries |
| T7.3 | Latency | wrk2 p50/p99/p99.9 at fixed rates reported for ki and the Go reference |
| T7.4 | TechEmpower verify | `./tfb --mode verify --test ki` passes for Plaintext and JSON |
| T7.5 | Regression gate | CI fails if a tracked metric regresses beyond its threshold (thresholds set from the first measured baseline) |
| T7.7 | With metrics, ki keeps pace with go-chi with metrics (gate) | The Go reference adds an equivalent RED middleware on `prometheus/client_golang`: same metric names, same labels (route from chi's `RoutePattern()`), same histogram buckets. Both servers run smallnest's hello world (`/hello`, 0 ms and 10 ms processing) on the same Linux bare-metal host, interleaved runs (≥ 5 each) at 1, N/2 and N cores. **Throughput:** ki + metrics median ≥ go-chi + metrics median. **Latency:** wrk2 at a fixed 70 % of go-chi's max rate, ki p99 and p99.9 ≤ go-chi's. Both servers' `/metrics` report the same request counts after the run (proves both did the same work). A miss fails CI |
| T7.6 | Observability overhead | smallnest runs published with logging and metrics off, access log on, and RED metrics on, so their cost is measured; TechEmpower runs use the `null` log sink |

Benchmarks run on Linux bare metal; macOS numbers are not published (its `SO_REUSEPORT` does not
load-balance).

### Phase 8 — TechEmpower database tests (private)

`bench/sql/` builds `ki_bench_sql`, a non-installed static target over libpq async + pipeline mode.
It is linked only by the TechEmpower app and only with `KI_BUILD_BENCH=ON`.

| ID | Test | Passes when |
|---|---|---|
| T8.1 | Not exported | `cmake --install`, `conan create` and the vcpkg manifest contain no SQL target or libpq dependency |
| T8.2 | Async + pipelined | N queries issued from one coroutine are pipelined on one connection; no thread blocks |
| T8.3 | RAII | Connections and results are released on every path, including cancellation (ASan clean) |
| T8.4 | Literal-only SQL | `query{std::string{...}}` fails to compile; `query{"SELECT ..."}` compiles; values bind only as typed parameters |
| T8.5 | TechEmpower verify | `/db`, `/queries`, `/updates`, `/fortunes` pass `./tfb --mode verify` |

### Later

Ada (WHATWG URL parser, `url_search_params`) for full URL parsing, IDNA hosts and URL building, with a differential fuzz test against ki's own decoder (they must agree on valid input and differ only where ki rejects input WHATWG accepts).

Authentication: fetching and refreshing JWKS from a URL (needs the HTTP client), token introspection
(RFC 7662), login flows (authorization code with PKCE), and an Argon2id `verify` on libsodium.

In-process TLS with `asio::ssl` and OpenSSL, as an option (ADR 0003).

Multipart form parsing (go: `TestParseMultipartForm*`, `TestMultipartReader*`, `TestMissingFileMultipartRequest`, `TestFormValueCallsParseMultipartForm`), with streamed file parts and size limits.

HTTP/2 via nghttp2 (one coroutine per stream), TLS with ALPN, `hijack()` for WebSockets, an HTTP
client with `RoundTripper`-style middleware, io_uring on Linux if profiling calls for it.

Observability: a ki-native metrics data model (per-thread sharded, lock-free) behind the same
recorder interface, replacing prometheus-cpp; an OpenTelemetry adapter that scrapes through ki
rather than the OTel Prometheus exporter's embedded server; Quill and spdlog adapters for `ki::log`.

## Open items

| Item | Default until decided |
|---|---|
| CI host: GitHub Actions or GitLab | GitHub Actions workflow written; unused until a remote exists |
| License (required by Conan Center and vcpkg) | `TBD` placeholder in the recipe and manifest |
| What BSON is needed for (MongoDB or a binary wire format) | Glaze covers both; no BSON-specific work planned |
