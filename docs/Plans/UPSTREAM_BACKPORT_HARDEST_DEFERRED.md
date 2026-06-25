# Hardest deferred backports — full enumeration (2026-06-20)

**61 functional commits** were deliberately deferred for fork-safety — they form two tightly-coupled,
non-mission clusters where the fork is structurally behind, so a mechanical cherry-pick is unsafe. This
is the **complete per-commit list**, grouped by sub-feature and ranked hardest → least-hard, with the
concrete reason each is hard.

- rich-message / instant-view SEND: **46**
- poll-media SEND / links: **12**
- misc instant-view SEND helpers: **3**

Legend — **Risk**: chance of silently breaking something (esp. wire serialization / privacy).

---

## 🥇 TIER 1 — `messageRichMessage` content type + WebPageBlock SEND  (46 commits · HARDEST)

The fork has **no `RichMessage.cpp` / `messageRichMessage` at all** — this is an entire new
message-content subsystem the fork never took. The structural blocker:
`726b15907` (landed-render-only) declares `clone()`/`get_input_page_block()` as **pure virtuals**, but
their impls live in `48f727bb1` (part of this epic) → applying either alone makes **all ~33 WebPageBlock
subtypes abstract**. Everything below is circularly entangled with it.

### 1a. Content-type core (the structural blockers)
| upstream | subject | why hard |
|---|---|---|
| `e0f07c5d8` | Add td_api::richMessage | new public type, root of the subsystem |
| `1a3dcfe41` | Add td_api::messageRichMessage | new message content type — no RichMessage.cpp in fork |
| `48f727bb1` | Implement RichMessage::clone() | **the clone() impls 726b15907 needs across all 33 subtypes** |
| `033d7b14a` | Support RichMessage in get_message_content_min_user_ids() | content-pipeline integration |
| `111c58199` | Support RichText in get_message_content_has_bot_commands | content-pipeline integration |
| `f9ed7a80b` | Support RichText in can_send_message_content | send-gate integration |
| `9f0f70ff2` | Check can_send_messages in RichMessage::can_send | **permission gate — must stay fail-closed** |
| `a31a0348a` | Allow copying of rich messages only for bots | **permission gate (bot-only copy)** |
| `e3e63092f` | Use skip_bot_commands in get_rich_message_object | the `context->skip_bot_commands_` the fork lacks |
| `f18e7df84` | Support messageRichText in update_used_hashtags | hashtag indexing integration |
| `1eaeaf657` | Support replies to rich messages in other chats | reply plumbing |
| `b629bd3b3` | Use get_message_content_rich_message if possible | content dispatch |
| `d44b6cab4` | Add td_api::getFullRichMessage | new request method |
| `c1ca7625e` | Support rich messages in profile tabs | profile integration |
| `fbe572ddc` | Extract authentication codes from rich messages | **security-adjacent: auth-code extraction path** |

### 1b. Input / send path (WIRE-SERIALIZATION RISK)
| upstream | subject | why hard |
|---|---|---|
| `2d88e8264` | Add td_api::inputRichMessage | input value type |
| `2ded57e38` | Add td_api::inputMessageRichMessage | input message content |
| `ca9098cf2` | Support inputRichMessage | **serializes onto the wire — runtime-only correctness** |
| `8d54e2af9` | Add sendRichMessageDraft | new send path |
| `79eaa8a30` | Support messageRichMessage in updatePendingMessage | pending-message plumbing |
| `c710b047f` | Support inputMessageRichMessage in inline query results | inline integration |
| `f79ba0d8d` | Support telegram_api::botInlineMessageRichMessage | inline wire type |
| `d7302206b` | Add td_api::draftMessageContentRichMessage | draft content type |
| `bf8b07963` | Add td_api::inputAnimation | input file type (rich-message media) |
| `3271d2733` | Add td_api::inputAudio | input file type |
| `e6245bdf2` | Add td_api::inputDocument | input file type |
| `499c22792` | Add td_api::inputPhoto | input file type |
| `ad25dc7a2` | Add td_api::inputVideo | input file type |
| `1eff7c49a` | Support rich messages in editMessageText | edit path |
| `bd9a93fae` | Support rich messages in editInlineMessageText | edit path |
| `45b40989d` | Support rich messages in editBusinessMessageText | edit path |
| `39d6b8026` | Support rich messages in editQuickReplyMessage | edit path |
| `5e21b56e9` | Support rich messages in move_message_content_sticker_set_to_top | content op |

### 1c. Options / limits (must land for safety bounds)
| upstream | subject |
|---|---|
| `b5bc4248c` | Add option "rich_message_text_length_max" |
| `0fd58b421` | Add option "rich_message_block_count_max" |
| `a5a345f20` | Add option "rich_message_depth_max" |
| `27d21d4b8` | Add option "rich_message_media_count_max" |
| `0e0c37df2` | Add option "rich_message_table_column_count_max" |

### 1d. WebPageBlock SEND + tg_cli/doc tail
| upstream | subject | note |
|---|---|---|
| `aa2d3178b` | Add pageBlockAnimation.has_spoiler | needs WebPageBlockAnimation clone()/get_input_page_block |
| `d68320c85` | tg_cli: add and use as_input_message | cli only |
| `a74c276fd` | tg_cli: add InputRichMessage | cli only |
| `691dc32b4` | tg_cli: allow to pass rich messages instead of text | cli only |
| `2cf217f42` | Improve pageBlockListItem.type documentation | doc only |
| `e65c04fd1` | Document blocks specific to instant view | doc only |
| `01b0cecba` | Update pageBlockChatLink documentation | doc only |
| `1f55d8096` | Fix richTextMention documentation | doc only |

**What a safe backport requires:** port the whole epic in upstream order as a *new subsystem*, with
**runtime round-trip serialization tests against real fixtures** (source-contract tests cannot catch a
wrong wire format). Multi-session. Preserve the permission gates (`9f0f70ff2`, `a31a0348a`) fail-closed.

---

## 🥈 TIER 2 — `can_see_results` (poll voter-visibility)  (1 commit · HIGHEST SECURITY RISK)

| upstream | subject |
|---|---|
| `0d1e95e89` | Add Poll.can_see_results |

Smallest hard item but the **single most security-sensitive line in the whole 247-commit delta.** It
rewrites the exact `get_poll_object` region the fork hardened (W3-P). The De-Morgan count-hide rephrase
is fine, **but** the fork's exclusive `if (!can_get_voters && !is_bot()) { recent_voters.clear(); }`
(upstream has none) must be kept **byte-for-byte** — rewriting it to `if (!can_see_results)` fails the
pinned contract test; deleting it re-exposes recent voters the fork deliberately hides (fail-open /
privacy regression). Also blocked: the fork's poll TL ctor is several schema versions behind.

---

## 🥉 TIER 3 — poll-media links + web-pages-in-polls  (11 commits)

| upstream | subject | why hard |
|---|---|---|
| `984e1f2b4` | Add td_api::pollMediaLink | needs web-page-in-poll infra |
| `3b0d5e432` | Add td_api::InputPollMedia | input refactor of poll media |
| `a2b0e37ad` | Add td_api::inputPollMediaLink | depends on InputPollMedia |
| `d02fe8bbe` | Add PollManager::get_poll_web_page_ids | web-pages-in-polls accessor |
| `7b7f7cf21` | Register poll web pages | hard-depends on get_poll_web_page_ids |
| `002d49193` | Remove empty web pages from polls | web-pages-in-polls plumbing |
| `31c1a0222` | Register web pages of loaded-from-database polls | web-pages-in-polls plumbing |
| `3f66d431a` | Fix merge_message_contents for polls | needs `get_individual_message_content_refs` (also deferred) |
| `fe5ab8d49` | Return no media for invalid poll media | depends on pollMediaLink |
| `3a2c08346` | Simplify get_poll_media_object usage | depends on the link refactor |
| `0ae2fd646` | tg_cli: support inputPollMediaLink | cli only |

Cannot land standalone — the link/web-page sub-feature reaches back into the deferred web-page subsystem;
stubbing risks an incoherent half-feature.

---

## TIER 4 — misc instant-view SEND helpers  (3 commits · low risk, blocked on Tier 1)

| upstream | subject | blocked on |
|---|---|---|
| `9dfaec306` | Ignore anchors for shared media | `RichText::get_index_mask` |
| `44db71825` | Fix trim_first | `RichText::trim_first` / `get_input_rich_text` |
| `e80270f4c` | Fix get_input_page_table_cell | `get_input_page_block` (Tier 1) |

---

## Bottom line

| Tier | What | Commits | Risk | Unblock |
|---|---|---|---|---|
| 1 | messageRichMessage + WebPageBlock SEND | 46 | very high (new subsystem, wire serialization) | port whole epic + runtime fixtures |
| 2 | `can_see_results` | 1 | high (privacy) | keep `recent_voters.clear()` byte-exact |
| 3 | poll-media links / web-pages-in-polls | 11 | med-high | land with web-page subsystem |
| 4 | misc instant-view helpers | 3 | low | after Tier 1 |

None is a cherry-pick. Tier 1 is a multi-session subsystem port with real wire-format risk; Tier 2 is
small but the most security-critical. If anything is taken, take **Tier 2 first, in isolation**, W3-P
guard preserved byte-for-byte and re-verified — it is the only one with security value; the rest is
non-mission rich-content rendering/sending.
