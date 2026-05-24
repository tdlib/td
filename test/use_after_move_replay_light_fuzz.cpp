// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

#include <array>
#include <random>

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
  std::size_t source_index;
  const char *required;
  const char *forbidden;
};

TEST(Wave2UseAfterMoveLightFuzz, ContractsRemainStableUnderRandomizedProbeOrder) {
  std::array<td::string, 5> sources = {
      normalize_for_contract(td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp")),
      normalize_for_contract(td::mtproto::test::read_repo_text_file("td/telegram/CommonDialogManager.cpp")),
      normalize_for_contract(td::mtproto::test::read_repo_text_file("td/telegram/DialogManager.cpp")),
      normalize_for_contract(td::mtproto::test::read_repo_text_file("td/telegram/StickersManager.cpp")),
      normalize_for_contract(td::mtproto::test::read_repo_text_file("td/telegram/SecretChatActor.cpp")),
  };

  const std::array<Probe, 8> probes = {
      Probe{0, "add_message_to_dialog(to_dialog,std::move(message),false,true,&need_update,&need_update_dialog_pos,"
                "\"ForwardMessagesLogEvent\"));",
            "\"forwardmessageagain\""},
      Probe{0, "add_message_to_dialog(d,std::move(message),false,true,&need_update,&need_update_dialog_pos,"
                "\"SendQuickReplyShortcutMessagesLogEvent\"));",
            "\"sendquickreplyshortcutmessageagain\""},
      Probe{1, "autototal_count=narrow_cast<int32>(chats->chats_.size());",
            "on_get_common_dialogs(user_id_,offset_chat_id_,std::move(chats->chats_),"
            "narrow_cast<int32>(chats->chats_.size()));"},
      Probe{2, "autototal_count=narrow_cast<int32>(blocked_peers->blocked_.size());",
            "on_get_blocked_dialogs(offset_,limit_,narrow_cast<int32>(blocked_peers->blocked_.size()),"
            "std::move(blocked_peers->blocked_),std::move(promise_));"},
      Probe{0, "autototal_count=narrow_cast<int32>(dialogs->dialogs_.size());",
            "on_get_dialogs(folder_id_,std::move(dialogs->dialogs_),narrow_cast<int32>(dialogs->dialogs_.size()),"
            "std::move(dialogs->messages_),std::move(promise_));"},
      Probe{3, "if(file_type==FileType::Sticker){returnget_input_media(file_upload_id.get_file_id(),"
                "std::move(input_file),nullptr,string());}",
            "autoinput_media=[&]{returnfile_type==FileType::Sticker?"},
      Probe{4, "autois_expected=(error.code()==400&&error.message()==\"ENCRYPTION_DECLINED\")||error.code()==403;",
            "returnon_fatal_error(std::move(error),(error.code()==400&&error.message()==\"ENCRYPTION_DECLINED\")"
            "||error.code()==403);"},
      Probe{4, "returnon_fatal_error(std::move(error),is_expected);", ""},
  };

  std::mt19937_64 rng(0x5340472B0ULL);
  std::uniform_int_distribution<std::size_t> probe_dist(0, probes.size() - 1);

  constexpr td::int32 kIterations = 10000;
  for (td::int32 i = 0; i < kIterations; ++i) {
    const auto &probe = probes[probe_dist(rng)];
    const auto &source = sources[probe.source_index];

    ASSERT_TRUE(source.find(probe.required) != td::string::npos);
    if (probe.forbidden[0] != '\0') {
      ASSERT_TRUE(source.find(probe.forbidden) == td::string::npos);
    }
  }
}

}  // namespace
