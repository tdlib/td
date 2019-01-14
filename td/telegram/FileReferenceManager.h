//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/telegram/ChatId.h"
#include "td/telegram/ChannelId.h"
#include "td/telegram/files/FileManager.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/UserId.h"

#include "td/utils/Variant.h"

#include <unordered_map>

namespace td {

extern int VERBOSITY_NAME(file_references);

class FileReferenceManager : public Actor {
 public:
  FileSourceId create_message_file_source(FullMessageId full_message_id);

  void update_file_reference(FileId file_id, vector<FileSourceId> file_source_ids, Promise<> promise);

 private:
  struct FileSourceMessage {
    FullMessageId full_message_id;
  };
  struct FileSourceUserPhoto {
    UserId user_id;
    int64 photo_id;
  };
  struct FileSourceChatPhoto {
    ChatId chat_id;
  };
  struct FileSourceChannelPhoto {
    ChannelId channel_id;
  };
  struct FileSourceWallpapers {
    // empty
  };
  struct FileSourceWebPage {
    string url;
  };
  struct FileSourceSavedAnimations {
    // empty
  };

  using FileSource = Variant<FileSourceMessage, FileSourceUserPhoto, FileSourceChatPhoto, FileSourceChannelPhoto,
                             FileSourceWallpapers, FileSourceWebPage, FileSourceSavedAnimations>;

  vector<FileSource> file_sources_;
  std::unordered_map<FullMessageId, FileSourceId, FullMessageIdHash> full_message_id_to_file_source_id_;

  int32 last_file_source_id_{0};
};

}  // namespace td
