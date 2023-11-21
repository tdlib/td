//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"
#include "td/actor/MultiTimeout.h"

#include "td/utils/common.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/Status.h"

#include <functional>

namespace td {

class Td;

class TranscriptionManager final : public Actor {
 public:
  TranscriptionManager(Td *td, ActorShared<> parent);

  void on_update_trial_parameters(int32 weekly_number, int32 duration_max, int32 cooldown_until);

  using TranscribedAudioHandler =
      std::function<void(Result<telegram_api::object_ptr<telegram_api::updateTranscribedAudio>>)>;
  void subscribe_to_transcribed_audio_updates(int64 transcription_id, TranscribedAudioHandler on_update);

  void on_update_transcribed_audio(telegram_api::object_ptr<telegram_api::updateTranscribedAudio> &&update);

  void get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const;

 private:
  static constexpr int32 AUDIO_TRANSCRIPTION_TIMEOUT = 60;

  void tear_down() final;

  static void on_pending_audio_transcription_timeout_callback(void *td, int64 transcription_id);

  static string get_trial_parameters_database_key();

  void load_trial_parameters();

  void save_trial_parameters();

  void send_update_speech_recognition_trial() const;

  td_api::object_ptr<td_api::updateSpeechRecognitionTrial> get_update_speech_recognition_trial_object() const;

  void on_pending_audio_transcription_failed(int64 transcription_id, Status &&error);

  struct TrialParameters {
    int32 weekly_number_ = 0;
    int32 duration_max_ = 0;
    int32 left_tries_ = 0;
    int32 cooldown_until_ = 0;

    void update_left_tries();

    td_api::object_ptr<td_api::updateSpeechRecognitionTrial> get_update_speech_recognition_trial_object() const;

    template <class StorerT>
    void store(StorerT &storer) const;

    template <class ParserT>
    void parse(ParserT &parser);
  };

  friend bool operator==(const TrialParameters &lhs, const TrialParameters &rhs);

  Td *td_;
  ActorShared<> parent_;

  TrialParameters trial_parameters_;

  FlatHashMap<int64, TranscribedAudioHandler> pending_audio_transcriptions_;
  MultiTimeout pending_audio_transcription_timeout_{"PendingAudioTranscriptionTimeout"};
};

}  // namespace td
