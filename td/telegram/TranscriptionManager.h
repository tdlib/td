//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"

namespace td {

class Td;

class TranscriptionManager final : public Actor {
 public:
  TranscriptionManager(Td *td, ActorShared<> parent);

  void on_update_trial_parameters(int32 weekly_number, int32 duration_max, int32 cooldown_until);

  void get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const;

 private:
  void tear_down() final;

  void send_update_speech_recognition_trial() const;

  td_api::object_ptr<td_api::updateSpeechRecognitionTrial> get_update_speech_recognition_trial_object() const;

  struct TrialParameters {
    int32 weekly_number_ = 0;
    int32 duration_max_ = 0;
    int32 left_tries_ = 0;
    int32 cooldown_until_ = 0;

    td_api::object_ptr<td_api::updateSpeechRecognitionTrial> get_update_speech_recognition_trial_object() const;
  };

  friend bool operator==(const TrialParameters &lhs, const TrialParameters &rhs);

  Td *td_;
  ActorShared<> parent_;

  TrialParameters trial_parameters_;
};

}  // namespace td
