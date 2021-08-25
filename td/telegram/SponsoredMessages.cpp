//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/SponsoredMessages.h"

#include "td/telegram/ContactsManager.h"
#include "td/telegram/MessageContent.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/Td.h"

namespace td {

class GetSponsoredMessagesQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::sponsoredMessages>> promise_;
  ChannelId channel_id_;

 public:
  explicit GetSponsoredMessagesQuery(Promise<td_api::object_ptr<td_api::sponsoredMessages>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(ChannelId channel_id) {
    channel_id_ = channel_id;
    auto input_channel = td->contacts_manager_->get_input_channel(channel_id);
    if (input_channel == nullptr) {
      return promise_.set_error(Status::Error(3, "Chat info not found"));
    }
    send_query(G()->net_query_creator().create(telegram_api::channels_getSponsoredMessages(std::move(input_channel))));
  }

  void on_result(uint64 id, BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::channels_getSponsoredMessages>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto sponsored_messages = result_ptr.move_as_ok();

    td->contacts_manager_->on_get_users(std::move(sponsored_messages->users_), "GetSponsoredMessagesQuery");
    td->contacts_manager_->on_get_chats(std::move(sponsored_messages->chats_), "GetSponsoredMessagesQuery");

    vector<td_api::object_ptr<td_api::sponsoredMessage>> messages;
    for (auto &sponsored_message : sponsored_messages->messages_) {
      DialogId dialog_id(sponsored_message->from_id_);
      if (!dialog_id.is_valid() || !td->messages_manager_->have_dialog_info_force(dialog_id)) {
        LOG(ERROR) << "Receive unknown sponsor " << dialog_id;
        continue;
      }
      td->messages_manager_->force_create_dialog(dialog_id, "GetSponsoredMessagesQuery");
      auto message_text =
          get_message_text(td->contacts_manager_.get(), std::move(sponsored_message->message_),
                           std::move(sponsored_message->entities_), true, true, 0, false, "GetSponsoredMessagesQuery");
      int32 ttl = 0;
      auto content = get_message_content(td, std::move(message_text), nullptr, dialog_id, true, UserId(), &ttl);
      if (ttl != 0) {
        LOG(ERROR) << "Receive sponsored message with TTL " << ttl;
        continue;
      }

      messages.push_back(td_api::make_object<td_api::sponsoredMessage>(
          sponsored_message->random_id_.as_slice().str(), dialog_id.get(), sponsored_message->start_param_,
          get_message_content_object(content.get(), td, DialogId(channel_id_), 0, false, true, -1)));
    }

    promise_.set_value(td_api::make_object<td_api::sponsoredMessages>(std::move(messages)));
  }

  void on_error(uint64 id, Status status) final {
    td->contacts_manager_->on_get_channel_error(channel_id_, status, "GetSponsoredMessagesQuery");
    promise_.set_error(std::move(status));
  }
};

void get_dialog_sponsored_messages(Td *td, DialogId dialog_id,
                                   Promise<td_api::object_ptr<td_api::sponsoredMessages>> &&promise) {
  if (!td->messages_manager_->have_dialog_force(dialog_id, "get_sponsored_messages")) {
    return promise.set_error(Status::Error(400, "Chat not found"));
  }
  if (dialog_id.get_type() != DialogType::Channel ||
      td->contacts_manager_->get_channel_type(dialog_id.get_channel_id()) != ContactsManager::ChannelType::Broadcast) {
    return promise.set_value(td_api::make_object<td_api::sponsoredMessages>());
  }

  td->create_handler<GetSponsoredMessagesQuery>(std::move(promise))->send(dialog_id.get_channel_id());
}

}  // namespace td
