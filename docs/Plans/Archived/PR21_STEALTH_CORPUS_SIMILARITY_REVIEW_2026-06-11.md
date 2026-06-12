# PR #21 Stealth Corpus Similarity Review

Review target: `review-pr-21` / `origin/stealth-corpus-real-dump-similarity` at `fb0d98c4e`, compared against `origin/master`.

Scope: verify whether the PR implements `docs/Plans/STEALTH_CORPUS_REAL_DUMP_SIMILARITY_TEST_PLAN_2026-06-08.md` and whether it also closes the assigned client-side stealth/TLS runtime issues from 2026-06-08.

Conclusion: the PR is not ready to merge as a full remediation. It adds useful corpus-similarity scaffolding, but it does not close the assigned runtime stealth risks, and two of the new release-facing gates are weaker than their names and the plan imply.

## Findings

### High: assigned runtime stealth weaknesses are not fixed

Confidence: ~95%.

The PR does not modify any of the runtime files or existing regression tests implicated by the assigned June 8 client-side stealth/TLS risk list. `git diff --name-only origin/master...HEAD` has no changes under:

- `td/mtproto/IStreamTransport.cpp`
- `td/mtproto/stealth/StealthRuntimeParams.cpp`
- `td/mtproto/stealth/TlsHelloProfileRegistry.cpp`
- `td/mtproto/stealth/StealthConfig.cpp`
- `td/mtproto/TlsInit.cpp`
- the prior risk tests such as `test_stream_transport_activation_fail_closed.cpp` and `test_stealth_config_tls_init_profile_temporal_divergence.cpp`

The live code still has the same core runtime behaviors:

- Fail-open stealth activation: `td/mtproto/IStreamTransport.cpp:69-88` logs config/decorator failures and returns legacy `ObfuscatedTransport`.
- Release gating remains opt-in: `td/mtproto/stealth/StealthRuntimeParams.cpp:284-286` returns OK when `release_mode_profile_gating` is false, and the default constructor at `:306-312` does not enable it.
- Profile selection still lacks per-install entropy: `td/mtproto/stealth/TlsHelloProfileRegistry.cpp:849-859` hashes only destination, time bucket, and platform hints.
- Profile is still selected twice: `td/mtproto/stealth/StealthConfig.cpp:420-429` selects at config construction time, while `td/mtproto/TlsInit.cpp:234-254` selects again at ClientHello send time.

Security impact: this PR may improve test evidence around real-dump similarity, but it does not remove the concrete runtime downgrade/correlation/TOCTOU risks the coworker was asked to analyze and fix. Treating this as a full remediation would be a false closure.

Expected remediation: either split PR #21 explicitly as "corpus similarity tests only" and open follow-up PRs for the five runtime risks, or extend this PR with real runtime changes and adversarial regression tests for fail-closed activation, release-grade mobile defaults, single profile binding, per-install entropy, and independent Firefox macOS weighting.

### High: exact-field gate can pass without checking catalog-backed critical fields

Confidence: ~90%.

`test/stealth/test_tls_generator_fixture_exact_fields_gate.cpp:47-57` treats `Exact`, `Catalog`, and `Policy` as equally "enforceable" for cipher suites, extension sets, and supported versions. It then calls `FamilyLaneMatcher::matches_exact_invariants()` at `:59-66`.

The matcher skips any empty invariant vector:

- `test/stealth/FamilyLaneMatchers.cpp:132-147` only checks cipher suites / extension set when the generated baseline vector is non-empty.
- `test/stealth/FamilyLaneMatchers.cpp:148-168` does the same for supported groups, ALPN, compression, and supported versions.

The generated oracle/header is internally fresh in this checkout (`render_header(build_baselines(load_samples(...))) == ReviewedFamilyLaneBaselines.h`), but it currently contains catalog-status fields with empty exact invariant vectors. The target release-facing lanes show the gap directly:

- `apple_ios_tls/non_ru_egress`: `cipher`, `supported_groups`, `supported_versions`, and `alpn` are `Catalog` with empty exact vectors.
- `chromium_linux_desktop/non_ru_egress`: `extension_set` and `alpn` are `Catalog` with empty exact vectors.
- `firefox_linux_desktop/non_ru_egress`: `cipher` and `extension_set` are `Catalog` with empty exact vectors.

Concrete generated-header examples include `test/stealth/ReviewedFamilyLaneBaselines.h:141`, `:146`, `:232`, and `:411-412`.

Security impact: a release-facing "exact fields" gate can produce false confidence. For mixed/catalog fields, the test may only verify that status is not `Unavailable`/`Mixed`, while the actual wire value is not checked against a catalog-specific matcher. A damaging generator drift in those fields could survive this gate.

Expected remediation: do not let `Catalog` or `Policy` pass through `matches_exact_invariants()` without a field-specific catalog/policy matcher. For each release-critical field, require one of:

- `Exact`: non-empty invariant plus exact equality.
- `Catalog`: generated value is a member of the fixture-derived observed catalog.
- `Policy`: generated value satisfies a named policy and any required fixture-derived set membership.

Also add mutant/negative tests proving that wrong cipher suites, extension sets, and supported versions fail for each status type.

### Medium-high: wire-length gate still uses a broad percent envelope

Confidence: ~85%.

The plan says `test_tls_generator_wire_length_fixture_gate.cpp` should replace broad envelopes with "fixture-derived exact lengths or explicit SNI-adjusted length models" (`docs/Plans/STEALTH_CORPUS_REAL_DUMP_SIMILARITY_TEST_PLAN_2026-06-08.md:155-156`).

The implementation instead defines `kBuilderJitterTolerancePercent = 15.0` at `test/stealth/test_tls_generator_wire_length_fixture_gate.cpp:55-60` and asserts `matcher.within_wire_length_envelope(..., 15.0)` at `:72-76`.

The matcher accepts any value within a percentage of an observed sample:

- `test/stealth/FamilyLaneMatchers.cpp:234-247` computes `allowed = sample * tolerance_percent / 100` and returns true if the difference is within that window.

Security impact: this can admit wire lengths never observed in reviewed dumps. It is fixture-anchored, but it is still an envelope rather than exact membership or an explicit SNI/padding model. In this checkout, the 15% rule accepts very wide ranges:

- `apple_ios_tls/non_ru_egress`: observed `[512, 1540, 1543]`, accepted approximately `435..1775`.
- `firefox_linux_desktop/non_ru_egress`: observed `[1890, 1899, 1905, 2207, 2209, 2213]`, accepted approximately `1606..2545`.
- `chromium_linux_desktop/non_ru_egress`: observed `[1715, 1779, 1782, 1794, 1870, 1882, 1902, 1914, 1946, 1966, 1978]`, accepted approximately `1457..2275`.

That weakens the release-facing claim the plan was trying to make.

Expected remediation: replace the 15% tolerance with one of:

- exact observed catalog membership when builder jitter is disabled or controlled;
- an explicit model that accounts for SNI length and the documented padding target, with bounds expressed in bytes and backed by fixture-derived inputs;
- a diagnostic-only label if the test is intentionally tolerance-based and not a release gate.

### Medium: C++ gates remain unverified locally

Confidence: ~80%; likely environment-specific.

The new C++ test files are registered in `test/CMakeLists.txt:978-982`, but `build/test/run_all_tests` was absent before verification. I configured a fresh `TDLIB_STEALTH_SHAPING=ON` build and attempted `cmake --build build --target run_all_tests --parallel 10`. The build reached the touched stealth test objects, including `test_tls_generator_extension_count_similarity.cpp` and `test_tls_generator_shuffle_similarity.cpp`, but failed because the filesystem was full while the compiler wrote `/tmp/cc*.s` files:

```text
fatal error: error writing to /tmp/ccM7I7fn.s: No space left on device
fatal error: error writing to /tmp/ccR7pHlR.s: No space left on device
gmake: *** [Makefile:1089: run_all_tests] Error 2
```

The PR handoff also records that C++ build/run/ctest were not executed in the author's environment and were deferred to Linux CI (`docs/Plans/fingerprint-hardening-master-plan-2026-05-24/handoffs/stealth_corpus_similarity_claude_2026-06-08.json:76-87`, `:135`).

Merge impact: this is not proof of a code defect, but it means the release-facing C++ gates are still not confirmed by this local review. Do not merge based only on source inspection; require CI evidence for the five new filters and preferably the stealth/TLS slice.

## Positive Evidence

- SocratiCode search was usable for `/home/david_osipov/tdlib-obf` and located the relevant runtime and similarity-gate code paths. I did not have a callable `codebase_status` tool in this session, so I did not independently prove index freshness.
- The PR-specific Python tests pass under unittest discovery:
  - `PYTHONPATH=test/analysis python3 -m unittest discover -s test/analysis -p 'test_family_lane_oracle_generation.py' -v`
  - `PYTHONPATH=test/analysis python3 -m unittest discover -s test/analysis -p 'test_corpus_iteration_tier_naming_contract.py' -v`
  - `PYTHONPATH=test/analysis python3 -m unittest discover -s test/analysis -p 'test_similarity_release_gate_contract.py' -v`
- The baseline generator loaded 458 samples and produced 33 family/lane baselines.
- The generated header is fresh against the generator and fixtures in this checkout: `render_header(build_baselines(load_samples(...))) == ReviewedFamilyLaneBaselines.h`.
- The source-identity bug from the older fingerprint-hardening notes appears corrected in `test/analysis/build_family_lane_baselines.py:281-285`: `_source_identity()` uses `(source_kind, source_sha256)` and not `source_path`.

## Recommended Merge Gate

Block merge until the PR author either narrows the PR claim or fixes the issues above.

Minimum evidence before reconsideration:

1. Runtime remediation PR or explicit follow-up tickets for the five assigned client-side risks.
2. Catalog/policy matchers for catalog-status release-critical fields, with negative/mutant tests.
3. Wire-length gate changed from percent envelope to exact/model-based gate, or downgraded to diagnostic naming/documentation.
4. Passing CI or local Linux output for:

```bash
cmake --build build --target run_all_tests --parallel 10
./build/test/run_all_tests --filter 'TlsReleaseSimilarityUnavailableFailClosed|TlsGeneratorFixtureExactFieldsGate|TlsGeneratorExtensionCountSimilarity|TlsGeneratorWireLengthFixtureGate|TlsGeneratorShuffleSimilarity'
```
