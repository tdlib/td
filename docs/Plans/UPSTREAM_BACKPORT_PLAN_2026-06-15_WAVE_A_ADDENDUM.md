<!--
SPDX-FileCopyrightText: Copyright 2026 telemt community
SPDX-License-Identifier: MIT
telemt: https://github.com/telemt
telemt: https://t.me/telemtrs
-->

# Wave A Addendum — Post-Baseline Upstream Delta Inventory (2026-06-15)

**Parent plan:** [UPSTREAM_BACKPORT_PLAN_2026-06-15.md](UPSTREAM_BACKPORT_PLAN_2026-06-15.md)
**Wave:** A — Provenance & Fetch Gate (evidence-only; **no production code modified**)
**Date executed:** 2026-06-15
**Status:** Wave A COMPLETE. Awaiting maintainer approval before Wave B (implementation).

---

## 1. What ran

The parent plan's §2.4 fetch gate was executed live (network was available this run; the plan had
assumed CI-only). Concretely:

1. `git remote add upstream https://github.com/tdlib/td.git` — wired (was absent).
2. `git fetch --depth=400 upstream master` — baseline object `e0943d068ce9` materialized; it is an
   ancestor of the new tip.
3. Minted two **immutable** annotated baseline tags (policy-compliant; never moved):
   - `upstream-baseline-2026-05-24-e0943d068ce9` → `e0943d068ce90b5010f1aea946e6901e25b43bf6`
     (carried-forward lower bound).
   - `upstream-baseline-2026-06-15-a17f87c4cff7` → `a17f87c4cff7b90b278d12b91ba0614383aaee82`
     (new upstream tip = lower bound for the next cycle).
4. Enumerated and classified the full delta.

### Provenance (resolved)

| Field | Value |
|---|---|
| Upstream remote | `https://github.com/tdlib/td.git` (now wired) |
| Lower bound (baseline) | `e0943d068ce90b5010f1aea946e6901e25b43bf6` (tdlib 1.8.64) |
| Upper bound (new tip) | `a17f87c4cff7b90b278d12b91ba0614383aaee82` |
| Comparison range | `e0943d068ce9..upstream/master` (= `..a17f87c4cff7`) |
| Commit count (no-merges) | **247** |
| Upstream date span | 2026-05-19 → 2026-06-13 |
| Downstream HEAD | `e91e98f9` (`feat/adaptive-runtime-profile-rotation`) |
| New baseline tag | `upstream-baseline-2026-06-15-a17f87c4cff7` |

---

## 2. Methodology (this delta)

- **Enumerate:** `git log --no-merges e0943d068ce9..upstream/master` with per-commit file lists,
  parsed in the context-mode sandbox (raw log never entered the working context).
- **Cluster:** by touched subsystem + subject verb + feature epic.
- **Status:** every commit is *ahead* of the fork baseline → baseline downstream status is
  **`missing`** for all 247 (none are fork ancestors). The decision is therefore the **gate**, not
  the status.
- **Semantic / applicability detection (the real work):** for every correctness/security candidate,
  the touched file's presence in the fork was checked, and the **specific touched symbol / prereq
  feature** was verified in the fork tree. This caught several "attractive" fixes that are in fact
  **coupled to deferred upstream feature epics** and would not apply cleanly (see §5).
- **Tools:** `git`, context-mode sandbox (`ctx_execute`), targeted `grep` over the fork tree. No
  build, no edits.

---

## 3. Headline findings

1. **Zero of the 247 commits touch the stealth transport core** (`td/mtproto/*`, `tdnet/*`,
   TlsInit, ClientHello, proxy/QUIC). Verified by subsystem tally (0) and per-candidate `--stat`
   grep (0). Stealth/DPI regression risk for the entire delta is therefore **structurally low**.
   The one transport-*adjacent* item is the **MTProto layer-227 bump** (`5aec5b120`), which is
   deferred (huge `telegram_api.tl` blast radius) but flagged below because layer is observable in
   `initConnection`.
2. The delta is overwhelmingly **new product/feature expansion**: a large **rich-message /
   instant-view (RichText / PageBlock)** epic, a new **WebBrowser settings** subsystem (files absent
   in the fork), a **PollMedia-link** epic, **chat-join / guard-bot** features, **live-location**
   separation, **search type-filter**, and **business-bot/session** work. These are mission-misfit
   for a stealth proxy fork → `defer_pending_context` (same posture as W11-AI2).
3. After prereq verification, **only ~3 small correctness fixes are cleanly applicable**, none
   stealth-sensitive, all LOW–MED. Two initially-attractive **poll fixes are prereq-coupled to the
   deferred PollMedia epic and must NOT be naively cherry-picked.**

### Final gate tally (247)

| Gate | Count |
|---|---:|
| `defer_pending_context` | 207 |
| `reject_not_relevant` | 28 |
| `accept_with_repair` (clean Wave-B) | 8 |
| `local_equivalent_adaptation` (API-sensitive) | 4 |
| **Total** | **247** |

Downstream status for all 247 = **`missing`** (none present downstream); the table above is the
*gating* decision over those missing commits.

---

## 4. Wave B candidate queue (cleanly applicable — recommended subset)

Risk rubric (0–5): Sec · Corr · Fork · Conflict · API · Test · **Stealth**.

| SHA | Subject | Sec | Corr | Fork | Conf | API | Test | Stealth | Gate | Note (prereq verified) |
|---|---|:-:|:-:|:-:|:-:|:-:|:-:|:-:|---|---|
| `84f21a1d8` | Fix add_message_content_dependencies for ManagedBotCreated | 1 | 3 | 3 | 1 | 0 | 4 | 0 | accept_with_repair | `MessageManagedBotCreated` present (MessageContent.cpp:1737); W6-M relevant. **TOP pick.** |
| `dc73b3ca3` | Fix repair of loaded from database chats | 1 | 3 | 2 | 3 | 0 | 3 | 0 | accept_with_repair | repair blocks present (`send_get_dialog_query`, `reload_dialog_info_full`); moderate blast radius — align to fork parse path. |
| `a74cc9af8` | Ignore draft replies to local messages | 1 | 2 | 2 | 2 | 0 | 4 | 0 | local_equivalent_adaptation | fork guard already diverged (DraftMessage.hpp:76 adds `is_valid_scheduled`); add `\|\| is_local()`. |
| `e95e1fd0d` | Fix DialogAction comparison | 0 | 2 | 2 | 1 | 0 | 3 | 0 | accept_with_repair | DialogAction.h present; small correctness. Optional. |
| `39ea84dff` | Fix option name | 0 | 2 | 1 | 1 | 1 | 2 | 0 | accept_with_repair | DialogActionManager.cpp present. Optional, verify option string. |
| `1a8d24176` | Repair video properties from alternative videos | 0 | 2 | 1 | 2 | 0 | 3 | 0 | accept_with_repair | VideosManager present. Optional. |
| `d78ceefc7` | Don't warn about invalid entities in old checklist tasks | 0 | 1 | 1 | 2 | 0 | 2 | 0 | accept_with_repair | ToDoItem/ToDoList present; logging-noise reduction. Low. |
| `4e59e82d0` | Fix includes | 0 | 1 | 0 | 3 | 0 | 1 | 0 | accept_with_repair | include hygiene; fork include graph differs → likely drop / low value. |

### API-sensitive adaptations (nullable schema — route via Doxygen/API pipeline)

| SHA | Subject | API risk | Gate | Note |
|---|---|:-:|---|---|
| `108b33f15` | Make pageBlockTable.caption nullable | 4 | local_equivalent_adaptation | `pageBlockTable`/`pageBlockCaption` exist in fork schema (td_api.tl:3842/3876); null-safety hardening but public contract change. |
| `6817f61b9` | Make credit and table caption nullable | 4 | local_equivalent_adaptation | same |
| `39d166296` | Make pageBlockCaption.credit nullable | 4 | local_equivalent_adaptation | same |
| `8df09c1ba` | Make pageBlockCaption nullable | 4 | local_equivalent_adaptation | also touches WebPagesManager.cpp |

**Recommended minimal Wave B (high-confidence, low-risk):** `84f21a1d8`, `a74cc9af8`, `dc73b3ca3`
(in that priority order). `e95e1fd0d`, `39ea84dff`, `1a8d24176`, `d78ceefc7` are optional low-value.
`4e59e82d0` (includes) is recommended **drop**. The nullable cluster is **defer-or-adapt** pending an
API-pipeline decision.

---

## 5. Prereq-coupled fixes — DEMOTED to defer (do NOT cherry-pick)

These looked like clean correctness fixes but target symbols/features **absent** in the fork
(verified), so they are coupled to deferred upstream epics:

| SHA | Subject | Evidence of coupling |
|---|---|---|
| `fe5ab8d49` | Return no media for invalid poll media | fork has **0** `pollMediaLink`/`PollMedia`/`get_poll_media_object` (code + td_api.tl); belongs to deferred PollMedia epic |
| `3f66d431a` | Fix merge_message_contents for polls | fork merge path is **paid-media** (`old_->media`), not the new **poll-media** path this fix targets |
| `e80270f4c` | Fix get_input_page_table_cell | `get_input_page_table_cell` absent in fork WebPageBlock.cpp (rich-text refactor symbol) |
| `44db71825` | Fix trim_first | `trim_first` absent in fork WebPageBlock.cpp |
| `2fe415631` | Don't send automatic rich text to the server | depends on rich-text refactor (`for_each_rich_text` etc. = 0 in fork) |
| `09b9c2491` | Fix logging | WebPageBlock logging path tied to refactored code |
| `9dfaec306` | Ignore anchors for shared media | depends on upstream "anchor handling" epic (`7445746de`) |

> **Optional follow-up (not Wave B):** the *defensive idea* in `3f66d431a` (type-mismatch guard on
> media-array merge) and `fe5ab8d49` (fail-closed on invalid media) could be re-expressed as a
> **local-equivalent hardening** of the fork's existing paid-media merge path, independent of the
> PollMedia feature. Record as a separate optional task, not an upstream cherry-pick.

---

## 6. reject_not_relevant (28) — recorded, not reimplemented

Doc/typo-only (12): `a17f87c4c`, `091776347`, `062f26052`, `d6debbb2a`, `3f9f03c40`, `5d11c72e3`,
`82607eeb1`, `1f55d8096`, `e65c04fd1`, `3e17b9042`, `24ede94a8`, `bed709341`.
Build/CI-only, fork has own build (2): `c0757dd09` (Revert iOS CMAKE_MAKE_PROGRAM), `9ae32a391`
(unset CMAKE_FIND_ROOT_PATH_MODE_PROGRAM).
Touch only files **absent** in the fork (14): `6d0824e37`, `9f0f70ff2`, `ca9098cf2`, `b167a2230`,
`e0f07c5d8`, `63eb20eec`, `a3349d3c8`, `69a62cb28`, `e1142ec7a`, `c88895fe6`, `fa5adaeea`,
`9903855b9` (WebBrowserManager / RichMessage / WebDomainException / FormattedDate / JoinChatBotResult
/ WebBrowserSettings).

---

## 7. defer_pending_context (207) — by epic (mission-misfit / owner-class expansion)

| Epic / theme | Count | Representative SHAs |
|---|---:|---|
| Rich-message epic (RichText/RichMessage) | 45 | `d17125721`, `2ded57e38`, `1a3dcfe41`, `d44b6cab4` |
| Instant-view / PageBlock epic | 25 | `4f3676d76`, `aa2d3178b`, `1a36c8649`, `f65a89d6d` |
| Chat-join / guard-bot epic | 15 | `434ef4b47`, `b449204c5`, `b64291cda`, `1f10692ff` |
| Poll-media-link epic | 15 | `a2b0e37ad`, `984e1f2b4`, `d02fe8bbe`, `fe5ab8d49`* |
| WebBrowser settings subsystem | 11 | `63196bac4`, `2bce3ecbe`, `533ca510b`, `37fa274f2` |
| Live-location separation | 9 | `680230746`, `b07e4b77e`, `fa95018d4`, `ff3f4d4d4` |
| Business-bot / session | 6 | `f17f5507e`, `855da9437`, `9b4bbc644`, `40e24dcb4` |
| Search type-filter | 6 | `26d8c6a81`, `316e93443`, `e58779017`, `9ec11ab28` |
| MTProto layer 227 bump | 1 | `5aec5b120` ⚠ observable in `initConnection` — see §3 |
| Misc refactor / util / other | 74 | `c42606c72`, `19801099a`, `4ae93d66b`, `0160f50b5` |

\* prereq-coupled fixes from §5 are grouped here by theme.

The full per-commit list is reproducible deterministically from the tagged range:
`git log --oneline upstream-baseline-2026-05-24-e0943d068ce9..upstream-baseline-2026-06-15-a17f87c4cff7`.

---

## 8. Stealth / DPI assessment (mandatory for this fork)

- **Transport core untouched:** 0 commits in `td/mtproto`, `tdnet`, TlsInit, ClientHello, proxy,
  QUIC. No wire-shape, packet-size, timing, or fingerprint surface is affected by any candidate.
- **Layer-227 bump (`5aec5b120`, deferred):** the MTProto API layer is sent in `initConnection` and
  is weakly observable. Staying on an old layer can itself be a mild fingerprint vs. current
  official clients; conversely, bumping it is a large `telegram_api.tl` change with broad blast
  radius. **Decision: defer**, and treat any future layer bump as a transport-adjacent change
  requiring the TlsHello/stealth slice + fixture review (AGENTS.md), not a routine schema update.
- **No candidate in the recommended Wave B alters observable network behavior.** Stealth score = 0
  across the recommended set.

---

## 9. Per-candidate verification plan (Wave B, on approval)

Adversarial TDD first (gating §5.2–§5.9; `.github/instructions/TDD_approach.instructions.md`). For
each recommended candidate, before editing:

- `84f21a1d8` — contract test: a `ManagedBotCreated` message content resolves its `bot_user_id`
  dependency (RED: dependency missing before fix). Negative: invalid/zero bot id stays fail-closed.
- `a74cc9af8` — contract test: draft reply-to a **local** message id is ignored like a yet-unsent id;
  boundary: valid/scheduled/local/server ids. Preserve the fork's existing `is_valid_scheduled`
  branch.
- `dc73b3ca3` — contract test for DB-loaded dialog repair when `resolve_force` fails: messages are
  re-fetched and dialog info reloaded; verify source-label propagation; stress: many unresolved
  dialogs. Adversarial: malformed/partial DB state stays fail-closed.

Build/test lanes (Linux CI):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DTD_ENABLE_BENCHMARKS=OFF -DTDLIB_STEALTH_SHAPING=ON
cmake --build build --target run_all_tests --parallel 4
ctest --test-dir build --output-on-failure
./build/test/run_all_tests --filter TlsHello     # sanity only (no transport candidate, but keep the guard)
python3 tools/ci/run_sanitizer_matrix.py
```

Each candidate is its own branch + single patch; no mixing (gating §1).

---

## 10. Required record updates (on Wave B approval)

- `CHANGELOG.md` Fork Backport Record: add a 2026-06-15 intake-cycle entry naming both baseline tags,
  the range, the 247-count, and the gate tally (entry drafted in this cycle).
- `UPSTREAM_BACKPORT_MANIFEST_2026-05-08.md`: open a successor manifest section (or new dated
  manifest) carrying the 247 rows with their gate decisions and this addendum as the evidence anchor.
- Keep W11-AI2 status unchanged (still open-deferred).

---

## 11. Approval gate (Wave A → Wave B)

Wave A is complete and evidence-backed. **No production code was modified.**

Requested decisions before Wave B:
1. Approve the **minimal Wave B** (`84f21a1d8`, `a74cc9af8`, `dc73b3ca3`) as TDD-first single-patch
   backports.
2. Confirm **drop** of `4e59e82d0` (includes) and **defer** of the nullable-schema cluster pending an
   API-pipeline decision.
3. Confirm the **prereq-coupled poll/instant-view fixes stay deferred** (no naive cherry-pick).
4. Confirm the **layer-227 bump stays deferred** and is treated as transport-adjacent if revisited.
5. Confirm the 207 product-epic commits remain `defer_pending_context` (mission-misfit), consistent
   with the W11-AI2 posture.
