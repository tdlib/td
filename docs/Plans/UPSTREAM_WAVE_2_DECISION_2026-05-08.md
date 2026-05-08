# Upstream Wave 2 Decision Report

Date: 2026-05-08
Document role: execution template and decision log for the Wave 2 shortlist
Preflight reference: `UPSTREAM_WAVE_2_PREFLIGHT_2026-05-08.md`
Scope freeze: W2-001 through W2-003 only

Use this document during execution. No candidate may be re-bundled, widened, or replaced here; if
scope changes, the preflight annex must be revised first.

## 1. Summary Table

| Candidate | Upstream Commit(s) | Planned Class | Final Class | Status | Notes |
|---|---|---|---|---|---|
| W2-001 | `a09adfc63` | Direct-backport candidate, pending contract/tests | `PENDING` | Not started | Reply-thread normalization |
| W2-002 | `386eca6fe` + `1a9ef3d68` | Direct-backport candidate bundle, pending adaptation | `PENDING` | Not started | Group-administrator rights correctness |
| W2-003 | `5340472b0` | Research / audit input only | `PENDING` | Not started | Lifetime audit only |

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
- Final class: `PENDING`
- Execution status: `Not started`

#### 4.1.1 Outcome Summary

`[fill during execution]`

#### 4.1.2 Contracts Touched

1. `MessageReplyHeader::MessageReplyHeader(...)`
2. `[fill if consumer contract becomes part of the owning seam]`

#### 4.1.3 Risk IDs

1. `W2-R-001`
2. `W2-R-002`

#### 4.1.4 Local Files Touched

1. `[fill during execution]`

#### 4.1.5 Tests Added or Updated

1. `test/message_reply_header_contract.cpp`
2. `test/message_reply_header_adversarial.cpp`
3. `test/message_reply_header_integration.cpp`

#### 4.1.6 RED-Phase Evidence

1. `[fill during execution]`

#### 4.1.7 Focused Validation

1. `[fill during execution]`

#### 4.1.8 Wider Validation

1. `[fill during execution]`

#### 4.1.9 C++23 / Security / Architecture Notes

1. `[fill during execution]`

#### 4.1.10 Decision Rationale

`[fill during execution]`

#### 4.1.11 Residual Risks and Follow-Up

1. `[fill during execution]`

### 4.2 W2-002

- Upstream commit(s): `386eca6fe`, `1a9ef3d68`
- Planned class from preflight: `Direct-backport candidate bundle, pending adaptation`
- Final class: `PENDING`
- Execution status: `Not started`

#### 4.2.1 Outcome Summary

`[fill during execution]`

#### 4.2.2 Contracts Touched

1. `DialogParticipantStatus::GroupAdministrator(...)`
2. `ChatManager::Chat::parse(...)`

#### 4.2.3 Risk IDs

1. `W2-R-003`
2. `W2-R-004`

#### 4.2.4 Local Files Touched

1. `[fill during execution]`

#### 4.2.5 Tests Added or Updated

1. `[fill during execution]`

#### 4.2.6 RED-Phase Evidence

1. `[fill during execution]`

#### 4.2.7 Focused Validation

1. `[fill during execution]`

#### 4.2.8 Wider Validation

1. `[fill during execution]`

#### 4.2.9 C++23 / Security / Architecture Notes

1. `[fill during execution]`

#### 4.2.10 Decision Rationale

`[fill during execution]`

#### 4.2.11 Residual Risks and Follow-Up

1. `[fill during execution]`

### 4.3 W2-003

- Upstream commit(s): `5340472b0`
- Planned class from preflight: `Research / audit input only`
- Final class: `PENDING`
- Execution status: `Not started`

#### 4.3.1 Outcome Summary

`[fill during execution]`

#### 4.3.2 Contracts Touched

1. `[fill during execution if any hunk is accepted]`

#### 4.3.3 Risk IDs

1. `W2-R-005`

#### 4.3.4 Local Files Touched

1. `[fill during execution]`

#### 4.3.5 Tests Added or Updated

1. `[fill during execution]`

#### 4.3.6 RED-Phase Evidence

1. `[fill during execution]`

#### 4.3.7 Focused Validation

1. `[fill during execution]`

#### 4.3.8 Wider Validation

1. `[fill during execution]`

#### 4.3.9 C++23 / Security / Architecture Notes

1. `[fill during execution]`

#### 4.3.10 Decision Rationale

`[fill during execution]`

#### 4.3.11 Residual Risks and Follow-Up

1. `[fill during execution]`

## 5. Final Wave 2 Closeout Checklist

1. Every candidate has a non-`PENDING` final class.
2. Every accepted or repaired candidate has test evidence recorded.
3. Every rejected or deferred candidate has a local-evidence rationale recorded.
4. No decision widened scope beyond the preflight annex.
