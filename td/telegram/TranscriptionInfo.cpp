//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/TranscriptionInfo.h"

namespace td {

bool TranscriptionInfo::start_recognize_speech(Promise<Unit> &&promise) {
  if (is_transcribed_) {
    promise.set_value(Unit());
    return false;
  }
  speech_recognition_queries_.push_back(std::move(promise));
  if (speech_recognition_queries_.size() == 1) {
    last_transcription_error_ = Status::OK();
    return true;
  }
  return false;
}

vector<Promise<Unit>> TranscriptionInfo::on_final_transcription(string &&text, int64 transcription_id) {
  CHECK(!is_transcribed_);
  CHECK(transcription_id_ == 0 || transcription_id_ == transcription_id);
  CHECK(transcription_id != 0);
  transcription_id_ = transcription_id;
  is_transcribed_ = true;
  text_ = std::move(text);
  last_transcription_error_ = Status::OK();

  CHECK(!speech_recognition_queries_.empty());
  auto promises = std::move(speech_recognition_queries_);
  speech_recognition_queries_.clear();

  return std::move(promises);
}

bool TranscriptionInfo::on_partial_transcription(string &&text, int64 transcription_id) {
  CHECK(!is_transcribed_);
  CHECK(transcription_id_ == 0 || transcription_id_ == transcription_id);
  CHECK(transcription_id != 0);
  bool is_changed = text_ != text;
  transcription_id_ = transcription_id;
  text_ = std::move(text);
  last_transcription_error_ = Status::OK();

  return is_changed;
}

vector<Promise<Unit>> TranscriptionInfo::on_failed_transcription(Status &&error) {
  CHECK(!is_transcribed_);
  transcription_id_ = 0;
  text_.clear();
  last_transcription_error_ = std::move(error);

  CHECK(!speech_recognition_queries_.empty());
  auto promises = std::move(speech_recognition_queries_);
  speech_recognition_queries_.clear();
  return promises;
}

unique_ptr<TranscriptionInfo> TranscriptionInfo::copy_if_transcribed(const unique_ptr<TranscriptionInfo> &info) {
  if (info == nullptr || !info->is_transcribed_) {
    return nullptr;
  }
  auto result = make_unique<TranscriptionInfo>();
  result->is_transcribed_ = true;
  result->transcription_id_ = info->transcription_id_;
  result->text_ = info->text_;
  return result;
}

bool TranscriptionInfo::update_from(unique_ptr<TranscriptionInfo> &old_info, unique_ptr<TranscriptionInfo> &&new_info) {
  if (new_info == nullptr || !new_info->is_transcribed_) {
    return false;
  }
  CHECK(new_info->transcription_id_ != 0);
  CHECK(new_info->last_transcription_error_.is_ok());
  CHECK(new_info->speech_recognition_queries_.empty());
  if (old_info == nullptr) {
    old_info = std::move(new_info);
    return true;
  }
  if (old_info->transcription_id_ != 0 || !old_info->speech_recognition_queries_.empty()) {
    return false;
  }
  CHECK(!old_info->is_transcribed_);
  old_info = std::move(new_info);
  return true;
}

td_api::object_ptr<td_api::SpeechRecognitionResult> TranscriptionInfo::get_speech_recognition_result_object() const {
  if (is_transcribed_) {
    return td_api::make_object<td_api::speechRecognitionResultText>(text_);
  }
  if (!speech_recognition_queries_.empty()) {
    return td_api::make_object<td_api::speechRecognitionResultPending>(text_);
  }
  if (last_transcription_error_.is_error()) {
    return td_api::make_object<td_api::speechRecognitionResultError>(td_api::make_object<td_api::error>(
        last_transcription_error_.code(), last_transcription_error_.message().str()));
  }
  return nullptr;
}

}  // namespace td
