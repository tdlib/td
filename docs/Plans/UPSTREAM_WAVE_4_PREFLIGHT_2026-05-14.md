<!--
SPDX-FileCopyrightText: Copyright 2026 telemt community
SPDX-License-Identifier: MIT
telemt: https://github.com/telemt
telemt: https://t.me/telemtrs
-->

# Upstream Wave 4 Preflight Annex (W4-G)

Date: 2026-05-14
Scope: Wave 4 guest-bot capability bundle only (`W4-G`)
Backlog anchor: `original..upstream/master`
Canonical manifest: `docs/Plans/UPSTREAM_BACKPORT_MANIFEST_2026-05-08.md`
Canonical gating plan: `docs/Plans/UPSTREAM_BACKPORT_GATING_PLAN_2026-05-08.md`
Status: Historical preflight archive; the repository audit recorded in
`docs/Plans/UPSTREAM_BACKPORT_GATING_PLAN_2026-05-08.md` Sections `0.1`, `0.2`, and `0.3.4.a`
closes the wave-level W4 backlog while this annex remains the frozen guest-query scope/risk/RED record
Historical note: The activation rules below are preserved as archival planning criteria, not live blockers.

## 1. Objective

This annex originally froze one bounded Wave 4 slice: the upstream guest-bot capability bundle centered on guest
query delivery, guest-query answering, guest caller exposure, guest-bot top-dialog category/rating, and
strict guest-query identifier validation.

This annex no longer serves as a live broad-sync gate. It records the exact upstream commit set,
the current repository-resident adaptation seams, the risk model, and the validated RED/Survive suite
that now guards the slice.

## 2. Scope Freeze

### 2.1 In Scope (exact W4-G rows/commits)

1. 58:`57259ff9e` - Add `message.guest_bot_caller_id`
2. 59:`49e592ccc` - Add `MessagesManager::get_guest_message_object`
3. 60:`7aed695bf` - Add `td_api::updateNewGuestQuery`
4. 61:`339ff0c6c` - Add `td_api::answerGuestQuery`
5. 143:`3fc0b253d` - Add `td_api::topChatCategoryGuestBots`
6. 144:`9175d061a` - Locally update guest-bot rating
7. 145:`3fbbd52ff` - Improve naming of `userTypeBot.supports_guest_queries`
8. 166:`64d4cea86` - Check guest query identifier

### 2.2 Explicitly Out of Scope

1. Personal-chat history or personal-channel history APIs outside `W4-G`
2. Non-guest Wave 2B guest-message fixes except where already consumed by current W4 runtime seams
3. AI/Text-composition, managed-bot, poll, stealth, or link-parser lanes
4. Any direct cherry-pick outside the eight rows above

Freeze rule: no commit outside the eight rows above may be folded into a W4 execution or hardening branch
unless this annex is revised and re-approved.

## 3. Mission-Fit and Current Repository State

### 3.1 Why W4-G Is Mission-Fit

W4-G changes update ordering, message directionality semantics, top-dialog rating, and bot capability
classification across multiple managers. That makes it small in commit count but high in semantic blast
radius, which is why the gating plan classifies it as a deferred bundle until the exact seams and attack
surface are frozen.

### 3.2 Repository-Resident W4 Seams Already Present

The current tree already contains bounded W4 implementation seams in these local owners:

1. `td/generate/scheme/td_api.tl`
2. `td/generate/scheme/telegram_api.tl`
3. `td/telegram/GuestQueryQtsUpdate.h`
4. `td/telegram/GuestBotTopDialog.h`
5. `td/telegram/UpdatesManager.cpp`
6. `td/telegram/UpdatesManager.h`
7. `td/telegram/MessagesManager.cpp`
8. `td/telegram/MessagesManager.h`
9. `td/telegram/InlineQueriesManager.cpp`
10. `td/telegram/InlineQueriesManager.h`
11. `td/telegram/Requests.cpp`
12. `td/telegram/Requests.h`
13. `td/telegram/TopDialogCategory.cpp`
14. `td/telegram/TopDialogCategory.h`
15. `td/telegram/TopDialogManager.cpp`
16. `td/telegram/UserManager.cpp`
17. `td/telegram/UserManager.h`
18. `td/telegram/cli.cpp`

The current tree also already contains bounded W4 tests in these files:

1. `test/guest_query_contract.cpp`
2. `test/guest_query_adversarial.cpp`
3. `test/guest_query_integration.cpp`
4. `test/guest_query_light_fuzz.cpp`
5. `test/guest_query_runtime_harness.cpp`
6. `test/guest_query_runtime_adversarial.cpp`
7. `test/guest_query_server_result_runtime.cpp`
8. `test/guest_query_stress.cpp`
9. `test/guest_bot_top_dialog_runtime.cpp`

## 4. Contract Snapshot (W4-G)

### 4.1 Contract W4G-C-001: Guest Query QTS Update Gate

Boundary: `dispatch_guest_query_qts_update(...)` plus `UpdatesManager` guest-query update handling.

- Inputs: `query_id`, guest query main message, optional reference messages, current QTS lane.
- Outputs: exactly one of `InvalidQueryId`, `EmptyMessage`, or `UpdateSent`.
- Side effects: `Td::send_update` may happen only on the successful path.
- Preconditions: network input is untrusted and may be malformed, truncated, reordered, or replayed.
- Postconditions: non-positive identifiers are rejected before any message conversion; null reference
  conversions are skipped; null main-message conversion fails closed.

### 4.2 Contract W4G-C-002: Guest Message Conversion and Caller Emission

Boundary: `MessagesManager::get_guest_message_object(...)`, `create_message(...)`, and
`get_message_guest_sender_object(...)`.

- Inputs: a `telegram_api::Message`, plus guest/business context flags.
- Outputs: a `td_api::message` with `guest_bot_caller_id` populated from internal guest routing state when valid.
- Side effects: dialog creation, dependency registration, observable message updates.
- Preconditions: the path is bot-only and may see invalid guest caller peers.
- Postconditions: invalid guest caller peers fail closed; guest caller state is preserved through parse,
  object conversion, dependency propagation, and `update_message(...)` diffing.

### 4.3 Contract W4G-C-003: Guest Query Answer Path

Boundary: `Requests::on_request(answerGuestQuery)`, `InlineQueriesManager::answer_guest_query(...)`, and
`SetBotGuestChatResultQuery`.

- Inputs: `guest_query_id`, one `InputInlineQueryResult`, authenticated bot context.
- Outputs: `td_api::inlineMessageId` only.
- Side effects: a single `telegram_api::messages_setBotGuestChatResult` RPC.
- Preconditions: identifier must be strictly positive; the result may fail input normalization.
- Postconditions: the path must not collapse onto inline-query answer semantics or return `Unit`; invalid IDs
  are rejected before RPC dispatch.

### 4.4 Contract W4G-C-004: Guest-Bot Top Dialog Category and Rating

Boundary: `TopDialogCategory`, `TopDialogManager`, `MessagesManager::update_top_dialogs(...)`, and
`note_guest_bot_top_dialog_use(...)`.

- Inputs: top-dialog category requests, guest message metadata, cached last-message watermark.
- Outputs: dedicated `BotGuest` category behavior and bounded rating updates.
- Side effects: `on_dialog_used(TopDialogCategory::BotGuest, ...)` and monotonic guest-bot watermark updates.
- Preconditions: forwarded messages, non-bot senders, invalid guest caller routing, or non-user guest bot dialogs
  are attacker-controlled failure modes.
- Postconditions: guest-bot category never aliases inline-bot category; rating happens only for non-forward,
  monotonic, bot-backed, user-dialog guest usage routed via `my_dialog_id`.

### 4.5 Contract W4G-C-005: Guest Capability Naming and Filtering

Boundary: `userTypeBot.supports_guest_queries`, `telegram_api::user.bot_guestchat`, and guest-bot filtering in
`TopDialogManager`.

- Inputs: schema fields, parsed user bot data, category selection.
- Outputs: guest capability stays explicit and separately named from inline capability.
- Side effects: top-dialog filtering and client-visible bot capability metadata.
- Preconditions: upstream naming churn or partial field backports may blur inline and guest semantics.
- Postconditions: guest capability naming remains explicit; guest category rejects bots without username or
  `is_guestchat_bot`.

## 5. Upstream Delta Findings and Local Adaptation Decisions

These findings are based on the exact W4-G commit set and the current local implementation.

1. `64d4cea86` was adapted into `td/telegram/GuestQueryQtsUpdate.h` instead of leaving identifier validation
   inline inside `UpdatesManager`. The local helper hardens the gate from zero-only semantics to `query_id <= 0`
   and ensures conversion never starts before the identifier is accepted.
2. `7aed695bf` was adapted into a reusable dispatcher plus runtime harnesses rather than a one-off update branch.
   This local shape keeps QTS update ordering, empty-message failure, and reference-message behavior pinned in one
   place.
3. `57259ff9e` and `49e592ccc` were adapted as more than schema surface. The local tree preserves guest caller
   state through parse, `create_message(...)`, `get_guest_message_object(...)`, dependency propagation, object
   emission, and `update_message(...)` guest caller diffs.
4. `339ff0c6c` was adapted onto a dedicated `SetBotGuestChatResultQuery` handler that sends
   `telegram_api::messages_setBotGuestChatResult` and converts the RPC result into `td_api::inlineMessageId`.
   The local path intentionally does not reuse `SetInlineBotResultsQuery` or inline-query `Unit` semantics.
   Malformed result parsing is now pinned by dedicated runtime tests that exercise constructor confusion,
   truncation, trailing-data rejection, and repeated malformed-packet stress.
5. `3fc0b253d` and `3fbbd52ff` were adapted with a dedicated `BotGuest` category, `bot_guest` category name,
   `telegram_api::topPeerCategoryBotsGuestChat` input mapping, CLI exposure, and explicit `username` plus
   `is_guestchat_bot` filtering in `TopDialogManager`.
6. `9175d061a` is a local-only hardening seam. The local tree adds `GuestBotTopDialogCandidate` and
   `note_guest_bot_top_dialog_use(...)` to prevent ungated or stale guest-bot rating updates.

Verdict: direct cherry-pick remains forbidden. The current repository-resident W4 shape is an adaptation-only
baseline and is materially more defensive than a narrow upstream import.

## 6. Dependent Audit (must remain valid)

1. `td/telegram/UpdatesManager.cpp` guest-query QTS update routing
2. `td/telegram/MessagesManager.cpp` guest message parsing, conversion, dependency propagation, update diffs,
   live-location guards, and top-dialog rating
3. `td/telegram/InlineQueriesManager.cpp` guest answer RPC/result handling
4. `td/telegram/TopDialogCategory.cpp` and `td/telegram/TopDialogManager.cpp` guest category mapping/filtering
5. `td/telegram/UserManager.cpp` guest capability ingestion and bot-data exposure
6. `td/generate/scheme/td_api.tl` and `td/generate/scheme/telegram_api.tl` guest schema surfaces

Noticed but not touching in this annex:

1. Personal-chat history APIs that were not classified into `W4-G`
2. Any non-guest Wave 2B guest-message behavior outside the already consumed runtime seams
3. Fixture-driven or live-RPC response fuzzing beyond the current malformed-packet runtime harness for
   `messages_setBotGuestChatResult`

## 7. Risk Register

| Risk ID | Contract | Category | Attack/Failure Scenario | Impact | Guarded By |
|---|---|---|---|---|---|
| W4G-R-001 | C-001 | Input validation | Non-positive guest query identifier reaches conversion or update emission | Fail-open update path / replay confusion | `w4_guest_query_contract`, `w4_guest_query_adversarial`, `w4_guest_query_runtime_harness` |
| W4G-R-002 | C-001 | Update ordering | Main/reference messages are converted before identifier validation | Side effects on rejected updates | `w4_guest_query_contract`, `w4_guest_query_adversarial`, `w4_guest_query_integration` |
| W4G-R-003 | C-002 | Cross-context state propagation | Guest caller field is dropped between parse, message object emission, dependencies, or update diffing | Caller confusion / missing security context | `w4_guest_query_contract`, `w4_guest_query_integration`, `w4_guest_query_light_fuzz`, `business_guest_message_*` |
| W4G-R-004 | C-003 | Request authorization and semantics | `answerGuestQuery` accepts invalid IDs or falls back to inline-query result semantics | Wrong API contract / fail-open RPC path | `w4_guest_query_contract`, `w4_guest_query_adversarial` |
| W4G-R-005 | C-004 | Ranking integrity | Forwarded, stale, non-bot, or non-user guest messages pollute BotGuest rating | Local state pollution / category drift | `w4_guest_query_adversarial`, `w4_guest_bot_top_dialog_runtime`, `w4_guest_query_integration` |
| W4G-R-006 | C-005 | Capability confusion | Guest category aliases inline bots or accepts non-guest bots | Policy bypass / wrong category semantics | `w4_guest_query_adversarial`, `w4_guest_query_integration`, `w4_guest_query_light_fuzz` |
| W4G-R-007 | C-001/C-003 | Malformed deserialization | Truncated or corrupted guest-query TL payload parses permissively or crashes | Crash / malformed update acceptance | `w4_guest_query_runtime_adversarial` |
| W4G-R-008 | C-003 | Result-path deserialization | Corrupted or trailing-data guest-result responses parse permissively or map to a wrong `inlineMessageId` | Crash / wrong result acceptance | `w4_guest_query_server_result_runtime` |

## 8. RED/Survive Suite Already Present

### 8.1 Contract

1. `test/guest_query_contract.cpp`

### 8.2 Adversarial

1. `test/guest_query_adversarial.cpp`
2. `test/guest_query_runtime_adversarial.cpp`

### 8.3 Integration

1. `test/guest_query_integration.cpp`

### 8.4 Runtime Harness

1. `test/guest_query_runtime_harness.cpp`
2. `test/guest_bot_top_dialog_runtime.cpp`
3. `test/guest_query_server_result_runtime.cpp`

### 8.5 Light Fuzz

1. `test/guest_query_light_fuzz.cpp`
2. `test/guest_query_runtime_adversarial.cpp` deterministic bit-flip case

### 8.6 Stress

1. `test/guest_query_stress.cpp`
2. `test/guest_query_runtime_adversarial.cpp` repeated malformed-fixture stress case

## 9. Validation Snapshot (2026-05-14)

The following W4 validation slices were executed successfully from the current build tree:

1. Contract, adversarial, integration, runtime harness, server-result runtime, runtime adversarial, and guest
   top-dialog runtime tests
2. Source-level light fuzz tests
3. Source-level repeated-read stress test

Executed CTest names included the full `Test_GuestQuery*` and `Test_GuestBotTopDialogRuntime*` slices,
including:

1. `Test_GuestQueryContract_*`
2. `Test_GuestQueryAdversarial_*`
3. `Test_GuestQueryIntegration_*`
4. `Test_GuestQueryRuntimeHarness_*`
5. `Test_GuestQueryServerResultRuntime_*`
6. `Test_GuestQueryRuntimeAdversarial_*`
7. `Test_GuestQueryLightFuzz_*`
8. `Test_GuestQueryStress_*`
9. `Test_GuestBotTopDialogRuntime_*`

Result: all executed W4 tests passed.

## 10. Historical Decision Policy for W4-G

1. Direct cherry-pick remains forbidden.
2. The repository-resident W4 guest-query implementation is the bounded adaptation baseline.
3. Future W4 work must preserve the dedicated helper boundaries:
   - `dispatch_guest_query_qts_update(...)`
   - `SetBotGuestChatResultQuery`
   - `GuestBotTopDialogCandidate` / `note_guest_bot_top_dialog_use(...)`
4. Any future refactor that removes strict positive identifier gating before conversion, collapses BotGuest back
   onto inline-bot behavior, or drops guest caller propagation is a stop condition.

## 11. Historical Exit Criteria for Further W4 Work

At publication time, any additional W4 branch or scope expansion could start only when all conditions below were met:

1. Product objective for W4 guest-bot support is explicitly approved.
2. The exact active commit subset is frozen against this annex.
3. Existing W4 tests reproduce their intended fail-closed expectations before any new behavior change.
4. Any new W4 code preserves deny-by-default behavior for invalid identifiers, malformed messages, and guest-bot
   category/rating eligibility.
5. The full W4 test matrix above is rerun after the change.

No concrete defect was observed in the current bounded W4 implementation during this pass. Optional future work,
if the product objective widens, is deeper fixture-driven or live-response fuzzing for the
`messages_setBotGuestChatResult` response path beyond the now-landed malformed-packet runtime harness.