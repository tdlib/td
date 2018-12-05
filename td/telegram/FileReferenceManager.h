//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/telegram/files/FileManager.h"
#include "td/telegram/MessageId.h"

#include <map>
#include <unordered_map>

namespace td {

class FileReferenceManager : public Actor {
 public:
  FileSourceId create_file_source(FullMessageId full_message_id);
  void update_file_reference(FileId file_id, std::vector<FileSourceId> sources, Promise<> promise);

 private:
  td::int32 last_file_source_id_{0};
  std::map<FileSourceId, FullMessageId> to_full_message_id_;
  std::unordered_map<FullMessageId, FileSourceId, FullMessageIdHash> from_full_message_id_;
};

}  // namespace td
