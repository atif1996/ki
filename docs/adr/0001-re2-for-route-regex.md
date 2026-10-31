# ADR 0001: RE2 for regular expressions in route patterns

- Status: accepted
- Date: 2026-10-31
- Plan: Phase 2 (T2.2), decision "Regex params"

## Context

chi lets a route parameter carry a regular expression: `/articles/{id:[0-9]+}`,
`/{slug:^[a-z]+}/posts`, `/foo-{suffix:[a-z]{2,3}}.json`. ki promises that chi route tables port
unchanged, so it needs a regex engine for those parameters.

The requirements come from ki's principles and from where the patterns run:

- **Patterns are matched against request paths**, which attackers control. An engine whose matching
  time can grow exponentially with the input (catastrophic backtracking, ReDoS) turns a slow regex in
  a route table into a denial-of-service hole.
- **No exceptions for expected errors.** An invalid pattern is reported when the route is registered,
  as an error value; matching never throws.
- **chi compatibility.** chi uses Go's `regexp` package, whose syntax is RE2's. Patterns written for
  chi should mean the same thing in ki.
- **Patterns are runtime strings.** A route table can be built from configuration, so compile-time-only
  engines are not enough.
- **Available through Conan and vcpkg** on Linux, macOS and Windows.

## Decision

Use [RE2](https://github.com/google/re2) (version 2025-11-05, BSD-3-Clause) for regex route
parameters. Never use `std::regex`.

RE2 is a private dependency of the ki library: its headers appear only in `src/`, never in ki's
public headers. Patterns are compiled once, when the route is registered, with error logging turned
off; a pattern that fails to compile makes the registration fail with an error. Matching uses the
compiled `re2::RE2` with the anchors chi adds (`^...$`), so a parameter must match the regex as a
whole.

## Alternatives considered

| Option | Why not |
|---|---|
| `std::regex` | Throws `std::regex_error` on invalid patterns; backtracking engine with exponential worst cases on hostile input; slow and inconsistent across standard libraries. |
| CTRE (compile-time regular expressions) | Patterns must be compile-time constants, which rules out route tables built at runtime; syntax differs from Go's in places. |
| PCRE2 | Backtracking engine: needs match limits tuned per deployment to stay safe; C API built on raw pointers; its syntax is a superset of Go's, so patterns could behave differently from chi. |
| Boost.Regex | Backtracking, reports errors by throwing, and adds Boost, which ki otherwise avoids. |
| Typed parameters only (`{id:int}`, `{id:uuid}`) | No regex engine at all, but chi route tables with regexes would not port, breaking the compatibility goal. Typed shorthands can still be added on top of RE2 later. |

## Consequences

Pros:

- **Linear-time matching.** RE2 does not backtrack, so matching time is bounded by the input length
  for any pattern. A request path cannot make routing slow, whatever regexes the route table holds.
- **Same syntax as chi.** Go's `regexp` implements RE2 syntax, so chi's regex patterns and tests port
  without changes.
- **No exceptions.** Invalid patterns are detected with `RE2::ok()` and reported as registration
  errors; matching returns a boolean.
- **Mature and widely used.** Developed by Google, used in production at scale, and packaged in Conan
  Center and vcpkg for all three platforms.
- **Kept out of the public API.** ki's headers do not include RE2, so it can be replaced later without
  changing ki's API.

Cons:

- **Abseil comes with it.** RE2 depends on Abseil (Apache-2.0), which Conan builds as 92 static
  libraries and which takes several minutes to compile from source on first use. Package caches hide
  this after the first build.
- **Possible Abseil version conflicts.** An application that already uses a different Abseil version
  must reconcile it with ki's through its package manager.
- **No backreferences or lookaround.** RE2 leaves these out to guarantee linear time. Go's `regexp`
  has the same limits, so no chi pattern relies on them.
- **Larger binaries.** RE2 and the parts of Abseil it uses add to ki's size, even for applications
  that never use regex parameters.

Revisit if the Abseil dependency causes conflicts for users or if binary size matters; a regex-free
build with typed parameters only could then become an option.
