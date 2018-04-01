//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/Game.h"

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/telegram/AnimationsManager.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/DocumentsManager.h"
#include "td/telegram/Photo.h"
#include "td/telegram/Td.h"

#include "td/utils/common.h"
#include "td/utils/logging.h"

namespace td {

Game::Game(Td *td, tl_object_ptr<telegram_api::game> &&game, DialogId owner_dialog_id)
    : Game(td, std::move(game->title_), std::move(game->description_), std::move(game->photo_),
           std::move(game->document_), owner_dialog_id) {
  id_ = game->id_;
  access_hash_ = game->access_hash_;
  short_name_ = game->short_name_;
}

Game::Game(Td *td, string title, string description, tl_object_ptr<telegram_api::Photo> &&photo,
           tl_object_ptr<telegram_api::Document> &&document, DialogId owner_dialog_id)
    : title_(std::move(title)), description_(std::move(description)) {
  CHECK(td != nullptr);
  CHECK(photo != nullptr);
  if (photo->get_id() == telegram_api::photo::ID) {
    photo_ = get_photo(td->file_manager_.get(), move_tl_object_as<telegram_api::photo>(photo), owner_dialog_id);
  }
  if (document != nullptr) {
    int32 document_id = document->get_id();
    if (document_id == telegram_api::document::ID) {
      auto parsed_document =
          td->documents_manager_->on_get_document(move_tl_object_as<telegram_api::document>(document), owner_dialog_id);
      switch (parsed_document.first) {
        case DocumentsManager::DocumentType::Animation:
          animation_file_id_ = parsed_document.second;
          break;
        case DocumentsManager::DocumentType::Audio:
        case DocumentsManager::DocumentType::General:
        case DocumentsManager::DocumentType::Sticker:
        case DocumentsManager::DocumentType::Video:
        case DocumentsManager::DocumentType::VideoNote:
        case DocumentsManager::DocumentType::VoiceNote:
        case DocumentsManager::DocumentType::Unknown:
          LOG(ERROR) << "Receive non-animation document in the game";
          break;
        default:
          UNREACHABLE();
      }
    }
  }
}

Game::Game(UserId bot_user_id, string short_name) : bot_user_id_(bot_user_id), short_name_(std::move(short_name)) {
  if (!bot_user_id_.is_valid()) {
    bot_user_id_ = UserId();
  }
}

bool Game::empty() const {
  return short_name_.empty();
}

void Game::set_bot_user_id(UserId bot_user_id) {
  if (bot_user_id.is_valid()) {
    bot_user_id_ = bot_user_id;
  } else {
    bot_user_id_ = UserId();
  }
}

UserId Game::get_bot_user_id() const {
  return bot_user_id_;
}

void Game::set_message_text(FormattedText &&text) {
  text_ = std::move(text);
}

const FormattedText &Game::get_message_text() const {
  return text_;
}

tl_object_ptr<td_api::game> Game::get_game_object(const Td *td) const {
  return make_tl_object<td_api::game>(
      id_, short_name_, title_, get_formatted_text_object(text_), description_,
      get_photo_object(td->file_manager_.get(), &photo_),
      td->animations_manager_->get_animation_object(animation_file_id_, "get_game_object"));
}

tl_object_ptr<telegram_api::inputMediaGame> Game::get_input_media_game(const Td *td) const {
  auto input_user = td->contacts_manager_->get_input_user(bot_user_id_);
  CHECK(input_user != nullptr);
  return make_tl_object<telegram_api::inputMediaGame>(
      make_tl_object<telegram_api::inputGameShortName>(std::move(input_user), short_name_));
}

bool operator==(const Game &lhs, const Game &rhs) {
  return lhs.id_ == rhs.id_ && lhs.access_hash_ == rhs.access_hash_ && lhs.bot_user_id_ == rhs.bot_user_id_ &&
         lhs.short_name_ == rhs.short_name_ && lhs.title_ == rhs.title_ && lhs.description_ == rhs.description_ &&
         lhs.photo_ == rhs.photo_ && lhs.animation_file_id_ == rhs.animation_file_id_ && lhs.text_ == rhs.text_;
}

bool operator!=(const Game &lhs, const Game &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const Game &game) {
  return string_builder << "Game[id = " << game.id_ << ", access_hash = " << game.access_hash_
                        << ", bot = " << game.bot_user_id_ << ", short_name = " << game.short_name_
                        << ", title = " << game.title_ << ", description = " << game.description_
                        << ", photo = " << game.photo_ << ", animation_file_id = " << game.animation_file_id_ << "]";
}

}  // namespace td
