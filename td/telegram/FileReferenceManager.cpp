//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/FileReferenceManager.h"

#include "td/telegram/AnimationsManager.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/files/FileManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/WebPagesManager.h"

#include "td/actor/MultiPromise.h"

#include "td/utils/format.h"
#include "td/utils/overloaded.h"
#include "td/utils/Variant.h"

namespace td {

int VERBOSITY_NAME(file_references) = VERBOSITY_NAME(WARNING);

/*
fileSourceMessage chat_id:int53 message_id:int53 = FileSource;         // repaired with get_messages_from_server
fileSourceUserProfilePhoto user_id:int32 photo_id:int64 = FileSource;  // repaired with photos.getUserPhotos
fileSourceBasicGroupPhoto basic_group_id:int32 = FileSource;           // repaired with messages.getChats
fileSourceSupergroupPhoto supergroup_id:int32 = FileSource;            // repaired with channels.getChannels
fileSourceWebPage url:string = FileSource;                             // repaired with messages.getWebPage
fileSourceWallpapers = FileSource;                                     // repaired with account.getWallPapers
fileSourceSavedAnimations = FileSource;                                // repaired with messages.getSavedGifs
*/

FileSourceId FileReferenceManager::create_message_file_source(FullMessageId full_message_id) {
  auto it = full_message_id_to_file_source_id_.find(full_message_id);
  if (it != full_message_id_to_file_source_id_.end()) {
    return it->second;
  }

  auto source_id = FileSourceId{++last_file_source_id_};
  FileSourceMessage source{full_message_id};
  file_sources_.emplace_back(source);
  full_message_id_to_file_source_id_[full_message_id] = source_id;
  return source_id;
}

void FileReferenceManager::update_file_reference(FileId file_id, vector<FileSourceId> file_source_ids,
                                                 Promise<> promise) {
  VLOG(file_references) << "Trying to load valid file_reference from server: " << file_id << " " << file_source_ids;
  MultiPromiseActorSafe mpas{"UpdateFileReferenceMultiPromiseActor"};
  mpas.set_ignore_errors(true);
  mpas.add_promise(std::move(promise));
  auto lock = mpas.get_promise();
  for (auto source_id : file_source_ids) {
    auto index = static_cast<size_t>(source_id.get()) - 1;
    CHECK(index < file_sources_.size());

    auto new_promise = PromiseCreator::lambda([promise = mpas.get_promise(), file_id, source_id,
                                               file_manager = G()->file_manager()](Result<Unit> result) mutable {
      if (result.is_error() && result.error().code() != 429 && result.error().code() < 500 && !G()->close_flag()) {
        VLOG(file_references) << "Invalid source id " << source_id << " " << result.error();
        send_closure(file_manager, &FileManager::remove_file_source, file_id, source_id);
      }
      // NB: main promise must send closure to FileManager
      // So the closure will be executed only after the bad source id is removed
      promise.set_value(Unit());
    });
    file_sources_[index].visit(overloaded(
        [&](const FileSourceMessage &source) {
          send_closure_later(G()->messages_manager(), &MessagesManager::get_messages_from_server,
                             vector<FullMessageId>{source.full_message_id}, std::move(new_promise), nullptr);
        },
        [&](const FileSourceUserPhoto &source) {
        //  send_closure_later(G()->contacts_manager(), &ContactsManager::get_user_photo_from_server, source.user_id,
        //                     source.photo_id, std::move(new_promise));
        },
        [&](const FileSourceChatPhoto &source) {
        //  send_closure_later(G()->contacts_manager(), &ContactsManager::get_chat_photo_from_server, source.chat_id,
        //                     std::move(new_promise));
        },
        [&](const FileSourceChannelPhoto &source) {
        //  send_closure_later(G()->contacts_manager(), &ContactsManager::get_channel_photo_from_server,
        //                     source.channel_id, std::move(new_promise));
        },
        [&](const FileSourceWallpapers &source) {
        //  send_closure_later(G()->wallpaper_manager(), &WallpaperManager::get_wallpapers_from_server,
        //                     std::move(new_promise));
        },
        [&](const FileSourceWebPage &source) {
          send_closure_later(G()->web_pages_manager(), &WebPagesManager::reload_web_page_by_url, source.url,
                             std::move(new_promise));
        },
        [&](const FileSourceSavedAnimations &source) {
          /*
          // TODO this is wrong, because we shouldn't pass animations hash to the call
          // we also sometimes need to do two simultaneous calls one with and one without hash
          send_closure_later(G()->animations_manager(), &AnimationsManager::reload_saved_animations,
                             true, std::move(new_promise));
          */
        }));
  }
  lock.set_value(Unit());
}

}  // namespace td
