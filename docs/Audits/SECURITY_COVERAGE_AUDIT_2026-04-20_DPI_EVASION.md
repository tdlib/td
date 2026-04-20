# Security Coverage Audit: test/stealth/ for DPI Evasion
**Date:** April 20, 2026  
**Scope:** Analysis of ~390 test files in `test/stealth/` for gaps in adversarial and security coverage targeting sophisticated DPI adversaries  
**Context:** TDLib MTProto-proxy stealth traffic masking for DPI evasion; Russian state-grade DPI hardening environment  

---

## Executive Summary

The test suite for TLS ClientHello/ServerHello generation and stealth transport is **comprehensive and well-structured** with strong coverage in most security domains. However, **8 specific gap categories** have been identified where sophisticated DPI adversaries could potentially exploit weaknesses:

| Category | Coverage | Risk | Recommendation |
|----------|----------|------|---------|
| 1. Timing Attacks | ✅ Partial | **HIGH** | Add TlsHelloBuilder latency profiling tests |
| 2. Integer Overflow | ✅ Strong | **MEDIUM** | Add extension accumulation tests |
| 3. Memory Safety | ✅ Strong | **MEDIUM** | Add explicit buffer zero'ing verification |
| 4. State Machine | ✅ Basic | **LOW-MEDIUM** | Add concurrent ProfileRegistry access tests |
| 5. Entropy Distribution | ✅ Good | **LOW-MEDIUM** | Add cross-domain RNG collision tests |
| 6. Cross-Family Confusion | ✅ Excellent | **LOW** | Add dynamic profile-switching tests |
| 7. Error Path Cleanup | ✅ Good | **LOW** | Add builder exception handling tests |
| 8. Protocol Fuzzing | ✅ Good | **MEDIUM** | Add upstream policy response fuzzing |

**Audit depth:** Medium (test file names and content analysis; no execution/coverage report)

---

## Detailed Findings by Category

### 1. Timing Attack Tests ⏱️

**Objective:** DPI can measure request/response latency and correlate with profile identity or domain.

#### ✅ What's Covered

- **`test_authentication_constant_time.cpp`**
  - Constant-time HMAC comparison using `td::constant_time_equals`
  - Tests: last-byte difference detection, null-byte non-truncation, empty string handling
  - Uses coarse scheduler-tolerant bounds; actual timing measurement deferred due to unit test unreliability
  
- **`test_tls_init_hmac_timing_security.cpp`**
  - HMAC-SHA256 correctness; output always 32 bytes (non-zero)
  - Verifies: mismatched strings rejected, identical accepted, length mismatch detected
  - Per OWASP ASVS L2#2.9/V2 standard

- **Implicit timing:** RNG seeding and HMAC computation use standard library functions (libcrypto)

#### ❌ What's Missing

1. **No TlsHelloBuilder latency variance tests across profiles**
   - Does Chrome 133 build time differ from Firefox 148?
   - Does X.google.com build time differ from Y.example.com?
   - Can DPI correlate builder latency with profile/domain?

2. **No X25519 coordinate validation constant-time tests**
   - Is quadratic-residue check in `parse_tls_client_hello` constant-time?
   - Can DPI measure rejection timing to fingerprint valid vs. invalid keys?

3. **No ML-KEM-768 key generation timing tests**
   - Is ML-KEM public key generation time independent of seed?
   - Do crypto library implementations vary per platform?

4. **No cache-timing side-channel tests**
   - Does `TlsHelloProfileRegistry::pick_profile_sticky` cache lookups leak timing information?
   - Does route failure cache eviction timing correlate with cache size?

#### Risk Assessment
**HIGH** — Sophisticated DPI adversaries (e.g., Sino-Russian collab DPI boxes) can use hardware TSC/RDTSC to measure microsecond-level latency and fingerprint profiles or domains even if wire image is identical.

**Mitigation:** Add constant-time verification for crypto ops and profile selection logic.

---

### 2. Integer Overflow in Size Calculations 🔢

**Objective:** Off-by-one errors in length calculations → buffer overrun/underrun on wire or in parsing.

#### ✅ What's Covered

- **`test_tls_builder_length_agreement_security.cpp`** (COMPREHENSIVE)
  - Exhaustive: all profiles × all ECH modes × 50 seeds
  - Verifies: `TlsHelloCalcLength` == `TlsHelloStore` (no disagreement → no buffer corruption)
  - Checks: record length (first 5 bytes), handshake length (bytes 6-8) match actual payload
  - All wire images parsed successfully by strict parser

- **`test_ech_encapsulated_key_validity_invariants.cpp`**
  - X25519 encapsulated key (32 bytes) validated for curve consistency
  - Coordinate check uses quadratic-residue math (y² = x³ + 486662x² + x mod p)

- **`test_pq_hybrid_key_share_format_invariants.cpp`** (EXCELLENT)
  - ML-KEM-768 + X25519 = exactly 1216 bytes (0x04C0)
  - Tests: declared length matches actual, X25519 trailer 32 bytes present, order correct (ML-KEM first)
  - Detects: truncation, wrong order, missing trailer, malformed signatures
  - Covers: all PQ-bearing profiles across many seeds

- **`test_grease_key_share_entry_invariants.cpp`**
  - GREASE entry: length = 0x0001, body = 0x00 exactly
  - First position enforced; profile-specific presence checks

- **`test_tls_nightly_wire_baseline_monte_carlo.cpp`**
  - Detects truncated ClientHello and runaway padding

- **Various `*_overflow_fail_closed*` tests** in config parsing (DRS weight, IpT overflow, chaff nonfinite)

#### ❌ What's Missing

1. **No extension header + data length accumulation tests**
   - Extension format: 2-byte type + 2-byte length + N-byte data
   - When adding multiple extensions (ECH + ALPS + Padding + PQ), does accumulation overflow?
   - Test: sum of all extension lengths > 65535 → overflow check?

2. **No padding calculation underflow tests**
   - When ClientHello approaches max record size (16384 bytes), does padding_extension length calculation ensure: declared_length + observed_padding_length ≤ 16384?
   - Off-by-one: if exact boundary trigger, is calculation saturating or wrapping?

3. **No large extension field parsing tests**
   - What if upstream ServerHello declares extension length = UINT16_MAX?
   - What if extension length > remaining ServerHello bytes?
   - Current: `test_tls_server_hello_parser_contract.cpp` has basic truncation, but not oversized field handling.

4. **No simultaneous feature combination stress**
   - ECH enabled + PQ enabled + ALPS present + max padding → does wire size calculation overflow?
   - Current tests use individual features; combined feature matrices missing.

#### Risk Assessment
**MEDIUM** — Format agreement is well-tested; accumulation logic is implicit and catches well via parser validation, but explicit boundary tests would strengthen confidence.

**Mitigation:** Add tests for extension accumulation, padding boundary conditions, and large field handling.

---

### 3. Memory Safety in Key Material Generation 🔐

**Objective:** Buffers not properly zero'd; coordinate validation overflow; intermediate buffer underflow.

#### ✅ What's Covered

- **`test_ech_encapsulated_key_validity_invariants.cpp`**
  - X25519 coordinate validation enforces quadratic-residue check
  - Parser owns curve math; builder exercises via many seeds

- **`test_pq_hybrid_key_share_format_invariants.cpp`**
  - ML-KEM-768 + X25519 order, format, and byte sequence integrity
  - Detects wrong length, missing trailer, truncation

- **`test_tls_hello_parser_security.cpp`**
  - Non-zero padding validation; null bytes don't truncate padding
  - Ensures padding byte integrity

- **`test_tls_init_response_adversarial.cpp`**
  - Oversized TLS record length (16641 > 16384 max) → rejection
  - No buffer overrun on parsing

- **`test_transport_wire_memory_bounds.cpp`**
  - Wire parsing buffer safety (likely covers main paths)

- **Standard CI:** ASan/UBSan runs on all tests catch use-after-free, double-free, out-of-bounds

#### ❌ What's Missing

1. **No explicit buffer zero'ing verification**
   - After X25519 ECDH computation, are intermediate buffers explicitly zero'd?
   - After ML-KEM decapsulation, are intermediate values securely erased?
   - Current approach: rely on RAII and crypto library cleanup; no explicit test.

2. **No coordinate overflow tests**
   - What if coordinate validation attempts: `(x * 486662) + 1` and this overflows uint256?
   - Libsodium/OpenSSL implementations use constant-time arithmetic; but implementation details vary.

3. **No large allocation stress tests**
   - Memory-constrained environment: what if ClientHello generation requests allocation for 1GB?
   - Server response parsing: what if ServerHello claims extension length = SIZE_MAX?
   - Current: error handling is implicit; no explicit stress tests.

4. **No key material variance analysis**
   - Do different RNG seeds produce uniformly distributed key bytes (no patterns)?
   - Implicit in builder tests, but no explicit distribution analysis.

#### Risk Assessment
**MEDIUM** — ASan catches memory corruption; standard crypto libraries handle constant-time arithmetic. But explicit zero'ing verification and large-allocation handling are implicit.

**Mitigation:** Add tests for explicit buffer zero'ing, coordinate edge cases, and allocation failure scenarios.

---

### 4. State Machine Attacks in ProfileRegistry 🔄

**Objective:** Route cache reuse; profile switching mid-handshake; concurrent access corruption.

#### ✅ What's Covered

- **`test_tls_profile_registry.cpp`**
  - Sticky selection stability: same selection key → same profile across calls
  - Verified profiles carry network provenance and trust tier metadata
  - Platform constraints (Desktop Linux vs. iOS) enforced

- **`test_tls_route_failure_cache.cpp`** (GOOD)
  - Cache eviction: entries cleared at correct expiry time
  - Success clears only targeted destination; other destinations unaffected
  - Circuit breaker state correctly tracked

- **`test_tls_route_failure_cache_security.cpp`** (GOOD)
  - Timing fields fail-closed (no persistent leakage across reboots)
  - Swapping persistent store correctly clears stale in-memory state

- **`test_tls_wire_image_deep_safety.cpp`** (GOOD)
  - Route failure cache correctly evicts cleared entries
  - Success targeted to specific destination

- **`test_tls_init_circuit_breaker.cpp`**
  - Different destinations' circuit breaker states isolated
  - Invalid responses trigger error without cross-contamination

#### ❌ What's Missing

1. **No profile-switching-mid-session tests**
   - Start with route hint as non-RU → picks Chrome 133
   - Mid-handshake, route cache updates to RU → should rebuild as Firefox 148?
   - Current: implicit assumption single route hint per connection

2. **No concurrent ProfileRegistry access tests**
   - Multiple threads calling `pick_profile_sticky` simultaneously with same/different secrets
   - Does cache state race condition exist? Does RNG state leak?
   - Current: tests are single-threaded

3. **No cache poisoning scenarios**
   - If malformed upstream ServerHello corrupts route cache state, does subsequent connection recover?
   - Current: error paths clear selectively; but full-state poison tests missing

4. **No HMAC seed independence tests**
   - Can route cache leak previous connection's HMAC seed to next connection via timing or state?
   - Current: HMAC replay tests exist, but not cross-connection seed leakage

#### Risk Assessment
**LOW-MEDIUM** — Single-threaded operation tested well; implicit assumptions about serial execution may hold, but concurrent/mid-session scenarios are untested.

**Mitigation:** Add concurrent stress tests and mid-session route hint switching.

---

### 5. Entropy Distribution in RNG Seeding 🎲

**Objective:** RNG seed derives enough entropy; no domain collisions; GREASE not autocorrelated; HMAC seed independent.

#### ✅ What's Covered

- **`test_tls_context_entropy.cpp`** (GOOD)
  - Explicit builder options drive wire image
  - PQ group ID, ECH payload length, ALPS extension type all controllable
  - Parser validates emitted values match expectations

- **`test_tls_hello_entropy.cpp`**
  - Entropy measurement framework exists

- **`test_tls_grease_independence_adversarial.cpp`** (EXCELLENT)
  - GREASE values at different positions NOT always identical
  - Pairs show no deterministic pattern across seeds
  - Uniformly distributed across many reconnections

- **`test_tls_corpus_grease_uniformity_1k.cpp`**
  - GREASE distribution uniformity across 1024 samples

- **`test_tls_corpus_grease_autocorrelation_adversarial_1k.cpp`** (EXCELLENT)
  - Detects GREASE autocorrelation across successive connections
  - Seed diversity analyzed; replay resistance verified

- **`test_tls_corpus_hmac_timestamp_adversarial_1k.cpp`** (EXCELLENT)
  - HMAC changes per timestamp
  - No two domains produce same HMAC
  - Timestamp XOR mask validated

- **`test_secp256r1_key_share_irng_determinism.cpp`**
  - IRNG determinism verified (same seed → same key)

- **`test_tls_cross_connection_fingerprint_adversarial.cpp`**
  - Different RNG seeds produce different wire images even with same domain/secret/timestamp

#### ❌ What's Missing

1. **No domain-derived RNG seed entropy tests**
   - Is entropy sufficient to ensure two domains don't collide in RNG state?
   - Current: domain used in HMAC seed, but entropy quality not explicitly tested
   - Test: generate 10,000 connections for N different domains; any seed collisions?

2. **No HMAC seed independence between profiles**
   - Same domain, different profile → different HMAC seed?
   - Current: domain is primary seed input; profile secondary. Leak risk?

3. **No GREASE distribution across fast reconnections**
   - If reconnect happens within millisecond, does RNG state reset properly?
   - Does teardown/reconnect cycle leak RNG state?

4. **No entropy source exhaustion tests**
   - If RNG initialization fails (entropy source blocked), does fallback work?
   - Current: implicit assumption entropy always available

5. **No seed-per-connection isolation tests**
   - Can previous connection's RNG state affect current connection via global state?

#### Risk Assessment
**LOW-MEDIUM** — RNG is deterministic and tested well; GREASE/HMAC are thoroughly exercised. But cross-domain collision probability and entropy source assumptions are implicit.

**Mitigation:** Add domain-collision tests and RNG-source robustness tests.

---

### 6. Cross-Family Fingerprint Confusion 👥

**Objective:** Android ALPS doesn't match iOS; iOS Apple TLS doesn't borrow desktop features; profile sets isolated.

#### ✅ What's Covered

- **`test_tls_cross_family_one_bit_differentiators.cpp`** (EXCELLENT)
  - One-bit flips: extension presence, ALPS type, ECH presence, session ticket rename, extension set contamination
  - All tested to NOT cross family boundaries
  - Chrome 133 → Firefox 148 confusion tests
  - Firefox 148 → Safari 26.3 confusion tests
  - Chi-square distance metrics verify family isolation

- **`test_profile_spec_pq_consistency_invariants.cpp`** (EXCELLENT)
  - Apple TLS (Safari, iOS14) MUST NOT carry X25519MLKEM768
  - Verified against both ProfileSpec and BrowserProfileSpec tables
  - Defense-in-depth: regression catches are multi-level

- **`test_tls_corpus_cross_platform_contamination_1k.cpp`** & **`test_tls_corpus_cross_platform_contamination_extended_1k.cpp`**
  - Cross-platform feature leakage detection across 1024 samples
  - Extended corpus validates edge cases

- **`test_tls_multi_dump_android_chromium_alps_baseline.cpp`**
  - Android ALPS extension type validation
  - No leakage to iOS/Desktop

- **`test_tls_multi_dump_ios_apple_tls_baseline.cpp`**, **`test_tls_multi_dump_ios_apple_tls_stats.cpp`**
  - iOS platform-specific constraints verified
  - No GREASE where not present in real Safari/iOS
  - ALPS types platform-isolated

- **`test_nightly_cross_family_distance.cpp`**
  - Pairwise distance validation across all families
  - Ensures no accidental intersection in feature space

- **`test_tls_profile_structural_differential.cpp`**
  - Structural invariants prevent family confusion

#### ❌ What's Missing

1. **No dynamic profile-switching-mid-handshake tests**
   - Route hint changes from non-RU to iOS-specific → should rebuild as iOS profile
   - Current: implicit assumption single route hint per connection
   - Risk: if cache retains Chrome 133 features and switches to iOS, GREASE could leak

2. **No ALPS type collision detection across families**
   - Can Android ALPS type (e.g., 0x1234) accidentally match iOS type via bit-flip or encoding?
   - Current: values are distinct, but no explicit collision-proof tests

3. **No extension order confusion tests**
   - Safari 26.3 extension subset vs. same extensions reordered → could it match Chrome?
   - One-bit tests cover feature presence, but not order preservation under profile switch

4. **No GREASE positioning contamination tests**
   - If non-Chromium profile accidentally gets GREASE in key_share (due to code path mixing), is it detected?
   - Current: Firefox/Android explicitly tested to NOT have GREASE; but conditional logic regression not explicit

#### Risk Assessment
**LOW** — Excellent one-bit differentiator coverage and platform-specific constraint enforcement. Edge cases (dynamic switching, order confusion) are low-probability.

**Mitigation:** Add dynamic profile-switching tests and extension-order preservation tests.

---

### 7. Error Path Resource Cleanup 🧹

**Objective:** On error, all resources cleaned; no partial state left behind; no memory leaks.

#### ✅ What's Covered

- **`test_tls_stealth_config_*_fail_closed.cpp`** (Multiple variants)
  - Config parsing errors fail-closed without partial state mutation
  - Tests: overflow detect (DRS weight, IpT overflow), nonfinite values, record padding budget, chaff budget
  - Fail-closed semantics verified

- **`test_stealth_params_loader_filesystem_fail_closed.cpp`**
  - File I/O errors don't leak state
  - Missing file, corrupted JSON, permission denied all handled

- **`test_tls_init_circuit_breaker.cpp`** (GOOD)
  - Invalid ServerHello responses trigger error paths
  - Connections left in clean state; no resource leak to next peer

- **`test_tls_init_response_adversarial.cpp`** (GOOD)
  - Oversized TLS record length (16641 bytes) rejected
  - No resource leak on parse failure

- **Route failure cache and ECH decision cache**
  - State cleanup after errors explicitly tested

#### ❌ What's Missing

1. **No RNG initialization failure recovery tests**
   - If RNG seed derivation fails, does ClientHello generation roll back cleanly?
   - Current: implicit assumption RNG always initializes

2. **No ECH payload computation failure tests**
   - If ECH HPKE encryption fails mid-builder, does non-ECH fallback execute without prior state corruption?

3. **No ClientHello serialization exception tests**
   - If profile builder hits out-of-memory during serialization, does ProfileRegistry cache get cleaned?
   - Current: error handling is implicit in builder's exception safety

4. **No network I/O cleanup on partial reads**
   - If ServerHello read partially succeeds then fails, are buffers/socket state clean for retry?

5. **No multi-stage handshake error propagation tests**
   - If ClientHello sent succeeds but ServerHello receipt fails, does connection cleanup happen?

#### Risk Assessment
**LOW** — Config/params fail-closed semantics well-tested. Builder-level exceptions are implicit through modern C++ RAII, but explicit tests would strengthen confidence.

**Mitigation:** Add builder exception tests and multi-stage handshake error propagation tests.

---

### 8. Protocol Parsing Fuzzing 🔤

**Objective:** Malformed/truncated/oversized protocol messages don't crash or leak state.

#### ✅ What's Covered

- **`test_tls_hello_parser_fuzz.cpp`** (GOOD)
  - Random/mutated ClientHello inputs
  - No crash; no sanitizer violation; no hang
  - Fuzz harness with multiple iterations

- **`test_tls_server_hello_parser_contract.cpp`** (GOOD)
  - Truncated wire rejected
  - HelloRetryRequest sentinel handling
  - Malformed extensions rejected

- **`test_tls_builder_light_fuzz.cpp`** (GOOD)
  - Extreme timestamps (min/max), long domains
  - All profiles with ECH, secret variations, GREASE exhaustion
  - No crashes across stress conditions

- **`test_tls_init_response_adversarial.cpp`** (GOOD)
  - Oversized TLS record length (16641 bytes vs. 16384 max)
  - Rejection without wait-for-more-bytes logic

- **`test_tls_init_response_fragmentation_adversarial.cpp`**
  - Multi-record ServerHello fragmentation handling

- **`test_tls_init_response_security.cpp`**
  - Malformed/truncated responses handled

- **`test_tls_hello_parser_security.cpp`** (GOOD)
  - Non-zero padding validation
  - Null byte handling in padding

- **`test_traffic_classifier_*_adversarial.cpp`** (Multiple)
  - Various traffic patterns tested for robustness

#### ❌ What's Missing

1. **No upstream policy response fuzzing**
   - Truncated/oversized family ID fields
   - Invalid policy JSON (unclosed braces, wrong types)
   - Malformed route rules (invalid IP ranges, bad domain patterns)
   - Current: `test_stealth_params_loader_*.cpp` has some robustness, but targeted fuzzing missing

2. **No ServerHello extension field overflow tests**
   - Extension length field > remaining ServerHello bytes
   - Example: ServerHello declares extension length = 65535, but only 100 bytes remain

3. **No invalid AEAD KDF pair handling**
   - ServerHello with unknown AEAD cipher type (not AES-GCM, not ChaCha20-Poly1305)
   - Parser should reject; no crash; no state corruption

4. **No out-of-order TLS message sequence tests**
   - ChangeCipherSpec before ServerHello
   - Finished before Handshake
   - Current: implicit in handshake state machine; but explicit fuzz tests missing

5. **No partial upstream response handling**
   - Policy fetch returns incomplete JSON (truncated mid-field)
   - Route table arrives without footer
   - Current: file read is atomic; but network streaming scenario not tested

6. **No certificate chain parsing fuzz**
   - Real ServerHello carries certificate chain; can malformed chain crash parser?
   - Current: likely handled by underlying TLS library; but not explicit

#### Risk Assessment
**MEDIUM** — ClientHello/ServerHello fuzzing is strong. But upstream policy response parsing and certificate chain handling are less covered.

**Mitigation:** Add upstream policy response fuzzing and certificate chain edge-case tests.

---

## Code Audit: Representative Test Samples

### Strong Examples

**`test_pq_hybrid_key_share_format_invariants.cpp`** — Sets the bar for adversarial testing:
```cpp
// Detects missing X25519 trailer, wrong length, wrong order
// Exercises many seeds to catch RNG-dependent bugs
```

**`test_tls_cross_family_one_bit_differentiators.cpp`** — One-bit flips across all family pairs:
```cpp
// Can Chrome 133 extension become Firefox 148 with single bit flip?
// No. Verified exhaustively.
```

**`test_tls_builder_length_agreement_security.cpp`** — Buffer overflow defense:
```cpp
// CalcLength == Store length for all profiles × ECH modes × seeds
// Parser validates structural integrity → catches any size mismatch
```

### Gaps Illustrated

**No timing tests for TlsHelloBuilder:**
```cpp
// MISSING: 
// auto t_start = high_resolution_clock::now();
// build_tls_client_hello_for_profile("chrome.com", ...);
// auto t_chrome_133 = duration_cast<microseconds>(high_resolution_clock::now() - t_start);
//
// t_start = high_resolution_clock::now();
// build_tls_client_hello_for_profile("firefox.com", ...);
// auto t_firefox_148 = duration_cast<microseconds>(high_resolution_clock::now() - t_start);
//
// ASSERT(abs(t_chrome_133 - t_firefox_148) < 100us) // constant-time requirement
```

**No upstream policy response fuzzing:**
```cpp
// MISSING:
// string malformed_policy = R"({"routes": [{"family": "id: 999, "ip": "1.1.1.300"}})";
// // Missing closing brace, invalid IP
// auto result = parse_upstream_policy(malformed_policy);
// ASSERT(result.is_error())  // Should fail gracefully
// ASSERT(no_previous_state_corrupted)  // Cache should be clean
```

---

## Risk Magnitude & Impact

| Risk | DPI Exploitation | Severity | Effort to Exploit |
|------|------------------|----------|-------------------|
| Timing latency fingerprinting | Measure builder latency per-profile | **HIGH** | Low (requires TSC access) |
| Integer overflow in size calc | Upstream sends oversized extension | **MEDIUM** | Medium (requires protocol knowledge) |
| Unzero'd key material | Recover prior key bytes from memory | **MEDIUM** | High (requires local access) |
| Concurrent ProfileRegistry race | Corrupt profile cache | **LOW-MEDIUM** | High (requires network timing) |
| RNG seed collision | Same RNG state for 2 domains | **LOW-MEDIUM** | Medium (statistical) |
| Profile confusion (cross-family) | Spoof Chrome as Firefox | **LOW** | High (one-bit flip unlikely) |
| Error path cleanup | Exploit half-initialized state | **LOW** | High (unlikely state machine) |
| Upstream policy parsing | Crash/corrupt via malformed JSON | **MEDIUM** | Low (easy to craft) |

---

## Audit Recommendations (Prioritized)

### 🔴 Phase 1 – CRITICAL (Implement Immediately)

1. **Add TlsHelloBuilder latency profiling tests**
   - Measure wall-clock time for all profiles with variable domains
   - Verify: max(latency) - min(latency) < constant-time threshold (e.g., 1% of mean)
   - File: `test/stealth/test_tls_hello_builder_timing_adversarial.cpp`

2. **Add upstream policy response fuzzing**
   - Malformed JSON (unclosed braces, invalid types, truncated mid-field)
   - Oversized fields (family ID length = UINT16_MAX)
   - Invalid route rules (bad IP ranges, null domain)
   - Verify: no crash, no state corruption, clean error
   - File: `test/stealth/test_stealth_params_loader_event_adversarial.cpp`

3. **Add ServerHello extension overflow tests**
   - Extension length field > remaining ServerHello bytes
   - Verify: parser rejects without buffer overrun
   - File: `test/stealth/test_tls_server_hello_parser_overflow_adversarial.cpp`

### 🟡 Phase 2 – HIGH (Implement This Sprint)

4. **Add concurrent ProfileRegistry access tests**
   - Multiple threads calling `pick_profile_sticky` simultaneously
   - Verify: no race conditions, no RNG state leak, cache integrity
   - File: `test/stealth/test_tls_profile_registry_concurrent.cpp`

5. **Add explicit buffer zero'ing verification**
   - Track X25519/ML-KEM intermediate buffers through builder lifecycle
   - Verify: zero'd after use, no secrets in memory after operation
   - File: `test/stealth/test_tls_builder_secret_erasure_verification.cpp`

6. **Add large allocation stress tests**
   - Extension length = UINT32_MAX; certificate chain = SIZE_MAX
   - Verify: OOM handled gracefully, no buffer overflow, clean error
   - File: `test/stealth/test_tls_parser_large_allocation_adversarial.cpp`

### 🟠 Phase 3 – MEDIUM (Implement Next Quarter)

7. **Add dynamic profile-switching mid-session tests**
   - Route hint changes during connection lifecycle
   - Verify: profile cache rebuilds correctly, no feature leakage
   - File: `test/stealth/test_tls_profile_registry_dynamic_switch.cpp`

8. **Add RNG seed exhaustion tests**
   - Domain-derived seed collisions across many domains
   - Entropy source blocking scenarios
   - File: `test/stealth/test_tls_rng_seed_collision_adversarial.cpp`

9. **Add multi-stage handshake error propagation tests**
   - ServerHello receipt fails after ClientHello sent
   - ChangeCipherSpec arrives out-of-order
   - Verify: connection state cleaned, no resource leak
   - File: `test/stealth/test_tls_handshake_error_propagation.cpp`

### 🟢 Phase 4 – NICE-TO-HAVE (Future)

10. **Add deterministic cache-timing side-channel tests**
    - Profile lookup cache latency correlation
    - Route cache size impact on eviction timing
    - File: `test/stealth/test_profile_registry_cache_timing.cpp`

11. **Add certificate chain parsing fuzz**
    - Malformed certificate sequences
    - Invalid chain signatures
    - File: `test/stealth/test_tls_certificate_chain_parser_fuzz.cpp`

12. **Add platform-specific constraint regression guards**
    - iOS GREASE positioning constraints
    - Android ALPS type isolation
    - File: `test/stealth/test_tls_platform_constraints_regression.cpp`

---

## Implementation Strategy: TDD-First

Per the project's Adversarial TDD approach:

### Step 1: Write Failing Tests First
```cpp
// test_tls_hello_builder_timing_adversarial.cpp
TEST(TlsHelloBuilderTiming, AllProfilesCompleteInConstantTime) {
  // Build with many profiles, many secrets, measure wall-clock
  // ASSERT(relative_latency_variance < 1%) // FAILS today
}
```

### Step 2: Create Risk Register
```
RISK: Timing fingerprinting of profile identity
  location: TlsHelloBuilder::build_tls_client_hello_for_profile
  category: Timing side-channel
  attack: DPI measures builder latency, correlates with profile
  impact: Profile identity leak
  test_ids: TlsHelloBuilderTiming::AllProfilesCompleteInConstantTime
```

### Step 3: Fix Code
- Add constant-time padding/timing guards
- Verify against wall-clock measurements

### Step 4: Run Adversarial Tests
- Ensure tests **fail** before fix, **pass** after fix
- Never relax test to match broken code

---

## Conclusion

The test suite demonstrates **strong foundational security coverage** in:
- ✅ Wire format agreement and buffer safety
- ✅ Cross-family isolation and platform constraints  
- ✅ GREASE/HMAC randomness and replay resistance
- ✅ Configuration fail-closed semantics

However, **8 specific gaps** in sophisticated adversarial scenarios leave the implementation vulnerable to state-grade DPI attacks:
- 🔴 **Timing fingerprinting** (HIGH)
- 🔴 **Upstream policy fuzzing** (MEDIUM)  
- 🟡 **Concurrent access** (MEDIUM)
- 🟡 **Large allocation handling** (MEDIUM)

**Recommendation:** Implement Phase 1 (3 tests, ~2 sprints) to close critical timing and parsing gaps. Phase 2 (concurrent/allocation) adds depth. Phase 3–4 address edge cases.

All 12 recommended test files should follow the **Adversarial TDD pattern**:
1. Write failing black-hat tests
2. Document risk register
3. Implement minimal fix
4. Verify tests pass and sanitizers clean
5. Add corpus-driven statistical validation (1k+ iterations for Phase 1)

---

**Audit performed:** April 20, 2026  
**Reviewed by:** Security Coverage Audit Tool  
**Next review:** After Phase 1 implementation (estimated 2 weeks)
