//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/SponsoredMessageManager.h"

#include "td/telegram/ChannelId.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/LinkManager.h"
#include "td/telegram/MessageContent.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/net/NetQueryCreator.h"
#include "td/telegram/OptionManager.h"
#include "td/telegram/Photo.h"
#include "td/telegram/ServerMessageId.h"
#include "td/telegram/Td.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UserId.h"

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"
#include "td/utils/logging.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/Status.h"

namespace td {

class GetSponsoredMessagesQuery final : public Td::ResultHandler {
  Promise<telegram_api::object_ptr<telegram_api::messages_SponsoredMessages>> promise_;
  ChannelId channel_id_;

 public:
  explicit GetSponsoredMessagesQuery(
      Promise<telegram_api::object_ptr<telegram_api::messages_SponsoredMessages>> &&promise)
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

    auto ptr = result_ptr.move_as_ok();
    LOG(DEBUG) << "Receive result for GetSponsoredMessagesQuery: " << to_string(ptr);
    promise_.set_value(std::move(ptr));
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

class ClickSponsoredMessageQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  ChannelId channel_id_;

 public:
  explicit ClickSponsoredMessageQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(ChannelId channel_id, const string &message_id) {
    channel_id_ = channel_id;
    auto input_channel = td_->contacts_manager_->get_input_channel(channel_id);
    if (input_channel == nullptr) {
      return promise_.set_value(Unit());
    }
    send_query(G()->net_query_creator().create(
        telegram_api::channels_clickSponsoredMessage(std::move(input_channel), BufferSlice(message_id))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::channels_clickSponsoredMessage>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }
    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    td_->contacts_manager_->on_get_channel_error(channel_id_, status, "ClickSponsoredMessageQuery");
    promise_.set_error(std::move(status));
  }
};

struct SponsoredMessageManager::SponsoredMessage {
  int64 local_id = 0;
  bool is_recommended = false;
  bool show_dialog_photo = false;
  DialogId sponsor_dialog_id;
  ServerMessageId server_message_id;
  string start_param;
  string invite_hash;
  unique_ptr<MessageContent> content;
  string sponsor_info;
  string additional_info;
  string site_url;
  string site_name;
  DialogPhoto site_photo;

  SponsoredMessage(int64 local_id, bool is_recommended, bool show_dialog_photo, DialogId sponsor_dialog_id,
                   ServerMessageId server_message_id, string start_param, string invite_hash,
                   unique_ptr<MessageContent> content, string sponsor_info, string additional_info, string site_url,
                   string site_name, DialogPhoto site_photo)
      : local_id(local_id)
      , is_recommended(is_recommended)
      , show_dialog_photo(show_dialog_photo)
      , sponsor_dialog_id(sponsor_dialog_id)
      , server_message_id(server_message_id)
      , start_param(std::move(start_param))
      , invite_hash(std::move(invite_hash))
      , content(std::move(content))
      , sponsor_info(std::move(sponsor_info))
      , additional_info(std::move(additional_info))
      , site_url(std::move(site_url))
      , site_name(std::move(site_name))
      , site_photo(std::move(site_photo)) {
  }
};

struct SponsoredMessageManager::SponsoredMessageInfo {
  string random_id_;
  bool is_viewed_ = false;
  bool is_clicked_ = false;
};

struct SponsoredMessageManager::DialogSponsoredMessages {
  vector<Promise<td_api::object_ptr<td_api::sponsoredMessages>>> promises;
  vector<SponsoredMessage> messages;
  FlatHashMap<int64, SponsoredMessageInfo> message_infos;
  int32 messages_between = 0;
  bool is_premium = false;
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
  if (it != dialog_sponsored_messages_.end() && it->second->promises.empty()) {
    dialog_sponsored_messages_.erase(it);
  }
}

td_api::object_ptr<td_api::messageSponsor> SponsoredMessageManager::get_message_sponsor_object(
    const SponsoredMessage &sponsored_message) const {
  td_api::object_ptr<td_api::MessageSponsorType> type;
  td_api::object_ptr<td_api::chatPhotoInfo> photo;
  switch (sponsored_message.sponsor_dialog_id.get_type()) {
    case DialogType::User: {
      auto user_id = sponsored_message.sponsor_dialog_id.get_user_id();
      if (!td_->contacts_manager_->is_user_bot(user_id)) {
        LOG(ERROR) << "Sponsor " << user_id << " is not a bot";
        return nullptr;
      }
      auto bot_username = td_->contacts_manager_->get_user_first_username(user_id);
      if (bot_username.empty()) {
        LOG(ERROR) << "Sponsor " << user_id << " has no username";
        return nullptr;
      }
      type = td_api::make_object<td_api::messageSponsorTypeBot>(
          td_->contacts_manager_->get_user_id_object(user_id, "messageSponsorTypeBot"),
          td_api::make_object<td_api::internalLinkTypeBotStart>(bot_username, sponsored_message.start_param, false));
      if (sponsored_message.show_dialog_photo) {
        photo = get_chat_photo_info_object(td_->file_manager_.get(),
                                           td_->contacts_manager_->get_user_dialog_photo(user_id));
      }
      break;
    }
    case DialogType::Channel: {
      auto channel_id = sponsored_message.sponsor_dialog_id.get_channel_id();
      if (!td_->contacts_manager_->is_broadcast_channel(channel_id)) {
        LOG(ERROR) << "Sponsor " << channel_id << " is not a channel";
        return nullptr;
      }
      td_api::object_ptr<td_api::InternalLinkType> link;
      if (sponsored_message.server_message_id.is_valid()) {
        link = td_api::make_object<td_api::internalLinkTypeMessage>(
            PSTRING() << LinkManager::get_t_me_url() << "c/" << channel_id.get() << '/'
                      << sponsored_message.server_message_id.get());
      }
      type = td_api::make_object<td_api::messageSponsorTypePublicChannel>(
          td_->messages_manager_->get_chat_id_object(sponsored_message.sponsor_dialog_id, "sponsoredMessage"),
          std::move(link));
      if (sponsored_message.show_dialog_photo) {
        photo = get_chat_photo_info_object(td_->file_manager_.get(),
                                           td_->contacts_manager_->get_channel_dialog_photo(channel_id));
      }
      break;
    }
    case DialogType::None: {
      if (sponsored_message.invite_hash.empty()) {
        CHECK(!sponsored_message.site_url.empty());
        type = td_api::make_object<td_api::messageSponsorTypeWebsite>(sponsored_message.site_url,
                                                                      sponsored_message.site_name);
        if (sponsored_message.show_dialog_photo) {
          photo = get_chat_photo_info_object(td_->file_manager_.get(), &sponsored_message.site_photo);
        }
        break;
      }
      auto invite_link = LinkManager::get_dialog_invite_link(sponsored_message.invite_hash, false);
      auto chat_invite_link_info = td_->contacts_manager_->get_chat_invite_link_info_object(invite_link);
      if (chat_invite_link_info == nullptr) {
        LOG(ERROR) << "Failed to get invite link info for " << invite_link;
        return nullptr;
      }
      if (chat_invite_link_info->type_->get_id() != td_api::inviteLinkChatTypeChannel::ID) {
        LOG(ERROR) << "Receive sponsor chat of a wrong type " << to_string(chat_invite_link_info->type_);
        return nullptr;
      }
      type = td_api::make_object<td_api::messageSponsorTypePrivateChannel>(chat_invite_link_info->title_, invite_link);
      if (sponsored_message.show_dialog_photo) {
        photo = std::move(chat_invite_link_info->photo_);
      }
      break;
    }
    default:
      break;
  }
  return td_api::make_object<td_api::messageSponsor>(std::move(type), std::move(photo), sponsored_message.sponsor_info);
}

td_api::object_ptr<td_api::sponsoredMessage> SponsoredMessageManager::get_sponsored_message_object(
    DialogId dialog_id, const SponsoredMessage &sponsored_message) const {
  auto sponsor = get_message_sponsor_object(sponsored_message);
  if (sponsor == nullptr) {
    return nullptr;
  }
  return td_api::make_object<td_api::sponsoredMessage>(
      sponsored_message.local_id, sponsored_message.is_recommended,
      get_message_content_object(sponsored_message.content.get(), td_, dialog_id, 0, false, true, -1, false, false),
      std::move(sponsor), sponsored_message.additional_info);
}

td_api::object_ptr<td_api::sponsoredMessages> SponsoredMessageManager::get_sponsored_messages_object(
    DialogId dialog_id, const DialogSponsoredMessages &sponsored_messages) const {
  auto messages = transform(sponsored_messages.messages, [this, dialog_id](const SponsoredMessage &message) {
    return get_sponsored_message_object(dialog_id, message);
  });
  td::remove_if(messages, [](const auto &message) { return message == nullptr; });
  return td_api::make_object<td_api::sponsoredMessages>(std::move(messages), sponsored_messages.messages_between);
}

void SponsoredMessageManager::get_dialog_sponsored_messages(
    DialogId dialog_id, Promise<td_api::object_ptr<td_api::sponsoredMessages>> &&promise) {
  if (!td_->messages_manager_->have_dialog_force(dialog_id, "get_dialog_sponsored_message")) {
    return promise.set_error(Status::Error(400, "Chat not found"));
  }
  if (dialog_id.get_type() != DialogType::Channel) {
    return promise.set_value(td_api::make_object<td_api::sponsoredMessages>());
  }

  auto &messages = dialog_sponsored_messages_[dialog_id];
  if (messages != nullptr && messages->promises.empty()) {
    if (messages->is_premium == td_->option_manager_->get_option_boolean("is_premium", false)) {
      // use cached value
      return promise.set_value(get_sponsored_messages_object(dialog_id, *messages));
    } else {
      // drop cache
      messages = nullptr;
      delete_cached_sponsored_messages_timeout_.cancel_timeout(dialog_id.get());
    }
  }

  if (messages == nullptr) {
    messages = make_unique<DialogSponsoredMessages>();
  }
  messages->promises.push_back(std::move(promise));
  if (messages->promises.size() == 1) {
    auto query_promise = PromiseCreator::lambda(
        [actor_id = actor_id(this),
         dialog_id](Result<telegram_api::object_ptr<telegram_api::messages_SponsoredMessages>> &&result) mutable {
          send_closure(actor_id, &SponsoredMessageManager::on_get_dialog_sponsored_messages, dialog_id,
                       std::move(result));
        });
    td_->create_handler<GetSponsoredMessagesQuery>(std::move(query_promise))->send(dialog_id.get_channel_id());
  }
}

void SponsoredMessageManager::on_get_dialog_sponsored_messages(
    DialogId dialog_id, Result<telegram_api::object_ptr<telegram_api::messages_SponsoredMessages>> &&result) {
  G()->ignore_result_if_closing(result);

  auto &messages = dialog_sponsored_messages_[dialog_id];
  CHECK(messages != nullptr);
  auto promises = std::move(messages->promises);
  reset_to_empty(messages->promises);
  CHECK(messages->messages.empty());
  CHECK(messages->message_infos.empty());

  if (result.is_error()) {
    dialog_sponsored_messages_.erase(dialog_id);
    fail_promises(promises, result.move_as_error());
    return;
  }

  auto sponsored_messages_ptr = result.move_as_ok();
  switch (sponsored_messages_ptr->get_id()) {
    case telegram_api::messages_sponsoredMessages::ID: {
      auto sponsored_messages =
          telegram_api::move_object_as<telegram_api::messages_sponsoredMessages>(sponsored_messages_ptr);

      td_->contacts_manager_->on_get_users(std::move(sponsored_messages->users_), "on_get_dialog_sponsored_messages");
      td_->contacts_manager_->on_get_chats(std::move(sponsored_messages->chats_), "on_get_dialog_sponsored_messages");

      for (auto &sponsored_message : sponsored_messages->messages_) {
        DialogId sponsor_dialog_id;
        ServerMessageId server_message_id;
        string invite_hash;
        string site_url;
        string site_name;
        DialogPhoto site_photo;
        if (sponsored_message->from_id_ != nullptr) {
          sponsor_dialog_id = DialogId(sponsored_message->from_id_);
          if (!sponsor_dialog_id.is_valid() ||
              !td_->messages_manager_->have_dialog_info_force(sponsor_dialog_id, "on_get_dialog_sponsored_messages")) {
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
          td_->contacts_manager_->on_get_dialog_invite_link_info(
              invite_link, std::move(sponsored_message->chat_invite_), Promise<Unit>());
          auto chat_invite_link_info = td_->contacts_manager_->get_chat_invite_link_info_object(invite_link);
          if (chat_invite_link_info == nullptr) {
            LOG(ERROR) << "Failed to get invite link info from " << chat_invite << " for "
                       << to_string(sponsored_message);
            continue;
          }
          invite_hash = std::move(sponsored_message->chat_invite_hash_);
        } else if (sponsored_message->webpage_ != nullptr && !sponsored_message->webpage_->url_.empty()) {
          site_url = std::move(sponsored_message->webpage_->url_);
          site_name = std::move(sponsored_message->webpage_->site_name_);
          if (sponsored_message->webpage_->photo_ != nullptr) {
            auto photo = get_photo(td_, std::move(sponsored_message->webpage_->photo_), DialogId());
            site_photo = as_fake_dialog_photo(photo, DialogId(), false);
          }
        } else {
          LOG(ERROR) << "Receive " << to_string(sponsored_message);
          continue;
        }

        auto message_text = get_message_text(td_->contacts_manager_.get(), std::move(sponsored_message->message_),
                                             std::move(sponsored_message->entities_), true, true, 0, false,
                                             "on_get_dialog_sponsored_messages");
        int32 ttl = 0;
        bool disable_web_page_preview = false;
        auto content = get_message_content(td_, std::move(message_text), nullptr, sponsor_dialog_id, true, UserId(),
                                           &ttl, &disable_web_page_preview, "on_get_dialog_sponsored_messages");
        if (ttl != 0) {
          LOG(ERROR) << "Receive sponsored message with self-destruct time " << ttl;
          continue;
        }
        CHECK(disable_web_page_preview);

        current_sponsored_message_id_ = current_sponsored_message_id_.get_next_message_id(MessageType::Local);
        if (!current_sponsored_message_id_.is_valid_sponsored()) {
          LOG(ERROR) << "Sponsored message identifier overflowed";
          current_sponsored_message_id_ = MessageId::max().get_next_message_id(MessageType::Local);
          CHECK(current_sponsored_message_id_.is_valid_sponsored());
        }
        auto local_id = current_sponsored_message_id_.get();
        CHECK(!current_sponsored_message_id_.is_valid());
        CHECK(!current_sponsored_message_id_.is_scheduled());
        SponsoredMessageInfo message_info;
        message_info.random_id_ = sponsored_message->random_id_.as_slice().str();
        auto is_inserted = messages->message_infos.emplace(local_id, std::move(message_info)).second;
        CHECK(is_inserted);
        messages->messages.emplace_back(
            local_id, sponsored_message->recommended_, sponsored_message->show_peer_photo_, sponsor_dialog_id,
            server_message_id, std::move(sponsored_message->start_param_), std::move(invite_hash), std::move(content),
            std::move(sponsored_message->sponsor_info_), std::move(sponsored_message->additional_info_),
            std::move(site_url), std::move(site_name), std::move(site_photo));
      }
      messages->messages_between = sponsored_messages->posts_between_;
      break;
    }
    case telegram_api::messages_sponsoredMessagesEmpty::ID:
      break;
    default:
      UNREACHABLE();
  }
  messages->is_premium = td_->option_manager_->get_option_boolean("is_premium", false);

  for (auto &promise : promises) {
    promise.set_value(get_sponsored_messages_object(dialog_id, *messages));
  }
  delete_cached_sponsored_messages_timeout_.set_timeout_in(dialog_id.get(), 300.0);
}

void SponsoredMessageManager::view_sponsored_message(DialogId dialog_id, MessageId sponsored_message_id) {
  auto it = dialog_sponsored_messages_.find(dialog_id);
  if (it == dialog_sponsored_messages_.end()) {
    return;
  }
  auto random_id_it = it->second->message_infos.find(sponsored_message_id.get());
  if (random_id_it == it->second->message_infos.end() || random_id_it->second.is_viewed_) {
    return;
  }

  random_id_it->second.is_viewed_ = true;
  td_->create_handler<ViewSponsoredMessageQuery>()->send(dialog_id.get_channel_id(), random_id_it->second.random_id_);
}

void SponsoredMessageManager::click_sponsored_message(DialogId dialog_id, MessageId sponsored_message_id,
                                                      Promise<Unit> &&promise) {
  if (!dialog_id.is_valid() || !sponsored_message_id.is_valid_sponsored()) {
    return promise.set_error(Status::Error(400, "Invalid message specified"));
  }
  auto it = dialog_sponsored_messages_.find(dialog_id);
  if (it == dialog_sponsored_messages_.end()) {
    return promise.set_value(Unit());
  }
  auto random_id_it = it->second->message_infos.find(sponsored_message_id.get());
  if (random_id_it == it->second->message_infos.end() || random_id_it->second.is_clicked_) {
    return promise.set_value(Unit());
  }

  random_id_it->second.is_clicked_ = true;
  td_->create_handler<ClickSponsoredMessageQuery>(std::move(promise))
      ->send(dialog_id.get_channel_id(), random_id_it->second.random_id_);
}

}  // namespace td
