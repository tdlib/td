//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/AttachMenuManager.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/Document.h"
#include "td/telegram/DocumentsManager.h"
#include "td/telegram/files/FileManager.h"
#include "td/telegram/Td.h"

#include "td/actor/PromiseFuture.h"

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"
#include "td/utils/misc.h"

namespace td {

class GetAttachMenuBotsQuery final : public Td::ResultHandler {
  Promise<telegram_api::object_ptr<telegram_api::AttachMenuBots>> promise_;

 public:
  explicit GetAttachMenuBotsQuery(Promise<telegram_api::object_ptr<telegram_api::AttachMenuBots>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(int64 hash) {
    send_query(G()->net_query_creator().create(telegram_api::messages_getAttachMenuBots(hash)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_getAttachMenuBots>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetAttachMenuBotsQuery: " << to_string(ptr);

    promise_.set_value(std::move(ptr));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

bool operator==(const AttachMenuManager::AttachMenuBot &lhs, const AttachMenuManager::AttachMenuBot &rhs) {
  return lhs.user_id_ == rhs.user_id_ && lhs.name_ == rhs.name_ &&
         lhs.default_icon_file_id_ == rhs.default_icon_file_id_ &&
         lhs.ios_static_icon_file_id_ == rhs.ios_static_icon_file_id_ &&
         lhs.ios_animated_icon_file_id_ == rhs.ios_animated_icon_file_id_ &&
         lhs.android_icon_file_id_ == rhs.android_icon_file_id_ && lhs.macos_icon_file_id_ == rhs.macos_icon_file_id_;
}

bool operator!=(const AttachMenuManager::AttachMenuBot &lhs, const AttachMenuManager::AttachMenuBot &rhs) {
  return !(lhs == rhs);
}

AttachMenuManager::AttachMenuManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

void AttachMenuManager::start_up() {
  init();
}

void AttachMenuManager::init() {
  if (!is_active()) {
    return;
  }
  if (is_inited_) {
    return;
  }
  is_inited_ = true;

  reload_attach_menu_bots();
}

void AttachMenuManager::tear_down() {
  parent_.reset();
}

bool AttachMenuManager::is_active() const {
  return td_->auth_manager_->is_authorized() && !td_->auth_manager_->is_bot();
}

void AttachMenuManager::reload_attach_menu_bots() {
  if (!is_active()) {
    return;
  }
  auto promise = PromiseCreator::lambda(
      [actor_id = actor_id(this)](Result<telegram_api::object_ptr<telegram_api::AttachMenuBots>> &&result) {
        send_closure(actor_id, &AttachMenuManager::on_reload_attach_menu_bots, std::move(result));
      });
  td_->create_handler<GetAttachMenuBotsQuery>(std::move(promise))->send(0);
}

void AttachMenuManager::on_reload_attach_menu_bots(
    Result<telegram_api::object_ptr<telegram_api::AttachMenuBots>> &&result) {
  if (!is_active()) {
    return;
  }
  if (result.is_error()) {
    // TODO retry after some time
    return;
  }

  is_inited_ = true;

  auto attach_menu_bots_ptr = result.move_as_ok();
  auto constructor_id = attach_menu_bots_ptr->get_id();
  if (constructor_id == telegram_api::attachMenuBotsNotModified::ID) {
    return;
  }
  CHECK(constructor_id == telegram_api::attachMenuBots::ID);
  auto attach_menu_bots = move_tl_object_as<telegram_api::attachMenuBots>(attach_menu_bots_ptr);

  td_->contacts_manager_->on_get_users(std::move(attach_menu_bots->users_), "on_reload_attach_menu_bots");

  auto new_hash = attach_menu_bots->hash_;
  vector<AttachMenuBot> new_attach_menu_bots;

  for (auto &bot : attach_menu_bots->bots_) {
    UserId user_id(bot->bot_id_);
    if (!td_->contacts_manager_->have_user(user_id)) {
      LOG(ERROR) << "Have no information about " << user_id;
      new_hash = 0;
      continue;
    }

    AttachMenuBot attach_menu_bot;
    attach_menu_bot.user_id_ = user_id;
    attach_menu_bot.name_ = std::move(bot->short_name_);
    for (auto &icon : bot->icons_) {
      Slice name = icon->name_;
      int32 document_id = icon->icon_->get_id();
      if (document_id == telegram_api::documentEmpty::ID) {
        LOG(ERROR) << "Have no icon for " << user_id << " with name " << name;
        new_hash = 0;
        continue;
      }
      CHECK(document_id == telegram_api::document::ID);

      if (name != "default_static" && name != "ios_static" && name != "ios_animated" && name != "android_animated" &&
          name != "macos_animated") {
        LOG(ERROR) << "Have icon for " << user_id << " with name " << name;
        continue;
      }

      auto expected_document_type = ends_with(name, "_static") ? Document::Type::General : Document::Type::Sticker;
      auto parsed_document =
          td_->documents_manager_->on_get_document(move_tl_object_as<telegram_api::document>(icon->icon_), DialogId());
      if (parsed_document.type != expected_document_type) {
        LOG(ERROR) << "Receive wrong attach menu bot icon for " << user_id;
        continue;
      }
      switch (name[5]) {
        case 'l':
          attach_menu_bot.default_icon_file_id_ = parsed_document.file_id;
          break;
        case 't':
          attach_menu_bot.ios_static_icon_file_id_ = parsed_document.file_id;
          break;
        case 'n':
          attach_menu_bot.ios_animated_icon_file_id_ = parsed_document.file_id;
          break;
        case 'i':
          attach_menu_bot.android_icon_file_id_ = parsed_document.file_id;
          break;
        case '_':
          attach_menu_bot.macos_icon_file_id_ = parsed_document.file_id;
          break;
        default:
          UNREACHABLE();
      }
    }
    new_attach_menu_bots.push_back(std::move(attach_menu_bot));
  }

  bool need_update = new_attach_menu_bots != attach_menu_bots_;
  if (need_update || hash_ != new_hash) {
    hash_ = new_hash;
    attach_menu_bots_ = std::move(new_attach_menu_bots);

    if (need_update) {
      send_update_attach_menu_bots();
    }
  }
}

td_api::object_ptr<td_api::updateAttachMenuBots> AttachMenuManager::get_update_attach_menu_bots_object() const {
  CHECK(is_active());
  auto bots = transform(attach_menu_bots_, [td = td_](const AttachMenuBot &bot) {
    auto get_file = [td](FileId file_id) {
      if (!file_id.is_valid()) {
        return nullptr;
      }
      return td->file_manager_->get_file_object(file_id);
    };

    return td_api::make_object<td_api::attachMenuBot>(
        td->contacts_manager_->get_user_id_object(bot.user_id_, "attachMenuBot"), bot.name_,
        get_file(bot.default_icon_file_id_), get_file(bot.ios_static_icon_file_id_),
        get_file(bot.ios_animated_icon_file_id_), get_file(bot.android_icon_file_id_),
        get_file(bot.macos_icon_file_id_));
  });
  return td_api::make_object<td_api::updateAttachMenuBots>(std::move(bots));
}

void AttachMenuManager::send_update_attach_menu_bots() const {
  send_closure(G()->td(), &Td::send_update, get_update_attach_menu_bots_object());
}

void AttachMenuManager::get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const {
  if (!is_active()) {
    return;
  }

  updates.push_back(get_update_attach_menu_bots_object());
}

}  // namespace td
