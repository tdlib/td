<!--
SPDX-FileCopyrightText: Copyright 2026 telemt community
SPDX-License-Identifier: MIT
telemt: https://github.com/telemt
telemt: https://t.me/telemtrs
-->

# Logging Subsystem Hardening Plan (Revised)

**Plan ID:** logging-hardening-2026-05-07-rev3  
**Date:** 2026-05-07  
**Status:** Code-Verified, TDD-Ready  
**Primary Goal:** Remove UB races, spin-contention waste, and silent-failure behavior from logging across both production and active test/benchmark mutation paths, without breaking stealth runtime behavior, browser-mimic invariants, or fixture-grounded DPI-evasion validation.

**Non-negotiable method:** Contract -> Attack -> Red -> Green -> Survive -> Refactor. No implementation change without failing tests first.

---

## 1. Why This Revision Was Needed

The previous draft identified important defects correctly, but some implementation/test details were unsafe or non-portable. This revision is grounded in the current code and test wiring.

| Previous draft issue | Verified repository reality | Correction in this revision |
|---|---|---|
| Required `is_lock_free()==true` for correctness gates | Lock-free is platform-dependent and not required for correctness | Lock-free is treated as optimization telemetry, not a pass/fail invariant |
| Truncation marker write used full buffer indexing | `Logger` flushes `as_cslice()` from `StringBuilder`; writing by full buffer size can corrupt unrelated bytes | Marker policy is defined against the emitted mutable slice only |
| `log_interface` risk was framed as sink destruction/UAF | In production, sink objects are static-lifetime (`default_log`, `ts_log`, `null_log`); race is still UB but not typical UAF | Risk text now matches actual lifetime model and UB impact |
| Affected callsites under-scoped | Production reads/writes are in `check.cpp`, `Logging.cpp`, `StorageManager.cpp`, `FileManager.cpp`, `cli.cpp`, plus macros | Full production dependency inventory is included |
| Test references did not ensure active test target wiring | `test/logging_macro_v501_contract.cpp` is not wired in `test/CMakeLists.txt` | New tests are explicitly planned under `test/` and must be added to `test/CMakeLists.txt` |
| `TsCerr` contention path deferred as out-of-scope | `TsCerr` has the same tight spin-without-backoff pattern as `TsLog` and is used by default stderr logging path | `TsCerr` hardening is now in-scope as a first-class milestone |

---

## 2. Verified Baseline In Code

### 2.1 Confirmed Defects

| ID | Severity | Location | Verified defect |
|---|---|---|---|
| LOG-01 | CRITICAL | `tdutils/td/utils/logging.cpp`, `tdutils/td/utils/logging.h` macros | `log_interface` is raw global pointer read/written concurrently without atomic load/store semantics |
| LOG-02 | CRITICAL | `td/telegram/Logging.cpp`, all `VERBOSITY_NAME(tag)` mutable globals | Tag verbosity gates are mutable non-atomic globals read concurrently via `VLOG` path |
| LOG-03 | IMPORTANT | `tdutils/td/utils/TsLog.cpp` | Busy spin lock loop has no backoff/yield hint |
| LOG-04 | IMPORTANT | `tdutils/td/utils/logging.cpp` `Logger::~Logger()` | Overflow flag is ignored during flush; truncation is silent |
| LOG-05 | IMPORTANT | `td/telegram/Log.cpp` | `fatal_error_callback` is read outside lock as plain function pointer |
| LOG-06 | IMPORTANT | `tdutils/td/utils/TsCerr.cpp` | stderr spin lock loop has no backoff/yield hint under contention |

### 2.2 Production Callsite Inventory (Must Be Covered)

`log_interface` access currently appears in production code at least in:

1. `tdutils/td/utils/logging.h` (`LOG_IMPL` macro path)
2. `tdutils/td/utils/check.cpp`
3. `td/telegram/Logging.cpp`
4. `td/telegram/StorageManager.cpp`
5. `td/telegram/files/FileManager.cpp`
6. `td/telegram/cli.cpp`

If `log_interface` changes to atomic, all production accesses above must migrate in one milestone to avoid mixed unsafe usage.

### 2.3 Non-Production Mutation Surfaces (Must Also Be Covered)

Direct `log_interface` read/write usage also exists in active tests/bench code. If pointer semantics change, these must migrate in the same patchset to keep tests meaningful and avoid accidental bypass of hardened helpers.

1. `tdutils/test/log.cpp`
2. `test/stealth/test_tls_init_log_contract.cpp`
3. `test/stealth/test_stealth_params_loader_reload_log_contract.cpp`
4. `test/stealth/test_stream_transport_activation_fail_closed.cpp`
5. `test/stealth/test_raw_connection_error_contract.cpp`

### 2.4 Existing Coverage Gap

1. `tdutils/test/log.cpp` is benchmark-oriented and does not assert race safety contracts.
2. No existing adversarial/fuzz/stress suite targets `log_interface` swap races, tag race visibility, truncation signaling, or fatal callback concurrency.
3. `test/logging_macro_v501_contract.cpp` is currently not wired into `test/CMakeLists.txt` and does not protect runtime race behavior.
4. No dedicated contract/adversarial/stress coverage currently exists for `TsCerr` contention behavior.

---

## 3. Security And DPI Constraints

The repository operates under active DPI pressure. Logging hardening must not regress transport mimicry validation.

1. Never guess fixtures. Use the real pipeline rooted in:
   - `docs/Samples/Traffic dumps/`
   - `test/analysis/fixtures/`
   - `test/analysis/fixtures/imported/import_manifest.json`
2. Keep ECH/route behavior validation fixture-grounded; do not replace with synthetic-only checks.
3. Logging must remain secret-safe:
   - no proxy secret dumps
   - no private key material
   - no raw sensitive payload echoes

**ASVS alignment focus:**

1. V5 Input validation: reject invalid tag/stream inputs fail-closed.
2. V7 Error handling: no silent truncation.
3. V9 Network/protocol observability: preserve structured public-status logs.
4. V11 Concurrency: no data races in hot logging paths.

---

## 4. Contract Snapshot (Mandatory Before Any Code Changes)

### CONTRACT: Active Log Sink Pointer

1. Read path for every `LOG`/`VLOG` call must be data-race free.
2. Write path (`set_current_stream`, CLI swaps, test overrides) must use synchronized store semantics.
3. Sink object lifetime remains static in production; race fix targets UB elimination, not ownership redesign.

### CONTRACT: Runtime Tag Verbosity

1. Mutable tag levels are runtime-gated shared state.
2. Reads in hot path must be race-free and low overhead.
3. Writes must remain fail-closed and clamped.

### CONTRACT: Logger Truncation Behavior

1. Overflowed message must be explicitly marked in emitted output.
2. Marker insertion must operate on emitted slice bounds only.
3. Marker policy must not allocate on hot path.

### CONTRACT: Fatal Callback Pointer

1. Callback read/write concurrency must be race-free.
2. Null callback path must remain safe and non-calling.

### CONTRACT: TsLog Contention Behavior

1. Lock acquire remains bounded spin semantics.
2. Under contention, loop provides backoff hint to reduce CPU monopolization.

### CONTRACT: TsCerr Contention Behavior

1. stderr lock acquire remains bounded spin semantics.
2. Under contention, loop provides backoff hint to reduce CPU monopolization.
3. stderr output serialization semantics remain unchanged.

---

## 5. Risk Register

```
RISK: LOG-R01
  location: logging.h/logging.cpp + direct production callsites
  category: concurrency race (shared pointer)
  attack: concurrent stream swaps and log emissions trigger UB read/write race
  impact: crash, stale sink visibility, undefined behavior

RISK: LOG-R02
  location: VLOG tag globals + Logging.cpp set/get tag level
  category: concurrency race (shared mutable integer)
  attack: high-frequency log-gate reads race with admin level updates
  impact: UB, gate corruption, unpredictable suppression/enablement

RISK: LOG-R03
  location: TsLog.cpp
  category: resource exhaustion
  attack: sustained multi-thread log storm causes tight spin CPU burn
  impact: latency collapse and throughput starvation

RISK: LOG-R04
  location: Logger::~Logger
  category: error-path integrity
  attack: oversized messages silently truncate during incident response
  impact: incorrect operational diagnostics

RISK: LOG-R05
  location: Log.cpp fatal callback shim
  category: concurrency race (function pointer)
  attack: callback set/clear races with fatal callback invocation path
  impact: UB, stale/null callback behavior

RISK: LOG-R06
  location: stealth and transport log callsites
  category: sensitive data exposure
  attack: future hardening patch accidentally logs secret/protected fields
  impact: telemetry leakage under hostile DPI environment

RISK: LOG-R07
  location: TsCerr.cpp
  category: resource exhaustion
  attack: multithreaded stderr/fatal log pressure spins without backoff
  impact: CPU burn and reduced diagnostic reliability under load

RISK: LOG-R08
  location: test/bench direct log_interface mutation sites
  category: hardening drift
  attack: production hardening lands, tests still mutate raw state via stale pattern
  impact: false confidence, compile drift, or bypassed hardening paths in tests
```

---

## 6. Test Plan (Write First, Make Red First)

All tests are separate files. No inline tests in production files.

### 6.1 Contract Tests

1. `test/logging_stream_pointer_contract.cpp`
2. `test/logging_tag_verbosity_contract.cpp`
3. `test/logging_truncation_contract.cpp`
4. `test/logging_fatal_callback_contract.cpp`
5. `test/logging_tscerr_spin_contract.cpp`
6. `test/stealth/test_logging_secret_hygiene_source_contract.cpp`

Contract assertions include symbol-level behavior pins, fail-closed semantics, and no-secret logging guards.

### 6.2 Adversarial Tests

1. `test/logging_stream_pointer_adversarial.cpp`
2. `test/logging_tag_verbosity_adversarial.cpp`
3. `test/logging_truncation_adversarial.cpp`
4. `test/logging_fatal_callback_adversarial.cpp`
5. `test/logging_spin_adversarial.cpp`
6. `test/logging_tscerr_spin_adversarial.cpp`

Key adversarial scenarios:

1. Multi-thread stream swap storms with concurrent high-volume log emission.
2. Tag level toggle storms against active `VLOG(tag)` gates.
3. Oversized payload logging with marker verification and newline normalization.
4. Callback churn race against callback wrapper invocation.
5. Lock contention bursts for `TsLog` with starvation detection.

### 6.3 Integration Tests

1. `test/logging_stream_pointer_integration.cpp`
2. `test/logging_tag_verbosity_integration.cpp`
3. `test/stealth/test_logging_fixture_route_ech_integration.cpp`

Integration coverage must include real-fixture route/ECH context using imported manifest and reviewed fixtures.

### 6.4 Light Fuzz Tests

1. `test/logging_stream_pointer_light_fuzz.cpp`
2. `test/logging_tag_verbosity_light_fuzz.cpp`
3. `test/logging_truncation_light_fuzz.cpp`

Minimum 10,000 randomized operation steps per fuzz test in CI mode.

### 6.5 Stress Tests

1. `test/logging_spin_stress.cpp`
2. `test/logging_stream_pointer_stress.cpp`
3. `test/logging_tag_verbosity_stress.cpp`
4. `test/logging_tscerr_spin_stress.cpp`

Stress tests must assert functional correctness and bounded completion time under 14-core runs.

### 6.6 Test Wiring Requirement

Every new test file above must be added to `test/CMakeLists.txt` (or `tdutils/CMakeLists.txt` only if intentionally scoped there). No orphan tests.

---

## 7. Implementation Milestones (Only After Red Tests)

### M1. Eliminate `log_interface` race everywhere

**Files:**

1. `tdutils/td/utils/logging.h`
2. `tdutils/td/utils/logging.cpp`
3. `tdutils/td/utils/check.cpp`
4. `td/telegram/Logging.cpp`
5. `td/telegram/StorageManager.cpp`
6. `td/telegram/files/FileManager.cpp`
7. `td/telegram/cli.cpp`
8. `tdutils/test/log.cpp`
9. `test/stealth/test_tls_init_log_contract.cpp`
10. `test/stealth/test_stealth_params_loader_reload_log_contract.cpp`
11. `test/stealth/test_stream_transport_activation_fail_closed.cpp`
12. `test/stealth/test_raw_connection_error_contract.cpp`

**Implementation requirements:**

1. Replace plain shared-pointer reads/writes with explicit atomic load/store semantics.
2. Add dedicated helpers (`load_active_log_interface`, `store_active_log_interface`) and use them uniformly.
3. Do not use fence+plain-pointer pattern.
4. Treat `is_lock_free()` as informational only.
5. Update direct test/bench mutation sites in the same milestone so race tests exercise hardened APIs, not stale raw-state paths.

### M2. Eliminate tag verbosity race

**Files:**

1. `tdutils/td/utils/logging.h`
2. `td/telegram/Logging.cpp`
3. Tag declarations/definitions as required by chosen strategy

**Implementation requirements:**

1. Use one coherent race-free strategy for both read and write path (atomic-backed or `atomic_ref` with validated alignment contract).
2. Preserve existing public tag API and clamping semantics.
3. Ensure `VLOG` hot path remains low-overhead.

### M3. Make truncation explicit and safe

**Files:**

1. `tdutils/td/utils/logging.cpp`

**Implementation requirements:**

1. If `sb_.is_error()` is true, emitted log line must end with a deterministic truncation marker.
2. Marker insertion must be done against the emitted mutable slice bounds, not raw buffer capacity.
3. Preserve newline policy and avoid additional dynamic allocations on hot path.

### M4. Fix fatal callback race

**Files:**

1. `td/telegram/Log.cpp`

**Implementation requirements:**

1. Callback pointer read/write must use atomic semantics.
2. Null callback path must stay fail-safe.
3. Keep current lock ownership for unrelated mutable state intact.

### M5. Reduce TsLog contention burn

**Files:**

1. `tdutils/td/utils/TsLog.cpp`

**Implementation requirements:**

1. Add bounded backoff hint in spin loop.
2. Keep behavior deterministic and signal-safe enough for current usage profile.
3. Do not introduce sleeping that materially increases uncontended latency.

### M6. Reduce TsCerr contention burn

**Files:**

1. `tdutils/td/utils/TsCerr.cpp`

**Implementation requirements:**

1. Add bounded backoff hint in stderr spin loop.
2. Preserve stderr write semantics and lock scope.
3. Keep fatal-path behavior deterministic.

---

## 8. Fixture-Grounded Stealth Regression Gate (Mandatory)

Logging hardening must not regress route/ECH/QUIC mimicry behavior validated on real captures.

Run at least:

1. `python3 test/analysis/run_corpus_smoke.py --registry test/analysis/profiles_validation.json --fixtures-root test/analysis/fixtures/clienthello --server-hello-fixtures-root test/analysis/fixtures/serverhello`
2. `./build/test/run_all_tests --filter TlsHello`
3. `./build/test/run_all_tests --filter ImportedCorpusIntegration`
4. `./build/test/run_all_tests --filter TLS_RouteEchQuicBlockMatrix`
5. `./build/test/run_all_tests --filter LaneH5ArcImportedIntegration`

The imported manifest in `test/analysis/fixtures/imported/import_manifest.json` is the source-of-truth pairing contract.

---

## 9. Verification Checklist

```
[ ] All contract/adversarial/integration/light_fuzz/stress tests exist in separate files
[ ] All new tests are wired into active CMake test targets
[ ] Red-first evidence captured before implementation
[ ] LOG-R01..LOG-R08 each mapped to at least one adversarial test
[ ] No race reports under available sanitizer configuration
[ ] No silent truncation: marker appears on overflow path
[ ] No secrets leak in logging source contracts
[ ] Fixture-grounded smoke and stealth route/ECH gates pass
[ ] No public API breakage for logging consumers
[ ] No TODOs, no suppressed errors, no relaxed tests to force green
```

---

## 10. Noticed But Not Touching (Separate Follow-Up)

1. `test/logging_macro_v501_contract.cpp` is not currently wired and appears to use a different test style than active `run_all_tests` conventions.

---

## 11. Phase 6 — Post-Audit Residual Cleanup

**Date added:** 2026-05-08  
**Status:** Planned — 3 milestones (M7, M8, M9)  
**Trigger:** Architectural audit of the Phase 1–6 implementation surfaced three residual findings that were not in scope for M1–M6 but require their own Contract → Attack → Red → Green → Survive cycle.

### 11.1 Findings Summary

| Finding | ID | Severity | Description |
|---|---|---|---|
| F-1 | LOG-R09 | Minor / Inconsistency | `TsCerr` spinlock uses `memory_order::seq_cst` for both `test_and_set` and `clear`; `TsLog` uses canonical `acquire`/`release`. Incorrect ordering semantics on ARM. |
| F-2 | LOG-R10 | Minor / Overhead | `VLOG` hot-path reads `std::atomic<int>` tag globals via implicit `operator int()`, which resolves to `seq_cst`. A level-gate read only needs `memory_order_relaxed`. |
| F-3 | LOG-R11 | Low risk / Survivor | `verbosity_blkch` in `tde2e/td/e2e/TestBlockchain.h` and `.cpp` is still declared as plain `int`. DEFECT-LOG-02 partial survivor. Currently zero-risk (not in registry, no `VLOG(blkch)` call sites exist). Becomes an active LOG-R02-class race if callers are ever added. |

### 11.2 Risk Register Additions

```
RISK: LOG-R09
  location: tdutils/td/utils/TsCerr.cpp:65, 78
  category: unnecessary memory ordering strength / ARM correctness concern
  attack: on ARMv8/weak-memory targets, seq_cst on atomic_flag::clear emits a
          full STLR + DMB ISH barrier sequence; acquire/release is sufficient
          and uses only LDAXR/STLR, halving barrier cost under contention
  impact: 2-5x excess fence overhead on stderr hot path under contention
          on ARM targets; no correctness impact on x86-TSO
  test_ids: LOG-T-09-CONTRACT, LOG-T-09-BUILD

RISK: LOG-R10
  location: tdutils/td/utils/logging.h, LOG_IMPL_FULL macro line 62
  category: unnecessary memory ordering strength / ARM hot-path overhead
  attack: VLOG(tag) expands to LOG_IMPL(DEBUG, tag, ...) which passes
          VERBOSITY_NAME(tag) (an std::atomic<int>) directly as runtime_level
          to LOG_IMPL_FULL; the comparison runtime_level > options.get_level()
          invokes std::atomic<int>::operator int() at seq_cst strength;
          on ARM, relaxed is sufficient — level-gate staleness by a single
          store is harmless (at most one extra or suppressed log line)
  impact: one full seq_cst load per LOG/VLOG call site on every non-stripped
          log statement on ARM targets;  constexpr int levels (FATAL, ERROR,
          WARNING, INFO, DEBUG, NEVER) are unaffected — they are not atomic
  test_ids: LOG-T-10-CONTRACT, LOG-T-10-BUILD

RISK: LOG-R11
  location: tde2e/td/e2e/TestBlockchain.h:26, tde2e/td/e2e/TestBlockchain.cpp:23
  category: plain-int global data race — DEFECT-LOG-02 partial survivor
  attack: verbosity_blkch is extern int; if any future caller adds VLOG(blkch)
          under concurrent set_tag_verbosity_level, the read/write race is UB
  impact: currently zero — no VLOG(blkch) sites, not in log_tags registry;
          becomes active LOG-R02-class data race the moment a caller is added
  test_ids: LOG-T-11-CONTRACT
```

### 11.3 Contract Snapshot

**CONTRACT: TsCerr spinlock memory ordering**

```
CONTRACT: TsCerr::enterCritical spinlock
  acquire path: lock().test_and_set(memory_order_acquire)
  release path: lock().clear(memory_order_release)
  invariant:    seq_cst MUST NOT appear in the spinlock acquire or release paths
  matches:      TsLog canonical acquire/release spinlock pattern
```

**CONTRACT: VLOG level-gate load ordering**

```
CONTRACT: LOG_IMPL_FULL runtime_level read
  read type:    memory_order_relaxed for std::atomic<int> tag globals
  read type:    direct value (no atomic load) for constexpr int static levels
  mechanism:    overloaded load_verbosity_level(int) / load_verbosity_level(const std::atomic<int>&)
                called in the runtime_level > options.get_level() guard
  invariant:    no implicit operator int() seq_cst conversion in LOG_IMPL_FULL
  invariant:    constexpr int levels must compile through load_verbosity_level without
                any atomic include requirement (int overload resolves it)
```

**CONTRACT: verbosity_blkch declaration type**

```
CONTRACT: VERBOSITY_NAME(blkch) global
  declaration:  extern std::atomic<int> verbosity_blkch  (TestBlockchain.h)
  definition:   std::atomic<int> verbosity_blkch{VERBOSITY_NAME(INFO)}  (TestBlockchain.cpp)
  invariant:    type must match all other tag globals converted under M2 / DEFECT-LOG-02
```

### 11.4 Test Plan (Write First, Make Red First)

All tests are separate files. No inline tests in production files.

#### Contract tests

`test/logging_tscerr_ordering_contract.cpp`
- Source-contract: assert `TsCerr.cpp` contains `memory_order_acquire` and `memory_order_release` on the spinlock paths.
- Source-contract: assert `TsCerr.cpp` does **not** contain `memory_order::seq_cst` on any `test_and_set` or `clear` line.
- Build-contract: `TsCerr` still compiles, behavior of enter/exit is unchanged.

`test/logging_vlog_relaxed_contract.cpp`
- Source-contract: assert `logging.h` contains a `load_verbosity_level` overload for `const std::atomic<int>&` returning a `relaxed` load.
- Source-contract: assert `LOG_IMPL_FULL` guard text in `logging.h` contains `load_verbosity_level(runtime_level)` and does **not** contain bare `runtime_level > options`.
- Behavioral contract: invoking `VLOG` with a custom `std::atomic<int>` tag at level 0 compiles and does not emit when log level is below gate.

`test/logging_tde2e_blkch_contract.cpp`
- Source-contract: assert `TestBlockchain.h` contains `std::atomic<int>` for `verbosity_blkch`.
- Source-contract: assert `TestBlockchain.h` does **not** contain bare `extern int verbosity_blkch`.

#### Build-verification tests (compile-error guards)

Add static assertions or build-time checks:

- In `TsCerr.h` or a comment block: document that `seq_cst` must not appear in the spinlock paths — the source contract test enforces this.
- In `logging.h`: the `load_verbosity_level` overloads must be in a visible namespace so the macro can call them without ADL ambiguity.

#### Adversarial tests

`test/logging_tscerr_ordering_adversarial.cpp`
- Verify that high-volume concurrent `TsCerr` write storms complete without deadlock or ordering failure.
- Existing `test/logging_tscerr_spin_adversarial.cpp` already covers contention behavior; this test pins the **ordering contract** variant independently so a future accidental revert to `seq_cst` is caught by both files.

#### Test wiring

All three new test files must be added to `test/CMakeLists.txt` under the `run_all_tests` target. No orphan tests.

### 11.5 Implementation Milestones

#### M7 — Normalize TsCerr spinlock to acquire/release

**Files:**
1. `tdutils/td/utils/TsCerr.cpp`

**Required changes:**
1. Line 65: `lock().test_and_set(std::memory_order::seq_cst)` → `lock().test_and_set(std::memory_order_acquire)`
2. Line 78: `lock().clear(std::memory_order::seq_cst)` → `lock().clear(std::memory_order_release)`

**Verification:** `test/logging_tscerr_ordering_contract.cpp` (new), `test/logging_tscerr_spin_contract.cpp` (existing must still pass), `test/logging_tscerr_spin_adversarial.cpp` (existing must still pass), `test/logging_tscerr_spin_stress.cpp` (existing must still pass).

**Non-goals:** Do not change `TsLog`; do not change `TsCerr` locking scope, yield policy, or `enterCritical` return semantics.

#### M8 — Relaxed VLOG level-gate loads

**Files:**
1. `tdutils/td/utils/logging.h`

**Required changes:**

1. In the `td` namespace (or `td::detail`), add two overloads before `LOG_IMPL_FULL` is used:
   ```cpp
   inline int load_verbosity_level(int level) noexcept {
     return level;
   }
   inline int load_verbosity_level(const std::atomic<int>& level) noexcept {
     return level.load(std::memory_order_relaxed);
   }
   ```
2. In `LOG_IMPL_FULL` (line 62), change:
   ```cpp
   runtime_level > options.get_level()
   ```
   to:
   ```cpp
   ::td::load_verbosity_level(runtime_level) > options.get_level()
   ```

**Design rationale:** `memory_order_relaxed` is correct for a level gate because:
- The gate controls whether a log message is emitted, not whether shared data is safely visible.
- Staleness by one store (enabling or suppressing at most one extra message during a level change) is operationally harmless.
- The `store` path (`store_tag_verbosity_level` and explicit `store` calls) continues to use `memory_order_release`, which is correct and unaffected by this change.
- On x86-TSO, `relaxed` load of `atomic<int>` compiles identically to `seq_cst` load — zero overhead change there.
- On ARMv8/AArch64, `relaxed` load emits `LDR` vs `LDAR` for `seq_cst` — roughly 1/3 the latency per LOG call under sequential consistency budget.

**Constraints:**
- `constexpr int` levels (`FATAL`, `ERROR`, `WARNING`, `INFO`, `DEBUG`, `NEVER`) resolve through the `int` overload — no include or atomic header required for them.
- The `int` overload must be marked `constexpr` if the compiler permits, so `LOG_IS_STRIPPED` compile-time path is not degraded.
- Must not change `LOG_IMPL` or `LOG_IMPL_FULL` signature or arity.
- Must not affect `log_options.get_level()` read semantics (unrelated atomic).

**Verification:** `test/logging_vlog_relaxed_contract.cpp` (new), `test/logging_tag_verbosity_contract.cpp` (existing must still pass), `test/logging_tag_verbosity_adversarial.cpp` (existing must still pass), `test/logging_tag_verbosity_light_fuzz.cpp` (existing must still pass), full `run_all_tests` suite.

#### M9 — Fix verbosity_blkch type to std::atomic<int>

**Files:**
1. `tde2e/td/e2e/TestBlockchain.h`
2. `tde2e/td/e2e/TestBlockchain.cpp`

**Required changes:**
1. `TestBlockchain.h`: change `extern int VERBOSITY_NAME(blkch)` → `extern std::atomic<int> VERBOSITY_NAME(blkch)` and add `#include <atomic>` if not already present.
2. `TestBlockchain.cpp`: change `int VERBOSITY_NAME(blkch) = VERBOSITY_NAME(INFO)` → `std::atomic<int> VERBOSITY_NAME(blkch){VERBOSITY_NAME(INFO)}`.

**Verification:** `test/logging_tde2e_blkch_contract.cpp` (new), project builds without errors, `ctest -R tde2e` still passes.

### 11.6 Execution Order

Milestones are independent of each other. Suggested order: M9 (simplest, lowest risk) → M7 (local, self-contained) → M8 (touches macro, benefits from M7+M9 being green first).

Each milestone must follow the full cycle: Contract → Attack → Red → Green → Survive → Refactor.

### 11.7 Phase 6 Verification Checklist

```
[ ] test/logging_tscerr_ordering_contract.cpp exists and passes
[ ] test/logging_vlog_relaxed_contract.cpp exists and passes
[ ] test/logging_tde2e_blkch_contract.cpp exists and passes
[ ] All three new test files wired into test/CMakeLists.txt
[ ] TsCerr.cpp contains no memory_order::seq_cst in spinlock paths
[ ] logging.h contains load_verbosity_level overloads (int and std::atomic<int>)
[ ] LOG_IMPL_FULL guard uses load_verbosity_level(runtime_level), not bare runtime_level
[ ] TestBlockchain.h/cpp declare verbosity_blkch as std::atomic<int>
[ ] All existing logging tests still pass (90 tests, no regressions)
[ ] LOG-R09, LOG-R10, LOG-R11 each mapped to at least one contract or adversarial test
[ ] Fixture-grounded stealth smoke and TlsHello gates pass (no DPI regression)
[ ] No TODOs, no suppressed errors, no tests relaxed to force green
```
