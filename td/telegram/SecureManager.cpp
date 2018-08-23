//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/SecureManager.h"

#include "td/telegram/ContactsManager.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/files/FileId.h"
#include "td/telegram/files/FileManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/misc.h"
#include "td/telegram/net/NetQueryDispatcher.h"
#include "td/telegram/PasswordManager.h"
#include "td/telegram/Td.h"

#include "td/utils/buffer.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/optional.h"
#include "td/utils/Slice.h"

namespace td {

class GetSecureValue : public NetQueryCallback {
 public:
  GetSecureValue(ActorShared<SecureManager> parent, std::string password, SecureValueType type,
                 Promise<SecureValueWithCredentials> promise);

 private:
  ActorShared<SecureManager> parent_;
  string password_;
  SecureValueType type_;
  Promise<SecureValueWithCredentials> promise_;
  optional<EncryptedSecureValue> encrypted_secure_value_;
  optional<secure_storage::Secret> secret_;

  void on_error(Status error);
  void on_secret(Result<secure_storage::Secret> r_secret, bool dummy);
  void loop() override;
  void start_up() override;

  void on_result(NetQueryPtr query) override;
};

class GetAllSecureValues : public NetQueryCallback {
 public:
  GetAllSecureValues(ActorShared<SecureManager> parent, std::string password, Promise<TdApiSecureValues> promise);

 private:
  ActorShared<SecureManager> parent_;
  string password_;
  Promise<TdApiSecureValues> promise_;
  optional<vector<EncryptedSecureValue>> encrypted_secure_values_;
  optional<secure_storage::Secret> secret_;

  void on_error(Status error);
  void on_secret(Result<secure_storage::Secret> r_secret, bool dummy);
  void loop() override;
  void start_up() override;

  void on_result(NetQueryPtr query) override;
};

class SetSecureValue : public NetQueryCallback {
 public:
  SetSecureValue(ActorShared<SecureManager> parent, string password, SecureValue secure_value,
                 Promise<SecureValueWithCredentials> promise);

 private:
  ActorShared<SecureManager> parent_;
  string password_;
  SecureValue secure_value_;
  Promise<SecureValueWithCredentials> promise_;
  optional<secure_storage::Secret> secret_;

  size_t files_left_to_upload_ = 0;
  vector<SecureInputFile> files_to_upload_;
  vector<SecureInputFile> translations_to_upload_;
  optional<SecureInputFile> front_side_;
  optional<SecureInputFile> reverse_side_;
  optional<SecureInputFile> selfie_;

  class UploadCallback;
  std::shared_ptr<UploadCallback> upload_callback_;

  enum class State : int32 { WaitSecret, WaitSetValue } state_ = State::WaitSecret;

  class UploadCallback : public FileManager::UploadCallback {
   public:
    explicit UploadCallback(ActorId<SetSecureValue> actor_id);

   private:
    ActorId<SetSecureValue> actor_id_;
    void on_upload_ok(FileId file_id, tl_object_ptr<telegram_api::InputFile> input_file) override;
    void on_upload_encrypted_ok(FileId file_id, tl_object_ptr<telegram_api::InputEncryptedFile> input_file) override;
    void on_upload_secure_ok(FileId file_id, tl_object_ptr<telegram_api::InputSecureFile> input_file) override;
    void on_upload_error(FileId file_id, Status status) override;
  };

  void on_upload_ok(FileId file_id, tl_object_ptr<telegram_api::InputSecureFile> input_file);
  void on_upload_error(FileId file_id, Status status);

  void on_error(Status error);

  void on_secret(Result<secure_storage::Secret> r_secret, bool x);

  void start_up() override;
  void hangup() override;
  void tear_down() override;

  void loop() override;
  void on_result(NetQueryPtr query) override;

  void start_upload(FileManager *file_manager, FileId &file_id, SecureInputFile &info);
  void merge(FileManager *file_manager, FileId file_id, EncryptedSecureFile &encrypted_file);
};

class SetSecureValueErrorsQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit SetSecureValueErrorsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(tl_object_ptr<telegram_api::InputUser> input_user,
            vector<tl_object_ptr<telegram_api::SecureValueError>> input_errors) {
    send_query(G()->net_query_creator().create(
        create_storer(telegram_api::users_setSecureValueErrors(std::move(input_user), std::move(input_errors)))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::users_setSecureValueErrors>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    bool ptr = result_ptr.move_as_ok();
    LOG(DEBUG) << "Receive result for SetSecureValueErrorsQuery " << ptr;
    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    if (status.code() != 0) {
      promise_.set_error(std::move(status));
    } else {
      promise_.set_error(Status::Error(400, status.message()));
    }
  }
};

GetSecureValue::GetSecureValue(ActorShared<SecureManager> parent, std::string password, SecureValueType type,
                               Promise<SecureValueWithCredentials> promise)
    : parent_(std::move(parent)), password_(std::move(password)), type_(type), promise_(std::move(promise)) {
}

void GetSecureValue::on_error(Status error) {
  if (error.code() != 0) {
    promise_.set_error(std::move(error));
  } else {
    promise_.set_error(Status::Error(400, error.message()));
  }
  stop();
}

void GetSecureValue::on_secret(Result<secure_storage::Secret> r_secret, bool dummy) {
  if (r_secret.is_error()) {
    if (!G()->close_flag()) {
      LOG(ERROR) << "Receive error instead of secret: " << r_secret.error();
    }
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
  auto r_secure_value = decrypt_secure_value(file_manager, *secret_, *encrypted_secure_value_);
  if (r_secure_value.is_error()) {
    return on_error(r_secure_value.move_as_error());
  }

  send_closure(parent_, &SecureManager::on_get_secure_value, r_secure_value.ok());

  promise_.set_value(r_secure_value.move_as_ok());
  stop();
}

void GetSecureValue::start_up() {
  std::vector<telegram_api::object_ptr<telegram_api::SecureValueType>> types;
  types.push_back(get_input_secure_value_type(type_));

  auto query = G()->net_query_creator().create(create_storer(telegram_api::account_getSecureValue(std::move(types))));

  G()->net_query_dispatcher().dispatch_with_callback(std::move(query), actor_shared(this));

  send_closure(G()->password_manager(), &PasswordManager::get_secure_secret, password_,
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
  encrypted_secure_value_ =
      get_encrypted_secure_value(G()->td().get_actor_unsafe()->file_manager_.get(), std::move(result[0]));
  if (encrypted_secure_value_.value().type == SecureValueType::None) {
    return on_error(Status::Error(404, "Not Found"));
  }
  loop();
}

GetAllSecureValues::GetAllSecureValues(ActorShared<SecureManager> parent, std::string password,
                                       Promise<TdApiSecureValues> promise)
    : parent_(std::move(parent)), password_(std::move(password)), promise_(std::move(promise)) {
}

void GetAllSecureValues::on_error(Status error) {
  if (error.code() != 0) {
    promise_.set_error(std::move(error));
  } else {
    promise_.set_error(Status::Error(400, error.message()));
  }
  stop();
}

void GetAllSecureValues::on_secret(Result<secure_storage::Secret> r_secret, bool dummy) {
  if (r_secret.is_error()) {
    if (!G()->close_flag()) {
      LOG(ERROR) << "Receive error instead of secret: " << r_secret.error();
    }
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
  auto r_secure_values = decrypt_secure_values(file_manager, *secret_, *encrypted_secure_values_);
  if (r_secure_values.is_error()) {
    return on_error(r_secure_values.move_as_error());
  }

  for (auto &secure_value : r_secure_values.ok()) {
    send_closure(parent_, &SecureManager::on_get_secure_value, secure_value);
  }

  auto secure_values = transform(r_secure_values.move_as_ok(),
                                 [](SecureValueWithCredentials &&value) { return std::move(value.value); });
  promise_.set_value(get_passport_elements_object(file_manager, std::move(secure_values)));
  stop();
}

void GetAllSecureValues::start_up() {
  auto query = G()->net_query_creator().create(create_storer(telegram_api::account_getAllSecureValues()));

  G()->net_query_dispatcher().dispatch_with_callback(std::move(query), actor_shared(this));

  send_closure(G()->password_manager(), &PasswordManager::get_secure_secret, password_,
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

SetSecureValue::SetSecureValue(ActorShared<SecureManager> parent, string password, SecureValue secure_value,
                               Promise<SecureValueWithCredentials> promise)
    : parent_(std::move(parent))
    , password_(std::move(password))
    , secure_value_(std::move(secure_value))
    , promise_(std::move(promise)) {
}

SetSecureValue::UploadCallback::UploadCallback(ActorId<SetSecureValue> actor_id) : actor_id_(actor_id) {
}

void SetSecureValue::UploadCallback::on_upload_ok(FileId file_id, tl_object_ptr<telegram_api::InputFile> input_file) {
  CHECK(input_file == nullptr);
  send_closure_later(actor_id_, &SetSecureValue::on_upload_ok, file_id, nullptr);
}

void SetSecureValue::UploadCallback::on_upload_encrypted_ok(
    FileId file_id, tl_object_ptr<telegram_api::InputEncryptedFile> input_file) {
  UNREACHABLE();
}

void SetSecureValue::UploadCallback::on_upload_secure_ok(FileId file_id,
                                                         tl_object_ptr<telegram_api::InputSecureFile> input_file) {
  send_closure_later(actor_id_, &SetSecureValue::on_upload_ok, file_id, std::move(input_file));
}

void SetSecureValue::UploadCallback::on_upload_error(FileId file_id, Status error) {
  send_closure_later(actor_id_, &SetSecureValue::on_upload_error, file_id, std::move(error));
}

void SetSecureValue::on_upload_ok(FileId file_id, tl_object_ptr<telegram_api::InputSecureFile> input_file) {
  SecureInputFile *info_ptr = nullptr;
  for (auto &info : files_to_upload_) {
    if (info.file_id == file_id) {
      info_ptr = &info;
      break;
    }
  }
  for (auto &info : translations_to_upload_) {
    if (info.file_id == file_id) {
      info_ptr = &info;
      break;
    }
  }
  if (front_side_ && front_side_.value().file_id == file_id) {
    info_ptr = &front_side_.value();
  }
  if (reverse_side_ && reverse_side_.value().file_id == file_id) {
    info_ptr = &reverse_side_.value();
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
  loop();
}

void SetSecureValue::on_upload_error(FileId file_id, Status error) {
  on_error(std::move(error));
}

void SetSecureValue::on_error(Status error) {
  if (error.code() != 0) {
    promise_.set_error(std::move(error));
  } else {
    promise_.set_error(Status::Error(400, error.message()));
  }
  stop();
}

void SetSecureValue::on_secret(Result<secure_storage::Secret> r_secret, bool x) {
  if (r_secret.is_error()) {
    if (!G()->close_flag()) {
      LOG(ERROR) << "Receive error instead of secret: " << r_secret.error();
    }
    return on_error(r_secret.move_as_error());
  }
  secret_ = r_secret.move_as_ok();
  loop();
}

void SetSecureValue::start_up() {
  send_closure(G()->password_manager(), &PasswordManager::get_secure_secret, password_,
               PromiseCreator::lambda([actor_id = actor_id(this)](Result<secure_storage::Secret> r_secret) {
                 send_closure(actor_id, &SetSecureValue::on_secret, std::move(r_secret), true);
               }));
  auto *file_manager = G()->td().get_actor_unsafe()->file_manager_.get();

  // Remove duplicate files
  FileId front_side_file_id;
  if (secure_value_.front_side.file_id.is_valid()) {
    front_side_file_id = file_manager->get_file_view(secure_value_.front_side.file_id).file_id();
    front_side_ = SecureInputFile();
  }
  FileId reverse_side_file_id;
  if (secure_value_.reverse_side.file_id.is_valid()) {
    reverse_side_file_id = file_manager->get_file_view(secure_value_.reverse_side.file_id).file_id();
    reverse_side_ = SecureInputFile();
    if (front_side_file_id == reverse_side_file_id) {
      return on_error(Status::Error(400, "Front side and reverse side must be different"));
    }
  }
  FileId selfie_file_id;
  if (secure_value_.selfie.file_id.is_valid()) {
    selfie_file_id = file_manager->get_file_view(secure_value_.selfie.file_id).file_id();
    selfie_ = SecureInputFile();
    if (front_side_file_id == selfie_file_id) {
      return on_error(Status::Error(400, "Front side and selfie must be different"));
    }
    if (reverse_side_file_id == selfie_file_id) {
      return on_error(Status::Error(400, "Reverse side and selfie must be different"));
    }
  }

  if (!secure_value_.files.empty()) {
    CHECK(!front_side_file_id.is_valid());
    CHECK(!reverse_side_file_id.is_valid());
    CHECK(!selfie_file_id.is_valid());
    for (auto it = secure_value_.files.begin(); it != secure_value_.files.end();) {
      auto file_id = file_manager->get_file_view(it->file_id).file_id();
      bool is_duplicate = false;
      for (auto pit = secure_value_.files.begin(); pit != it; pit++) {
        if (file_id == file_manager->get_file_view(pit->file_id).file_id()) {
          is_duplicate = true;
          break;
        }
      }
      if (is_duplicate) {
        it = secure_value_.files.erase(it);
      } else {
        ++it;
      }
    }
  }
  if (!secure_value_.translations.empty()) {
    for (auto it = secure_value_.translations.begin(); it != secure_value_.translations.end();) {
      auto file_id = file_manager->get_file_view(it->file_id).file_id();
      bool is_duplicate = file_id == front_side_file_id || file_id == reverse_side_file_id || file_id == selfie_file_id;
      for (auto pit = secure_value_.translations.begin(); pit != it; pit++) {
        if (file_id == file_manager->get_file_view(pit->file_id).file_id()) {
          is_duplicate = true;
          break;
        }
      }
      for (auto &dated_file : secure_value_.files) {
        if (file_id == file_manager->get_file_view(dated_file.file_id).file_id()) {
          is_duplicate = true;
          break;
        }
      }
      if (is_duplicate) {
        it = secure_value_.translations.erase(it);
      } else {
        ++it;
      }
    }
  }

  upload_callback_ = std::make_shared<UploadCallback>(actor_id(this));

  files_to_upload_.resize(secure_value_.files.size());
  for (size_t i = 0; i < files_to_upload_.size(); i++) {
    start_upload(file_manager, secure_value_.files[i].file_id, files_to_upload_[i]);
  }
  translations_to_upload_.resize(secure_value_.translations.size());
  for (size_t i = 0; i < translations_to_upload_.size(); i++) {
    start_upload(file_manager, secure_value_.translations[i].file_id, translations_to_upload_[i]);
  }
  if (front_side_) {
    start_upload(file_manager, secure_value_.front_side.file_id, front_side_.value());
  }
  if (reverse_side_) {
    start_upload(file_manager, secure_value_.reverse_side.file_id, reverse_side_.value());
  }
  if (selfie_) {
    start_upload(file_manager, secure_value_.selfie.file_id, selfie_.value());
  }
}

void SetSecureValue::start_upload(FileManager *file_manager, FileId &file_id, SecureInputFile &info) {
  auto file_view = file_manager->get_file_view(file_id);
  if (!file_view.is_encrypted_secure()) {
    auto download_file_id = file_manager->dup_file_id(file_id);
    file_id = file_manager
                  ->register_generate(FileType::Secure, FileLocationSource::FromServer, file_view.suggested_name(),
                                      PSTRING() << "#file_id#" << download_file_id.get(), DialogId(), file_view.size())
                  .ok();
  }

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
    auto input_secure_value =
        get_input_secure_value_object(file_manager, encrypt_secure_value(file_manager, *secret_, secure_value_),
                                      files_to_upload_, front_side_, reverse_side_, selfie_, translations_to_upload_);
    auto save_secure_value =
        telegram_api::account_saveSecureValue(std::move(input_secure_value), secret_.value().get_hash());
    auto query = G()->net_query_creator().create(create_storer(save_secure_value));

    G()->net_query_dispatcher().dispatch_with_callback(std::move(query), actor_shared(this));
    state_ = State::WaitSetValue;
  }
}

void SetSecureValue::hangup() {
  on_error(Status::Error(406, "Request aborted"));
}

void SetSecureValue::tear_down() {
  auto *file_manager = G()->td().get_actor_unsafe()->file_manager_.get();
  if (file_manager == nullptr) {
    return;
  }
  for (auto &file_info : files_to_upload_) {
    file_manager->upload(file_info.file_id, nullptr, 0, 0);
  }
  for (auto &file_info : translations_to_upload_) {
    file_manager->upload(file_info.file_id, nullptr, 0, 0);
  }
  if (front_side_) {
    file_manager->upload(front_side_.value().file_id, nullptr, 0, 0);
  }
  if (reverse_side_) {
    file_manager->upload(reverse_side_.value().file_id, nullptr, 0, 0);
  }
  if (selfie_) {
    file_manager->upload(selfie_.value().file_id, nullptr, 0, 0);
  }
}

void SetSecureValue::on_result(NetQueryPtr query) {
  auto r_result = fetch_result<telegram_api::account_saveSecureValue>(std::move(query));
  if (r_result.is_error()) {
    return on_error(r_result.move_as_error());
  }
  auto result = r_result.move_as_ok();
  auto *file_manager = G()->td().get_actor_unsafe()->file_manager_.get();
  auto encrypted_secure_value = get_encrypted_secure_value(file_manager, std::move(result));
  if (encrypted_secure_value.type == SecureValueType::None) {
    return on_error(Status::Error(500, "Receive invalid Telegram Passport element"));
  }
  if (secure_value_.files.size() != encrypted_secure_value.files.size()) {
    return on_error(Status::Error(500, "Different file count"));
  }
  for (size_t i = 0; i < secure_value_.files.size(); i++) {
    merge(file_manager, secure_value_.files[i].file_id, encrypted_secure_value.files[i]);
  }
  if (secure_value_.front_side.file_id.is_valid() && encrypted_secure_value.front_side.file.file_id.is_valid()) {
    merge(file_manager, secure_value_.front_side.file_id, encrypted_secure_value.front_side);
  }
  if (secure_value_.reverse_side.file_id.is_valid() && encrypted_secure_value.reverse_side.file.file_id.is_valid()) {
    merge(file_manager, secure_value_.reverse_side.file_id, encrypted_secure_value.reverse_side);
  }
  if (secure_value_.selfie.file_id.is_valid() && encrypted_secure_value.selfie.file.file_id.is_valid()) {
    merge(file_manager, secure_value_.selfie.file_id, encrypted_secure_value.selfie);
  }
  for (size_t i = 0; i < secure_value_.translations.size(); i++) {
    merge(file_manager, secure_value_.translations[i].file_id, encrypted_secure_value.translations[i]);
  }
  auto r_secure_value = decrypt_secure_value(file_manager, *secret_, encrypted_secure_value);
  if (r_secure_value.is_error()) {
    return on_error(r_secure_value.move_as_error());
  }

  send_closure(parent_, &SecureManager::on_get_secure_value, r_secure_value.ok());

  promise_.set_value(r_secure_value.move_as_ok());
  stop();
}

void SetSecureValue::merge(FileManager *file_manager, FileId file_id, EncryptedSecureFile &encrypted_file) {
  auto file_view = file_manager->get_file_view(file_id);
  CHECK(!file_view.empty());
  CHECK(file_view.encryption_key().has_value_hash());
  if (file_view.encryption_key().value_hash().as_slice() != encrypted_file.file_hash) {
    LOG(ERROR) << "Hash mismatch";
    return;
  }
  auto status = file_manager->merge(encrypted_file.file.file_id, file_id);
  LOG_IF(ERROR, status.is_error()) << status.error();
}

class DeleteSecureValue : public NetQueryCallback {
 public:
  DeleteSecureValue(ActorShared<SecureManager> parent, SecureValueType type, Promise<Unit> promise)
      : parent_(std::move(parent)), type_(std::move(type)), promise_(std::move(promise)) {
  }

 private:
  ActorShared<SecureManager> parent_;
  SecureValueType type_;
  Promise<Unit> promise_;

  void start_up() override {
    std::vector<telegram_api::object_ptr<telegram_api::SecureValueType>> types;
    types.push_back(get_input_secure_value_type(type_));
    auto query =
        G()->net_query_creator().create(create_storer(telegram_api::account_deleteSecureValue(std::move(types))));
    G()->net_query_dispatcher().dispatch_with_callback(std::move(query), actor_shared(this));
  }

  void on_result(NetQueryPtr query) override {
    auto r_result = fetch_result<telegram_api::account_deleteSecureValue>(std::move(query));
    if (r_result.is_error()) {
      promise_.set_error(r_result.move_as_error());
    } else {
      promise_.set_value(Unit());
    }
    stop();
  }
};

class GetPassportAuthorizationForm : public NetQueryCallback {
 public:
  GetPassportAuthorizationForm(
      ActorShared<SecureManager> parent, string password, int32 authorization_form_id, UserId bot_user_id, string scope,
      string public_key,
      Promise<std::pair<std::map<SecureValueType, SuitableSecureValue>, TdApiAuthorizationForm>> promise)
      : parent_(std::move(parent))
      , password_(std::move(password))
      , authorization_form_id_(authorization_form_id)
      , bot_user_id_(bot_user_id)
      , scope_(std::move(scope))
      , public_key_(std::move(public_key))
      , promise_(std::move(promise)) {
  }

 private:
  ActorShared<SecureManager> parent_;
  string password_;
  int32 authorization_form_id_;
  UserId bot_user_id_;
  string scope_;
  string public_key_;
  Promise<std::pair<std::map<SecureValueType, SuitableSecureValue>, TdApiAuthorizationForm>> promise_;
  optional<secure_storage::Secret> secret_;
  telegram_api::object_ptr<telegram_api::account_authorizationForm> authorization_form_;

  void on_secret(Result<secure_storage::Secret> r_secret, bool dummy) {
    if (r_secret.is_error()) {
      if (!G()->close_flag()) {
        LOG(ERROR) << "Receive error instead of secret: " << r_secret.error();
      }
      return on_error(r_secret.move_as_error());
    }
    secret_ = r_secret.move_as_ok();
    loop();
  }

  void on_error(Status error) {
    if (error.code() != 0) {
      promise_.set_error(std::move(error));
    } else {
      promise_.set_error(Status::Error(400, error.message()));
    }
    stop();
  }

  void start_up() override {
    auto account_get_authorization_form =
        telegram_api::account_getAuthorizationForm(bot_user_id_.get(), std::move(scope_), std::move(public_key_));
    auto query = G()->net_query_creator().create(create_storer(account_get_authorization_form));
    G()->net_query_dispatcher().dispatch_with_callback(std::move(query), actor_shared(this));

    send_closure(G()->password_manager(), &PasswordManager::get_secure_secret, password_,
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
    LOG(INFO) << "Receive " << to_string(authorization_form_);
    loop();
  }

  void loop() override {
    if (!secret_ || !authorization_form_) {
      return;
    }

    G()->td().get_actor_unsafe()->contacts_manager_->on_get_users(std::move(authorization_form_->users_));

    auto *file_manager = G()->td().get_actor_unsafe()->file_manager_.get();
    vector<vector<SuitableSecureValue>> required_types;
    std::map<SecureValueType, SuitableSecureValue> all_types;
    for (auto &type_ptr : authorization_form_->required_types_) {
      CHECK(type_ptr != nullptr);
      vector<SuitableSecureValue> required_type;
      switch (type_ptr->get_id()) {
        case telegram_api::secureRequiredType::ID: {
          auto value = get_suitable_secure_value(move_tl_object_as<telegram_api::secureRequiredType>(type_ptr));
          all_types.emplace(value.type, value);
          required_type.push_back(std::move(value));
          break;
        }
        case telegram_api::secureRequiredTypeOneOf::ID: {
          auto type_one_of = move_tl_object_as<telegram_api::secureRequiredTypeOneOf>(type_ptr);
          for (auto &type : type_one_of->types_) {
            if (type->get_id() == telegram_api::secureRequiredType::ID) {
              auto value = get_suitable_secure_value(move_tl_object_as<telegram_api::secureRequiredType>(type));
              all_types.emplace(value.type, value);
              required_type.push_back(std::move(value));
            } else {
              LOG(ERROR) << to_string(type);
            }
          }
          break;
        }
        default:
          UNREACHABLE();
      }
      if (!required_type.empty()) {
        required_types.push_back(required_type);
      }
    }

    std::vector<TdApiSecureValue> values;
    for (auto suitable_type : all_types) {
      auto type = suitable_type.first;
      for (auto &value : authorization_form_->values_) {
        if (value == nullptr) {
          continue;
        }
        auto value_type = get_secure_value_type(value->type_);
        if (value_type != type) {
          continue;
        }

        auto r_secure_value =
            decrypt_secure_value(file_manager, *secret_, get_encrypted_secure_value(file_manager, std::move(value)));
        value = nullptr;
        if (r_secure_value.is_error()) {
          LOG(ERROR) << "Failed to decrypt secure value: " << r_secure_value.error();
          break;
        }

        send_closure(parent_, &SecureManager::on_get_secure_value, r_secure_value.ok());

        auto r_passport_element =
            get_passport_element_object(file_manager, std::move(r_secure_value.move_as_ok().value));
        if (r_passport_element.is_error()) {
          LOG(ERROR) << "Failed to get passport element object: " << r_passport_element.error();
          break;
        }

        values.push_back(r_passport_element.move_as_ok());
        break;
      }
    }

    vector<td_api::object_ptr<td_api::passportElementError>> errors;
    for (auto &error_ptr : authorization_form_->errors_) {
      CHECK(error_ptr != nullptr);
      SecureValueType type = SecureValueType::None;
      td_api::object_ptr<td_api::PassportElementErrorSource> source;
      string message;
      switch (error_ptr->get_id()) {
        case telegram_api::secureValueError::ID: {
          auto error = move_tl_object_as<telegram_api::secureValueError>(error_ptr);
          type = get_secure_value_type(error->type_);
          message = std::move(error->text_);
          source = td_api::make_object<td_api::passportElementErrorSourceUnspecified>();
          break;
        }
        case telegram_api::secureValueErrorData::ID: {
          auto error = move_tl_object_as<telegram_api::secureValueErrorData>(error_ptr);
          type = get_secure_value_type(error->type_);
          message = std::move(error->text_);
          string field_name = get_secure_value_data_field_name(type, error->field_);
          if (field_name.empty()) {
            break;
          }
          source = td_api::make_object<td_api::passportElementErrorSourceDataField>(std::move(field_name));
          break;
        }
        case telegram_api::secureValueErrorFile::ID: {
          auto error = move_tl_object_as<telegram_api::secureValueErrorFile>(error_ptr);
          type = get_secure_value_type(error->type_);
          message = std::move(error->text_);
          source = td_api::make_object<td_api::passportElementErrorSourceFile>();
          break;
        }
        case telegram_api::secureValueErrorFiles::ID: {
          auto error = move_tl_object_as<telegram_api::secureValueErrorFiles>(error_ptr);
          type = get_secure_value_type(error->type_);
          message = std::move(error->text_);
          source = td_api::make_object<td_api::passportElementErrorSourceFiles>();
          break;
        }
        case telegram_api::secureValueErrorFrontSide::ID: {
          auto error = move_tl_object_as<telegram_api::secureValueErrorFrontSide>(error_ptr);
          type = get_secure_value_type(error->type_);
          message = std::move(error->text_);
          source = td_api::make_object<td_api::passportElementErrorSourceFrontSide>();
          break;
        }
        case telegram_api::secureValueErrorReverseSide::ID: {
          auto error = move_tl_object_as<telegram_api::secureValueErrorReverseSide>(error_ptr);
          type = get_secure_value_type(error->type_);
          message = std::move(error->text_);
          source = td_api::make_object<td_api::passportElementErrorSourceReverseSide>();
          break;
        }
        case telegram_api::secureValueErrorSelfie::ID: {
          auto error = move_tl_object_as<telegram_api::secureValueErrorSelfie>(error_ptr);
          type = get_secure_value_type(error->type_);
          message = std::move(error->text_);
          source = td_api::make_object<td_api::passportElementErrorSourceSelfie>();
          break;
        }
        case telegram_api::secureValueErrorTranslationFile::ID: {
          auto error = move_tl_object_as<telegram_api::secureValueErrorTranslationFile>(error_ptr);
          type = get_secure_value_type(error->type_);
          message = std::move(error->text_);
          source = td_api::make_object<td_api::passportElementErrorSourceTranslationFile>();
          break;
        }
        case telegram_api::secureValueErrorTranslationFiles::ID: {
          auto error = move_tl_object_as<telegram_api::secureValueErrorTranslationFiles>(error_ptr);
          type = get_secure_value_type(error->type_);
          message = std::move(error->text_);
          source = td_api::make_object<td_api::passportElementErrorSourceTranslationFiles>();
          break;
        }
        default:
          UNREACHABLE();
      }
      if (source == nullptr) {
        continue;
      }

      errors.push_back(td_api::make_object<td_api::passportElementError>(get_passport_element_type_object(type),
                                                                         message, std::move(source)));
    }

    auto authorization_form = make_tl_object<td_api::passportAuthorizationForm>(
        authorization_form_id_, get_passport_required_elements_object(required_types), std::move(values),
        std::move(errors), authorization_form_->privacy_policy_url_);

    promise_.set_value({std::move(all_types), std::move(authorization_form)});
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
        if (file_manager == nullptr) {
          return promise.set_value(nullptr);
        }
        auto r_passport_element = get_passport_element_object(file_manager, r_secure_value.move_as_ok().value);
        if (r_passport_element.is_error()) {
          LOG(ERROR) << "Failed to get passport element object: " << r_passport_element.error();
          return promise.set_value(nullptr);
        }
        promise.set_value(r_passport_element.move_as_ok());
      });

  refcnt_++;
  create_actor<GetSecureValue>("GetSecureValue", actor_shared(this), std::move(password), type, std::move(new_promise))
      .release();
}

class GetPassportConfig : public NetQueryCallback {
 public:
  GetPassportConfig(ActorShared<SecureManager> parent, string country_code,
                    Promise<td_api::object_ptr<td_api::text>> promise)
      : parent_(std::move(parent)), country_code_(std::move(country_code)), promise_(std::move(promise)) {
  }

 private:
  ActorShared<SecureManager> parent_;
  string country_code_;
  Promise<td_api::object_ptr<td_api::text>> promise_;

  void start_up() override {
    auto query = G()->net_query_creator().create(create_storer(telegram_api::help_getPassportConfig(0)));
    G()->net_query_dispatcher().dispatch_with_callback(std::move(query), actor_shared(this));
  }

  void on_result(NetQueryPtr query) override {
    auto r_result = fetch_result<telegram_api::help_getPassportConfig>(std::move(query));
    if (r_result.is_error()) {
      promise_.set_error(r_result.move_as_error());
      stop();
      return;
    }

    auto config = r_result.move_as_ok();
    switch (config->get_id()) {
      case telegram_api::help_passportConfigNotModified::ID:
        promise_.set_error(Status::Error(500, "Wrong server response"));
        break;
      case telegram_api::help_passportConfig::ID: {
        const string &data =
            static_cast<const telegram_api::help_passportConfig *>(config.get())->countries_langs_->data_;
        auto begin_pos = data.find((PSLICE() << '"' << country_code_ << "\":\"").c_str());
        if (begin_pos == string::npos) {
          promise_.set_value(nullptr);
          break;
        }

        begin_pos += 4 + country_code_.size();
        auto end_pos = data.find('"', begin_pos);
        if (end_pos == string::npos) {
          return promise_.set_error(Status::Error(500, "Wrong server response"));
        }
        promise_.set_value(td_api::make_object<td_api::text>(data.substr(begin_pos, end_pos - begin_pos)));
        break;
      }
      default:
        UNREACHABLE();
    }
    stop();
  }
};

void SecureManager::on_get_secure_value(SecureValueWithCredentials value) {
  auto type = value.value.type;
  secure_value_cache_[type] = std::move(value);
}

void SecureManager::get_all_secure_values(std::string password, Promise<TdApiSecureValues> promise) {
  refcnt_++;
  create_actor<GetAllSecureValues>("GetAllSecureValues", actor_shared(this), std::move(password), std::move(promise))
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
        auto r_passport_element = get_passport_element_object(file_manager, r_secure_value.move_as_ok().value);
        if (r_passport_element.is_error()) {
          LOG(ERROR) << "Failed to get passport element object: " << r_passport_element.error();
          return promise.set_error(Status::Error(500, "Failed to get passport element object"));
        }
        promise.set_value(r_passport_element.move_as_ok());
      });
  set_secure_value_queries_[type] = create_actor<SetSecureValue>(
      "SetSecureValue", actor_shared(this), std::move(password), std::move(secure_value), std::move(new_promise));
}

void SecureManager::delete_secure_value(SecureValueType type, Promise<Unit> promise) {
  refcnt_++;
  auto new_promise = PromiseCreator::lambda(
      [actor_id = actor_id(this), type, promise = std::move(promise)](Result<Unit> result) mutable {
        send_closure(actor_id, &SecureManager::on_delete_secure_value, type, std::move(promise), std::move(result));
      });
  create_actor<DeleteSecureValue>("DeleteSecureValue", actor_shared(this), type, std::move(new_promise)).release();
}

void SecureManager::on_delete_secure_value(SecureValueType type, Promise<Unit> promise, Result<Unit> result) {
  if (result.is_error()) {
    return promise.set_error(result.move_as_error());
  }

  secure_value_cache_.erase(type);
  promise.set_value(Unit());
}

void SecureManager::set_secure_value_errors(Td *td, tl_object_ptr<telegram_api::InputUser> input_user,
                                            vector<tl_object_ptr<td_api::inputPassportElementError>> errors,
                                            Promise<Unit> promise) {
  CHECK(td != nullptr);
  CHECK(input_user != nullptr);
  vector<tl_object_ptr<telegram_api::SecureValueError>> input_errors;
  for (auto &error : errors) {
    if (error == nullptr) {
      return promise.set_error(Status::Error(400, "Error must be non-empty"));
    }
    if (error->type_ == nullptr) {
      return promise.set_error(Status::Error(400, "Type must be non-empty"));
    }
    if (!clean_input_string(error->message_)) {
      return promise.set_error(Status::Error(400, "Error message must be encoded in UTF-8"));
    }
    if (error->source_ == nullptr) {
      return promise.set_error(Status::Error(400, "Error source must be non-empty"));
    }

    auto type = get_input_secure_value_type(get_secure_value_type_td_api(error->type_));
    switch (error->source_->get_id()) {
      case td_api::inputPassportElementErrorSourceUnspecified::ID: {
        auto source = td_api::move_object_as<td_api::inputPassportElementErrorSourceUnspecified>(error->source_);
        input_errors.push_back(make_tl_object<telegram_api::secureValueError>(
            std::move(type), BufferSlice(source->element_hash_), error->message_));
        break;
      }
      case td_api::inputPassportElementErrorSourceDataField::ID: {
        auto source = td_api::move_object_as<td_api::inputPassportElementErrorSourceDataField>(error->source_);
        if (!clean_input_string(source->field_name_)) {
          return promise.set_error(Status::Error(400, "Field name must be encoded in UTF-8"));
        }

        input_errors.push_back(make_tl_object<telegram_api::secureValueErrorData>(
            std::move(type), BufferSlice(source->data_hash_), source->field_name_, error->message_));
        break;
      }
      case td_api::inputPassportElementErrorSourceFrontSide::ID: {
        auto source = td_api::move_object_as<td_api::inputPassportElementErrorSourceFrontSide>(error->source_);
        input_errors.push_back(make_tl_object<telegram_api::secureValueErrorFrontSide>(
            std::move(type), BufferSlice(source->file_hash_), error->message_));
        break;
      }
      case td_api::inputPassportElementErrorSourceReverseSide::ID: {
        auto source = td_api::move_object_as<td_api::inputPassportElementErrorSourceReverseSide>(error->source_);
        input_errors.push_back(make_tl_object<telegram_api::secureValueErrorReverseSide>(
            std::move(type), BufferSlice(source->file_hash_), error->message_));
        break;
      }
      case td_api::inputPassportElementErrorSourceSelfie::ID: {
        auto source = td_api::move_object_as<td_api::inputPassportElementErrorSourceSelfie>(error->source_);
        input_errors.push_back(make_tl_object<telegram_api::secureValueErrorSelfie>(
            std::move(type), BufferSlice(source->file_hash_), error->message_));
        break;
      }
      case td_api::inputPassportElementErrorSourceTranslationFile::ID: {
        auto source = td_api::move_object_as<td_api::inputPassportElementErrorSourceTranslationFile>(error->source_);
        input_errors.push_back(make_tl_object<telegram_api::secureValueErrorTranslationFile>(
            std::move(type), BufferSlice(source->file_hash_), error->message_));
        break;
      }
      case td_api::inputPassportElementErrorSourceTranslationFiles::ID: {
        auto source = td_api::move_object_as<td_api::inputPassportElementErrorSourceTranslationFiles>(error->source_);
        if (source->file_hashes_.empty()) {
          return promise.set_error(Status::Error(400, "File hashes must be non-empty"));
        }
        auto file_hashes = transform(source->file_hashes_, [](Slice hash) { return BufferSlice(hash); });
        input_errors.push_back(make_tl_object<telegram_api::secureValueErrorTranslationFiles>(
            std::move(type), std::move(file_hashes), error->message_));
        break;
      }
      case td_api::inputPassportElementErrorSourceFile::ID: {
        auto source = td_api::move_object_as<td_api::inputPassportElementErrorSourceFile>(error->source_);
        input_errors.push_back(make_tl_object<telegram_api::secureValueErrorFile>(
            std::move(type), BufferSlice(source->file_hash_), error->message_));
        break;
      }
      case td_api::inputPassportElementErrorSourceFiles::ID: {
        auto source = td_api::move_object_as<td_api::inputPassportElementErrorSourceFiles>(error->source_);
        if (source->file_hashes_.empty()) {
          return promise.set_error(Status::Error(400, "File hashes must be non-empty"));
        }
        auto file_hashes = transform(source->file_hashes_, [](Slice hash) { return BufferSlice(hash); });
        input_errors.push_back(make_tl_object<telegram_api::secureValueErrorFiles>(
            std::move(type), std::move(file_hashes), error->message_));
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
                                                    string public_key, string nonce,
                                                    Promise<TdApiAuthorizationForm> promise) {
  refcnt_++;
  auto authorization_form_id = ++max_authorization_form_id_;
  auto &form = authorization_forms_[authorization_form_id];
  form.bot_user_id = bot_user_id;
  form.scope = scope;
  form.public_key = public_key;
  form.nonce = nonce;
  form.is_received = false;
  auto new_promise = PromiseCreator::lambda(
      [actor_id = actor_id(this), authorization_form_id, promise = std::move(promise)](
          Result<std::pair<std::map<SecureValueType, SuitableSecureValue>, TdApiAuthorizationForm>>
              r_authorization_form) mutable {
        send_closure(actor_id, &SecureManager::on_get_passport_authorization_form, authorization_form_id,
                     std::move(promise), std::move(r_authorization_form));
      });
  create_actor<GetPassportAuthorizationForm>("GetPassportAuthorizationForm", actor_shared(this), std::move(password),
                                             authorization_form_id, bot_user_id, std::move(scope),
                                             std::move(public_key), std::move(new_promise))
      .release();
}

void SecureManager::on_get_passport_authorization_form(
    int32 authorization_form_id, Promise<TdApiAuthorizationForm> promise,
    Result<std::pair<std::map<SecureValueType, SuitableSecureValue>, TdApiAuthorizationForm>> r_authorization_form) {
  auto it = authorization_forms_.find(authorization_form_id);
  CHECK(it != authorization_forms_.end());
  CHECK(it->second.is_received == false);
  if (r_authorization_form.is_error()) {
    authorization_forms_.erase(it);
    return promise.set_error(r_authorization_form.move_as_error());
  }

  auto authorization_form = r_authorization_form.move_as_ok();
  it->second.options = std::move(authorization_form.first);
  it->second.is_received = true;
  CHECK(authorization_form.second != nullptr);
  promise.set_value(std::move(authorization_form.second));
}

void SecureManager::send_passport_authorization_form(int32 authorization_form_id, std::vector<SecureValueType> types,
                                                     Promise<> promise) {
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

  std::vector<SecureValueCredentials> credentials;
  credentials.reserve(types.size());
  for (auto type : types) {
    auto value_it = secure_value_cache_.find(type);
    if (value_it == secure_value_cache_.end()) {
      return promise.set_error(Status::Error(400, "Passport Element with the specified type is not found"));
    }
    credentials.push_back(value_it->second.credentials);
  }

  std::vector<telegram_api::object_ptr<telegram_api::secureValueHash>> hashes;
  for (auto &c : credentials) {
    hashes.push_back(telegram_api::make_object<telegram_api::secureValueHash>(get_input_secure_value_type(c.type),
                                                                              BufferSlice(c.hash)));
    auto options_it = it->second.options.find(c.type);
    if (options_it == it->second.options.end()) {
      return promise.set_error(Status::Error(400, "Passport Element with the specified type was not requested"));
    }
    auto &options = options_it->second;
    if (!options.is_selfie_required) {
      c.selfie = optional<SecureFileCredentials>();
    }
    if (!options.is_translation_required) {
      c.translations.clear();
    }
  }

  auto r_encrypted_credentials =
      get_encrypted_credentials(credentials, it->second.nonce, it->second.public_key,
                                it->second.scope[0] == '{' && it->second.scope.back() == '}');
  if (r_encrypted_credentials.is_error()) {
    return promise.set_error(r_encrypted_credentials.move_as_error());
  }

  auto td_query = telegram_api::account_acceptAuthorization(
      it->second.bot_user_id.get(), it->second.scope, it->second.public_key, std::move(hashes),
      get_secure_credentials_encrypted_object(r_encrypted_credentials.move_as_ok()));
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

void SecureManager::get_preferred_country_code(string country_code, Promise<td_api::object_ptr<td_api::text>> promise) {
  refcnt_++;
  for (auto &c : country_code) {
    c = to_upper(c);
  }
  create_actor<GetPassportConfig>("GetPassportConfig", actor_shared(this), std::move(country_code), std::move(promise))
      .release();
}

void SecureManager::hangup() {
  container_.for_each(
      [](auto id, Promise<NetQueryPtr> &promise) { promise.set_error(Status::Error(500, "Request aborted")); });
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
