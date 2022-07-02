//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/files/FileId.h"
#include "td/telegram/FullMessageId.h"
#include "td/telegram/SecretInputMedia.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"
#include "td/actor/MultiTimeout.h"

#include "td/utils/common.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/FlatHashSet.h"
#include "td/utils/Promise.h"
#include "td/utils/Status.h"

namespace td {

class Td;

class VoiceNotesManager final : public Actor {
 public:
  VoiceNotesManager(Td *td, ActorShared<> parent);

  int32 get_voice_note_duration(FileId file_id) const;

  tl_object_ptr<td_api::voiceNote> get_voice_note_object(FileId file_id) const;

  void create_voice_note(FileId file_id, string mime_type, int32 duration, string waveform, bool replace);

  void register_voice_note(FileId voice_note_file_id, FullMessageId full_message_id, const char *source);

  void unregister_voice_note(FileId voice_note_file_id, FullMessageId full_message_id, const char *source);

  void recognize_speech(FullMessageId full_message_id, Promise<Unit> &&promise);

  void rate_speech_recognition(FullMessageId full_message_id, bool is_good, Promise<Unit> &&promise);

  void on_update_transcribed_audio(string &&text, int64 transcription_id, bool is_final);

  void on_voice_note_transcribed(FileId file_id, string &&text, int64 transcription_id, bool is_final);

  void on_voice_note_transcription_failed(FileId file_id, Status &&error);

  tl_object_ptr<telegram_api::InputMedia> get_input_media(FileId file_id,
                                                          tl_object_ptr<telegram_api::InputFile> input_file) const;

  SecretInputMedia get_secret_input_media(FileId voice_note_file_id,
                                          tl_object_ptr<telegram_api::InputEncryptedFile> input_file,
                                          const string &caption, int32 layer) const;

  FileId dup_voice_note(FileId new_id, FileId old_id);

  void merge_voice_notes(FileId new_id, FileId old_id, bool can_delete_old);

  template <class StorerT>
  void store_voice_note(FileId file_id, StorerT &storer) const;

  template <class ParserT>
  FileId parse_voice_note(ParserT &parser);

 private:
  static constexpr int32 TRANSCRIPTION_TIMEOUT = 60;

  class VoiceNote {
   public:
    string mime_type;
    int32 duration = 0;
    bool is_transcribed = false;
    string waveform;
    int64 transcription_id = 0;
    string text;

    FileId file_id;
  };

  static void on_voice_note_transcription_timeout_callback(void *voice_notes_manager_ptr, int64 transcription_id);

  VoiceNote *get_voice_note(FileId file_id);

  const VoiceNote *get_voice_note(FileId file_id) const;

  FileId on_get_voice_note(unique_ptr<VoiceNote> new_voice_note, bool replace);

  void on_pending_voice_note_transcription_failed(int64 transcription_id, Status &&error);

  void on_voice_note_transcription_updated(FileId file_id);

  void tear_down() final;

  Td *td_;
  ActorShared<> parent_;

  FlatHashMap<FileId, unique_ptr<VoiceNote>, FileIdHash> voice_notes_;

  FlatHashMap<FileId, vector<Promise<Unit>>, FileIdHash> speech_recognition_queries_;
  FlatHashMap<int64, FileId> pending_voice_note_transcription_queries_;
  MultiTimeout voice_note_transcription_timeout_{"VoiceNoteTranscriptionTimeout"};

  FlatHashMap<FileId, FlatHashSet<FullMessageId, FullMessageIdHash>, FileIdHash> voice_note_messages_;
  FlatHashMap<FullMessageId, FileId, FullMessageIdHash> message_voice_notes_;
};

}  // namespace td
