// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/common.h"
#include "td/utils/Random.h"
#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

#include <array>

namespace {

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

struct Probe {
  size_t source_index;
  td::Slice snippet;
};

std::array<td::string, 3> load_normalized_sources() {
  const std::array<td::Slice, 3> source_paths = {
      "td/telegram/DraftMessage.hpp",
      "td/telegram/MessagesManager.cpp",
      "td/telegram/DialogManager.cpp",
  };

  std::array<td::string, source_paths.size()> normalized_sources;
  for (size_t i = 0; i < source_paths.size(); ++i) {
    normalized_sources[i] = normalize_for_contract(td::mtproto::test::read_repo_text_file(source_paths[i]));
  }
  return normalized_sources;
}

}  // namespace

TEST(W2BReplyAndUsernameLightFuzz, RandomizedProbeOrderKeepsRequiredGuardPatternsPinned) {
  auto normalized_sources = load_normalized_sources();

  const std::array<Probe, 15> required = {
      Probe{0, "autoclear_same_chat_yet_unsent_reply=[this](){"},
      Probe{0, "automessage_id=message_input_reply_to_.get_same_chat_reply_to_message_id();"},
      Probe{0,
            "if((message_id.is_valid()||message_id.is_valid_scheduled())&&message_id.is_yet_unsent())"
            "{message_input_reply_to_={};}"},
      Probe{0,
            "message_input_reply_to_=MessageInputReplyTo::regular(legacy_reply_to_message_id);"
            "clear_same_chat_yet_unsent_reply();"},
      Probe{0,
            "if(has_message_input_reply_to){td::parse(message_input_reply_to_,parser);"
            "clear_same_chat_yet_unsent_reply();}"},
      Probe{1, "if(!can_reply_to_message(d,message_id,m)){message_id={};m=nullptr;}"},
      Probe{1,
            "if(m==nullptr){if(message_id.is_server()&&d->dialog_id.get_type()!=DialogType::SecretChat&&"
            "d->last_new_message_id.is_valid()&&message_id>d->last_new_message_id&&"
            "(d->notification_info!=nullptr&&"
            "message_id<=d->notification_info->max_push_notification_message_id_)){"},
      Probe{1,
            "if((message_id.is_valid()||message_id.is_valid_scheduled())&&!message_id.is_local()&&"
            "(is_message_forward(m)||!message_id.is_yet_unsent())){"},
      Probe{1, "update_message_reply_to_message_id(d,m,message_id,false,\"restore_message_reply_to_message_id\");"},
      Probe{
          1,
          "autoimplicit_reply_to_message_id=get_message_topic(d->dialog_id,m).get_implicit_reply_to_message_id(td_);"},
      Probe{1, "set_message_reply(d,m,MessageInputReplyTo::regular(implicit_reply_to_message_id),false);"},
      Probe{2,
            "if(error.message()==\"USERNAME_OCCUPIED\"){returnpromise.set_value("
            "CheckDialogUsernameResult::Occupied);}"},
      Probe{2,
            "if(error.message()==\"USERNAME_PURCHASE_AVAILABLE\"){returnpromise.set_value("
            "CheckDialogUsernameResult::Purchasable);}"},
      Probe{2, "returnpromise.set_error(std::move(error));"},
      Probe{2, "promise.set_value(result.ok()?CheckDialogUsernameResult::Ok:CheckDialogUsernameResult::Occupied);"},
  };

  for (int i = 0; i < 12000; ++i) {
    auto idx = static_cast<size_t>(td::Random::fast(0, static_cast<int>(required.size()) - 1));
    const auto &probe = required[idx];
    ASSERT_NE(td::string::npos, normalized_sources[probe.source_index].find(probe.snippet.str()));
  }
}

TEST(W2BReplyAndUsernameLightFuzz, RandomizedProbeOrderKeepsLegacyUnsafePatternsAbsent) {
  auto normalized_sources = load_normalized_sources();

  const std::array<Probe, 13> forbidden = {
      Probe{0, "if(has_message_input_reply_to){td::parse(message_input_reply_to_,parser);}if(has_local_content){"},
      Probe{0, "if(message_id.is_valid()&&message_id.is_yet_unsent()){message_input_reply_to_={};}"},
      Probe{1, "if(!can_reply_to_message(d,message_id,m)){message_id={};}"},
      Probe{1, "if(!can_reply_to_message(d,message_id,m)){m=nullptr;}"},
      Probe{1, "if(!can_reply_to_message(d,message_id,m)){message_id={};}if(m==nullptr){if(message_id.is_server()"},
      Probe{1, "if((message_id.is_valid()||message_id.is_valid_scheduled())&&!message_id.is_local()){"},
      Probe{
          1,
          "update_message_reply_to_message_id(d,m,replied_message_id,false,\"restore_message_reply_to_message_id\");"},
      Probe{1, "set_message_reply(d,m,MessageInputReplyTo::regular(message_id),false);"},
      Probe{1, "set_message_reply(d,m,MessageInputReplyTo::regular(replied_message_id),false);"},
      Probe{2, R"(begins_with(G()->get_option_string("my_phone_number"),"1"))"},
      Probe{2, "if(error.message()==\"USERNAME_PURCHASE_AVAILABLE\"){returnpromise.set_error(std::move(error));}"},
      Probe{2, "promise.set_value(result.ok()?CheckDialogUsernameResult::Ok:CheckDialogUsernameResult::Purchasable);"},
      Probe{2, "promise.set_value(result.ok()?CheckDialogUsernameResult::Ok:CheckDialogUsernameResult::Invalid);"},
  };

  for (int i = 0; i < 12000; ++i) {
    auto idx = static_cast<size_t>(td::Random::fast(0, static_cast<int>(forbidden.size()) - 1));
    const auto &probe = forbidden[idx];
    ASSERT_EQ(td::string::npos, normalized_sources[probe.source_index].find(probe.snippet.str()));
  }
}
