<!-- SPDX-FileCopyrightText: Copyright 2026 telemt community -->
<!-- SPDX-License-Identifier: MIT -->
<!-- telemt: https://github.com/telemt -->
<!-- telemt: https://t.me/telemtrs -->

# PR #21 Stealth Corpus Similarity Review (Fresh pass, 2026-06-12)

Review target: `origin/stealth-corpus-real-dump-similarity` at `a195226bd8491ec52dfdca1068ed1049d84b8808` (`refs/pull/21/head`), compared against `origin/master`.

Important setup note: the local branch `origin/pr-21` was stale (`fb0d98c4`); the actual GitHub PR head is `a195226bd`. All findings below are about the real PR head, not the stale local review branch.

## Findings

### Superseded high: the earlier exact-fields compile blocker is no longer live on the current staged head

Confidence: ~99% that the original finding was real on the earlier snapshot, and
~99% that it is now fixed on the current staged head.

Evidence:

- The original fresh pass observed a real type mismatch between
  `td::unique_ptr<std::string>` and `std::unique_ptr<std::string>` in
  `test_tls_generator_fixture_exact_fields_gate.cpp`.
- On the current staged head, `cmake --build build --target run_all_tests --parallel 10`
  now completes successfully.
- `./build/test/run_all_tests --filter TlsGeneratorFixtureExactFieldsGate` now
  passes locally on Linux.

Impact:

- This specific blocker is superseded on the current staged head.
- It should not be carried forward as a live merge objection unless the compile
  failure is reproduced again on a newer commit.

### Medium: the per-install entropy mitigation is now wired into production

Confidence: ~95%.

Evidence:

- `td/mtproto/stealth/TlsHelloProfileRegistry.cpp:928-942` now mints and persists a stable per-install salt when a KV store is configured.
- `td/mtproto/stealth/TlsHelloProfileRegistry.cpp:1012-1016` initializes that salt from the runtime store in `set_runtime_ech_failure_store(...)`.
- `td/telegram/net/ConnectionCreator.cpp:1442` and `:1972` wire `G()->td_db()->get_config_pmc_shared()` into `set_runtime_ech_failure_store(...)` on the live TLS path and at startup.
- `test/stealth/test_tls_profile_selection_per_install_entropy.cpp` passes on this staged head, including store-mint and store-restore coverage.

Impact:

- This finding is superseded on the current staged head: the repository does
  activate stable per-install entropy for real runtime clients when the config
  PMC store is available.
- Residual caveat: the explicit `0` sentinel still exists for tests and for any
  runtime path that intentionally omits the store, but that is no longer the
  default live wiring in this repository.

### Medium-high: the branch materially improves mobile posture, but it does not fully close the remaining iOS/default-policy release-grade risk

Confidence: ~90%.

Evidence:

- `td/mtproto/stealth/TlsHelloProfileRegistry.cpp:157-180` now marks `AndroidChromium_Alps` as `release_gating=true` with verified browser-capture provenance, while `Chrome147_IOSChromium` still remains `release_gating=false`.
- `td/mtproto/stealth/StealthRuntimeParams.cpp:297-314` rejects `release_mode_profile_gating=true` unless the platform has at least one allowed profile with `release_gating=true`.
- `test/stealth/test_tls_mobile_release_grade_lane.cpp:94-155` explicitly documents and tests that:
  - iOS at `TransportConfidence::Unknown` still picks only `IOS14`;
  - Android reaches `AndroidChromium_Alps` at established confidence;
  - Android at `Unknown` still fail-closes to `Android11_OkHttp_Advisory`.
- `test/stealth/test_tls_runtime_profile_policy_fail_closed.cpp:163-176` passes and pins that Android release mode is allowed at established confidence but rejected at `Unknown`.

Impact:

- The branch does more than just improve iOS reachability: it also adds a real
  Android release-gated lane for established-confidence runtime selection.
- But it still does not deliver a fully closed default mobile posture, because
  iOS under `TransportConfidence::Unknown` remains limited to advisory IOS14 and
  `Chrome147_IOSChromium` is still not release-gated.
- So the original mobile release-grade risk is materially narrowed, not fully
  eliminated.

### Medium: the PR response document is materially stale relative to the actual PR contents

Confidence: ~95%.

Evidence:

- `docs/Plans/PR21_REVIEW_RESPONSE_2026-06-11.md:6-12` says the runtime stealth fixes live on a separate branch, `stealth-runtime-hardening`, not in PR #21.
- The actual PR head `a195226bd` already contains those runtime commits (`d813a74ea`, `3078bccc5`, `0b2a40fe7`, `a195226bd`) on top of the corpus-similarity work.
- The same document also still claims Android is advisory-only, which was true
  before the staged Android Chromium promotion but is false on the current head.

Impact:

- Reviewers relying on this new response doc can misunderstand what is actually being merged into `main`.
- This is not a code-execution bug, but it is a review/process bug inside the PR itself.

## Residual Risks That Are Still Real But Less Immediate

### Residual design weakness: the double profile selection seam still exists

Evidence:

- `td/mtproto/stealth/StealthConfig.cpp:419-424` still binds `config.profile` at transport-config creation time.
- `td/mtproto/TlsInit.cpp:234-253` still performs an independent `pick_runtime_profile(...)` at hello-send time.
- `test/stealth/test_stealth_config_tls_init_profile_temporal_divergence.cpp:31-36` and `:174-206` explicitly document that the fix only eliminates the record-size consequence, not the existence of the two-time selection seam itself.

Assessment:

- The branch materially reduces the live record-size risk by clamping to the platform floor.
- It does **not** eliminate the architectural TOCTOU seam; it only contains the currently proven consequence.
- I do not treat this as a merge blocker on its own because the reviewed live impact is narrower after the fix, but the design weakness is still open.

### Latent generator contract bug: release baselines are built from all loaded samples, not only authoritative samples

Evidence:

- `test/analysis/build_family_lane_baselines.py:803-808` builds invariants, catalogs, and histograms from `group`.
- `test/analysis/build_family_lane_baselines.py:809-851` uses `authoritative_group` only for sample-count, source-count, session-count, and tier metadata.
- `test/analysis/build_family_lane_baselines.py:271-278` clearly distinguishes authoritative from advisory sources.

Assessment:

- With the current fixture set this is latent rather than active: the current checked-in ClientHello corpus is `browser_capture` only.
- But the code does not encode the plan's rule "diagnostic/advisory fixtures must not enter release-gating evidence" at the place where the actual evidence catalogs are built.
- If advisory or imported fixtures are added later, the release-facing catalogs can be silently contaminated while the tier counters still look authoritative.

## Verification Notes

- Verified the actual GitHub PR head via Git refs: `refs/pull/21/head` resolves to `a195226bd8491ec52dfdca1068ed1049d84b8808`.
- `cmake --build build --target run_all_tests --parallel 10` completes on this staged head.
- `test_tls_generator_fixture_exact_fields_gate`, `test_tls_mobile_release_grade_lane`,
  `test_tls_profile_selection_per_install_entropy`,
  `test_tls_runtime_profile_policy_fail_closed`, and the related targeted runtime
  suites pass locally on Linux.
- The modified Python analysis suites (`test_release_cohort_identity_contract`,
  `test_similarity_release_gate_contract`) pass via `unittest discover`.

## Bottom Line

This PR is no longer blocked by the earlier exact-fields compile error or by
unwired per-install entropy on the current staged head.

The remaining substantive open issue is narrower:

- the double profile-selection seam still exists as a design weakness;
- the mobile release-grade story is materially improved, but iOS/default-Unknown
  posture is still not fully closed.
