//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/AnimationsManager.h"
#include "td/telegram/BackgroundManager.h"
#include "td/telegram/ChannelId.h"
#include "td/telegram/ChatId.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/FileReferenceManager.h"
#include "td/telegram/files/FileSourceId.h"
#include "td/telegram/FullMessageId.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/StickersManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/UserId.h"
#include "td/telegram/WebPagesManager.h"

#include "td/utils/common.h"
#include "td/utils/overloaded.h"
#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void FileReferenceManager::store_file_source(FileSourceId file_source_id, StorerT &storer) const {
  auto index = static_cast<size_t>(file_source_id.get()) - 1;
  CHECK(index < file_sources_.size());
  auto &source = file_sources_[index];
  td::store(source.get_offset(), storer);
  source.visit(overloaded([&](const FileSourceMessage &source) { td::store(source.full_message_id, storer); },
                          [&](const FileSourceUserPhoto &source) {
                            td::store(source.user_id, storer);
                            td::store(source.photo_id, storer);
                          },
                          [&](const FileSourceChatPhoto &source) { td::store(source.chat_id, storer); },
                          [&](const FileSourceChannelPhoto &source) { td::store(source.channel_id, storer); },
                          [&](const FileSourceWallpapers &source) {},
                          [&](const FileSourceWebPage &source) { td::store(source.url, storer); },
                          [&](const FileSourceSavedAnimations &source) {},
                          [&](const FileSourceRecentStickers &source) { td::store(source.is_attached, storer); },
                          [&](const FileSourceFavoriteStickers &source) {},
                          [&](const FileSourceBackground &source) {
                            td::store(source.background_id, storer);
                            td::store(source.access_hash, storer);
                          },
                          [&](const FileSourceChatFull &source) { td::store(source.chat_id, storer); },
                          [&](const FileSourceChannelFull &source) { td::store(source.channel_id, storer); }));
}

template <class ParserT>
FileSourceId FileReferenceManager::parse_file_source(Td *td, ParserT &parser) {
  auto type = parser.fetch_int();
  switch (type) {
    case 0: {
      FullMessageId full_message_id;
      td::parse(full_message_id, parser);
      return td->messages_manager_->get_message_file_source_id(full_message_id);
    }
    case 1: {
      UserId user_id;
      int64 photo_id;
      td::parse(user_id, parser);
      td::parse(photo_id, parser);
      return td->contacts_manager_->get_user_profile_photo_file_source_id(user_id, photo_id);
    }
    case 2: {
      ChatId chat_id;
      td::parse(chat_id, parser);
      return FileSourceId();  // there is no need to repair chat photos
    }
    case 3: {
      ChannelId channel_id;
      td::parse(channel_id, parser);
      return FileSourceId();  // there is no need to repair channel photos
    }
    case 4:
      return FileSourceId();  // there is no way to repair old wallpapers
    case 5: {
      string url;
      td::parse(url, parser);
      return td->web_pages_manager_->get_url_file_source_id(url);
    }
    case 6:
      return td->animations_manager_->get_saved_animations_file_source_id();
    case 7: {
      bool is_attached;
      td::parse(is_attached, parser);
      return td->stickers_manager_->get_recent_stickers_file_source_id(is_attached);
    }
    case 8:
      return td->stickers_manager_->get_favorite_stickers_file_source_id();
    case 9: {
      BackgroundId background_id;
      int64 access_hash;
      td::parse(background_id, parser);
      td::parse(access_hash, parser);
      return td->background_manager_->get_background_file_source_id(background_id, access_hash);
    }
    case 10: {
      ChatId chat_id;
      td::parse(chat_id, parser);
      return td->contacts_manager_->get_chat_full_file_source_id(chat_id);
    }
    case 11: {
      ChannelId channel_id;
      td::parse(channel_id, parser);
      return td->contacts_manager_->get_channel_full_file_source_id(channel_id);
    }
    default:
      parser.set_error("Invalid type in FileSource");
      return FileSourceId();
  }
}

}  // namespace td
