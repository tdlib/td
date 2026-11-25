//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/CallManager.h"

#include "td/telegram/DialogId.h"
#include "td/telegram/files/FileManager.h"
#include "td/telegram/files/FileType.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UpdatesManager.h"
#include "td/telegram/UserManager.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/FlatHashSet.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/SliceBuilder.h"

#include <limits>
#include <memory>

namespace td {

class SetCallRatingQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  InputCallId call_id_;

 public:
  explicit SetCallRatingQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(InputCallId call_id, telegram_api::object_ptr<telegram_api::inputPhoneCall> input_phone_call, int32 rating,
            const string &comment) {
    call_id_ = std::move(call_id);
    bool user_initiative = false;
    send_query(G()->net_query_creator().create(
        telegram_api::phone_setCallRating(0, user_initiative, std::move(input_phone_call), rating, comment)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::phone_setCallRating>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for SetCallRatingQuery: " << to_string(ptr);
    send_closure(G()->call_manager(), &CallManager::on_set_call_rating, std::move(call_id_));
    td_->updates_manager_->on_get_updates(std::move(ptr), std::move(promise_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class SaveCallDebugQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  InputCallId call_id_;

 public:
  explicit SaveCallDebugQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(InputCallId call_id, telegram_api::object_ptr<telegram_api::inputPhoneCall> input_phone_call,
            const string &data) {
    call_id_ = std::move(call_id);
    send_query(G()->net_query_creator().create(telegram_api::phone_saveCallDebug(
        std::move(input_phone_call), telegram_api::make_object<telegram_api::dataJSON>(std::move(data)))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::phone_saveCallDebug>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for SaveCallDebugQuery: " << ptr;
    send_closure(G()->call_manager(), &CallManager::on_save_debug_information, std::move(call_id_), ptr);
    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class SaveCallLogQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  InputCallId call_id_;
  FileUploadId file_upload_id_;

 public:
  explicit SaveCallLogQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(InputCallId call_id, telegram_api::object_ptr<telegram_api::inputPhoneCall> input_phone_call,
            FileUploadId file_upload_id, telegram_api::object_ptr<telegram_api::InputFile> &&input_file) {
    call_id_ = std::move(call_id);
    file_upload_id_ = file_upload_id;
    send_query(G()->net_query_creator().create(
        telegram_api::phone_saveCallLog(std::move(input_phone_call), std::move(input_file))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::phone_saveCallLog>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for SaveCallLogQuery: " << ptr;
    send_closure(G()->call_manager(), &CallManager::on_save_log, std::move(call_id_), file_upload_id_, Status::OK(),
                 std::move(promise_));
  }

  void on_error(Status status) final {
    send_closure(G()->call_manager(), &CallManager::on_save_log, std::move(call_id_), file_upload_id_,
                 std::move(status), std::move(promise_));
  }
};

CallManager::CallManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

void CallManager::tear_down() {
  parent_.reset();
}

void CallManager::update_call(telegram_api::object_ptr<telegram_api::updatePhoneCall> call) {
  auto call_id = [phone_call = call->phone_call_.get()] {
    switch (phone_call->get_id()) {
      case telegram_api::phoneCallEmpty::ID:
        return static_cast<const telegram_api::phoneCallEmpty *>(phone_call)->id_;
      case telegram_api::phoneCallWaiting::ID:
        return static_cast<const telegram_api::phoneCallWaiting *>(phone_call)->id_;
      case telegram_api::phoneCallRequested::ID:
        return static_cast<const telegram_api::phoneCallRequested *>(phone_call)->id_;
      case telegram_api::phoneCallAccepted::ID:
        return static_cast<const telegram_api::phoneCallAccepted *>(phone_call)->id_;
      case telegram_api::phoneCall::ID:
        return static_cast<const telegram_api::phoneCall *>(phone_call)->id_;
      case telegram_api::phoneCallDiscarded::ID:
        return static_cast<const telegram_api::phoneCallDiscarded *>(phone_call)->id_;
      default:
        UNREACHABLE();
        return static_cast<int64>(0);
    }
  }();
  LOG(DEBUG) << "Receive UpdateCall for " << call_id;

  auto &info = call_info_[call_id];

  if (!info.call_id.is_valid() && call->phone_call_->get_id() == telegram_api::phoneCallRequested::ID) {
    info.call_id = create_call_actor();
  }

  if (!info.call_id.is_valid()) {
    LOG(INFO) << "Call identifier is not valid for " << call_id << ", postpone update " << to_string(call);
    info.updates.push_back(std::move(call));
    return;
  }

  auto actor = get_call_actor(info.call_id);
  if (actor.empty()) {
    LOG(INFO) << "Drop update: " << to_string(call);
  }
  send_closure(actor, &CallActor::update_call, std::move(call->phone_call_));
}

void CallManager::update_call_signaling_data(int64 call_id, string data) {
  auto info_it = call_info_.find(call_id);
  if (info_it == call_info_.end() || !info_it->second.call_id.is_valid()) {
    LOG(INFO) << "Ignore signaling data for " << call_id;
    return;
  }

  auto actor = get_call_actor(info_it->second.call_id);
  if (actor.empty()) {
    LOG(INFO) << "Ignore signaling data for " << info_it->second.call_id;
    return;
  }
  send_closure(actor, &CallActor::update_call_signaling_data, std::move(data));
}

void CallManager::create_call(UserId user_id, CallProtocol &&protocol, bool is_video, Promise<CallId> promise) {
  TRY_STATUS_PROMISE(promise, td_->user_manager_->get_input_user(user_id));
  LOG(INFO) << "Create call with " << user_id;
  auto call_id = create_call_actor();
  auto actor = get_call_actor(call_id);
  CHECK(!actor.empty());
  auto safe_promise = SafePromise<CallId>(std::move(promise), Status::Error(400, "Call not found"));
  send_closure(actor, &CallActor::create_call, user_id, std::move(protocol), is_video, std::move(safe_promise));
}

void CallManager::accept_call(CallId call_id, CallProtocol &&protocol, Promise<Unit> promise) {
  auto actor = get_call_actor(call_id);
  if (actor.empty()) {
    return promise.set_error(400, "Call not found");
  }
  auto safe_promise = SafePromise<Unit>(std::move(promise), Status::Error(400, "Call not found"));
  send_closure(actor, &CallActor::accept_call, std::move(protocol), std::move(safe_promise));
}

void CallManager::send_call_signaling_data(CallId call_id, string &&data, Promise<Unit> promise) {
  auto actor = get_call_actor(call_id);
  if (actor.empty()) {
    return promise.set_error(400, "Call not found");
  }
  auto safe_promise = SafePromise<Unit>(std::move(promise), Status::Error(400, "Call not found"));
  send_closure(actor, &CallActor::send_call_signaling_data, std::move(data), std::move(safe_promise));
}

void CallManager::discard_call(CallId call_id, bool is_disconnected, const string &invite_link, int32 duration,
                               bool is_video, int64 connection_id, Promise<Unit> promise) {
  auto actor = get_call_actor(call_id);
  if (actor.empty()) {
    return promise.set_error(400, "Call not found");
  }
  auto safe_promise = SafePromise<Unit>(std::move(promise), Status::Error(400, "Call not found"));
  send_closure(actor, &CallActor::discard_call, is_disconnected, invite_link, duration, is_video, connection_id,
               std::move(safe_promise));
}

void CallManager::on_closing() {
  for (auto &it : id_to_actor_) {
    send_closure(it.second, &CallActor::on_closing);
  }
}

void CallManager::fetch_input_phone_call(InputCallId call_id,
                                         Promise<telegram_api::object_ptr<telegram_api::inputPhoneCall>> &&promise) {
  switch (call_id.get_type()) {
    case InputCallId::Type::Call: {
      auto actor = get_call_actor(call_id.get_call_id());
      if (actor.empty()) {
        return promise.set_error(400, "Call not found");
      }
      auto safe_promise = SafePromise<telegram_api::object_ptr<telegram_api::inputPhoneCall>>(
          std::move(promise), Status::Error(400, "Call not found"));
      send_closure(actor, &CallActor::get_input_phone_call_to_promise, std::move(safe_promise));
      break;
    }
    case InputCallId::Type::Message: {
      send_closure(G()->messages_manager(), &MessagesManager::get_input_phone_call_to_promise,
                   call_id.get_message_full_id(), std::move(promise));
      break;
    }
    default:
      UNREACHABLE();
  }
}

void CallManager::rate_call(td_api::object_ptr<td_api::InputCall> &&input_call, int32 rating, string comment,
                            vector<td_api::object_ptr<td_api::CallProblem>> &&problems, Promise<Unit> promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  if (rating < 1 || rating > 5) {
    return promise.set_error(400, "Invalid rating specified");
  }
  TRY_RESULT_PROMISE(promise, call_id, InputCallId::get_input_call_id(input_call));
  fetch_input_phone_call(
      call_id, [actor_id = actor_id(this), call_id, rating, comment = std::move(comment),
                problems = std::move(problems), promise = std::move(promise)](
                   Result<telegram_api::object_ptr<telegram_api::inputPhoneCall>> r_input_phone_call) mutable {
        if (r_input_phone_call.is_error()) {
          return promise.set_error(r_input_phone_call.move_as_error());
        }
        send_closure(actor_id, &CallManager::do_rate_call, std::move(call_id), r_input_phone_call.move_as_ok(), rating,
                     std::move(comment), std::move(problems), std::move(promise));
      });
}

void CallManager::do_rate_call(InputCallId call_id,
                               telegram_api::object_ptr<telegram_api::inputPhoneCall> input_phone_call, int32 rating,
                               string comment, vector<td_api::object_ptr<td_api::CallProblem>> &&problems,
                               Promise<Unit> promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  if (rating == 5) {
    comment.clear();
  }

  FlatHashSet<string> tags;
  for (auto &problem : problems) {
    if (problem == nullptr) {
      continue;
    }

    const char *tag = [problem_id = problem->get_id()] {
      switch (problem_id) {
        case td_api::callProblemEcho::ID:
          return "echo";
        case td_api::callProblemNoise::ID:
          return "noise";
        case td_api::callProblemInterruptions::ID:
          return "interruptions";
        case td_api::callProblemDistortedSpeech::ID:
          return "distorted_speech";
        case td_api::callProblemSilentLocal::ID:
          return "silent_local";
        case td_api::callProblemSilentRemote::ID:
          return "silent_remote";
        case td_api::callProblemDropped::ID:
          return "dropped";
        case td_api::callProblemDistortedVideo::ID:
          return "distorted_video";
        case td_api::callProblemPixelatedVideo::ID:
          return "pixelated_video";
        default:
          UNREACHABLE();
          return "";
      }
    }();
    if (tags.insert(tag).second) {
      if (!comment.empty()) {
        comment += ' ';
      }
      comment += '#';
      comment += tag;
    }
  }

  td_->create_handler<SetCallRatingQuery>(std::move(promise))
      ->send(call_id, std::move(input_phone_call), rating, comment);
}

void CallManager::on_set_call_rating(InputCallId call_id) {
  if (call_id.get_type() == InputCallId::Type::Call) {
    auto actor = get_call_actor(call_id.get_call_id());
    if (!actor.empty()) {
      send_closure(actor, &CallActor::on_set_call_rating);
    }
  }
}

void CallManager::send_call_debug_information(td_api::object_ptr<td_api::InputCall> &&input_call, string data,
                                              Promise<Unit> promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  TRY_RESULT_PROMISE(promise, call_id, InputCallId::get_input_call_id(input_call));
  fetch_input_phone_call(
      call_id, [actor_id = actor_id(this), call_id, data = std::move(data), promise = std::move(promise)](
                   Result<telegram_api::object_ptr<telegram_api::inputPhoneCall>> r_input_phone_call) mutable {
        if (r_input_phone_call.is_error()) {
          return promise.set_error(r_input_phone_call.move_as_error());
        }
        send_closure(actor_id, &CallManager::do_send_call_debug_information, std::move(call_id),
                     r_input_phone_call.move_as_ok(), std::move(data), std::move(promise));
      });
}

void CallManager::do_send_call_debug_information(
    InputCallId call_id, telegram_api::object_ptr<telegram_api::inputPhoneCall> input_phone_call, string data,
    Promise<Unit> promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  td_->create_handler<SaveCallDebugQuery>(std::move(promise))->send(call_id, std::move(input_phone_call), data);
}

void CallManager::on_save_debug_information(InputCallId call_id, bool result) {
  if (call_id.get_type() == InputCallId::Type::Call) {
    auto actor = get_call_actor(call_id.get_call_id());
    if (!actor.empty()) {
      send_closure(actor, &CallActor::on_save_debug_information, result);
    }
  }
}

void CallManager::send_call_log(td_api::object_ptr<td_api::InputCall> &&input_call,
                                td_api::object_ptr<td_api::InputFile> log_file, Promise<Unit> promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  TRY_RESULT_PROMISE(promise, call_id, InputCallId::get_input_call_id(input_call));

  auto *file_manager = td_->file_manager_.get();
  TRY_RESULT_PROMISE(promise, file_id,
                     file_manager->get_input_file_id(FileType::CallLog, log_file, DialogId(), false, false));

  FileView file_view = file_manager->get_file_view(file_id);
  if (file_view.is_encrypted()) {
    return promise.set_error(400, "Can't use encrypted file");
  }
  if (!file_view.has_full_local_location() && !file_view.has_generate_location()) {
    return promise.set_error(400, "Need local or generate location to upload call log");
  }

  upload_log_file(std::move(call_id), {file_id, FileManager::get_internal_upload_id()}, std::move(promise));
}

void CallManager::upload_log_file(InputCallId call_id, FileUploadId file_upload_id, Promise<Unit> &&promise) {
  LOG(INFO) << "Ask to upload call log " << file_upload_id;

  class UploadLogFileCallback final : public FileManager::UploadCallback {
    ActorId<CallManager> actor_id_;
    InputCallId call_id_;
    Promise<Unit> promise_;

   public:
    UploadLogFileCallback(ActorId<CallManager> actor_id, InputCallId call_id, Promise<Unit> &&promise)
        : actor_id_(actor_id), call_id_(std::move(call_id)), promise_(std::move(promise)) {
    }

    void on_upload_ok(FileUploadId file_upload_id, telegram_api::object_ptr<telegram_api::InputFile> input_file) final {
      send_closure_later(actor_id_, &CallManager::on_upload_log_file, std::move(call_id_), file_upload_id,
                         std::move(promise_), std::move(input_file));
    }

    void on_upload_error(FileUploadId file_upload_id, Status error) final {
      send_closure_later(actor_id_, &CallManager::on_upload_log_file_error, std::move(call_id_), file_upload_id,
                         std::move(promise_), std::move(error));
    }
  };

  send_closure(G()->file_manager(), &FileManager::upload, file_upload_id,
               std::make_shared<UploadLogFileCallback>(actor_id(this), std::move(call_id), std::move(promise)), 1, 0);
}

void CallManager::on_upload_log_file(InputCallId call_id, FileUploadId file_upload_id, Promise<Unit> &&promise,
                                     telegram_api::object_ptr<telegram_api::InputFile> input_file) {
  TRY_STATUS_PROMISE(promise, G()->close_status());

  LOG(INFO) << "Log " << file_upload_id << " for " << call_id << " has been uploaded";

  if (input_file == nullptr) {
    return promise.set_error(500, "Failed to reupload call log");
  }

  fetch_input_phone_call(
      call_id, [actor_id = actor_id(this), call_id, file_upload_id, input_file = std::move(input_file),
                promise = std::move(promise)](
                   Result<telegram_api::object_ptr<telegram_api::inputPhoneCall>> r_input_phone_call) mutable {
        if (r_input_phone_call.is_error()) {
          send_closure(G()->file_manager(), &FileManager::delete_partial_remote_location, file_upload_id);
          return promise.set_error(r_input_phone_call.move_as_error());
        }
        send_closure(actor_id, &CallManager::do_send_call_log, std::move(call_id), r_input_phone_call.move_as_ok(),
                     file_upload_id, std::move(input_file), std::move(promise));
      });
}

void CallManager::on_upload_log_file_error(InputCallId call_id, FileUploadId file_upload_id, Promise<Unit> &&promise,
                                           Status status) {
  TRY_STATUS_PROMISE(promise, G()->close_status());

  LOG(WARNING) << "Log " << file_upload_id << " for " << call_id << " has upload error " << status;
  CHECK(status.is_error());

  promise.set_error(status.code() > 0 ? status.code() : 500,
                    status.message());  // TODO CHECK that status has always a code
}

void CallManager::do_send_call_log(InputCallId call_id,
                                   telegram_api::object_ptr<telegram_api::inputPhoneCall> input_phone_call,
                                   FileUploadId file_upload_id,
                                   telegram_api::object_ptr<telegram_api::InputFile> &&input_file,
                                   Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  td_->create_handler<SaveCallLogQuery>(std::move(promise))
      ->send(call_id, std::move(input_phone_call), file_upload_id, std::move(input_file));
}

void CallManager::on_save_log(InputCallId call_id, FileUploadId file_upload_id, Status status, Promise<Unit> promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());

  send_closure(G()->file_manager(), &FileManager::delete_partial_remote_location, file_upload_id);

  if (status.is_error()) {
    auto bad_parts = FileManager::get_missing_file_parts(status);
    if (!bad_parts.empty()) {
      // TODO on_upload_log_file_parts_missing(file_upload_id, std::move(bad_parts));
      // return;
    }
    return promise.set_error(std::move(status));
  }
  if (call_id.get_type() == InputCallId::Type::Call) {
    auto actor = get_call_actor(call_id.get_call_id());
    if (!actor.empty()) {
      send_closure(actor, &CallActor::on_save_log);
    }
  }
  promise.set_value(Unit());
}

CallId CallManager::create_call_actor() {
  if (next_call_id_ == std::numeric_limits<int32>::max()) {
    next_call_id_ = 1;
  }
  auto id = CallId(next_call_id_++);
  CHECK(id.is_valid());
  auto it_flag = id_to_actor_.emplace(id, ActorOwn<CallActor>());
  CHECK(it_flag.second);
  LOG(INFO) << "Create CallActor: " << id;
  auto main_promise = PromiseCreator::lambda([actor_id = actor_id(this), id](Result<int64> call_id) {
    send_closure(actor_id, &CallManager::set_call_id, id, std::move(call_id));
  });
  it_flag.first->second = create_actor<CallActor>(PSLICE() << "Call " << id.get(), td_, id,
                                                  actor_shared(this, id.get()), std::move(main_promise));
  return id;
}

void CallManager::set_call_id(CallId call_id, Result<int64> r_server_call_id) {
  if (r_server_call_id.is_error()) {
    return;
  }
  auto server_call_id = r_server_call_id.move_as_ok();
  auto &call_info = call_info_[server_call_id];
  CHECK(!call_info.call_id.is_valid() || call_info.call_id == call_id);
  call_info.call_id = call_id;
  auto actor = get_call_actor(call_id);
  if (actor.empty()) {
    return;
  }
  for (auto &update : call_info.updates) {
    send_closure(actor, &CallActor::update_call, std::move(update->phone_call_));
  }
  call_info.updates.clear();
}

ActorId<CallActor> CallManager::get_call_actor(CallId call_id) {
  auto it = id_to_actor_.find(call_id);
  if (it == id_to_actor_.end()) {
    return ActorId<CallActor>();
  }
  return it->second.get();
}

void CallManager::hangup() {
  close_flag_ = true;
  for (auto &it : id_to_actor_) {
    LOG(INFO) << "Ask to close CallActor " << it.first.get();
    it.second.reset();
  }
  if (id_to_actor_.empty()) {
    stop();
  }
}

void CallManager::hangup_shared() {
  auto token = narrow_cast<int32>(get_link_token());
  auto it = id_to_actor_.find(CallId(token));
  CHECK(it != id_to_actor_.end());
  LOG(INFO) << "Closed CallActor " << it->first.get();
  it->second.release();
  id_to_actor_.erase(it);
  if (close_flag_ && id_to_actor_.empty()) {
    stop();
  }
}

}  // namespace td
