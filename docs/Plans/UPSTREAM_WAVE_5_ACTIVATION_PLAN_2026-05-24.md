<!--
SPDX-FileCopyrightText: Copyright 2026 telemt community
SPDX-License-Identifier: MIT
telemt: https://github.com/telemt
telemt: https://t.me/telemtrs
-->

# Upstream Wave 5 Activation Plan (W11-AI2)

**Plan ID:** upstream-wave-5-activation-2026-05-24
**Date:** 2026-05-24
**Scope:** Product-driven activation plan for the deferred Wave 5 text-composition bundle
**Backlog anchor:** `origin/master..upstream/master`
**Canonical manifest:** `docs/Plans/UPSTREAM_BACKPORT_MANIFEST_2026-05-08.md`
**Canonical gating plan:** `docs/Plans/UPSTREAM_BACKPORT_GATING_PLAN_2026-05-08.md`
**Preflight archive:** `docs/Plans/UPSTREAM_WAVE_5_PREFLIGHT_2026-05-14.md`

## 1. Objective

Implement the deferred Wave 5 text-composition / AI-compose capability bundle so downstream client
developers can adopt `tdlib-obf` without material feature regression against upstream TDLib, while
preserving the repository's vendor/security fork model, fail-closed behavior, and adversarial TDD
requirements.

This is a product-parity program, not a bulk upstream sync. Every upstream row remains subject to
bounded local adaptation where that improves correctness, security, or compatibility.

## 2. Task Definition

Execute the deferred `W11-AI2` bundle as a staged activation lane around the current local owners
`TranslationManager`, `Requests`, `UpdatesManager`, `LinkManager`, `WebPagesManager`, and `cli`,
while introducing the missing Wave 5 owner classes and preserving the already-landed local baseline
(`InputText`, current style cache, update plumbing, and slug validation).

## 3. Acceptance Criteria

1. All 18 deferred `W5-AI` rows plus the cross-wave compile-closeout row are accounted for by exact
   cherry-pick, bounded local adaptation, or explicit no-op reconciliation inside this activation lane.
2. Existing local Wave 5 baseline behavior remains compatible: `composeTextWithAi`, `fixTextWithAi`,
   `updateTextCompositionStyles`, current style sanitization, and current slug validation must not regress.
3. The missing owner classes and request surfaces land behind RED-first contract coverage in separate
   test files and pass contract, adversarial, integration, light-fuzz, stress, and runtime-owner-path checks.
4. All new request and update boundaries satisfy the repository secure-coding rules and OWASP ASVS L2
   expectations listed in Section 8.
5. Touched files introduce zero new compiler diagnostics, zero new `.clang-tidy` or formatting violations,
   zero new SonarCloud bug/vulnerability/security-hotspot findings, and zero new SAST/PVS findings in
   the touched slice.
6. Any public API addition or removal is reflected in the Doxygen source-of-truth files, the published
   input allowlist, and the rendered integrator-facing page.
7. The release handoff includes the exact upstream baseline tag, commit accounting, test evidence,
   static-analysis evidence, and a migration note for downstream integrators.
8. Baseline provenance is explicit: every preserved behavior is mapped to current `master` code anchors,
   and any upstream hash that is not a direct ancestor is documented as local-equivalent adaptation
   evidence before coding starts.
9. Cross-lane non-regression checks stay green: Wave 5 activation must not regress stealth/TLS/QUIC
   fixture validation (`test/analysis/fixtures/*`) or the existing `TlsHello` runtime slice.

## 4. Exact Scope

### 4.1 Deferred rows to activate

1. `d72be7609`
2. `176915344`
3. `a05aeeb9c`
4. `b77099227`
5. `fc903aab3`
6. `3ba1e630b`
7. `23971a844`
8. `327531a54`
9. `ee93de50b`
10. `8a7d707ee`
11. `27b1ee8cd`
12. `f5b5a6e11`
13. `64972181c`
14. `86d375553`
15. `3e10a17e6`
16. `6113d3822`
17. `36e726f93`
18. `3678c2d42`
19. `49b3bcbb6`

Scope note: this activation set contains 18 deferred `W5-AI` rows plus the cross-wave compile-closeout
row. `49b3bcbb6` is a cross-wave `W1-T`/`W5-AI` compile-closeout row that has no standalone
transport value, but it remains in this lane as a closeout guard for the owner classes once they exist.

### 4.2 Upstream evidence rows for local baseline behavior (must be re-mapped before coding)

1. `c3a6ecea6`
2. `d747885cb`
3. `528988dd9`
4. `0c6ea7e09`
5. `9571c262f`
6. `ff051c4dc`
7. `df4bfee0d`
8. `c96e67c38`
9. `58d72a0e8`
10. `a26ccb8c5`
11. `990b821c8`

Ancestry audit note (2026-05-24): from this upstream evidence set, only `c3a6ecea6` and
`d747885cb` are direct ancestors of current `master`. The remaining rows are represented by
repository-resident local-equivalent adaptations and must be tracked through explicit
file-level/behavior-level evidence (plus local commit anchors where available) instead of claiming
direct hash ancestry.

### 4.3 Scope grouping

1. **Owner model and persistence:** `d72be7609`, `176915344`, `a05aeeb9c`, `fc903aab3`,
   `3ba1e630b`, `327531a54`, `ee93de50b`
2. **Request surface and CRUD APIs:** `23971a844`, `8a7d707ee`, `27b1ee8cd`, `64972181c`,
   `86d375553`, `3e10a17e6`, `36e726f93`
3. **Update/reload lifecycle:** `b77099227`, `6113d3822`
4. **Link and preview exposure:** `f5b5a6e11`, `3678c2d42`
5. **Auxiliary compile-closeout:** `49b3bcbb6`

### 4.4 Upstream delta audit snapshot (2026-05-24)

1. All 19 activation hashes in Section 4.1 are present in `upstream/master` and are not ancestors of
   local `master`; this means 18 deferred `W5-AI` rows plus the cross-wave compile-closeout row.
2. No additional Wave 5 text-composition commits were introduced in `upstream/master` after
   `49b3bcbb6` up to baseline `e0943d068ce9`.
3. Commit-scope verification matches this plan's grouping:
   - owner/model rows touch `AiComposeTone*`, `AiComposeToneExample*`, `TranslationManager`,
     `td_api.tl`, and build split wiring;
   - request rows touch `Requests.*`, `TranslationManager.*`, `cli.cpp`, and `td_api.tl`, but do
     not include lifecycle-only rows;
    - `b77099227` touches `UpdatesManager.cpp`, `TranslationManager.*`, and `AiComposeTone.h`, but
       no `Requests.*` files, so it is update/reload lifecycle work rather than request-surface work;
   - `6113d3822` touches only `TranslationManager.cpp`, so it is request-triggered reload ordering
     rather than CRUD surface definition;
   - `3678c2d42` is documentation-only in `td_api.tl`; link/preview runtime work is limited to
     `f5b5a6e11` and its `WebPagesManager.cpp` projection;
   - `49b3bcbb6` remains a compile-closeout row touching only `AiComposeToneExample.hpp`.

### 4.5 Local baseline provenance map

The rows below are not implementation authorization by themselves; they are the repository-resident
anchors that must be re-verified in Phase 0 before any owner or request code is changed. Direct ancestors
still need contract coverage because later local hardening can drift from the original upstream intent.

| Upstream evidence row | Local provenance and tests to pin before coding |
|---|---|
| `c3a6ecea6` | Direct ancestor for `TranslationManager.InputText`; preserve `composeTextWithAi` / `fixTextWithAi` request projection in `td/generate/scheme/td_api.tl`, `td/telegram/Requests.cpp`, and the runtime owner-path tests. |
| `d747885cb` | Direct ancestor for `TranslationManager::get_input_text`; preserve request conversion semantics in `td/telegram/TranslationManager.cpp` and `td/telegram/Requests.cpp`. |
| `528988dd9` | Local-equivalent behavior is the current fail-closed flat style cache in `td/telegram/TranslationManager.cpp`, with sanitization anchored by `test/translation_manager.cpp`; do not reintroduce the deprecated `ai_compose_styles` app-config path. |
| `0c6ea7e09` | Local-equivalent option ownership is in `td/telegram/OptionManager.cpp` and `td/telegram/ConfigManager.cpp`, pinned by `test/text_composition_style_example_count_option_contract.cpp` and companion adversarial/light-fuzz/stress suites. |
| `9571c262f` | Local-equivalent premium-limit surface is in `td/generate/scheme/td_api.tl`, `td/telegram/OptionManager.cpp`, and `td/telegram/ConfigManager.cpp`, pinned by `test/text_composition_control_plane_contract.cpp`. |
| `ff051c4dc` | Local-equivalent title-length default and app-config alias are in `td/telegram/OptionManager.cpp` and `td/telegram/ConfigManager.cpp`, pinned by `test/text_composition_control_plane_contract.cpp`. |
| `df4bfee0d` | Local-equivalent prompt-length default and app-config alias are in `td/telegram/OptionManager.cpp` and `td/telegram/ConfigManager.cpp`, pinned by `test/text_composition_control_plane_contract.cpp`. |
| `c96e67c38` | Local-equivalent update handling is in `td/generate/scheme/telegram_api.tl` and `td/telegram/UpdatesManager.cpp`, pinned by `test/text_composition_updates_manager_contract.cpp`. |
| `58d72a0e8` | Local-equivalent promise-aware reload path is in `td/telegram/TranslationManager.*`, pinned by `test/text_composition_reload_contract.cpp` and `test/text_composition_update_promise_contract.cpp`. |
| `a26ccb8c5` | Local-equivalent stale/reordered update hardening is in `td/telegram/UpdatesManager.cpp` and `td/telegram/TranslationManager.cpp`, pinned by `test/text_composition_updates_manager_contract.cpp`, `test/text_composition_reload_contract.cpp`, and runtime harness tests. |
| `990b821c8` | Local-equivalent deep-link surface is in `td/generate/scheme/td_api.tl`, `td/telegram/LinkManager.cpp`, and `test/link.cpp`, pinned by `test/text_composition_link_contract.cpp`. |

## 5. Operating Principles

1. No bulk cherry-pick. Each row is reviewed and either transplanted exactly or adapted locally with
   explicit rationale.
2. Compatibility first. Existing downstream client-facing Wave 5 behavior must keep working while the
   richer upstream capability set is added.
3. Deny by default. Invalid style identifiers, malformed payloads, unsupported preview/link shapes,
   and stale reload/update paths fail closed before mutation or RPC dispatch.
4. Generated artifacts stay single-owner. `td_api.tl`, generated headers, and split outputs are
   regenerated in DAG order and never hand-edited after generation.
5. One reversible slice at a time. Each phase below must land in a reviewable branch with focused
   validation before the next phase starts.
6. `master` remains downstream reality. Upstream provenance is recorded through the baseline-tag policy
   in `docs/Documentation/FORK_MAINTENANCE_POLICY.md` rather than through ancestry tricks.
7. Apply the architecture checklist from `.github/instructions/architecture.instructions.md` on every
   owner-class and request-surface decision: SRP, DRY, minimal public surface, explicit error semantics,
   and one authoritative owner per contract.
8. Treat `td/generate/scheme/td_api.tl`, `td/generate/doxygen_tl_docs.py`, `Requests.*`, `cli.cpp`,
   `Doxyfile.in`, and `docs/api/public_api_surfaces.md` as the public API documentation control plane.
   Never hand-edit generated `td_api.h` or `td_api.hpp` outputs.
9. Because `EXTRACT_ALL = NO`, undocumented public symbols are treated as missing surface area. If a public
   input or symbol is added or removed, update both `Doxyfile.in` and `docs/api/public_api_surfaces.md` in
   the same slice and verify the rendered page an integrator will read.
10. Any new diagnostics must route through the existing logging subsystem only; use the `LOG` / `VLOG` path,
    not ad hoc `stderr`, direct `TsCerr`, or parallel logger state, and do not log prompts, titles, tokens,
    or other user-controlled style content.
11. If a slice touches fatal logging behavior, sinks, callbacks, or externally controllable tags, preserve the
    existing subsystem contracts around `process_fatal_error()`, callback reentrancy guards, and active-sink
    atomic ownership.
12. C++ implementation must follow the repo's modern C++ and C++23 guidance: rule-of-zero where possible,
    explicit ownership, no raw `new` / `delete`, no C-style casts, no output-parameter APIs for new helpers,
    and prefer `std::span`, `std::string_view`, `std::expected` / `std::optional`, selection initializers,
    `std::to_underlying`, and `.contains()` where they simplify the touched slice without widening scope.
13. Activation stays commit-allowlist driven: if upstream adds new Wave 5 rows after baseline tagging, stop,
   re-open preflight, and update the allowlist before coding.
14. Cross-lane transport fixture gate is mandatory for every release candidate, even though this lane is
   product/API scoped. ECH is blocked in the target censorship environment.
   It must not rely on ECH availability as an evasion primitive.
   It must preserve browser-mimic behavior when ECH is absent or blocked.
   QUIC Ru-to-non-Ru blocking is treated as a transport non-regression sentinel, not as authorization to add
   transport changes under this plan. Real traffic dump corpus must be the evidence source for any TLS/ECH/QUIC
   claim: use corpora generated from `docs/Samples/Traffic dumps/` into `test/analysis/fixtures/`.
   Generated seeds are runtime stress inputs, not independent browser evidence.

## 6. Branch and Baseline Workflow

1. Fetch `upstream` and create the exact upstream baseline tag before implementation starts.
2. Open a dedicated activation branch from current `master`, not from the reference branch.
3. Keep a read-only reference branch from the same upstream commit for diff review.
4. Record the baseline tag name in the changelog and in the final implementation closeout.
5. Freeze the deferred-commit allowlist (`d72be7609` ... `49b3bcbb6`) for this lane; any new upstream Wave 5
   delta requires a preflight addendum before implementation continues.

Planned branch assets:

```bash
git fetch upstream --tags
git tag -a upstream-baseline-2026-05-24-e0943d068ce9 e0943d068ce90b5010f1aea946e6901e25b43bf6 \
  -m "Exact upstream baseline for Wave 5 activation started 2026-05-24"
git branch upstream-reference/2026-05-24-e0943d068ce9 e0943d068ce90b5010f1aea946e6901e25b43bf6
git switch -c feature/w11-ai2-wave5-activation-2026-05-24 master
```

## 7. Contracts To Pin Before Code Changes

### 7.1 Structured tone model contract

Boundary: new `AiComposeTone*` owners plus migration from the existing flat triple cache.

- Inputs: current triple-based cache, new structured tone/example payloads, partially migrated data.
- Outputs: one canonical in-memory model and one backward-compatible `td_api` projection.
- Postconditions: no partial record acceptance, no lossy round-trips, no cross-user leakage.

### 7.2 Request boundary contract

Boundary: create/edit/search/get/add/remove/delete text-composition style handlers in `Requests` and
their owner calls.

- Inputs: user-controlled titles, prompts, slugs, style identifiers, example selectors, emoji identifiers.
- Outputs: exact `td_api` result or exact fail-closed user error.
- Postconditions: invalid input is rejected before RPC dispatch and failed requests do not mutate cache state.

### 7.3 Update lifecycle contract

Boundary: `updateAiComposeTones` handling, timer reloads, request-triggered refreshes, and emitted
`updateTextCompositionStyles` snapshots.

- Inputs: repeated or reordered updates, concurrent reloads, successful style mutations.
- Outputs: one coherent client-visible snapshot.
- Postconditions: promise completion on all paths and no stale reload overwrites.

### 7.4 Link and preview contract

Boundary: `internalLinkTypeTextCompositionStyle`, preview materialization, slug fallback resolution,
and cache-miss compose flows.

- Inputs: user-controlled slugs, percent-encoded links, cached and uncached styles.
- Outputs: typed link/preview objects or deterministic rejection.
- Postconditions: only explicit slug paths are accepted; invalid slugs never alias other styles.

### 7.5 Control-plane cohesion contract

Boundary: app-config mapping, option defaults, premium-limit exposure, and config-version bumps.

- Inputs: app-config integers, defaults, future schema additions.
- Outputs: one coherent option namespace and monotonic version ownership.
- Postconditions: no option lands without mapping, default, docs, and version ownership together.

## 8. Security and ASVS L2 Requirements

### 8.1 ASVS-aligned gates

1. **V4 Access Control / deny-by-default:** style-management requests must keep existing authorization
   behavior and reject unsupported callers before owner dispatch.
2. **V5 Input Validation:** titles, prompts, slugs, identifiers, example selectors, length limits,
   and custom-emoji references are validated at the first entry point. Contract tests must pin current
   option-backed defaults exactly: `text_composition_style_title_length_max=12`,
   `text_composition_style_prompt_length_max=1024`, and `added_text_composition_style_max=5/20`
   (non-premium/premium), including boundary and limit-plus-one rejection cases.
3. **V7 Error Handling:** errors must not leak internal owner names, local paths, or partial migration
   state; promises and temporary resources must be cleaned up on all failure paths.
4. **V8 Data Integrity:** persistence migration and cache replacement must preserve deterministic
   round-trips and reject partially migrated records.
5. **V9 Communications / untrusted updates:** update payloads are treated as untrusted server input;
   reload/update state machines must reject malformed or stale data instead of widening behavior.
6. **Configuration safety:** config/version changes must be monotonic and reviewable; no silent fallback
   to deprecated option keys may be reintroduced.

### 8.2 Additional repository security posture

1. Preserve fail-closed slug validation already shared between `TranslationManager` and `LinkManager`.
2. Do not widen accepted path forms beyond upstream necessity without explicit review.
3. No raw string concatenation for structured request payloads or path parsing.
4. No logging of user prompts, titles, or style payloads in production paths unless already permitted
   by existing subsystem rules and redacted where necessary.
5. If new public API comments are added, they must document the integrator-facing contract that Doxygen will
   publish: ownership, lifetime, nullability, thread-safety, callback restrictions, sync vs async behavior,
   and fatal/crash semantics.
6. Treat `creator_user_id` and style-example hydration as untrusted persisted input: unresolved users,
   malformed ownership, or partial records must fail closed without cache corruption or reload storms.

### 8.3 Static-analysis and code-quality security gate

1. This lane is release-blocked by any new bug, vulnerability, taint, or security-hotspot finding in the
   touched files from SonarCloud / SonarQube Connected Mode when configured.
2. This lane is release-blocked by any new warning in `.clang-tidy`, compiler diagnostics, or repository SAST
   checks for the touched files.
3. Static-analysis suppression is not an acceptable first fix. Findings must be either repaired or proven to be
   false positives with a file-local rationale and a regression test where applicable.
4. Because `sonar-project.properties` and `.github/workflows/sonar.yml` are already present in the repository,
   the branch or PR SonarCloud scan is part of the mandatory acceptance path, not optional hygiene.
5. Because `tools/ci/sast_scan.sh` already exists, the activation closeout must include a targeted SAST/PVS pass
   or a documented reason why the environment could not execute it locally, with CI remaining blocking.

## 9. Risk Register

| Risk ID | Area | Category | Concrete failure | Required test families |
|---|---|---|---|---|
| `W5R-01` | Tone-model migration | Data integrity | old flat triples and new structured records produce lossy or partial round-trips | contract, integration, adversarial, stress |
| `W5R-02` | Request validation | Input validation | oversized or malformed title/prompt/slug reaches RPC dispatch | contract, adversarial, light-fuzz |
| `W5R-03` | Update lifecycle | State machine | repeated `updateAiComposeTones` leaves promises unresolved or overwrites newer cache | contract, runtime harness, stress |
| `W5R-04` | Link/preview | Input validation | invalid slug or preview path aliases another style or silently falls back | contract, adversarial, light-fuzz |
| `W5R-05` | Control plane | Config cohesion | option/version rows land inconsistently and desynchronize limits | contract, integration, stress |
| `W5R-06` | Missing owners | Build integrity | local owner introduction compiles on one intermediate slice but not on complete generated surfaces | contract, build, runtime owner-path |
| `W5R-07` | Cache/user loading | Resource handling | partial user-loading or example hydration corrupts cache and client projection | integration, stress, adversarial |
| `W5R-08` | Backward compatibility | Consumer compatibility | existing `composeTextWithAi` / `fixTextWithAi` clients regress when richer owners land | contract, integration, runtime owner-path |
| `W5R-09` | Public API docs | API integrity | new `td_api.tl` / request-plumbing surface lands without Doxygen-visible integrator documentation | contract, integration, docs validation |
| `W5R-10` | Static analysis | Code quality | touched files introduce new SonarCloud, `.clang-tidy`, or compiler findings that hide real defects | build, lint, SAST, Sonar |
| `W5R-11` | Logging and secrecy | Information exposure | new diagnostics leak user-controlled text-composition payloads or bypass the logging subsystem | contract, adversarial, code review |
| `W5R-12` | Persisted owner refs | Resource/state exhaustion | malformed persisted `creator_user_id` or example ownership forces repeated cache discard/reload churn | adversarial, integration, stress |
| `W5R-13` | Cross-lane transport safety | Regression containment | Wave 5 activation accidentally perturbs TLS/ECH/QUIC browser-mimic fixture behavior or stealth test outcomes | integration, runtime harness, fixture smoke, real-dump provenance audit |

Each HIGH risk entry above must have at least one hostile test that can fail on a controlled mutant.

## 10. TDD Execution Model

### 10.1 Test-file policy

All new tests live in separate files. Avoid redundant suite proliferation: extend repository-resident
`text_composition_*` families first, then add only the missing owner/model seams.

Existing suites that must be extended in this lane:

- `test/text_composition_control_plane_*`
- `test/text_composition_style_catalog_sanitizer_*`
- `test/text_composition_style_name_*`
- `test/text_composition_link_*`
- `test/text_composition_reload_*`
- `test/text_composition_update_promise_*`
- `test/text_composition_updates_manager_*`
- `test/text_composition_owner_path_runtime.cpp`
- `test/text_composition_runtime_harness.cpp`

Plan-audit suites that must stay green while this document evolves:

- `test/analysis/test_wave5_activation_plan_contract.py`
- `test/analysis/test_wave5_activation_plan_adversarial.py`
- `test/analysis/test_wave5_activation_plan_integration.py`
- `test/analysis/test_wave5_activation_plan_light_fuzz.py`
- `test/analysis/test_wave5_activation_plan_stress.py`

New files reserved for gaps not already covered by the suites above:

- `test/text_composition_tones_contract.cpp`
- `test/text_composition_tones_adversarial.cpp`
- `test/text_composition_tones_integration.cpp`
- `test/text_composition_tones_light_fuzz.cpp`
- `test/text_composition_tones_stress.cpp`
- `test/text_composition_style_requests_contract.cpp`
- `test/text_composition_style_requests_adversarial.cpp`
- `test/text_composition_style_requests_integration.cpp`
- `test/text_composition_style_requests_light_fuzz.cpp`
- `test/text_composition_style_requests_stress.cpp`

Every added C++ runtime test file must be wired in `test/CMakeLists.txt` in the same patch.
Python documentation-analysis tests under `test/analysis/` must stay reachable through repository-standard
`python3 -m unittest discover` commands; no orphan test sources.

### 10.2 Red-first gates

Before each implementation phase:

1. Enumerate symbol blast radius first (`vscode_listCodeUsages` or equivalent) for every touched owner/helper/API.
2. Pin the exact contract in tests.
3. Add at least one hostile mutant that proves the test can fail.
4. Run the narrowest failing test or harness first.
5. Implement the minimum change to make that failing slice pass.
6. Re-run the same narrow validation before widening scope.
7. For every request or migration slice, add at least one security-focused hostile case covering malformed input,
   stale state replay, partial migration data, or unauthorized caller behavior.
8. For every public API slice, add at least one source-of-truth or rendered-surface contract proving the new API
   is documented through the intended Doxygen input surface.
9. For every new helper or owner seam, add at least one contract assertion that keeps ownership, nullability,
   and value/error semantics explicit instead of relying on undocumented output mutation.

### 10.3 Validation commands

Use repository-standard commands. Because Wave 5 touches `td_api.tl` and adds tests, refresh discovery in
the usual repository order: configure -> build `run_all_tests` -> configure before relying on new CTest entries.

Plan-audit gate (mandatory before implementation starts and after any plan edit):

```bash
python3 -m unittest discover -s test/analysis -p 'test_wave5_activation_plan_*.py' -v
```

Core loop:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DTD_ENABLE_BENCHMARKS=OFF -DTDLIB_STEALTH_SHAPING=ON
cmake --build build --target run_all_tests --parallel 14
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DTD_ENABLE_BENCHMARKS=OFF -DTDLIB_STEALTH_SHAPING=ON
./build/test/run_all_tests --filter TextComposition
```

Focused runtime checks should continue to use the existing `run_all_tests` binary for the touched slice.
When CTest discovery does not expose per-case entries, do not rely on `ctest -R 'TextComposition|AiCompose'`
as a filter proxy; use `run_all_tests --filter ...` directly.

Cross-lane non-regression checks (mandatory before release):

```bash
./build/test/run_all_tests --filter TlsHello
python3 test/analysis/run_corpus_smoke.py --registry test/analysis/profiles_validation.json --fixtures-root test/analysis/fixtures/clienthello --server-hello-fixtures-root test/analysis/fixtures/serverhello
```

Static-analysis and lint loop for touched files:

```bash
clang-format --dry-run --Werror <changed-files>
clang-tidy -p build <changed-cpp-files>
./tools/ci/sast_scan.sh
```

SonarCloud / SonarQube gate:

1. Local preferred path: use SonarQube or SonarCloud Connected Mode for touched files when available.
2. CI required path: the existing `.github/workflows/sonar.yml` and `sonar-project.properties` scan must run on the
   activation branch or PR, and touched files must show zero new bug/vulnerability/security-hotspot findings.
3. `get_errors` remains a mandatory fast local diagnostic gate even when SonarCloud is unavailable.

Public API documentation gate for slices that touch `td_api.tl`, `Requests.*`, or `cli.cpp`:

```bash
cmake --build build --target td_generate_api_docs
```

Documentation rules:

1. Update the correct source-of-truth file: `td_api.tl` for generated API types and methods,
   `td/generate/doxygen_tl_docs.py` for generated helper/banner docs, handwritten public headers for manual
   public APIs, and `Doxyfile.in` plus `docs/api/public_api_surfaces.md` for the published input allowlist.
2. Review `build/docs/api/html/index.html` or the relevant generated page, not just source diffs.
3. If the `td_generate_api_docs` target is missing, rerun CMake configure after installing Python 3 and Doxygen.
4. If Doxygen is still unavailable locally, the slice must update the source-of-truth files and record the
   missing rendered-page review as an environment limitation rather than skipping the documentation gate.

## 11. Phased Implementation Plan

### Phase 0: Baseline, blast radius, and RED inventory

Scope:

1. Confirm activation criteria from `UPSTREAM_BACKPORT_GATING_PLAN_2026-05-08.md` Section `0.3.14`:
   explicit owner/product approval, frozen API scope, per-commit deep review path, and security sign-off path.
2. Create the upstream baseline tag and reference branch.
3. Enumerate current symbol usages for `TranslationManager`, `Requests`, `UpdatesManager`,
   `LinkManager`, `WebPagesManager`, and the future `AiComposeTone*` owners.
4. Run and keep green the plan-audit suites under `test/analysis/` so this document remains a reliable gate.
5. Land the contract and adversarial tests for the existing baseline and all planned mutation points.
6. Record baseline-provenance mapping for Section 4.2 rows that are not direct `master` ancestors.

Exit gate:

1. Section `0.3.14` activation criteria are documented as satisfied for this lane.
2. Plan-audit suites under `test/analysis/` are green.
3. Baseline-provenance mapping for non-ancestor baseline rows is complete and reviewable.
4. All planned contract files exist and fail for the intended reason before production changes start.

### Phase 1: Introduce owner model and structured persistence

Rows:

1. `d72be7609`
2. `176915344`
3. `a05aeeb9c`
4. `fc903aab3`
5. `3ba1e630b`
6. `327531a54`
7. `ee93de50b`

Primary files:

1. new `td/telegram/AiComposeTone.*`
2. new `td/telegram/AiComposeToneExample.*`
3. `td/telegram/TranslationManager.*`
4. `td/generate/scheme/td_api.tl`
5. `CMakeLists.txt`
6. `SplitSource.php`

Requirements:

1. Structured owner introduction must preserve existing flat-triple compatibility during migration.
2. User/example hydration must not widen trust or leak partially loaded state.
3. No request-surface expansion lands before the structured owner round-trip is proven.
4. Owner introduction must keep responsibilities split cleanly: owner model, request routing, update routing,
   and preview/link projection stay in separate seams instead of one monolithic manager.
5. New owner helpers should expose explicit value/error and ownership semantics and prefer modern narrow types
   such as `std::span` or `std::string_view` where that reduces ambiguity without forcing broad refactors.

Exit gate:

1. Contract, adversarial, integration, security, light-fuzz, and stress tests pass for migration and canonical owner projection.
2. Touched owner files have zero new compiler, `.clang-tidy`, SonarCloud, and SAST findings.

### Phase 2: Request surface and CRUD APIs

Rows:

1. `23971a844`
2. `8a7d707ee`
3. `27b1ee8cd`
4. `64972181c`
5. `86d375553`
6. `3e10a17e6`
7. `36e726f93`

Primary files:

1. `td/telegram/Requests.*`
2. `td/telegram/TranslationManager.*`
3. `td/telegram/cli.cpp`
4. `td/generate/scheme/td_api.tl`

Requirements:

1. Input validation stays at request entry points.
2. Every handler must use one owner path; duplicated validation or duplicated reload logic is not allowed.
3. Failed create/edit/delete paths must not mutate local cache or leave refresh promises unresolved.
4. Because this phase changes public API surfaces, `td_api.tl` remains the source of truth and Doxygen-visible
   documentation is updated in the same slice.
5. If public inputs are added or removed, update `Doxyfile.in` and `docs/api/public_api_surfaces.md` in the
   same patch so the rendered API surface remains discoverable.

Exit gate:

1. CRUD request surface passes contract, adversarial, security, and owner-path runtime tests with controlled invalid-input mutants.
2. Doxygen/public API validation is complete for the touched API surface, including rendered-page review.

### Phase 3: Update lifecycle, periodic reload ordering, and cache coherence

Rows:

1. `b77099227`
2. `6113d3822`

Primary files:

1. `td/telegram/UpdatesManager.*`
2. `td/telegram/TranslationManager.*`
3. `td/telegram/AiComposeTone.h`
4. `td/telegram/Requests.*`

Local baseline rows to reconcile in this phase: `c96e67c38`, `58d72a0e8`, and `a26ccb8c5`. They
are not direct cherry-pick rows for this lane, but their local-equivalent behavior must survive the new owner model.
If `Requests.*` is touched in this phase, it is strictly local-adaptation-only; upstream lifecycle rows do not
authorize new CRUD/public API surface here.

Requirements:

1. Promise completion remains explicit on all update, periodic reload, and request-triggered reload paths.
2. Stale reloads may not overwrite newer local state.
3. Runtime harnesses must execute the owner path, not just source-shape contracts.
4. Any diagnostic additions must route through the logging subsystem and avoid payload disclosure.
5. If new log points are introduced, they must preserve existing fatal, callback, and active-sink contracts and
   use subsystem verbosity/tag controls instead of custom output paths.

Exit gate:

1. Runtime harness, security, and stress tests prove coherent snapshots under repeated or reordered updates.

### Phase 4: Link and preview parity

Rows:

1. `f5b5a6e11`
2. `3678c2d42`

Primary files:

1. `td/telegram/LinkManager.*`
2. `td/telegram/WebPagesManager.cpp`
3. `td/generate/scheme/td_api.tl`

Requirements:

1. Preserve existing strict slug-validation contract unless a broader form is proven necessary and safe.
2. Preview emission must depend on validated structured style state, not on ad hoc fallback strings.
3. Documentation updates land only after runtime parity exists.
4. Link/preview parsing remains an anti-corruption boundary: external link shapes are translated once and are not
   allowed to leak permissive upstream parsing into unrelated local flows.
5. Any new public preview/link types must stay documented in the published Doxygen surface and not rely on
   generated-output-only comments.

Exit gate:

1. Round-trip link and preview tests pass for both cached and uncached valid slugs and reject malformed inputs.
2. Link/preview files have zero new SonarCloud, `.clang-tidy`, and compiler findings.

### Phase 5: Auxiliary compile-closeout and release readiness

Rows:

1. `49b3bcbb6`

Primary files:

1. `td/telegram/AiComposeToneExample.hpp`
2. any generated or split-owner files introduced by Phases 1 through 4

Requirements:

1. Treat the compile-fix row as a closeout guard after owners exist locally.
2. Do not import upstream's broken intermediate owner state.
3. Regenerate all affected generated artifacts and rerun targeted build/tests before release.
4. Run the static-analysis, SonarCloud, and documentation gates across the final touched slice before declaring release readiness.
5. Final release readiness includes a rendered Doxygen review for the public API deltas and a final pass over any
   touched logging points for secrecy and subsystem integration.

Exit gate:

1. Clean focused build, clean diagnostics for touched files, zero new lint/static/security findings, and passing focused CTest slice.

### Phase 6: Fork record and maintenance policy closeout

Primary files:

1. `CHANGELOG.md`
2. `docs/Documentation/FORK_MAINTENANCE_POLICY.md`
3. `docs/Plans/UPSTREAM_BACKPORT_GATING_PLAN_2026-05-08.md`
4. `docs/Plans/UPSTREAM_BACKPORT_MANIFEST_2026-05-08.md`

Requirements:

1. Add the final Wave 5 implementation accounting to `CHANGELOG.md` so the fork-owned backport record shows
   which deferred rows were implemented exactly, adapted locally, or reconciled as no-op.
2. Update `docs/Documentation/FORK_MAINTENANCE_POLICY.md` when the Wave 5 intake changes the recommended
   upstream-baseline, reference-branch, or downstream closeout workflow for future waves.
3. Keep the policy and changelog aligned with the exact baseline tag, the final row accounting, and the
   vendor/security-fork model already documented in this repository.
4. No release-ready Wave 5 closeout exists until these fork-owned documentation records are updated in the
   same lane as the final implementation evidence.

Exit gate:

1. `CHANGELOG.md` and `docs/Documentation/FORK_MAINTENANCE_POLICY.md` both reflect the completed Wave 5 intake.
2. Canonical accounting documents stay synchronized with the final implementation outcome.

## 12. Documentation and Release Closeout

1. Update the changelog backport record to account for each of the 18 deferred `W5-AI` rows plus the
   cross-wave compile-closeout row.
2. Record the exact baseline tag in the closeout note.
3. Add a downstream client migration note summarizing newly available text-composition APIs and any
   compatibility-preserving aliases or validation differences.
4. Keep the canonical manifest and gating plan synchronized with the final implementation outcome.
5. Include a closeout evidence summary covering tests passed, sanitizer status if run, `.clang-tidy`/format status,
   SonarCloud outcome, SAST outcome, documentation gate outcome, and residual accepted risks.
6. If public API changed, list the exact Doxygen source-of-truth files updated and the rendered page that was reviewed.
7. Treat `CHANGELOG.md` and `docs/Documentation/FORK_MAINTENANCE_POLICY.md` as mandatory final-wave deliverables,
   not optional afterthoughts.

## 13. Explicit Non-Goals

1. No ancestry rewriting or fake merge strategy to reduce GitHub `behind` counters.
2. No bulk sync of unrelated upstream product lanes.
3. No weakening of local fail-closed behavior merely to match upstream implementation shape.
4. No release of partially introduced owner classes without their matching request, update, and preview contracts.
5. No transport-fingerprint behavior changes are introduced under this lane; only non-regression validation
   against existing stealth fixture/runtime slices is allowed.
