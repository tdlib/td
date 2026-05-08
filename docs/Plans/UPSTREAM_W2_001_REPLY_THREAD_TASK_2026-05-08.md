# W2-001 Reply Thread Normalization Task

Date: 2026-05-08
Wave: 2
Candidate ID: W2-001
Upstream commit: `a09adfc63` (`Fix reply_to_top_id.`)
Status: Ready for contract capture and RED-phase execution

## 1. Objective

Turn W2-001 into a bounded implementation task that can be executed without reopening Wave 2 scope.
The task is limited to the local reply-thread normalization seam in
`td/telegram/MessageReplyHeader.cpp` and its directly affected consumers.

## 2. Local Hypothesis and Cheap Disconfirming Check

### 2.1 Falsifiable Local Hypothesis

The local `MessageReplyHeader::MessageReplyHeader(...)` implementation mishandles a specific topic-
reply header shape:

1. `forum_topic_ == true`
2. `reply_to_top_id_ == 0`
3. `reply_from_ != nullptr`
4. `reply_to_peer_id_ == nullptr`
5. `reply_to_msg_id_ != 0`

In this shape, the header represents a topic-thread anchor that upstream now normalizes by moving
`reply_to_msg_id_` into `reply_to_top_id_` before constructing `RepliedMessageInfo`. The local fork
does not perform this rewrite.

Expected bug surface in the local tree:

- `top_thread_message_id_` may be recoverable later from `replied_message_info_`, but
  `replied_message_info_.get_same_chat_reply_to_message_id(false)` still exposes a same-chat reply
  target that should have been consumed as a thread-root anchor.
- Consumers in `MessagesManager` and `QuickReplyManager` can therefore observe a reply-to-message
  semantic where the intended meaning is thread attachment.

### 2.2 Cheap Disconfirming Check

Write a constructor-level RED test that builds a `telegram_api::messageReplyHeader` with exactly the
five conditions above and asserts both of these postconditions:

1. `top_thread_message_id_` becomes the server message ID from `reply_to_msg_id_`
2. `replied_message_info_.get_same_chat_reply_to_message_id(false)` is invalid after normalization

If the second assertion already holds in the local tree, the current hypothesis is false and the
task must pause for one more nearby semantic read before editing production code.

## 3. Local Owning Surface

Primary constructor seam:

1. `td/telegram/MessageReplyHeader.cpp`
2. `td/telegram/MessageReplyHeader.h`

Dependent consumers verified locally:

1. `td/telegram/MessagesManager.cpp`
   - reply-header construction for ordinary messages
   - reply-header construction for service messages
2. `td/telegram/QuickReplyManager.cpp`
   - consumes `replied_message_info_.get_same_chat_reply_to_message_id(true)`
3. `td/telegram/RepliedMessageInfo.cpp`
   - parses `reply_to_msg_id_`, `reply_to_peer_id_`, and `reply_from_`

Scope rule: no edits outside this surface unless a failing RED test proves one of these consumers is
the true owning bug site.

## 4. Contract Snapshot

Contract anchor: `MessageReplyHeader::MessageReplyHeader(Td *, tl_object_ptr<telegram_api::MessageReplyHeader> &&,
DialogId, MessageId, int32)`

- Inputs: raw reply header object, owner `dialog_id`, current `message_id`, and message `date`
- Outputs: normalized `top_thread_message_id_`, `is_topic_message_`, `replied_message_info_`, or
  `story_full_id_`
- Side effects: logs invalid header shapes; determines downstream reply/thread semantics
- Preconditions: server may send partially populated or legacy topic-reply headers
- Postconditions:
  1. Explicit `reply_to_top_id_` remains authoritative for thread rooting
  2. Recoverable topic-thread anchors must become `top_thread_message_id_` even if the server omits
     `reply_to_top_id_`
  3. A synthesized thread anchor must not remain exposed as same-chat reply-to-message state in
     `replied_message_info_`
  4. Non-topic, cross-chat, scheduled, or peer-qualified replies must not be rewritten into thread
     anchors

## 5. RED Test Plan

Add the following new test files under `test/` and wire them into `test/CMakeLists.txt` as part of
the implementation task.

### 5.1 Contract File

File: `test/message_reply_header_contract.cpp`

Required RED tests:

1. `TEST(MessageReplyHeaderContract, preserves_explicit_top_thread_id_for_forum_topic)`
2. `TEST(MessageReplyHeaderContract, normalizes_missing_top_id_into_thread_anchor_when_origin_present)`
3. `TEST(MessageReplyHeaderContract, leaves_same_chat_reply_intact_when_not_forum_topic)`
4. `TEST(MessageReplyHeaderContract, does_not_rewrite_when_reply_to_peer_id_is_present)`

The second test is the primary RED discriminator and must fail on the current local tree before any
production edit is applied.

### 5.2 Adversarial File

File: `test/message_reply_header_adversarial.cpp`

Required RED tests:

1. `TEST(MessageReplyHeaderAdversarial, does_not_promote_cross_chat_reply_into_thread_anchor)`
2. `TEST(MessageReplyHeaderAdversarial, does_not_promote_missing_origin_into_thread_anchor)`
3. `TEST(MessageReplyHeaderAdversarial, clears_topic_flag_when_no_valid_thread_target_remains)`
4. `TEST(MessageReplyHeaderAdversarial, rejects_conflicting_top_id_and_reply_message_id_semantics)`

### 5.3 Integration File

File: `test/message_reply_header_integration.cpp`

Required RED tests:

1. `TEST(MessageReplyHeaderIntegration, messages_manager_consumer_observes_thread_anchor_not_same_chat_reply)`
2. `TEST(MessageReplyHeaderIntegration, quick_reply_consumer_does_not_treat_thread_anchor_as_reply_target)`

If an integration seam is too expensive to stand up immediately, the constructor-level RED suite may
be authored first, but the integration file remains mandatory before final classification of W2-001.

## 6. Expected First Failure

At least one RED test must fail against the current local tree for the exact missing-rewrite shape
described in Section 2. If all constructor-level tests pass immediately, do not widen scope; instead:

1. inspect `RepliedMessageInfo::get_same_chat_reply_to_message_id(...)`
2. inspect the nearest consumer (`QuickReplyManager` first, then `MessagesManager`)
3. update the task with the corrected owning seam before any production edit

## 7. Implementation Constraints

1. Keep the production change limited to the `MessageReplyHeader` normalization seam unless a RED
   test proves the bug is actually owned by a consumer.
2. Do not fold unrelated reply parsing, quote handling, or link-manager work into W2-001.
3. Preserve scheduled-reply, story-reply, and cross-chat reply behavior.
4. Use the smallest local rewrite necessary to consume the thread-anchor case before
   `RepliedMessageInfo` parses the header.

## 8. Validation Plan

After the first substantive edit, run focused validation immediately:

1. build the touched tests
2. run the new `MessageReplyHeader*` tests first
3. rerun the nearest existing reply-sensitive or message-ingest tests if any are touched
4. rerun broader targeted validation only after the focused slice is green

Repository-specific note: exact CTest case discovery for `run_all_tests` is refreshed at CMake
configure time, so adding new `TEST(...)` cases requires rerunning CMake configure before exact-case
CTest selection can see them.

## 9. Exit Criteria

W2-001 is ready for decision classification only when:

1. all contract, adversarial, and integration tests above exist
2. the primary RED discriminator failed before the production fix
3. the final fix preserves thread anchoring while removing false same-chat reply semantics
4. focused validation passes with no new file-level errors
5. the result can be recorded in the Wave 2 decision report without adding any extra upstream commit
