//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/FileReferenceManager.h"

#include "td/telegram/AnimationsManager.h"
#include "td/telegram/BackgroundManager.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/files/FileManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/StickerSetId.h"
#include "td/telegram/StickersManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/WebPagesManager.h"

#include "td/utils/common.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/overloaded.h"
#include "td/utils/Time.h"

namespace td {

int VERBOSITY_NAME(file_references) = VERBOSITY_NAME(INFO);

bool FileReferenceManager::is_file_reference_error(const Status &error) {
  return error.is_error() && error.code() == 400 && begins_with(error.message(), "FILE_REFERENCE_");
}

size_t FileReferenceManager::get_file_reference_error_pos(const Status &error) {
  if (!is_file_reference_error(error)) {
    return 0;
  }
  auto offset = Slice("FILE_REFERENCE_").size();
  if (error.message().size() <= offset || !is_digit(error.message()[offset])) {
    return 0;
  }
  return to_integer<size_t>(error.message().substr(offset)) + 1;
}

/*
fileSourceMessage chat_id:int53 message_id:int53 = FileSource;           // repaired with get_message_from_server
fileSourceUserProfilePhoto user_id:int32 photo_id:int64 = FileSource;    // repaired with photos.getUserPhotos
fileSourceBasicGroupPhoto basic_group_id:int32 = FileSource;             // no need to repair
fileSourceSupergroupPhoto supergroup_id:int32 = FileSource;              // no need to repair
fileSourceWebPage url:string = FileSource;                               // repaired with messages.getWebPage
fileSourceWallpapers = FileSource;                                       // can't be repaired
fileSourceSavedAnimations = FileSource;                                  // repaired with messages.getSavedGifs
fileSourceRecentStickers is_attached:Bool = FileSource;                  // repaired with messages.getRecentStickers, not reliable
fileSourceFavoriteStickers = FileSource;                                 // repaired with messages.getFavedStickers, not reliable
fileSourceBackground background_id:int64 access_hash:int64 = FileSource; // repaired with account.getWallPaper
fileSourceBasicGroupFull basic_group_id:int32 = FileSource;              // repaired with messages.getFullChat
fileSourceSupergroupFull supergroup_id:int32 = FileSource;               // repaired with messages.getFullChannel
*/

FileSourceId FileReferenceManager::get_current_file_source_id() const {
  return FileSourceId(narrow_cast<int32>(file_sources_.size()));
}

template <class T>
FileSourceId FileReferenceManager::add_file_source_id(T source, Slice source_str) {
  file_sources_.emplace_back(std::move(source));
  VLOG(file_references) << "Create file source " << file_sources_.size() << " for " << source_str;
  return get_current_file_source_id();
}

FileSourceId FileReferenceManager::create_message_file_source(FullMessageId full_message_id) {
  FileSourceMessage source{full_message_id};
  return add_file_source_id(source, PSLICE() << full_message_id);
}

FileSourceId FileReferenceManager::create_user_photo_file_source(UserId user_id, int64 photo_id) {
  FileSourceUserPhoto source{photo_id, user_id};
  return add_file_source_id(source, PSLICE() << "photo " << photo_id << " of " << user_id);
}

FileSourceId FileReferenceManager::create_web_page_file_source(string url) {
  FileSourceWebPage source{std::move(url)};
  auto source_str = PSTRING() << "web page of " << source.url;
  return add_file_source_id(std::move(source), source_str);
}

FileSourceId FileReferenceManager::create_saved_animations_file_source() {
  FileSourceSavedAnimations source;
  return add_file_source_id(source, "saved animations");
}

FileSourceId FileReferenceManager::create_recent_stickers_file_source(bool is_attached) {
  FileSourceRecentStickers source{is_attached};
  return add_file_source_id(source, PSLICE() << "recent " << (is_attached ? "attached " : "") << "stickers");
}

FileSourceId FileReferenceManager::create_favorite_stickers_file_source() {
  FileSourceFavoriteStickers source;
  return add_file_source_id(source, PSLICE() << "favorite stickers");
}

FileSourceId FileReferenceManager::create_background_file_source(BackgroundId background_id, int64 access_hash) {
  FileSourceBackground source{background_id, access_hash};
  return add_file_source_id(source, PSLICE() << background_id);
}

FileSourceId FileReferenceManager::create_chat_full_file_source(ChatId chat_id) {
  FileSourceChatFull source{chat_id};
  return add_file_source_id(source, PSLICE() << "full " << chat_id);
}

FileSourceId FileReferenceManager::create_channel_full_file_source(ChannelId channel_id) {
  FileSourceChannelFull source{channel_id};
  return add_file_source_id(source, PSLICE() << "full " << channel_id);
}

bool FileReferenceManager::add_file_source(NodeId node_id, FileSourceId file_source_id) {
  bool is_added = nodes_[node_id].file_source_ids.add(file_source_id);
  VLOG(file_references) << "Add " << (is_added ? "new" : "old") << ' ' << file_source_id << " for file " << node_id;
  return is_added;
}

bool FileReferenceManager::remove_file_source(NodeId node_id, FileSourceId file_source_id) {
  bool is_removed = nodes_[node_id].file_source_ids.remove(file_source_id);
  if (is_removed) {
    VLOG(file_references) << "Remove " << file_source_id << " from file " << node_id;
  } else {
    VLOG(file_references) << "Can't find " << file_source_id << " from file " << node_id << " to remove it";
  }
  return is_removed;
}

vector<FileSourceId> FileReferenceManager::get_some_file_sources(NodeId node_id) {
  auto it = nodes_.find(node_id);
  if (it == nodes_.end()) {
    return {};
  }
  return it->second.file_source_ids.get_some_elements();
}

vector<FullMessageId> FileReferenceManager::get_some_message_file_sources(NodeId node_id) {
  auto file_source_ids = get_some_file_sources(node_id);

  vector<FullMessageId> result;
  for (auto file_source_id : file_source_ids) {
    auto index = static_cast<size_t>(file_source_id.get()) - 1;
    CHECK(index < file_sources_.size());
    const auto &file_source = file_sources_[index];
    if (file_source.get_offset() == 0) {
      result.push_back(file_source.get<FileSourceMessage>().full_message_id);
    }
  }
  return result;
}

void FileReferenceManager::merge(NodeId to_node_id, NodeId from_node_id) {
  auto from_it = nodes_.find(from_node_id);
  if (from_it == nodes_.end()) {
    return;
  }

  auto &to = nodes_[to_node_id];
  auto &from = from_it->second;
  VLOG(file_references) << "Merge " << to.file_source_ids.size() << " and " << from.file_source_ids.size()
                        << " sources of files " << to_node_id << " and " << from_node_id;
  CHECK(!to.query || to.query->proxy.empty());
  CHECK(!from.query || from.query->proxy.empty());
  if (to.query || from.query) {
    if (!to.query) {
      to.query = make_unique<Query>();
      to.query->generation = ++query_generation_;
    }
    if (from.query) {
      combine(to.query->promises, std::move(from.query->promises));
      to.query->active_queries += from.query->active_queries;
      from.query->proxy = {to_node_id, to.query->generation};
    }
  }
  to.file_source_ids.merge(std::move(from.file_source_ids));
  run_node(to_node_id);
  run_node(from_node_id);
}

void FileReferenceManager::run_node(NodeId node_id) {
  Node &node = nodes_[node_id];
  if (!node.query) {
    return;
  }
  if (node.query->active_queries != 0) {
    return;
  }
  VLOG(file_references) << "Trying to repair file reference for file " << node_id;
  if (node.query->promises.empty()) {
    node.query = {};
    return;
  }
  if (!node.file_source_ids.has_next()) {
    VLOG(file_references) << "Have no more file sources to repair file reference for file " << node_id;
    for (auto &p : node.query->promises) {
      if (node.file_source_ids.empty()) {
        p.set_error(Status::Error(400, "File source is not found"));
      } else {
        p.set_error(Status::Error(429, "Too Many Requests: retry after 1"));
      }
    }
    node.query = {};
    return;
  }
  if (node.last_successful_repair_time >= Time::now() - 60) {
    VLOG(file_references) << "Recently repaired file reference for file " << node_id << ", do not try again";
    for (auto &p : node.query->promises) {
      p.set_error(Status::Error(429, "Too Many Requests: retry after 60"));
    }
    node.query = {};
    return;
  }
  auto file_source_id = node.file_source_ids.next();
  send_query({node_id, node.query->generation}, file_source_id);
}

void FileReferenceManager::send_query(Destination dest, FileSourceId file_source_id) {
  VLOG(file_references) << "Send file reference repair query for file " << dest.node_id << " with generation "
                        << dest.generation << " from " << file_source_id;
  auto &node = nodes_[dest.node_id];
  node.query->active_queries++;

  auto promise = PromiseCreator::lambda([dest, file_source_id, file_reference_manager = G()->file_reference_manager(),
                                         file_manager = G()->file_manager()](Result<Unit> result) {
    if (G()->close_flag()) {
      VLOG(file_references) << "Ignore file reference repair from " << file_source_id << " during closing";
      return;
    }

    auto new_promise = PromiseCreator::lambda([dest, file_source_id, file_reference_manager](Result<Unit> result) {
      if (G()->close_flag()) {
        VLOG(file_references) << "Ignore file reference repair from " << file_source_id << " during closing";
        return;
      }

      Status status;
      if (result.is_error()) {
        status = result.move_as_error();
      }
      send_closure(file_reference_manager, &FileReferenceManager::on_query_result, dest, file_source_id,
                   std::move(status), 0);
    });

    send_closure(file_manager, &FileManager::on_file_reference_repaired, dest.node_id, file_source_id,
                 std::move(result), std::move(new_promise));
  });
  auto index = static_cast<size_t>(file_source_id.get()) - 1;
  CHECK(index < file_sources_.size());
  file_sources_[index].visit(overloaded(
      [&](const FileSourceMessage &source) {
        send_closure_later(G()->messages_manager(), &MessagesManager::get_message_from_server, source.full_message_id,
                           std::move(promise), nullptr);
      },
      [&](const FileSourceUserPhoto &source) {
        send_closure_later(G()->contacts_manager(), &ContactsManager::reload_user_profile_photo, source.user_id,
                           source.photo_id, std::move(promise));
      },
      [&](const FileSourceChatPhoto &source) {
        send_closure_later(G()->contacts_manager(), &ContactsManager::reload_chat, source.chat_id, std::move(promise));
      },
      [&](const FileSourceChannelPhoto &source) {
        send_closure_later(G()->contacts_manager(), &ContactsManager::reload_channel, source.channel_id,
                           std::move(promise));
      },
      [&](const FileSourceWallpapers &source) { promise.set_error(Status::Error("Can't repair old wallpapers")); },
      [&](const FileSourceWebPage &source) {
        send_closure_later(G()->web_pages_manager(), &WebPagesManager::reload_web_page_by_url, source.url,
                           std::move(promise));
      },
      [&](const FileSourceSavedAnimations &source) {
        send_closure_later(G()->animations_manager(), &AnimationsManager::repair_saved_animations, std::move(promise));
      },
      [&](const FileSourceRecentStickers &source) {
        send_closure_later(G()->stickers_manager(), &StickersManager::repair_recent_stickers, source.is_attached,
                           std::move(promise));
      },
      [&](const FileSourceFavoriteStickers &source) {
        send_closure_later(G()->stickers_manager(), &StickersManager::repair_favorite_stickers, std::move(promise));
      },
      [&](const FileSourceBackground &source) {
        send_closure_later(G()->background_manager(), &BackgroundManager::reload_background, source.background_id,
                           source.access_hash, std::move(promise));
      },
      [&](const FileSourceChatFull &source) {
        send_closure_later(G()->contacts_manager(), &ContactsManager::reload_chat_full, source.chat_id,
                           std::move(promise));
      },
      [&](const FileSourceChannelFull &source) {
        send_closure_later(G()->contacts_manager(), &ContactsManager::reload_channel_full, source.channel_id,
                           std::move(promise), "repair file reference");
      }));
}

FileReferenceManager::Destination FileReferenceManager::on_query_result(Destination dest, FileSourceId file_source_id,
                                                                        Status status, int32 sub) {
  VLOG(file_references) << "Receive result of file reference repair query for file " << dest.node_id
                        << " with generation " << dest.generation << " from " << file_source_id << ": " << status << " "
                        << sub;
  auto &node = nodes_[dest.node_id];

  auto query = node.query.get();
  if (!query) {
    return dest;
  }
  if (query->generation != dest.generation) {
    return dest;
  }
  query->active_queries--;
  CHECK(query->active_queries >= 0);

  if (!query->proxy.empty()) {
    query->active_queries -= sub;
    CHECK(query->active_queries >= 0);
    auto new_proxy = on_query_result(query->proxy, file_source_id, std::move(status), query->active_queries);
    query->proxy = new_proxy;
    run_node(dest.node_id);
    return new_proxy;
  }

  if (status.is_ok()) {
    node.last_successful_repair_time = Time::now();
    for (auto &p : query->promises) {
      p.set_value(Unit());
    }
    node.query = {};
  }

  run_node(dest.node_id);
  return dest;
}

void FileReferenceManager::repair_file_reference(NodeId node_id, Promise<> promise) {
  auto main_file_id = G()->td().get_actor_unsafe()->file_manager_->get_file_view(node_id).file_id();
  VLOG(file_references) << "Repair file reference for file " << node_id << "/" << main_file_id;
  node_id = main_file_id;
  auto &node = nodes_[node_id];
  if (!node.query) {
    node.query = make_unique<Query>();
    node.query->generation = ++query_generation_;
    node.file_source_ids.reset_position();
    VLOG(file_references) << "Create new file reference repair query with generation " << query_generation_;
  }
  node.query->promises.push_back(std::move(promise));
  run_node(node_id);
}

void FileReferenceManager::reload_photo(PhotoSizeSource source, Promise<Unit> promise) {
  switch (source.get_type()) {
    case PhotoSizeSource::Type::DialogPhotoBig:
    case PhotoSizeSource::Type::DialogPhotoSmall:
      send_closure(G()->contacts_manager(), &ContactsManager::reload_dialog_info, source.dialog_photo().dialog_id,
                   std::move(promise));
      break;
    case PhotoSizeSource::Type::StickerSetThumbnail:
      send_closure(G()->stickers_manager(), &StickersManager::reload_sticker_set,
                   StickerSetId(source.sticker_set_thumbnail().sticker_set_id),
                   source.sticker_set_thumbnail().sticker_set_access_hash, std::move(promise));
      break;
    default:
      promise.set_error(Status::Error("Unexpected PhotoSizeSource type"));
  }
}

}  // namespace td
