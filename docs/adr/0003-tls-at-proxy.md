# ADR 0003: TLS terminated by a proxy in v1

- Status: accepted
- Date: 2026-10-31
- Plan: Phase 3 (T3.15), Later (in-process TLS)

## Context

Go's `net/http` serves TLS itself (`ListenAndServeTLS`), and so do many C++ HTTP frameworks. Doing the same
in ki means OpenSSL in core (Asio's TLS, `asio::ssl`, is built on OpenSSL and its API-compatible forks
only), plus certificate loading and reloading, cipher and protocol policy, ALPN for HTTP/2, and the tests
for all of it.

In the deployments ki targets, TLS is usually already terminated in front of the application: a
Kubernetes ingress, a cloud load balancer, nginx or Envoy, or a service-mesh sidecar (Istio, Linkerd) that
also encrypts traffic between services with mTLS. Those components manage certificates (ACME,
cert-manager), rotate them without restarts, and speak HTTP/2 and HTTP/3 to clients.

## Decision

ki v1 serves plain HTTP/1.1 and is deployed behind a TLS-terminating proxy. In-process TLS is deferred.

- **Trust only the proxy.** The client's address and the original scheme arrive in `X-Forwarded-For` and
  `X-Forwarded-Proto`. ki reads them only from configured trusted proxies, through the `ClientIP`
  middleware (Phase 3), never from arbitrary clients.
- **`require_https` middleware.** Rejects a request unless a trusted proxy reports
  `X-Forwarded-Proto: https`, so routes such as those using Basic authentication never accept
  credentials sent over plain HTTP by the client.
- **The proxy-to-ki hop is unencrypted.** That is acceptable on loopback, within a pod (sidecar), or where
  a service mesh adds mTLS, and not across untrusted networks. The deployment documentation states this.
- **Local development** uses plain HTTP without a proxy.

## Alternatives considered

| Option | Why not now |
|---|---|
| In-process TLS with `asio::ssl` and OpenSSL | Puts OpenSSL, certificate management and TLS policy in core for v1, when the target deployments already terminate TLS. It remains the plan for later, as an option. |
| TLS on OS libraries (SChannel, Network.framework) | Asio does not support them; ki would need its own TLS stream for each platform. |
| A TLS stack built on libsodium | libsodium provides primitives, not TLS. Implementing the TLS handshake, record layer and X.509 validation is exactly the security-critical code that should come from a dedicated library. |

## Consequences

Pros:

- **No OpenSSL in core.** ki's core depends only on libsodium for crypto (ADR 0002).
- **Mature TLS where it matters.** Certificate rotation, cipher policy, OCSP and HTTP/2 and HTTP/3 to
  clients come from components built for them.
- **Less to secure and test in ki** for v1.
- **Benchmarks unaffected.** The smallnest harness and TechEmpower use plain HTTP.

Cons:

- **A proxy is required in production** wherever traffic crosses an untrusted network.
- **Encryption in transit everywhere** (zero-trust or compliance requirements) depends on a service mesh
  or on in-process TLS arriving later.
- **Trusted-proxy configuration matters.** A wrong trusted-proxy list would let clients forge their
  address or scheme; the `ClientIP` tests, including chi's four security advisories, cover this.

Revisit when a deployment needs end-to-end TLS without a mesh, or ki needs to speak HTTP/2 directly to
clients.
