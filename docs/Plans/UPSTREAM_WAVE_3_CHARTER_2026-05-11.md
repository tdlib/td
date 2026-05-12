<!--
SPDX-FileCopyrightText: Copyright 2026 telemt community
SPDX-License-Identifier: MIT
telemt: https://github.com/telemt
telemt: https://t.me/telemtrs
-->

# Upstream Wave 3 Capability Charter

Date: 2026-05-11
Plan role: capability charter for Wave 3 (`W3-P`) before any further intake
Backlog anchor: `original..upstream/master`
Canonical manifest: `docs/Plans/UPSTREAM_BACKPORT_MANIFEST_2026-05-08.md`
Canonical gating plan: `docs/Plans/UPSTREAM_BACKPORT_GATING_PLAN_2026-05-08.md`
Status: Deferred for further intake (charter published; repository already contains bounded W3-B0 baseline and W3-B2 hardening seams/tests, but explicit product objective approval is still required before any new Wave 3 branch or scope expansion)

## 1. Objective and Gate

This charter splits the deferred Wave 3 poll/poll-media backlog into bounded sub-bundles with exact
commit membership, schema/runtime freeze boundaries, and kill-switch criteria.

This document does not authorize further Wave 3 intake by itself. A Wave 3 preflight annex and
explicit product objective approval are mandatory before any new Wave 3 branch is opened or any
deferred Wave 3 scope is widened.

Audit note (2026-05-12): the current repository already contains the bounded W3-B0 baseline and
bounded W3-B2 policy hardening seams/tests referenced by this charter. The gate above therefore
controls further intake and branch opening, not whether those repository-resident seams exist.

## 2. Lane Inventory and Split Outcome

1. Canonical `W3-P` inventory size in the manifest: 70 rows.
2. Already merged bounded hardening baseline inside this fork (kept as historical `defer` rows in the
   manifest): rows 18, 140, 142.
3. Remaining deferred execution queue after excluding baseline: 67 commits.

### 2.1 Sub-bundle Map

| Bundle ID | Name | Rows | Current Class | Bundle Objective |
|---|---|---:|---|---|
| W3-B0 | Existing Defensive Baseline | 3 | Freeze/keep | Preserve already merged fail-closed hardening seams; no re-intake |
| W3-B1 | Poll Core Routing and Option-State | 17 | Defer | Poll ownership/routing and local option-state normalization without media expansion |
| W3-B2 | Poll Policy, Voters, Statistics, Unread State | 22 | Defer | Poll visibility and voter/statistics policy semantics under deny-by-default |
| W3-B3 | Poll Media Input, Upload, File-Reference, Render | 22 | Defer | Poll media pipeline and content/file-reference integration as one security-sensitive bundle |
| W3-B4 | Cross-Cutting Layer/CLI/Hygiene Spillover | 6 | Defer | Layer bump, CLI behavior, and lane-coupled hygiene changes isolated from runtime expansion |

## 3. Exact Commit Membership

### 3.1 W3-B0 Existing Defensive Baseline (already merged, freeze/keep)

Rows/commits: 18:21275249c, 140:c81e6da9f, 142:3e78ebcd8.

### 3.2 W3-B1 Poll Core Routing and Option-State (deferred)

Rows/commits: 11:c4394fe8c, 12:b0c75c13a, 13:42ea83d4e, 14:ea683b225, 28:01fd58957,
29:72e61404c, 30:d93059e8e, 31:ff7f15184, 32:583f1b04e, 33:81fbe7a74, 37:742be8390,
55:ade6176c9, 109:57b36795f, 110:4dd8cfe6f, 111:68d952e9b, 149:1e05885fb, 156:17ac64020.

### 3.3 W3-B2 Poll Policy, Voters, Statistics, Unread State (deferred)

Rows/commits: 42:084707e99, 44:bb6574d9f, 45:7d56f9c58, 46:bcd2c683c, 47:f654c5c81,
48:1eaf2481e, 49:d6ef00fa9, 51:04498cfbb, 52:1f68a4a84, 65:271c71136, 66:978979edb,
67:ca82791de, 68:b00c67763, 69:0b9e9829b, 153:b5c87eb91, 155:e7cbde50c, 158:d51464eb2,
159:c6411b9c9, 160:dc470c164, 162:1574780ca, 186:02473d316, 187:aaea672ae.

### 3.4 W3-B3 Poll Media Input, Upload, File-Reference, Render (deferred)

Rows/commits: 105:d59ef0167, 106:65b967524, 112:145840ff2, 113:8104480e0, 114:50d50976f,
120:3a7e9d476, 121:f94495bce, 128:fa2559582, 129:7da70901a, 130:f08c9f287, 131:4989ed3a8,
133:8f783b8e5, 135:574e087fb, 136:ea8bf7ce0, 137:513ec03f8, 138:06cf9ea87, 139:c6eb7d1da,
141:bf4bfcf8e, 150:b2fd2e468, 151:8ff835b82, 152:410f57214, 181:4a92ce7b4.

### 3.5 W3-B4 Cross-Cutting Layer/CLI/Hygiene Spillover (deferred)

Rows/commits: 4:aea46d7b1, 41:7e3361e5a, 164:9b9bae910, 188:293990c9a, 189:66cf68106,
192:def0fb1e5.

## 4. Schema-Freeze and Runtime Ownership Table

| Bundle ID | td_api.tl and schema freeze surface | Request/update entry points | Owning runtime seams | Persistence surfaces | Kill-switch gate |
|---|---|---|---|---|---|
| W3-B0 | No new schema intake; freeze current merged shape | Existing quick-reply and forwarded-statistics paths only | `MessageContent`, `QuickReplyManager`, `MessagesManager`, `PollManager` | Existing poll runtime state only | Any regression of fail-closed behavior in existing Wave 3 suites blocks all Wave 3 work |
| W3-B1 | Freeze poll option/state fields; no poll media fields | Poll control routes (`setPollAnswer`, `stopPoll`, `addPollOption`, `deletePollOption`) via existing request plumbing | `PollManager`, `PollOption`, `MessageContent`, `MessagesManager` | Local poll option/state serialization (`PollOption.*`, `PollManager` logevent/state paths) | If malformed option/state payload can crash or bypass normalization, reject the bundle |
| W3-B2 | Freeze to policy and statistics objects only (`can_get_vote_statistics`, restriction reasons, unread state flags) | `getPollVoteStatistics`, message property/update emission paths | `PollManager`, `MessageContent`, `MessagesManager`, `Requests` | Poll voter/statistics visibility and unread counters in runtime/persisted message state | Any fail-open visibility or ambiguous eligibility state without explicit deny-by-default handling blocks intake |
| W3-B3 | Freeze poll-media schema additions (`inputMessagePoll.media`, `inputPollTypeQuiz.explanation_media`, `inputPollOption.media`) as one atomic review scope | Send/upload/media conversion and retry/file-reference handlers | `MessageContent`, `PollManager`, `FileManager`, `FileReferenceManager`, `BusinessConnectionManager`, `cli.cpp` | Message-content media/file-id remotes, thumbnail/file-reference refresh, upload mapping | Any user-controlled path from unknown poll/file state to crash, unintended media emission, or silent file-reference bypass blocks intake |
| W3-B4 | No schema expansion unless required as explicit prerequisite for approved B1-B3 branch | `cli.cpp` poll command paths; protocol layer integration points | `cli.cpp`, protocol layer touch points, minor runtime seams touched by spillover commits | None new by default; must not widen persisted format | If layer/CLI/hygiene changes alter runtime semantics without isolated compatibility evidence, reject direct import |

## 5. Kill-Switch Criteria (Mandatory)

1. **W3-B0 baseline kill-switch:** if existing bounded hardening tests regress (`quick_reply_poll_media_*`,
   `poll_media_send_guard_*`, `forwarded_poll_statistics_*`, `poll_state_runtime_*`), Wave 3 activation stops.
2. **W3-B1 kill-switch:** if option/state refactors introduce parse-time abort behavior (`CHECK`/crash) or
   non-deterministic local poll option serialization, reject bundle activation.
3. **W3-B2 kill-switch:** if voter/statistics/restriction semantics can become fail-open for unauthorized,
   cross-chat, or forwarded contexts, reject bundle activation.
4. **W3-B3 kill-switch:** if missing poll state, missing file references, or malformed media payloads can
   reach send/upload/render as successful operations, reject bundle activation.
5. **W3-B4 kill-switch:** if layer/CLI/hygiene commits cannot be isolated from behavior changes in runtime
   poll semantics, reject direct import and keep deferred.

## 6. Required Test Families Before Any Wave 3 Code Intake

For every activated Wave 3 sub-bundle branch, tests must be authored first and must fail before code edits.

1. Contract tests for touched seams and schema boundaries.
2. Adversarial tests for malformed IDs, stale state, replayed flows, and cross-context misuse.
3. Integration tests across manager boundaries (`MessageContent`, `PollManager`, `MessagesManager`,
   `FileManager`, `FileReferenceManager`, request/update plumbing as applicable).
4. Light fuzz tests for parser/normalization boundaries and policy gates.
5. Stress tests for sustained poll/media operations and fail-closed invariants.

Per-bundle emphasis:

1. W3-B1: contract + adversarial + persistence round-trip first.
2. W3-B2: adversarial policy matrix + integration update-order tests first.
3. W3-B3: adversarial malformed media/file-reference + light fuzz + stress first.
4. W3-B4: regression/compatibility tests proving no unintended runtime semantic drift.

## 7. Decision Policy for Import vs Adapt vs Reject

1. Direct cherry-pick is forbidden for all W3 bundles by default.
2. Commits in the known high-blast media expansion cluster are pre-classified as reject-for-direct-import
   and require adaptation-only handling if ever activated: `b2fd2e468`, `8ff835b82`, `410f57214`,
   `bf4bfcf8e`, `d59ef0167`, `65b967524`, `8104480e0`, `50d50976f`.
3. W3-B0 baseline commits remain frozen and must not be reworked unless required to restore fail-closed
   behavior after a proven regression.
4. Any commit that cannot be mapped cleanly to one active sub-bundle is blocked and stays deferred.

## 8. Activation and Exit Criteria

Any new Wave 3 execution branch may be opened only when all conditions below are met:

1. Product objective for Wave 3 is explicitly approved.
2. A Wave 3 preflight annex is published with the exact active sub-bundle subset and row references.
3. Contract snapshot and risk register are approved for the active sub-bundle.
4. RED-phase test suite exists and demonstrates intended failures before implementation.
5. All kill-switch criteria remain untriggered throughout adaptation and validation.

Until then, all deferred Wave 3 intake outside the already merged W3-B0 baseline remains blocked,
and no additional Wave 3 branch may be opened beyond the repository-resident bounded seams noted
above.
