# Upstream Wave 2B Closure Note (Bounded)

Date: 2026-05-11
Last updated: 2026-05-18
Scope: Wave 2B residual micro-correctness queue only (9 commits)
Reference manifest: `docs/Plans/UPSTREAM_BACKPORT_MANIFEST_2026-05-08.md`

## 1. Closure Decision

This note records bounded closure for the currently deferred Wave 2B queue: each of the 9 manifest
entries is now covered by a production seam in the local tree and by explicit tests (contract and/or
adversarial/integration/source-contract) with anchored citations.

Queue size and entries are sourced from:

- `docs/Plans/UPSTREAM_BACKPORT_MANIFEST_2026-05-08.md:26`
- `docs/Plans/UPSTREAM_BACKPORT_MANIFEST_2026-05-08.md:80`
- `docs/Plans/UPSTREAM_BACKPORT_MANIFEST_2026-05-08.md:84`
- `docs/Plans/UPSTREAM_BACKPORT_MANIFEST_2026-05-08.md:208`
- `docs/Plans/UPSTREAM_BACKPORT_MANIFEST_2026-05-08.md:213`
- `docs/Plans/UPSTREAM_BACKPORT_MANIFEST_2026-05-08.md:214`
- `docs/Plans/UPSTREAM_BACKPORT_MANIFEST_2026-05-08.md:217`
- `docs/Plans/UPSTREAM_BACKPORT_MANIFEST_2026-05-08.md:224`
- `docs/Plans/UPSTREAM_BACKPORT_MANIFEST_2026-05-08.md:227`
- `docs/Plans/UPSTREAM_BACKPORT_MANIFEST_2026-05-08.md:228`

## 2. Coverage Matrix (All 9 W2B Entries)

| Commit | Upstream summary | Coverage verdict | Production seam citations | Test seam citations |
|---|---|---|---|---|
| `8fc2344f3` | Fix value of can_manage_tags in basic groups | Covered | `td/telegram/DialogParticipant.cpp:49`, `td/telegram/DialogParticipant.cpp:397`, `td/telegram/DialogParticipant.cpp:400` | `test/dialog_participant_group_admin_contract.cpp:75`, `test/dialog_participant_group_admin_source_contract.cpp:52` |
| `d5714b0b8` | Fix replies to invalid messages | Covered | `td/telegram/MessagesManager.cpp:20690`, `td/telegram/MessagesManager.cpp:20692` | `test/reply_and_username_contract.cpp:44`, `test/reply_and_username_adversarial.cpp:37` |
| `bcbe2f309` | Restore replies to yet unsent messages only for forwards | Covered | `td/telegram/MessagesManager.cpp:35217`, `td/telegram/MessagesManager.cpp:35219` | `test/reply_and_username_contract.cpp:52`, `test/reply_and_username_adversarial.cpp:44` |
| `84d2ea0d8` | Fix handling of `USERNAME_OCCUPIED` | Covered | `td/telegram/DialogManager.cpp:3017` | `test/reply_and_username_contract.cpp:77`, `test/reply_and_username_integration.cpp:27` |
| `a96365b5f` | Remove special handling of `USERNAME_PURCHASE_AVAILABLE` | Covered | `td/telegram/DialogManager.cpp:3020`, `td/telegram/DialogManager.cpp:3023`, `td/telegram/DialogManager.cpp:3026` | `test/reply_and_username_contract.cpp:77`, `test/reply_and_username_adversarial.cpp:57`, `test/reply_and_username_integration.cpp:63` |
| `9c62782dc` | Ignore `can_manage_topics` in basic groups | Covered | `td/telegram/DialogParticipant.cpp:71`, `td/telegram/DialogParticipant.cpp:76`, `td/telegram/DialogParticipant.cpp:227`, `td/telegram/DialogParticipant.cpp:229`, `td/telegram/ChatManager.cpp:1941` | `test/dialog_participant_group_admin_contract.cpp:54`, `test/dialog_participant_restricted_rights_contract.cpp:14`, `test/dialog_participant_restricted_rights_source_contract.cpp:27`, `test/dialog_participant_restricted_rights_source_contract.cpp:52` |
| `562bce098` | Ignore draft replies to yet unsent messages | Covered (local adaptation includes scheduled IDs) | `td/telegram/DraftMessage.hpp:77` | `test/reply_and_username_contract.cpp:27`, `test/reply_and_username_integration.cpp:27` |
| `aeddf8ca3` | Fix processing of guest messages | Covered | `td/telegram/MessagesManager.cpp:10962`, `td/telegram/MessagesManager.cpp:11033`, `td/telegram/MessagesManager.cpp:11072`, `td/telegram/MessagesManager.cpp:11112`, `td/telegram/MessagesManager.cpp:11196`, `td/telegram/MessagesManager.cpp:11197` | `test/business_guest_message_contract.cpp:30`, `test/business_guest_message_contract.cpp:60`, `test/business_guest_message_contract.cpp:95`, `test/business_guest_message_adversarial.cpp:82`, `test/business_guest_message_integration.cpp:48` |
| `336504954` | Don't expect own guest messages to be outgoing | Covered | `td/telegram/MessagesManager.cpp:10971`, `td/telegram/MessagesManager.cpp:10973` | `test/business_guest_message_contract.cpp:40`, `test/business_guest_message_contract.cpp:50`, `test/business_guest_message_adversarial.cpp:47`, `test/business_guest_message_integration.cpp:56` |

## 3. Focused Validation Snapshot

The following focused suites are already present and were re-run during this hardening pass:

1. `DialogParticipantGroupAdmin` (29/29 selected tests passed)
2. `DialogParticipantRestrictedRights` (11/11 selected tests passed)
3. `ReplyAndUsername` (14/14 selected tests passed)
4. `BusinessGuestMessage` (26/26 selected tests passed)

Representative test anchors:

- `test/dialog_participant_group_admin_contract.cpp:54`
- `test/dialog_participant_group_admin_contract.cpp:75`
- `test/dialog_participant_restricted_rights_contract.cpp:14`
- `test/reply_and_username_contract.cpp:27`
- `test/reply_and_username_contract.cpp:44`
- `test/reply_and_username_contract.cpp:52`
- `test/reply_and_username_contract.cpp:77`
- `test/business_guest_message_contract.cpp:30`
- `test/business_guest_message_contract.cpp:40`
- `test/business_guest_message_contract.cpp:50`

## 4. Scope Guard

This closure is bounded to the Wave 2B queue listed above. It does not authorize intake from later
capability waves and does not reclassify broader Wave 3/4/5 backlog work.

## 5. W2B Guest-Lane Hardening Decision (2026-05-18)

This subsection records one narrow hardening adaptation in the already-covered W2B guest-message seam.

1. Valuable behavior (kept): reply-to-story validation explicitly allows only these dialog scopes:
	`my_dialog_id`, destination `dialog_id`, `DialogId(sender_user_id)`, and
	`message_info.guest_bot_via_dialog_id`.
	- Production anchor: `td/telegram/MessagesManager.cpp:11112`.
2. Not valuable behavior (rejected): a guest-lane bypass clause that skipped this validation when
	`is_guest_message` is true.
	- Rejected pattern (kept absent):
	  `story_dialog_id!=message_info.guest_bot_via_dialog_id&&!is_guest_message`.
3. Why this adaptation is valuable:
	- It is fail-closed for untrusted update payloads; invalid story ownership references are dropped in
	  all lanes, including guest-lane inputs.
	- It keeps the explicit guest exception (`guest_bot_via_dialog_id`) without creating a general bypass.
4. Why bypass semantics are not valuable:
	- They widen the accepted input surface for guest messages without a protocol requirement.
	- They create avoidable divergence from boundary-validation consistency under the same owner checks.
5. Test anchors for this decision:
	- `test/business_guest_message_contract.cpp:95`
	- `test/business_guest_message_adversarial.cpp:82`
	- `test/business_guest_message_integration.cpp:48`
	- `test/business_guest_message_light_fuzz.cpp:39`
	- `test/business_guest_message_stress.cpp:44`
