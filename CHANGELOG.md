## Upstream Intake Cycle (15 June 2026)

Selective intake cycle over the post-baseline upstream delta. Provenance:

- Upstream remote: `https://github.com/tdlib/td.git`
- Baseline (lower bound): `upstream-baseline-2026-05-24-e0943d068ce9` →
  `e0943d068ce90b5010f1aea946e6901e25b43bf6` (tdlib 1.8.64)
- New tip (upper bound): `upstream-baseline-2026-06-15-a17f87c4cff7` →
  `a17f87c4cff7b90b278d12b91ba0614383aaee82`
- Comparison range: `e0943d068ce9..a17f87c4cff7` — **247 commits** (2026-05-19 → 2026-06-13)

Plan and evidence: [docs/Plans/UPSTREAM_BACKPORT_PLAN_2026-06-15.md](docs/Plans/UPSTREAM_BACKPORT_PLAN_2026-06-15.md)
and its Wave A addendum
[docs/Plans/UPSTREAM_BACKPORT_PLAN_2026-06-15_WAVE_A_ADDENDUM.md](docs/Plans/UPSTREAM_BACKPORT_PLAN_2026-06-15_WAVE_A_ADDENDUM.md).

### Backported commits this cycle (5 of 247)

| Upstream SHA | Mode | Fork file | Summary |
|---|---|---|---|
| `84f21a1d8` | exact | `td/telegram/MessageContent.cpp` | `add_message_content_dependencies` resolves `bot_user_id` for ManagedBotCreated |
| `a74cc9af8` | local-equivalent | `td/telegram/DraftMessage.hpp` | clear persisted-draft reply to local/yet-unsent same-chat message |
| `dc73b3ca3` | local-equivalent | `td/telegram/MessagesManager.cpp` | DB-dialog repair re-fetches messages + reloads full dialog info |
| `c3759d5c5` | exact | `td/telegram/CallActor.cpp` | pending-call notification posted via `send_closure_later` |
| `e95e1fd0d` | local-equivalent | `td/telegram/DialogAction.h` | `operator==` also compares `random_id_` and `text_` |

Wave A (provenance/inventory) is complete; gate tally over the 247 (all downstream-status `missing`):
`defer_pending_context` 207 · `reject_not_relevant` 28 · `accept_with_repair` 8 ·
`local_equivalent_adaptation` 4. **No stealth-transport (`td/mtproto`, `tdnet`, TlsInit) commit in
the delta.** The W11-AI2 deferral is unchanged.

Wave B (minimal correctness backports) is implemented in the tree — TDD-first contract tests then
minimal fix; full build/`ctest`/sanitizer matrix runs on Linux CI. Landed:

- `84f21a1d8` exact backport: `add_message_content_dependencies` now resolves the `bot_user_id`
  dependency for `ManagedBotCreated` content (`td/telegram/MessageContent.cpp`).
- `a74cc9af8` local-equivalent: persisted-draft parse clears same-chat replies that are yet-unsent
  **or** local, preserving the fork's existing `is_valid_scheduled()` guard
  (`td/telegram/DraftMessage.hpp`).
- `dc73b3ca3` local-equivalent: repair of DB-loaded dialogs re-fetches unresolved messages and reloads
  full dialog info on failed dependency resolution, keeping the fork's caller-`source` provenance
  (`td/telegram/MessagesManager.cpp`).

Wave B-2 (after a dry-run cherry-pick feasibility sweep of all 244 remaining commits — 55 apply
cleanly, 189 conflict — only 2 are safe standalone fixes):

- `c3759d5c5` exact: pending-call notification posted via `send_closure_later` instead of
  `send_closure` (reentrancy/ordering hardening) (`td/telegram/CallActor.cpp`).
- `e95e1fd0d` local-equivalent: `DialogAction::operator==` now also compares `random_id_` and `text_`;
  the upstream `RichMessage message_` field is intentionally dropped (feature absent in the fork)
  (`td/telegram/DialogAction.h`).

Remaining ~239 commits stay deferred/rejected — product epics (rich-message, instant-view, WebBrowser,
PollMedia, chat-join, live-location, …) requiring `td_api.tl` + new files + layer-227, which would
violate fork policy "No bulk sync"; not safely backportable standalone.

Contract tests: `test/managed_bot_created_dependency_contract.cpp`,
`test/draft_local_reply_ignore_contract.cpp`, `test/parse_dialog_repair_refetch_contract.cpp`,
`test/call_notification_send_closure_later_contract.cpp`, `test/dialog_action_equality_fields_contract.cpp`.
Closeout: [docs/Plans/UPSTREAM_BACKPORT_PLAN_2026-06-15_WAVE_B_CLOSEOUT.md](docs/Plans/UPSTREAM_BACKPORT_PLAN_2026-06-15_WAVE_B_CLOSEOUT.md).

## Fork Backport Record (24 May 2026)

This section is maintained by this fork and supplements the upstream changelog, which is not
currently maintained for fork-specific selective backports and hardening work.

`tdlib-obf` is maintained as a vendor/security fork of TDLib: `master` is downstream reality,
while exact upstream provenance and selective intake decisions are tracked separately.

Canonical provenance and audit records:

- [docs/Plans/UPSTREAM_BACKPORT_MANIFEST_2026-05-08.md](docs/Plans/UPSTREAM_BACKPORT_MANIFEST_2026-05-08.md)
- [docs/Plans/UPSTREAM_BACKPORT_GATING_PLAN_2026-05-08.md](docs/Plans/UPSTREAM_BACKPORT_GATING_PLAN_2026-05-08.md)

Important: GitHub ahead/behind counters are ancestry-based. A change can be documented here as
backported or locally adapted without reducing the reported `behind` count unless the exact
upstream commits are ancestors of this branch.

### Selective Upstream Backports And Local-Equivalent Adaptations

- `W2-C` exact or accept-with-repair backports: `a09adfc63`, `386eca6fe`, `1a9ef3d68`,
  `5340472b0`.
  Local adaptation: topic reply normalization, least-privilege administrator-rights repair,
  rank-preserving load normalization, and lifetime hardening for use-after-move paths.

- `W2B` bounded local-equivalent backports: `8fc2344f3`, `d5714b0b8`, `bcbe2f309`,
  `84d2ea0d8`, `a96365b5f`, `9c62782dc`, `562bce098`, `aeddf8ca3`, `336504954`.
  Local adaptation: basic-group rights normalization, reply-to-invalid-message fail-closed
  cleanup, username error handling repair, and related reply/draft semantics hardening.

- `W3-P` poll hardening:
  direct merged slice `21275249c`, `c81e6da9f`, `3e78ebcd8`; repository-resident
  local-equivalent slice `084707e99`, `bb6574d9f`, `7d56f9c58`, `bcd2c683c`, `f654c5c81`,
  `1eaf2481e`, `d6ef00fa9`, `04498cfbb`, `1f68a4a84`, `271c71136`, `978979edb`, `ca82791de`,
  `b00c67763`, `0b9e9829b`, `b5c87eb91`, `e7cbde50c`, `d51464eb2`, `c6411b9c9`, `dc470c164`,
  `1574780ca`, `02473d316`, `aaea672ae`.
  Local adaptation: member-only and country-restricted polls, poll vote statistics, explicit
  unread-poll-vote state and update fanout, fail-closed voter visibility, restriction-reason
  hardening, quick-reply poll media guards, and poll media send guards.

- `W4-G` guest-query and guest-bot capability bundle: `57259ff9e`, `49e592ccc`, `7aed695bf`,
  `339ff0c6c`, `3fc0b253d`, `9175d061a`, `3fbbd52ff`, `64d4cea86`.
  Local adaptation: guest caller provenance is preserved through parse or object or update paths,
  guest-query dispatch runs through `dispatch_guest_query_qts_update(...)`, result parsing remains
  fail-closed, guest-bot top-dialog selection is isolated, and invalid identifiers are rejected on
  strict-positive checks before update emission.

- `W5-AI` bounded text-composition slice currently accounted in the local tree:
  `c3a6ecea6`, `d747885cb`, `528988dd9`, `0c6ea7e09`, `9571c262f`, `ff051c4dc`, `df4bfee0d`,
  `c96e67c38`, `58d72a0e8`, `a26ccb8c5`, `990b821c8`.
  Local adaptation: input-text plumbing, shared style-slug validation at the link boundary,
  promise-safe `updateAiComposeTones` handling, and config-surface baseline hardening.
  Remaining exact-scope owner or product expansion stays intentionally deferred in `W11-AI2`.

- `W6-M` bounded managed-bot token and access-settings backports: `3819fded5`, `19292458f`,
  `b6aa479a9`, `83506493e`.
  Local adaptation: compatibility-preserving `getManagedBotToken` aliasing, typed
  `botAccessSettings` read or write surfaces, and shared bot-session or ownership fail-closed
  dispatch via `dispatch_get_managed_bot_token(...)`,
  `dispatch_managed_bot_access_settings_read(...)`, and
  `dispatch_managed_bot_access_settings_write(...)`.

- `W7-D` tooling and documentation deltas: `3bde4782c`, `f3713bba0`, `ed87ce103`.
  Local adaptation: iOS reproducibility controls are already present locally, `tdl-coroutines`
  was already listed, and the fork adds `react-native-tdlib` plus an explicit third-party wrapper
  dependency-audit note in `example/README.md`.

- `W8-X` bounded residual hardening: `13003156a`, `05600741a`.
  Additional `W8-X` rows already incorporated or semantically consumed elsewhere in the local
  tree: `3d38fb7aa`, `bc79a6d2d`, `528988dd9`, `c96e67c38`, `0c6ea7e09`, `ff051c4dc`,
  `df4bfee0d`, `a26ccb8c5`.
  Local adaptation: targeted `MessageContent` null guards, invalid-file-id fail-closed handling,
  and accounting that consumes some residual rows via the bounded `W5-AI` baseline rather than
  separate cherry-picks.

This section intentionally does not claim full upstream-exact parity. For research-only rows,
ignored rows, and exact-scope deferred bundles that are not treated as landed backports,
use the manifest and gating plan above.