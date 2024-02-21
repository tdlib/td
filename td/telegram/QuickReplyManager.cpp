//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/QuickReplyManager.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/Dependencies.h"
#include "td/telegram/DialogManager.h"
#include "td/telegram/MessageContent.h"
#include "td/telegram/MessageForwardInfo.h"
#include "td/telegram/MessageReplyHeader.h"
#include "td/telegram/Td.h"

namespace td {

QuickReplyManager::QuickReplyManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

void QuickReplyManager::tear_down() {
  parent_.reset();
}

td_api::object_ptr<td_api::MessageContent> QuickReplyManager::get_quick_reply_message_message_content_object(
    const QuickReplyMessage *m) const {
  return get_message_content_object(m->content.get(), td_, DialogId(), 0, m->is_content_secret, true, -1,
                                    m->invert_media, m->disable_web_page_preview);
}

unique_ptr<QuickReplyManager::QuickReplyMessage> QuickReplyManager::create_message(
    telegram_api::object_ptr<telegram_api::Message> message_ptr, const char *source) const {
  LOG(DEBUG) << "Receive from " << source << " " << to_string(message_ptr);
  CHECK(message_ptr != nullptr);

  switch (message_ptr->get_id()) {
    case telegram_api::messageEmpty::ID:
      break;
    case telegram_api::message::ID: {
      auto message = move_tl_object_as<telegram_api::message>(message_ptr);
      if (message->quick_reply_shortcut_id_ == 0) {
        LOG(ERROR) << "Receive a quick reply without shortcut from " << source;
        break;
      }

      auto my_dialog_id = td_->dialog_manager_->get_my_dialog_id();
      if (DialogId(message->peer_id_) != my_dialog_id || message->from_id_ == nullptr ||
          DialogId(message->from_id_) != my_dialog_id || message->views_ != 0 || message->forwards_ != 0 ||
          message->replies_ != nullptr || message->reactions_ != nullptr || message->edit_date_ != 0 ||
          message->ttl_period_ != 0 || !message->out_ || message->post_ || message->edit_hide_ ||
          message->from_scheduled_ || message->pinned_ || message->noforwards_ || message->mentioned_ ||
          message->media_unread_ || message->reply_markup_ != nullptr || !message->restriction_reason_.empty() ||
          !message->post_author_.empty() || message->from_boosts_applied_ != 0 || message->saved_peer_id_ != nullptr) {
        LOG(ERROR) << "Receive an invalid quick reply from " << source << ": " << to_string(message);
        break;
      }

      auto message_id = MessageId::get_message_id(message_ptr, false);
      auto forward_header = std::move(message->fwd_from_);
      UserId via_bot_user_id;
      if (message->flags_ & telegram_api::message::VIA_BOT_ID_MASK) {
        via_bot_user_id = UserId(message->via_bot_id_);
        if (!via_bot_user_id.is_valid()) {
          LOG(ERROR) << "Receive invalid " << via_bot_user_id << " from " << source;
          via_bot_user_id = UserId();
        }
      }
      auto media_album_id = message->grouped_id_;

      MessageSelfDestructType ttl;
      bool disable_web_page_preview = false;
      auto content = get_message_content(
          td_,
          get_message_text(td_->contacts_manager_.get(), std::move(message->message_), std::move(message->entities_),
                           true, td_->auth_manager_->is_bot(), 0, media_album_id != 0, source),
          std::move(message->media_), my_dialog_id, message->date_, true, via_bot_user_id, &ttl,
          &disable_web_page_preview, source);

      auto reply_header = MessageReplyHeader(td_, std::move(message->reply_to_), my_dialog_id, message_id, -1, false);
      if (reply_header.story_full_id_ != StoryFullId()) {
        LOG(ERROR) << "Receive reply to " << reply_header.story_full_id_;
        reply_header.story_full_id_ = {};
      }

      auto content_type = content->get_type();
      bool is_content_secret =
          ttl.is_secret_message_content(content_type);  // must be calculated before TTL is adjusted
      if (!ttl.is_empty()) {
        if (!ttl.is_valid()) {
          LOG(ERROR) << "Wrong " << ttl << " received in " << message_id << " from " << source;
          ttl = {};
        } else {
          ttl.ensure_at_least(get_message_content_duration(content.get(), td_) + 1);
        }
      }

      if (is_expired_message_content(content_type)) {
        LOG(ERROR) << "Receive " << content_type << " from " << source;
        break;
      }

      auto result = make_unique<QuickReplyMessage>();
      result->message_id = message_id;
      result->ttl = ttl;
      result->disable_web_page_preview = disable_web_page_preview;
      result->forward_info = MessageForwardInfo::get_message_forward_info(td_, std::move(forward_header));
      result->replied_message_info = std::move(reply_header.replied_message_info_);
      result->via_bot_user_id = via_bot_user_id;
      result->disable_notification = message->silent_;
      result->is_content_secret = is_content_secret;
      result->legacy_layer = (message->legacy_ ? MTPROTO_LAYER : 0);
      result->invert_media = message->invert_media_;
      result->content = std::move(content);

      if (media_album_id != 0) {
        if (!is_allowed_media_group_content(content_type)) {
          if (content_type != MessageContentType::Unsupported) {
            LOG(ERROR) << "Receive media group identifier " << media_album_id << " in " << message_id
                       << " with content "
                       << oneline(to_string(get_quick_reply_message_message_content_object(result.get())));
          }
        } else {
          result->media_album_id = media_album_id;
        }
      }

      Dependencies dependencies;
      add_quick_reply_message_dependencies(dependencies, result.get());
      for (auto dependent_dialog_id : dependencies.get_dialog_ids()) {
        td_->dialog_manager_->force_create_dialog(dependent_dialog_id, source, true);
      }

      return result;
    }
    case telegram_api::messageService::ID:
      LOG(ERROR) << "Receive " << to_string(message_ptr);
      break;
    default:
      UNREACHABLE();
      break;
  }
  return nullptr;
}

void QuickReplyManager::add_quick_reply_message_dependencies(Dependencies &dependencies, const QuickReplyMessage *m) const {
  auto is_bot = td_->auth_manager_->is_bot();
  m->replied_message_info.add_dependencies(dependencies, is_bot);
  dependencies.add_dialog_and_dependencies(m->real_forward_from_dialog_id);
  dependencies.add(m->via_bot_user_id);
  if (m->forward_info != nullptr) {
    m->forward_info->add_dependencies(dependencies);
  }
  add_message_content_dependencies(dependencies, m->content.get(), is_bot);
}

}  // namespace td
