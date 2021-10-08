//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/SponsoredMessageManager.h"

#include "td/telegram/ChannelId.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessageContent.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/net/NetQueryCreator.h"
#include "td/telegram/Td.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"
#include "td/utils/logging.h"
#include "td/utils/Status.h"

#include <limits>

namespace td {

class GetSponsoredMessagesQuery final : public Td::ResultHandler {
  Promise<telegram_api::object_ptr<telegram_api::messages_sponsoredMessages>> promise_;
  ChannelId channel_id_;

 public:
  explicit GetSponsoredMessagesQuery(
      Promise<telegram_api::object_ptr<telegram_api::messages_sponsoredMessages>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(ChannelId channel_id) {
    channel_id_ = channel_id;
    auto input_channel = td->contacts_manager_->get_input_channel(channel_id);
    if (input_channel == nullptr) {
      return promise_.set_error(Status::Error(400, "Chat info not found"));
    }
    send_query(G()->net_query_creator().create(telegram_api::channels_getSponsoredMessages(std::move(input_channel))));
  }

  void on_result(uint64 id, BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::channels_getSponsoredMessages>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    promise_.set_value(result_ptr.move_as_ok());
  }

  void on_error(uint64 id, Status status) final {
    td->contacts_manager_->on_get_channel_error(channel_id_, status, "GetSponsoredMessagesQuery");
    promise_.set_error(std::move(status));
  }
};

class ViewSponsoredMessageQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  ChannelId channel_id_;

 public:
  explicit ViewSponsoredMessageQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(ChannelId channel_id, const string &message_id) {
    channel_id_ = channel_id;
    auto input_channel = td->contacts_manager_->get_input_channel(channel_id);
    if (input_channel == nullptr) {
      return promise_.set_error(Status::Error(400, "Chat info not found"));
    }
    send_query(G()->net_query_creator().create(
        telegram_api::channels_viewSponsoredMessage(std::move(input_channel), BufferSlice(message_id))));
  }

  void on_result(uint64 id, BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::channels_viewSponsoredMessage>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) final {
    td->contacts_manager_->on_get_channel_error(channel_id_, status, "ViewSponsoredMessageQuery");
    promise_.set_error(std::move(status));
  }
};

struct SponsoredMessageManager::SponsoredMessage {
  int32 local_id = 0;
  DialogId sponsor_dialog_id;
  string start_param;
  unique_ptr<MessageContent> content;

  SponsoredMessage() = default;
  SponsoredMessage(int32 local_id, DialogId sponsor_dialog_id, string start_param, unique_ptr<MessageContent> content)
      : local_id(local_id)
      , sponsor_dialog_id(sponsor_dialog_id)
      , start_param(std::move(start_param))
      , content(std::move(content)) {
  }
};

struct SponsoredMessageManager::DialogSponsoredMessages {
  vector<Promise<td_api::object_ptr<td_api::sponsoredMessages>>> promises;
  vector<SponsoredMessage> messages;
  std::unordered_map<int32, string> message_random_ids;
};

SponsoredMessageManager::SponsoredMessageManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
  delete_cached_sponsored_messages_timeout_.set_callback(on_delete_cached_sponsored_messages_timeout_callback);
  delete_cached_sponsored_messages_timeout_.set_callback_data(static_cast<void *>(this));
}

SponsoredMessageManager::~SponsoredMessageManager() = default;

void SponsoredMessageManager::tear_down() {
  parent_.reset();
}

void SponsoredMessageManager::on_delete_cached_sponsored_messages_timeout_callback(void *sponsored_message_manager_ptr,
                                                                                   int64 dialog_id_int) {
  if (G()->close_flag()) {
    return;
  }

  auto sponsored_message_manager = static_cast<SponsoredMessageManager *>(sponsored_message_manager_ptr);
  send_closure_later(sponsored_message_manager->actor_id(sponsored_message_manager),
                     &SponsoredMessageManager::delete_cached_sponsored_messages, DialogId(dialog_id_int));
}

void SponsoredMessageManager::delete_cached_sponsored_messages(DialogId dialog_id) {
  if (G()->close_flag()) {
    return;
  }

  auto it = dialog_sponsored_messages_.find(dialog_id);
  CHECK(it != dialog_sponsored_messages_.end());
  CHECK(it->second->promises.empty());
  dialog_sponsored_messages_.erase(it);
}

td_api::object_ptr<td_api::sponsoredMessage> SponsoredMessageManager::get_sponsored_message_object(
    DialogId dialog_id, const SponsoredMessage &sponsored_message) const {
  td_api::object_ptr<td_api::InternalLinkType> link;
  switch (sponsored_message.sponsor_dialog_id.get_type()) {
    case DialogType::User: {
      auto user_id = sponsored_message.sponsor_dialog_id.get_user_id();
      if (!td_->contacts_manager_->is_user_bot(user_id)) {
        break;
      }
      auto bot_username = td_->contacts_manager_->get_user_username(user_id);
      if (bot_username.empty()) {
        break;
      }
      link = td_api::make_object<td_api::internalLinkTypeBotStart>(bot_username, sponsored_message.start_param);
      break;
    }
    default:
      break;
  }
  return td_api::make_object<td_api::sponsoredMessage>(
      sponsored_message.local_id, sponsored_message.sponsor_dialog_id.get(), std::move(link),
      get_message_content_object(sponsored_message.content.get(), td_, dialog_id, 0, false, true, -1));
}

td_api::object_ptr<td_api::sponsoredMessages> SponsoredMessageManager::get_sponsored_messages_object(
    DialogId dialog_id, const DialogSponsoredMessages &sponsored_messages) const {
  return td_api::make_object<td_api::sponsoredMessages>(
      transform(sponsored_messages.messages, [this, dialog_id](const SponsoredMessage &sponsored_message) {
        return get_sponsored_message_object(dialog_id, sponsored_message);
      }));
}

void SponsoredMessageManager::get_dialog_sponsored_messages(
    DialogId dialog_id, Promise<td_api::object_ptr<td_api::sponsoredMessages>> &&promise) {
  if (!td_->messages_manager_->have_dialog_force(dialog_id, "get_sponsored_messages")) {
    return promise.set_error(Status::Error(400, "Chat not found"));
  }
  if (dialog_id.get_type() != DialogType::Channel ||
      td_->contacts_manager_->get_channel_type(dialog_id.get_channel_id()) != ContactsManager::ChannelType::Broadcast) {
    return promise.set_value(td_api::make_object<td_api::sponsoredMessages>());
  }

  auto &messages = dialog_sponsored_messages_[dialog_id];
  if (messages != nullptr && messages->promises.empty()) {
    return promise.set_value(get_sponsored_messages_object(dialog_id, *messages));
  }

  if (messages == nullptr) {
    messages = make_unique<DialogSponsoredMessages>();
  }
  messages->promises.push_back(std::move(promise));
  if (messages->promises.size() == 1) {
    auto query_promise = PromiseCreator::lambda(
        [actor_id = actor_id(this),
         dialog_id](Result<telegram_api::object_ptr<telegram_api::messages_sponsoredMessages>> &&result) mutable {
          send_closure(actor_id, &SponsoredMessageManager::on_get_dialog_sponsored_messages, dialog_id,
                       std::move(result));
        });
    td_->create_handler<GetSponsoredMessagesQuery>(std::move(query_promise))->send(dialog_id.get_channel_id());
  }
}

void SponsoredMessageManager::on_get_dialog_sponsored_messages(
    DialogId dialog_id, Result<telegram_api::object_ptr<telegram_api::messages_sponsoredMessages>> &&result) {
  auto &messages = dialog_sponsored_messages_[dialog_id];
  CHECK(messages != nullptr);
  auto promises = std::move(messages->promises);
  reset_to_empty(messages->promises);
  CHECK(messages->messages.empty());
  CHECK(messages->message_random_ids.empty());

  if (result.is_ok() && G()->close_flag()) {
    result = Global::request_aborted_error();
  }
  if (result.is_error()) {
    dialog_sponsored_messages_.erase(dialog_id);
    for (auto &promise : promises) {
      promise.set_error(result.error().clone());
    }
    return;
  }

  auto sponsored_messages = result.move_as_ok();

  td_->contacts_manager_->on_get_users(std::move(sponsored_messages->users_), "on_get_dialog_sponsored_messages");
  td_->contacts_manager_->on_get_chats(std::move(sponsored_messages->chats_), "on_get_dialog_sponsored_messages");

  for (auto &sponsored_message : sponsored_messages->messages_) {
    DialogId sponsor_dialog_id(sponsored_message->from_id_);
    if (!sponsor_dialog_id.is_valid() || !td_->messages_manager_->have_dialog_info_force(sponsor_dialog_id)) {
      LOG(ERROR) << "Receive unknown sponsor " << sponsor_dialog_id;
      continue;
    }
    td_->messages_manager_->force_create_dialog(sponsor_dialog_id, "on_get_dialog_sponsored_messages");
    auto message_text = get_message_text(td_->contacts_manager_.get(), std::move(sponsored_message->message_),
                                         std::move(sponsored_message->entities_), true, true, 0, false,
                                         "on_get_dialog_sponsored_messages");
    int32 ttl = 0;
    auto content = get_message_content(td_, std::move(message_text), nullptr, sponsor_dialog_id, true, UserId(), &ttl);
    if (ttl != 0) {
      LOG(ERROR) << "Receive sponsored message with TTL " << ttl;
      continue;
    }

    CHECK(current_sponsored_message_id_ < std::numeric_limits<int32>::max());
    auto local_id = ++current_sponsored_message_id_;
    messages->message_random_ids[local_id] = sponsored_message->random_id_.as_slice().str();
    messages->messages.emplace_back(local_id, sponsor_dialog_id, std::move(sponsored_message->start_param_),
                                    std::move(content));
  }

  for (auto &promise : promises) {
    promise.set_value(get_sponsored_messages_object(dialog_id, *messages));
  }
  delete_cached_sponsored_messages_timeout_.set_timeout_in(dialog_id.get(), 300.0);
}

void SponsoredMessageManager::view_sponsored_message(DialogId dialog_id, int32 sponsored_message_id,
                                                     Promise<Unit> &&promise) {
  if (!td_->messages_manager_->have_dialog_force(dialog_id, "view_sponsored_message")) {
    return promise.set_error(Status::Error(400, "Chat not found"));
  }

  auto it = dialog_sponsored_messages_.find(dialog_id);
  if (it == dialog_sponsored_messages_.end()) {
    return promise.set_value(Unit());
  }
  auto random_id_it = it->second->message_random_ids.find(sponsored_message_id);
  if (random_id_it == it->second->message_random_ids.end()) {
    return promise.set_value(Unit());
  }

  auto random_id = std::move(random_id_it->second);
  it->second->message_random_ids.erase(random_id_it);
  td_->create_handler<ViewSponsoredMessageQuery>(std::move(promise))->send(dialog_id.get_channel_id(), random_id);
}

}  // namespace td
