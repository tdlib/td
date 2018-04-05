//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/SecureManager.h"

#include "td/telegram/Global.h"
#include "td/telegram/PasswordManager.h"
#include "td/telegram/net/NetQueryDispatcher.h"

namespace td {
GetSecureValue::GetSecureValue(ActorShared<> parent, std::string password, SecureValueType type,
                               Promise<TdApiSecureValue> promise)
    : parent_(std::move(parent)), password_(std::move(password)), type_(type), promise_(std::move(promise)) {
}

void GetSecureValue::on_error(Status status) {
  promise_.set_error(std::move(status));
  stop();
}

void GetSecureValue::on_secret(Result<secure_storage::Secret> r_secret, bool dummy) {
  LOG_IF(ERROR, r_secret.is_error()) << r_secret.error();
  LOG_IF(ERROR, r_secret.is_ok()) << r_secret.ok().get_hash();
  if (r_secret.is_error()) {
    return on_error(r_secret.move_as_error());
  }
  secret_ = r_secret.move_as_ok();
  loop();
}

void GetSecureValue::loop() {
  if (!encrypted_secure_value_ || !secret_) {
    return;
  }
  auto *file_manager = G()->file_manager().get_actor_unsafe();
  auto r_secure_value = decrypt_encrypted_secure_value(file_manager, *secret_, *encrypted_secure_value_);
  if (r_secure_value.is_error()) {
    return on_error(r_secure_value.move_as_error());
  }
  promise_.set_result(get_passport_data_object(file_manager, r_secure_value.move_as_ok()));
  stop();
}

void GetSecureValue::start_up() {
  std::vector<telegram_api::object_ptr<telegram_api::SecureValueType>> vec;
  vec.push_back(get_secure_value_type_telegram_object(type_));

  auto query = G()->net_query_creator().create(create_storer(telegram_api::account_getSecureValue(std::move(vec))));

  G()->net_query_dispatcher().dispatch_with_callback(std::move(query), actor_shared(this));

  send_closure(G()->password_manager(), &PasswordManager::get_secure_secret, password_, optional<int64>(),
               PromiseCreator::lambda([actor_id = actor_id(this)](Result<secure_storage::Secret> r_secret) {
                 send_closure(actor_id, &GetSecureValue::on_secret, std::move(r_secret), true);
               }));
}

void GetSecureValue::on_result(NetQueryPtr query) {
  auto r_result = fetch_result<telegram_api::account_getSecureValue>(std::move(query));
  if (r_result.is_error()) {
    return on_error(r_result.move_as_error());
  }
  auto result = r_result.move_as_ok();
  if (result.size() != 1) {
    return on_error(Status::Error(PSLICE() << "Expected vector of size 1 got " << result.size()));
  }
  LOG(ERROR) << to_string(result[0]);
  encrypted_secure_value_ = get_encrypted_secure_value(G()->file_manager().get_actor_unsafe(), std::move(result[0]));
  loop();
}

SetSecureValue::SetSecureValue(ActorShared<> parent, string password, SecureValue secure_value,
                               Promise<TdApiSecureValue> promise)
    : parent_(std::move(parent))
    , password_(std::move(password))
    , secure_value_(std::move(secure_value))
    , promise_(std::move(promise)) {
}

SetSecureValue::UploadCallback::UploadCallback(ActorId<SetSecureValue> actor_id) : actor_id_(actor_id) {
}

void SetSecureValue::UploadCallback::on_upload_ok(FileId file_id, tl_object_ptr<telegram_api::InputFile> input_file) {
  UNREACHABLE();
}
void SetSecureValue::UploadCallback::on_upload_encrypted_ok(
    FileId file_id, tl_object_ptr<telegram_api::InputEncryptedFile> input_file) {
  UNREACHABLE();
}
void SetSecureValue::UploadCallback::on_upload_secure_ok(FileId file_id,
                                                         tl_object_ptr<telegram_api::InputSecureFile> input_file) {
  send_closure(actor_id_, &SetSecureValue::on_upload_ok, file_id, std::move(input_file));
}
void SetSecureValue::UploadCallback::on_upload_error(FileId file_id, Status error) {
  send_closure(actor_id_, &SetSecureValue::on_upload_error, file_id, std::move(error));
}

void SetSecureValue::on_upload_ok(FileId file_id, tl_object_ptr<telegram_api::InputSecureFile> input_file) {
  SecureInputFile *info_ptr = nullptr;
  for (auto &info : to_upload_) {
    if (info.file_id != file_id) {
      continue;
    }
    info_ptr = &info;
    break;
  }
  if (selfie_ && selfie_.value().file_id == file_id) {
    info_ptr = &selfie_.value();
  }
  CHECK(info_ptr);
  auto &info = *info_ptr;
  CHECK(!info.input_file);
  info.input_file = std::move(input_file);
  CHECK(files_left_to_upload_ != 0);
  files_left_to_upload_--;
  return loop();
}
void SetSecureValue::on_upload_error(FileId file_id, Status error) {
  return on_error(std::move(error));
}

void SetSecureValue::on_error(Status status) {
  promise_.set_error(std::move(status));
  stop();
}

void SetSecureValue::on_secret(Result<secure_storage::Secret> r_secret, bool x) {
  LOG_IF(ERROR, r_secret.is_error()) << r_secret.error();
  LOG_IF(ERROR, r_secret.is_ok()) << r_secret.ok().get_hash();
  if (r_secret.is_error()) {
    return on_error(r_secret.move_as_error());
  }
  secret_ = r_secret.move_as_ok();
  loop();
}

void SetSecureValue::start_up() {
  send_closure(G()->password_manager(), &PasswordManager::get_secure_secret, password_, optional<int64>(),
               PromiseCreator::lambda([actor_id = actor_id(this)](Result<secure_storage::Secret> r_secret) {
                 send_closure(actor_id, &SetSecureValue::on_secret, std::move(r_secret), true);
               }));
  auto *file_manager = G()->file_manager().get_actor_unsafe();

  // Remove duplicated files
  for (auto it = secure_value_.files.begin(); it != secure_value_.files.end();) {
    bool is_duplicate = false;
    for (auto pit = secure_value_.files.begin(); pit != it; pit++) {
      if (file_manager->get_file_view(*it).file_id() == file_manager->get_file_view(*pit).file_id()) {
        is_duplicate = true;
        break;
      }
    }
    if (secure_value_.selfie.is_valid() &&
        file_manager->get_file_view(*it).file_id() == file_manager->get_file_view(secure_value_.selfie).file_id()) {
      is_duplicate = true;
    }
    if (is_duplicate) {
      it = secure_value_.files.erase(it);
    } else {
      it++;
    }
  }

  to_upload_.resize(secure_value_.files.size());
  upload_callback_ = std::make_shared<UploadCallback>(actor_id(this));
  for (size_t i = 0; i < to_upload_.size(); i++) {
    start_upload(file_manager, secure_value_.files[i], to_upload_[i]);
  }
  if (selfie_) {
    start_upload(file_manager, secure_value_.selfie, selfie_.value());
  }
}

void SetSecureValue::start_upload(FileManager *file_manager, FileId file_id, SecureInputFile &info) {
  info.file_id = file_manager->dup_file_id(file_id);
  file_manager->upload(info.file_id, upload_callback_, 1, 0);
  files_left_to_upload_++;
}

void SetSecureValue::loop() {
  if (state_ == State::WaitSecret) {
    if (!secret_) {
      return;
    }
    if (files_left_to_upload_ != 0) {
      return;
    }
    auto *file_manager = G()->file_manager().get_actor_unsafe();
    auto input_secure_value = get_input_secure_value_object(
        file_manager, encrypt_secure_value(file_manager, *secret_, secure_value_), to_upload_, selfie_);
    auto save_secure_value =
        telegram_api::account_saveSecureValue(std::move(input_secure_value), secret_.value().get_hash());
    LOG(ERROR) << to_string(save_secure_value);
    auto query = G()->net_query_creator().create(create_storer(save_secure_value));

    G()->net_query_dispatcher().dispatch_with_callback(std::move(query), actor_shared(this));
    state_ = State::WaitSetValue;
  }
}

void SetSecureValue::tear_down() {
  auto *file_manager = G()->file_manager().get_actor_unsafe();
  for (auto &file_info : to_upload_) {
    file_manager->upload(file_info.file_id, nullptr, 0, 0);
  }
}

void SetSecureValue::on_result(NetQueryPtr query) {
  auto r_result = fetch_result<telegram_api::account_saveSecureValue>(std::move(query));
  if (r_result.is_error()) {
    return on_error(r_result.move_as_error());
  }
  auto result = r_result.move_as_ok();
  LOG(ERROR) << to_string(result);
  auto *file_manager = G()->file_manager().get_actor_unsafe();
  auto encrypted_secure_value = get_encrypted_secure_value(file_manager, std::move(result));
  if (secure_value_.files.size() != encrypted_secure_value.files.size()) {
    return on_error(Status::Error("Different files count"));
  }
  for (size_t i = 0; i < secure_value_.files.size(); i++) {
    merge(file_manager, secure_value_.files[i], encrypted_secure_value.files[i]);
  }
  if (secure_value_.selfie.is_valid()) {
    merge(file_manager, secure_value_.selfie, encrypted_secure_value.selfie);
  }
  auto r_secure_value = decrypt_encrypted_secure_value(file_manager, *secret_, encrypted_secure_value);
  if (r_secure_value.is_error()) {
    return on_error(r_secure_value.move_as_error());
  }
  promise_.set_result(get_passport_data_object(file_manager, r_secure_value.move_as_ok()));
  stop();
}

void SetSecureValue::merge(FileManager *file_manager, FileId file_id, SecureFile &encrypted_file) {
  auto file_view = file_manager->get_file_view(file_id);
  CHECK(!file_view.empty());
  CHECK(file_view.encryption_key().has_value_hash());
  if (file_view.encryption_key().value_hash().as_slice() != encrypted_file.file_hash) {
    LOG(ERROR) << "hash mismatch";
    return;
  }
  auto status = file_manager->merge(encrypted_file.file_id, file_id);
  LOG_IF(ERROR, status.is_error()) << status.error();
}

SecureManager::SecureManager(ActorShared<> parent) : parent_(std::move(parent)) {
}

void SecureManager::get_secure_value(std::string password, SecureValueType type, Promise<TdApiSecureValue> promise) {
  refcnt_++;
  create_actor<GetSecureValue>("GetSecureValue", actor_shared(), std::move(password), type, std::move(promise))
      .release();
}

void SecureManager::set_secure_value(string password, SecureValue secure_value, Promise<TdApiSecureValue> promise) {
  refcnt_++;
  auto type = secure_value.type;
  set_secure_value_queries_[type] = create_actor<SetSecureValue>("SetSecureValue", actor_shared(), std::move(password),
                                                                 std::move(secure_value), std::move(promise));
}

void SecureManager::hangup() {
  dec_refcnt();
}

void SecureManager::hangup_shared() {
  dec_refcnt();
}
void SecureManager::dec_refcnt() {
  refcnt_--;
  if (refcnt_ == 0) {
    stop();
  }
}
}  // namespace td
