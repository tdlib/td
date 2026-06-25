<!--
SPDX-FileCopyrightText: Copyright 2026 telemt community
SPDX-License-Identifier: MIT
telemt: https://github.com/telemt
telemt: https://t.me/telemtrs
-->

# Upstream Backport — Phase 1 Closeout (2026-06-16)

**Cycle:** 2026-06-15 intake (`e0943d068ce9..a17f87c4cff7`, 247-commit delta)
**Branch:** `feat/upstream-backport-bulk` (off `feat/adaptive-runtime-profile-rotation`)
**Approach:** two-phase — Phase 1 lands clean/tractable commits (build green); Phase 2 integrates the
complex feature epics one at a time.

---

## 1. Phase 1 result

| | |
|---|---|
| Upstream commits landed | **~101** cherry-picks + **4** local build-fixes = **105 commits** |
| Build state | **GREEN** — full `tdcore` compiles on Linux (Docker Ubuntu 24.04, gcc-13, libstdc++) |
| Deferred to Phase 2 | **145** (categorized below; logged in `UPSTREAM_BACKPORT_PHASE2_DEFERRED_2026-06-16.txt`) |
| Pushed | `origin/feat/upstream-backport-bulk` |

Phase 1 landed: layer-227 MTProto schema bump, the instant-view/PageBlock type additions, managed-bot
+ business + chat-join + webbrowser *type* definitions, plus correctness/refactor commits in
stable areas. Every batch was compile-verified before continuing.

## 2. Build environment (reproducible)

Native macOS build is **not** possible (fork's `logging.cpp` uses `std::atomic<std::shared_ptr<>>`,
unsupported by Apple libc++). Linux is required. Used a persistent Docker container:

```bash
docker run -d --name tdlib-build -v <repo>:/src -w /src ubuntu:24.04 sleep infinity
docker exec tdlib-build apt-get install -y build-essential cmake ninja-build gperf zlib1g-dev libssl-dev ccache
cmake -S /src -B /build-linux -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTDLIB_STEALTH_SHAPING=ON \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
cmake --build /build-linux --target tdcore --parallel 3 -- -k 0
```

## 3. Critical learnings (reusable for Phase 2)

1. **TL codegen doubling — clean-regen fix.** Any commit changing a `*.tl` schema, built
   incrementally, produced a *doubled* generated `td_api.h`/`telegram_api.h` (every class redefined →
   100k+ errors). Root cause: stale generated `td/generate/auto/` outputs. **Fix:** delete the
   generated headers/split-cpp/`*.tlo` before rebuilding whenever a `.tl` changed. This unblocked
   layer-227 (initially mis-diagnosed as unmergeable).
2. **Stealth-file protection — `-Xno-renames`.** git rename-detection mis-matched upstream
   `WebBrowserManager.cpp` content onto the fork's stealth files (`td/mtproto/ConfigWindowTable.h`,
   `SessionBlendTable.h`). **Fix:** all cherry-picks use `git cherry-pick -Xno-renames` so new files
   are added as new files, never merged into stealth files.
3. **Memory.** Parallel compiles OOM the 7.75 GB Docker VM on giant TUs (`MessagesManager.cpp`); use
   `--parallel 3`.
4. **Conflict-resolution principle:** always preserve the fork's fail-closed hardening (W3-P voter
   visibility, BotAccessSettings validation, RestrictedRights auth guards). Take upstream only where
   provably equivalent; keep HEAD where the fork is stricter. The Linux build + a HIGH-severity
   security-review catch (a fail-open `RestrictedRights` bug from an early scripted resolution)
   validated this discipline.

## 4. Deferred to Phase 2 (145, categorized)

These are real feature epics that clash with the fork's heavy customizations or are coupled
multi-commit features. They are **not lost** — each is in
`UPSTREAM_BACKPORT_PHASE2_DEFERRED_2026-06-16.txt` with its SHA, subject, and reason.

| Epic / category | ~count | Why deferred |
|---|---:|---|
| **rich-message** (RichMessage, messageRichMessage, inputRichMessage, input* file types, draft content) | ~53 | new feature epic; entangled with deferred PollMedia via `merge_message_contents`; needs whole-epic integration |
| **poll-media / W3-P** (PollMedia, pollMediaLink, poll web pages, Poll.can_see_results) | ~30 | clashes with fork's custom poll schema (PollVoteRestrictionReason, members_only, country_codes, fail-closed voter visibility) — high risk to W3-P hardening |
| **WebPageBlock rich-text refactor** (for_each_rich_text recurse_text, RichText helpers) | ~16 | fork's WebPageBlock diverged in member names (`blocks` vs `page_blocks` per class) + `.h` decl mismatches |
| **search type-filter** (searchChats/PublicChats type_filter, DialogTypeFilter, search_dialogs move) | ~14 | changes search data structures (`found_public_dialogs_` → 2D type_num map) |
| **webbrowser** (WebBrowserManager/Settings/DomainException, in-app browser) | ~10 | WebBrowserManager not landed; rename-collision risk; non-mission feature |
| **live-location** (separate messageLiveLocation/inputMessageLiveLocation) | ~7 | foundation conflicted in fork-customized poll/td_api.tl area |
| **chat-join / guard-bot** (updateChatJoinResult, answerChatJoinRequestQuery, join_dialog) | ~5 | tangled UpdatesManager dispatch + guard-bot plumbing |
| **auth web-token / passkey** | ~3 | fork refactored auth error handling (`handle_expected_query_error`); security-sensitive |
| **doc-only / cosmetic / tg_cli / version-bump / iOS-build** | ~21 | no functionality (rejected, not真 Phase-2 work) — fork manages its own docs/version/iOS build |

Net **functional** Phase-2 work ≈ **124** commits (145 − ~21 non-functional).

## 5. Phase 2 plan (per-epic integration, recommended order)

Do one epic at a time: cherry-pick its commits in order, resolve each conflict preserving fork
hardening, fix the C++, build-verify the epic, then move on.

1. **live-location (~7)** — most isolated; good warm-up.
2. **chat-join / guard-bot (~5)** — moderate, mostly additive.
3. **search type-filter (~14)** — mechanical data-structure change, low fork-clash.
4. **webbrowser (~10)** — land WebBrowserManager first, then settings/updates.
5. **WebPageBlock rich-text refactor (~16)** — reconcile `blocks`/`page_blocks` member naming first.
6. **rich-message (~53)** — depends on #5; the big one; integrate types → content → managers.
7. **poll-media / W3-P (~30)** — last and hardest; requires careful manual merge of the fork's poll
   schema with upstream PollMedia, preserving every W3-P fail-closed guard.

Skip permanently: the ~21 doc/cosmetic/version/iOS-build commits (no functionality, fork-managed).

## 6. Status

Phase 1 complete and compile-verified on Linux. Awaiting go-ahead to begin Phase 2 epic integration.
No production-branch (`feat/adaptive-runtime-profile-rotation`) change — all Phase-1 work is on the
isolated `feat/upstream-backport-bulk` branch, reviewable as a unit.
