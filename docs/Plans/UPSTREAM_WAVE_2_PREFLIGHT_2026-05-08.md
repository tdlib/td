# Upstream Wave 2 Preflight Annex

Date: 2026-05-08
Scope: Non-stealth correctness shortlist only
Backlog anchor: `original..upstream/master`
Planning basis: full backlog reviewed for mission fit; Wave 2 activates only the non-stealth
hardening shortlist frozen in the main gating plan.

## 1. Objective

This annex defines the first non-stealth hardening wave after the Wave 1 transport/parser work.
It does not authorize bulk feature ingestion from upstream. The only purpose of Wave 2 is to lift
small, correctness-oriented fixes that improve state integrity, reply-thread semantics, and
move/lifetime hygiene without importing the large `td/telegram` feature surface.

Wave 2 exists because the upstream backlog is highly concentrated in `td/telegram/`, but that file
count mostly represents product/API expansion rather than reliability work. Changed-file count is
not a sufficient backport signal. The active shortlist below was selected because each item appears
to improve correctness or hardening directly.

## 2. Scope Freeze

### 2.1 In Scope

1. `a09adfc63` (`Fix reply_to_top_id.`)
2. `386eca6fe` (`Fix DialogParticipantStatus::GroupAdministrator.`)
3. `1a9ef3d68` (`Repair group administrator rights on load.`)
4. `5340472b0` (`Fix possible use after move.`) as a hunk-by-hunk audit input only

### 2.2 Out of Scope for This Annex

1. Wave 1 transport, proxy-link, or TLS profile work
2. Large `td/telegram` feature bundles such as poll media, AI compose/text composition styles,
   managed-bot access, and similar API-expansion lanes
3. Config obfuscation churn such as `6b82cc832`
4. Proxy metadata/API expansion such as `691cb6a77` unless reopened under a separate API-boundary task
5. Bulk cherry-picking based only on upstream proximity or shared touched files

## 3. Evidence Basis

The shortlist was frozen from the reviewed upstream range using the following local evidence:

1. The local fork still lacks the upstream `reply_to_top_id_` repair path in
   `td/telegram/MessageReplyHeader.cpp`.
2. The local fork still uses the older `GroupAdministrator(bool is_creator, ...)` semantics in
   `td/telegram/DialogParticipant.cpp` and does not perform the corresponding load-time repair in
   `td/telegram/ChatManager.cpp`.
3. The local tree still contains at least one call site upstream adjusted in the use-after-move
   cleanup, such as evaluating `dialogs->dialogs_.size()` at the `on_get_dialogs(...)` call site in
   `td/telegram/MessagesManager.cpp`.
4. Broad null-content hardening from `13003156a` is already partially absorbed locally via existing
   `CHECK(content != nullptr)` guards, so it is not a Wave 2 priority candidate.

## 4. Candidate Table

| Wave 2 ID | Upstream Commit(s) | Local Owning Area | Planning Class | Why It Is In Scope |
|---|---|---|---|---|
| W2-001 | `a09adfc63` | `td/telegram/MessageReplyHeader.cpp` | Direct-backport candidate, pending contract/tests | Narrow semantic repair for malformed topic replies and missing `reply_to_top_id_` recovery |
| W2-002 | `386eca6fe` + `1a9ef3d68` | `td/telegram/DialogParticipant.cpp`, `td/telegram/ChatManager.cpp` | Direct-backport candidate bundle, pending adaptation | Fixes administrator-right interpretation and persisted-load repair |
| W2-003 | `5340472b0` | `td/telegram/*` multi-file lifetime sites | Research / audit input only | Correct change class, but local divergence makes blind cherry-pick unsafe |

## 5. Contract Snapshot

### 5.1 W2-001 — Reply Thread Normalization

Contract anchor: `MessageReplyHeader::MessageReplyHeader(...)`

- Inputs: raw `telegram_api::MessageReplyHeader`, target `dialog_id`, current `message_id`, message `date`
- Outputs: normalized `top_thread_message_id_`, `is_topic_message_`, and `replied_message_info_`
- Side effects: logs malformed headers; influences downstream thread classification
- Preconditions: reply header may be partially populated or malformed by the server
- Postconditions: if a topic reply can be normalized to a valid top-thread mapping, the local state
  must preserve that mapping instead of silently dropping thread affinity

Dependent assumptions to preserve:

1. Channel-topic replies must not lose top-thread linkage when the server omits `reply_to_top_id_`
   but still provides recoverable same-chat reply metadata.
2. User dialogs must not incorrectly gain topic semantics unless the normalized header is valid.

### 5.2 W2-002 — Group Administrator Rights Semantics

Contract anchors:

1. `DialogParticipantStatus::GroupAdministrator(...)`
2. `ChatManager::Chat::parse(...)` for persisted chat status load

- Inputs: chat administrator rights, creator/editability flags, persisted chat state
- Outputs: normalized `DialogParticipantStatus` with correct admin flags and editability semantics
- Side effects: affects API-visible member status objects and persisted-load recovery
- Preconditions: older persisted data may encode group-administrator state using legacy semantics
- Postconditions: group administrators must not inherit creator-only rights or editability semantics
  merely because the current user is a creator; persisted loads must normalize legacy administrator states

Dependent assumptions to preserve:

1. Group-administrator status returned through API objects must remain consistent with stored rights.
2. Loading older chat state must repair legacy admin semantics deterministically.

### 5.3 W2-003 — Use-After-Move / Lifetime Audit

Contract anchor: per-hunk review of moved containers, moved messages, and replayed dialog vectors.

- Inputs: moved vectors/objects in dialog/message replay and management code
- Outputs: no reads from moved-from containers or stale semantic labels after mutation
- Side effects: correctness of counts, logging labels, and resend/replay paths
- Preconditions: local call sites may already have diverged from upstream due to unrelated edits
- Postconditions: any accepted hunk must remove a real lifetime hazard without widening scope into
  unrelated feature churn

## 6. Risk Register

| Risk ID | Candidate | Category | Attack / Failure Scenario | Impact | Planned Test Surface |
|---|---|---|---|---|---|
| W2-R-001 | W2-001 | Protocol/state semantics | Server sends topic reply metadata without `reply_to_top_id_`; local code drops thread linkage | Replies attach to wrong thread or lose thread context | Topic-reply contract tests plus malformed-header adversarial cases |
| W2-R-002 | W2-001 | Input normalization | Header fields conflict (`reply_to_top_id_`, `reply_to_msg_id_`, `reply_from_`) | Incorrect reconstruction of thread affinity | Red tests for conflicting combinations and fail-closed rejection |
| W2-R-003 | W2-002 | Persistence/load correctness | Legacy persisted chat status loads as creator-like admin or wrong editability flags | Incorrect permissions, API-visible rights drift | Persisted-load contract tests and API object round-trip tests |
| W2-R-004 | W2-002 | Access-control semantics | Group admin inherits creator-only flags after normalization | Broken least-privilege behavior | Rights-matrix tests for creator vs admin separation |
| W2-R-005 | W2-003 | Move/lifetime safety | Code reads from moved-from container or object for counts/labels/state | Undefined behavior, wrong counts, inconsistent replay behavior | Narrow regression tests per accepted hunk; compile + sanitizer-friendly review |

Every HIGH or correctness-critical risk above must have a dedicated failing test before any code edit
is accepted into a Wave 2 branch.

## 7. Planned Test Surface

### 7.1 W2-001 — Reply Thread Normalization

New test family requirements:

1. Contract tests for valid topic replies with explicit `reply_to_top_id_`
2. Red tests for missing `reply_to_top_id_` plus recoverable `reply_to_msg_id_`
3. Adversarial tests for conflicting or malformed combinations of topic-reply fields
4. Negative tests ensuring user dialogs and non-thread-capable dialogs do not gain topic semantics

### 7.2 W2-002 — Administrator Rights Repair

New test family requirements:

1. Contract tests for `GroupAdministrator` rights shape versus creator rights shape
2. Persisted-load tests for `ChatManager::Chat::parse(...)` with legacy admin state
3. API object tests ensuring repaired state exports correct `chatMemberStatusAdministrator` semantics
4. Adversarial rights-matrix tests verifying creator-only flags are never leaked into group-admin state

### 7.3 W2-003 — Lifetime Audit

New test family requirements:

1. Narrow regression tests only for accepted hunks
2. No bundle-wide test rewrite until a concrete local defect is confirmed
3. Prefer compile/runtime checks and targeted replay-path tests over speculative broad edits

## 8. C++23 Adaptation Notes

1. Favor local repairs that preserve current interfaces unless a semantic contract change is required.
2. Avoid importing upstream patterns that depend on surrounding feature churn in `td/telegram`.
3. Treat `5340472b0` as source material for local C++23-safe lifetime cleanup, not as a direct patch set.
4. Keep each accepted edit minimal and isolated enough that focused validation can falsify it quickly.

## 9. Wave 2 Execution Order

1. Freeze the shortlist above and keep all non-shortlist `td/telegram` feature work deferred.
2. Start with W2-001 (`a09adfc63`) because it is the smallest behavioral repair with the clearest local gap.
3. Continue with W2-002 as one coupled bundle; do not split `386eca6fe` from `1a9ef3d68`.
4. Review W2-003 only after W2-001 and W2-002 are either accepted or rejected, and only hunk by hunk.
5. For every candidate: contract snapshot -> dependent audit -> red tests -> minimal repair -> focused validation -> wider validation.

## 10. Validation Matrix

Wave 2 acceptance requires, for the touched slice:

1. Focused unit/contract/adversarial tests for the changed behavior
2. No new file-level errors in touched C++ files
3. Successful targeted build/test validation using the repository's 14-core baseline where appropriate
4. No scope creep into unrelated feature bundles
5. Clear final class per candidate: Accept, Accept with Repair, Reject, or Defer

## 11. Manifest Freeze Appendix

This annex activates only the non-stealth correctness shortlist above. For the current planning date,
the remaining upstream backlog is frozen as follows:

1. Wave 1 research input only: `28e0d0dbe`, `00eedc5f9`, `a82128ab8`, `bfab03f7a`
2. Already incorporated or superseded: `3d38fb7aa`, `bc79a6d2d`
3. Deferred targeted API/config lanes: `691cb6a77`, `6b82cc832`, `8921c22f0`, `dd78f94a8`, `990b821c8`, `49b3bcbb6`, `e86cd4496`
4. Deferred feature expansion: all remaining non-shortlist `td/telegram` commits in `original..upstream/master`, including poll-media, AI compose/text-composition style, managed-bot access, premium purchase, guest-bot, and similar product-surface work

Freeze rule: no commit outside W2-001 through W2-003 may enter a Wave 2 branch unless this annex is
revised first.

## 12. Exit Criteria for Preflight

Wave 2 preflight is complete only when:

1. The shortlist remains frozen to W2-001 through W2-003.
2. Each candidate has contract tests and adversarial/failure-path tests defined before editing code.
3. No large feature lane is bundled into the same branch as a correctness repair.
4. A per-candidate decision report is produced after execution.

## 13. Working Documents Frozen With This Annex

1. `UPSTREAM_W2_001_REPLY_THREAD_TASK_2026-05-08.md` — concrete contract-and-red-test task for W2-001
2. `UPSTREAM_WAVE_2_DECISION_2026-05-08.md` — Wave 2 execution template and decision log
