//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/DialogOnlineMemberManager.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/DialogManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/Td.h"

#include "td/utils/buffer.h"
#include "td/utils/logging.h"
#include "td/utils/Status.h"
#include "td/utils/Time.h"

namespace td {

class GetOnlinesQuery final : public Td::ResultHandler {
  DialogId dialog_id_;

 public:
  void send(DialogId dialog_id) {
    dialog_id_ = dialog_id;
    CHECK(dialog_id.get_type() == DialogType::Channel);
    auto input_peer = td_->dialog_manager_->get_input_peer(dialog_id, AccessRights::Read);
    if (input_peer == nullptr) {
      return on_error(Status::Error(400, "Can't access the chat"));
    }

    send_query(G()->net_query_creator().create(telegram_api::messages_getOnlines(std::move(input_peer))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_getOnlines>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    td_->dialog_online_member_manager_->on_update_dialog_online_member_count(dialog_id_, result->onlines_, true);
  }

  void on_error(Status status) final {
    td_->dialog_manager_->on_get_dialog_error(dialog_id_, status, "GetOnlinesQuery");
    td_->dialog_online_member_manager_->on_update_dialog_online_member_count(dialog_id_, 0, true);
  }
};

DialogOnlineMemberManager::DialogOnlineMemberManager(Td *td, ActorShared<> parent)
    : td_(td), parent_(std::move(parent)) {
  update_dialog_online_member_count_timeout_.set_callback(on_update_dialog_online_member_count_timeout_callback);
  update_dialog_online_member_count_timeout_.set_callback_data(static_cast<void *>(this));
}

void DialogOnlineMemberManager::tear_down() {
  parent_.reset();
}

void DialogOnlineMemberManager::on_update_dialog_online_member_count_timeout_callback(
    void *dialog_online_member_manager_ptr, int64 dialog_id_int) {
  if (G()->close_flag()) {
    return;
  }

  auto dialog_online_member_manager = static_cast<DialogOnlineMemberManager *>(dialog_online_member_manager_ptr);
  send_closure_later(dialog_online_member_manager->actor_id(dialog_online_member_manager),
                     &DialogOnlineMemberManager::on_update_dialog_online_member_count_timeout, DialogId(dialog_id_int));
}

void DialogOnlineMemberManager::on_update_dialog_online_member_count_timeout(DialogId dialog_id) {
  if (G()->close_flag()) {
    return;
  }

  LOG(INFO) << "Expired timeout for number of online members in " << dialog_id;
  bool is_open = td_->messages_manager_->is_dialog_opened(dialog_id);
  if (!is_open) {
    send_update_chat_online_member_count(dialog_id, 0);
    return;
  }

  if (dialog_id.get_type() == DialogType::Channel && !td_->dialog_manager_->is_broadcast_channel(dialog_id)) {
    auto participant_count = td_->contacts_manager_->get_channel_participant_count(dialog_id.get_channel_id());
    auto has_hidden_participants = td_->contacts_manager_->get_channel_effective_has_hidden_participants(
        dialog_id.get_channel_id(), "on_update_dialog_online_member_count_timeout");
    if (participant_count == 0 || participant_count >= 195 || has_hidden_participants) {
      td_->create_handler<GetOnlinesQuery>()->send(dialog_id);
    } else {
      td_->contacts_manager_->get_channel_participants(dialog_id.get_channel_id(),
                                                       td_api::make_object<td_api::supergroupMembersFilterRecent>(),
                                                       string(), 0, 200, 200, Auto());
    }
    return;
  }
  if (dialog_id.get_type() == DialogType::Chat) {
    // we need actual online status state, so we need to reget chat participants
    td_->contacts_manager_->repair_chat_participants(dialog_id.get_chat_id());
    return;
  }
}

void DialogOnlineMemberManager::on_update_dialog_online_member_count(DialogId dialog_id, int32 online_member_count,
                                                                     bool is_from_server) {
  if (td_->auth_manager_->is_bot()) {
    return;
  }

  if (!dialog_id.is_valid()) {
    LOG(ERROR) << "Receive number of online members in invalid " << dialog_id;
    return;
  }

  if (td_->dialog_manager_->is_broadcast_channel(dialog_id)) {
    LOG_IF(ERROR, online_member_count != 0)
        << "Receive " << online_member_count << " as a number of online members in a channel " << dialog_id;
    return;
  }

  if (online_member_count < 0) {
    LOG(ERROR) << "Receive " << online_member_count << " as a number of online members in a " << dialog_id;
    return;
  }

  set_dialog_online_member_count(dialog_id, online_member_count, is_from_server,
                                 "on_update_channel_online_member_count");
}

void DialogOnlineMemberManager::on_dialog_opened(DialogId dialog_id) {
  auto online_count_it = dialog_online_member_counts_.find(dialog_id);
  if (online_count_it == dialog_online_member_counts_.end()) {
    return;
  }
  auto &info = online_count_it->second;
  CHECK(!info.is_update_sent);
  if (Time::now() - info.update_time < ONLINE_MEMBER_COUNT_CACHE_EXPIRE_TIME) {
    info.is_update_sent = true;
    send_update_chat_online_member_count(dialog_id, info.online_member_count);
  }
}

void DialogOnlineMemberManager::on_dialog_closed(DialogId dialog_id) {
  auto online_count_it = dialog_online_member_counts_.find(dialog_id);
  if (online_count_it != dialog_online_member_counts_.end()) {
    auto &info = online_count_it->second;
    info.is_update_sent = false;
  }
  update_dialog_online_member_count_timeout_.set_timeout_in(dialog_id.get(), ONLINE_MEMBER_COUNT_CACHE_EXPIRE_TIME);
}

void DialogOnlineMemberManager::set_dialog_online_member_count(DialogId dialog_id, int32 online_member_count,
                                                               bool is_from_server, const char *source) {
  if (td_->auth_manager_->is_bot()) {
    return;
  }

  if (online_member_count < 0) {
    LOG(ERROR) << "Receive online_member_count = " << online_member_count << " in " << dialog_id;
    online_member_count = 0;
  }

  switch (dialog_id.get_type()) {
    case DialogType::Chat: {
      auto participant_count = td_->contacts_manager_->get_chat_participant_count(dialog_id.get_chat_id());
      if (online_member_count > participant_count) {
        online_member_count = participant_count;
      }
      break;
    }
    case DialogType::Channel: {
      auto participant_count = td_->contacts_manager_->get_channel_participant_count(dialog_id.get_channel_id());
      if (participant_count != 0 && online_member_count > participant_count) {
        online_member_count = participant_count;
      }
      break;
    }
    default:
      break;
  }

  bool is_open = td_->messages_manager_->is_dialog_opened(dialog_id);
  auto &info = dialog_online_member_counts_[dialog_id];
  LOG(INFO) << "Change number of online members from " << info.online_member_count << " to " << online_member_count
            << " in " << dialog_id << " from " << source;
  bool need_update = is_open && (!info.is_update_sent || info.online_member_count != online_member_count);
  info.online_member_count = online_member_count;
  info.update_time = Time::now();

  if (need_update) {
    info.is_update_sent = true;
    send_update_chat_online_member_count(dialog_id, online_member_count);
  }
  if (is_open) {
    if (is_from_server) {
      update_dialog_online_member_count_timeout_.set_timeout_in(dialog_id.get(), ONLINE_MEMBER_COUNT_UPDATE_TIME);
    } else {
      update_dialog_online_member_count_timeout_.add_timeout_in(dialog_id.get(), ONLINE_MEMBER_COUNT_UPDATE_TIME);
    }
  }
}

void DialogOnlineMemberManager::send_update_chat_online_member_count(DialogId dialog_id,
                                                                     int32 online_member_count) const {
  if (td_->auth_manager_->is_bot()) {
    return;
  }

  send_closure(
      G()->td(), &Td::send_update,
      td_api::make_object<td_api::updateChatOnlineMemberCount>(
          td_->dialog_manager_->get_chat_id_object(dialog_id, "updateChatOnlineMemberCount"), online_member_count));
}

void DialogOnlineMemberManager::get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const {
  for (const auto &it : dialog_online_member_counts_) {
    auto dialog_id = it.first;
    if (it.second.is_update_sent && td_->messages_manager_->is_dialog_opened(dialog_id)) {
      updates.push_back(td_api::make_object<td_api::updateChatOnlineMemberCount>(
          td_->dialog_manager_->get_chat_id_object(dialog_id, "updateChatOnlineMemberCount"),
          it.second.online_member_count));
    }
  }
}

}  // namespace td
