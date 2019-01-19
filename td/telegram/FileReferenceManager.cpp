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
#include "td/telegram/WallpaperManager.h"
#include "td/telegram/WebPagesManager.h"

#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/overloaded.h"

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

FileSourceId FileReferenceManager::get_current_file_source_id() const {
  return FileSourceId(narrow_cast<int32>(file_sources_.size()));
}

template <class T>
FileSourceId FileReferenceManager::add_file_source_id(T source) {
  file_sources_.emplace_back(std::move(source));
  return get_current_file_source_id();
}

FileSourceId FileReferenceManager::create_message_file_source(FullMessageId full_message_id) {
  VLOG(file_references) << "Create file source for " << full_message_id;
  FileSourceMessage source{full_message_id};
  return add_file_source_id(source);
}

FileSourceId FileReferenceManager::create_user_photo_file_source(UserId user_id, int64 photo_id) {
  VLOG(file_references) << "Create file source for photo " << photo_id << " of " << user_id;
  FileSourceUserPhoto source{photo_id, user_id};
  return add_file_source_id(source);
}

FileSourceId FileReferenceManager::create_chat_photo_file_source(ChatId chat_id) {
  VLOG(file_references) << "Create file source for photo of " << chat_id;
  FileSourceChatPhoto source{chat_id};
  return add_file_source_id(source);
}

FileSourceId FileReferenceManager::create_channel_photo_file_source(ChannelId channel_id) {
  VLOG(file_references) << "Create file source for photo of " << channel_id;
  FileSourceChannelPhoto source{channel_id};
  return add_file_source_id(source);
}

FileSourceId FileReferenceManager::create_wallpapers_file_source() {
  VLOG(file_references) << "Create file source for wallpapers";
  FileSourceWebPage source;
  return add_file_source_id(source);
}

FileSourceId FileReferenceManager::create_web_page_file_source(string url) {
  VLOG(file_references) << "Create file source for web page of " << url;
  FileSourceWebPage source{std::move(url)};
  return add_file_source_id(std::move(source));
}

FileSourceId FileReferenceManager::create_saved_animations_file_source() {
  VLOG(file_references) << "Create file source for saved animations";
  FileSourceSavedAnimations source;
  return add_file_source_id(source);
}

void FileReferenceManager::add_file_source(NodeId node_id, FileSourceId file_source_id) {
  VLOG(file_references) << "Add file source " << file_source_id << " for file " << node_id;
  nodes_[node_id].file_source_ids.add(file_source_id);
}

void FileReferenceManager::remove_file_source(NodeId node_id, FileSourceId file_source_id) {
  VLOG(file_references) << "Remove file source " << file_source_id << " from file " << node_id;
  nodes_[node_id].file_source_ids.remove(file_source_id);
}

void FileReferenceManager::merge(NodeId to_node_id, NodeId from_node_id) {
  VLOG(file_references) << "Merge sources of files " << to_node_id << " and " << from_node_id;
  auto &to = nodes_[to_node_id];
  auto &from = nodes_[from_node_id];
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
  VLOG(file_references) << "Run file references repair for file " << node_id;
  if (node.query->promises.empty()) {
    node.query = {};
    return;
  }
  if (!node.file_source_ids.has_next()) {
    for (auto &p : node.query->promises) {
      p.set_value(Unit());
    }
    node.query = {};
    return;
  }
  auto file_source_id = node.file_source_ids.next();
  send_query({node_id, node.query->generation}, file_source_id);
}

void FileReferenceManager::send_query(Destination dest, FileSourceId file_source_id) {
  VLOG(file_references) << "Send file references repair query for file " << dest.node_id << " with generation "
                        << dest.generation << " from source " << file_source_id;
  auto &node = nodes_[dest.node_id];
  node.query->active_queries++;

  auto promise = PromiseCreator::lambda([dest, file_source_id, file_reference_manager = G()->file_reference_manager(),
                                         file_manager = G()->file_manager()](Result<Unit> result) mutable {
    auto new_promise =
        PromiseCreator::lambda([dest, file_source_id, file_reference_manager](Result<Unit> result) mutable {
          Status status;
          if (result.is_error()) {
            status = result.move_as_error();
          }
          send_closure(file_reference_manager, &FileReferenceManager::on_query_result, dest, file_source_id,
                       std::move(status), 0);
        });
    if (result.is_error()) {
      new_promise.set_result(std::move(result));
    }
    send_lambda(file_manager, [file_manager, dest, new_promise = std::move(new_promise)]() mutable {
      auto view = file_manager.get_actor_unsafe()->get_file_view(dest.node_id);
      CHECK(!view.empty());
      if (view.has_active_remote_location()) {
        new_promise.set_value({});
      } else {
        new_promise.set_error(Status::Error("No active remote location"));
      }
    });
  });
  auto index = static_cast<size_t>(file_source_id.get()) - 1;
  CHECK(index < file_sources_.size());
  file_sources_[index].visit(overloaded(
      [&](const FileSourceMessage &source) {
        send_closure_later(G()->messages_manager(), &MessagesManager::get_messages_from_server,
                           vector<FullMessageId>{source.full_message_id}, std::move(promise), nullptr);
      },
      [&](const FileSourceUserPhoto &source) {
        //  send_closure_later(G()->contacts_manager(), &ContactsManager::get_user_photo_from_server, source.user_id,
        //                     source.photo_id, std::move(promise));
      },
      [&](const FileSourceChatPhoto &source) {
        //  send_closure_later(G()->contacts_manager(), &ContactsManager::get_chat_photo_from_server, source.chat_id,
        //                     std::move(promise));
      },
      [&](const FileSourceChannelPhoto &source) {
        //  send_closure_later(G()->contacts_manager(), &ContactsManager::get_channel_photo_from_server,
        //                     source.channel_id, std::move(promise));
      },
      [&](const FileSourceWallpapers &source) {
        //  send_closure_later(G()->wallpaper_manager(), &WallpaperManager::get_wallpapers_from_server,
        //                     std::move(promise));
      },
      [&](const FileSourceWebPage &source) {
        send_closure_later(G()->web_pages_manager(), &WebPagesManager::reload_web_page_by_url, source.url,
                           std::move(promise));
      },
      [&](const FileSourceSavedAnimations &source) {
        /*
          // TODO this is wrong, because we shouldn't pass animations hash to the call
          // we also sometimes need to do two simultaneous calls one with and one without hash
          send_closure_later(G()->animations_manager(), &AnimationsManager::reload_saved_animations,
                             true, std::move(promise));
          */
      }));
}

FileReferenceManager::Destination FileReferenceManager::on_query_result(Destination dest, FileSourceId file_source_id,
                                                                        Status status, int32 sub) {
  VLOG(file_references) << "Receive result of file references repair query for file " << dest.node_id
                        << " with generation " << dest.generation << " from source " << file_source_id << ": " << status
                        << " " << sub;
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
    for (auto &p : query->promises) {
      p.set_value(Unit());
    }
    node.query = {};
  }
  if (status.is_error() && status.error().code() != 429 && status.error().code() < 500 && !G()->close_flag()) {
    VLOG(file_references) << "Invalid source id " << file_source_id << " " << status;
    remove_file_source(dest.node_id, file_source_id);
  }

  run_node(dest.node_id);
  return dest;
}

void FileReferenceManager::repair_file_reference(NodeId node_id, Promise<> promise) {
  VLOG(file_references) << "Repair file reference for file " << node_id;
  auto &node = nodes_[node_id];
  if (!node.query) {
    node.query = make_unique<Query>();
    node.query->generation = ++query_generation_;
    node.file_source_ids.reset_position();
    VLOG(file_references) << "new query " << query_generation_;
  }
  node.query->promises.push_back(std::move(promise));
  run_node(node_id);
}

}  // namespace td
