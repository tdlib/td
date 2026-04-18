# tdlib-obf (Telegram Database Library - Stealth & Anti-Censorship Fork)

<img width="917" height="512" alt="A detailed, stylized illustration of a cutaway view of a futuristic submersible cockpit. Four hooded, secret-keeping figures with painted faces are inside, all making a 'hush' gesture to signal silence. A large wrench has just dropped to the floor with a 'CLANG!' sound effect, creating tension. An orange crab wearing a blue paper plane hat (with a Telegram-like logo) is startled on a table. The interior is full of complex screens and wires, and red laser grids are outside in the dark water, representing a covert coding mission related to obfuscation and evasion of detection." src="https://github.com/user-attachments/assets/37d677a6-fd38-4948-93a5-ae30eff39b9c" />


`tdlib-obf` is a specialized, security-hardened fork of [TDLib](https://github.com/tdlib/td) designed for high-threat network environments. It implements advanced MTProto-proxy stealth traffic-masking to evade state-level Deep Packet Inspection (DPI) and introduces robust, defense-in-depth transport security enhancements to protect client integrity.

This fork is designed to work in tandem with the [telemt](https://github.com/telemt) Rust MTProxy server.

---

## Table of Contents
- [What is this fork about?](#what-is-this-fork-about)
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

---

## Implementation Status

### Implemented Features

#### DPI Evasion & Stealth Shaping
*   **Capture-Driven TLS ClientHello Masking:** Realistic, snapshot-backed browser profiles derived from real-world PCAP data, including anchored extension shuffling and accurate GREASE distributions.
*   **Dynamic Record Sizing (DRS):** TLS record padding to capture-driven targets, eliminating the easily detectable small-record frequency of standard MTProto keepalives and ACKs.
*   **Inter-Packet Timing (IPT) Obfuscation:** Log-normal and Markov-chain based packet delay scheduling to mimic interactive web browsing, with keepalive bypasses to maintain connection stability.
*   **MTProto Crypto Bucket Elimination:** Random padding applied pre-encryption to break the highly recognizable 64/128-byte quantization peaks of standard MTProto.
*   **Route-Aware ECH (Encrypted ClientHello):** Fail-closed ECH policies with runtime circuit breakers (e.g., automatically disabling ECH on egress routes where it is actively dropped by censors).
*   **Stealth Connection Count Capping:** Caps concurrent TCP connections to a single proxy to browser-realistic limits, fixing the behavioral anomaly of standard TDLib opening 18-50 connections to a single endpoint.
*   **Proxy Retry Spam Hardening:** Exponential backoff enforced for proxy connections even when the client is online, preventing reconnect storms that aid DPI correlation.

#### Validation & Infrastructure
*   **Statistical Corpus Validation:** A massive TDD-based validation pipeline running 1k+ iteration Monte Carlo tests and multi-dump statistical comparisons against real browser PCAPs to ensure the generator does not drift from real-world traffic.
*   **Hot-Reloadable Stealth Parameters:** Stealth parameters (DRS bins, IPT timings, profile weights) can be loaded and hot-reloaded from a strict JSON configuration.
*   **Vendored SQLCipher Upgrade:** Cleanly vendored and upgraded SQLCipher (v4.14.0) with pristine upstream isolation and generated C++ wrappers.

### Work in Progress / Not Yet Implemented

While the TLS handshake and record-sizing layers are highly advanced, the following areas are currently pending implementation:

*   **L7 HTTP/2 / HTTP/1.1 Payload Framing:** *Crucial limitation.* Currently, after the TLS handshake, raw MTProto is sent inside TLS-like records. True HTTP/2 multiplexing or HTTP/1.1 message grammar is not yet implemented. Advanced DPI looking for strict L7 HTTP semantics may still flag this traffic.
*   **Active Connection Lifecycle Camouflage:** Make-before-break connection rotation and chunk-boundary rotation to prevent unnaturally long-lived single-origin TLS flows (which browsers rarely exhibit).
*   **ServerHello Realism:** The client-side ClientHello is highly realistic, and the client-side response parser has been upgraded from a fixed-prefix check to a proper sequential TLS record reader (validating handshake record type, version, length bounds, ChangeCipherSpec, and ApplicationData completion). However, full semantic ServerHello matrix validation — verifying the server's selected cipher suite, extension set, and record layout against real-browser response patterns — still requires pending integration with the `telemt` server.

---

## Building and Testing

`tdlib-obf` uses CMake. A C++17 compatible compiler, OpenSSL, zlib, and gperf are required.

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
```

*Note: Nightly statistical corpus tests require setting the `TD_NIGHTLY_CORPUS=1` environment variable to run the full 1024-iteration Monte Carlo validations.*

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
