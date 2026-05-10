# Upstream Wave 2 Decision Report

Date: 2026-05-08
Last updated: 2026-05-09
Document role: execution template and decision log for the Wave 2 shortlist
Preflight reference: `UPSTREAM_WAVE_2_PREFLIGHT_2026-05-08.md`
Scope freeze: W2-001 through W2-003 only

Historical scope note: this report records the 2026-05-08 Wave 2 decision cycle only. Any later
branch-local W2-003 or W3-P work must be tracked and merged as separate lanes and must not be
interpreted as part of this completed Wave 2 decision.

Use this document during execution. No candidate may be re-bundled, widened, or replaced here; if
scope changes, the preflight annex must be revised first.

## 1. Summary Table

| Candidate | Upstream Commit(s) | Planned Class | Final Class | Status | Notes |
|---|---|---|---|---|---|
| W2-001 | `a09adfc63` | Direct-backport candidate, pending contract/tests | `Accept with Repair` | Completed | Reply-thread normalization plus fail-closed channel topic/thread finalization seam validated; additional hardening pass (2026-05-09) added fallback-repair for stale/future explicit thread anchors and cleared 16/16 tests |
| W2-002 | `386eca6fe` + `1a9ef3d68` | Direct-backport candidate bundle, pending adaptation | `Accept with Repair` | Completed | Group-admin rights/load-repair semantics validated with added Chat::parse source-contract coverage |
| W2-003 | `5340472b0` | Research / audit input only | `Defer` | Completed | Lifetime audit remains intentionally out of this wave |

Allowed final classes:

1. `Accept`
2. `Accept with Repair`
3. `Reject`
4. `Defer`

## 2. Global Scope Check

Complete before finalizing any candidate outcome:

1. No extra upstream commits were added beyond W2-001 through W2-003.
2. No large `td/telegram` feature bundle was pulled into the same branch.
3. Every accepted or repaired candidate has contract tests and adversarial tests mapped.
4. Every rejected or deferred candidate has an explicit rationale tied to local evidence.

## 3. Candidate Report Template

Repeat the section below for each candidate while keeping the candidate ID and upstream commit set
fixed.

### 3.X Candidate `[W2-00X]`

- Upstream commit(s): `[hashes]`
- Planned class from preflight: `[planned class]`
- Final class: `[Accept | Accept with Repair | Reject | Defer]`
- Execution status: `[Not started | In progress | Completed]`

#### 3.X.1 Outcome Summary

`[2-5 sentence summary of what was found and what decision was reached]`

#### 3.X.2 Contracts Touched

1. `[contract 1]`
2. `[contract 2]`

#### 3.X.3 Risk IDs

1. `[risk id]`
2. `[risk id]`

#### 3.X.4 Local Files Touched

1. `[file path]`
2. `[file path]`

#### 3.X.5 Tests Added or Updated

1. `[test file and test names]`
2. `[test file and test names]`

#### 3.X.6 RED-Phase Evidence

1. `[which tests failed first]`
2. `[why the failure proved the bug or disproved the hypothesis]`

#### 3.X.7 Focused Validation

1. `[focused build/test command or task]`
2. `[result]`

#### 3.X.8 Wider Validation

1. `[broader build/test/lint/typecheck command or task]`
2. `[result]`

#### 3.X.9 C++23 / Security / Architecture Notes

1. `[C++23 adaptation note]`
2. `[security or least-privilege note]`
3. `[architecture/scope note]`

#### 3.X.10 Decision Rationale

`[why the final class is correct and why alternative classes were rejected]`

#### 3.X.11 Residual Risks and Follow-Up

1. `[residual risk, if any]`
2. `[separate follow-up task only if required]`

## 4. Candidate Slots

### 4.1 W2-001

- Upstream commit(s): `a09adfc63`
- Planned class from preflight: `Direct-backport candidate, pending contract/tests`
- Final class: `Accept with Repair`
- Execution status: `Completed`

#### 4.1.1 Outcome Summary

W2-001 is accepted with repair. The local seam now normalizes the malformed forum-topic header
shape before reply parsing, preserving top-thread affinity while preventing same-chat reply leakage
from the promoted field. The implementation follows upstream semantics but is adapted as a
dedicated helper with explicit fail-closed guards and expanded local test coverage.

#### 4.1.2 Contracts Touched

1. `MessageReplyHeader::MessageReplyHeader(...)`
2. `MessageReplyHeader::normalize_topic_reply_header(...)` and its interaction with `RepliedMessageInfo`
3. `MessageReplyHeader::finalize_channel_topic_thread_state(...)`

#### 4.1.3 Risk IDs

1. `W2-R-001`
2. `W2-R-002`

#### 4.1.4 Local Files Touched

1. `td/telegram/MessageReplyHeader.cpp`
2. `td/telegram/MessageReplyHeader.h`
3. `test/CMakeLists.txt`
4. `test/message_reply_header_contract.cpp`
5. `test/message_reply_header_adversarial.cpp`
6. `test/message_reply_header_integration.cpp`
7. `test/message_reply_header_light_fuzz.cpp`
8. `test/message_reply_header_stress.cpp`

#### 4.1.5 Tests Added or Updated

1. `test/message_reply_header_contract.cpp` (contract preservation and guard behavior)
2. `test/message_reply_header_adversarial.cpp` (cross-chat, missing-origin, conflicting-field rejection)
3. `test/message_reply_header_integration.cpp` (runtime integration with `RepliedMessageInfo`)
4. `test/message_reply_header_light_fuzz.cpp` (10k randomized shape checks)
5. `test/message_reply_header_stress.cpp` (250k deterministic promotion/rejection loop)
6. Added first-pass hardening checks:
   - `MessageReplyHeaderContract.PromotesEarlierSameChatReplyFallbackIntoThreadAnchor`
   - `MessageReplyHeaderAdversarial.ClearsTopicFlagWhenFallbackThreadTargetIsNotEarlierThanMessage`
7. Added second-pass hardening checks (2026-05-09 adversarial audit):
   - `MessageReplyHeaderContract.RepairsInvalidExplicitThreadAnchorUsingEarlierSameChatFallback`
   - `MessageReplyHeaderAdversarial.ClearsTopicFlagWhenExplicitAndFallbackThreadTargetsAreNotEarlierThanMessage`

#### 4.1.6 RED-Phase Evidence

1. Integration evidence originally relied on source-shape checks and was strengthened to runtime
   integration assertions over `normalize_topic_reply_header` plus `RepliedMessageInfo` parsing.
2. Runtime integration tests now verify that unnormalized headers preserve same-chat reply linkage,
   while normalized headers consume that linkage as expected.
3. Final hardening pass introduced RED compile failures because
   `MessageReplyHeader::finalize_channel_topic_thread_state(...)` did not exist yet, then turned
   green after the seam was implemented and constructor finalization was routed through it.
4. Second-pass adversarial audit (2026-05-09) found that `finalize_channel_topic_thread_state`
   cleared topic state immediately when an explicit anchor was present but chronologically invalid
   (not earlier than the message ID) without attempting the same-chat fallback path. Test
   `RepairsInvalidExplicitThreadAnchorUsingEarlierSameChatFallback` went RED (assertion abort at
   message_reply_header_contract.cpp:104). Implementation was updated to introduce an
   `is_usable_thread_anchor` lambda and try the fallback before fail-closing; constructor was
   updated to eagerly fetch the fallback when the explicit anchor is stale or future. Both new
   tests turned GREEN without relaxing any prior test.

#### 4.1.7 Focused Validation

1. `./build/test/run_all_tests --filter MessageReplyHeader`
2. Result after first-pass hardening: passed 14/14 selected tests.
3. Result after second-pass hardening (2026-05-09): passed 16/16 selected tests (2 additional tests added in this pass).

#### 4.1.8 Wider Validation

1. `ctest --test-dir build --output-on-failure -j 14`
2. W2-001 tests passed in broad lane; full-suite runs intermittently reported unrelated subprocess aborts
   (`sqlite_phase3_statement_blob_roundtrip_light_fuzz`, `TQueue_random`, and a stealth-params light-fuzz case),
   but targeted reruns in isolation passed clean.

#### 4.1.9 C++23 / Security / Architecture Notes

1. Adapted as a narrow helper seam to keep C++23 code clarity and minimize constructor complexity.
2. Guard set is fail-closed: promotion only under exact same-chat topic shape.
3. Scope remained bounded to W2-001 owning files and dedicated tests.

#### 4.1.10 Decision Rationale

`Accept with Repair` is correct because local implementation now matches upstream intended semantics
while preserving repository style and adding stronger local runtime coverage than direct cherry-pick
alone. `Reject` and `Defer` were not selected because behavior delta was real and now validated.

#### 4.1.11 Residual Risks and Follow-Up

1. Constructor-level full `Td` wiring remains heavy for unit isolation; current runtime integration
   validates seam interaction without full actor stack bootstrap.
2. Future hardening can add an end-to-end message-constructor harness if a lightweight `Td` test
   fixture is introduced.

### 4.2 W2-002

- Upstream commit(s): `386eca6fe`, `1a9ef3d68`
- Planned class from preflight: `Direct-backport candidate bundle, pending adaptation`
- Final class: `Accept with Repair`
- Execution status: `Completed`

#### 4.2.1 Outcome Summary

W2-002 is accepted with repair as one coupled bundle. Group-administrator semantics were corrected
to prevent creator-context promotion-right escalation, and persisted load repair now preserves rank
while normalizing to least-privilege administrator state. Local naming and test coverage were
expanded to make creator-context behavior explicit and auditable. Final assessment added explicit
source-contract coverage that pins the `ChatManager::Chat::parse(...)` repair branch textually.

#### 4.2.2 Contracts Touched

1. `DialogParticipantStatus::GroupAdministrator(...)`
2. `ChatManager::Chat::parse(...)`

#### 4.2.3 Risk IDs

1. `W2-R-003`
2. `W2-R-004`

#### 4.2.4 Local Files Touched

1. `td/telegram/DialogParticipant.cpp`
2. `td/telegram/DialogParticipant.h`
3. `td/telegram/ChatManager.cpp`
4. `test/CMakeLists.txt`
5. `test/dialog_participant_group_admin_contract.cpp`
6. `test/dialog_participant_group_admin_adversarial.cpp`
7. `test/dialog_participant_group_admin_integration.cpp`
8. `test/dialog_participant_group_admin_light_fuzz.cpp`
9. `test/dialog_participant_group_admin_source_contract.cpp`
10. `test/dialog_participant_group_admin_stress.cpp`

#### 4.2.5 Tests Added or Updated

1. `test/dialog_participant_group_admin_contract.cpp` (least-privilege and editability contracts)
2. `test/dialog_participant_group_admin_adversarial.cpp` (constructor-path escalation resistance)
3. `test/dialog_participant_group_admin_integration.cpp` (runtime constructor + load-repair semantics)
4. `test/dialog_participant_group_admin_light_fuzz.cpp` (10k rank/context permutations)
5. `test/dialog_participant_group_admin_source_contract.cpp` (pins `ChatManager::Chat::parse(...)` repair branch and non-creator scope)
6. `test/dialog_participant_group_admin_stress.cpp` (250k constructor stability loop)

#### 4.2.6 RED-Phase Evidence

1. Focused execution initially exposed a light-fuzz expectation mismatch on rank length handling,
   which was corrected to align test expectation with existing rank normalization contract.
2. No evidence supported reverting least-privilege semantics; tests were tightened, not relaxed.
3. Final assessment found that runtime tests did not explicitly pin the private parse-repair branch,
   so dedicated source-contract RED assertions were added and then validated green.

#### 4.2.7 Focused Validation

1. `./build/test/run_all_tests --filter DialogParticipantGroupAdmin`
2. Result: passed 12/12 selected tests (including 2 source-contract checks).

#### 4.2.8 Wider Validation

1. `ctest --test-dir build --output-on-failure -j 14`
2. W2-002 tests passed in broad lane; intermittent unrelated subprocess aborts from non-W2 tests were
   observed in full-suite runs but did not reproduce under targeted reruns.

#### 4.2.9 C++23 / Security / Architecture Notes

1. Parameter naming was updated to `is_current_user_creator` to clarify semantics in C++23 code.
2. Security posture remains least-privilege: no creator-right inheritance into promote-members.
3. Load-repair preserves administrator rank while normalizing rights.

#### 4.2.10 Decision Rationale

`Accept with Repair` is correct because both coupled upstream intents are implemented and validated
as one bounded local bundle. Splitting the pair would risk inconsistent persisted-load behavior,
while rejecting would retain known rights-semantics drift.

#### 4.2.11 Residual Risks and Follow-Up

1. Runtime coverage for `ChatManager::Chat::parse(...)` remains mostly seam-level; deeper serialized
   fixture replay can be added in a follow-up if parser-format regressions become a concern.
2. Keep W2-002 and any future admin-right changes bundled in review to avoid semantic skew.

### 4.3 W2-003

- Upstream commit(s): `5340472b0`
- Planned class from preflight: `Research / audit input only`
- Final class: `Defer`
- Execution status: `Completed`

#### 4.3.1 Outcome Summary

W2-003 remains deferred. The commit class is correct (lifetime/use-after-move hardening), but the
local fork has broad divergence across the touched surfaces and this wave is intentionally scoped to
the bounded W2-001/W2-002 semantics bundle. No W2-003 code was merged in this decision cycle.

Post-decision branch-state note (2026-05-09): the current local branch now contains a separate
W2-003 source-and-test lane prepared for independent review. Those changes are outside this decision
record and must not be merged or squashed into the completed W2-001/W2-002 lane.

#### 4.3.2 Contracts Touched

1. No production contracts were modified in this wave for W2-003.

#### 4.3.3 Risk IDs

1. `W2-R-005`

#### 4.3.4 Local Files Touched

1. None.

#### 4.3.5 Tests Added or Updated

1. None.

#### 4.3.6 RED-Phase Evidence

1. Not applicable for deferred candidate.

#### 4.3.7 Focused Validation

1. No focused execution was run for W2-003 in this wave.

#### 4.3.8 Wider Validation

1. Not applicable for deferred candidate.

#### 4.3.9 C++23 / Security / Architecture Notes

1. Deferred due divergence and wave-scope discipline; do not apply as blind cherry-pick.

#### 4.3.10 Decision Rationale

`Defer` is correct because accepting W2-003 inside the current wave would violate bounded scope and
risk coupling unrelated lifetime edits across multiple heavily diverged files.

#### 4.3.11 Residual Risks and Follow-Up

1. Re-open W2-003 only as a separate hunk-by-hunk lifetime hardening task with dedicated adversarial
   tests per touched manager.

## 5. Final Wave 2 Closeout Checklist

1. Every candidate has a non-`PENDING` final class.
2. Every accepted or repaired candidate has test evidence recorded.
3. Every rejected or deferred candidate has a local-evidence rationale recorded.
4. No decision widened scope beyond the preflight annex.
5. Broad-lane reliability triage policy has been applied to any unrelated subprocess aborts.

## 6. Broad-Lane Reliability Triage Policy

This policy is mandatory for Wave 2 closeout runs when `ctest --test-dir build --output-on-failure -j 14`
reports intermittent subprocess aborts outside W2-owned tests.

### 6.1 Trigger Condition

Apply this policy only when all of the following are true:

1. Focused W2 slices are green (`MessageReplyHeader`, `DialogParticipantGroupAdmin`).
2. Broad-lane failures are outside W2-owned test IDs.
3. Failure mode is intermittent subprocess abort or timeout without deterministic W2 reproduction.

### 6.2 Mandatory Triage Steps

1. Re-run the exact failing tests in isolation with `-j 1`:
   `ctest --test-dir build --output-on-failure -j 1 -R "<exact failed test regex>"`
2. Re-run the same tests once more under parallel load if needed (`-j 14`) to probe stability.
3. Classify outcome:
   - `Infra/flake`: isolated reruns pass and no W2 test regresses.
   - `Code regression`: any isolated rerun still fails deterministically.

### 6.3 Gate Decision Rule

1. If classified as `Infra/flake`, Wave 2 candidate decisions may remain `Completed`, but the report
   must record exact failed test names and rerun outcomes.
2. If classified as `Code regression`, Wave 2 closeout is blocked; affected candidate status must
   return to `In progress` until deterministic green is restored.

### 6.4 Minimum Evidence To Record

1. Original broad-lane command and timestamp.
2. Exact failed test names.
3. Isolation rerun command(s) and results.
4. Final classification (`Infra/flake` or `Code regression`) with rationale.
