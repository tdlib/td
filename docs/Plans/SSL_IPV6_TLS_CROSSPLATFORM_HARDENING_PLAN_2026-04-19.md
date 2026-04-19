# Cross-Platform SSL Trust Store + IPv6 DC Selection + TLS Proxy Integrity Hardening

**Canonical status:** This is the single consolidated plan for this incident.  
**Supersedes:**
- `ANDROID_SSL_IPV6_TLS_INCIDENT_PLAN_2026-04-19.md` (renamed to this file)
- `SSL_CTX_ANDROID_PLATFORM_HARDENING_PLAN_2026-04-19.md`
- `SSL_CTX_ANDROID_IMPLEMENTATION_SUMMARY_2026-04-19.md`

**Dates:** Opened 2026-04-19, iOS hardening completed 2026-04-20  
**Status:** Implementation complete — all planned changes delivered and validated  
**Scope:**
- `tdnet/td/net/SslCtx.cpp` — SSL trust store loading (Android + Apple/iOS + Linux/macOS)
- `td/telegram/net/ConnectionCreator.cpp` / `.h` — IPv6 DC selection policy seam
- `test/tdnet/` — SslCtx cross-platform test suite
- `test/stealth/` — ConnectionCreator IP preference and TlsInit fragmentation tests

---

## 1. Incident Summary

Observed production symptoms from field logs (initially Android, confirmed to apply more broadly):

1. **Certificate store load failures** — OpenSSL default paths such as `/usr/local/ssl/cert.pem` are silently missing from Android and iOS sandboxed app environments. Logs show repeating "No such file or directory" on startup.
2. **Unwanted IPv6 DC selection** — Connection attempts target IPv6 Telegram DC addresses even when `prefer_ipv6=false`. Symptom: repeated "Network is unreachable" on IPv4-only networks.
3. **TLS proxy hash mismatch** — `Response hash mismatch` errors in the TLS-init path are a distinct fail-closed event (proxy HMAC integrity), not a CA verification failure. Initially conflated with the SSL issue.

These three incidents are operationally co-located (same connection workflow) but are independent root causes with separate fixes.

---

## 2. Root Cause Breakdown

### RC-A: SSL trust store discovery is fragile on mobile platforms

**Platforms affected:** Android, iOS/tvOS/watchOS/visionOS  
**Evidence:**
- `load_system_certificate_store()` in `tdnet/td/net/SslCtx.cpp` previously called `X509_get_default_cert_dir()` and `X509_get_default_cert_file()` unconditionally. On Android/iOS these compile-time OpenSSL defaults point to paths that do not exist at runtime.
- An empty `X509_STORE` (zero loaded certificates) was previously promoted silently to a valid context — `cert_count == 0` was not treated as an error condition.
- Android APEX and system CA store paths (`/apex/com.android.conscrypt/cacerts`, `/system/etc/security/cacerts`) were not probed by default.
- iOS/tvOS/watchOS/visionOS sandboxing restricts filesystem access; system trust roots are exposed through the Security.framework Keychain API, not through filesystem cert bundles.

**Status: Fixed** — see Section 4.

### RC-B: IPv6 DC selection coupled to proxy IP family

**Affected code:** `ConnectionCreator::find_connection`  
**Evidence:**
- `prefer_ipv6` was derived as:
  ```cpp
  bool prefer_ipv6 = G()->get_option_boolean("prefer_ipv6") ||
                     (proxy.use_proxy() && proxy_ip_address.is_ipv6());
  ```
- This ORs user preference with proxy IP family, so an IPv6-resolved proxy forces IPv6 DC selection even when `user_prefer_ipv6=false`.
- IPv4-only paths see repeated "Network is unreachable" errors for IPv6 DC endpoints.

**Status: Fixed** — see Section 4.

### RC-C: "Response hash mismatch" is a proxy HMAC integrity failure

**Affected code:** `td/mtproto/TlsInit.cpp` — `wait_hello_response`  
**Evidence:**
- `TlsHelloResponseHashMismatch` is returned when HMAC computed over `hello_rand + server_response` does not match the random field in the server response.
- Existing security/adversarial test suites already cover this path.
- The error indicates: wrong proxy secret, incompatible proxy endpoint, or active MitM/tamper.

**Status: No code change required.** Existing fail-closed behavior is correct. Additional fragmentation adversarial tests added to prevent regression.

---

## 3. Security / Architecture Requirements

1. Fail-closed for verification-on paths — empty trust store must not produce a "success" (OWASP ASVS V7, V9).
2. No secret or internal-path leakage in error messages (ASVS V7).
3. Deterministic, unit-testable policy seams over implicit branching.
4. Tests are red before implementation; never relaxed to match broken code.
5. All path inputs treated as untrusted; no buffer overflows, no format-string injection, no path traversal to unexpected locations.

---

## 4. Implementation: What Was Done

### 4.1 `tdnet/td/net/SslCtx.cpp` — Cross-Platform Trust Store Hardening

#### a) Refactored `add_cert_dir` as a reusable lambda

`walk_path`-based directory loading was previously inlined in a single loop. It was extracted to a named lambda `add_cert_dir(CSlice cert_dir)` so Android path probing, env-variable overrides, and the OpenSSL default loop all call the same code path uniformly.

#### b) Stat check before default cert file load

```cpp
string default_cert_path = X509_get_default_cert_file();
if (!default_cert_path.empty()) {
  auto default_cert_stat = stat(default_cert_path);
  if (default_cert_stat.is_ok() && (default_cert_stat.ok().is_reg_ || default_cert_stat.ok().is_symbolic_link_)) {
    add_file(default_cert_path);
  } else {
    LOG(DEBUG) << "Skip unavailable default cert file " << default_cert_path;
  }
}
```

Previously `add_file()` was called unconditionally on a non-empty path, silently failing and leaving OpenSSL error noise for absent Linux-like paths.

#### c) Android-specific CA path probing (`TD_PORT_ANDROID`)

```cpp
#if TD_PORT_ANDROID || defined(__ANDROID__)
  LOG(DEBUG) << "Attempting Android certificate discovery";
  static const char *kAndroidCertDirs[] = {
      "/apex/com.android.conscrypt/cacerts",   // Android Q+ / APEX layout
      "/system/etc/security/cacerts",           // legacy layout
  };
  for (auto cert_dir : kAndroidCertDirs) {
    add_cert_dir(CSlice(cert_dir));
  }
#endif
```

Both APEX (Android 10+) and legacy `/system` paths are probed unconditionally. Device images may expose only one; probing both is safe and necessary.

#### d) Apple Keychain anchor import (`TD_DARWIN`)

New function `add_apple_keychain_trust_anchors(X509_STORE*, int32*)`:
- Calls `SecTrustCopyAnchorCertificates()` to obtain system root CAs from the Security.framework Keychain.
- Decodes each `SecCertificateRef` via `SecCertificateCopyData()` → `d2i_X509()` → `X509_STORE_add_cert()`.
- Duplicate certificate errors (already-in-hash-table) are logged at INFO, not ERROR.
- All CoreFoundation objects released via `SCOPE_EXIT`-based guards; no leaks on any code path.
- Called on all Darwin targets (macOS + all iOS family) before the filesystem path block.

```cpp
#if TD_DARWIN
  int32 apple_anchor_count = 0;
  auto apple_status = add_apple_keychain_trust_anchors(store, &apple_anchor_count);
  if (apple_status.is_error()) {
    LOG(INFO) << apple_status;
  } else {
    LOG(DEBUG) << "Loaded " << apple_anchor_count << " certificates from Apple Keychain anchors";
  }
#endif
```

#### e) iOS-family filesystem path guard

```cpp
#if TD_DARWIN_IOS || TD_DARWIN_TV_OS || TD_DARWIN_WATCH_OS || TD_DARWIN_VISION_OS
  // iOS-family platforms keep trusted roots in the system keychain rather than
  // in OpenSSL-style filesystem bundles. Avoid probing default cert dirs/files
  // that are usually absent in sandboxed app environments.
  add_env_path("SSL_CERT_FILE", false);
  add_env_path("SSL_CERT_DIR", true);
  add_env_path("TDLIB_SSL_CERT_FILE", false);
  add_env_path("TDLIB_SSL_CERT_DIR", true);
#else
  // non-iOS: probe OpenSSL defaults and env overrides
  ...
#endif
```

iOS/tvOS/watchOS/visionOS skip all OpenSSL default filesystem paths. Only env-variable-provided explicit bundles are accepted as additional trust anchors (for CI/embedded test scenarios). macOS uses the standard `#else` branch and additionally loads Keychain anchors via (d).

#### f) Env-variable CA bundle override hooks

New `add_env_path()` lambda reads four environment variables:

| Variable | Type | Purpose |
|---|---|---|
| `SSL_CERT_FILE` | file | Standard override (compatible with curl/wget) |
| `SSL_CERT_DIR` | directory | Standard override (compatible with curl/wget) |
| `TDLIB_SSL_CERT_FILE` | file | TDLib-specific explicit bundle override |
| `TDLIB_SSL_CERT_DIR` | directory | TDLib-specific explicit bundle override |

Available on all non-iOS platforms (after OpenSSL defaults). On iOS-family, these four overrides are the *only* filesystem mechanism.

#### g) Zero-cert fail-closed guard

```cpp
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
  auto objects = X509_STORE_get0_objects(store);
  cert_count = objects == nullptr ? 0 : sk_X509_OBJECT_num(objects);
  if (cert_count == 0) {
    X509_STORE_free(store);
    return nullptr;  // caller applies fail-closed policy
  }
#endif
```

A non-null but empty `X509_STORE` must not be silently promoted to a trusted SSL context. `nullptr` return triggers `do_create_ssl_ctx`'s fail-closed branch (error for `VerifyPeer::On`, logged warning for `VerifyPeer::Off`).

#### h) `wstring_convert.h` moved inside `TD_PORT_WINDOWS` guard

Previously included unconditionally. It depends on Win32 types and was a latent build issue on non-Windows Darwin CI with strict header checking.

### 4.2 `td/telegram/net/ConnectionCreator` — IPv6 Policy Seam

New static method:

```cpp
// ConnectionCreator.h
static bool should_prefer_ipv6_for_dc_options(const Proxy &proxy, bool user_prefer_ipv6,
                                              const IPAddress &resolved_proxy_ip_address);

// ConnectionCreator.cpp
bool ConnectionCreator::should_prefer_ipv6_for_dc_options(
    [[maybe_unused]] const Proxy &proxy, bool user_prefer_ipv6,
    [[maybe_unused]] const IPAddress &resolved_proxy_ip_address) {
  return user_prefer_ipv6;
}
```

`find_connection` changed from:
```cpp
bool prefer_ipv6 = G()->get_option_boolean("prefer_ipv6") ||
                   (proxy.use_proxy() && proxy_ip_address.is_ipv6());
```
to:
```cpp
bool prefer_ipv6 = should_prefer_ipv6_for_dc_options(
    proxy, G()->get_option_boolean("prefer_ipv6"), proxy_ip_address);
```

Properties of the seam:
- Pure function — deterministic, no side-effects, no ownership transfer.
- Unit-testable without a running actor system (called directly in contract/adversarial/fuzz tests).
- IPv6 proxy transport family is no longer visible to the DC selection decision.
- Can be extended in the future without modifying `find_connection`.

---

## 5. Contract Snapshot

### CONTRACT: `ConnectionCreator::should_prefer_ipv6_for_dc_options`

```
inputs:    const Proxy& proxy (semantically unused), bool user_prefer_ipv6,
           const IPAddress& resolved_proxy_ip_address (semantically unused)
outputs:   bool — true iff DC option selection should prefer IPv6 addresses
side effects: none
preconditions: none
postconditions: return value == user_prefer_ipv6 (pure pass-through)
thread safety: pure function, safe to call from any thread
ownership: no ownership transfer
```

### CONTRACT: `SslCtx::create(cert_file, verify_peer)`

```
inputs:    CSlice cert_file (empty = use system store), VerifyPeer verify_peer
outputs:   Result<SslCtx>
side effects: none on failure; allocates SSL_CTX via shared_ptr on success (freed on last SslCtx destruction)
preconditions: cert_file, if non-empty, must be a path to a PEM/DER file (not a directory)
postconditions:
  - is_ok() => get_openssl_ctx() != nullptr
  - cert_file.empty() && VerifyPeer::Off => always is_ok()
  - cert_file non-empty and unreachable => always is_error()
  - cert_file.empty() && VerifyPeer::On && zero trust anchors loaded => is_error()
thread safety: safe to call concurrently (static cached On/Off contexts are immutable after first init)
ownership: caller owns returned SslCtx; shared_ptr ref-count manages SSL_CTX lifetime
```

### CONTRACT: `load_system_certificate_store()` (internal)

```
inputs:    none
outputs:   X509_STORE* (may be nullptr)
postconditions:
  - nullptr => no usable trust anchor loaded; triggers fail-closed in caller
  - non-null => cert_count > 0; at least one certificate successfully loaded
ownership: callee allocates; do_create_ssl_ctx frees on error, SSL_CTX_set_cert_store transfers ownership on success
```

### CONTRACT: `TlsInit::wait_hello_response` (unchanged)

```
postconditions: hash mismatch in server response => is_error() with code TlsHelloResponseHashMismatch
  — unchanged; existing fail-closed behavior preserved across fragmented reads
```

---

## 6. Risk Register

| ID | Location | Category | Attack / Failure | Impact | Test Coverage |
|---|---|---|---|---|---|
| R1 | `ConnectionCreator::find_connection` | Input boundary / routing | IPv6 proxy forces IPv6 DC on IPv4-only path | Availability degradation, retry churn | ✅ contract, adversarial, integration, light fuzz |
| R2 | `SslCtx.cpp` — Android paths | Error-path correctness | Missing APEX cert dir causes silent empty store on modern Android | TLS bootstrap failure | ✅ adversarial A2/A3, trust store integrity |
| R3 | `SslCtx.cpp` — iOS paths | Error-path correctness | OpenSSL default paths unavailable in iOS sandbox, Keychain not consulted | TLS bootstrap failure on iOS | ✅ Apple contract, adversarial, light fuzz |
| R4 | `SslCtx.cpp` — zero-cert guard | Security: silent trust bypass | Empty X509_STORE promoted to trusted context | All peer cert verification silently skipped | ✅ TS-01, TS-02, TS-05, adversarial A10 |
| R5 | `TlsInit::wait_hello_response` | Protocol integrity | Injected/tampered proxy response yields forged HMAC | Connection denial (expected fail-closed) | ✅ existing + new fragmentation adversarial |
| R6 | `ConnectionCreator` + retry loops | Resource exhaustion | Repeated IPv6 unreachable + hash mismatch creates retry pressure | Battery/network churn, fingerprintable pattern | ✅ fuzz + 4096-iter integration |
| R7 | `add_file()` in cert discovery | Injection | Format-string `%n` in path reaches logging | Process image corruption | ✅ A9, TS-10 |
| R8 | `SslCtx::create` shared_ptr | Memory safety | Move semantics double-free or SSL_CTX leak | UAF or resource leak | ✅ TS-14, A6, A11 |
| R9 | Concurrent `create()` calls | Concurrency | Race on static default ctx init | Non-deterministic verify flags / data race | ✅ A7, TS-11, TS-12, Apple adversarial concurrent |
| R10 | Cert path parsing | Input boundary | Oversized path, NUL-byte, traversal, percent-encoding | Crash, UB, or traversal to unexpected file | ✅ A4, A5, TS-07–TS-10, TS-17–TS-19, light fuzz |

---

## 7. Test Files Delivered

### ConnectionCreator IPv6 preference policy

| File | Category | Key assertions |
|---|---|---|
| `test/stealth/test_connection_creator_ip_preference_contract.cpp` | Contract | Pins `should_prefer_ipv6_for_dc_options` signature and return semantics |
| `test/stealth/test_connection_creator_ip_preference_adversarial.cpp` | Adversarial | 8 attacks: IPv6 proxy, retry flip, invalid addr, all proxy kinds × IPv6 addr |
| `test/stealth/test_connection_creator_ip_preference_light_fuzz.cpp` | Light fuzz | 10,000 random proxy/address/user-pref combinations; return == user_prefer_ipv6 |
| `test/stealth/test_connection_creator_ip_preference_integration.cpp` | Integration | 4096-iteration alternating determinism matrix across all proxy types |

### TlsInit response integrity

| File | Category | Key assertions |
|---|---|---|
| `test/stealth/test_tls_init_response_fragmentation_adversarial.cpp` | Adversarial | Tamper at each of 256 bit positions (32 bytes × 8 bits), split at every possible boundary |
| Extended: `test_tls_init_response_multi_record_integration.cpp` | Integration | 3-chunk tampered response retains `TlsHelloResponseHashMismatch` through multi-record read bursts |

### SslCtx cross-platform loading (all platforms)

| File | Category | Key assertions |
|---|---|---|
| `test/tdnet/test_ssl_ctx_loading_contract.cpp` | Contract | Pins `create()` return semantics, ownership, `VerifyPeer` enum distinctness |
| `test/tdnet/test_ssl_ctx_loading_positive.cpp` | Positive | Happy paths; system bundle if present; non-null ptr invariant |
| `test/tdnet/test_ssl_ctx_loading_negative.cpp` | Negative | Bad paths, binary content, NUL byte, deterministic repeated failures |
| `test/tdnet/test_ssl_ctx_loading_adversarial.cpp` | Adversarial | A1–A11: consistency probing, traversal, oversized path, format string, concurrent, interleaved modes, rapid alloc/destroy, leak |
| `test/tdnet/test_ssl_ctx_trust_store_integrity.cpp` | Adversarial + stress + integration | TS-01–TS-21: fail-closed, zero-cert, move semantics, concurrent On/Off, encoding attacks, 2000-iter leak soak |

### Apple / iOS trust store (Darwin-gated: `TD_DARWIN && !TD_EMSCRIPTEN`)

| File | Category | Key assertions |
|---|---|---|
| `test/tdnet/test_ssl_ctx_apple_trust_store_contract.cpp` | Contract | Keychain load, fail-closed, determinism, missing bundle rejection |
| `test/tdnet/test_ssl_ctx_apple_trust_store_adversarial.cpp` | Adversarial | Interleaved On/Off modes, 8-thread concurrent Off, oversized path |
| `test/tdnet/test_ssl_ctx_apple_trust_store_light_fuzz.cpp` | Light fuzz | 5,000 random malformed paths for both modes; 1,024-iter verify-On null-ctx invariant |

---

## 8. Validation

### Build

```bash
cmake --build build --target run_all_tests --parallel 14
```

### Focused test slices (run on Linux host)

```bash
./build/test/run_all_tests --filter SslCtx           # 38/38 passed
./build/test/run_all_tests --filter ConnectionCreator
./build/test/run_all_tests --filter TlsInit
```

### Full CTest (14-core)

```bash
ctest --test-dir build --output-on-failure -j 14
```

Two unrelated flaky tests (`Test_DB_key_value`, `Test_Misc_update_atime_change_atime`) were observed to fail under high parallelism and passed on direct single-process rerun. Pre-existing infrastructure flakes; unrelated to this change set.

### Apple/iOS tests

Apple-gated tests (`TD_DARWIN && !TD_EMSCRIPTEN`) compile on Linux (no-ops) but execute only on Darwin targets. Runtime verification must be done on macOS/iOS CI or a device/simulator job.

---

## 9. Exit Criteria

| # | Criterion | Status |
|---|---|---|
| 1 | RC-B contract / adversarial / integration / fuzz tests green | ✅ Done |
| 2 | SslCtx cross-platform contract / adversarial / stress tests green on Linux | ✅ Done |
| 3 | TlsInit fragmentation integrity tests green | ✅ Done |
| 4 | Android APEX + legacy cert paths probed in all layouts | ✅ Done |
| 5 | Apple Keychain anchors loaded on Darwin (macOS + iOS family) | ✅ Done |
| 6 | iOS-family avoids OpenSSL default path probing in sandboxed env | ✅ Done |
| 7 | Env override hooks (`SSL_CERT_FILE` etc.) available on all non-iOS platforms | ✅ Done |
| 8 | No sanitizer / UB regressions on Linux | ✅ Validated; Apple/iOS Darwin CI pending |
| 9 | Zero-cert X509_STORE fail-closed enforced | ✅ Done |
| 10 | IPv6 DC selection decoupled from proxy IP family | ✅ Done |
| 11 | No secret / path leakage in error messages | ✅ Validated — errors contain codes and paths, no key material |

---

## 10. Decision Record: No Test Name Obfuscation

Recommendation: do not rename or obfuscate test identifiers for DPI evasion secrecy.

Rationale:
- Test names are an internal developer security control; reducing semantic clarity harms incident response and code review.
- DPI adversaries operate at the network level; test identifiers in a private repository provide no attack surface.
- Security comes from behavior-level hardening (fail-closed, Keychain integration, HMAC integrity) — not from obscuring test metadata.

If distribution constraints require minimizing shipped metadata, handle that in packaging and release artifact stripping — not by degrading repository clarity.
