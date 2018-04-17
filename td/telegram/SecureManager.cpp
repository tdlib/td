//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/SecureManager.h"

#include "td/telegram/ContactsManager.h"
#include "td/telegram/files/FileManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/misc.h"
#include "td/telegram/net/NetQueryDispatcher.h"
#include "td/telegram/PasswordManager.h"
#include "td/telegram/Td.h"

#include "td/utils/buffer.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"

#include <mutex>

namespace td {

class SetSecureValueErrorsQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit SetSecureValueErrorsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(tl_object_ptr<telegram_api::InputUser> input_user,
            vector<tl_object_ptr<telegram_api::SecureValueError>> input_errors) {
    send_query(G()->net_query_creator().create(
        create_storer(telegram_api::account_setSecureValueErrors(std::move(input_user), std::move(input_errors)))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::account_setSecureValueErrors>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    bool ptr = result_ptr.move_as_ok();
    LOG(DEBUG) << "Receive result for SetSecureValueErrorsQuery " << ptr;
    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

GetSecureValue::GetSecureValue(ActorShared<> parent, std::string password, SecureValueType type,
                               Promise<SecureValueWithCredentials> promise)
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

  auto *file_manager = G()->td().get_actor_unsafe()->file_manager_.get();
  auto r_secure_value = decrypt_encrypted_secure_value(file_manager, *secret_, *encrypted_secure_value_);
  if (r_secure_value.is_error()) {
    return on_error(r_secure_value.move_as_error());
  }
  promise_.set_result(r_secure_value.move_as_ok());
  stop();
}

void GetSecureValue::start_up() {
  std::vector<telegram_api::object_ptr<telegram_api::SecureValueType>> vec;
  vec.push_back(get_secure_value_type_object(type_));

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
  if (result.empty()) {
    return on_error(Status::Error(404, "Not Found"));
  }
  if (result.size() != 1) {
    return on_error(Status::Error(PSLICE() << "Expected vector of size 1 got " << result.size()));
  }
  LOG(ERROR) << to_string(result[0]);
  encrypted_secure_value_ =
      get_encrypted_secure_value(G()->td().get_actor_unsafe()->file_manager_.get(), std::move(result[0]));
  loop();
}

GetAllSecureValues::GetAllSecureValues(ActorShared<> parent, std::string password,
                                       Promise<TdApiAllSecureValues> promise)
    : parent_(std::move(parent)), password_(std::move(password)), promise_(std::move(promise)) {
}

void GetAllSecureValues::on_error(Status status) {
  promise_.set_error(std::move(status));
  stop();
}

void GetAllSecureValues::on_secret(Result<secure_storage::Secret> r_secret, bool dummy) {
  LOG_IF(ERROR, r_secret.is_error()) << r_secret.error();
  LOG_IF(ERROR, r_secret.is_ok()) << r_secret.ok().get_hash();
  if (r_secret.is_error()) {
    return on_error(r_secret.move_as_error());
  }
  secret_ = r_secret.move_as_ok();
  loop();
}

void GetAllSecureValues::loop() {
  if (!encrypted_secure_values_ || !secret_) {
    return;
  }

  auto *file_manager = G()->td().get_actor_unsafe()->file_manager_.get();
  auto r_secure_values = decrypt_encrypted_secure_values(file_manager, *secret_, *encrypted_secure_values_);
  if (r_secure_values.is_error()) {
    return on_error(r_secure_values.move_as_error());
  }
  auto secure_values = transform(r_secure_values.move_as_ok(),
                                 [](SecureValueWithCredentials &&value) { return std::move(value.value); });
  promise_.set_result(get_all_passport_data_object(file_manager, std::move(secure_values)));
  stop();
}

void GetAllSecureValues::start_up() {
  auto query = G()->net_query_creator().create(create_storer(telegram_api::account_getAllSecureValues()));

  G()->net_query_dispatcher().dispatch_with_callback(std::move(query), actor_shared(this));

  send_closure(G()->password_manager(), &PasswordManager::get_secure_secret, password_, optional<int64>(),
               PromiseCreator::lambda([actor_id = actor_id(this)](Result<secure_storage::Secret> r_secret) {
                 send_closure(actor_id, &GetAllSecureValues::on_secret, std::move(r_secret), true);
               }));
}

void GetAllSecureValues::on_result(NetQueryPtr query) {
  auto r_result = fetch_result<telegram_api::account_getAllSecureValues>(std::move(query));
  if (r_result.is_error()) {
    return on_error(r_result.move_as_error());
  }
  encrypted_secure_values_ =
      get_encrypted_secure_values(G()->td().get_actor_unsafe()->file_manager_.get(), r_result.move_as_ok());
  loop();
}

SetSecureValue::SetSecureValue(ActorShared<> parent, string password, SecureValue secure_value,
                               Promise<SecureValueWithCredentials> promise)
    : parent_(std::move(parent))
    , password_(std::move(password))
    , secure_value_(std::move(secure_value))
    , promise_(std::move(promise)) {
}

SetSecureValue::UploadCallback::UploadCallback(ActorId<SetSecureValue> actor_id) : actor_id_(actor_id) {
}

void SetSecureValue::UploadCallback::on_upload_ok(FileId file_id, tl_object_ptr<telegram_api::InputFile> input_file) {
  send_closure(actor_id_, &SetSecureValue::on_upload_ok, file_id, nullptr);
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
  auto *file_manager = G()->td().get_actor_unsafe()->file_manager_.get();

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
    auto *file_manager = G()->td().get_actor_unsafe()->file_manager_.get();
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
  auto *file_manager = G()->td().get_actor_unsafe()->file_manager_.get();
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
  auto *file_manager = G()->td().get_actor_unsafe()->file_manager_.get();
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
  promise_.set_result(r_secure_value.move_as_ok());
  stop();
}

void SetSecureValue::merge(FileManager *file_manager, FileId file_id, EncryptedSecureFile &encrypted_file) {
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

class GetPassportAuthorizationForm : public NetQueryCallback {
 public:
  GetPassportAuthorizationForm(ActorShared<> parent, string password, int32 authorization_form_id, UserId bot_user_id,
                               string scope, string public_key, Promise<TdApiAuthorizationForm> promise)
      : parent_(std::move(parent))
      , password_(std::move(password))
      , authorization_form_id_(authorization_form_id)
      , bot_user_id_(bot_user_id)
      , scope_(std::move(scope))
      , public_key_(std::move(public_key))
      , promise_(std::move(promise)) {
  }

 private:
  ActorShared<> parent_;
  string password_;
  int32 authorization_form_id_;
  UserId bot_user_id_;
  string scope_;
  string public_key_;
  Promise<TdApiAuthorizationForm> promise_;
  optional<secure_storage::Secret> secret_;
  telegram_api::object_ptr<telegram_api::account_authorizationForm> authorization_form_;

  void on_secret(Result<secure_storage::Secret> r_secret, bool dummy) {
    LOG_IF(ERROR, r_secret.is_error()) << r_secret.error();
    LOG_IF(ERROR, r_secret.is_ok()) << r_secret.ok().get_hash();
    if (r_secret.is_error()) {
      return on_error(r_secret.move_as_error());
    }
    secret_ = r_secret.move_as_ok();
    loop();
  }

  void on_error(Status status) {
    promise_.set_error(std::move(status));
    stop();
  }

  void start_up() override {
    auto account_get_authorization_form =
        telegram_api::account_getAuthorizationForm(bot_user_id_.get(), std::move(scope_), std::move(public_key_));
    auto query = G()->net_query_creator().create(create_storer(account_get_authorization_form));
    G()->net_query_dispatcher().dispatch_with_callback(std::move(query), actor_shared(this));

    send_closure(G()->password_manager(), &PasswordManager::get_secure_secret, password_, optional<int64>(),
                 PromiseCreator::lambda([actor_id = actor_id(this)](Result<secure_storage::Secret> r_secret) {
                   send_closure(actor_id, &GetPassportAuthorizationForm::on_secret, std::move(r_secret), true);
                 }));
  }

  void on_result(NetQueryPtr query) override {
    auto r_result = fetch_result<telegram_api::account_getAuthorizationForm>(std::move(query));
    if (r_result.is_error()) {
      return on_error(r_result.move_as_error());
    }
    authorization_form_ = r_result.move_as_ok();
    loop();
  }

  void loop() override {
    if (!secret_ || !authorization_form_) {
      return;
    }

    G()->td().get_actor_unsafe()->contacts_manager_->on_get_users(std::move(authorization_form_->users_));

    auto *file_manager = G()->td().get_actor_unsafe()->file_manager_.get();
    std::vector<TdApiSecureValue> values;
    bool is_selfie_required =
        (authorization_form_->flags_ & telegram_api::account_authorizationForm::SELFIE_REQUIRED_MASK) != 0;
    auto types = get_secure_value_types(authorization_form_->required_types_);
    for (auto type : types) {
      for (auto &value : authorization_form_->values_) {
        if (value == nullptr) {
          continue;
        }
        auto value_type = get_secure_value_type(value->type_);
        if (value_type != type) {
          continue;
        }

        auto r_secure_value = decrypt_encrypted_secure_value(
            file_manager, *secret_, get_encrypted_secure_value(file_manager, std::move(value)));
        value = nullptr;
        if (r_secure_value.is_error()) {
          LOG(ERROR) << "Failed to decrypt secure value: " << r_secure_value.error();
          break;
        }

        auto r_passport_data = get_passport_data_object(file_manager, std::move(r_secure_value.move_as_ok().value));
        if (r_passport_data.is_error()) {
          LOG(ERROR) << "Failed to get passport data object: " << r_passport_data.error();
          break;
        }

        values.push_back(r_passport_data.move_as_ok());
        break;
      }
    }
    promise_.set_value(make_tl_object<td_api::passportAuthorizationForm>(
        authorization_form_id_, get_passport_data_types_object(types), std::move(values), is_selfie_required,
        authorization_form_->privacy_policy_url_));
    stop();
  }
};

SecureManager::SecureManager(ActorShared<> parent) : parent_(std::move(parent)) {
}

void SecureManager::get_secure_value(std::string password, SecureValueType type, Promise<TdApiSecureValue> promise) {
  auto new_promise =
      PromiseCreator::lambda([promise = std::move(promise)](Result<SecureValueWithCredentials> r_secure_value) mutable {
        if (r_secure_value.is_error()) {
          return promise.set_error(r_secure_value.move_as_error());
        }
        auto *file_manager = G()->td().get_actor_unsafe()->file_manager_.get();
        auto r_passport_data = get_passport_data_object(file_manager, r_secure_value.move_as_ok().value);
        if (r_passport_data.is_error()) {
          LOG(ERROR) << "Failed to get passport data object: " << r_passport_data.error();
          return promise.set_value(nullptr);
        }
        promise.set_value(r_passport_data.move_as_ok());
      });
  do_get_secure_value(std::move(password), type, std::move(new_promise));
}

void SecureManager::do_get_secure_value(std::string password, SecureValueType type,
                                        Promise<SecureValueWithCredentials> promise) {
  refcnt_++;
  create_actor<GetSecureValue>("GetSecureValue", actor_shared(), std::move(password), type, std::move(promise))
      .release();
}

void SecureManager::get_all_secure_values(std::string password, Promise<TdApiAllSecureValues> promise) {
  refcnt_++;
  create_actor<GetAllSecureValues>("GetAllSecureValues", actor_shared(), std::move(password), std::move(promise))
      .release();
}

void SecureManager::set_secure_value(string password, SecureValue secure_value, Promise<TdApiSecureValue> promise) {
  refcnt_++;
  auto type = secure_value.type;
  auto new_promise =
      PromiseCreator::lambda([promise = std::move(promise)](Result<SecureValueWithCredentials> r_secure_value) mutable {
        if (r_secure_value.is_error()) {
          return promise.set_error(r_secure_value.move_as_error());
        }
        auto *file_manager = G()->td().get_actor_unsafe()->file_manager_.get();
        auto r_passport_data = get_passport_data_object(file_manager, r_secure_value.move_as_ok().value);
        if (r_passport_data.is_error()) {
          LOG(ERROR) << "Failed to get passport data object: " << r_passport_data.error();
          return promise.set_error(Status::Error(500, "Failed to get passport data object"));
        }
        promise.set_value(r_passport_data.move_as_ok());
      });
  set_secure_value_queries_[type] = create_actor<SetSecureValue>("SetSecureValue", actor_shared(), std::move(password),
                                                                 std::move(secure_value), std::move(new_promise));
}

void SecureManager::delete_secure_value(SecureValueType type, Promise<Unit> promise) {
  // TODO
}

void SecureManager::set_secure_value_errors(Td *td, tl_object_ptr<telegram_api::InputUser> input_user,
                                            vector<tl_object_ptr<td_api::PassportDataError>> errors,
                                            Promise<Unit> promise) {
  CHECK(td != nullptr);
  CHECK(input_user != nullptr);
  vector<tl_object_ptr<telegram_api::SecureValueError>> input_errors;
  for (auto &error_ptr : errors) {
    if (error_ptr == nullptr) {
      return promise.set_error(Status::Error(400, "Error must be non-empty"));
    }
    switch (error_ptr->get_id()) {
      case td_api::passportDataErrorDataField::ID: {
        auto error = td_api::move_object_as<td_api::passportDataErrorDataField>(error_ptr);
        if (error->type_ == nullptr) {
          return promise.set_error(Status::Error(400, "Type must be non-empty"));
        }
        if (!clean_input_string(error->message_)) {
          return promise.set_error(Status::Error(400, "Error message must be encoded in UTF-8"));
        }
        if (!clean_input_string(error->field_name_)) {
          return promise.set_error(Status::Error(400, "Field name must be encoded in UTF-8"));
        }

        auto type = get_secure_value_type_object(get_secure_value_type_td_api(error->type_));
        input_errors.push_back(make_tl_object<telegram_api::secureValueErrorData>(
            std::move(type), BufferSlice(error->data_hash_), error->field_name_, error->message_));
        break;
      }
      case td_api::passportDataErrorFile::ID: {
        auto error = td_api::move_object_as<td_api::passportDataErrorFile>(error_ptr);
        if (error->type_ == nullptr) {
          return promise.set_error(Status::Error(400, "Type must be non-empty"));
        }
        if (!clean_input_string(error->message_)) {
          return promise.set_error(Status::Error(400, "Error message must be encoded in UTF-8"));
        }

        auto type = get_secure_value_type_object(get_secure_value_type_td_api(error->type_));
        input_errors.push_back(make_tl_object<telegram_api::secureValueErrorFile>(
            std::move(type), BufferSlice(error->file_hash_), error->message_));
        break;
      }
      case td_api::passportDataErrorFiles::ID: {
        auto error = td_api::move_object_as<td_api::passportDataErrorFiles>(error_ptr);
        if (error->type_ == nullptr) {
          return promise.set_error(Status::Error(400, "Type must be non-empty"));
        }
        if (!clean_input_string(error->message_)) {
          return promise.set_error(Status::Error(400, "Error message must be encoded in UTF-8"));
        }
        if (error->file_hashes_.empty()) {
          return promise.set_error(Status::Error(400, "Error hashes must be non-empty"));
        }

        auto type = get_secure_value_type_object(get_secure_value_type_td_api(error->type_));
        auto file_hashes = transform(error->file_hashes_, [](Slice hash) { return BufferSlice(hash); });
        input_errors.push_back(make_tl_object<telegram_api::secureValueErrorFiles>(
            std::move(type), std::move(file_hashes), error->message_));
        break;
      }
      case td_api::passportDataErrorSelfie::ID: {
        auto error = td_api::move_object_as<td_api::passportDataErrorSelfie>(error_ptr);
        if (error->type_ == nullptr) {
          return promise.set_error(Status::Error(400, "Type must be non-empty"));
        }
        if (!clean_input_string(error->message_)) {
          return promise.set_error(Status::Error(400, "Error message must be encoded in UTF-8"));
        }

        auto type = get_secure_value_type_object(get_secure_value_type_td_api(error->type_));
        input_errors.push_back(make_tl_object<telegram_api::secureValueErrorSelfie>(
            std::move(type), BufferSlice(error->file_hash_), error->message_));
        break;
      }
      default:
        UNREACHABLE();
    }
  }
  td->create_handler<SetSecureValueErrorsQuery>(std::move(promise))
      ->send(std::move(input_user), std::move(input_errors));
}

void SecureManager::get_passport_authorization_form(string password, UserId bot_user_id, string scope,
                                                    string public_key, string payload,
                                                    Promise<TdApiAuthorizationForm> promise) {
  refcnt_++;
  auto authorization_form_id = ++authorization_form_id_;
  authorization_forms_[authorization_form_id] =
      AuthorizationForm{bot_user_id, scope, public_key, payload, false, false};
  auto new_promise =
      PromiseCreator::lambda([actor_id = actor_id(this), authorization_form_id, promise = std::move(promise)](
                                 Result<TdApiAuthorizationForm> r_authorization_form) mutable {
        send_closure(actor_id, &SecureManager::on_get_passport_authorization_form, authorization_form_id,
                     std::move(promise), std::move(r_authorization_form));
      });
  create_actor<GetPassportAuthorizationForm>("GetPassportAuthorizationForm", actor_shared(), std::move(password),
                                             authorization_form_id, bot_user_id, std::move(scope),
                                             std::move(public_key), std::move(new_promise))
      .release();
}

void SecureManager::on_get_passport_authorization_form(int32 authorization_form_id,
                                                       Promise<TdApiAuthorizationForm> promise,
                                                       Result<TdApiAuthorizationForm> r_authorization_form) {
  auto it = authorization_forms_.find(authorization_form_id);
  CHECK(it != authorization_forms_.end());
  CHECK(it->second.is_received == false);
  if (r_authorization_form.is_error()) {
    authorization_forms_.erase(it);
    return promise.set_error(r_authorization_form.move_as_error());
  }
  it->second.is_received = true;

  auto authorization_form = r_authorization_form.move_as_ok();
  CHECK(authorization_form != nullptr);
  it->second.is_selfie_required = authorization_form->is_selfie_required_;
  promise.set_value(std::move(authorization_form));
}

void SecureManager::send_passport_authorization_form(string password, int32 authorization_form_id,
                                                     std::vector<SecureValueType> types, Promise<> promise) {
  auto it = authorization_forms_.find(authorization_form_id);
  if (it == authorization_forms_.end()) {
    return promise.set_error(Status::Error(400, "Unknown authorization_form_id"));
  }
  if (!it->second.is_received) {
    return promise.set_error(Status::Error(400, "Authorization form isn't received yet"));
  }
  if (types.empty()) {
    return promise.set_error(Status::Error(400, "Types must be non-empty"));
  }

  struct JoinPromise {
    std::mutex mutex_;
    Promise<std::vector<SecureValueCredentials>> promise_;
    std::vector<SecureValueCredentials> credentials_;
    int wait_cnt_{0};
  };

  auto join = std::make_shared<JoinPromise>();
  std::lock_guard<std::mutex> guard(join->mutex_);
  for (auto type : types) {
    join->wait_cnt_++;
    do_get_secure_value(password, type,
                        PromiseCreator::lambda([join](Result<SecureValueWithCredentials> r_secure_value) {
                          std::lock_guard<std::mutex> guard(join->mutex_);
                          if (!join->promise_) {
                            return;
                          }
                          if (r_secure_value.is_error()) {
                            return join->promise_.set_error(r_secure_value.move_as_error());
                          }
                          join->credentials_.push_back(r_secure_value.move_as_ok().credentials);
                          join->wait_cnt_--;
                          LOG(ERROR) << tag("wait_cnt", join->wait_cnt_);
                          if (join->wait_cnt_ == 0) {
                            LOG(ERROR) << "set promise";
                            join->promise_.set_value(std::move(join->credentials_));
                          }
                        }));
  }
  join->promise_ =
      PromiseCreator::lambda([promise = std::move(promise), actor_id = actor_id(this),
                              authorization_form_id](Result<vector<SecureValueCredentials>> r_credentials) mutable {
        LOG(ERROR) << "on promise";
        if (r_credentials.is_error()) {
          return promise.set_error(r_credentials.move_as_error());
        }
        send_closure(actor_id, &SecureManager::do_send_passport_authorization_form, authorization_form_id,
                     r_credentials.move_as_ok(), std::move(promise));
      });
}

void SecureManager::do_send_passport_authorization_form(int32 authorization_form_id,
                                                        vector<SecureValueCredentials> credentials, Promise<> promise) {
  LOG(ERROR) << "do_send_passport_authorization_form";
  auto it = authorization_forms_.find(authorization_form_id);
  if (it == authorization_forms_.end()) {
    return promise.set_error(Status::Error(400, "Unknown authorization_form_id"));
  }
  if (credentials.empty()) {
    return promise.set_error(Status::Error(400, "Empty types"));
  }
  std::vector<telegram_api::object_ptr<telegram_api::secureValueHash>> hashes;
  for (auto &c : credentials) {
    hashes.push_back(telegram_api::make_object<telegram_api::secureValueHash>(get_secure_value_type_object(c.type),
                                                                              BufferSlice(c.hash)));
  }

  auto r_encrypted_credentials =
      get_encrypted_credentials(credentials, it->second.payload, it->second.is_selfie_required, it->second.public_key);
  if (r_encrypted_credentials.is_error()) {
    return promise.set_error(r_encrypted_credentials.move_as_error());
  }

  auto td_query = telegram_api::account_acceptAuthorization(
      it->second.bot_user_id.get(), it->second.scope, it->second.public_key, std::move(hashes),
      get_secure_credentials_encrypted_object(r_encrypted_credentials.move_as_ok()));
  LOG(ERROR) << to_string(td_query);
  auto query = G()->net_query_creator().create(create_storer(td_query));
  auto new_promise =
      PromiseCreator::lambda([promise = std::move(promise)](Result<NetQueryPtr> r_net_query_ptr) mutable {
        auto r_result = fetch_result<telegram_api::account_acceptAuthorization>(std::move(r_net_query_ptr));
        if (r_result.is_error()) {
          return promise.set_error(r_result.move_as_error());
        }
        promise.set_value(Unit());
      });
  send_with_promise(std::move(query), std::move(new_promise));
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

void SecureManager::on_result(NetQueryPtr query) {
  auto token = get_link_token();
  container_.extract(token).set_value(std::move(query));
}

void SecureManager::send_with_promise(NetQueryPtr query, Promise<NetQueryPtr> promise) {
  auto id = container_.create(std::move(promise));
  G()->net_query_dispatcher().dispatch_with_callback(std::move(query), actor_shared(this, id));
}

}  // namespace td
