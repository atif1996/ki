# ADR 0002: Authentication and authorization middleware

- Status: accepted
- Date: 2026-10-31
- Plan: Phase 3 (T3.12, T3.13) and Phase 4 (T4.14 - T4.21)

## Context

Applications built on ki need to authenticate requests and restrict routes to callers with particular
roles. chi has `middleware.BasicAuth` in its core and leaves bearer tokens to the separate
`go-chi/jwtauth` package; route-level restrictions are expressed as per-route middleware with
`r.With(...)` and `r.Group(...)`.

ki's constraints:

- **No exceptions** for expected failures, and no exception from a dependency may escape ki's API.
- **Security by default.** Credentials and tokens arrive in attacker-controlled headers. Parsing must be
  strict, comparisons constant-time, and error responses must not echo request content or reveal why a
  credential failed.
- **Optional heavy dependencies.** Applications that do not use bearer tokens should not link OpenSSL.
- **Crypto from libraries.** ki does not implement cryptographic primitives itself.
- **No TLS in ki for v1.** TLS is terminated by a proxy in front of ki (ADR 0003), so the only crypto ki
  needs is for authentication.
- **ki acts as a resource server.** It validates credentials issued elsewhere; it does not issue tokens
  or run login flows.

## Decision

### One principal, one way to require roles

Every authentication middleware produces the same result: a `ki::auth::Principal` stored as the typed
request value `ki.auth.principal`.

```cpp
struct Principal {
    std::string subject;              // user name or JWT "sub"
    std::vector<std::string> roles;
    std::vector<std::string> scopes;
    std::string scheme;               // "Basic" or "Bearer"
};
```

Authorization is middleware that reads the principal, so it works the same whichever scheme
authenticated the request:

```cpp
r.use({ki::auth::basic_auth("admin area", verify)});
r.with({ki::auth::require_roles({"admin"})}).del("/users/{id}", delete_user);

r.group([](ki::Router& g) {
    g.use({ki::auth::require_scopes({"invoices:write"})});
    g.post("/invoices", create_invoice);
});
```

- No principal: **401**. A principal without the required roles or scopes: **403**.
- `require_roles` requires all listed roles; `require_any_role` accepts any one of them.
- Restrictions are attached with `use`, `with` and `group`, as in chi, so `walk()` lists the middlewares
  guarding every route.

### Basic authentication (core, on libsodium)

`ki::auth::basic_auth(realm, verify)` implements RFC 7617:

- `verify` is `std::function<std::optional<Principal>(std::string_view user, std::string_view password)>`,
  so applications check a password store of their choosing. A stored-password database should use a
  slow hash (bcrypt, Argon2id) inside `verify`, and should verify against a dummy hash for unknown users
  so the slow hash runs either way.
- `ki::auth::static_credentials(map)` is the chi-style convenience for credentials the application
  already holds in its configuration. It never compares the submitted strings directly:
  - At construction it stores HMAC-SHA256 digests of each user name and password under a random key
    generated at startup, and discards the plaintext.
  - Per request it computes the same digests of the submitted user name and password, then compares
    them against **every** configured entry with a constant-time comparison, combining the results
    without branching or stopping early.
  - Because the inputs are hashed first, a guess that shares a prefix with a real user name or password
    produces an unrelated digest, so how far a comparison gets reveals nothing. Because every entry is
    always compared, the time depends only on the number of configured users, not on which user (if
    any) matched, and fixed-length digests hide input lengths.
  - The random key means digests left in memory cannot be matched against precomputed tables.
  - The primitives come from libsodium: `crypto_auth_hmacsha256` for the digests, `sodium_memcmp` for
    the constant-time comparison and `randombytes_buf` for the key. The digests resist timing attacks;
    they are not meant to protect a stolen credential store, which is what the slow hashes in `verify`
    are for.
- libsodium also provides Argon2id (`crypto_pwhash_str_verify`), so ki can later offer a ready-made
  `verify` for applications that store password hashes.
- Parsing is strict: scheme `Basic` (case-insensitive), canonical base64, a `:` separating user and
  password. Malformed headers are treated as missing credentials.
- Failures answer **401** with `WWW-Authenticate: Basic realm="...", charset="UTF-8"`. The realm is
  validated when the middleware is created, so it cannot inject into the header.

### Bearer JWT (separate library `ki::jwt`)

JWT verification is one way to produce a `Principal`, not part of ki's core, so it is a separate library:
its own CMake target `ki::jwt` (`find_package(ki COMPONENTS jwt)`), a Conan `with_jwt` option and a
vcpkg `jwt` feature. It depends on core ki's public auth API and nothing in core depends on it, so it can
move to its own repository later without code changes. Only applications that link it get jwt-cpp and
OpenSSL.

`ki::jwt::authenticate(verifier)` validates `Authorization: Bearer <token>` access tokens:

- **Library:** [jwt-cpp](https://github.com/Thalhammer/jwt-cpp) 0.7.2 with OpenSSL, behind ki's own
  API. jwt-cpp verifies with `std::error_code`, but decoding a malformed token throws; ki catches at that
  boundary and treats it as an invalid token. jwt-cpp and OpenSSL headers never appear in ki's public
  headers.
- **Keys:** loaded at startup from a JWKS document (a string or file). A token's `kid` selects the key,
  and `alg` must match the algorithm that key is pinned to.
- **Ed25519 by default; everything else per key.** Only EdDSA (Ed25519) is accepted without
  configuration: its signatures are deterministic, so no per-signature nonce can leak the key as with
  ECDSA, and it has no curve or parameter choices to get wrong. ES256/ES384 and RSA (RS256/384/512,
  PS256/384/512) are enabled per key. Integrations with identity providers that do not issue EdDSA tokens
  (Entra ID, Google, Okta) enable the algorithm their keys use; Keycloak can issue EdDSA. HMAC
  algorithms are accepted only with an explicitly configured symmetric key; `none` is never accepted.
- **Claims checked:** signature, `exp` and `nbf` with a configurable leeway (default 60 s), `iss` and
  `aud` against configured values (exact match), and any claims the application marks as required.
- **Principal mapping:** `sub` becomes the subject. The roles claim path is configurable because
  identity providers differ (`roles`, Keycloak's `realm_access.roles`, Entra ID's `roles`); scopes come
  from space-separated `scope` or the `scp` array. The verified claims remain available to handlers.
- **Failures** answer **401** with `WWW-Authenticate: Bearer error="invalid_token"` (RFC 6750). The
  response never includes token content or the specific reason; the reason is logged at debug level,
  without the token.

### Crypto libraries

ki uses two, each linked only by what needs it:

- **libsodium** (ISC) in core, for Basic authentication: HMAC-SHA256, constant-time comparison, random
  bytes, and Argon2id for later. Small, exception-free, on all three platforms.
- **OpenSSL 3.x** (Apache-2.0) in `ki::jwt` only, because jwt-cpp verifies every algorithm with OpenSSL,
  and ES256 (NIST P-256) and RSA, which many identity providers require, are not in libsodium.
  Pinned to 3.x in both package managers until jwt-cpp is tested against OpenSSL 4.

| Library | Basic auth | JWT ES256 / RSA | TLS under Asio | No exceptions | FIPS | Why not as the only library |
|---|---|---|---|---|---|---|
| OpenSSL | yes | yes, via jwt-cpp | yes | yes (C API) | provider | Would make every Basic auth user link OpenSSL, though only JWT needs it. |
| libsodium | yes | Ed25519 only | no | yes | no | No P-256 or RSA, so it cannot verify ES256 or RS256 tokens. |
| Crypto++ | yes | yes, but not with jwt-cpp | no | no: throws throughout | no (historical only) | Needs a hand-written JWS verifier and an exception boundary on every call. |
| OS-native (CNG, CommonCrypto) | Windows, macOS | yes, but not with jwt-cpp | no | yes | yes | No Linux userland equivalent, so three implementations to maintain. |

## Alternatives considered

| Option | Why not |
|---|---|
| Hand-written JWS verification over OpenSSL | Signature verification and JOSE header handling are easy to get subtly wrong (algorithm confusion, `alg: none`, key selection); a widely used library is the safer starting point. |
| cpp-jwt | Reports errors by throwing throughout, and offers less control over algorithm pinning per key. |
| l8w8jwt | C library with raw-pointer APIs, and not packaged in Conan Center. |
| Validating tokens only in a proxy (oauth2-proxy, Envoy) | Still a good deployment option, but applications need the principal and roles inside the handler, and ki should work without a proxy. |
| Plain constant-time comparison of the submitted strings | Correct for the password itself, but a hash-map lookup of the user name still leaks through early exits and hit/miss paths, and raw strings leak their lengths. Hashing first and scanning every entry closes all three. |
| bcrypt for `static_credentials` | Its slowness protects stored hashes, which `static_credentials` does not keep. bcrypt is not packaged for all three platforms (libxcrypt has no Windows support and is LGPL; Openwall crypt_blowfish would need vendoring); libsodium's Argon2id serves the same purpose for `verify` callbacks. |
| ki's own SHA-256 and HMAC | Primitives should come from a reviewed library rather than ki. |
| JWT verification inside core ki | Would force jwt-cpp and OpenSSL on every user; the principal model keeps core independent of any one scheme. |
| A bearer-only design | Basic authentication is simple, dependency-free and still common for internal and machine-to-machine endpoints; chi ships it in its core. |

## Consequences

Pros:

- **One authorization model.** Roles and scopes work identically for Basic and Bearer credentials, and
  for any scheme added later, because each produces a `Principal`.
- **chi's idiom.** Restrictions are ordinary middleware attached with `use`, `with` and `group`.
- **Small core dependency.** Basic authentication adds only libsodium to core; jwt-cpp and OpenSSL come
  only with the separate `ki::jwt` library.
- **Hardened defaults.** Constant-time comparisons, algorithm pinning per key, no `alg: none`, exact
  issuer and audience matching, and error responses that reveal nothing.

Cons:

- **Two crypto libraries** when an application uses both Basic and Bearer authentication.
- **OpenSSL dependency** for `ki::jwt`, with the usual build and version management, especially on
  Windows.
- **An exception boundary.** jwt-cpp throws while decoding; ki must catch at every call into it.
- **Keys are static until ki has an HTTP client.** Fetching and refreshing a JWKS URL (and rotating keys
  without a restart) waits for the client.
- **Basic credentials are only as safe as the transport.** With TLS terminated at a proxy (ADR 0003), ki
  offers `require_https`, which rejects requests unless a trusted proxy reports `X-Forwarded-Proto:
  https`, so credentials are never accepted over plain HTTP from the client.

## Not in scope

Issuing tokens, OAuth login flows (authorization code with PKCE), sessions and cookies, and token
introspection (RFC 7662). These can be added later as separate components.

## Open questions

- Which identity providers to test against first (Keycloak, Okta, Entra ID), which sets the default
  roles claim and algorithms in the examples.
- Whether the JWKS refresh should poll on an interval or follow the response's `Cache-Control`.
