//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/VoiceNotesManager.h"

#include "td/telegram/files/FileId.hpp"

#include "td/utils/logging.h"
#include "td/utils/tl_helpers.h"

namespace td {

template <class T>
void VoiceNotesManager::store_voice_note(FileId file_id, T &storer) const {
  auto it = voice_notes_.find(file_id);
  CHECK(it != voice_notes_.end());
  const VoiceNote *voice_note = it->second.get();
  store(voice_note->mime_type, storer);
  store(voice_note->duration, storer);
  store(voice_note->waveform, storer);
  store(file_id, storer);
}

template <class T>
FileId VoiceNotesManager::parse_voice_note(T &parser) {
  auto voice_note = make_unique<VoiceNote>();
  parse(voice_note->mime_type, parser);
  parse(voice_note->duration, parser);
  parse(voice_note->waveform, parser);
  parse(voice_note->file_id, parser);
  return on_get_voice_note(std::move(voice_note), true);
}

}  // namespace td
