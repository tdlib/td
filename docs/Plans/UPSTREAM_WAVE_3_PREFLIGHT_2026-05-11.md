<!--
SPDX-FileCopyrightText: Copyright 2026 telemt community
SPDX-License-Identifier: MIT
telemt: https://github.com/telemt
telemt: https://t.me/telemtrs
-->

# Upstream Wave 3 Preflight Annex (W3-B2)

Date: 2026-05-11
Scope: Wave 3 sub-bundle W3-B2 only (poll policy, voters, statistics, unread state)
Backlog anchor: `original..upstream/master`
Canonical manifest: `docs/Plans/UPSTREAM_BACKPORT_MANIFEST_2026-05-08.md`
Canonical charter: `docs/Plans/UPSTREAM_WAVE_3_CHARTER_2026-05-11.md`
Canonical gating plan: `docs/Plans/UPSTREAM_BACKPORT_GATING_PLAN_2026-05-08.md`
Status: Historical preflight archive; the repository audit recorded in
`docs/Plans/UPSTREAM_BACKPORT_GATING_PLAN_2026-05-08.md` Sections `0.1`, `0.2`, and `0.3.3.a`
closes the wave-level W3 backlog while this annex remains the frozen W3-B2 scope/risk/RED record
Historical note: The activation rules below are preserved as archival planning criteria, not live blockers.

## 1. Objective

This annex originally activated planning for one bounded Wave 3 slice: W3-B2 policy semantics for poll voters,
poll statistics, restriction reasons, and unread poll-vote state.

This annex no longer serves as a live intake gate by itself. It preserves scope,
contracts, risks, and RED expectations that were frozen for any future W3-B2 branch, while recording the bounded
repository-resident seams/tests already present in the tree.

## 2. Scope Freeze

### 2.1 In Scope (exact W3-B2 rows/commits)

1. 42:`084707e99` - Support member-only polls
2. 44:`bb6574d9f` - Support limiting poll voters to specific countries
3. 45:`7d56f9c58` - Add `Poll.can_get_vote_statistics`
4. 46:`bcd2c683c` - Move vote-statistics capability to `messageProperties`
5. 47:`f654c5c81` - Add `td_api::getPollVoteStatistics`
6. 48:`1eaf2481e` - Add `message.contains_unread_poll_votes`
7. 49:`d6ef00fa9` - Add `updateMessageContainsUnreadPollVotes`
8. 51:`04498cfbb` - Ignore unread_poll_votes while votes are being read
9. 52:`1f68a4a84` - Clear unread poll-vote counters before updates
10. 65:`271c71136` - Recent-voter processing clarifications
11. 66:`978979edb` - Improve `PollManager::can_get_poll_voters`
12. 67:`ca82791de` - Immediate result visibility for unallowed countries
13. 68:`b00c67763` - Add `poll.vote_restriction_reason`
14. 69:`0b9e9829b` - Add more vote-restriction reasons
15. 153:`b5c87eb91` - Add option `poll_country_count_max`
16. 155:`e7cbde50c` - Restrict voter-limiting to channel chats
17. 158:`d51464eb2` - Improve membership-required restriction reason
18. 159:`c6411b9c9` - Add `pollVoteRestrictionReasonOther`
19. 160:`dc470c164` - Remove quick-reply restriction reason
20. 162:`1574780ca` - Improve `poll.can_get_voters`
21. 186:`02473d316` - Improve membership-required restriction reason handling
22. 187:`aaea672ae` - Allow result visibility in membership-limited polls

### 2.2 Explicitly Out of Scope

1. W3-B1 poll core routing and local option-state refactors.
2. W3-B3 poll media schema/upload/file-reference/render expansion.
3. W3-B4 layer/cli/hygiene spillover commits.
4. Any non-W3 lanes (W1/W2/W2B/W4/W5/W6/W7/W8).

Freeze rule: no commit outside the 22 rows above may enter a W3-B2 execution branch unless this annex
is revised and re-approved.

## 3. Mission-Fit and Evidence Basis

W3-B2 is selected as the first candidate because it is capability-sensitive but bounded enough to
evaluate deny-by-default policy semantics without opening poll-media upload/file-reference risk.

Evidence basis in the local tree:

1. Existing W3 defensive tests already cover adjacent poll seams and provide baseline safety rails:
   `forwarded_poll_statistics_*`, `poll_voter_visibility_*`, `poll_state_runtime_*`.
2. The charter split isolates policy/voter/statistics/unread semantics from media transport surfaces,
   which reduces blast radius for first capability-wave preflight.
3. Current gating policy already marks direct cherry-pick as forbidden for Wave 3; adaptation-only
   review remains mandatory.
4. As of 2026-05-12, the repository already contains bounded W3-B2 policy seams and focused test
   files for forwarded statistics, voter visibility, unread poll votes, and restriction-reason
   mapping; this annex remains the approval and scope-control record for any further intake.

## 4. Contract Snapshot (W3-B2)

### 4.1 Contract W3B2-C-001: Poll Voter-Visibility Policy

Boundary: poll voter/result visibility decisions in `PollManager` and policy exposure in
`MessageContent`/message properties.

- Inputs: poll metadata, dialog/chat type, member/country eligibility constraints, caller context.
- Outputs: deterministic allow/deny signals for voters and results (`can_get_voters` and equivalents).
- Side effects: user-visible API fields and message properties.
- Preconditions: policy fields may be partially present or stale in persisted/runtime state.
- Postconditions: any ambiguous or conflicting eligibility state must fail closed.

### 4.2 Contract W3B2-C-002: Poll Statistics Capability and Request Path

Boundary: `Poll.can_get_vote_statistics` + `messageProperties` exposure + `td_api::getPollVoteStatistics`.

- Inputs: poll ID, dialog context, permission/capability flags, forwarded-message context.
- Outputs: statistics retrieval is allowed only when capability is explicitly true.
- Side effects: request routing and externally visible error semantics.
- Preconditions: forwarded or re-shared contexts may obscure original poll ownership.
- Postconditions: no statistics call may succeed through inferred or default-allow capability.

### 4.3 Contract W3B2-C-003: Unread Poll-Vote State and Update Emission

Boundary: message unread poll-vote flags, update generation, and pre-update counter clearing.

- Inputs: read/vote transitions, message lifecycle events, topic/dialog update ordering.
- Outputs: consistent `contains_unread_poll_votes` state and aligned updates.
- Side effects: counter mutations and update fan-out.
- Preconditions: out-of-order update timing and concurrent read/vote transitions are possible.
- Postconditions: stale unread counters cannot leak after an acknowledged read path.

### 4.4 Contract W3B2-C-004: Restriction Reason Taxonomy and Mapping

Boundary: restriction reason enum/object mapping from internal policy state to td_api objects.

- Inputs: membership/country/policy constraints from poll and chat context.
- Outputs: stable reason object selection without deprecated/removed reasons.
- Side effects: API compatibility and client policy interpretation.
- Preconditions: legacy reason values may still appear in persisted state.
- Postconditions: removed reason classes are never emitted; unknown values fail closed to safe default.

## 5. Dependent Audit (must remain valid)

1. `td/telegram/PollManager.cpp` policy predicates and reason mapping.
2. `td/telegram/MessageContent.cpp` policy exposure in message properties and object conversion.
3. `td/telegram/MessagesManager.cpp` unread poll-vote state transitions and update ordering.
4. `td/telegram/Requests.cpp`/`Requests.h` request entry for poll statistics.
5. `td/generate/scheme/td_api.tl` fields/functions tied to W3-B2 only.

Noticed but not touching in this annex:

1. Poll media schema and upload semantics (W3-B3).
2. Option/state storage refactors (W3-B1).

## 6. Risk Register

| Risk ID | Contract | Category | Attack/Failure Scenario | Impact | RED Test IDs |
|---|---|---|---|---|---|
| W3B2-R-001 | C-001 | Access-control semantics | Unauthorized context obtains voter/result visibility via permissive fallback | Policy bypass / info leak | W3B2-RED-001, W3B2-RED-002 |
| W3B2-R-002 | C-001 | Input normalization | Mixed membership/country constraints produce contradictory allow state | Fail-open visibility | W3B2-RED-003 |
| W3B2-R-003 | C-002 | Request authorization | `getPollVoteStatistics` succeeds without explicit capability | Data exposure | W3B2-RED-004, W3B2-RED-005 |
| W3B2-R-004 | C-002 | Forwarded-context policy | Forwarded/reposted poll path bypasses capability gate | Cross-context policy bypass | W3B2-RED-006 |
| W3B2-R-005 | C-003 | Update-order correctness | Unread poll-vote update arrives after counter clear with stale state | State drift / UI inconsistency | W3B2-RED-007, W3B2-RED-008 |
| W3B2-R-006 | C-003 | Replay/dup handling | Duplicate read/vote events re-open unread state | Incorrect unread state | W3B2-RED-009 |
| W3B2-R-007 | C-004 | Enum mapping safety | Removed restriction reason still emitted | Client contract break | W3B2-RED-010 |
| W3B2-R-008 | C-004 | Legacy/persisted compatibility | Unknown legacy reason value maps to permissive state | Policy ambiguity | W3B2-RED-011 |

Any HIGH/CRITICAL risk above requires at least one failing RED test before any adaptation code is accepted.

## 7. RED Test Plan and IDs

The IDs below are mandatory and must be used in commit/PR notes and decision logs.

| RED Test ID | Planned File | Scenario (must fail first) | Expected RED Signal |
|---|---|---|---|
| W3B2-RED-001 | `test/poll_voter_visibility_contract.cpp` | Non-eligible viewer cannot obtain voters/results in member-only polls | Assertion mismatch: visibility incorrectly allowed |
| W3B2-RED-002 | `test/poll_voter_visibility_adversarial.cpp` | Cross-chat/cross-context caller attempts voter visibility retrieval | Assertion mismatch: fail-open branch reached |
| W3B2-RED-003 | `test/poll_voter_visibility_integration.cpp` | Combined membership+country restrictions produce contradictory policy state | Integration assertion fails on deterministic deny |
| W3B2-RED-004 | `test/forwarded_poll_statistics_contract.cpp` | Statistics capability absent but request path still returns success | Assertion mismatch or unexpected success object |
| W3B2-RED-005 | `test/forwarded_poll_statistics_adversarial.cpp` | Stale/unknown poll context attempts statistics retrieval | Assertion mismatch: expected fail-closed error/null |
| W3B2-RED-006 | `test/forwarded_poll_statistics_integration.cpp` | Forwarded poll flow bypasses `can_get_vote_statistics` gating | Integration assertion fails on unauthorized access |
| W3B2-RED-007 | `test/poll_unread_votes_contract.cpp` (new) | Counter-clear invariant before update emission is violated | Contract assertion on update state ordering fails |
| W3B2-RED-008 | `test/poll_unread_votes_integration.cpp` (new) | Out-of-order read/vote/update sequence leaves stale unread flag | Integration assertion fails on final unread state |
| W3B2-RED-009 | `test/poll_unread_votes_adversarial.cpp` (new) | Replayed duplicate transitions re-open unread markers | Adversarial assertion fails on idempotence |
| W3B2-RED-010 | `test/poll_restriction_reason_contract.cpp` (new) | Removed quick-reply restriction reason still emitted | Contract assertion fails on reason type |
| W3B2-RED-011 | `test/poll_restriction_reason_adversarial.cpp` (new) | Unknown legacy reason maps to permissive output | Assertion mismatch: expected safe fallback reason |
| W3B2-RED-012 | `test/poll_restriction_reason_integration.cpp` (new) | Mixed policy states emit inconsistent reason taxonomy across objects | Integration assertion fails on mapping consistency |

## 8. Planned Test Families by Phase

1. Contract: `poll_voter_visibility_contract`, `forwarded_poll_statistics_contract`,
   `poll_unread_votes_contract`, `poll_restriction_reason_contract`.
2. Adversarial: `poll_voter_visibility_adversarial`, `forwarded_poll_statistics_adversarial`,
   `poll_unread_votes_adversarial`, `poll_restriction_reason_adversarial`.
3. Integration: existing `poll_voter_visibility_integration`, `forwarded_poll_statistics_integration`,
   plus new unread/reason integration suites.
4. Light fuzz: extend `poll_state_runtime_adversarial`/new focused fuzz harness for policy and reason mapping.
5. Stress: add high-iteration visibility/statistics/unread transition stability tests.

## 9. C++23 and Security Adaptation Rules (W3-B2)

1. Direct cherry-pick remains forbidden; only adaptation with bounded seam changes is allowed.
2. Deny-by-default must hold for all voter/statistics/restriction decisions.
3. Unknown/stale poll state must fail closed and never trigger crash-oriented control flow.
4. Schema and runtime changes must remain bounded to W3-B2; any accidental W3-B3 coupling is a stop condition.

## 10. Historical Validation Matrix for Execution Readiness

At publication time, any additional W3-B2 execution or rework branch could start only when:

1. Product objective for W3-B2 is explicitly approved.
2. All RED IDs in Section 7 are present in test code, and any new W3-B2 branch reproduces the
   intended RED failures for the exact change surface before branch-specific code edits.
3. Contract and risk IDs are referenced by test names/comments and decision logs.
4. Kill-switch W3-B2 from the charter remains untriggered during adaptation.
5. Focused and wider validation targets are defined (`run_all_tests --filter` subsets and `ctest -j 14`).

## 11. Working Documents Frozen With This Annex

1. `docs/Plans/UPSTREAM_WAVE_3_CHARTER_2026-05-11.md`
2. `docs/Plans/UPSTREAM_BACKPORT_GATING_PLAN_2026-05-08.md`
3. `docs/Plans/UPSTREAM_BACKPORT_MANIFEST_2026-05-08.md`
