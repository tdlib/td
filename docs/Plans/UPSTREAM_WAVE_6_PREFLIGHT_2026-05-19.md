<!--
SPDX-FileCopyrightText: Copyright 2026 telemt community
SPDX-License-Identifier: MIT
telemt: https://github.com/telemt
telemt: https://t.me/telemtrs
-->

# Upstream Wave 6 Preflight Annex (W6-M)

Date: 2026-05-19
Scope: Wave 6 managed-bot access-settings capability bundle plus mandatory layer prerequisite planning
Backlog anchor: `original..upstream/master`
Canonical manifest: `docs/Plans/UPSTREAM_BACKPORT_MANIFEST_2026-05-08.md`
Canonical gating plan: `docs/Plans/UPSTREAM_BACKPORT_GATING_PLAN_2026-05-08.md`
Status: Historical preflight archive; the repository audit recorded in
`docs/Plans/UPSTREAM_BACKPORT_GATING_PLAN_2026-05-08.md` Sections `0.1`, `0.2`, and `0.3.9`
closes the wave-level W6 backlog while this annex remains the frozen access-settings scope/risk/RED record
Historical note: The activation rules below are preserved as archival planning criteria, not live blockers.
Current-state note (2026-05-24): this annex captures a pre-implementation snapshot. The layer prerequisite and
managed-bot access-settings implementation are now repository-resident (`MTPROTO_LAYER = 225`,
`BotAccessSettings`/request-manager seams present); see
`docs/Plans/UPSTREAM_BACKPORT_GATING_PLAN_2026-05-08.md` Section `0.3.9` for executable reconciliation evidence.

## 1. Objective

This annex originally froze the deferred Wave 6 managed-bot access-settings bundle and defined the exact activation
preconditions that would have applied before any implementation work could start.

This annex no longer serves as a live direct-cherry-pick gate. It records:

1. Exact Wave 6 commit scope and current local divergence.
2. Layer-225 prerequisite dependency and migration blast radius.
3. Security risk matrix for managed-bot access transitions.
4. Mandatory RED-first test inventory required before implementation.

## 2. Scope Freeze

### 2.1 In Scope (exact W6-M rows/commits)

1. 84:`19292458f` - Add `td_api::botAccessSettings`.
2. 85:`3819fded5` - Rename `td_api::getBotToken` to `getManagedBotToken`.
3. 86:`b6aa479a9` - Add `td_api::getManagedBotAccessSettings`.
4. 87:`83506493e` - Add `td_api::setManagedBotAccessSettings`.

Mandatory prerequisite (outside W6 lane but required for W6 activation):

1. 41:`7e3361e5a` - Update layer to 225 (introduces constructor support required by W6 access-settings RPCs).

### 2.2 Explicitly Out of Scope

1. Any non-W6 capability expansion (W3 poll-media, W4 guest-query, W5 text-composition).
2. Any direct cherry-pick of W6 rows without satisfying layer and charter gates.
3. Any policy widening that weakens deny-by-default ownership checks.
4. Any runtime behavior that allows implicit privilege inheritance between manager bot and managed bot.

Freeze rule: no W6 execution branch may be created until Sections 4, 6, and 7 gates are accepted.

## 3. Current Baseline and Divergence

### 3.1 Repository-Resident W6 Hardening Already Landed

The repository currently contains a bounded W6 compatibility/hardening slice for token export:

1. `td/generate/scheme/td_api.tl` now exposes `getManagedBotToken` and keeps legacy `getBotToken`.
2. `td/telegram/Requests.h` and `td/telegram/Requests.cpp` route both token endpoints through one manager path.
3. `td/telegram/BotInfoManager.cpp` fail-closes token export for non-owned bots (`Bot must be owned`).
4. `td/telegram/cli.cpp` exposes managed aliases (`gmbt`/`gmbtr`) while retaining legacy aliases.

### 3.2 Missing W6 Access-Settings Owners and API Surface

The current tree does not contain the full managed-bot access-settings implementation owners:

1. No local `td/telegram/BotAccessSettings.cpp`.
2. No local `td/telegram/BotAccessSettings.h`.
3. No local `td_api::botAccessSettings` class/object surface in `td_api.tl`.
4. No local request handlers for `getManagedBotAccessSettings` or `setManagedBotAccessSettings`.

Therefore full W6 activation is not a patch-level extension; it is a schema and runtime boundary expansion.

### 3.3 Verified Layer Prerequisite Mismatch

Wave 6 access-settings RPCs are coupled to layer-225 constructors introduced by `7e3361e5a`:

1. `bots.accessSettings#dd1fbf93`.
2. `bots.getAccessSettings#213853a3`.
3. `bots.editAccessSettings#31813cd8`.

The local layer is still pinned to 224 (`td/telegram/Version.h`: `MTPROTO_LAYER = 224`).

Activation without layer migration would be non-compilable or force unverifiable stubs.

## 4. Layer-225 Migration Blast Radius

### 4.1 Why Layer Migration Is a W6 Activation Gate

W6 read/write access-settings methods require generated TL constructor symbols from layer 225.
Without those symbols, W6 cannot be safely implemented as real RPC paths.

### 4.2 Verified Prerequisite File Blast Radius (commit `7e3361e5a`)

| Blast Zone | Files Touched by Layer Prerequisite | Why It Matters for W6 Activation | Risk Level |
|---|---|---|---|
| TL schema and constructor generation | `td/generate/scheme/telegram_api.tl` | Source of `bots.accessSettings`, `bots.getAccessSettings`, `bots.editAccessSettings` constructors | Critical |
| Protocol version gate | `td/telegram/Version.h` | Changes runtime layer constant and negotiation assumptions | Critical |
| Update routing | `td/telegram/UpdatesManager.cpp`, `td/telegram/UpdatesManager.h` | Layer migration can alter update compatibility surfaces and ordering | High |
| User capability surfaces | `td/telegram/UserManager.cpp` | Bot capability fields and parsing may shift under layer migration | High |
| Top dialog categorization | `td/telegram/TopDialogCategory.cpp`, `td/telegram/TopDialogManager.cpp` | Existing W4 guest-bot behavior is sensitive to layer/schema changes | Medium |
| Poll/media behavior | `td/telegram/PollManager.cpp` | Existing W3 hardening paths may regress if layer migration is unvalidated | Medium |
| Business/auth/notifications/webpage seams | `td/telegram/AuthManager.cpp`, `td/telegram/BusinessConnectionManager.cpp`, `td/telegram/NotificationManager.cpp`, `td/telegram/WebPagesManager.cpp`, `td/telegram/TranslationManager.cpp` | Cross-feature surfaces can regress even if W6 code is isolated | Medium |

### 4.3 Blast-Radius Gating Rule

Before any W6 access-settings implementation begins:

1. A dedicated layer-225 migration preflight must freeze scope and rollback strategy.
2. Cross-wave regression gates must include focused W3, W4, and W5 suites because those seams are touched by prerequisite layer migration.
3. No access-settings code should be written until layer migration compiles and generated schema diffs are reviewed.

## 5. Contract Snapshot (W6-M)

### 5.1 Contract W6-C-001: Managed-Bot Ownership Boundary

Boundary: all managed-bot token/access-settings read/write entry points.

- Inputs: manager identity, target bot identity, requested operation.
- Outputs: requested data/change only for owned managed bots.
- Side effects: outbound RPCs only after ownership is proven.
- Postcondition: non-owned bot operations fail closed before network dispatch.

### 5.2 Contract W6-C-002: Access-Settings Read Semantics

Boundary: `getManagedBotAccessSettings` request path.

- Inputs: managed bot identifier.
- Outputs: one `botAccessSettings` object with stable field semantics.
- Side effects: none beyond fetch RPC and dependency resolution.
- Postcondition: malformed or partial settings payloads are rejected, never default-accepted.

### 5.3 Contract W6-C-003: Access-Settings Write Transition Semantics

Boundary: `setManagedBotAccessSettings` request path.

- Inputs: current state, requested state transition, add-users set.
- Outputs: explicit success/failure status.
- Side effects: one idempotent write RPC with auditable transition intent.
- Postcondition: ambiguous transitions (ownership race, stale state, replay) fail closed.

### 5.4 Contract W6-C-004: Layer/Constructor Integrity

Boundary: generated constructor bindings and manager query classes.

- Inputs: compiled TL schema and runtime layer constant.
- Outputs: constructor-call compatibility for all W6 RPCs.
- Side effects: none beyond build generation.
- Postcondition: if constructor symbols or layer pin are mismatched, build must fail (no fallback stubs).

### 5.5 Contract W6-C-005: Backward-Compatible Token Endpoint Transition

Boundary: `getBotToken` and `getManagedBotToken` public API paths.

- Inputs: same bot ID/revoke parameters through either endpoint.
- Outputs: same guarded behavior and response contract.
- Side effects: identical manager method invocation path.
- Postcondition: legacy route cannot bypass ownership guard.

## 6. Risk Matrix

| Risk ID | Contract | Category | Attack / Failure Scenario | Impact | Required Guard |
|---|---|---|---|---|---|
| W6-R-001 | W6-C-001 | Access control | Non-owned bot attempts read/write of managed access settings | Privilege escalation | Ownership contract + adversarial unauthorized tests |
| W6-R-002 | W6-C-003 | Replay / state integrity | Replayed or stale `setManagedBotAccessSettings` payload overwrites newer policy | Unauthorized policy rollback | Transition sequencing tests + integration state checks |
| W6-R-003 | W6-C-002 | Input validation | Malformed `add_users` list accepted as partial valid state | Policy corruption | Parse strictness tests + fuzz malformed vectors |
| W6-R-004 | W6-C-004 | Schema/layer integrity | Layer-224 runtime attempts layer-225 constructor usage | Build/runtime mismatch | Compile-time layer gate + constructor presence tests |
| W6-R-005 | W6-C-005 | Compatibility bypass | Legacy `getBotToken` route bypasses managed-ownership checks | Token disclosure | Route-equivalence contract tests |
| W6-R-006 | W6-C-003 | Concurrency | Concurrent token revoke and access-settings update produce contradictory state | Inconsistent security posture | Stress tests with interleaved transitions |
| W6-R-007 | W6-C-002 / C-003 | Resource exhaustion | Oversized user vectors or repeated update bursts trigger unbounded work | DoS | Size-bound validation + stress envelope tests |
| W6-R-008 | W6-C-001 / C-003 | Authorization confusion | Manager-bot identity/caller context drift across request/update seams | Broken least privilege | Integration tests across Requests/BotInfoManager paths |

## 7. Mandatory RED Test Inventory Before Any W6 Access-Settings Implementation

No W6 access-settings code work is authorized until these tests exist as separate files and fail for the
right reason against the current tree.

### 7.1 Contract Tests

1. `test/w6_managed_bot_access_settings_contract.cpp`
2. `test/w6_managed_bot_access_layer_contract.cpp`
3. `test/w6_managed_bot_access_token_route_contract.cpp`

### 7.2 Adversarial Tests

1. `test/w6_managed_bot_access_settings_adversarial.cpp` (unauthorized read/write attempts)
2. `test/w6_managed_bot_access_replay_adversarial.cpp` (stale/replayed transition payloads)
3. `test/w6_managed_bot_access_input_adversarial.cpp` (malformed/oversized add-users vectors)

### 7.3 Integration Tests

1. `test/w6_managed_bot_access_settings_integration.cpp` (Requests -> manager -> query path)
2. `test/w6_managed_bot_access_layer_integration.cpp` (constructor presence with layer migration)
3. `test/w6_managed_bot_access_transition_integration.cpp` (deny-by-default state transitions)

### 7.4 Light Fuzz

1. `test/w6_managed_bot_access_settings_light_fuzz.cpp`
2. `test/w6_managed_bot_access_transition_light_fuzz.cpp`

Minimum deterministic corpus: 10,000 iterations with positive, boundary, and malformed state vectors.

### 7.5 Stress

1. `test/w6_managed_bot_access_settings_stress.cpp`
2. `test/w6_managed_bot_access_concurrency_stress.cpp`

Required stress focus: repeated interleaving of revoke-token and set-access-settings operations under bounded
resource ceilings.

## 8. Historical Planning Decision and Activation Order

| Phase | Status | Description | Gate |
|---|---|---|---|
| W6-P0 | Completed | Publish this preflight annex | This document |
| W6-P1 | Blocked | Approve managed-bot security charter (deny-by-default transitions, replay/revocation semantics) | Charter approval |
| W6-P2 | Blocked | Approve layer-225 migration plan and blast-radius regression matrix | Layer preflight approval |
| W6-P3 | Blocked | Author full RED suite from Section 7 and verify RED against current tree | RED evidence |
| W6-P4 | Blocked | Implement W6 access-settings read path (`botAccessSettings`, `getManagedBotAccessSettings`) | P1 + P2 + P3 |
| W6-P5 | Blocked | Implement W6 access-settings write path (`setManagedBotAccessSettings`) with strict transition guards | P1 + P2 + P3 |
| W6-P6 | Blocked | Run Survive verification: sanitizer-clean targeted suites + bounded stress/fuzz | P4 + P5 |

Historical recommendation at publication time:

1. Keep W6 full access-settings capability in planning mode.
2. Treat `3819fded5` bounded token adaptation as complete but non-authorizing for W6 feature activation.
3. Require layer migration and charter approval before opening any W6 implementation branch.
