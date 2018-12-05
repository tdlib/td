//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/FileReferenceManager.h"

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
  ++last_file_source_id_;
  to_full_message_id_[last_file_source_id_] = full_message_id;
  from_full_message_id_[full_message_id] = FileSourceId{last_file_source_id_};
  return FileSourceId{last_file_source_id_};
}

void FileReferenceManager::update_file_reference(FileId file_id, std::vector<FileSourceId> sources, Promise<> promise) {
  LOG(ERROR) << "update file reference: " << file_id << " " << format::as_array(sources);
  //if (td::Random::fast(0, 3) == 0) {
  //promise.set_error(td::Status::Error("Error"));
  //return;
  //}

  //if (td::Random::fast(0, 3) == 0) {
  //promise.set_value(Unit());
  //return;
  //}

  MultiPromiseActorSafe mpas{"UpdateFileReferenceMultiPromiseActor"};
  mpas.add_promise(std::move(promise));
  auto lock = mpas.get_promise();
  SCOPE_EXIT {
    lock.set_value(Unit());
  };
  for (auto &source_id : sources) {
    auto it = to_full_message_id_.find(source_id);
    if (it != to_full_message_id_.end()) {
      std::vector<FullMessageId> message_ids = {it->second};
      auto new_promise = PromiseCreator::lambda([promise = mpas.get_promise(), file_id, source_id,
                                                 file_manager = G()->file_manager()](Result<Unit> res) mutable {
        if (res.is_error()) {
          send_closure(file_manager, &FileManager::remove_file_source, file_id, source_id);
          send_lambda(file_manager, [promise = std::move(promise)]() mutable { promise.set_value({}); });
        } else {
          promise.set_value(Unit());
        }
      });

      LOG(ERROR) << "Ask for " << it->second;
      send_closure_later(G()->messages_manager(), &MessagesManager::get_messages_from_server, std::move(message_ids),
                         std::move(new_promise), nullptr);
    } else {
      LOG(ERROR) << "Invalid source id " << source_id << " " << file_id;
      send_closure(G()->file_manager(), &FileManager::remove_file_source, file_id, source_id);
      send_lambda(G()->file_manager(), [promise = mpas.get_promise()]() mutable { promise.set_value(Unit()); });
    }
  }
}

}  // namespace td
