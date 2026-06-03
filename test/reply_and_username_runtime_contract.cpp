// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/DialogId.h"
#include "td/telegram/DraftMessage.h"
#include "td/telegram/DraftMessage.hpp"
#include "td/telegram/Global.h"
#include "td/telegram/InputMessageText.hpp"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/MessageInputReplyTo.h"
#include "td/telegram/MessageInputReplyTo.hpp"
#include "td/telegram/MessageQuote.h"
#include "td/telegram/ScheduledServerMessageId.h"
#include "td/telegram/ServerMessageId.h"
#include "td/telegram/UserId.h"
#include "td/telegram/Version.h"

#include "td/actor/actor.h"
#include "td/actor/ConcurrentScheduler.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/tests.h"
#include "td/utils/tl_storers.h"

#include "test/stealth/SourceContractFileReader.h"

namespace {

constexpr td::int32 kHasInputMessageTextFlag = 1 << 0;
constexpr td::int32 kHasMessageInputReplyToFlag = 1 << 1;

class GlobalContextScope final {
 public:
  GlobalContextScope() : old_context_(td::Scheduler::context()), context_(std::make_shared<td::Global>()) {
    context_->this_ptr_ = context_;
    td::Scheduler::context() = context_.get();
  }

  ~GlobalContextScope() {
    td::Scheduler::context() = old_context_;
  }

 private:
  td::ActorContext *old_context_ = nullptr;
  std::shared_ptr<td::Global> context_;
};

td::string normalize_for_contract(td::Slice source) {
  td::string normalized;
  normalized.reserve(source.size());
  for (auto c : source) {
    switch (static_cast<unsigned char>(c)) {
      case ' ':
      case '\t':
      case '\r':
      case '\n':
        continue;
      default:
        break;
    }
    normalized.push_back(c);
  }
  return normalized;
}

td::BufferSlice serialize_modern_draft_with_reply(const td::MessageInputReplyTo &reply_to) {
  constexpr td::int32 draft_flags = kHasMessageInputReplyToFlag;
  constexpr td::int32 draft_date = 1720000000;

  td::TlStorerCalcLength calc;
  calc.store_int(static_cast<td::int32>(td::Version::Next) - 1);
  td::store(draft_flags, calc);
  td::store(draft_date, calc);
  td::store(reply_to, calc);

  td::BufferSlice payload(calc.get_length());
  td::TlStorerUnsafe storer(payload.as_mutable_slice().ubegin());
  storer.store_int(static_cast<td::int32>(td::Version::Next) - 1);
  td::store(draft_flags, storer);
  td::store(draft_date, storer);
  td::store(reply_to, storer);
  return payload;
}

td::BufferSlice serialize_legacy_draft_with_reply(td::MessageId legacy_reply_to_message_id) {
  constexpr td::int32 draft_date = 1730000000;
  td::InputMessageText input_message_text;

  td::TlStorerCalcLength calc;
  calc.store_int(static_cast<td::int32>(td::Version::SupportRepliesInOtherChats) - 1);
  td::store(draft_date, calc);
  td::store(legacy_reply_to_message_id, calc);
  td::store(input_message_text, calc);

  td::BufferSlice payload(calc.get_length());
  td::TlStorerUnsafe storer(payload.as_mutable_slice().ubegin());
  storer.store_int(static_cast<td::int32>(td::Version::SupportRepliesInOtherChats) - 1);
  td::store(draft_date, storer);
  td::store(legacy_reply_to_message_id, storer);
  td::store(input_message_text, storer);
  return payload;
}

td::Result<td::BufferSlice> parse_and_reserialize_draft(td::Slice payload) {
  td::ConcurrentScheduler scheduler(0, 0);
  scheduler.start();

  td::Result<td::BufferSlice> result;
  {
    auto guard = scheduler.get_main_guard();
    GlobalContextScope context_scope;

    result = [&]() -> td::Result<td::BufferSlice> {
      td::DraftMessage draft;
      TRY_STATUS(td::log_event_parse(draft, payload));
      return td::log_event_store(draft);
    }();
  }

  scheduler.finish();
  return result;
}

struct StoredDraftView {
  td::int32 flags = 0;
  td::int32 date = 0;
  td::MessageInputReplyTo reply_to;
};

td::Result<StoredDraftView> parse_current_stored_draft(td::Slice payload) {
  td::ConcurrentScheduler scheduler(0, 0);
  scheduler.start();

  td::Result<StoredDraftView> result;
  {
    auto guard = scheduler.get_main_guard();
    GlobalContextScope context_scope;

    result = [&]() -> td::Result<StoredDraftView> {
      td::LogEventParser parser(payload);
      StoredDraftView stored_draft;

      stored_draft.flags = parser.fetch_int();
      td::parse(stored_draft.date, parser);
      if ((stored_draft.flags & kHasInputMessageTextFlag) != 0) {
        td::InputMessageText input_message_text;
        td::parse(input_message_text, parser);
      }
      if ((stored_draft.flags & kHasMessageInputReplyToFlag) != 0) {
        td::parse(stored_draft.reply_to, parser);
      }
      parser.fetch_end();
      if (parser.get_status().is_error()) {
        return parser.get_status();
      }
      return stored_draft;
    }();
  }

  scheduler.finish();
  return result;
}

td::MessageId make_regular_message_id(bool yet_unsent, td::int32 seed) {
  auto server_message_id = td::ServerMessageId(td::max<td::int32>(1, seed));
  if (!yet_unsent) {
    return td::MessageId(server_message_id);
  }
  auto unsent_raw_id = (static_cast<td::int64>(server_message_id.get()) << 20) | 1;
  return td::MessageId(unsent_raw_id);
}

td::MessageId make_scheduled_message_id(bool yet_unsent, td::int32 seed) {
  auto scheduled_server_message_id = td::ScheduledServerMessageId(td::max<td::int32>(1, seed % ((1 << 18) - 1)));
  auto server_message_id = td::MessageId(scheduled_server_message_id, (1 << 30) + 1024 + seed);
  if (!yet_unsent) {
    return server_message_id;
  }
  return server_message_id.get_next_message_id(td::MessageType::YetUnsent);
}

TEST(ReplyAndUsernameRuntimeContract, LegacyDraftParsePathInvokesSharedYetUnsentSanitizer) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/DraftMessage.hpp");
  auto normalized = normalize_for_contract(source);

  auto helper_pos = normalized.find("autoclear_same_chat_yet_unsent_reply=[this](){");
  auto legacy_clear_pos = normalized.find(
      "message_input_reply_to_=MessageInputReplyTo::regular(legacy_reply_to_message_id);"
      "clear_same_chat_yet_unsent_reply();");

  ASSERT_TRUE(helper_pos != td::string::npos);
  ASSERT_TRUE(legacy_clear_pos != td::string::npos);
  ASSERT_TRUE(helper_pos < legacy_clear_pos);
}

TEST(ReplyAndUsernameRuntimeContract, ModernDraftParsePathStillInvokesSharedYetUnsentSanitizer) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/DraftMessage.hpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(normalized.find("if(has_message_input_reply_to){td::parse(message_input_reply_to_,parser);"
                              "clear_same_chat_yet_unsent_reply();}") != td::string::npos);
  ASSERT_TRUE(normalized.find("if(has_message_input_reply_to){td::parse(message_input_reply_to_,parser);}"
                              "if(has_local_content){") == td::string::npos);
}

TEST(ReplyAndUsernameRuntimeContract, InvalidReplyRejectionClearsMessageIdBeforeUnknownServerReplyAllowance) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto normalized = normalize_for_contract(source);

  auto clear_reject_pos = normalized.find("if(!can_reply_to_message(d,message_id,m)){message_id={};m=nullptr;}");
  auto missing_message_pos = normalized.find("if(m==nullptr){", clear_reject_pos);
  auto unknown_server_allow_pos = normalized.find(
      "if(message_id.is_server()&&d->dialog_id.get_type()!=DialogType::SecretChat&&"
      "d->last_new_message_id.is_valid()&&message_id>d->last_new_message_id&&"
      "(d->notification_info!=nullptr&&"
      "message_id<=d->notification_info->max_push_notification_message_id_)){",
      missing_message_pos);

  ASSERT_TRUE(clear_reject_pos != td::string::npos);
  ASSERT_TRUE(missing_message_pos != td::string::npos);
  ASSERT_TRUE(unknown_server_allow_pos != td::string::npos);
  ASSERT_TRUE(clear_reject_pos < missing_message_pos);
  ASSERT_TRUE(missing_message_pos < unknown_server_allow_pos);
}

TEST(ReplyAndUsernameRuntimeContract, RestoreReplyUsesResolvedMessageIdForUpdatePath) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto normalized = normalize_for_contract(source);

  auto resolve_pos = normalized.find(
      "automessage_id=get_message_id_by_random_id(d,m->reply_to_random_id,"
      "\"restore_message_reply_to_message_id\");");
  auto guard_pos = normalized.find(
      "if((message_id.is_valid()||message_id.is_valid_scheduled())&&!message_id.is_local()&&"
      "(is_message_forward(m)||!message_id.is_yet_unsent())){",
      resolve_pos);
  auto update_pos = normalized.find(
      "update_message_reply_to_message_id(d,m,message_id,false,\"restore_message_reply_to_message_id\");", guard_pos);
  auto fallback_pos = normalized.find(
      "autoimplicit_reply_to_message_id=get_message_topic(d->dialog_id,m)."
      "get_implicit_reply_to_message_id(td_);",
      guard_pos);

  ASSERT_TRUE(resolve_pos != td::string::npos);
  ASSERT_TRUE(guard_pos != td::string::npos);
  ASSERT_TRUE(update_pos != td::string::npos);
  ASSERT_TRUE(fallback_pos != td::string::npos);
  ASSERT_TRUE(resolve_pos < guard_pos);
  ASSERT_TRUE(guard_pos < update_pos);
  ASSERT_TRUE(update_pos < fallback_pos);
}

TEST(ReplyAndUsernameRuntimeContract, LegacyDraftParseClearsScheduledYetUnsentReplyAtRuntime) {
  auto legacy_reply_to_message_id = make_scheduled_message_id(true, 777);
  ASSERT_TRUE(legacy_reply_to_message_id.is_valid_scheduled());
  ASSERT_TRUE(legacy_reply_to_message_id.is_yet_unsent());

  auto payload = serialize_legacy_draft_with_reply(legacy_reply_to_message_id);
  auto normalized = parse_and_reserialize_draft(payload.as_slice());
  ASSERT_TRUE(normalized.is_ok());

  auto view = parse_current_stored_draft(normalized.ok().as_slice());
  ASSERT_TRUE(view.is_ok());
  ASSERT_EQ(0, view.ok().flags & kHasMessageInputReplyToFlag);
}

TEST(ReplyAndUsernameRuntimeContract, ModernDraftParseClearsSameChatScheduledYetUnsentReplyAtRuntime) {
  auto reply_to_message_id = make_scheduled_message_id(true, 888);
  ASSERT_TRUE(reply_to_message_id.is_valid_scheduled());
  ASSERT_TRUE(reply_to_message_id.is_yet_unsent());

  auto payload = serialize_modern_draft_with_reply(td::MessageInputReplyTo::regular(reply_to_message_id));
  auto normalized = parse_and_reserialize_draft(payload.as_slice());
  ASSERT_TRUE(normalized.is_ok());

  auto view = parse_current_stored_draft(normalized.ok().as_slice());
  ASSERT_TRUE(view.is_ok());
  ASSERT_EQ(0, view.ok().flags & kHasMessageInputReplyToFlag);
}

TEST(ReplyAndUsernameRuntimeContract, ModernDraftParseKeepsExternalYetUnsentReplyAtRuntime) {
  auto reply_to_message_id = make_regular_message_id(true, 999);
  ASSERT_TRUE(reply_to_message_id.is_valid());
  ASSERT_TRUE(reply_to_message_id.is_yet_unsent());

  td::DialogId external_dialog_id(td::UserId(static_cast<td::int64>(424242)));
  ASSERT_TRUE(external_dialog_id.is_valid());
  td::MessageInputReplyTo external_reply_to(reply_to_message_id, external_dialog_id, td::MessageQuote(), 0,
                                            td::string(), "runtime_external_yet_unsent");

  auto payload = serialize_modern_draft_with_reply(external_reply_to);
  auto normalized = parse_and_reserialize_draft(payload.as_slice());
  ASSERT_TRUE(normalized.is_ok());

  auto view = parse_current_stored_draft(normalized.ok().as_slice());
  ASSERT_TRUE(view.is_ok());
  ASSERT_NE(0, view.ok().flags & kHasMessageInputReplyToFlag);

  auto reply_message_full_id = view.ok().reply_to.get_reply_message_full_id(td::DialogId());
  ASSERT_EQ(reply_to_message_id, reply_message_full_id.get_message_id());
  ASSERT_EQ(external_dialog_id, reply_message_full_id.get_dialog_id());
}

TEST(ReplyAndUsernameRuntimeContract, ModernDraftParseKeepsExternalServerReplyAtRuntime) {
  auto reply_to_message_id = make_regular_message_id(false, 1234);
  ASSERT_TRUE(reply_to_message_id.is_server());

  td::DialogId external_dialog_id(td::UserId(static_cast<td::int64>(525252)));
  ASSERT_TRUE(external_dialog_id.is_valid());
  td::MessageInputReplyTo external_reply_to(reply_to_message_id, external_dialog_id, td::MessageQuote(), 0,
                                            td::string(), "runtime_external_server");

  auto payload = serialize_modern_draft_with_reply(external_reply_to);
  auto normalized = parse_and_reserialize_draft(payload.as_slice());
  ASSERT_TRUE(normalized.is_ok());

  auto view = parse_current_stored_draft(normalized.ok().as_slice());
  ASSERT_TRUE(view.is_ok());
  ASSERT_NE(0, view.ok().flags & kHasMessageInputReplyToFlag);

  auto reply_message_full_id = view.ok().reply_to.get_reply_message_full_id(td::DialogId());
  ASSERT_EQ(reply_to_message_id, reply_message_full_id.get_message_id());
  ASSERT_EQ(external_dialog_id, reply_message_full_id.get_dialog_id());
}

TEST(ReplyAndUsernameRuntimeContract, ModernDraftParseLightFuzzDropsYetUnsentRepliesFailClosed) {
  constexpr int kIterations = 6000;
  for (int i = 1; i <= kIterations; ++i) {
    bool use_scheduled_message_id = (i % 2) == 0;
    bool use_yet_unsent_message_id = (i % 3) != 0;
    bool use_external_dialog = (i % 5) == 0;

    auto reply_to_message_id = use_scheduled_message_id ? make_scheduled_message_id(use_yet_unsent_message_id, i)
                                                        : make_regular_message_id(use_yet_unsent_message_id, i);
    auto reply_to_dialog_id =
        use_external_dialog ? td::DialogId(td::UserId(static_cast<td::int64>(700000 + i))) : td::DialogId();
    if (use_external_dialog) {
      ASSERT_TRUE(reply_to_dialog_id.is_valid());
    }
    td::MessageInputReplyTo reply_to(reply_to_message_id, reply_to_dialog_id, td::MessageQuote(), 0, td::string(),
                                     "runtime_light_fuzz");

    auto payload = serialize_modern_draft_with_reply(reply_to);
    auto normalized = parse_and_reserialize_draft(payload.as_slice());
    ASSERT_TRUE(normalized.is_ok());

    auto view = parse_current_stored_draft(normalized.ok().as_slice());
    ASSERT_TRUE(view.is_ok());

    bool has_reply_to_message_id = (view.ok().flags & kHasMessageInputReplyToFlag) != 0;
    bool should_keep_reply_to_message_id =
        reply_to_message_id.is_valid() && (!reply_to_message_id.is_yet_unsent() || reply_to_dialog_id.is_valid());
    ASSERT_EQ(should_keep_reply_to_message_id, has_reply_to_message_id);
  }
}

}  // namespace
