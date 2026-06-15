<!--
SPDX-FileCopyrightText: Copyright 2026 telemt community
SPDX-License-Identifier: MIT
telemt: https://github.com/telemt
telemt: https://t.me/telemtrs
-->

# Wave B Closeout — Minimal Correctness Backports (2026-06-15)

**Parent plan:** [UPSTREAM_BACKPORT_PLAN_2026-06-15.md](UPSTREAM_BACKPORT_PLAN_2026-06-15.md) ·
**Wave A:** [addendum](UPSTREAM_BACKPORT_PLAN_2026-06-15_WAVE_A_ADDENDUM.md)
**Wave:** B — implementation of the recommended minimal subset.
**Baseline:** `upstream-baseline-2026-06-15-a17f87c4cff7` (range `e0943d068ce9..a17f87c4cff7`).
**Discipline:** adversarial TDD — contract tests written first (RED), then minimal fix (GREEN). Full
build / `ctest` / sanitizer matrix run on **Linux CI** (not buildable in the authoring environment).

---

## 1. Landed backports (3)

All three are correctness/fail-closed fixes in code the fork carries; **none touch stealth transport**
(`td/mtproto`, `tdnet`, TlsInit, ClientHello, proxy/QUIC). Each is its own minimal change, no unrelated
edits.

| Upstream SHA | Subject | Fork file | Adaptation |
|---|---|---|---|
| `84f21a1d8` | Fix add_message_content_dependencies for ManagedBotCreated | `td/telegram/MessageContent.cpp` | **exact** — case now resolves `content->bot_user_id` as a dependency. |
| `a74cc9af8` | Ignore draft replies to local messages | `td/telegram/DraftMessage.hpp` | **local-equivalent** — extended the fork's existing guard `(is_valid() \|\| is_valid_scheduled())` to also clear `is_local()` replies (upstream only had `is_valid()`); fork's `is_valid_scheduled()` hardening preserved. |
| `dc73b3ca3` | Fix repair of loaded from database chats | `td/telegram/MessagesManager.cpp` | **local-equivalent** — on failed `resolve_force`, also re-fetch each message from server and reload full dialog info; kept the fork's caller-`source` provenance instead of upstream's hard-coded `"parse_dialog N"` labels. |

### Adaptation rationale
- `84f21a1d8` is a clean exact port: the `MessageManagedBotCreated` type and the
  `add_message_content_dependencies` switch both exist in the fork (W6-M managed-bot surface). Without
  the fix the referenced bot user id is never registered as a dependency → silent fail-open dangling
  reference.
- `a74cc9af8`: the fork had already diverged (added `is_valid_scheduled()`), so an exact cherry-pick
  would have reverted local hardening. The local-equivalent merges both: left side keeps the fork
  superset, right side adds the upstream `is_local()` clear. Fail-closed: ids that do not survive a
  restart are dropped from the persisted draft reply.
- `dc73b3ca3`: applied only the **substantive** behavior (message re-fetch + full dialog reload on
  unresolved dependencies). The upstream cosmetic source-label rewrite (`source` → `"parse_dialog N"`)
  was intentionally **not** taken because it would erase the fork's richer caller-`source` logging
  provenance.

---

## 2. Tests added (contract, RED-first)

Following the fork's established source-contract pattern (`test/stealth/SourceContractFileReader.h`
+ `read_repo_text_file`, as used by `poll_voter_visibility_contract.cpp`). Each test pins the fixed
contract on the working-tree source; it was RED before the corresponding edit and is GREEN after.
Registered in `test/CMakeLists.txt` (`TD_TEST_SOURCE`).

| Test file | Suite | Pins |
|---|---|---|
| `test/managed_bot_created_dependency_contract.cpp` | `ManagedBotCreatedDependencyContract` | ManagedBotCreated case adds `bot_user_id` dependency |
| `test/draft_local_reply_ignore_contract.cpp` | `DraftLocalReplyIgnoreContract` | draft parse clears same-chat local **and** yet-unsent replies; fork's `is_valid_scheduled` guard intact |
| `test/parse_dialog_repair_refetch_contract.cpp` | `ParseDialogRepairRefetchContract` | failed force-resolve triggers per-message refetch + dialog re-query + full-info reload |

**Author-environment self-verification (not a substitute for CI):** the exact `extract_region` +
`normalize_for_contract` logic of each test was replayed against the post-edit source files — all
needles present (RED→GREEN confirmed); all region markers resolve uniquely.

---

## 3. Verification status

- ✅ Edits localized to confirmed-present symbols; idioms match the surrounding switch/guard code.
- ✅ All three files already carry the dual SPDX header (BSL-1.0 AND MIT) required for modified
  Boost-derived files — no header changes needed.
- ✅ Contract-test needles verified against real post-edit source in the sandbox.
- ⏳ **Pending on Linux CI** (per repo policy / AGENTS.md):
  ```bash
  cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DTD_ENABLE_BENCHMARKS=OFF -DTDLIB_STEALTH_SHAPING=ON
  cmake --build build --target run_all_tests --parallel 4
  ctest --test-dir build --output-on-failure
  ./build/test/run_all_tests --filter ManagedBotCreatedDependencyContract
  ./build/test/run_all_tests --filter DraftLocalReplyIgnoreContract
  ./build/test/run_all_tests --filter ParseDialogRepairRefetchContract
  python3 tools/ci/run_sanitizer_matrix.py
  ```
- Indexer noise observed during editing (`-Wunused-function secret_to_telegram`, clangd
  `unused-includes` in `MessageContent.cpp` / `DraftMessage.hpp` / `MessagesManager.cpp`) is
  **pre-existing**, unrelated to these edits, and consistent with the indexer-noise note in gating
  §0.3.13. No new warning is introduced by the changed lines.

---

## 4. Explicitly NOT done (recorded)

- The 207 product-epic commits (rich-message / instant-view / WebBrowser / PollMedia / chat-join /
  live-location / search-filter / business-bot) remain `defer_pending_context` — mission-misfit, same
  posture as W11-AI2.
- The prereq-coupled poll & instant-view fixes (`fe5ab8d49`, `3f66d431a`, `e80270f4c`, `44db71825`,
  `2fe415631`, `09b9c2491`, `9dfaec306`) remain deferred (target symbols absent in the fork). Not
  cherry-picked.
- The nullable-schema cluster (`108b33f15`, `6817f61b9`, `39d166296`, `8df09c1ba`) remains deferred
  pending an API/Doxygen-pipeline decision (public contract change).
- `4e59e82d0` ("Fix includes") dropped — fork include graph differs; no value.
- Layer-227 bump (`5aec5b120`) deferred — transport-adjacent, large blast radius.
- **NOTICED BUT NOT TOUCHING — requires separate task:** the *defensive idea* behind `3f66d431a` /
  `fe5ab8d49` (media-array type-mismatch guard + fail-closed on invalid media) could be re-expressed
  as a local-equivalent hardening of the fork's existing paid-media merge path, independent of the
  deferred PollMedia feature.

---

## 5. Status

Wave B implementation complete in the working tree (3 backports + 3 contract tests + CMake
registration). Awaiting Linux CI green to mark the 2026-06-15 intake cycle's active queue closed.
W11-AI2 remains open-deferred (unchanged).
