//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/telegram/files/FileId.h"
#include "td/telegram/SecretInputMedia.h"

#include "td/utils/common.h"

#include <unordered_map>

namespace td {

class Td;

class VoiceNotesManager {
 public:
  explicit VoiceNotesManager(Td *td);

  int32 get_voice_note_duration(FileId file_id) const;

  tl_object_ptr<td_api::voiceNote> get_voice_note_object(FileId file_id);

  void create_voice_note(FileId file_id, string mime_type, int32 duration, string waveform, bool replace);

  tl_object_ptr<telegram_api::InputMedia> get_input_media(FileId file_id,
                                                          tl_object_ptr<telegram_api::InputFile> input_file) const;

  SecretInputMedia get_secret_input_media(FileId voice_file_id,
                                          tl_object_ptr<telegram_api::InputEncryptedFile> input_file,
                                          const string &caption) const;

  FileId dup_voice_note(FileId new_id, FileId old_id);

  bool merge_voice_notes(FileId new_id, FileId old_id, bool can_delete_old);

  template <class StorerT>
  void store_voice_note(FileId file_id, StorerT &storer) const;

  template <class ParserT>
  FileId parse_voice_note(ParserT &parser);

 private:
  class VoiceNote {
   public:
    string mime_type;
    int32 duration = 0;
    string waveform;

    FileId file_id;

    bool is_changed = true;
  };

  const VoiceNote *get_voice_note(FileId file_id) const;

  FileId on_get_voice_note(unique_ptr<VoiceNote> new_voice_note, bool replace);

  Td *td_;
  std::unordered_map<FileId, unique_ptr<VoiceNote>, FileIdHash> voice_notes_;
};

}  // namespace td
