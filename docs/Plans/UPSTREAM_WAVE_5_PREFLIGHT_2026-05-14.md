<!--
SPDX-FileCopyrightText: Copyright 2026 telemt community
SPDX-License-Identifier: MIT
telemt: https://github.com/telemt
telemt: https://t.me/telemtrs
-->

# Upstream Wave 5 Preflight Annex (W5-AI)

Date: 2026-05-14
Scope: Wave 5 text-composition / AI-compose bundle plus semantically required adjacent rows only
Backlog anchor: `original..upstream/master`
Canonical manifest: `docs/Plans/UPSTREAM_BACKPORT_MANIFEST_2026-05-08.md`
Canonical gating plan: `docs/Plans/UPSTREAM_BACKPORT_GATING_PLAN_2026-05-08.md`
Status: Historical preflight archive; the repository audit recorded in
`docs/Plans/UPSTREAM_BACKPORT_GATING_PLAN_2026-05-08.md` Sections `0.1`, `0.2`, and `0.3.6`
closes the wave-level W5 backlog while this annex remains the frozen text-composition scope/risk/RED record
Historical note: The activation rules below are preserved as archival planning criteria, not live blockers.

## 1. Objective

This annex originally froze one bounded Wave 5 slice: the upstream text-composition / AI-compose capability bundle
centered on structured text-composition styles, style example objects, create/edit/search/add/remove/delete
style APIs, config-controlled limits, update delivery, and text-composition link / preview exposure.

This annex no longer serves as a live direct-sync gate. It records the exact upstream commit set, the verified
current local baseline, the local divergence points, the risk model, and the RED-first test inventory that
must exist before any implementation is attempted.

Wave 5 is not a stealth-transport lane. It has no real-fixture DPI evidence advantage by itself, so it stays
product-driven and deferred unless the product objective is strong enough to justify a large schema and state
surface expansion.

## 2. Scope Freeze

### 2.1 In Scope (exact rows / commits)

| Sublane | Exact Rows / Commits | Local Owners / Seams | Why It Stays Coupled |
|---|---|---|---|
| W5-A foundation already represented locally | 5:`c3a6ecea6`, 10:`d747885cb` | `td/telegram/MessagesManager.cpp`, `td/telegram/TranslationManager.*` | Upstream introduced `InputText` and `get_input_text` and also rewired message-translation call sites to the shared input container; these rows remain evidence inputs, not direct cherry-pick targets |
| W5-B structured tone model and persistence | 63:`d72be7609`, 71:`176915344`, 72:`a05aeeb9c`, 75:`fc903aab3`, 76:`3ba1e630b`, 78:`327531a54`, 79:`ee93de50b` | `CMakeLists.txt`, `SplitSource.php`, `td/generate/scheme/td_api.tl`, missing `td/telegram/AiComposeTone.*`, missing `td/telegram/AiComposeToneExample.*`, `td/telegram/TranslationManager.*` | Introduces the missing owner classes, expands the style payload shape, and changes persistence / user-loading assumptions |
| W5-C control-plane options and limits | 70:`528988dd9`, 88:`0c6ea7e09`, 94:`9571c262f`, 95:`ff051c4dc`, 96:`df4bfee0d` | `td/generate/scheme/td_api.tl`, `td/telegram/ConfigManager.*`, `td/telegram/OptionManager.cpp`, `td/telegram/Premium.cpp` | These rows together define the option namespace, app-config mapping, premium limit type, and length / count ceilings; `9571c262f`, `ff051c4dc`, and `df4bfee0d` also extend `td_api.tl` with the premium limit type and updated doc constraints; they are unsafe as independent cherry-picks |
| W5-D request surface and update lifecycle | 73:`b77099227`, 74:`c96e67c38`, 77:`23971a844`, 80:`8a7d707ee`, 82:`27b1ee8cd`, 89:`64972181c`, 97:`86d375553`, 98:`3e10a17e6`, 99:`58d72a0e8`, 100:`6113d3822`, 101:`36e726f93`, 195:`a26ccb8c5` | `td/generate/scheme/td_api.tl`, `td/telegram/Requests.*`, `td/telegram/TranslationManager.*`, `td/telegram/UpdatesManager.*`, `td/telegram/cli.cpp` | Request-handler commits (`23971a844` through `36e726f93`) each extend `td_api.tl` with the corresponding new API; reload ordering, update delivery, and the later upstream bugfix all participate in one state machine; `c96e67c38`, `58d72a0e8`, and `a26ccb8c5` must be reviewed as an inseparable triple |
| W5-E link / preview / slug-resolution / build edge | 81:`990b821c8`, 83:`f5b5a6e11`, 194:`3678c2d42`, 199:`49b3bcbb6` | `td/generate/scheme/td_api.tl`, `td/telegram/AiComposeTone.cpp`, `td/telegram/LinkManager.cpp`, `td/telegram/LinkManager.h`, `td/telegram/WebPagesManager.cpp`, `test/link.cpp`, missing `td/telegram/AiComposeToneExample.hpp` | The deep-link and preview surfaces are externally reachable, the link row also changes slug fallback resolution in `AiComposeTones`, and the compile-fix row proves upstream shipped at least one broken intermediate state |

### 2.2 Explicitly Out of Scope

1. Any Wave 1 transport / parser research, including TLS, proxy links, or fixture-driven stealth work.
2. Poll, guest-bot, managed-bot, paid-media, or residual mixed-queue rows outside the exact commit set above.
3. Any attempt to treat this lane as generic "hardening". Wave 5 is a product and schema expansion lane first.
4. Any direct cherry-pick of a row in this lane without first creating the RED suites in Section 8.

Freeze rule: no commit outside the exact rows above may be folded into a Wave 5 branch unless this annex is
revised and re-approved.

## 3. Mission-Fit and Current Repository State

### 3.1 Why W5-AI Was Initially Deferred

Wave 5 touches `td_api.tl`, request routing, update routing, persisted state, config versioning, CLI entry
points, and new externally visible link / preview types. That is the opposite of a bounded correctness fix.
It can still be worth doing, but only as a disciplined product lane with explicit contracts, a migration plan,
and an adversarial test matrix.

Unlike transport-facing waves, Wave 5 has no real traffic-fixture authority to lean on. The absence of such
evidence is itself a gating signal: this lane is not justified by DPI pressure alone and must not be framed as
stealth work.

### 3.2 Repository-Resident Baseline Already Present

The current tree already contains a smaller text-composition baseline:

1. `td/generate/scheme/td_api.tl` already defines `textCompositionStyle(name, custom_emoji_id, title)` and
   `updateTextCompositionStyles`.
2. `td/telegram/TranslationManager.*` already defines `InputText`, `get_input_text(...)`,
   `sanitize_ai_compose_styles(...)`, `on_update_ai_compose_styles(...)`, persisted `ai_compose_styles_`, and
   `get_update_text_composition_styles()`.
3. `td/telegram/Requests.*` and `td/telegram/cli.cpp` already expose `composeTextWithAi` and `fixTextWithAi`.
4. `td/telegram/ConfigManager.*` and `td/telegram/OptionManager.cpp` already carry
   `text_composition_style_example_count`, the `aicompose_tone_examples_num` mapping, and
   `ConfigManager::AppConfig::CURRENT_VERSION = 122`.
5. The old `ai_compose_styles` app-config key is already removed from the local config path.

### 3.3 Verified Local Divergence From Upstream Wave 5

The current tree does not contain the main upstream Wave 5 owner classes at all:

1. There is no local `td/telegram/AiComposeTone.cpp`.
2. There is no local `td/telegram/AiComposeTone.h`.
3. There is no local `td/telegram/AiComposeTone.hpp`.
4. There is no local `td/telegram/AiComposeToneExample.cpp`.
5. There is no local `td/telegram/AiComposeToneExample.h`.
6. There is no local `td/telegram/AiComposeToneExample.hpp`.

Therefore the upstream bundle is not a small patch over existing local owners. It is a class introduction,
state-model migration, and schema expansion.

### 3.4 Existing Local Tests Already Relevant

The current tree already pins part of the Wave 5 foundation:

1. `test/translation_manager.cpp` validates `sanitize_ai_compose_styles(...)` against malformed triples.
2. `test/ai_compose_styles_config_removal_{contract,adversarial,integration,light_fuzz,stress}.cpp` pins the
   absence of the deprecated `ai_compose_styles` app-config key and the current config version seam.
3. `test/text_composition_style_example_count_option_{contract,adversarial,integration,light_fuzz,stress}.cpp`
   pins the example-count option mapping, default value, and version bump.

These tests are not enough for Wave 5, but they prove the lane must preserve an existing local contract rather
than start from an empty state.

## 4. Contract Snapshot (W5-AI)

### 4.1 Contract W5-C-001: Existing Style Triple Ingress and Persistence

Boundary: `TranslationManager::sanitize_ai_compose_styles(...)`, `on_update_ai_compose_styles(...)`,
`get_update_text_composition_styles()`, and the current binlog key `ai_compose_styles`.

- Inputs: flat `vector<string>` triples from config or database, plus the current in-memory cache.
- Outputs: only valid triples survive, and client-facing `updateTextCompositionStyles` objects are derived
  from that sanitized cache.
- Side effects: binlog write, binlog erase, and `Td::send_update(...)` when the effective state changes.
- Preconditions: the source is untrusted and may contain malformed field counts, invalid signed identifiers,
  or empty titles.
- Postconditions: malformed triples are dropped, equal-state updates do not fan out again, and empty effective
  state erases the persisted key.

### 4.2 Contract W5-C-002: Structured Tone Model Migration

Boundary: the future `AiComposeTones` owner plus any migration path from the existing flat triple cache.

- Inputs: the current triple-only store, future structured style objects, and upgrade-time mixed-version data.
- Outputs: one deterministic in-memory model plus one backward-compatible client-facing projection.
- Side effects: persisted state parse/store, potential user dependency loading, and cache replacement.
- Preconditions: old and new serialized shapes may coexist during upgrade or partial rollback.
- Postconditions: no silent field truncation, no cross-user leakage, and no acceptance of partially migrated
  records that cannot round-trip.

### 4.3 Contract W5-C-003: Text-Composition Style Request Boundary

Boundary: `Requests::on_request(...)` for create/edit/search/get/add/remove/delete text-composition style APIs,
plus the owning `TranslationManager` helpers.

- Inputs: user-supplied titles, prompts, style identifiers, example selectors, and optional emoji identifiers.
- Outputs: a specific `td_api` result object or a specific user-visible error.
- Side effects: at most one RPC and one state refresh per accepted request.
- Preconditions: the caller is authorized, non-bot, and the current option limits are loaded.
- Postconditions: invalid or oversized input is rejected before RPC dispatch, and failed requests do not mutate
  local cache state.

### 4.4 Contract W5-C-004: Update and Reload Ordering

Boundary: `UpdatesManager` handling of `updateAiComposeTones`, `reload_ai_compose_tones(...)` sequencing,
timer-driven refresh, and `send_update_text_composition_styles()`.

- Inputs: server updates, local create/edit/delete success paths, and periodic reload triggers.
- Outputs: one coherent text-composition snapshot exposed to clients.
- Side effects: timer scheduling, cache replacement, promise completion, and update emission.
- Preconditions: updates may be repeated, reordered, or arrive while a reload is already in flight.
- Postconditions: stale reloads must not overwrite newer local state, reload-triggering update handlers must
   still resolve their incoming `Promise<Unit>`, and `c96e67c38` plus `a26ccb8c5` are treated as one
   correctness boundary.

### 4.5 Contract W5-C-005: Deep-Link and Preview Exposure

Boundary: `internalLinkTypeTextCompositionStyle` parsing plus `linkPreviewTypeTextCompositionStyle` emission.

- Inputs: user-controlled base64url style slugs, percent-encoded link forms, preview metadata, and cache-miss
   compose / link flows that may fall back to `inputAiComposeToneSlug`.
- Outputs: a structured internal link or preview object, or a fail-closed error.
- Side effects: link routing only; no mutation is allowed before full validation.
- Preconditions: accepted style names are slug-like identifiers, not arbitrary UTF-8 titles.
- Postconditions: invalid or too-short slugs are rejected deterministically, generated links round-trip through
   the same slug validation rules, and cache-miss fallback only uses the explicit slug path rather than silently
   aliasing another style.

### 4.6 Contract W5-C-006: Control-Plane Option Cohesion

Boundary: `ConfigManager`, `OptionManager`, and `Premium` text-composition style count / length / premium-limit
surfaces.

- Inputs: app-config integers, internal defaults, and premium-limit enum exposure.
- Outputs: one coherent set of internal options plus one monotonic app-config version.
- Side effects: option updates and client-visible premium limit metadata.
- Preconditions: version changes are coupled to option mapping additions.
- Postconditions: no option is added without its mapping, default, and version ownership being pinned together.

## 5. Upstream Delta Findings and Local Adaptation Decisions

These findings are based on the exact row set above and the verified current local baseline.

1. Rows `c3a6ecea6` and `d747885cb` are already functionally represented in the local
   `TranslationManager::InputText` and `get_input_text(...)` seams, and row `c3a6ecea6` also rewires
   `MessagesManager::translate_message_text(...)` through that shared input container. They remain upstream
   evidence, not direct import candidates.
2. The upstream Wave 5 owner classes do not exist locally. Any implementation here must introduce new local
   owners instead of pretending that upstream is patching an existing local `AiComposeTone` abstraction.
3. The current local persistence model is a flat triple cache in `TranslationManager`, while the upstream bundle
   expands the style payload shape through custom-style support, style examples, and `english_example` data.
   This is a migration problem, not a string-field addition.
4. The local control plane already consumed the old-key removal (`528988dd9`) and example-count option
   foundation (`0c6ea7e09`) and tied them to `ConfigManager::AppConfig::CURRENT_VERSION = 122`. Upstream then
   continues the same lane with explicit version steps `123` (`9571c262f`), `124` (`ff051c4dc`), and `125`
   (`df4bfee0d`). Therefore these rows cannot be cherry-picked one by one; they must be adapted as one coherent
   versioned extension spanning ConfigManager, OptionManager, and Premium limit exposure.
5. `c96e67c38`, `58d72a0e8`, and `a26ccb8c5` form an inseparable triple across the `updateAiComposeTones`
   lifecycle. `c96e67c38` first introduced the update handler calling `reload_ai_compose_tones()` (no args)
   while never resolving the incoming `Promise<Unit>`. `58d72a0e8` changed the function signature to
   `reload_ai_compose_tones(Promise<Unit>&&)` and updated both call sites to `Auto()`, but the on-update
   handler still did not resolve its update promise. `a26ccb8c5` added the missing
   `promise.set_value(Unit())`. Any cherry-pick of `c96e67c38` without both `58d72a0e8` and `a26ccb8c5`
   would import a broken-or-non-compiling state.
6. `990b821c8` is not only a LinkManager route addition. It also changes `AiComposeTones::get_input_ai_compose_tone`
   to fall back to `inputAiComposeToneSlug(name)` when the requested style is not cached locally but the slug is a
   valid base64url identifier. This makes the row a parse and resolution boundary, not a cosmetic link feature.
7. The upstream `addstyle` route accepts only base64url slugs of length at least `8`, both in query form
   (`tg://addstyle?slug=...`) and path form (`/addstyle/<slug>`), and the reverse-link generator applies the same
   validation before producing a link. This exact slug contract should be treated as authoritative evidence for the
   local threat model.
8. `f5b5a6e11` is not purely presentational. It extends `WebPagesManager::WebPage` persistence with
   `custom_emoji_ids_` and emits `linkPreviewTypeTextCompositionStyle` only when the cached preview resolves to a
   single custom emoji; otherwise it requests a reload. The preview row therefore changes stored web-page state and
   reload behavior, not only `td_api.tl`.
9. `49b3bcbb6` shows that upstream shipped at least one broken intermediate `AiComposeToneExample.hpp` state.
   This is direct evidence that intermediate Wave 5 cherry-picks are unsafe even when they look small.
10. Any eventual implementation must be adapted to the repository's existing C++23 and actor-model rules. The
   upstream class shapes are research input only; they are not style or architecture authority.
11. Direct cherry-pick remains forbidden for the entire lane. The only acceptable path is a bounded local
   adaptation after the RED suite exists and after the sublane order in Section 9 is accepted.
12. The precise upstream option defaults are now confirmed from code: `text_composition_style_title_length_max`
   = **12**, `text_composition_style_prompt_length_max` = **1024**, `added_text_composition_style_max` = **5**
   (non-premium) / **20** (premium). These values deviate substantially from the static doc-comment figures in
   `23971a844` (1-64 for title, 1-512 for prompt) which were replaced by the dynamic option references in
   `ff051c4dc` and `df4bfee0d`. Any W5-R-002 adversarial test suite must use these exact ceiling values.
   The app-config key mappings are: `aicompose_tone_title_length_max` → `text_composition_style_title_length_max`
   (version 124), `aicompose_tone_prompt_length_max` → `text_composition_style_prompt_length_max` (version 125).
13. `d72be7609` expands `textCompositionStyle` from 3 fields (name, custom_emoji_id, title) to 8 fields by
   adding `is_custom`, `is_creator`, `install_count`, `prompt`, and `creator_user_id`; `ee93de50b` then adds
   `english_example`. The `prompt` field is the private AI prompt that must only be populated for styles where
   `is_creator` is true. The `creator_user_id` field is the user dependency resolved by `3ba1e630b` via
   `Dependencies::resolve_force` on startup; if resolution fails, the entire `AiComposeTones` cache is
   discarded and a reload is triggered. An adversary who can inject an unresolvable `creator_user_id` into
   the local binlog can force repeated cache-wipe / reload cycles (W5-R-008 attack surface).

## 6. Dependent Audit (must remain valid)

If Wave 5 ever activates, these local seams must be audited together instead of being rediscovered ad hoc:

1. `td/generate/scheme/td_api.tl`
2. `td/telegram/TranslationManager.h`
3. `td/telegram/TranslationManager.cpp`
4. `td/telegram/MessagesManager.cpp`
5. `td/telegram/Requests.h`
6. `td/telegram/Requests.cpp`
7. `td/telegram/cli.cpp`
8. `td/telegram/ConfigManager.h`
9. `td/telegram/ConfigManager.cpp`
10. `td/telegram/OptionManager.cpp`
11. `td/telegram/Premium.cpp`
12. `td/telegram/UpdatesManager.h`
13. `td/telegram/UpdatesManager.cpp`
14. `td/telegram/LinkManager.h`
15. `td/telegram/LinkManager.cpp`
16. `td/telegram/WebPagesManager.cpp`

If the lane moves beyond planning, the future `AiComposeTone.*` and `AiComposeToneExample.*` owners must be
added to this dependent audit immediately.

## 7. Risk Register

| Risk ID | Contract | Category | Attack / Failure Scenario | Impact | Must Be Guarded By |
|---|---|---|---|---|---|
| W5-R-001 | W5-C-001 / W5-C-002 | Persistence / migration | Old triple-only data is parsed as a newer structured style and silently truncates or misroutes fields | State corruption / data loss | contract, integration, adversarial migration tests |
| W5-R-002 | W5-C-003 / W5-C-006 | Input validation | Oversized title, prompt, or example payload bypasses the configured option limits; note precise upstream defaults: title_max=12, prompt_max=1024, added_styles_max=5/20 (non-premium/premium) | DoS / inconsistent local state | request contract, adversarial, light fuzz |
| W5-R-003 | W5-C-005 | Link parsing / slug resolution | Short, malformed, or percent-encoded base64url slugs are accepted, or cache-miss fallback resolves the wrong style slug | Identity spoofing / fail-open routing | link adversarial, link light fuzz |
| W5-R-004 | W5-C-004 | Update ordering / completion | A reload-triggering update neither resolves its incoming promise nor preserves newer local state | Stalled update processing / stale client state | update integration, reload stress |
| W5-R-005 | W5-C-002 / W5-C-003 | User reference loading | Structured style records carry user references that are loaded or exposed without ownership checks | Privacy leak / access-control confusion | integration, adversarial ownership tests |
| W5-R-006 | W5-C-006 | Config cohesion | New options land without a matching version bump, default, or mapping | Silent control-plane drift | contract, adversarial config tests |
| W5-R-007 | W5-C-003 / W5-C-004 | Partial API rollout | New `td_api` types are generated without complete request or update handlers | Broken public API / compile or runtime mismatch | contract, integration, build validation |
| W5-R-008 | W5-C-004 / W5-C-005 | Resource exhaustion | Periodic reload or repeated malformed links cause unbounded retries, cache churn, or growth | CPU / memory exhaustion | stress, light fuzz, repeated malformed-input tests |

## 8. Mandatory RED / Survive Suite Before Any Implementation

No Wave 5 code work is authorized until the following test files exist as separate files and fail for the right
reason against the current tree.

### 8.1 Contract

1. `test/w5_text_composition_model_contract.cpp`
2. `test/w5_text_composition_request_contract.cpp`
3. `test/w5_text_composition_update_contract.cpp`
4. `test/w5_text_composition_config_contract.cpp`

### 8.2 Adversarial

1. `test/w5_text_composition_request_adversarial.cpp`
2. `test/w5_text_composition_link_adversarial.cpp`
3. `test/w5_text_composition_migration_adversarial.cpp`

### 8.3 Integration

1. `test/w5_text_composition_integration.cpp`
2. `test/w5_text_composition_migration_integration.cpp`
3. `test/w5_text_composition_option_integration.cpp`

### 8.4 Light Fuzz

1. `test/w5_text_composition_light_fuzz.cpp`
2. `test/w5_text_composition_link_light_fuzz.cpp`

### 8.5 Stress

1. `test/w5_text_composition_reload_stress.cpp`
2. `test/w5_text_composition_update_stress.cpp`

Required attack focus for the RED suite:

1. Field-count truncation and mixed-version persisted payloads.
2. Maximum-length title / prompt / example inputs, exact-boundary values, and limit-plus-one rejection.
3. Malformed, repeated, and reordered `updateAiComposeTones` deliveries.
4. Short or malformed base64url slugs, embedded-NUL query forms, and percent-encoding abuse across `addstyle`
   link parsing and generation.
5. Cache-miss deep-link or compose flows that fall back to `inputAiComposeToneSlug` instead of a cached style.
6. Repeated reload scheduling under load to expose leaks, unfulfilled update promises, or stale overwrite races.

## 9. Historical Planning Decision and Execution Order

| Sublane | Historical Planning Status | Why | Prerequisites |
|---|---|---|---|
| W5-A foundation already represented locally | Research input only | The current tree already contains equivalent `TranslationManager` helper seams, so direct import adds no value | None |
| W5-B structured tone model and persistence | Deep review first | This is the root migration boundary and blocks every later request or update surface | Exact `AiComposeTone` storage / parse contract review |
| W5-C control-plane options and limits | Deep review first | Local version `122` is already consumed by adjacent Wave 5 foundation rows; the remaining options must travel as one versioned extension | W5-B review plus versioning decision |
| W5-D request surface and update lifecycle | Blocked until W5-B and W5-C freeze | Public API and reload logic are unsafe before the owner class and control plane are defined | W5-B, W5-C, paired review of `c96e67c38` + `a26ccb8c5` |
| W5-E link / preview / slug-resolution / build edge | Security-gated later | Externally reachable input surfaces and WebPage preview persistence should land only after the state model and update flow are already pinned | W5-B, W5-D, slug and preview threat review |

Historical recommended execution order if the product objective were approved:

1. Freeze the `AiComposeTone` / `AiComposeToneExample` storage and migration contract.
2. Freeze the ConfigManager / OptionManager / Premium option set and versioning plan.
3. Write the full RED suite in Section 8 and confirm it fails against the current tree for the expected reasons.
4. Implement the W5-B state-model slice first, then the W5-C control plane.
5. Implement W5-D only with `c96e67c38` and `a26ccb8c5` treated as one inseparable boundary.
6. Implement W5-E last, after the link parser threat review passes.
7. Treat `49b3bcbb6` as the final build-stability cleanup only after the full lane compiles under the adapted
   local design.

Historical recommendation at publication time:

1. Publish this annex and keep Wave 5 in planning mode.
2. Permit source-diff review only for W5-B and W5-C until a product objective is approved.
3. Reject any request to cherry-pick individual Wave 5 rows ahead of their parent sublane.