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

bool operator==(const TranscriptionManager::TrialParameters &lhs, const TranscriptionManager::TrialParameters &rhs) {
  return lhs.weekly_number_ == rhs.weekly_number_ && lhs.duration_max_ == rhs.duration_max_ &&
         lhs.left_tries_ == rhs.left_tries_ && lhs.cooldown_until_ == rhs.cooldown_until_;
}

void TranscriptionManager::on_update_trial_parameters(int32 weekly_number, int32 duration_max, int32 cooldown_until) {
  if (!td_->auth_manager_->is_authorized()) {
    return;
  }
  CHECK(!td_->auth_manager_->is_bot());
  TrialParameters new_trial_parameters;
  new_trial_parameters.weekly_number_ = max(0, weekly_number);
  new_trial_parameters.duration_max_ = max(0, duration_max);
  new_trial_parameters.cooldown_until_ = max(0, cooldown_until);
  new_trial_parameters.left_tries_ = trial_parameters_.left_tries_;
  if (new_trial_parameters.cooldown_until_ <= G()->unix_time()) {
    new_trial_parameters.cooldown_until_ = 0;
    new_trial_parameters.left_tries_ = new_trial_parameters.weekly_number_;
  } else if (new_trial_parameters.left_tries_ > new_trial_parameters.weekly_number_) {
    new_trial_parameters.left_tries_ = new_trial_parameters.weekly_number_;
  }
  if (new_trial_parameters == trial_parameters_) {
    return;
  }

  trial_parameters_ = std::move(new_trial_parameters);
  send_update_speech_recognition_trial();
}

void TranscriptionManager::send_update_speech_recognition_trial() const {
  send_closure(G()->td(), &Td::send_update, get_update_speech_recognition_trial_object());
}

td_api::object_ptr<td_api::updateSpeechRecognitionTrial>
TranscriptionManager::get_update_speech_recognition_trial_object() const {
  CHECK(td_->auth_manager_->is_authorized());
  CHECK(!td_->auth_manager_->is_bot());
  return trial_parameters_.get_update_speech_recognition_trial_object();
}

td_api::object_ptr<td_api::updateSpeechRecognitionTrial>
TranscriptionManager::TrialParameters::get_update_speech_recognition_trial_object() const {
  return td_api::make_object<td_api::updateSpeechRecognitionTrial>(duration_max_, weekly_number_, left_tries_,
                                                                   cooldown_until_);
}

void TranscriptionManager::get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const {
  if (!td_->auth_manager_->is_authorized() || td_->auth_manager_->is_bot()) {
    return;
  }

  updates.push_back(get_update_speech_recognition_trial_object());
}

}  // namespace td
