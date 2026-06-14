<!--
SPDX-FileCopyrightText: Copyright 2026 telemt community
SPDX-License-Identifier: MIT
telemt: https://github.com/telemt
telemt: https://t.me/telemtrs
-->

# Custom Client Integration Guide

**Document Version:** 1.2
**Date:** 2026-05-03
**Scope:** Embedding `tdlib-obf` into custom client software through `tdjson`, generated TDLib bindings, or native C++ integration

---

## Audience

This document is for integrators who are building their own Telegram client software on top of `tdlib-obf` and need the actual integration contract:

1. how to build the library correctly;
2. what the client must provide at runtime;
3. how stealth shaping is activated;
4. what behavior changes when stealth is active;
5. which parts are public TDLib API and which parts are internal-only seams.

This is not a protocol design document and not a re-explanation of all stealth internals. It is an operational guide for shipping a client that uses this fork correctly.

You do **not** need to start with the original TDLib documentation to integrate this fork correctly. The upstream TDLib pages are still useful as API reference and for examples of the authorization/update model, but they do not describe this fork's stealth transport behavior, DoH defaults, or other network-policy changes.

Use this guide as the primary integration contract for `tdlib-obf`, and use the original TDLib docs as a secondary reference for object names, authorization states, and generic TDLib behavior.

Published `tdlib-obf` API documentation website: [https://telemt.github.io/tdlib-obf/](https://telemt.github.io/tdlib-obf/)

---

## Hard Integration Contract

These points are not optional.

1. `tdlib-obf` stealth shaping is **MTProto-proxy-only**. It activates only when TDLib is using an MTProto proxy secret that enters TLS-emulation mode.
2. Direct Telegram connections are unaffected. SOCKS5 and HTTP proxies are also unaffected.
3. The library must be built with `TDLIB_STEALTH_SHAPING=ON`.
4. If your client uses a TLS-emulation MTProto proxy secret while the library was compiled with `TDLIB_STEALTH_SHAPING=OFF`, TDLib fails fast with `LOG(FATAL)` instead of silently falling back to legacy behavior.
5. There is **no separate public stealth API**. Integrators use the normal TDLib proxy API: `addProxy`, `enableProxy`, `disableProxy`, `getProxies`.

Operational consequence: if your client does not support MTProto proxy configuration, there is nothing to integrate on the stealth side.

---

## Minimal TDLib Runtime Model

The original TDLib getting-started guide is still correct about one important thing: your application is integrating an asynchronous request/update engine, not a synchronous RPC client.

These are the minimum TDLib concepts your client must implement correctly even if you never read the upstream tutorial end-to-end.

### 1. Requests are asynchronous

Your client sends requests and receives responses later. If you use `tdjson`, attach an `@extra` field to requests and use it to correlate responses.

### 2. Authorization is a state machine

Your client must handle `updateAuthorizationState` and drive authorization by reacting to the current state.

The first required state is `authorizationStateWaitTdlibParameters`. At that point the client must call `setTdlibParameters` with correct application and storage settings, including:

1. `api_id`
2. `api_hash`
3. writable database directory paths
4. device and system metadata
5. secret-chat and local-cache settings appropriate for the product

After that, the client must continue reacting to later authorization states such as phone number, login code, registration, and password until it reaches `authorizationStateReady`.

### 3. Updates must be handled in receive order

TDLib relies on the application processing incoming updates and responses in the order they are received. Do not build an integration that reorders update handling arbitrarily.

### 4. Caches belong in the client layer

The application should maintain local caches of chats, users, groups, supergroups, and secret chats from updates such as:

1. `updateNewChat`
2. `updateUser`
3. `updateBasicGroup`
4. `updateSupergroup`
5. `updateSecretChat`

Do not assume every returned identifier should be followed by `getChat` or `getUser`. TDLib already sends the authoritative object stream through updates.

### 5. Chat lists are TDLib-managed

TDLib manages list ordering. The client should maintain chat lists by the `(position.order, chat.id)` pair and request more entries through `loadChats` when it needs more data.

### 6. File transfer state is update-driven

If your client uploads or downloads files, it must handle `updateFile` correctly. File progress and final local/remote locations are delivered through the update stream, not through a separate polling API.

### What the upstream docs are still good for

The following original pages are still useful as reference material:

1. `https://core.telegram.org/tdlib/getting-started` for the authorization flow and update model
2. `https://core.telegram.org/tdlib/docs/td__api_8h.html` for object names, type shapes, and generated API conventions

Treat them as reference, not as the authoritative guide for this fork's network behavior.

---

## What Your Client Must Provide

At the application level, a custom client must be able to do the following:

1. Accept an MTProto proxy server address, port, and secret from configuration, UI, MDM, or provisioning.
2. Run the normal TDLib async request/update loop and process updates in order.
3. Drive `setTdlibParameters` and the authorization-state machine correctly.
4. Persist the proxy configuration and re-apply it before authorization when needed.
5. Enable exactly one active proxy through the standard TDLib API.
6. Preserve the provider-issued MTProto secret string exactly as issued.
7. Expose or collect TDLib logs during rollout, because stealth activation, DoH behavior, and fallback decisions are logged.
8. Configure DoH options before the first network activity if the product needs non-default DNS resolution.
9. Tolerate lower parallel connection counts than upstream TDLib when stealth is active.

If your application architecture assumes many parallel raw connections to the same endpoint for download/upload throughput, you must revisit those assumptions for stealth-proxy deployments.

---

## Build And Packaging

### Required build inputs

The repository build contract is standard CMake-based TDLib plus the stealth option enabled.

Required components:

1. CMake
2. A C++23-capable compiler
3. OpenSSL
4. zlib
5. gperf

Optional binding outputs:

1. `tdjson` / C interface for FFI-based integrations
2. generated C++ TDLib API bindings
3. JNI bindings if `TD_ENABLE_JNI=ON`
4. .NET bindings if `TD_ENABLE_DOTNET=ON`

### Recommended build command

From repository root:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DTD_ENABLE_BENCHMARKS=OFF \
  -DTDLIB_STEALTH_SHAPING=ON

cmake --build build --parallel 4
```

Notes:

1. `TDLIB_STEALTH_SHAPING` defaults to `ON` in this fork, but integrators should still pass it explicitly in release builds so the contract is visible in CI and packaging scripts.
2. If you are building a distribution artifact for multiple client products, treat `TDLIB_STEALTH_SHAPING=ON` as a release invariant, not a local convenience flag.

### Smoke-test the build

After building:

```bash
cmake --build build --target run_all_tests --parallel 4
./build/test/run_all_tests --filter TlsHello
```

If you want a wider transport sanity pass:

```bash
ctest --test-dir build --output-on-failure -j 4
```

### Practical integration examples (`/example`)

Yes, integrators should use the repository `example/` tree as a practical companion to this contract.

Start here:

1. `example/README.md` for the cross-language map and build overview.

Per-platform entry points:

1. Python (`tdjson`): `example/python/README.md`, `example/python/tdjson_example.py`
2. C++: `example/cpp/README.md`, `example/cpp/td_example.cpp`, `example/cpp/tdjson_example.cpp`
3. Java/JNI and Java JSON: `example/java/README.md`, `example/java/org/drinkless/tdlib/example/Example.java`, `example/java/org/drinkless/tdlib/example/JsonExample.java`
4. Android packaging scripts: `example/android/README.md`, `example/android/check-environment.sh`, `example/android/fetch-sdk.sh`, `example/android/build-openssl.sh`, `example/android/build-tdlib.sh`
5. Apple/XCFramework build path: `example/ios/README.md`, `example/ios/build-openssl.sh`, `example/ios/build.sh`
6. Swift sample app: `example/swift/README.md`, `example/swift/src/main.swift`
7. .NET/C# paths: `example/csharp/README.md`, `example/csharp/TdExample.cs`, `example/uwp/README.md`, `example/uwp/build.ps1`
8. Browser/WebAssembly path: `example/web/README.md`, `example/web/build-openssl.sh`, `example/web/build-tdlib.sh`, `example/web/copy-tdlib.sh`, `example/web/build-tdweb.sh`, `example/web/tdweb/README.md`

Important for this fork: those examples come from the upstream TDLib ecosystem and are useful for integration structure, but your production `tdlib-obf` artifacts should still explicitly enforce `-DTDLIB_STEALTH_SHAPING=ON` as described in this guide.

---

## Public Client API Surface

Stealth integration is deliberately routed through existing TDLib proxy objects.

Relevant public API surface:

1. `proxyTypeMtproto secret:string = ProxyType`
2. `addProxy proxy:proxy enable:Bool = AddedProxy`
3. `enableProxy proxy_id:int32 = Ok`
4. `disableProxy = Ok`

If you use the JSON interface, the minimal request shape is:

```json
{
  "@type": "addProxy",
  "proxy": {
    "@type": "proxy",
    "server": "your-proxy.example.com",
    "port": 443,
    "type": {
      "@type": "proxyTypeMtproto",
      "secret": "PASTE_PROVIDER_SECRET_HERE"
    }
  },
  "enable": true
}
```

Important details:

1. The TL schema comment says the MTProto secret is hexadecimal.
2. The implementation currently accepts hexadecimal, Base64URL, and Base64 encodings through `ProxySecret::from_link`.
3. The safest policy for integrators is: **treat the secret as an opaque provider-issued string and pass it through unchanged**.

Do not normalize, lowercase, split, or reconstruct the secret string in UI or middleware layers unless you fully control the server-side generation logic.

For `tdjson` integrations, the same `setOption` mechanism used for generic TDLib options is also the public entry point for the DoH configuration described later in this guide.

---

## MTProto Secret Requirements

### Secrets that activate stealth

Stealth activation happens only for MTProto secrets that satisfy `ProxySecret::emulate_tls()`.

At the raw byte level, that means:

1. first byte `0xee`;
2. followed by a 16-byte MTProto proxy secret;
3. followed by an SNI domain string.

### Domain validation rules

If you generate or validate secrets yourself, the appended TLS-emulation domain must satisfy the library's fail-closed parser:

1. total appended domain length must be between `1` and `182` bytes;
2. labels must be ASCII alphanumeric or `-` only;
3. labels must not start with `-`;
4. labels must not end with `-`;
5. labels must not be empty;
6. each label must be at most `63` bytes.

This means embedded NUL bytes, control bytes, non-ASCII bytes, leading dots, trailing dots, empty labels, and overlong labels are all rejected.

### Secrets that do not activate stealth

These do **not** enter the stealth decorator path:

1. plain 16-byte MTProto secrets;
2. `0xdd` padded MTProto secrets;
3. SOCKS5 proxies;
4. HTTP proxies.

Those routes still work as supported TDLib proxy modes, but they do not get stealth shaping.

---

## What Changes When Stealth Is Active

Once the library is built with stealth enabled and the client uses an `ee...` MTProto proxy secret, the transport stack changes in these ways.

### 1. Browser-like TLS masking is automatic

The library wraps the MTProto obfuscated transport in `StealthTransportDecorator` and automatically selects a browser profile for TLS-emulation mode. Integrators do not choose a profile through public API.

Current runtime behavior is platform-aware and route-aware.

### 2. Connection counts are intentionally capped

For stealth TLS MTProto proxies, the connection-count planner caps session counts to browser-like levels:

1. main sessions: `1`
2. upload sessions: `1`
3. download sessions: `1`
4. small-download lane is merged into download

This is intentional. It reduces proxy-like connection fan-out that would otherwise be visible to DPI or telemetry.

Integrators should expect different bandwidth/concurrency behavior from upstream TDLib in this mode.

### 3. Reconnect pacing and anti-churn are enforced

The flow controller enforces a per-destination budget and minimum reconnect interval using runtime flow behavior policy. Current default policy includes:

1. maximum connects per 10 seconds per destination;
2. minimum reconnect interval;
3. connection lifetime and reuse constraints consumed by connection-pool policy.

Do not write client code that assumes it can force rapid repeated reconnects to the same stealth proxy endpoint without pacing.

### 4. QUIC is disabled by policy

The runtime route-policy validation keeps QUIC disabled. This fork is TCP/TLS-oriented for stealth-proxy mode. Do not design a custom client integration that depends on QUIC/HTTP3 support here.

### 5. Direct connections are unchanged

If the active transport is not MTProto-proxy TLS emulation, `tdlib-obf` behaves like TDLib plus the other hardening changes in this fork, but the stealth masking subsystem itself is not active.

---

## Failure Modes And Logging

Integrators should route TDLib logs somewhere observable during rollout.

Important behaviors:

1. **Build mismatch:** `ee...` secret plus `TDLIB_STEALTH_SHAPING=OFF` causes fatal termination with explicit diagnostics.
2. **Bad proxy secret:** `addProxy` / proxy construction fails with a `400` error.
3. **Runtime config rejection:** if transport stealth config validation fails during decorator construction, TDLib logs a warning and falls back to plain obfuscated MTProto transport for that connection.
4. **Decorator initialization failure:** TDLib logs a warning and falls back to plain obfuscated transport.
5. **Successful activation:** TDLib logs that stealth shaping is enabled for the emulate-TLS transport.

For production integrations, configure `setLogStream` or the equivalent binding-specific log sink before testing stealth deployment.

---

## TLS Trust Store Requirements

Stealth shaping does not remove the need for correct TLS trust handling in your client runtime environment.

Current trust-store behavior in this fork:

1. On non-iOS-family platforms, OpenSSL default cert locations are probed.
2. Environment overrides are supported through:
   1. `SSL_CERT_FILE`
   2. `SSL_CERT_DIR`
   3. `TDLIB_SSL_CERT_FILE`
   4. `TDLIB_SSL_CERT_DIR`
3. On Android, the implementation explicitly probes both:
   1. `/apex/com.android.conscrypt/cacerts`
   2. `/system/etc/security/cacerts`
4. On Apple platforms, trust anchors are loaded through `Security.framework` / keychain APIs.
5. iOS-family platforms intentionally avoid relying on OpenSSL default filesystem bundle probing.

What this means for integrators:

1. If you ship on Linux, Windows, or Android variants with unusual CA layouts, verify trust-store discovery early.
2. If your app sandbox does not expose the platform default bundle path, provide explicit cert overrides where appropriate.
3. If verification is enabled and no trusted certificates are available, this fork now fails closed instead of silently proceeding with an empty trust store.

---

## DNS-Over-HTTPS Configuration

This fork ships with built-in DoH resolver support and also allows integrators to point TDLib at a custom DoH endpoint.

This is not limited to stealth MTProto proxy mode. It affects the connection-creation path that resolves hostnames before network connections are opened.

### Default behavior

If you do nothing, hostname resolution uses a DoH resolver chain.

Current resolver order:

1. default / `dns_type=google`: Google first, Cloudflare fallback
2. `dns_type=cloudflare`: Cloudflare first, Google fallback
3. `dns_type=custom` without a custom URL: Google first, Cloudflare fallback

Built-in endpoints:

1. Google: `https://dns.google/resolve`
2. Cloudflare: `https://cloudflare-dns.com/dns-query`

### Public option keys

The public integration surface is `setOption`.

Supported option names:

1. `dns_type` with values `google`, `cloudflare`, or `custom`
2. `custom_dns_url`
3. `custom_dns_headers`

Important precedence rules:

1. if `custom_dns_url` is non-empty, the resolver switches to custom mode regardless of `dns_type`
2. if `dns_type=custom` but `custom_dns_url` is empty, TDLib falls back to the default Google-then-Cloudflare chain

### When to set these options

Set DNS options **before first network use**: before login, before enabling a proxy, and before any request that can trigger connection creation.

Reason: `ConnectionCreator` constructs and caches the DNS resolver actor on first use. Later option changes are not the safe integration path to rely on.

### `tdjson` examples

Use standard TDLib `setOption` requests.

Select Cloudflare-first resolution:

```json
{
  "@type": "setOption",
  "name": "dns_type",
  "value": {
    "@type": "optionValueString",
    "value": "cloudflare"
  }
}
```

Select a custom DoH endpoint:

```json
{
  "@type": "setOption",
  "name": "custom_dns_url",
  "value": {
    "@type": "optionValueString",
    "value": "https://resolver.example.com/dns-query"
  }
}
```

Pass an additional header to the custom resolver:

```json
{
  "@type": "setOption",
  "name": "custom_dns_headers",
  "value": {
    "@type": "optionValueString",
    "value": "Authorization: Bearer YOUR_TOKEN"
  }
}
```

### Custom header limitation

Despite the plural name, `custom_dns_headers` is currently parsed as a single `Header-Name: value` string and converted into one header pair. TDLib also derives and adds a `Host` header from `custom_dns_url` automatically.

If your deployment requires multiple custom headers or a different custom DoH request shape, that is currently a native-fork customization task rather than a supported public `tdjson` feature.

---

## Runtime Params: Public Contract vs Internal Seam

### What is public today

For normal client integrators, the effective public contract is simple:

1. stealth runtime behavior comes from the compiled default runtime params snapshot;
2. the client does not need to call any stealth-specific API;
3. there is currently **no public `td_api` or `tdjson` option** to point TDLib at a stealth runtime params file or trigger reloads.

### What exists internally

The codebase does contain an internal file-backed runtime params loader:

1. `StealthParamsLoader`
2. `set_runtime_stealth_params`
3. `get_runtime_stealth_params_snapshot`

This is relevant only if you maintain a native fork and want to wire your own advanced embedding seam.

### If you wire the loader yourself

The loader has a strict fail-closed file contract:

1. missing file means use defaults;
2. config must be a regular file;
3. file must be owned by the current user;
4. file must not be writable by group or others;
5. parent directory must be secure;
6. size limit is `64 KiB`;
7. JSON root must be an object with exact schema;
8. `version` must be `1`;
9. after five consecutive reload failures, reload enters a 60-second cooldown;
10. failed reload keeps the last-known-good published snapshot.

There is also a stability constraint: once a successful non-default publication happens, `platform_hints` cannot drift across reloads.

### Minimal internal-only example

If you are embedding at the C++ level and intentionally wiring the loader, a minimal accepted config shape looks like:

```json
{
  "version": 1,
  "profile_weights": {
    "chrome133": 50,
    "chrome131": 20,
    "chrome120": 15,
    "firefox148": 15,
    "safari26_3": 20,
    "ios14": 70,
    "android11_okhttp_advisory": 30
  },
  "route_policy": {
    "unknown": {"ech_mode": "disabled"},
    "ru": {"ech_mode": "disabled"},
    "non_ru": {"ech_mode": "rfc9180_outer"}
  },
  "route_failure": {
    "ech_failure_threshold": 4,
    "ech_disable_ttl_seconds": 600.0,
    "persist_across_restart": true
  },
  "bulk_threshold_bytes": 16384
}
```

If you are not maintaining a native fork, ignore this section and use the compiled defaults.

---

## Recommended Client-Side Integration Checklist

Before shipping `tdlib-obf` in a custom client, verify the following.

1. Your build scripts explicitly pass `-DTDLIB_STEALTH_SHAPING=ON`.
2. Your app can create and enable an MTProto proxy before authorization.
3. Your proxy configuration UI or provisioning path can store a provider-issued MTProto secret string without rewriting it.
4. You understand that only `ee...` TLS-emulation MTProto secrets activate stealth shaping.
5. You do not assume upstream TDLib connection parallelism when stealth proxy mode is enabled.
6. You have a logging path for TDLib warnings and info messages during rollout.
7. You validated certificate/trust-store discovery on each target OS.
8. If the product requires non-default DNS resolution, you set `dns_type`, `custom_dns_url`, and `custom_dns_headers` before first network activity.
9. You do not rely on QUIC/HTTP3 in this fork.

---

## Deployment Checklist

For a concrete rollout with a server such as `telemt`:

1. Obtain the MTProto proxy endpoint, port, and secret from the server operator.
2. Build `tdlib-obf` with `TDLIB_STEALTH_SHAPING=ON`.
3. Integrate through `tdjson` or your binding of choice without adding any stealth-specific public API.
4. If needed, set DoH options through `setOption` before first network use.
5. Add the MTProto proxy using `proxyTypeMtproto` and enable it.
6. Confirm logs show stealth activation instead of fallback.
7. Run at least the `TlsHello` test slice during CI for the library artifact you ship.

---

## Source Anchors

These are the primary code paths behind the integration contract described above.

1. `CMakeLists.txt`
2. `td/mtproto/IStreamTransport.cpp`
3. `td/mtproto/ProxySecret.h`
4. `td/mtproto/ProxySecret.cpp`
5. `td/telegram/net/Proxy.cpp`
6. `td/generate/scheme/td_api.tl`
7. `td/telegram/OptionManager.cpp`
8. `td/telegram/net/ConnectionCreator.cpp`
9. `tdnet/td/net/GetHostByNameActor.h`
10. `tdnet/td/net/GetHostByNameActor.cpp`
11. `td/telegram/net/StealthConnectionCountPolicy.cpp`
12. `td/telegram/net/ConnectionFlowController.cpp`
13. `td/mtproto/stealth/StealthConfig.cpp`
14. `td/mtproto/stealth/StealthRuntimeParams.cpp`
15. `td/mtproto/stealth/StealthParamsLoader.cpp`
16. `tdnet/td/net/SslCtx.cpp`

---

**Status:** Current and code-backed  
**Maintainer:** telemt community<!--
SPDX-FileCopyrightText: Copyright 2026 telemt community
SPDX-License-Identifier: MIT
telemt: https://github.com/telemt
telemt: https://t.me/telemtrs
-->

# Custom Client Integration Guide

**Document Version:** 1.0  
**Date:** 2026-05-02  
**Scope:** Embedding `tdlib-obf` into custom client software through `tdjson`, generated TDLib bindings, or native C++ integration

---

## Audience

This document is for integrators who are building their own Telegram client software on top of `tdlib-obf` and need the actual integration contract:

1. how to build the library correctly;
2. what the client must provide at runtime;
3. how stealth shaping is activated;
4. what behavior changes when stealth is active;
5. which parts are public TDLib API and which parts are internal-only seams.

This is not a protocol design document and not a re-explanation of all stealth internals. It is an operational guide for shipping a client that uses this fork correctly.

---

## Hard Integration Contract

These points are not optional.

1. `tdlib-obf` stealth shaping is **MTProto-proxy-only**. It activates only when TDLib is using an MTProto proxy secret that enters TLS-emulation mode.
2. Direct Telegram connections are unaffected. SOCKS5 and HTTP proxies are also unaffected.
3. The library must be built with `TDLIB_STEALTH_SHAPING=ON`.
4. If your client uses a TLS-emulation MTProto proxy secret while the library was compiled with `TDLIB_STEALTH_SHAPING=OFF`, TDLib fails fast with `LOG(FATAL)` instead of silently falling back to legacy behavior.
5. There is **no separate public stealth API**. Integrators use the normal TDLib proxy API: `addProxy`, `enableProxy`, `disableProxy`, `getProxies`.

Operational consequence: if your client does not support MTProto proxy configuration, there is nothing to integrate on the stealth side.

---

## What Your Client Must Provide

At the application level, a custom client must be able to do the following:

1. Accept an MTProto proxy server address, port, and secret from configuration, UI, MDM, or provisioning.
2. Persist the proxy configuration and re-apply it before authorization when needed.
3. Enable exactly one active proxy through the standard TDLib API.
4. Preserve the provider-issued MTProto secret string exactly as issued.
5. Expose or collect TDLib logs during rollout, because stealth activation and fallback decisions are logged.
6. Tolerate lower parallel connection counts than upstream TDLib when stealth is active.

If your application architecture assumes many parallel raw connections to the same endpoint for download/upload throughput, you must revisit those assumptions for stealth-proxy deployments.

---

## Build And Packaging

### Required build inputs

The repository build contract is standard CMake-based TDLib plus the stealth option enabled.

Required components:

1. CMake
2. A C++23-capable compiler
3. OpenSSL
4. zlib
5. gperf

Optional binding outputs:

1. `tdjson` / C interface for FFI-based integrations
2. generated C++ TDLib API bindings
3. JNI bindings if `TD_ENABLE_JNI=ON`
4. .NET bindings if `TD_ENABLE_DOTNET=ON`

### Recommended build command

From repository root:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DTD_ENABLE_BENCHMARKS=OFF \
  -DTDLIB_STEALTH_SHAPING=ON

cmake --build build --parallel 4
```

Notes:

1. `TDLIB_STEALTH_SHAPING` defaults to `ON` in this fork, but integrators should still pass it explicitly in release builds so the contract is visible in CI and packaging scripts.
2. If you are building a distribution artifact for multiple client products, treat `TDLIB_STEALTH_SHAPING=ON` as a release invariant, not a local convenience flag.

### Smoke-test the build

After building:

```bash
cmake --build build --target run_all_tests --parallel 4
./build/test/run_all_tests --filter TlsHello
```

If you want a wider transport sanity pass:

```bash
ctest --test-dir build --output-on-failure -j 4
```

---

## Public Client API Surface

Stealth integration is deliberately routed through existing TDLib proxy objects.

Relevant public API surface:

1. `proxyTypeMtproto secret:string = ProxyType`
2. `addProxy proxy:proxy enable:Bool = AddedProxy`
3. `enableProxy proxy_id:int32 = Ok`
4. `disableProxy = Ok`

If you use the JSON interface, the minimal request shape is:

```json
{
  "@type": "addProxy",
  "proxy": {
    "@type": "proxy",
    "server": "your-proxy.example.com",
    "port": 443,
    "type": {
      "@type": "proxyTypeMtproto",
      "secret": "PASTE_PROVIDER_SECRET_HERE"
    }
  },
  "enable": true
}
```

Important details:

1. The TL schema comment says the MTProto secret is hexadecimal.
2. The implementation currently accepts hexadecimal, Base64URL, and Base64 encodings through `ProxySecret::from_link`.
3. The safest policy for integrators is: **treat the secret as an opaque provider-issued string and pass it through unchanged**.

Do not normalize, lowercase, split, or reconstruct the secret string in UI or middleware layers unless you fully control the server-side generation logic.

---

## MTProto Secret Requirements

### Secrets that activate stealth

Stealth activation happens only for MTProto secrets that satisfy `ProxySecret::emulate_tls()`.

At the raw byte level, that means:

1. first byte `0xee`;
2. followed by a 16-byte MTProto proxy secret;
3. followed by an SNI domain string.

### Domain validation rules

If you generate or validate secrets yourself, the appended TLS-emulation domain must satisfy the library's fail-closed parser:

1. total appended domain length must be between `1` and `182` bytes;
2. labels must be ASCII alphanumeric or `-` only;
3. labels must not start with `-`;
4. labels must not end with `-`;
5. labels must not be empty;
6. each label must be at most `63` bytes.

This means embedded NUL bytes, control bytes, non-ASCII bytes, leading dots, trailing dots, empty labels, and overlong labels are all rejected.

### Secrets that do not activate stealth

These do **not** enter the stealth decorator path:

1. plain 16-byte MTProto secrets;
2. `0xdd` padded MTProto secrets;
3. SOCKS5 proxies;
4. HTTP proxies.

Those routes still work as supported TDLib proxy modes, but they do not get stealth shaping.

---

## What Changes When Stealth Is Active

Once the library is built with stealth enabled and the client uses an `ee...` MTProto proxy secret, the transport stack changes in these ways.

### 1. Browser-like TLS masking is automatic

The library wraps the MTProto obfuscated transport in `StealthTransportDecorator` and automatically selects a browser profile for TLS-emulation mode. Integrators do not choose a profile through public API.

Current runtime behavior is platform-aware and route-aware.

### 2. Connection counts are intentionally capped

For stealth TLS MTProto proxies, the connection-count planner caps session counts to browser-like levels:

1. main sessions: `1`
2. upload sessions: `1`
3. download sessions: `1`
4. small-download lane is merged into download

This is intentional. It reduces proxy-like connection fan-out that would otherwise be visible to DPI or telemetry.

Integrators should expect different bandwidth/concurrency behavior from upstream TDLib in this mode.

### 3. Reconnect pacing and anti-churn are enforced

The flow controller enforces a per-destination budget and minimum reconnect interval using runtime flow behavior policy. Current default policy includes:

1. maximum connects per 10 seconds per destination;
2. minimum reconnect interval;
3. connection lifetime and reuse constraints consumed by connection-pool policy.

Do not write client code that assumes it can force rapid repeated reconnects to the same stealth proxy endpoint without pacing.

### 4. QUIC is disabled by policy

The runtime route-policy validation keeps QUIC disabled. This fork is TCP/TLS-oriented for stealth-proxy mode. Do not design a custom client integration that depends on QUIC/HTTP3 support here.

### 5. Direct connections are unchanged

If the active transport is not MTProto-proxy TLS emulation, `tdlib-obf` behaves like TDLib plus the other hardening changes in this fork, but the stealth masking subsystem itself is not active.

---

## Failure Modes And Logging

Integrators should route TDLib logs somewhere observable during rollout.

Important behaviors:

1. **Build mismatch:** `ee...` secret plus `TDLIB_STEALTH_SHAPING=OFF` causes fatal termination with explicit diagnostics.
2. **Bad proxy secret:** `addProxy` / proxy construction fails with a `400` error.
3. **Runtime config rejection:** if transport stealth config validation fails during decorator construction, TDLib logs a warning and falls back to plain obfuscated MTProto transport for that connection.
4. **Decorator initialization failure:** TDLib logs a warning and falls back to plain obfuscated transport.
5. **Successful activation:** TDLib logs that stealth shaping is enabled for the emulate-TLS transport.

For production integrations, configure `setLogStream` or the equivalent binding-specific log sink before testing stealth deployment.

---

## TLS Trust Store Requirements

Stealth shaping does not remove the need for correct TLS trust handling in your client runtime environment.

Current trust-store behavior in this fork:

1. On non-iOS-family platforms, OpenSSL default cert locations are probed.
2. Environment overrides are supported through:
   1. `SSL_CERT_FILE`
   2. `SSL_CERT_DIR`
   3. `TDLIB_SSL_CERT_FILE`
   4. `TDLIB_SSL_CERT_DIR`
3. On Android, the implementation explicitly probes both:
   1. `/apex/com.android.conscrypt/cacerts`
   2. `/system/etc/security/cacerts`
4. On Apple platforms, trust anchors are loaded through `Security.framework` / keychain APIs.
5. iOS-family platforms intentionally avoid relying on OpenSSL default filesystem bundle probing.

What this means for integrators:

1. If you ship on Linux, Windows, or Android variants with unusual CA layouts, verify trust-store discovery early.
2. If your app sandbox does not expose the platform default bundle path, provide explicit cert overrides where appropriate.
3. If verification is enabled and no trusted certificates are available, this fork now fails closed instead of silently proceeding with an empty trust store.

---

## Runtime Params: Public Contract vs Internal Seam

### What is public today

For normal client integrators, the effective public contract is simple:

1. stealth runtime behavior comes from the compiled default runtime params snapshot;
2. the client does not need to call any stealth-specific API;
3. there is currently **no public `td_api` or `tdjson` option** to point TDLib at a stealth runtime params file or trigger reloads.

### What exists internally

The codebase does contain an internal file-backed runtime params loader:

1. `StealthParamsLoader`
2. `set_runtime_stealth_params`
3. `get_runtime_stealth_params_snapshot`

This is relevant only if you maintain a native fork and want to wire your own advanced embedding seam.

### If you wire the loader yourself

The loader has a strict fail-closed file contract:

1. missing file means use defaults;
2. config must be a regular file;
3. file must be owned by the current user;
4. file must not be writable by group or others;
5. parent directory must be secure;
6. size limit is `64 KiB`;
7. JSON root must be an object with exact schema;
8. `version` must be `1`;
9. after five consecutive reload failures, reload enters a 60-second cooldown;
10. failed reload keeps the last-known-good published snapshot.

There is also a stability constraint: once a successful non-default publication happens, `platform_hints` cannot drift across reloads.

### Minimal internal-only example

If you are embedding at the C++ level and intentionally wiring the loader, a minimal accepted config shape looks like:

```json
{
  "version": 1,
  "profile_weights": {
    "chrome133": 50,
    "chrome131": 20,
    "chrome120": 15,
    "firefox148": 15,
    "safari26_3": 20,
    "ios14": 70,
    "android11_okhttp_advisory": 30
  },
  "route_policy": {
    "unknown": {"ech_mode": "disabled"},
    "ru": {"ech_mode": "disabled"},
    "non_ru": {"ech_mode": "rfc9180_outer"}
  },
  "route_failure": {
    "ech_failure_threshold": 4,
    "ech_disable_ttl_seconds": 600.0,
    "persist_across_restart": true
  },
  "bulk_threshold_bytes": 16384
}
```

If you are not maintaining a native fork, ignore this section and use the compiled defaults.

---

## Recommended Client-Side Integration Checklist

Before shipping `tdlib-obf` in a custom client, verify the following.

1. Your build scripts explicitly pass `-DTDLIB_STEALTH_SHAPING=ON`.
2. Your app can create and enable an MTProto proxy before authorization.
3. Your proxy configuration UI or provisioning path can store a provider-issued MTProto secret string without rewriting it.
4. You understand that only `ee...` TLS-emulation MTProto secrets activate stealth shaping.
5. You do not assume upstream TDLib connection parallelism when stealth proxy mode is enabled.
6. You have a logging path for TDLib warnings and info messages during rollout.
7. You validated certificate/trust-store discovery on each target OS.
8. You do not rely on QUIC/HTTP3 in this fork.

---

## Deployment Checklist

For a concrete rollout with a server such as `telemt`:

1. Obtain the MTProto proxy endpoint, port, and secret from the server operator.
2. Build `tdlib-obf` with `TDLIB_STEALTH_SHAPING=ON`.
3. Integrate through `tdjson` or your binding of choice without adding any stealth-specific public API.
4. Add the MTProto proxy using `proxyTypeMtproto` and enable it.
5. Confirm logs show stealth activation instead of fallback.
6. Run at least the `TlsHello` test slice during CI for the library artifact you ship.

---

## Source Anchors

These are the primary code paths behind the integration contract described above.

1. `CMakeLists.txt`
2. `td/mtproto/IStreamTransport.cpp`
3. `td/mtproto/ProxySecret.h`
4. `td/mtproto/ProxySecret.cpp`
5. `td/telegram/net/Proxy.cpp`
6. `td/generate/scheme/td_api.tl`
7. `td/telegram/net/StealthConnectionCountPolicy.cpp`
8. `td/telegram/net/ConnectionFlowController.cpp`
9. `td/mtproto/stealth/StealthConfig.cpp`
10. `td/mtproto/stealth/StealthRuntimeParams.cpp`
11. `td/mtproto/stealth/StealthParamsLoader.cpp`
12. `tdnet/td/net/SslCtx.cpp`

---

**Status:** Current and code-backed  
**Maintainer:** telemt community
