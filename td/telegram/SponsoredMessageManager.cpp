//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/SponsoredMessageManager.h"

#include "td/telegram/ChannelId.h"
#include "td/telegram/ConfigShared.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/LinkManager.h"
#include "td/telegram/MessageContent.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/net/NetQueryCreator.h"
#include "td/telegram/ServerMessageId.h"
#include "td/telegram/Td.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"
#include "td/utils/logging.h"
#include "td/utils/Random.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/Status.h"

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
    auto input_channel = td_->contacts_manager_->get_input_channel(channel_id);
    if (input_channel == nullptr) {
      return promise_.set_error(Status::Error(400, "Chat info not found"));
    }
    send_query(G()->net_query_creator().create(telegram_api::channels_getSponsoredMessages(std::move(input_channel))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::channels_getSponsoredMessages>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    promise_.set_value(result_ptr.move_as_ok());
  }

  void on_error(Status status) final {
    td_->contacts_manager_->on_get_channel_error(channel_id_, status, "GetSponsoredMessagesQuery");
    promise_.set_error(std::move(status));
  }
};

class ViewSponsoredMessageQuery final : public Td::ResultHandler {
  ChannelId channel_id_;

 public:
  void send(ChannelId channel_id, const string &message_id) {
    channel_id_ = channel_id;
    auto input_channel = td_->contacts_manager_->get_input_channel(channel_id);
    if (input_channel == nullptr) {
      return;
    }
    send_query(G()->net_query_creator().create(
        telegram_api::channels_viewSponsoredMessage(std::move(input_channel), BufferSlice(message_id))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::channels_viewSponsoredMessage>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }
  }

  void on_error(Status status) final {
    td_->contacts_manager_->on_get_channel_error(channel_id_, status, "ViewSponsoredMessageQuery");
  }
};

struct SponsoredMessageManager::SponsoredMessage {
  int64 local_id = 0;
  DialogId sponsor_dialog_id;
  ServerMessageId server_message_id;
  string start_param;
  string invite_hash;
  unique_ptr<MessageContent> content;

  SponsoredMessage() = default;
  SponsoredMessage(int64 local_id, DialogId sponsor_dialog_id, ServerMessageId server_message_id, string start_param,
                   string invite_hash, unique_ptr<MessageContent> content)
      : local_id(local_id)
      , sponsor_dialog_id(sponsor_dialog_id)
      , server_message_id(server_message_id)
      , start_param(std::move(start_param))
      , invite_hash(std::move(invite_hash))
      , content(std::move(content)) {
  }
};

struct SponsoredMessageManager::DialogSponsoredMessages {
  vector<Promise<td_api::object_ptr<td_api::sponsoredMessage>>> promises;
  vector<SponsoredMessage> messages;
  FlatHashMap<int64, string> message_random_ids;
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
  td_api::object_ptr<td_api::chatInviteLinkInfo> chat_invite_link_info;
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
    case DialogType::Channel:
      if (sponsored_message.server_message_id.is_valid()) {
        auto channel_id = sponsored_message.sponsor_dialog_id.get_channel_id();
        auto t_me = G()->shared_config().get_option_string("t_me_url", "https://t.me/");
        link = td_api::make_object<td_api::internalLinkTypeMessage>(
            PSTRING() << t_me << "c/" << channel_id.get() << '/' << sponsored_message.server_message_id.get());
      }
      break;
    case DialogType::None: {
      CHECK(!sponsored_message.invite_hash.empty());
      auto invite_link = LinkManager::get_dialog_invite_link(sponsored_message.invite_hash, false);
      chat_invite_link_info = td_->contacts_manager_->get_chat_invite_link_info_object(invite_link);
      if (chat_invite_link_info == nullptr) {
        LOG(ERROR) << "Failed to get invite link info for " << invite_link;
        return nullptr;
      }
      link = td_api::make_object<td_api::internalLinkTypeChatInvite>(
          LinkManager::get_dialog_invite_link(sponsored_message.invite_hash, true));
    }
    default:
      break;
  }
  return td_api::make_object<td_api::sponsoredMessage>(
      sponsored_message.local_id, sponsored_message.sponsor_dialog_id.get(), std::move(chat_invite_link_info),
      std::move(link), get_message_content_object(sponsored_message.content.get(), td_, dialog_id, 0, false, true, -1));
}

td_api::object_ptr<td_api::sponsoredMessage> SponsoredMessageManager::get_sponsored_message_object(
    DialogId dialog_id, const DialogSponsoredMessages &sponsored_messages) const {
  if (sponsored_messages.messages.empty()) {
    return nullptr;
  }
  auto pos = Random::fast(0, static_cast<int>(sponsored_messages.messages.size()) - 1);
  return get_sponsored_message_object(dialog_id, sponsored_messages.messages[pos]);
}

void SponsoredMessageManager::get_dialog_sponsored_message(
    DialogId dialog_id, Promise<td_api::object_ptr<td_api::sponsoredMessage>> &&promise) {
  if (!td_->messages_manager_->have_dialog_force(dialog_id, "get_dialog_sponsored_message")) {
    return promise.set_error(Status::Error(400, "Chat not found"));
  }
  if (dialog_id.get_type() != DialogType::Channel ||
      td_->contacts_manager_->get_channel_type(dialog_id.get_channel_id()) != ContactsManager::ChannelType::Broadcast) {
    return promise.set_value(nullptr);
  }

  auto &messages = dialog_sponsored_messages_[dialog_id];
  if (messages != nullptr && messages->promises.empty()) {
    return promise.set_value(get_sponsored_message_object(dialog_id, *messages));
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
    DialogId sponsor_dialog_id;
    ServerMessageId server_message_id;
    string invite_hash;
    if (sponsored_message->from_id_ != nullptr) {
      sponsor_dialog_id = DialogId(sponsored_message->from_id_);
      if (!sponsor_dialog_id.is_valid() || !td_->messages_manager_->have_dialog_info_force(sponsor_dialog_id)) {
        LOG(ERROR) << "Receive unknown sponsor " << sponsor_dialog_id;
        continue;
      }
      server_message_id = ServerMessageId(sponsored_message->channel_post_);
      if (!server_message_id.is_valid() && server_message_id != ServerMessageId()) {
        LOG(ERROR) << "Receive invalid channel post in " << to_string(sponsored_message);
        server_message_id = ServerMessageId();
      }
      td_->messages_manager_->force_create_dialog(sponsor_dialog_id, "on_get_dialog_sponsored_messages");
    } else if (sponsored_message->chat_invite_ != nullptr && !sponsored_message->chat_invite_hash_.empty()) {
      auto invite_link = LinkManager::get_dialog_invite_link(sponsored_message->chat_invite_hash_, false);
      if (invite_link.empty()) {
        LOG(ERROR) << "Receive invalid invite link hash in " << to_string(sponsored_message);
        continue;
      }
      auto chat_invite = to_string(sponsored_message->chat_invite_);
      td_->contacts_manager_->on_get_dialog_invite_link_info(invite_link, std::move(sponsored_message->chat_invite_),
                                                             Promise<Unit>());
      auto chat_invite_link_info = td_->contacts_manager_->get_chat_invite_link_info_object(invite_link);
      if (chat_invite_link_info == nullptr) {
        LOG(ERROR) << "Failed to get invite link info from " << chat_invite << " for " << to_string(sponsored_message);
        continue;
      }
      invite_hash = std::move(sponsored_message->chat_invite_hash_);
    } else {
      LOG(ERROR) << "Receive " << to_string(sponsored_message);
      continue;
    }

    auto message_text = get_message_text(td_->contacts_manager_.get(), std::move(sponsored_message->message_),
                                         std::move(sponsored_message->entities_), true, true, 0, false,
                                         "on_get_dialog_sponsored_messages");
    int32 ttl = 0;
    bool disable_web_page_preview = false;
    auto content = get_message_content(td_, std::move(message_text), nullptr, sponsor_dialog_id, true, UserId(), &ttl,
                                       &disable_web_page_preview);
    if (ttl != 0) {
      LOG(ERROR) << "Receive sponsored message with TTL " << ttl;
      continue;
    }
    CHECK(disable_web_page_preview);

    current_sponsored_message_id_ = current_sponsored_message_id_.get_next_message_id(MessageType::Local);
    if (!current_sponsored_message_id_.is_valid_sponsored()) {
      LOG(ERROR) << "Sponsored message ID overflowed";
      current_sponsored_message_id_ = MessageId::max().get_next_message_id(MessageType::Local);
      CHECK(current_sponsored_message_id_.is_valid_sponsored());
    }
    auto local_id = current_sponsored_message_id_.get();
    CHECK(!current_sponsored_message_id_.is_valid());
    CHECK(!current_sponsored_message_id_.is_scheduled());
    CHECK(messages->message_random_ids.count(local_id) == 0);
    messages->message_random_ids[local_id] = sponsored_message->random_id_.as_slice().str();
    messages->messages.emplace_back(local_id, sponsor_dialog_id, server_message_id,
                                    std::move(sponsored_message->start_param_), std::move(invite_hash),
                                    std::move(content));
  }

  for (auto &promise : promises) {
    promise.set_value(get_sponsored_message_object(dialog_id, *messages));
  }
  delete_cached_sponsored_messages_timeout_.set_timeout_in(dialog_id.get(), 300.0);
}

void SponsoredMessageManager::view_sponsored_message(DialogId dialog_id, MessageId sponsored_message_id) {
  auto it = dialog_sponsored_messages_.find(dialog_id);
  if (it == dialog_sponsored_messages_.end()) {
    return;
  }
  auto random_id_it = it->second->message_random_ids.find(sponsored_message_id.get());
  if (random_id_it == it->second->message_random_ids.end()) {
    return;
  }

  auto random_id = std::move(random_id_it->second);
  it->second->message_random_ids.erase(random_id_it);
  td_->create_handler<ViewSponsoredMessageQuery>()->send(dialog_id.get_channel_id(), random_id);
}

}  // namespace td
