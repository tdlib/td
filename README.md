# tdlib-obf (Telegram Database Library - Stealth & Anti-Censorship Fork)

[![DeepWiki](https://img.shields.io/badge/DeepWiki-telemt%2Ftdlib--obf-blue.svg?logo=data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAACwAAAAyCAYAAAAnWDnqAAAAAXNSR0IArs4c6QAAA05JREFUaEPtmUtyEzEQhtWTQyQLHNak2AB7ZnyXZMEjXMGeK/AIi+QuHrMnbChYY7MIh8g01fJoopFb0uhhEqqcbWTp06/uv1saEDv4O3n3dV60RfP947Mm9/SQc0ICFQgzfc4CYZoTPAswgSJCCUJUnAAoRHOAUOcATwbmVLWdGoH//PB8mnKqScAhsD0kYP3j/Yt5LPQe2KvcXmGvRHcDnpxfL2zOYJ1mFwrryWTz0advv1Ut4CJgf5uhDuDj5eUcAUoahrdY/56ebRWeraTjMt/00Sh3UDtjgHtQNHwcRGOC98BJEAEymycmYcWwOprTgcB6VZ5JK5TAJ+fXGLBm3FDAmn6oPPjR4rKCAoJCal2eAiQp2x0vxTPB3ALO2CRkwmDy5WohzBDwSEFKRwPbknEggCPB/imwrycgxX2NzoMCHhPkDwqYMr9tRcP5qNrMZHkVnOjRMWwLCcr8ohBVb1OMjxLwGCvjTikrsBOiA6fNyCrm8V1rP93iVPpwaE+gO0SsWmPiXB+jikdf6SizrT5qKasx5j8ABbHpFTx+vFXp9EnYQmLx02h1QTTrl6eDqxLnGjporxl3NL3agEvXdT0WmEost648sQOYAeJS9Q7bfUVoMGnjo4AZdUMQku50McDcMWcBPvr0SzbTAFDfvJqwLzgxwATnCgnp4wDl6Aa+Ax283gghmj+vj7feE2KBBRMW3FzOpLOADl0Isb5587h/U4gGvkt5v60Z1VLG8BhYjbzRwyQZemwAd6cCR5/XFWLYZRIMpX39AR0tjaGGiGzLVyhse5C9RKC6ai42ppWPKiBagOvaYk8lO7DajerabOZP46Lby5wKjw1HCRx7p9sVMOWGzb/vA1hwiWc6jm3MvQDTogQkiqIhJV0nBQBTU+3okKCFDy9WwferkHjtxib7t3xIUQtHxnIwtx4mpg26/HfwVNVDb4oI9RHmx5WGelRVlrtiw43zboCLaxv46AZeB3IlTkwouebTr1y2NjSpHz68WNFjHvupy3q8TFn3Hos2IAk4Ju5dCo8B3wP7VPr/FGaKiG+T+v+TQqIrOqMTL1VdWV1DdmcbO8KXBz6esmYWYKPwDL5b5FA1a0hwapHiom0r/cKaoqr+27/XcrS5UwSMbQAAAABJRU5ErkJggg==)](https://deepwiki.com/telemt/tdlib-obf)



<img width="800" height="446" alt="A detailed stylized illustration, split between above and below the water. On the surface, an annoyed RKN-chan with white pigtails sits at a console on a tiny destroyer, with a 'SIGNAL BLOCKED' screen and a hammer labeled 'ЗАБАНЕНО'. Below, a massive submarine in a cutaway view rests on the seabed. Its hull has stickers for 'telemt', 'tdlib-obf', 'VPN Rocks', and 'Tor Lives'. Inside, four hooded dogs with painted faces make a 'hush' gesture, a crab wears a paper plane hat, and a wrench lies on the floor creating a 'CLANG!'. Complex monitors show 'Traffic Masking ACTIVE'. On the seabed, smiling fish hold glowing smartphones." src="https://github.com/user-attachments/assets/224ff810-4b80-4e39-86be-d3e6250c6f8a" />



`tdlib-obf` is a specialized, security-hardened fork of [TDLib](https://github.com/tdlib/td) designed for high-threat network environments. It implements advanced MTProto-proxy stealth traffic-masking to evade state-level Deep Packet Inspection (DPI) and introduces robust, defense-in-depth transport security enhancements to protect client integrity.

This fork is designed to work in tandem with the [telemt](https://github.com/telemt) Rust MTProxy server.

Published API documentation for this fork: [https://telemt.github.io/tdlib-obf/](https://telemt.github.io/tdlib-obf/)

Custom client integrators should start with: [Custom Client Integration Guide](docs/Documentation/CUSTOM_CLIENT_INTEGRATION_GUIDE.md)

> **Scope:** The entire stealth traffic-masking subsystem (DRS, IPT, greeting camouflage, chaff, bidirectional correlation defense, profile mimicry, ECH circuit breaking) is **active only when the client connects through an MTProto proxy** (i.e. when `ProxySecret::emulate_tls()` is true). Direct Telegram connections that do not go through a proxy are **not affected** by any of these mechanisms.

---

## Table of Contents
- [What is this fork about?](#what-is-this-fork-about)
- [Fork Maintenance Model](#fork-maintenance-model)
- [Implementation Status](#implementation-status)
  - [Implemented Features](#implemented-features)
  - [Work in Progress / Not Yet Implemented](#work-in-progress--not-yet-implemented)
- [Building and Testing](#building-and-testing)
- [Original TDLib Features](#original-tdlib-features)
- [License](#license)

---

## What is this fork about?

Standard MTProto traffic is highly susceptible to DPI classification due to predictable packet sizes, inter-packet timing, fixed connection counts, and static TLS fingerprints. 

**`tdlib-obf` addresses these threats through two primary pillars:**
1. **DPI Evasion (Stealth Shaping):** Transforming the wire image of MTProto traffic to statistically match real-world HTTPS browser traffic (Chrome, Firefox, Safari, iOS, Android) using capture-driven profiles. This includes shaping packet sizes, timing, and connection multiplexing behaviors.
2. **Transport Security Hardening:** Strengthening the internal connection lifecycle, protocol state management, and trust validation mechanisms to protect against network-level interference, unauthorized client modifications, and sophisticated traffic manipulation. *(Note: Specific security mechanisms are intentionally undocumented here to preserve their effectiveness against malicious actors).*

## Fork Maintenance Model

`tdlib-obf` is maintained as a vendor/security fork of [TDLib](https://github.com/tdlib/td), not as a close-tracking mirror.

- `master` is the downstream integration branch and reflects the fork's shipped reality.
- Upstream intake is selective: a change can land as an exact cherry-pick, a bounded local adaptation, or a documented defer/reject decision.
- GitHub ahead/behind counters are ancestry-based only; they are not treated here as a proxy for semantic parity or security equivalence.
- Each upstream intake cycle must record its exact upstream baseline as an annotated tag before downstream adaptation begins; the repeatable maintainer workflow lives in [docs/Documentation/FORK_MAINTENANCE_POLICY.md](docs/Documentation/FORK_MAINTENANCE_POLICY.md).

Authoritative backport and adaptation records live in:

- [CHANGELOG.md](CHANGELOG.md)
- [docs/Documentation/FORK_MAINTENANCE_POLICY.md](docs/Documentation/FORK_MAINTENANCE_POLICY.md)
- [docs/Plans/UPSTREAM_WAVE_5_ACTIVATION_PLAN_2026-05-24.md](docs/Plans/UPSTREAM_WAVE_5_ACTIVATION_PLAN_2026-05-24.md)
- [docs/Plans/UPSTREAM_BACKPORT_MANIFEST_2026-05-08.md](docs/Plans/UPSTREAM_BACKPORT_MANIFEST_2026-05-08.md)
- [docs/Plans/UPSTREAM_BACKPORT_GATING_PLAN_2026-05-08.md](docs/Plans/UPSTREAM_BACKPORT_GATING_PLAN_2026-05-08.md)

---

## Implementation Status

### Implemented Features

#### DPI Evasion & Stealth Shaping
*   **Capture-Driven TLS ClientHello Masking:** 11 browser profiles (Chrome 131, 133, 147 Windows, 147 iOS/Chromium; Firefox 148, 149 macOS, 149 Windows; Safari 26.3; iOS 14; Android OkHttp) derived from real-world `curl_cffi`/browser PCAP captures. Profiles carry provenance metadata (`source_kind`, `trust_tier`, `transport_claim_level`) distinguishing high-fidelity cross-layer-validated captures from advisory-tier uTLS snapshots. Includes anchored Chrome extension shuffling (`ChromeShuffleAnchored`), accurate 7-slot GREASE distributions, ML-KEM-768 PQ key share for current Chrome profiles, and BoringSSL-identical padding (plus per-build entropy randomization to defeat static record-size hashing).
*   **Dynamic Record Sizing (DRS):** Three-phase TLS record payload shaping (SlowStart → CongestionOpen → SteadyState) with empirically calibrated bin distributions per traffic class. Anti-repeat logic prevents consecutive same-size records. Phase transitions use a smooth anchor to avoid discontinuous size jumps. Idle reset (250–1200ms, randomly sampled) reverts to SlowStart after inactivity.
*   **Greeting Camouflage:** The first 1–5 records after connection open are sized from capture-aligned distributions matching actual browser DNS-resolution + initial-GET sequences, specifically targeting classifiers that profile the initial TLS session's size/timing signature.
*   **Inter-Packet Timing (IPT) Obfuscation:** Two-state Markov model with lognormal burst delays (μ=3.5ms, σ=0.8; capped 200ms) and heavy-tail Pareto idle gaps (α=1.5, scale=500ms, max=3s) to mimic interactive web browsing. Applied only to `Interactive` traffic; bypassed for `Keepalive`, `BulkData`, and `AuthHandshake`. Fail-closed exponent clamping prevents zero-delay collapse under adversarial parameter values.
*   **Bidirectional Correlation Defense:** Breaks response-size → request-size correlation used by flow-correlation attacks. After a small inbound response (≤192 bytes), the next outbound record receives a 1200-byte payload floor plus 4–24ms post-response jitter (lognormal sampled), preventing a DPI sensor from linking server responses to proxied client requests by size or timing.
*   **Traffic Classification:** `TrafficClassifier` maps live MTProto session state to `Interactive`, `BulkData`, `Keepalive`, or `AuthHandshake` hints, driving DRS bin selection, IPT bypass decisions, and chaff eligibility — ensuring statistically appropriate sizing for each traffic class independently.
*   **Idle Chaff Injection:** `ChaffScheduler` emits synthetic dummy TLS records after 15s of idle traffic, on 5s intervals, within a 4096 bytes/minute sliding budget. Record sizes are drawn from empirically calibrated idle-chaff bins (50–800 bytes). Prevents idle-gap fingerprinting where the absence of traffic is itself a classifier signal. Disabled by default; enabled per operator configuration.
*   **MTProto Crypto Bucket Elimination:** Random padding applied pre-encryption to break the highly recognizable 64/128-byte quantization peaks of standard MTProto.
*   **Route-Aware ECH (Encrypted ClientHello):** Fail-closed ECH policies with per-destination runtime circuit breakers. ECH is enabled only on non-RU, non-Unknown routes; RU and unresolved routes default to ECH-off. The circuit breaker disables ECH for a destination after 3 failures (TCP reset, hello timeout, TLS fatal alert, or ServerHello parser rejection), persisted to the KV store under daily-bucketed keys, with automatic re-enabling after a 300s TTL. This prevents repeated ECH probes on censored egress without operator intervention.
*   **Stealth Connection Count Capping:** Caps concurrent TCP connections to a single proxy to browser-realistic limits, fixing the behavioral anomaly of standard TDLib opening 18-50 connections to a single endpoint.
*   **Proxy Retry Spam Hardening:** Exponential backoff enforced for proxy connections even when the client is online, preventing reconnect storms that aid DPI correlation.

#### Validation & Infrastructure
*   **Statistical Corpus Validation:** A massive TDD-based validation pipeline running 1k+ iteration Monte Carlo tests and multi-dump statistical comparisons against real browser PCAPs to ensure the generator does not drift from real-world traffic.
*   **Hot-Reloadable Stealth Parameters:** Stealth parameters (DRS bins, IPT timings, profile weights) can be loaded and hot-reloaded from a strict JSON configuration.
*   **Vendored SQLCipher Upgrade:** Cleanly vendored and upgraded SQLCipher (v4.14.0) with pristine upstream isolation and generated C++ wrappers.

### Work in Progress / Not Yet Implemented

While the TLS handshake and record-sizing layers are highly advanced, the following areas are currently pending implementation:

*   **L7 HTTP/2 / HTTP/1.1 Payload Framing:** *Crucial limitation.* Currently, after the TLS handshake, raw MTProto is sent inside TLS-like records. True HTTP/2 multiplexing or HTTP/1.1 message grammar is not yet implemented. Advanced DPI looking for strict L7 HTTP semantics may still flag this traffic.
*   **QUIC / HTTP/3 Transport:** No QUIC transport layer is implemented. QUIC is blocked at the routing layer, forcing downgrade to TLS/TCP. No QUIC fingerprint mimicry (QUIC INITIAL packet structure, connection ID format, transport parameters) exists.
*   **Active Connection Lifecycle Camouflage:** Make-before-break connection rotation and chunk-boundary rotation to prevent unnaturally long-lived single-origin TLS flows (which browsers rarely exhibit).
*   **ServerHello Realism:** The client-side ClientHello is highly realistic, and the client-side response parser has been upgraded from a fixed-prefix check to a proper sequential TLS record reader (validating handshake record type, version, length bounds, ChangeCipherSpec, and ApplicationData completion). However, full semantic ServerHello matrix validation — verifying the server's selected cipher suite, extension set, and record layout against real-browser response patterns — still requires pending integration with the `telemt` server.

---

## Building and Testing

`tdlib-obf` uses CMake. A C++23 compatible compiler, OpenSSL, zlib, and gperf are required.

### Security Dependency Floor (zlib)

Builds require `zlib >= 1.3.2` by default.

Reason: we enforce a conservative minimum to avoid known vulnerable zlib lines associated with early-2026 buffer-overflow reports (notably CVE-2026-22184 in `contrib/untgz`) and to keep CI/dependency scans from accepting outdated zlib packages.

For Debian/Ubuntu system packages, `zlib 1.3` can also be accepted when it is the distro `dfsg` package variant (for example `1:1.3.dfsg-...`), because those repacks exclude `contrib/untgz` and are tracked as not affected for this CVE.

If your system package is older, use one of these options:

1. Upgrade the system zlib package to `1.3.2` or newer.
2. Use a newer custom zlib install and point CMake to it, for example:

```bash
cmake -S .. -B . \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DTD_ENABLE_BENCHMARKS=OFF \
  -DTDLIB_STEALTH_SHAPING=ON \
  -DZLIB_ROOT=/opt/zlib-1.3.2
```

If auto-discovery is still ambiguous in your environment, set explicit variables too:

```bash
cmake -S .. -B . \
  -DZLIB_INCLUDE_DIR=/opt/zlib-1.3.2/include \
  -DZLIB_LIBRARY=/opt/zlib-1.3.2/lib/libz.so
```

### Build Instructions

To build the library with stealth shaping enabled:

```bash
mkdir build
cd build
cmake -S .. -B . \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DTD_ENABLE_BENCHMARKS=OFF \
  -DTDLIB_STEALTH_SHAPING=ON
cmake --build . --parallel 4
```

### Running Tests

This fork relies heavily on an adversarial Test-Driven Development (TDD) approach. To run the test suites:

```bash
# Run the full test suite
ctest --test-dir build --output-on-failure

# Run only the stealth/TLS obfuscation slice
./build/test/run_all_tests --filter TlsHello

# Other common test slice filters
./build/test/run_all_tests --filter EntryWindow
./build/test/run_all_tests --filter AuxChannel
./build/test/run_all_tests --filter BlobStore
./build/test/run_all_tests --filter WindowCount
./build/test/run_all_tests --filter EntryCount
./build/test/run_all_tests --filter ReferenceTable
./build/test/run_all_tests --filter SourceLayout

# Statistical corpus tests (fast: 1k iterations)
./build/test/run_all_tests --filter 1k

# Full nightly corpus (1024-iteration Monte Carlo)
TD_NIGHTLY_CORPUS=1 ./build/test/run_all_tests --filter 1k
```

*Note: Nightly statistical corpus tests set `TD_NIGHTLY_CORPUS=1` to run the full 1024-iteration Monte Carlo validations. Without this flag, the corpus tests run a lighter smoke-test subset.*

---

## Original TDLib Features

Underneath the stealth and security hardening, this remains a fully functional version of TDLib:

* **Cross-platform**: Works on Android, iOS, Windows, macOS, Linux, and other *nix systems.
* **Multilanguage**: Can be easily used with any programming language that is able to execute C functions (via the JSON interface). Native Java (JNI) and .NET bindings are also supported.
* **High-performance**: Capable of handling tens of thousands of active connections simultaneously.
* **Reliable & Consistent**: Guarantees update ordering and remains stable on slow/unreliable Internet connections.
* **Fully-asynchronous**: Requests don't block each other; responses are sent when available.

For general TDLib API documentation, see the [HTML documentation](https://core.telegram.org/tdlib/docs/td__api_8h.html) and the [Getting Started](https://core.telegram.org/tdlib/getting-started) tutorial.

---

## License

`TDLib` is licensed under the terms of the Boost Software License. See [LICENSE_1_0.txt](http://www.boost.org/LICENSE_1_0.txt) for more information.

Modifications and additions in the `tdlib-obf` fork are licensed under the MIT License.
*Copyright 2026 telemt community.*
