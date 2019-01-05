//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/FileReferenceManager.h"

#include "td/telegram/files/FileManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessagesManager.h"

#include "td/actor/MultiPromise.h"

#include "td/utils/format.h"

namespace td {

FileSourceId FileReferenceManager::create_file_source(FullMessageId full_message_id) {
  auto it = from_full_message_id_.find(full_message_id);
  if (it != from_full_message_id_.end()) {
    return it->second;
  }
  auto source_id = FileSourceId{++last_file_source_id_};
  to_full_message_id_[source_id] = full_message_id;
  from_full_message_id_[full_message_id] = source_id;
  return source_id;
}

void FileReferenceManager::update_file_reference(FileId file_id, std::vector<FileSourceId> sources, Promise<> promise) {
  LOG(INFO) << "Trying to load valid file_reference from server: " << file_id << " " << format::as_array(sources);
  MultiPromiseActorSafe mpas{"UpdateFileReferenceMultiPromiseActor"};
  mpas.add_promise(std::move(promise));
  auto lock = mpas.get_promise();
  for (auto source_id : sources) {
    auto it = to_full_message_id_.find(source_id);
    auto new_promise = PromiseCreator::lambda([promise = mpas.get_promise(), file_id, source_id,
                                               file_manager = G()->file_manager()](Result<Unit> res) mutable {
      if (res.is_error()) {
        LOG(INFO) << "Invalid source id " << source_id << " " << res.error();
        send_closure(file_manager, &FileManager::remove_file_source, file_id, source_id);
      }
      // NB: main promise must send closure to FileManager
      // So the closure will be executed only after the bad source id is removed
      promise.set_value(Unit());
    });
    if (it == to_full_message_id_.end()) {
      new_promise.set_error(Status::Error("Unknown source id"));
      continue;
    }

    std::vector<FullMessageId> message_ids = {it->second};
    LOG(INFO) << source_id << ": load message from server " << it->second;
    send_closure_later(G()->messages_manager(), &MessagesManager::get_messages_from_server, std::move(message_ids),
                       std::move(new_promise), nullptr);
  }
  lock.set_value(Unit());
}

}  // namespace td
