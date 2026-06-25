# Upstream Backport — Phase 2 Closeout (2026-06-18)

Closeout record for **Phase 2** of the 2026-06-15 upstream intake cycle: integration of the feature
epics that Phase 1 deferred. Companion to
[UPSTREAM_BACKPORT_PHASE1_CLOSEOUT_2026-06-16.md](UPSTREAM_BACKPORT_PHASE1_CLOSEOUT_2026-06-16.md).

## Provenance

| Field | Value |
|---|---|
| Upstream remote | `https://github.com/tdlib/td.git` |
| Baseline (lower) | `upstream-baseline-2026-05-24-e0943d068ce9` (tdlib 1.8.64) |
| New tip (upper) | `upstream-baseline-2026-06-15-a17f87c4cff7` |
| Comparison range | `e0943d068ce9..a17f87c4cff7` — 247 commits |
| Downstream branch | `feat/upstream-backport-bulk` (pushed to `origin`) |
| Phase-2 base | `7c6053a0c` (Phase-1 / live-location tip) |
| Phase-2 tip | `799657fd1` |
| Phase-2 commits | 53 (46 upstream cherry-picks + 7 test/fix/doc) |

## Methodology

Per `docs/Plans/UPSTREAM_BACKPORT_PLAN_2026-06-15.md` and `prompt`-driven rules: **semantic** backport
detection (not git ancestry); fork-safety over upstream purity; tests before/with each pick; preserve
fork fail-closed hardening. Each epic was reconned (per-commit status: missing / exact_present /
semantic_equivalent / superseded_downstream / already_rejected_or_deferred), picked in dependency order
with `cherry-pick -Xno-renames`, conflict-resolved preserving fork form, then built (Docker Linux,
clean-regen codegen) and verified GREEN before the next epic.

## Verification (final, all GREEN)

| Suite | Result |
|---|---|
| Stealth / DPI suite | **239 / 239** |
| SonarBlocker source contracts | **51 / 51** |
| Phase-1 contracts | **3 / 3** |
| Phase-2 contracts | **16 / 16** |
| W3-P poll voter-visibility | **9 / 9** |
| RestrictedRights fail-closed | **12 / 12** |

Build: `tdcore` + `run_all_tests` compile clean on Linux (Ubuntu 24.04, gcc-13, libstdc++,
`-DTDLIB_STEALTH_SHAPING=ON`).

## Landed upstream cherry-picks (fork SHA → upstream SHA)

### Epic: chat-join / guard-bot (5 picked; 15 confirmed already-present)
| fork | upstream | subject |
|---|---|---|
| 0f7e3039f | b449204c5 | Add td_api::updateChatJoinResult. |
| a7da7bac6 | 3e17b9042 | Improve guard_bot_user_id documentation. |
| 148664613 | 7a154de35 | Return webAppUrl in chatJoinResultGuardBotApprovalRequired. |
| 076a33521 | e27fde415 | Improve updateChatJoinResult documentation. |
| 10d57f3db | 434ef4b47 | Add td_api::chatJoinResultRequestDeclined. |

### Epic: search-type-filter (6)
| fork | upstream | subject |
|---|---|---|
| e3bf4c7f0 | 316e93443 | Add enum DialogTypeFilter. |
| b6df47100 | bdf910f19 | Add type_filter in searchPublicChats. |
| c0d37150b | e58779017 | Add searchChatsOnServer.type_filter. |
| 8bd4dcd53 | 9ec11ab28 | Add searchRecentlyFoundChats.type_filter. |
| 90a19f1d9 | 21cbc0d17 | Move search_dialogs to DialogManager. |
| dfa39ab1e | 26d8c6a81 | Add searchChats,type_filter. |

### Epic: in-app web browser (12; 5 already-present)
| fork | upstream | subject |
|---|---|---|
| 0df55b744 | fad8f9afe | Add empty WebBrowserManager. |
| 319d9d04d | 63196bac4 | Add td_api::updateWebBrowserSettings. |
| 80017cb84 | 03e80a120 | Support updateWebBrowserSettings in getCurrentState. |
| 2d9461163 | 0ecf6802c | Save web browser settings between restarts. |
| c24e8ea4c | 8c0c85326 | Support updateWebBrowserSettings. |
| ac3732582 | a1b5ec524 | Support updateWebBrowserException. |
| 6b3d74627 | 9deb0b7d5 | Add td_api::changeWebBrowserSettings. |
| 2d90159d6 | 2bce3ecbe | Add td_api::addWebBrowserSettingsException. |
| ff4bffc4a | e3ef7dc74 | Add td_api::removeWebBrowserSettingsException. |
| 43f053348 | 533ca510b | Add td_api::removeAllWebBrowserSettingsExceptions. |
| a66962d89 | 37fa274f2 | Add td_api::getLinkWebBrowserType. |
| 58e73b385 | 6d0824e37 | Check for bots in WebBrowserManager::on_authorization_success. |

### Epic: instant-view RichText (8; 15 already-present/reverted-net-absent)
| fork | upstream | subject |
|---|---|---|
| b0e8c0c90 | 02e17a09d | Add parameter recurse_text to for_each_rich_text. |
| 8257d98a8 | 4d10e3ae4 | Add and use RichText::get_full_text(). |
| 54946a1b8 | ac0c191f3 | Add RichText.get_input_rich_text(). |
| e87b443d4 | 99e233f6c | Remove richTextAutoUrl. |
| 5446e98f9 | 0bca63e05 | Remove richTextAutoEmailAddress. |
| dad0c8114 | 5161b5958 | Remove richTextAutoPhoneNumber. |
| e17f4795f | 65cd30a81 | Add richTextBankCardNumber.bank_card_number. |
| 3fc633ca5 | d17125721 | Add td_api::richTextReferenceLink. |

### Epic: instant-view PageBlock render (5; 19 already-present, 2 deferred)
| fork | upstream | subject |
|---|---|---|
| 03fbd2f28 | ca67bf738 | Improve WebPageBlock::for_each_text. |
| 0dca299b2 | 4f3676d76 | Simplify PageBlock field names. |
| d049a7236 | 8df09c1ba | Make pageBlockCaption nullable. |
| b4d55dc4e | 39d166296 | Make pageBlockCaption.credit nullable. |
| ec6abea4d | 108b33f15 | Make pageBlockTable.caption nullable. |

### Epic: poll-media render (2; W3-P fail-closed preserved)
| fork | upstream | subject |
|---|---|---|
| 0ab408d5b | d549c79d7 | Add td_api::PollMedia. |
| 86a5ec2b3 | ef5759d20 | Compare type of poll option media. |

### Misc-functional (8)
| fork | upstream | subject |
|---|---|---|
| 3e7d14d12 | 1ab169454 | Simplify string option handling in app config. |
| 333323082 | a3349d3c8 | Open tonsite:// links only in the internal browser. (stealth-positive) |
| d58032b42 | e21eab837 | Add payload to automatic entities. |
| dcc4808bc | 6817f61b9 | Make credit and table caption nullable. |
| 1162e555f | d78ceefc7 | Don't warn about invalid entities in old checklist tasks. |
| 47f37113b | 7445746de | Improve anchor handling. |
| 85a2580b1 | 2fe415631 | Don't send automatic rich text to the server. |
| 783f524f0 | f46b58926 | Add td_api::checkAuthenticationWebToken. (auth, fail-closed) |

### Test / fix / doc commits (7)
| fork | subject |
|---|---|
| 66d5659a6 | test: chat-join/guard-bot contract guards |
| 94f6535bb | test: search-type-filter contract guards |
| 430c3dc24 | test: in-app web browser fail-closed contracts |
| 859a2d7f9 | test: instant-view RichText/PageBlock schema contracts |
| 98f2e48ab | test: poll-media render + fail-safe contract |
| 945545995 | fix: preserve fork forward-not-move invariant in d78ceefc7 ToDoItem ctor |
| 799657fd1 | test: checkAuthenticationWebToken fail-closed auth contract |

## Already present downstream (recorded, NOT reimplemented)

Recon proved the fork already carried these upstream changes (exact_present / superseded / reverted-net-absent),
so they were **not** picked:
- chat-join/guard-bot: 15 commits (fork landed via its own follow-on commits, e.g. dda81b79a).
- in-app web browser: WebDomainException + WebBrowserSettings sub-clusters (9903855b9, fa5adaeea,
  c88895fe6, e1142ec7a, 69a62cb28).
- instant-view RichText subtypes: richTextMathematicalExpression/CustomEmoji/Spoiler/Mention/Hashtag/
  Cashtag/BotCommand/DateTime/BankCardNumber/MentionName + for_each_rich_text (already in fork baseline);
  richTextAutoUrl/AutoEmailAddress/AutoPhoneNumber were add-then-removed upstream (the removals WERE picked).
- instant-view PageBlock subtypes: pageBlockPhoto/Video/Animation has_spoiler, SectionHeading, Math,
  Thinking, BlockQuote(Blocks), ListItem fields, add_dependencies, comparison operators, for_each_text, etc.
- poll-media: 32aaf2980 (Remove special handling for polls).

## Deferred — high-risk, non-mission, fork structurally behind (fork-safety)

Per the fork's standing rule (fork-safety > upstream-purity) these two tightly-coupled SEND/content
clusters were **deferred**, not force-merged. Forcing them requires hand-reconstructing a new content
subsystem with real wire-serialization risk.

1. **rich-message SEND / content (~36)** — `messageRichMessage` content type (fork has no `RichMessage.cpp`),
   `WebPageBlock::get_input_page_block` (726b15907) + `clone()` whose impls come from
   `48f727bb1 "Implement RichMessage::clone()"`; input types (inputAnimation/Audio/Document/Photo/Video);
   `get_input_page_table_cell`, "automatic rich text", `aa2d3178b` (pageBlockAnimation.has_spoiler).
   Applying 726b15907 alone makes all ~33 WebPageBlock subtypes abstract.
2. **poll-media SEND / links (~11)** — `pollMediaLink` (984e1f2b4), `InputPollMedia` (3b0d5e432),
   `inputPollMediaLink`, web-pages-in-polls (d02fe8bbe `get_poll_web_page_ids`, 7b7f7cf21, 002d49193,
   31c1a0222), `can_see_results` (0d1e95e89), `merge_message_contents` fix (3f66d431a).
   **If ever backported, 0d1e95e89 must keep the fork's `recent_voters.clear()` W3-P guard byte-exact.**
   Misc-entangled also deferred: 9dfaec306, 44db71825, e80270f4c.

## Not touched — non-functional (~22)

Documentation/typo/version/iOS-build/tg_cli commits — the fork manages its own docs/version/iOS build.
Recorded in `UPSTREAM_BACKPORT_PHASE2_DEFERRED_2026-06-16.txt`.

## Contract tests added (8 files)

`test/phase2_chatjoin_guardbot_contract.cpp`, `phase2_search_type_filter_contract.cpp`,
`phase2_webbrowser_failclosed_contract.cpp`, `phase2_instant_view_schema_contract.cpp`,
`phase2_poll_media_failsafe_contract.cpp`, `phase2_web_token_auth_failclosed_contract.cpp`
(+ updates to `message_content_null_guard_runtime.cpp`, `sonar_blocker_source_contract.cpp`).
