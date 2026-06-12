# PR #21 Stealth Corpus Similarity Review — Response (2026-06-11)

Response to `PR21_STEALTH_CORPUS_SIMILARITY_REVIEW_2026-06-11.md`. Every finding is
addressed below with the concrete change, where it landed, and how it is verified.

Actual PR scope at `origin/stealth-corpus-real-dump-similarity`:

- the corpus-similarity release-gate hardening (Findings 2 and 3);
- the runtime stealth follow-up commits for F1-F5 that were originally planned as
  a separate branch, but are now present on this PR head as well.

Build note: tdlib-obf does not build on macOS (zlib≥1.3.2 gate, missing
`htole*`, `std::atomic<std::shared_ptr>` unsupported by Apple libc++). The Python
generator + analysis suites are verified locally; **all C++ is verified on Linux
CI only** (see Finding 4 for commands). Findings flagged "CI-pending" below are
not unverified-by-omission — they are intentionally deferred to CI because the
toolchain cannot run on the author's machine.

## Finding 1 (High) — assigned runtime stealth weaknesses

Implemented on the current PR head:

| Risk | Fix | Commit |
|------|-----|--------|
| F1 fail-open activation | `create_transport` returns a `FailClosedStealthTransport` instead of a plain `ObfuscatedTransport` when emulate_tls stealth activation fails: `write()` drops data, `can_write()` is false, `read_next()` errors — the unmasked legacy fingerprint is never put on the wire | `80048c84` |
| F2 mobile release lane | effective weights carve a 1/7 slice of the iOS share for the verified `Chrome147_IOSChromium` lane (was pinned to 0); reachable once `transport_confidence` permits its cross-layer claim | `7f5a093a` |
| F3 profile TOCTOU | `apply_profile_record_size_limit` also clamps to `platform_record_size_floor()`, so a config-time vs hello-time profile divergence cannot exceed the record_size_limit the wire declared | `65bb23e5` |
| F4 per-install entropy | `stable_selection_hash` mixes in a per-install salt; when the runtime config KV store is present it is minted once, persisted, and restored automatically, while default/test path `0` still preserves the legacy deterministic vector | `d2062f83` + follow-up fix |
| F5 firefox weight aliasing | `Firefox149_MacOS26_3` gets its own `firefox149_macos26_3` weight slot instead of aliasing `firefox148`; effective default weights unchanged | `d2062f83` |

Adversarial regression tests added: `test_stream_transport_activation_fail_closed`
(updated to assert fail-closed), `test_tls_mobile_release_grade_lane`,
`test_stealth_config_tls_init_profile_temporal_divergence` (floor-binding test +
rewritten firefox-slot tests), `test_tls_profile_selection_per_install_entropy`,
`test_tls_profile_firefox_weight_independence`.

Honest residuals (documented, not papered over): at the default Unknown
`transport_confidence` iOS still selects advisory IOS14 (a cross-layer-claim
profile must not be used without evidence). Android is no longer advisory-only:
the reviewed `AndroidChromium_Alps` lane is browser-capture-backed,
`release_gating=true`, and reachable once `transport_confidence` is established,
while default Unknown confidence still fail-closes onto the advisory OkHttp
fallback. The remaining mobile gap is therefore narrower than the original F2:
iOS still lacks a profile that is both confidence-allowed at Unknown and
release-gated, so a full mobile/default-policy closure still needs either new
evidence or an explicit policy redesign.

## Finding 2 (High) — exact-field gate skipped catalog-backed critical fields

Branch `stealth-corpus-real-dump-similarity`, commit `300a3c5e`.

- `build_family_lane_baselines.py` emits per-field observed-value catalogs
  (`observed_cipher_suite_sequences`, `observed_extension_sets`,
  `observed_supported_versions_sequences`) into `SetMembershipCatalog`; header
  regenerated, byte-deterministic, matches the generator self-test.
- `FamilyLaneMatcher::matches_release_critical_field()` dispatches on
  `EvidenceFieldStatus`: Exact → non-empty exact equality; Catalog → membership in
  the observed catalog; Policy → fail closed (no named matcher yet);
  Unavailable/Mixed → fail closed.
- `test_tls_generator_fixture_exact_fields_gate` runs that dispatch for cipher
  suites, extension set, and supported versions, and adds mutant/negative tests
  proving a wrong value fails for both Exact and Catalog status.

## Finding 3 (Medium-high) — broad percent wire-length envelope

Branch `stealth-corpus-real-dump-similarity`, commit `300a3c5e`.

- `FamilyLaneMatcher::within_wire_length_byte_model()` bounds the generated length
  to within `max_byte_delta` of an observed sample, in bytes.
- `test_tls_generator_wire_length_fixture_gate` derives the budget from the
  generator mechanism: 255 B padding-target entropy (`rng.bounded(256u)`) + a
  fixture-derived 16 B SNI-length delta, replacing the arbitrary 15%.
  `within_wire_length_envelope` is retained only for the nightly self-calibrated
  Monte Carlo diagnostic.

## Finding 4 (Medium) — C++ gates verified locally on Linux

Unchanged for macOS: tdlib-obf still does not build there, so macOS-only local
verification remains unavailable. In this Linux checkout, however, the C++ build
and the PR-targeted runtime/release-gating tests were executed locally on
2026-06-12. Representative commands:

```bash
cmake --build build --target run_all_tests --parallel 10
./build/test/run_all_tests --filter TlsGeneratorFixtureExactFieldsGate
./build/test/run_all_tests --filter MobileReleaseGradeLane
./build/test/run_all_tests --filter PerInstallSelectionEntropy
./build/test/run_all_tests --filter TlsRuntimeReleaseProfileGatingContract
./build/test/run_all_tests --filter TlsRuntimeProfilePolicyFailClosed
./build/test/run_all_tests --filter StealthRuntimeDefaultsContract
./build/test/run_all_tests --filter StealthParamsLoaderProfileWeightBridgeContract
./build/test/run_all_tests --filter DarwinProfileHardcodingBug
./build/test/run_all_tests --filter TlsProfilePlatformCoherence
./build/test/run_all_tests --filter TlsProfileRegistry
./build/test/run_all_tests --filter StealthConfigTlsInitProfileTemporalDivergence
./build/test/run_all_tests --filter ConnectionCreatorTlsInitSourceContract
./build/test/run_all_tests --filter StreamTransportSeam
```

Locally verified on Linux:

- `cmake --build build --target run_all_tests --parallel 10` completed
  successfully.
- `test_tls_generator_fixture_exact_fields_gate` passed, so the earlier
  exact-fields compile blocker is no longer live on the current PR head.
- The targeted runtime suites above passed, including the new Android/mobile
  reachability and per-install entropy coverage.
- The Python analysis suites
  (`test_release_cohort_identity_contract`,
  `test_similarity_release_gate_contract`) passed via `unittest discover`.

Not completed in this turn: a full `ctest --test-dir build --output-on-failure`
run was started but not allowed to finish before response handoff, so this
document should not claim a completed full-suite result yet.
