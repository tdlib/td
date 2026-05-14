<!--
SPDX-FileCopyrightText: Copyright 2026 telemt community
SPDX-License-Identifier: MIT
telemt: https://github.com/telemt
telemt: https://t.me/telemtrs
-->

# Upstream Backport Gating Plan (Selective, Security-First, C++23-Strict)

**Plan ID:** upstream-backport-gate-2026-05-08
**Date:** 2026-05-08
**Last updated:** 2026-05-11
**Status:** Execution in Progress (W2/W2-003/W3-P split lanes and initial fingerprint hardening merged to master; Wave 3 critical intake reassessed; remaining waves still planning)
**Primary Goal:** Decide and execute backports from upstream tdlib only when they improve this codebase and pass strict quality/security/TDD/C++23 gates.
**Threat Context:** Active DPI adversary with very high budget and adaptive blocking behavior (ECH blocking and selective QUIC blocking). Every transport-facing decision must be adversarially validated.
**Current Stage Goal:** Keep remaining waves in planning mode, preserve truthful post-merge records, and use the canonical manifest plus decision logs to steer any further backlog intake.

---

## 0. Execution Delta vs Upstream (2026-05-10)

Compared against upstream range `original..upstream/master` and the merged `master` state in this
fork after PR #18 (`258125992`):

1. Implemented and adapted:
    - `a09adfc63` (`Fix reply_to_top_id.`) in `td/telegram/MessageReplyHeader.cpp`, adapted via
       dedicated seams (`normalize_topic_reply_header(...)`,
       `finalize_channel_topic_thread_state(...)`) and bounded hardening tests.
    - `386eca6fe` (`Fix DialogParticipantStatus::GroupAdministrator.`) in
       `td/telegram/DialogParticipant.cpp` and `td/telegram/DialogParticipant.h`, adapted with
       explicit least-privilege legacy rights seams.
    - `1a9ef3d68` (`Repair group administrator rights on load.`) in
       `td/telegram/ChatManager.cpp`, adapted as rank-preserving load normalization.
2. Merged to `master` as separate lanes via PR #18:
      - `5340472b0` (`Fix possible use after move.`) shipped as a standalone W2-003
         lifetime-hardening lane across `td/telegram/CommonDialogManager.cpp`,
         `td/telegram/DialogManager.cpp`, `td/telegram/MessagesManager.cpp`,
         `td/telegram/RecentDialogList.cpp`, `td/telegram/SecretChatActor.cpp`, and
         `td/telegram/StickersManager.cpp`. It was not folded into the completed W2-001/W2-002
         semantics lane.
      - Bounded W3-P poll hardening shipped separately via `21275249c` in
         `td/telegram/PollManager.cpp`, `c81e6da9f` in `td/telegram/QuickReplyManager.cpp`,
         `3e78ebcd8` in `td/telegram/MessagesManager.cpp`, and a supporting local seam in
         `td/telegram/MessageContent.cpp` / `td/telegram/MessageContent.h`. It was not folded into
         Wave 2.
      - Separate fingerprint/tooling work also landed independently in
         `td/mtproto/BrowserProfile.cpp`, `test/analysis/extract_client_hello_fixtures.py`, the
         reviewed ServerHello fixture refreshes, and
         `docs/Plans/FINGERPRINT_TEST_AND_PIPELINE_HARDENING_PLAN_2026-05-09.md`.
3. Historical wave-level outcome:
      - W2-001 and W2-002 are completed as `Accept with Repair`.
      - The 2026-05-08 Wave 2 decision report records W2-003 as `Defer` within that wave.
      - Canonical historical execution log: `docs/Plans/UPSTREAM_WAVE_2_DECISION_2026-05-08.md`.

---

## 1. Non-Negotiable Policy

1. No bulk sync from upstream.
2. Upstream code is input, not authority; each commit is adversarially validated before trust.
3. Every candidate must pass: Contract -> Attack -> Red -> Green -> Survive -> Refactor.
4. All runtime-impacting changes must be fail-closed under invalid input, malformed state, partial delivery, and adversarial timing.
5. C++23 and repository architecture rules are mandatory; C++14/17-era style is adapted or rejected.
6. OWASP ASVS L2-aligned security gates are mandatory.
7. No test weakening to make backports pass. Red tests are treated as defect discovery by default.
8. For TLS/QUIC/stealth behavior, synthetic-only tests are insufficient; real-fixture evidence is mandatory.
9. No cherry-pick, repair, or adaptation work may start for a wave until its preflight annex is approved and explicitly activated.

---

## 2. Scope and Current Backlog Reality

1. Local candidate stack previously reviewed: 16 commits on `chore/compiler-latest-cpp23-hardening`.
2. Upstream backlog from `original` to `upstream/master`: 199 commits.
3. Verified top-level path pressure is overwhelmingly application-heavy: `git log original..upstream/master --name-only` yields 491 touched entries under `td/telegram`, 69 under `td/generate`, and only 2 under `td/mtproto`.
4. Mission-surface path counts are small but still noisy: `td/mtproto` 2 commits, `td/telegram/net` 1, `td/telegram/LinkManager.cpp` 3, `test/link.cpp` 4, `test/mtproto.cpp` 2, and `td/telegram/ConfigManager.cpp` 8.
5. Therefore selective wave intake is mandatory; most of the 199-commit backlog is not a transport/stealth backport lane.
6. Thematic buckets such as "poll-heavy" or "AI-heavy" may still be useful as heuristics, but they are not admissible gating evidence until the preflight annex records exact commit membership.

## 2.1 Planning-Stage Evidence Snapshot (Verified Before Editing This Plan)

1. Verified remotes: `origin` points to `telemt/tdlib-obf`; `upstream` points to `tdlib/td`.
2. Verified comparison anchors: `original` is `8ff05a0e7`; `upstream/master` is `49b3bcbb6`; the backlog size is exactly 199 commits.
3. Path-only filtering is insufficient even on the apparently relevant surfaces:
4. `test/link.cpp` in this backlog mixes proxy-secret evidence (`a82128ab8`, `bfab03f7a`) with unrelated internal-link feature work (`dd78f94a8`, `990b821c8`).
5. `test/mtproto.cpp` hits in this backlog are only generic test/include churn (`bc79a6d2d`, `e86cd4496`) and should not be mistaken for transport hardening evidence.
6. The Darwin `TlsInit` change is not a single-commit candidate:
7. `00eedc5f9` depends on `28e0d0dbe` (`Add Op::random_value.`), so the pair must be reviewed as one coupled bundle if revisited at all.
8. Subject lines are not trustworthy as intake evidence by themselves:
9. `49b3bcbb6` (`Fix compilation error.`) touches only `td/telegram/AiComposeToneExample.hpp`, so it is not a stealth or transport candidate despite the generic title.
10. `6b82cc832` (`Encode key with base64.`) touches `td/telegram/ConfigManager.cpp`, not proxy-link parsing, so subject-only triage would misclassify it.
11. The only upstream `td/telegram/net` commit in the current mission-surface scan is `691cb6a77`, and it adds proxy comments/API metadata rather than transport behavior.
12. Local transport code has already diverged architecturally from upstream in critical areas:
13. `td/mtproto/TlsInit.cpp` in this fork is bound to stealth runtime profile selection, route hints, and ECH circuit-breaker policy; upstream `28e0d0dbe` + `00eedc5f9` still edit the legacy hardcoded Darwin `TlsHello` opcode table.
14. Proxy-link parsing in this fork is controlled by `td/telegram/LinkManager.cpp` and forwards into `td/mtproto/ProxySecret::from_link(...)`, which is also consumed by `td/telegram/net/Proxy.cpp` and serialized proxy paths. Any contract change here has broader consequences than a test-only upstream patch title suggests.
15. Real fixture evidence assets already exist and must be treated as authoritative:
16. `docs/Samples/Traffic dumps/`
17. `test/analysis/fixtures/clienthello/`
18. `test/analysis/fixtures/serverhello/`
19. `test/analysis/fixtures/record_sizes/`
20. `test/analysis/fixtures/imported/`
21. `test/analysis/profiles_validation.json`
22. The fixture pipeline already distinguishes release-gating reviewed evidence from exploratory imported evidence:
23. `test/analysis/profiles_validation.json` gates the reviewed corpus.
24. `test/analysis/profiles_imported.json` is a separate candidate lane for imported captures and is not release-authoritative by itself.

## 2.2 Intake Evidence Collection Rule (Mandatory Before Mission-Fit Filtering)

Before a commit can even be called a candidate, gather and record the following:

1. `git log --reverse --name-status --format='%H%x09%s' original..upstream/master` to build the full 199-commit manifest before narrowing scope.
2. `git show --name-status --format=medium <hash>` to verify touched files and reject misleading titles.
3. `git show --unified=80 <hash> -- <touched paths>` to inspect the real behavioral change, not the summary.
4. `git log original..upstream/master -- <relevant paths>` to check whether the commit is isolated or coupled to adjacent upstream changes.
5. Local owning abstraction and dependent-call-site comparison in this fork.
6. Existing local contract/adversarial/integration tests that already cover or supersede the area.

A commit title never qualifies a candidate by itself.

## 2.3 Two-Pass Backlog Intake and Lane Freeze

Before any wave annex is approved:

1. Pass A must assign every commit in `original..upstream/master` a manifest row with hash, title, touched paths, coarse area, initial class (`ignore`, `defer`, `deep-review`), and a note about title/path mismatch if present.
2. Pass B may open full diffs only after Pass A freezes which lanes are mission-fit enough to deserve deep review.
3. Coupled commits are reviewed as bundles, not singly. Example: `28e0d0dbe` + `00eedc5f9` are one Darwin `TlsInit` evidence bundle.
4. Test-only or comment-only upstream commits are never treated as semantic authority; they only survive as research input after the owning runtime boundary is identified locally.
5. No gate branch may be created until the annex enumerates every commit in the selected lane and explicitly marks all other backlog commits as out of scope for that wave.

## 2.3.1 Canonical Manifest Rule

The full 199-commit backlog must have one canonical planning artifact, not a duplicated appendix copied into
every later wave document.

1. The canonical manifest file is `docs/Plans/UPSTREAM_BACKPORT_MANIFEST_2026-05-08.md`.
2. Every wave preflight annex must reference canonical manifest row IDs instead of re-copying the full
   backlog table.
3. Wave documents may include a wave-scoped extract for readability, but the canonical manifest is the
   only authority for whole-backlog classification.
4. If a commit changes lane, class, or bundle membership, update the canonical manifest first and only
   then update the affected wave annex.
5. Divergent per-wave copies of the same manifest row are forbidden.

## 2.4 Current Mission-Surface Lane Inventory (Verified From Path Scan)

1. Darwin `TlsInit` evidence bundle: `28e0d0dbe`, `00eedc5f9`.
2. Proxy-link evidence bundle: `a82128ab8`, `bfab03f7a`.
3. Proxy metadata/API lane: `691cb6a77`.
4. Link-parser feature lane: `8921c22f0`, `dd78f94a8`, `990b821c8`.
5. Config/title-noise lane: `6b82cc832` plus other `ConfigManager.cpp` option churn; this lane is not a transport candidate without further evidence.
6. Test/include noise lane: `e86cd4496`, `bc79a6d2d`.
7. **Utility fix already incorporated lane**: `3d38fb7aa` (`Remove early initialization of tz_offset.`) — the fix is already present in this fork as part of `2b7356173` (`Merged most upstream changes`). The `namespace detail { // may cause static initialization order fiasco... }` comment exactly matches the upstream change. No further action.
8. **Test superseded lane**: `bc79a6d2d` (`Improve test.`) — the upstream simplifies `GetHostByNameActor` test. This fork already retains the richer version. No action.

## 2.5 Deep Diff Analysis Findings (Verified by Actual Full Diffs and Real Fixture Evidence)

These findings are recorded after reading full diffs for every mission-surface commit and cross-referencing
against `test/analysis/fixtures/clienthello/ios/` and `test/analysis/fixtures/clienthello/macos/` fixture
corpora. No finding in this section is based on subject-line inference.

### 2.5.1 Darwin TlsInit Bundle (`28e0d0dbe` + `00eedc5f9`) — Defect Findings

**Defect 1 — Static-initialization randomization (per-process, not per-connection):**

`Op::random_value()` selects one cipher suite variant by calling `Random::fast(...)` inside the factory
method itself, then stores the selected variant in `res.value`. The factory is called during C++ static
initialization of `static TlsHello result = [](){...}()`. This means the cipher suite variant choice is
made exactly once per process lifetime, not once per connection.

Impact for DPI evasion: every connection from the same process uses the same cipher suite list.
An observer who captures multiple connections from the same client can correlate them trivially by
cipher suite fingerprint. This reduces effective per-connection entropy from two choices to one
per-process label — a measurable DPI fingerprinting attack surface.

The correct implementation must select the variant at connection time (inside `generate_hello()` or
equivalent per-connection entry point), not at static initialization.

Verdict: this is a genuine code defect in the upstream implementation, not a test-only concern.
If any cipher suite randomization feature is ported to this fork, it must be per-connection.

**Defect 2 — Variant 1 cipher suite list is a phantom with no real fixture evidence:**

Upstream variant 1 length: `\x00\x1c` = 28 bytes = GREASE + 13 cipher suites:
`0x1302, 0x1301, 0x1303, 0xC02C, 0xC030, 0xC02B, 0xCCA9, 0xC02F, 0xCCA8, 0xC00A, 0xC009, 0xC014, 0xC013`

Fixture evidence for every Safari iOS 18.x capture in `fixtures/clienthello/ios/`:
- safari17_2: GREASE + `0x1301, 0x1302, 0x1303, C02C, C02B, CCA9, C030, C02F, CCA8, C00A, C009, C014, C013, 009D, 009C, 0035, 002F, C008, C012, 000A` (20 suites)
- safari18_7_6_ios18_7_6: GREASE + same order as safari17_2 (20 suites)

Fixture evidence for every Safari iOS 26.x capture in `fixtures/clienthello/ios/`:
- safari26_2_ios26_2_a, safari26_3_*, safari26_4_*: GREASE + `0x1302, 0x1303, 0x1301, C02C, C02B, CCA9, C030, C02F, CCA8, C00A, C009, C014, C013, 009D, 009C, 0035, 002F, C008, C012, 000A` (20 suites)

Observations:
- The order `0x1302, 0x1301, 0x1303` in variant 1 does not appear in any iOS or macOS Safari fixture in the corpus.
- Safari iOS 26 uses `0x1302, 0x1303, 0x1301` (confirmed by multiple independent captures).
- Safari iOS 18 uses `0x1301, 0x1302, 0x1303` (pre-iOS-26 ordering).
- Variant 1 omits all legacy RSA-static and 3DES cipher suites. Real Safari (all versions) includes `0x009D, 0x009C, 0x0035, 0x002F, 0xC008, 0xC012, 0x000A`.
- Including variant 1 would produce a cipher suite fingerprint that matches no real Safari client in the fixture corpus. This would increase DPI fingerprinting risk, not decrease it.

Verdict: variant 1 must not be ported. Variant 2 (`1302, 1303, 1301` + 20 suites) is fixture-verified for iOS/macOS 26.

**Finding 3 — ML-KEM 768 key share addition for Darwin is valid but already implemented in this fork:**

Upstream adds `Op::ml_kem_768_key()` to the Darwin cipher suite path. The real Safari iOS 26 fixture
confirms: `key_share_entries` includes group `0x11EC` (ML-KEM 768) with `key_exchange_length` = 1216
bytes (`0x04C0`). The `supported_groups` extension includes `0x11EC` before `0x001D`.

This fork's `TlsHelloProfileRegistry.cpp` already defines `BrowserProfile::Safari26_3` and
`BrowserProfile::IOS14` with `pq_group_id = 0x11EC`, and
`TlsHelloBuilder.h` already declares `kCurrentSingleLanePqGroupId = 0x11EC` and
`kCurrentSingleLanePqKeyShareLength = 0x04C0`, matching the real fixture data exactly.

Verdict: ML-KEM 768 for Darwin/iOS profiles is already correctly implemented in this fork. No porting needed for this aspect.

**Finding 4 — Upstream Darwin supported_versions change is fixture-consistent:**

Upstream removes TLS 1.0 and 1.1 from the Darwin supported_versions extension. Real Safari iOS 26
extension `0x002B` (supported_versions) body: `06 2A2A 0304 0303` = GREASE + TLS 1.3 + TLS 1.2 only.
This confirms the upstream change is correct. The fork's builder already uses the fixture-driven profile,
so this does not affect the fork directly.

### 2.5.2 Proxy-Link Evidence Bundle (`a82128ab8` + `bfab03f7a`) — Semantic Findings

**Finding 1 — "Invalid but accepted" semantics explicitly annotated by upstream:**

`bfab03f7a` adds comments `// invalid, but accepted` to two test vectors:
- `7ge9Ug57SJOnMe8J%2BSj5pyZnaXRodWIuY29t` (percent-encoded `+` → standard base64, not URL-safe)
- `7ge9Ug57SJOnMe8J%2FSj5pyZnaXRodWIuY29t` (percent-encoded `/` → standard base64, not URL-safe)

Both are accepted by `ProxySecret::from_link(...)` and normalized to their URL-safe base64 equivalents.
This confirms the parser has permissive encoding acceptance beyond the URL-safe-only spec.

Impact: multiple distinct input encodings normalize to the same secret. An adversary who can control
the raw URL string but not its post-normalization value could attempt to mask injection attempts behind
valid-looking normalized forms. This is a security boundary hygiene concern.

Required adversarial test surface: test that all accepted encoding variants normalize to the same
canonical form, that no two distinct canonical forms have overlapping acceptance sets, and that
out-of-spec encodings that should NOT be accepted (e.g., doubly-encoded, mixed standard + URL-safe)
are correctly rejected without silent normalization.

These test vectors are hostile evidence for the local `ProxySecret::from_link(...)` boundary and must
be harvested into the fork's adversarial test suite regardless of any direct backport decision.

**Finding 2 — `a82128ab8` is a community pull request (not levlam), increasing caution:**

`a82128ab8` was authored by `spark@uwtech.org`, not the core upstream author. Community-contributed
test vectors require independent verification of the expected output before any harvest.

### 2.5.3 `6b82cc832` (`Encode key with base64.`) — Misleading Title, Not Base64

Despite the commit title, `6b82cc832` does NOT encode the key with base64. It uses `hex_decode()` to
embed the Firebase Remote Config API key (`AIzaSyC2-kAkpDsroixRXw-sTw-Wfqo4NxjMwwM`) as a hex
string literal, so it becomes `"41497a61537943322d...4d"` in source. This is binary obfuscation to
evade string-search tools, not a cryptographic improvement. The key remains hardcoded (OWASP A02).

The fork's `ConfigManager.cpp` was NOT updated with this change (verified by diff against `original`
baseline — no change detected). The change has no transport or stealth benefit and introduces misleading
source code. This lane remains **Ignore** for transport gate purposes.

Note: if the Firebase bootstrap path is ever evaluated for a structural fix (reading the key from
environment or config), that is a separate task and must not be confused with this obfuscation commit.

### 2.5.4 `691cb6a77` (`Support comments for added proxy.`) — API Feature, DB Validation Gap

Adds a `comment` string field to the proxy management API. Each proxy now has a separate DB key via
`get_proxy_comment_database_key(proxy_id)`. The `set_proxy_comment` method in `ConnectionCreator`
lacks explicit length constraints on the comment string in the visible diff. An adversarial observer
could supply an unbounded comment string, potentially stressing DB storage or memory for the stored
proxy list.

This is not a transport hardening concern but warrants a note: if this API surface is ever enabled in
the fork, the `comment` field must have a documented maximum length and an input boundary test.

### 2.5.5 `bc79a6d2d` (`Improve test.`) — Upstream Simplified What Fork Already Has Better

The upstream `GetHostByNameActor` test refactoring in `test/mtproto.cpp` simplifies the original test
structure. This fork already retains the richer multi-resolver-type test loop from the original
version. The upstream "improvement" is a net regression in test coverage for this fork. No action.

### 2.5.6 `3d38fb7aa` (`Remove early initialization of tz_offset.`) — Already Fixed in This Fork

The `namespace detail { int init_tz_offset_private = Clocks::tz_offset(); }` static initialization
that could trigger POSIX `std::localtime` order-of-initialization UB is already commented out in
`tdutils/td/utils/port/Clocks.cpp` with the exact explanatory comment from `3d38fb7aa`.
This was incorporated as part of commit `2b7356173` (`Merged most upstream changes`).
No further action needed.

### 2.5.7 Non-Stealth Upstream Backlog Triage — Hardening-First Findings

The full upstream range changes 114 files in this fork's path layout, with 91 files under
`td/telegram/`. This concentration matters more than the raw file count: most of the non-stealth
backlog is product/API churn, not reliability hardening. The non-stealth intake policy for this
plan is therefore hardening-first, feature-second.

**Finding 1 — Small correctness fixes were the right non-stealth starting point:**

The narrowest worthwhile candidates identified for early correctness were:

- `a09adfc63` (`Fix reply_to_top_id.`): repairs topic-reply normalization in
   `td/telegram/MessageReplyHeader.cpp`. This gap is now implemented in the staged Wave 2 work with
   fork-specific seam hardening and dedicated contract/adversarial/fuzz/stress coverage.
- `386eca6fe` (`Fix DialogParticipantStatus::GroupAdministrator.`) plus `1a9ef3d68`
   (`Repair group administrator rights on load.`): repair administrator-right interpretation in
   `td/telegram/DialogParticipant.cpp` and persisted-load repair in `td/telegram/ChatManager.cpp`.
   This lane is now implemented in the staged Wave 2 work with fork-adapted least-privilege
   semantics and rank-preserving load normalization.

Verdict update (2026-05-09): this correctness lane completed Wave 2 gating and is classified in the
Wave 2 decision report (`Accept with Repair` for W2-001 and W2-002).

**Finding 2 — `5340472b0` is worth mining, but not blind cherry-picking:**

`5340472b0` (`Fix possible use after move.`) is the right class of change for a hardening review,
but it touches multiple files and some local call sites have already diverged. Example: the local
tree still computes `dialogs->dialogs_.size()` in `td/telegram/MessagesManager.cpp` at the call site
upstream adjusted, while other null-content guard patterns from nearby hardening commits are already
present locally. Treat this commit as a hunk-by-hunk lifetime audit input, not as an automatic
direct-backport candidate.

**Finding 3 — Broad null-content hardening is already partially absorbed here:**

`13003156a` (`Check that content isn't nullptr everywhere.`) is lower value as a backport target
because the local tree already contains many `CHECK(content != nullptr)` guards in
`td/telegram/MessageContent.cpp` and related message-content helpers. Re-review for specific missing
gaps if a crash signature appears, but do not prioritize this lane ahead of the reply-thread and
administrator-rights repairs.

**Finding 4 — Large `td/telegram` feature bundles are not hardening by default:**

The AI compose/text composition style lane, poll-media lane, managed-bot access lane, and other
large `td/telegram` expansions increase state surface, API surface, and message-content complexity.
They may still be valuable for product reasons, but they are not justified as "best-practice"
hardening work. Under this plan they remain deferred unless a separate product objective is approved
with its own contract snapshot, risk register, and adversarial test matrix.

### 2.5.8 Whole-Backlog Lane Catalog (Verified From Full 199-Commit Manifest)

The current draft was too Wave-1 and Wave-2 centric. The full backlog has now been coarse-classified
into explicit lanes so later work can be planned phase by phase instead of rediscovered from scratch.

| Lane ID | Coarse Commit Count | Dominant Files / Seams | Current Planning Class | Why It Must Be a Separate Wave |
|---|---:|---|---|---|
| W1-T transport / parser research | 10 | `td/mtproto/TlsInit.cpp`, `td/telegram/LinkManager.cpp`, `td/telegram/net/ConnectionCreator.cpp`, `test/link.cpp` | Research Input / Defer | Shared parse and transport boundaries; direct cherry-pick is unsafe |
| W2-C core correctness | 4 | `MessageReplyHeader`, `DialogParticipant`, `ChatManager`, `MessagesManager` | Deep Review | Small bounded repairs with direct correctness benefit |
| W2B residual micro-correctness | 9 | `MessagesManager`, `DialogManager`, `DraftMessage`, `DialogParticipant` | Defer | Bounded correctness queue kept separate from W2 core so each candidate can be audited in isolation |
| W3-P poll and poll-media capability bundle | 70 | `td/generate/scheme/td_api.tl`, `MessageContent.cpp`, `MessagesManager.cpp`, `PollManager.cpp`, `PollOption.*`, `FileManager.cpp`, `Requests.*`, `cli.cpp` | Defer | Large cross-layer feature bundle with message-content and file-reference blast radius |
| W4-G guest-bot capability bundle | 8 | `td/generate/scheme/td_api.tl`, `UpdatesManager.*`, `MessagesManager.cpp` | Defer | Update-ordering and message-directionality semantics across multiple managers |
| W5-AI text-composition / AI-compose bundle | 22 | `td/generate/scheme/td_api.tl`, `TranslationManager.*`, `AiComposeTone.*`, `Requests.*`, `cli.cpp`, `ConfigManager.cpp` | Defer | Product/API expansion with config, update, and persistence implications |
| W6-M managed-bot access bundle | 4 | `td/generate/scheme/td_api.tl`, `BotAccessSettings.*`, `Requests.*` | Defer | Small commit count but security-sensitive access-control surface |
| W7-D tooling / examples / docs | 3 | `example/*`, docs and wrappers | Defer | Low mission value; should not be mixed with runtime hardening |
| W8-X residual mixed queue | 69 | Mixed `td/telegram/*` files | Needs second-pass split | Too heterogeneous to wave-plan safely as one bucket |

Interpretation rule: the coarse count is a planning aid, not merge authority. Any lane promoted from
`Defer` to active review still needs an exact manifest row for every included commit.

Count provenance note: these numbers come from the current Pass A canonical manifest snapshot and may
shift only if a commit's lane assignment changes in a recorded manifest edit.

### 2.5.9 Schema-Coupling Findings (Verified by Path-Level Review)

The upstream backlog is not just `td/telegram`-heavy; it is schema-coupled.

1. `td/generate/scheme/td_api.tl` is touched by 68 upstream commits in the current 199-commit range.
2. Request and operator-entry surfaces move with it: `td/telegram/Requests.cpp` is touched by 23 commits,
   `td/telegram/Requests.h` by 16, and `td/telegram/cli.cpp` by 23.
3. Representative feature commits confirm capability-bundle fan-out rather than isolated fixes:
   - `b2fd2e468` (`Add inputMessagePoll.media.`) touches `td_api.tl`, `MessageContent.cpp`, and `cli.cpp`.
   - `7aed695bf` (`Add td_api::updateNewGuestQuery.`) touches `td_api.tl`, `UpdatesManager.cpp`, and `UpdatesManager.h`.
   - `23971a844` (`Add td_api::createTextCompositionStyle.`) touches `td_api.tl`, `Requests.*`, `TranslationManager.*`, and `cli.cpp`.
   - `19292458f` (`Add td_api::botAccessSettings.`) touches `CMakeLists.txt`, `SplitSource.php`, `td_api.tl`, and `BotAccessSettings.*`.
4. Therefore any lane that touches `td_api.tl` must be treated as a capability bundle by default.
   Per-commit cherry-picks are forbidden unless a later review proves the commit is mechanically isolated.

### 2.5.10 Residual Micro-Correctness Queue (Verified Small-Blast-Radius Candidates)

Outside the current Wave 2 shortlist, the backlog contains a second tier of bounded correctness
commits that should remain visible instead of being buried in the mixed queue:

1. Reply and draft semantics: `d5714b0b8`, `bcbe2f309`, `562bce098`.
2. Dialog / username error handling: `84d2ea0d8`, `a96365b5f`.
3. Basic-group rights normalization: `8fc2344f3`, `9c62782dc`.
4. Guest-message correctness: `aeddf8ca3`, `336504954`.

These are not promoted into the active Wave 2 shortlist yet. They are a separate deferred queue that
can be reviewed one bounded commit or coupled pair at a time after Wave 2 core closes.

### 2.5.11 Residual Mixed Queue Split Requirements (Must Happen Before Any Later Cherry-Pick)

`W8-X` is too large and heterogeneous to remain a single deferred bucket. Before any post-Wave-2
candidate is opened, it must be split into explicit sub-lanes with exact commit membership.

Required sub-lanes:

1. `W8-P` parse and validation hardening:
   commits that tighten input validation, identifier normalization, malformed-input rejection,
   and parser fail-closed behavior without introducing new API surface.
2. `W8-S` state and persistence repair:
   commits that change load/save semantics, round-trip normalization, draft/reply state repair,
   or persisted object recovery without requiring a capability bundle.
3. `W8-C` concurrency, lifetime, and actor-safety:
   commits that address use-after-move, callback ordering, actor state sequencing, ownership,
   or shared-state correctness.
4. `W8-A` API or feature spillover:
   commits that look small by subject or path but actually depend on schema churn, new request
   objects, update fan-out, or other capability-bundle semantics. These do not stay in Wave 8;
   they must be moved to Waves 3 through 6 or rejected.

Split rules:

1. A commit that touches `td_api.tl` or introduces new td_api request/update types cannot remain in `W8-*`.
2. A commit that changes both parser validation and persisted state must be assigned to the dominant
   runtime risk surface and cross-referenced from the secondary one.
3. A commit that cannot be explained by one dominant seam after a local owning-abstraction read stays
   blocked from execution until the canonical manifest records the unresolved ambiguity explicitly.
4. The `W8-X` bucket is only a temporary holding lane. It is not an execution wave.

Deliverable rule:

1. Publish the split as `docs/Plans/UPSTREAM_WAVE_8_SPLIT_2026-05-08.md` before any `W8-*` candidate
   is approved for contract capture.


---

## 3. Mission-Fit Intake Filter (Before Any Cherry-Pick)

Each commit must pass all intake checks before entering a gate branch.

1. Security and transport relevance: direct impact on stealth shaping, TLS/SSL, proxy links, parser hardening, state machine correctness, or resilience under hostile networks.
2. Architectural fit: no avoidable cross-layer leakage and no boundary collapse.
3. C++23 fit: either already compliant or has a clear low-risk adaptation path.
4. Testability fit: behavior can be pinned by contract plus adversarial tests in separate files.
5. Exclusion filter: product/API expansion without mission benefit is deferred or rejected.
6. Supersession check: if this fork already implements a stricter or more advanced architecture on the same surface, the upstream commit is rejected as a direct backport and may only survive as research input.
7. Boundary check: if the changed code sits at a parse, crypto, or transport boundary, fail-closed semantics take precedence over upstream compatibility assumptions.

---

## 4. Decision Classes

Each commit or tightly coupled commit group is assigned one class.

1. **Accept**: mission-fit and passes all gates without behavior repair.
2. **Accept with Repair**: net-positive, but defects/regressions were fixed in the gate branch before merge.
3. **Reject**: violates security/stealth invariants, architecture rules, C++23 standards, or introduces disproportionate risk.
4. **Defer**: not currently mission-fit or too coupled to safely validate in isolation.

Additional planning-stage tags:

1. **Research Input Only**: do not cherry-pick; mine the commit for fixture ideas, red-test seeds, or browser-pattern comparison only.
2. **Evidence Bundle**: small test/comment/follow-up commits that should be reviewed together for signal, but not necessarily merged together.

---

## 5. Mandatory Technical Gate Pipeline

All phases are mandatory and ordered.

## 5.0 Planning Preflight: Inventory Freeze

1. Complete the Pass A full-backlog manifest for all 199 upstream commits.
2. Freeze the current wave's candidate lanes and coupled bundles before any gate branch is created.
3. For test-only or comment-only upstream commits, map the implied behavior to the owning runtime boundary in this fork; if no runtime boundary matters, keep the commit in research-only storage.
4. For locally superseded seams such as `td/mtproto/TlsInit.cpp`, decide direct-backport rejection versus research-only status before Phase 0 starts.
5. Preflight annex approval is the only entry criterion to Phase 0 contract capture.

## 5.0.1 Planning Preflight: Cipher Suite and Extension Evidence Triangulation (Mandatory for Any Transport Change)

This sub-phase is mandatory for any upstream commit, profile update, or research harvest that claims
to improve browser plausibility of TLS cipher suites, extension ordering, key shares, or supported
groups. Fixture-only claims must be rejected.

1. For each proposed cipher suite list, identify the claimed browser/OS family and version.
2. Retrieve all matching fixture files from `test/analysis/fixtures/clienthello/{os_family}/`.
3. For each fixture, extract: cipher suite list (full, ordered), total count, GREASE presence and
   position, TLS 1.3 suite ordering (`0x1301/0x1302/0x1303`), legacy suite presence (`0xC00A`,
   `0xC014`, `0x009D`, `0x009C`, `0x0035`, `0x002F`, `0xC008`, `0xC012`, `0x000A`).
4. Compare the proposed cipher suite exactly against the fixture population:
   - Is the cipher suite order identical to the fixture (any variant)?
   - Is the suite count correct?
   - Are legacy suites included or excluded consistently with the fixture family?
5. If no existing fixture supports the proposed cipher suite, the change is **rejected as phantom**.
   A claim of "this is what browser X sends" requires at least one fixture matching it exactly.
6. For key share evidence: verify `key_share_entries` in the fixture against the proposed
   key share list (GREASE positions, PQ group 0x11EC / length 0x04C0, X25519 / 32 bytes).
7. For supported_groups evidence: verify `supported_groups` extension body against the proposed
   groups, including ordering.
8. Record in the preflight annex: which fixture files were consulted, what exact cipher suite
   sequences were found, and whether the proposed change is fixture-confirmed or phantom.

## 5.0.2 Planning Preflight: Schema and Capability-Bundle Gate (Mandatory for Any `td_api.tl` Touch)

This sub-phase is mandatory for any upstream commit bundle that touches `td/generate/scheme/td_api.tl`
or introduces new request/update/API objects.

1. Freeze the capability bundle before any cherry-pick branch is created. A capability bundle must list:
   schema files, generated or split-source impacts, request/CLI/update entry points, owning managers,
   persistence/state files, and test surfaces.
2. Cherry-picking a schema commit without its required runtime adapters is forbidden. `td_api.tl`,
   `Requests.*`, `cli.cpp`, `UpdatesManager.*`, and owning manager changes must be reviewed together.
3. For schema-coupled waves, direct cherry-pick is the exception rather than the default. The normal
   path is semantic transplant into local C++23-compliant code after RED tests exist.
4. Every capability bundle must define an explicit product objective. "Upstream has the feature" is not
   a valid objective.
5. Every capability bundle must declare at least one kill-switch criterion that would force rejection,
   such as unbounded state growth, permission-model ambiguity, parser ambiguity, or inability to pin the
   behavior with deterministic contract and adversarial tests.
6. Generated-surface bundles must include a rollback plan: which local files can be reverted cleanly if
   the schema introduction proves too coupled during RED-phase testing.

## 5.0.3 Planning Preflight: Canonical Manifest Row Schema

The canonical manifest row format must be fixed before Wave 1 preflight is approved.

Every manifest row must include at least:

1. stable row ID
2. upstream hash
3. subject line
4. touched paths
5. coarse lane ID
6. coupled bundle ID, if any
7. `td_api.tl` / generated-surface touch flag
8. local owning seam or nearest controlling abstraction
9. current planning class (`ignore`, `defer`, `deep-review`, `research-input`, `already-incorporated`)
10. prerequisite or follow-up commit set if isolated review would be invalid
11. local divergence note
12. security tags where applicable (`ASVS-V5`, `ASVS-V7`, `ASVS-V9`, `ASVS-V11`, DPI, secrets, etc.)
13. title/path mismatch note, if present
14. evidence note pointing to fixture files, local tests, or owning code

No wave annex may invent extra ad-hoc row formats for the same backlog.

## 5.0.4 Planning Preflight: Residual Queue Second-Pass Split Gate

Before any candidate from the residual mixed queue can move beyond `Defer`, complete the second-pass
split defined in Section 2.5.11.

Mandatory outputs:

1. exact commit membership for `W8-P`, `W8-S`, `W8-C`, and `W8-A`
2. one-paragraph objective for each sub-lane
3. one kill-switch criterion for each sub-lane
4. one default test-family emphasis for each sub-lane:
   - `W8-P`: negative, adversarial, and light fuzz first
   - `W8-S`: contract, integration, and persisted-load round-trip first
   - `W8-C`: regression, stress, and sanitizer-friendly lifetime checks first
   - `W8-A`: reject or migrate to a capability charter before tests are designed

The split is a planning gate, not an implementation task.

## 5.0.5 Planning Preflight: Managed-Bot Access Security Charter Gate

Wave 6 cannot move from `Defer` to active review until a dedicated managed-bot security charter exists.

The charter must define:

1. subject-object-action matrix for every access-setting transition
2. deny-by-default rule for any unspecified transition
3. persistence surfaces and load-time normalization rules
4. caller inventory across `Requests.*`, generated td_api entry points, and owning manager code
5. explicit revoked-access semantics and replay behavior
6. adversarial tests for contradictory, replayed, stale, and privilege-escalating transitions
7. kill-switch criteria for ambient privilege inheritance, ambiguous ownership, or unverifiable caller scope

No managed-bot access cherry-pick, adaptation task, or schema import may start before this charter is approved.


## 5.1 Phase 0: Contract Snapshot and Dependent Audit

For every touched boundary, document:

1. Inputs: types, ranges, nullability, invariants.
2. Outputs: success and error semantics.
3. Side effects: state/network/filesystem/logging.
4. Preconditions and postconditions.
5. Thread-safety and ownership/lifetime.

Required actions:

1. Add or update dedicated contract tests in separate files.
2. Enumerate dependent callers and assumptions that may break.
3. Record out-of-scope break risks as "NOTICED BUT NOT TOUCHING".
4. For shared parse or transport boundaries, the dependent audit must cover every production caller class rather than a single entry point. Example: `ProxySecret::from_link(...)` requires LinkManager, API proxy creation, and serialized proxy loading coverage.

## 5.2 Phase 1: Risk Register

Each HIGH or CRITICAL zone must include:

1. Location (file/symbol).
2. Category (memory, concurrency, protocol, resource exhaustion, crypto, logging secrecy, DPI fingerprinting).
3. Concrete exploit scenario.
4. Impact type (crash, corruption, bypass, leak, RCE).
5. Linked adversarial test IDs.

## 5.3 Phase 2: Red (Attack-First Tests)

Before any implementation adaptation or repair:

1. Write failing tests for all required families.
2. Verify failures are for the intended behavioral reason.
3. If a test passes before implementation change, strengthen test until it can fail on defect.
4. Keep tests in separate files; no inlined tests in source files.

## 5.4 Phase 3: Green (Minimal Correct Repair)

1. Implement the smallest safe change that satisfies failing tests.
2. Avoid unrelated refactors.
3. Never weaken tests to match broken behavior.

## 5.5 Phase 4: Survive (Adversarial Validation)

1. Every HIGH/CRITICAL risk has at least one adversarial test.
2. Input/parser boundaries have light fuzz coverage.
3. Resource-sensitive changes have stress validation.
4. Fail-closed behavior is verified for malformed, truncated, replayed, and out-of-order inputs.

## 5.6 Phase 5: C++23 and Architecture Compliance

Backport adaptation requirements:

1. Preserve or improve RAII ownership clarity and deterministic lifecycle handling.
2. Avoid legacy patterns when safer C++23/C++20/C++17 alternatives are already available in project style.
3. Maintain strict layer boundaries and anti-corruption translation seams.
4. Keep public surface minimal and behavior contracts explicit.

## 5.7 Phase 6: Static Analysis, Lint, and Compiler Hygiene

A candidate fails this gate if modified files introduce any of:

1. New compiler warnings or IDE errors.
2. New lint/static-analysis issues.
3. New security hotspot or taint findings without mitigation and tests.

Rule:

1. Zero new findings in touched files is required for Accept/Accept with Repair.

## 5.8 Phase 7: 14-Core Validation Matrix

1. Build and test validation runs use 14-core parallelism.
2. Exact test discovery is refreshed by configure when new tests are added.
3. Relevant transport/security slices are executed (TLSHello, SSL context, scheduler/lifecycle, and targeted exact-case suites).

## 5.9 Phase 8: Deterministic Classification

1. Accept only when all mandatory gates pass and no unresolved HIGH/CRITICAL risk remains.
2. Accept with Repair only when repairs are minimal, justified, and fully regression-tested.
3. Reject when security posture, stealth plausibility, architecture integrity, or C++23 compliance regresses.
4. Defer when coupling or mission misfit prevents safe isolated validation.

---

## 6. Real-Traffic Fixture Evidence Gate (No Guessing)

Transport and stealth validation must be anchored to real captures.

1. Source traffic dumps: `docs/Samples/Traffic dumps/`.
2. Test fixtures: `test/analysis/fixtures/`.
3. Validation registry: `test/analysis/profiles_validation.json`.

Mandatory rule:

1. If a change touches TLS/QUIC/ECH/proxy framing/shaping paths, acceptance requires fixture-driven tests built from real traffic dumps.

Operational checks:

1. Reviewed corpus (`fixtures/clienthello/**/*`, `fixtures/serverhello/**/*`, `profiles_validation.json`) is the release-gating lane.
2. Imported corpus (`fixtures/imported/**/*`, `profiles_imported.json`) is exploratory only; it can seed candidate fixtures and browser-pattern hypotheses, but it cannot by itself authorize acceptance.
3. If a candidate claims browser-plausibility improvement, compare against both reviewed and imported lanes; when they disagree, the reviewed lane wins until new captures are curated.
4. Refresh generated serverhello corpus when parser/profile logic changes.
5. Run reviewed corpus smoke validation before classifying a candidate.
6. Run imported-corpus smoke when the claim depends on newly imported families or unexplained browser variance.
7. Add new regression fixtures when a subtle evasion/fingerprint issue is discovered.
8. For any TLS/QUIC candidate, record which concrete dump families and generated fixture families were consulted.
9. For any browser-plausibility change, compare against imported fixture summaries before allowing a direct backport claim.

Reference commands:

```bash
python3 test/analysis/generate_server_hello_fixture_corpus.py \
  --registry test/analysis/profiles_validation.json \
  --input-root test/analysis/fixtures/clienthello \
  --output-root test/analysis/fixtures/serverhello

python3 test/analysis/run_corpus_smoke.py \
  --registry test/analysis/profiles_validation.json \
  --fixtures-root test/analysis/fixtures/clienthello \
  --server-hello-fixtures-root test/analysis/fixtures/serverhello

python3 test/analysis/run_corpus_smoke.py \
  --registry test/analysis/profiles_imported.json \
  --fixtures-root test/analysis/fixtures/imported/clienthello \
  --server-hello-fixtures-root test/analysis/fixtures/imported/serverhello
```

---

## 7. Required Test Families Per Non-Trivial Runtime Change

Unless explicitly and credibly marked not applicable, all families are required:

1. Contract tests.
2. Positive tests.
3. Negative rejection tests.
4. Edge and limit tests.
5. Adversarial black-hat tests.
6. Integration tests.
7. Light fuzz tests.
8. Stress tests.

Quality rules:

1. Test names describe scenario/attack behavior.
2. Assertions are specific and behaviorally meaningful.
3. Cleanup paths are validated in both success and failure cases.
4. No sleep-based synchronization for concurrency behavior.
5. Failing red tests are investigated as potential real defects first.

---

## 8. Security and OWASP ASVS L2 Gate Focus

Backports must preserve or improve these controls where relevant:

1. Input validation and strict bounds enforcement (ASVS V5).
2. Session/token handling hygiene where applicable (ASVS V2/V3).
3. Cryptographic correctness and nonce/key safety (ASVS V6).
4. Error handling without secret/internal leakage (ASVS V7).
5. Protocol state validation and malformed input rejection (ASVS V9).
6. Concurrency/shared-state safety under contention (ASVS V11).

DPI-domain additions:

1. No plaintext SNI leakage in fallback/error paths.
2. No identifiable fallback probes during transport transition.
3. No deterministic timing/fingerprint signatures introduced by retry/recovery logic.
4. QUIC/TLS behavior remains browser-plausible under fixture comparison.

---

## 9. 14-Core Execution Baseline

All heavy build/test validation runs must use 14 CPU cores.

Recommended build and ctest baseline:

```bash
cmake --build build --target run_all_tests --parallel 14
ctest --test-dir build --output-on-failure -j 14
```

Targeted exact-case sharding pattern:

```bash
./build/test/run_all_tests --list 2>&1 | rg '^(Test_GroupPrefix...)' > /tmp/group.txt
xargs -P 14 -I {} ./build/test/run_all_tests --exact "{}" < /tmp/group.txt
```

---

## 10. Wave Model

## 10.1 Wave 0 (Completed): Candidate 16-Commit Stack

Result: **Accept with Repair**.

Why:

1. Candidate stack had a scheduler budget regression with fail-open risk.
2. Repaired by restoring target-aware fail-closed budget logic and adding regression guards.

Repair branch and commits:

1. Branch: `chore/compiler-latest-cpp23-hardening-repair-scheduler`.
2. `c33106751` - tests: scheduler budget regression guards.
3. `57baeedba` - stealth: chaff scheduler budget fail-closed repair.

Validation evidence (14-core):

1. Lifecycle gate: 43/43.
2. SSL/certificate loading gate: 59/59.
3. TLSHello gate: 40/40.
4. Scheduler gate: 19/19.

Decision:

1. Original candidate stack: reject as-is.
2. Repaired candidate stack: acceptable for merge consideration.

## 10.2 Wave 1 (Next): Mission-Fit Upstream Subset Only

Wave 1 must start from lane-based preflight, not a subject-picked shortlist. After reading the real
upstream diffs and cross-referencing with real fixture evidence (see Section 2.5), the current
preflight classification is:

### 10.2.1 Darwin TlsInit Bundle: `28e0d0dbe` + `00eedc5f9`

- **Class:** Research Input Only / Evidence Bundle.
- **Direct-backport status:** Reject as a direct backport candidate.
- **Architecture reason:** this fork routes TLS-init through the stealth runtime profile selection
  system (`stealth::pick_runtime_profile`, `stealth::build_proxy_tls_client_hello_for_profile`).
  The upstream changes edit a monolithic hardcoded opcode table class inside `TlsInit.cpp`. These are
  architecturally incompatible; a line-for-line cherry-pick would bypass the local stealth seam.
- **Code defect reason 1 — per-process randomization bug (see Section 2.5.1 Defect 1):**
  `Op::random_value()` selects the cipher suite variant at C++ static initialization time, not at
  connection time. This is a DPI fingerprinting leak: all connections from the same process carry
  an identical cipher suite, allowing per-process correlation. Any cipher suite randomization feature
  ported to this fork must execute per-connection.
- **Code defect reason 2 — variant 1 is a phantom (see Section 2.5.1 Defect 2):**
  The upstream variant 1 cipher suite list (`0x1302, 0x1301, 0x1303` order + 13 suites, no RSA/3DES)
  does not match any real Safari fixture in the corpus. Using it would create a fingerprintable
  anomaly. Only variant 2 (`0x1302, 0x1303, 0x1301` + 20 suites) is fixture-verified for iOS/macOS 26.
- **Already-covered finding:** ML-KEM 768 (0x11EC, 1216 bytes) for Darwin profiles is already
  implemented correctly in `TlsHelloProfileRegistry.cpp` (`BrowserProfile::Safari26_3`,
  `BrowserProfile::IOS14`). No porting action needed for this aspect.

- **Required research action for Wave 1 or Wave 2 profile review (NOT a direct backport):**
  1. Confirm whether the fork's `IOS14` and `Safari26_3` profiles already emit the variant 2
     cipher suite order (`0x1302, 0x1303, 0x1301`) as verified by fixture corpus.
  2. If they emit the older iOS 18 order (`0x1301, 0x1302, 0x1303`), produce a profile update
     grounded in the `safari26_*` fixture evidence — not the upstream opcode table.
  3. Confirm the fork's supported_versions field for Safari/iOS profiles matches the
     fixture evidence (TLS 1.3 + TLS 1.2 only, no TLS 1.0/1.1).
  4. Write cipher-suite regression tests anchored to the reviewed fixture corpus before any
     profile change is merged.

### 10.2.2 Proxy-Link Evidence Bundle: `a82128ab8` + `bfab03f7a`

- **Class:** Research Input Only / Evidence Bundle.
- **Direct-backport status:** Reject as behavior authority.
- **Reason:** `a82128ab8` adds URL-encoded base64 test vectors (community PR). `bfab03f7a`
  explicitly annotates two vectors as "invalid, but accepted" — confirming permissive parser
  semantics. In this fork, those inputs must be adjudicated at the shared `ProxySecret::from_link(...)`
  boundary, then regression-tested through LinkManager, API proxy creation, and serialized proxy
  loading. Upstream comments do not settle local semantics.
- **Required adversarial harvest action (before Wave 1 completes):**
  1. Add the four new URL-encoded base64 test vectors from `a82128ab8` to the fork's proxy-secret
     adversarial test suite.
  2. Add the two "invalid, but accepted" vectors from `bfab03f7a` as explicit negative or
     documented-acceptance adversarial tests.
  3. Verify the fork's `ProxySecret::from_link(...)` behavior for all four encoding forms
     (standard base64, URL-safe base64, percent-encoded `+`/`/`, and URL-safe) produces the
     same canonical output and does not silently accept any form that should be rejected.
  4. Test that doubly-encoded, mixed-encoding, and out-of-spec vectors (e.g., padding `==` in
     URL-safe context) are correctly rejected without silent normalization.

### 10.2.3 Proxy Metadata/API Lane: `691cb6a77`

- **Class:** Defer.
- **Reason:** the only `td/telegram/net` hit in the backlog, but the diff is purely user-comment
  metadata (string field + DB key). Not transport hardening.
- **Note for future evaluation:** If the proxy comment API surface is ever considered, the
  `comment` field requires an explicit length bound and boundary validation tests. The current
  upstream code shows no such bound (see Section 2.5.4).

### 10.2.4 Link-Parser Feature Lane: `8921c22f0`, `dd78f94a8`, `990b821c8`

- **Class:** Defer / Out of mission scope.
- **Reason:** these path hits target task/poll and text-composition/managed-bot deep-link behavior,
  not proxy/TLS/QUIC or parser hardening on mission surfaces.

### 10.2.5 Config/Title-Noise Lane: `6b82cc832`

- **Class:** Ignore.
- **Reason:** despite the title "Encode key with base64", the actual change is `hex_decode()` of a
  hardcoded Firebase API key — binary obfuscation theater (OWASP A02), not base64 encoding, and
  not transport relevant. The fork's `ConfigManager.cpp` is unchanged from baseline. No action.

### 10.2.6 Already-Incorporated Lane: `3d38fb7aa`, plus `bc79a6d2d`

- `3d38fb7aa` (`Remove early initialization of tz_offset.`): **Already incorporated** in fork
  as part of `2b7356173`. No further action.
- `bc79a6d2d` (`Improve test.`): upstream simplification. Fork retains richer version. No action.

### 10.2.7 Compile/Include/Test-Noise Lane: `e86cd4496`, `49b3bcbb6`

- **Class:** Defer / No standalone value.

### 10.2.8 Per-Bundle Wave 1 Execution Protocol

For each research item classified above, in order:

1. Freeze the item in the preflight annex with explicit lane and bundle membership.
2. For Research Input Only items: harvest red-test vectors, fixture deltas, and boundary notes
   without cherry-picking.
3. For the proxy-link evidence bundle: execute the adversarial harvest action defined in §10.2.2
   before any proxy-boundary work begins.
4. For the profile review action from §10.2.1: treat as a separate profile-update task anchored
   to fixture corpus evidence, not a direct backport. Open a dedicated `stealth/darwin-profile-*`
   branch for that work.
5. If a direct-backport candidate survives future analysis, cherry-pick it to an isolated gate
   branch, produce contract snapshot and risk register, write failing tests first (fixture-driven
   and adversarial), apply minimal compliant repair/adaptation, run lint/static/security gates
   and the 14-core matrix, then classify as Accept / Accept with Repair / Reject / Defer.

## 10.3 Wave 2 (Non-Stealth Correctness Core) — Execution Update (2026-05-09)

1. Coarse size remains 4 commits (`a09adfc63`, `386eca6fe`, `1a9ef3d68`, `5340472b0`).
2. Implemented and adapted in this fork:
   - `a09adfc63` (`Fix reply_to_top_id.`) as W2-001, classified `Accept with Repair`.
   - `386eca6fe` + `1a9ef3d68` as W2-002, classified `Accept with Repair`.
3. Deferred from the original Wave 2 decision and later merged separately:
    - `5340472b0` (`Fix possible use after move.`), classified `Defer` for the original W2 cycle and
       later merged to `master` via PR #18 as its own W2-003 lifetime-focused lane. It remains outside
       the completed W2-001/W2-002 semantics decision record.
4. Wave 2 closeout evidence and gate decisions are recorded in
   `docs/Plans/UPSTREAM_WAVE_2_DECISION_2026-05-08.md`.
5. Remaining backlog work in this gating plan now starts from Wave 2B onward; no feature-wave
   activation is allowed without approved preflight and charter inputs. The bounded W3-P slice already
   merged on `master` does not authorize reopening the full Wave 3 capability bundle without a separate
   charter.

## 10.4 Wave 2B (Residual Micro-Correctness Queue)

After Wave 2 core is closed, a second correctness-only queue may be reviewed. This queue is frozen now
so it is not lost inside the large `td/telegram` backlog.

1. Coarse size: 9 commits.
2. Reply and draft semantics queue: `d5714b0b8`, `bcbe2f309`, `562bce098`.
3. Dialog / username error semantics queue: `84d2ea0d8`, `a96365b5f`.
4. Basic-group rights normalization queue: `8fc2344f3`, `9c62782dc`.
5. Guest-message correctness queue: `aeddf8ca3`, `336504954`.
6. Execution rule: one commit or tightly coupled pair per branch. No queue-wide batching.
7. If any candidate turns out to depend on schema/API churn or large feature assumptions, eject it from
   Wave 2B and move it to the relevant later capability wave.
8. Bounded closure evidence for this queue is recorded in
   `docs/Plans/UPSTREAM_WAVE_2B_CLOSURE_NOTE_2026-05-11.md`.

## 10.5 Wave 3 (Poll and Poll-Media Capability Bundle)

This is the largest upstream feature lane in the current backlog and must not be approached as a list of
easy cherry-picks.

1. Coarse size: 70 commits.
2. Dominant files: `td/generate/scheme/td_api.tl`, `td/telegram/MessageContent.cpp`,
   `td/telegram/MessagesManager.cpp`, `td/telegram/PollManager.cpp`, `td/telegram/PollOption.*`,
   `td/telegram/files/FileManager.cpp`, `td/telegram/Requests.*`, `td/telegram/cli.cpp`.
3. Planning class: Defer until there is an explicit product objective for poll/media parity.
4. Cherry-picking individual poll/media commits is forbidden by default because the lane mixes schema,
   storage, upload, quick-reply restrictions, file-reference handling, voter-visibility rules, and
   forwarded-message semantics. A bounded W3-P hardening slice has already landed separately on
   `master` via PR #18, but that does not authorize queue-wide poll/media intake or any folding back
   into Wave 2 work.
5. Any future Wave 3 charter must split the capability into at least these sub-bundles:
   - poll restrictions, voters, and statistics
   - poll local storage and option/state normalization
   - poll media input, upload, file-reference, and render paths
6. Required RED-phase test families include contract tests for message-content shape, negative tests for
   invalid media/file references, adversarial tests for forwarded and quick-reply contexts, light fuzz
   for poll-media payload parsing, and stress tests for attachment-heavy poll flows.
7. Poll/media is intentionally sequenced after correctness waves because it has the largest message-content
   blast radius in the whole backlog.

### 10.5.1 Critical Intake Assessment (2026-05-11)

This section records a commit-level decision for the most security-relevant Wave 3 upstream changes.
It is intentionally strict: Wave 3 is not complete in this fork and must not be treated as implicitly approved.

1. **Already imported and adapted (bounded W3-P hardening lane):**
   - `21275249c` (hide recent voters when unavailable)
   - `c81e6da9f` (disallow polls with media in quick replies)
   - `3e78ebcd8` (forwarded poll statistics path)
   - Local strengthening beyond upstream is already present and required to keep:
     fail-closed media helper usage in `MessageContent`/`QuickReplyManager`, and fail-closed
     unknown-poll handling in `PollManager` input-media accessors.

2. **Reject for direct import (feature/capability expansion with high blast radius):**
   - `b2fd2e468` (`inputMessagePoll.media`)
   - `8ff835b82` (`inputPollTypeQuiz.explanation_media`)
   - `410f57214` (`inputPollOption.media`)
   - `bf4bfcf8e` (multiple media upload in polls)
   - `d59ef0167`, `65b967524`, `8104480e0`, `50d50976f`
   Reason: these commits expand schema, input-media, upload, file-reference, and runtime message-content
   surfaces together. In this fork, direct import would materially increase attack surface without a
   dedicated capability charter and fail-closed redesign.

3. **Defer (charter-gated, not rejected forever):**
   - statistics/restrictions/unread-state lane, including `7d56f9c58`, `f654c5c81`, `bcd2c683c`,
     `1eaf2481e`, `d6ef00fa9`, `084707e99`, `bb6574d9f`, `b00c67763`, `0b9e9829b`,
     `d51464eb2`, `c6411b9c9`, `dc470c164`, `02473d316`, `aaea672ae`
   Reason: these change API shape, permission semantics, update behavior, and user-visible poll policy.
   They are capability work, not isolated hardening.

4. **Assessment conclusion:**
   - Wave 3 remains **partially implemented** in this fork.
   - The merged subset is a defensive lane, not feature parity with upstream Wave 3.
   - Any claim that Wave 3 is complete is incorrect unless a dedicated charter executes the deferred
     capability bundles through full TDD and security gates.

### 10.5.2 Adaptation Rules For Any Future Wave 3 Intake

If a deferred Wave 3 commit is reconsidered, adaptation must follow these rules (direct cherry-pick is forbidden):

1. Preserve fail-closed behavior as the first invariant:
   - missing/unknown poll state must deny operation and never trigger crash-oriented control flow;
   - runtime media detection must use central helper seams rather than ad-hoc file-id scans at call sites.
2. Keep schema and runtime changes decoupled:
   - no `td_api.tl` expansion may be merged in the same branch as storage/upload/runtime rewires;
   - each sub-bundle must have its own contract and risk register before code changes.
3. Require C++23 adaptation for touched seams:
   - no C++14/17-style carry-over where modern safer alternatives already exist in this codebase.
4. Deny-by-default for new policy states:
   - ambiguous voter/statistics/eligibility states must be blocked, not permissively inferred.

### 10.5.3 Mandatory Test Matrix For Wave 3 Adaptation Branches

For every accepted Wave 3 adaptation branch, tests must be written first and include all categories below:

1. **Contract tests:**
   - source-level seam pinning for quick-reply/media/send/forward statistics gates.
2. **Adversarial tests:**
   - malformed or stale poll IDs, missing manager state, inconsistent persisted poll payloads,
     and forwarded/imported edge semantics.
3. **Integration tests:**
   - runtime parse/store boundaries and cross-manager guard enforcement.
4. **Light fuzz tests:**
   - randomized probe coverage for guard invariants and forbidden legacy paths.
5. **Stress tests:**
   - repeated source/runtime invariance checks for fail-closed behavior under sustained load.

At minimum, adapted branches must keep the existing Wave 3 suite families green and extend them when
new seam risk is introduced: `quick_reply_poll_media_*`, `poll_media_send_guard_*`,
`forwarded_poll_statistics_*`, and `poll_state_runtime_*`.

### 10.5.4 Published Capability Charter

Wave 3 now has a dedicated capability charter:

`docs/Plans/UPSTREAM_WAVE_3_CHARTER_2026-05-11.md`

The charter freezes exact commit membership split into sub-bundles, schema/runtime ownership tables,
and per-bundle kill-switch criteria. Wave 3 remains deferred until explicit product-objective approval
and a Wave 3 preflight annex activate one bounded sub-bundle at a time.

### 10.5.5 Published Preflight Annex (W3-B2)

The first bounded Wave 3 preflight annex is published for W3-B2 policy/voter/statistics/unread scope:

`docs/Plans/UPSTREAM_WAVE_3_PREFLIGHT_2026-05-11.md`

This annex freezes exact W3-B2 row membership, contract snapshot boundaries, risk register, and
RED test IDs. Execution remains blocked until product objective approval.

## 10.6 Wave 4 (Guest-Bot and Personal-Chat Capability Bundle)

1. Coarse size: 8 commits.
2. Dominant files: `td/generate/scheme/td_api.tl`, `td/telegram/UpdatesManager.*`,
   `td/telegram/MessagesManager.cpp`.
3. Planning class: Defer until there is an explicit product objective for guest-bot support.
4. Bundle rule: review guest-query update delivery, identifier validation, personal-chat retrieval,
   rating updates, and message-directionality semantics together.
5. Required adversarial focus: spoofed or replayed guest query identifiers, out-of-order update delivery,
   cross-chat reply confusion, and incorrect outgoing/incoming classification.
6. The small commit count does not make this a small semantic change; it changes update and message
   state semantics and therefore remains outside hardening waves.

### 10.6.1 Published Preflight Annex (W4-G)

The bounded Wave 4 preflight annex is published for W4-G guest-bot capability scope:

`docs/Plans/UPSTREAM_WAVE_4_PREFLIGHT_2026-05-14.md`

This annex freezes exact W4-G row membership, contract boundaries, risk register, and the verified
repository-resident W4 RED/Survive suite, including malformed guest-result runtime coverage. Execution
remains blocked until product objective approval.

## 10.7 Wave 5 (AI Compose and Text-Composition Style Capability Bundle)

1. Coarse size: 22 commits.
2. Dominant files: `td/generate/scheme/td_api.tl`, `td/telegram/TranslationManager.*`,
   `td/telegram/AiComposeTone.*`, `td/telegram/Requests.*`, `td/telegram/cli.cpp`,
   `td/telegram/ConfigManager.cpp`.
3. Planning class: Defer until there is an explicit product objective for text-composition features.
4. Bundle rule: create/edit/delete/search/example/update flows must be reviewed together; direct
   cherry-picks of isolated API methods are forbidden.
5. Required risk surfaces: config-bound enforcement, update ordering, stale-tone reload behavior,
   persistence of user-loaded style metadata, and request/response error semantics.
6. Because this lane changes both product surface and configuration semantics, it must not be mixed with
   correctness or transport branches.

## 10.8 Wave 6 (Managed-Bot Access Capability Bundle)

1. Coarse size: 4 commits.
2. Dominant files: `td/generate/scheme/td_api.tl`, `td/telegram/BotAccessSettings.*`,
   `td/telegram/Requests.*`.
3. Planning class: Defer until an access-control objective and least-privilege model are documented.
4. Despite the small commit count, this lane is security-sensitive. It changes bot access settings and
   management flows, so it cannot enter the tree without an explicit permission contract and OWASP V2/V3
   review notes.
5. If activated later, the wave must include contract tests for access settings, adversarial tests for
   unauthorized or contradictory state transitions, and persistence/load checks.
6. Deny-by-default is mandatory: every unspecified transition or ambiguous caller path is rejected unless
   the approved security charter explicitly allows it.
7. Any sign of ambient privilege inheritance, stale access replay, or unclear revocation semantics is a
   wave-level kill switch rather than a bug-fix follow-up.

## 10.9 Wave 7 (Tooling, Examples, and Documentation)

1. Coarse size: 3 commits.
2. Contents: example updates, build reproducibility tweaks, wrapper docs, typos, version bump.
3. Planning class: Defer and keep independent from runtime waves.
4. This lane has the lowest mission priority and should never be used as justification to widen a runtime
   review branch.

## 10.10 Wave 8 (Residual Mixed Queue Split)

Wave 8 is planning-only until the residual mixed queue is split. It is not a place to execute
miscellaneous cherry-picks opportunistically.

1. Source lane: `W8-X` from Section 2.5.8.
2. Coarse size: 69 commits.
3. Required outcome: exact reassignment of every residual commit into `W8-P`, `W8-S`, `W8-C`, `W8-A`,
   or an existing Wave 2B / Wave 3-6 bundle.
4. Default class: Defer. No direct cherry-pick candidate may be opened directly from `W8-X`.
5. Commits migrated into `W8-A` are blocked from execution until the corresponding capability wave
   charter exists.
6. Commits migrated into `W8-P`, `W8-S`, or `W8-C` may be reviewed only one bounded commit or tightly
   coupled pair at a time, after the split document is approved.
7. Wave 8 exists to remove ambiguity from the backlog, not to accelerate intake.

## 10.11 Whole-Backlog Planning Order (What Happens Before Any Cherry-Pick)

1. Maintain the canonical full Pass A manifest `UPSTREAM_BACKPORT_MANIFEST_2026-05-08.md` as the
   single source of truth and enrich rows whenever lane/class/bundle assignments change.
2. Publish `UPSTREAM_WAVE_8_SPLIT_2026-05-08.md` so the residual mixed queue stops being a hidden
   source of opportunistic cherry-picks.
3. Publish a Wave 1 preflight annex (`UPSTREAM_WAVE_1_PREFLIGHT_2026-05-08.md`) that references the
   canonical manifest rows, freezes the corrected Wave 1 lane/bundle membership below, records
   evidence sources (cipher suite fixtures, exact comparison sequence), defect register items
   UPD-001 through UPD-004, and planned adversarial test surfaces. The annex must explicitly mark
   all other backlog commits as out of scope for Wave 1.
4. Lane freeze for Wave 1:
   - `28e0d0dbe` + `00eedc5f9`: Research Input Only. Reject direct backport. Record defect UPD-001
     (static-init randomization) and UPD-002 (phantom variant 1) in the annex. Extract only the
     variant 2 cipher suite content as fixture-verified profile research input.
   - `a82128ab8` + `bfab03f7a`: Research Input Only. Harvest all test vectors as adversarial
     inputs at the `ProxySecret::from_link(...)` boundary before Wave 1 closes.
   - `6b82cc832`: Ignore. Defect UPD-003 (hex obfuscation misrepresented as base64).
   - `691cb6a77`: Defer. Defect UPD-004 (unbounded comment field) noted.
   - `3d38fb7aa`: Already incorporated. No action.
   - `bc79a6d2d`: Already superseded by fork's richer test. No action.
   - All other backlog commits: Defer or Ignore for Wave 1.
5. Publish and approve Wave 2 core and Wave 2B micro-correctness annexes before touching any non-stealth
   runtime candidate.
6. Publish deferred capability charters for Waves 3 through 6, each with exact commit membership,
   schema-coupling notes, and kill-switch criteria, before any feature objective is approved.
7. Publish the Wave 6 managed-bot security charter before any managed-bot access discussion is allowed
   to move from planning to implementation.
8. Feature waves are opt-in, not automatic. After hardening waves finish, activate at most one feature
   wave at a time and prefer the smallest clearly justified bundle first.
9. Before any gate branch is created, the Wave 1.5 Darwin/iOS profile verification action (§15)
   must be underway.
10. For proxy-link candidates, treat upstream tests/comments as hostile evidence input at the shared
   `ProxySecret::from_link(...)` boundary, not as approved semantics.
11. Only after the relevant preflight annex is approved may Phase 0 contract capture and red-suite authoring begin.
12. First execution batch remains capped to research harvest or one bounded correctness candidate. No
   feature bundle may be opened as the first execution batch.

---

## 11. Deliverables Per Wave

Each wave must produce:

1. The canonical manifest in `docs/Plans/`: `UPSTREAM_BACKPORT_MANIFEST_<YYYY-MM-DD>.md`.
2. Preflight candidate annex in `docs/Plans/`: `UPSTREAM_WAVE_<N>_PREFLIGHT_<YYYY-MM-DD>.md`.
3. A wave-scoped manifest extract inside that preflight annex, referencing canonical manifest row IDs
   rather than duplicating the full backlog table.
4. For any schema-coupled feature lane, a capability charter in `docs/Plans/`: `UPSTREAM_WAVE_<N>_CHARTER_<YYYY-MM-DD>.md`.
5. For any schema-coupled feature lane, a schema-freeze table listing `td_api.tl`, request/update entry points, owning managers, persistence surfaces, and kill-switch criteria.
6. For the residual mixed queue, a split document in `docs/Plans/`: `UPSTREAM_WAVE_8_SPLIT_<YYYY-MM-DD>.md`.
7. For Wave 6 specifically, a managed-bot security charter with deny-by-default transition rules.
8. Decision report in `docs/Plans/`: `UPSTREAM_WAVE_<N>_DECISION_<YYYY-MM-DD>.md`.
9. Risk register with mapped test IDs.
10. Accepted commits and repaired commits list.
11. Rejected/deferred commits with explicit rationale.
12. Evidence summary: build status, static-analysis/lint delta, targeted suite counts, fixture corpus status, residual risks.

Per-commit annex is mandatory:

1. Upstream hash and description.
2. Mission-fit rationale.
3. Contracts touched.
4. Risk IDs and mapped tests.
5. C++23 adaptation notes.
6. Final class and justification.
7. Whether the commit is a direct backport candidate, a research-only input, or an evidence-bundle-only item.
8. Which real dump / generated fixture assets were consulted.
9. If reviewed as part of a bundle, the full prerequisite/follow-up commit set and why isolated review would be invalid.

---

## 12. Merge Readiness Criteria

A wave is merge-ready only when:

1. All selected commits are classified with evidence.
2. No unresolved HIGH/CRITICAL risk remains without adversarial test coverage.
3. High-risk and transport-relevant gates pass at 14-core validation.
4. C++23 and architecture constraints are satisfied.
5. Fail-closed behavior is verified in adversarial paths.
6. Real-fixture evidence gate is satisfied where transport-sensitive behavior is touched.
7. Modified files have zero new lint/static/security findings.

---

## 13. Out of Scope for This Plan

1. Full 199-commit upstream ingestion in one batch.
2. Bulk feature-parity ingestion without an explicit hardening or product objective, contract
   snapshot, and dedicated adversarial test plan.
3. Test relaxation to fit upstream behavior.
4. Partial cherry-picks of schema-coupled feature lanes (`td_api.tl` plus request/update/runtime changes) without an approved capability charter.

---

## 14. Upstream Code Defect Register (Findings From Full Diff Analysis, 2026-05-08)

This section records confirmed defects found in upstream code during the deep-diff evidence review.
These are NOT test-only concerns. They are code bugs that the upstream developers have not fixed. If
any of these upstream code paths were ever considered for direct cherry-pick, the defect must be
repaired first and verified by adversarial tests before classification as Accept with Repair.

| ID | Upstream Commit(s) | File | Defect | Impact | Required Action Before Any Port |
|---|---|---|---|---|---|
| UPD-001 | `28e0d0dbe`, `00eedc5f9` | `td/mtproto/TlsInit.cpp` | `Op::random_value()` evaluates cipher suite variant selection at C++ static initialization time (process start), not at per-connection time. All connections from the same process emit the same cipher suite. | DPI per-process correlation fingerprint. Reduces effective cipher suite entropy from 2 choices per-connection to 1 label per-process. | Per-connection randomization must replace static-init selection before any cipher-suite randomization feature is ported to this fork. |
| UPD-002 | `28e0d0dbe`, `00eedc5f9` | `td/mtproto/TlsInit.cpp` | Cipher suite variant 1 (`0x1302, 0x1301, 0x1303` order + 13 suites, omitting all RSA-static and 3DES legacy suites) has no matching real Safari fixture in the reviewed corpus (any version, any OS). | Browser plausibility regression: emitting variant 1 would produce a cipher suite fingerprint that no real Safari sends. Increases DPI detection surface. | Variant 1 must be excluded from any profile update or cipher suite change. Only variant 2 (`0x1302, 0x1303, 0x1301` + 20 suites) is fixture-verified. |
| UPD-003 | `6b82cc832` | `td/telegram/ConfigManager.cpp` | Commit title says "Encode key with base64" but actually uses `hex_decode()` — hex obfuscation, not base64. Bypasses string-search tools but keeps a hardcoded Firebase API key. OWASP A02 (Cryptographic Failures / Secret Hardcoding). | Firebase API key remains effectively hardcoded. Obfuscation provides only scraping resistance, no real security improvement. | If the ConfigManager bootstrap path is ever hardened, the key must be loaded from a secret store or environment — not re-encoded in a different format. Separate task. |
| UPD-004 | `691cb6a77` | `td/telegram/net/ConnectionCreator.cpp` | `proxy comment` field in `set_proxy_comment()` has no visible length bound in the upstream code. | Potential unbounded memory/DB allocation for arbitrarily large comment strings. Input boundary gap. | If the proxy comment API surface is adopted, explicit length validation and adversarial length tests are required. |

---

## 15. Darwin/iOS Profile Cipher Suite Verification Action Item (Wave 1.5)

This is a separate research-and-fix action triggered by the Darwin TlsInit analysis. It is NOT a
direct backport of upstream commits. It must happen before any Wave 2 Darwin cipher-suite work.

**Objective:** verify that the fork's `IOS14` and `Safari26_3` profile implementations emit cipher
suite order `0x1302, 0x1303, 0x1301` (iOS 26+ Safari, fixture-verified) rather than the older
`0x1301, 0x1302, 0x1303` (iOS 18 Safari, pre-26).

**Step-by-step process (TDD):**

1. Run the fixture corpus smoke for iOS and macOS:
   ```bash
   python3 test/analysis/run_corpus_smoke.py \
     --registry test/analysis/profiles_validation.json \
     --fixtures-root test/analysis/fixtures/clienthello \
     --server-hello-fixtures-root test/analysis/fixtures/serverhello
   ```
2. Inspect how the `IOS14` and `Safari26_3` profile builder emits cipher suites.
3. Write a red test: `build_proxy_tls_client_hello_for_profile(..., BrowserProfile::IOS14, ...)`
   must emit TLS 1.3 suites in order `0x1302, 0x1303, 0x1301` before other suites (iOS 26+).
   At this point the test should either pass (already correct) or fail (needs a profile fix).
4. If the test fails, update the profile's cipher suite order to match the `safari26_*` fixture
   evidence. This is a profile data correction, not an algorithm change.
5. Add a separate red test for supported_versions: the profile must emit only `{GREASE, TLS1.3, TLS1.2}`.
6. Write adversarial tests verifying no TLS 1.0 / 1.1 versions are present in any Darwin profile.
7. Run 14-core ctest and confirm all TlsHello and SslCtx tests pass.

**Branch prefix:** `stealth/darwin-profile-ios26-cipher-order-fix`

**Gate status:** This action is a prerequisite for any upstream cipher suite research harvest
into the fork's profile system. Must complete before Wave 2 TlsInit research work begins.

---

## 16. Immediate Next Planning Deliverables (Updated After Full-Backlog Lane Review, 2026-05-08)

No implementation starts yet. The immediate next work is planning-only and exists to make later
cherry-picks deterministic instead of opportunistic.

**Action 1 — Canonical full Pass A manifest published; perform row-quality enrichment**

`UPSTREAM_BACKPORT_MANIFEST_2026-05-08.md` is now present and contains all 199 commits. The next
planning step is row-quality enrichment and review discipline:

1. stable row ID
2. hash
3. subject
4. touched paths
5. coarse lane (`W1-T`, `W2-C`, `W2B`, `W3-P`, `W4-G`, `W5-AI`, `W6-M`, `W7-D`, `W8-X`)
6. initial class (`ignore`, `defer`, `deep-review`, `already-incorporated`, `research-input`)
7. bundle ID / prerequisites if isolated review is unsafe
8. owning seam and local divergence note
9. notes about title/path mismatch or schema coupling
10. security tags where applicable
11. pass-B overrides with rationale when a row moves lanes or classes

Pass A snapshot reminder from the canonical manifest:

1. W1-T: 10
2. W2-C: 4
3. W2B: 9
4. W3-P: 70
5. W4-G: 8
6. W5-AI: 22
7. W6-M: 4
8. W7-D: 3
9. W8-X: 69

**Action 2 — Publish the residual mixed queue split**

Create `UPSTREAM_WAVE_8_SPLIT_2026-05-08.md` and reassign every `W8-X` commit into:

1. `W8-P` parse and validation hardening
2. `W8-S` state and persistence repair
3. `W8-C` concurrency, lifetime, and actor-safety
4. `W8-A` API or feature spillover that must migrate to Waves 3 through 6 or be rejected

Each sub-lane needs exact commit membership, one objective, one kill-switch criterion, and one default
test-family emphasis.

**Action 3 — Publish the Wave 1 preflight annex**

`UPSTREAM_WAVE_1_PREFLIGHT_2026-05-08.md` must freeze the transport/parser research lane only,
including:

1. references to the canonical manifest row IDs for the exact Wave 1 commit set
2. defect register entries UPD-001 through UPD-004
3. cipher-suite evidence triangulation records from §5.0.1
4. proxy-link evidence-bundle handling rules
5. explicit out-of-scope marking for all other backlog commits

**Action 4 — Publish the Wave 2 core and Wave 2B annexes**

1. Keep `UPSTREAM_WAVE_2_PREFLIGHT_2026-05-08.md` focused on W2 core only.
2. Publish a separate annex for Wave 2B residual micro-correctness so those commits are not mixed into
   the core shortlist without a fresh contract snapshot.

**Action 5 — Publish deferred capability charters for Waves 3 through 6**

Each charter must include:

1. exact commit membership
2. schema-freeze table
3. owning runtime seams
4. kill-switch criteria
5. required RED/adversarial/fuzz/stress test families
6. explicit statement that the wave is deferred until a product objective is approved
7. for Wave 6 specifically: deny-by-default transition rules, caller inventory, revoked-access
   semantics, and persistence/load normalization requirements

**Action 6 — Hold implementation until a wave is approved**

No gate branch, cherry-pick, or code-adaptation task starts until:

1. the relevant annex or capability charter is approved
2. the scope freeze is explicit
3. the lane is evidence-backed rather than subject-inferred
4. the contract -> attack -> RED plan exists for that wave

