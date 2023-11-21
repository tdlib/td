//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/TranscriptionManager.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/Td.h"

namespace td {

TranscriptionManager::TranscriptionManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

void TranscriptionManager::tear_down() {
  parent_.reset();
}

void TranscriptionManager::on_update_trial_parameters(int32 weekly_number, int32 duration_max, int32 cooldown_until) {
  CHECK(!td_->auth_manager_->is_bot());
  weekly_number = max(0, weekly_number);
  duration_max = max(0, duration_max);
  cooldown_until = max(0, cooldown_until);
  int32 left_tries = trial_left_tries_;
  if (cooldown_until <= G()->unix_time()) {
    cooldown_until = 0;
    left_tries = weekly_number;
  } else if (left_tries > weekly_number) {
    left_tries = weekly_number;
  }
  if (weekly_number == trial_weekly_number_ && duration_max == trial_duration_max_ && left_tries == trial_left_tries_ &&
      cooldown_until == trial_cooldown_until_) {
    return;
  }

  trial_weekly_number_ = weekly_number;
  trial_duration_max_ = duration_max;
  trial_left_tries_ = left_tries;
  trial_cooldown_until_ = cooldown_until;
  send_update_speech_recognition_trial();
}

void TranscriptionManager::send_update_speech_recognition_trial() const {
  CHECK(!td_->auth_manager_->is_bot());
  send_closure(G()->td(), &Td::send_update, get_update_speech_recognition_trial_object());
}

td_api::object_ptr<td_api::updateSpeechRecognitionTrial>
TranscriptionManager::get_update_speech_recognition_trial_object() const {
  return td_api::make_object<td_api::updateSpeechRecognitionTrial>(trial_duration_max_, trial_weekly_number_,
                                                                   trial_left_tries_, trial_cooldown_until_);
}

}  // namespace td
