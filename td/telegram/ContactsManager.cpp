//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ContactsManager.h"

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/telegram_api.hpp"

#include "td/actor/PromiseFuture.h"
#include "td/actor/SleepActor.h"

#include "td/db/binlog/BinlogHelper.h"
#include "td/db/SqliteKeyValueAsync.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/ConfigShared.h"
#include "td/telegram/files/FileManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/InlineQueriesManager.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/misc.h"
#include "td/telegram/net/NetQuery.h"
#include "td/telegram/Photo.h"
#include "td/telegram/Photo.hpp"
#include "td/telegram/SecretChatActor.h"
#include "td/telegram/StickersManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/TopDialogManager.h"
#include "td/telegram/UpdatesManager.h"
#include "td/telegram/Version.h"

#include "td/utils/buffer.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Random.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/Time.h"
#include "td/utils/tl_helpers.h"

#include <algorithm>
#include <limits>
#include <tuple>
#include <utility>

namespace td {

class SetAccountTtlQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit SetAccountTtlQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(int32 account_ttl) {
    send_query(G()->net_query_creator().create(
        create_storer(telegram_api::account_setAccountTTL(make_tl_object<telegram_api::accountDaysTTL>(account_ttl)))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::account_setAccountTTL>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    bool result = result_ptr.move_as_ok();
    if (!result) {
      return on_error(id, Status::Error(500, "Internal Server Error"));
    }

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class GetAccountTtlQuery : public Td::ResultHandler {
  Promise<int32> promise_;

 public:
  explicit GetAccountTtlQuery(Promise<int32> &&promise) : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create(create_storer(telegram_api::account_getAccountTTL())));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::account_getAccountTTL>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetAccountTtlQuery: " << to_string(ptr);

    promise_.set_value(std::move(ptr->days_));
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class GetAuthorizationsQuery : public Td::ResultHandler {
  Promise<tl_object_ptr<td_api::sessions>> promise_;
  static constexpr int32 AUTHORIZATION_FLAG_IS_CURRENT = 1 << 0;
  static constexpr int32 AUTHORIZATION_FLAG_IS_OFFICIAL_APPLICATION = 1 << 1;

 public:
  explicit GetAuthorizationsQuery(Promise<tl_object_ptr<td_api::sessions>> &&promise) : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create(create_storer(telegram_api::account_getAuthorizations())));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::account_getAuthorizations>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetAuthorizationsQuery: " << to_string(ptr);

    auto results = make_tl_object<td_api::sessions>();
    results->sessions_.reserve(ptr->authorizations_.size());
    for (auto &authorization : ptr->authorizations_) {
      CHECK(authorization != nullptr);
      bool is_current = (authorization->flags_ & AUTHORIZATION_FLAG_IS_CURRENT) != 0;
      bool is_official_application = (authorization->flags_ & AUTHORIZATION_FLAG_IS_OFFICIAL_APPLICATION) != 0;

      results->sessions_.push_back(make_tl_object<td_api::session>(
          authorization->hash_, is_current, authorization->api_id_, authorization->app_name_,
          authorization->app_version_, is_official_application, authorization->device_model_, authorization->platform_,
          authorization->system_version_, authorization->date_created_, authorization->date_active_, authorization->ip_,
          authorization->country_, authorization->region_));
    }

    promise_.set_value(std::move(results));
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class ResetAuthorizationQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit ResetAuthorizationQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(int64 authorization_id) {
    send_query(
        G()->net_query_creator().create(create_storer(telegram_api::account_resetAuthorization(authorization_id))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::account_resetAuthorization>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    bool result = result_ptr.move_as_ok();
    LOG_IF(WARNING, !result) << "Failed to terminate session";
    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class ResetAuthorizationsQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit ResetAuthorizationsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create(create_storer(telegram_api::auth_resetAuthorizations())));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::auth_resetAuthorizations>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    bool result = result_ptr.move_as_ok();
    LOG_IF(WARNING, !result) << "Failed to terminate all sessions";
    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class GetWebAuthorizationsQuery : public Td::ResultHandler {
  Promise<tl_object_ptr<td_api::connectedWebsites>> promise_;

 public:
  explicit GetWebAuthorizationsQuery(Promise<tl_object_ptr<td_api::connectedWebsites>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create(create_storer(telegram_api::account_getWebAuthorizations())));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::account_getWebAuthorizations>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetWebAuthorizationsQuery: " << to_string(ptr);

    td->contacts_manager_->on_get_users(std::move(ptr->users_));

    auto results = make_tl_object<td_api::connectedWebsites>();
    results->websites_.reserve(ptr->authorizations_.size());
    for (auto &authorization : ptr->authorizations_) {
      CHECK(authorization != nullptr);
      UserId bot_user_id(authorization->bot_id_);
      if (!bot_user_id.is_valid()) {
        LOG(ERROR) << "Receive invalid bot " << bot_user_id;
        bot_user_id = UserId();
      }

      results->websites_.push_back(make_tl_object<td_api::connectedWebsite>(
          authorization->hash_, authorization->domain_,
          td->contacts_manager_->get_user_id_object(bot_user_id, "GetWebAuthorizationsQuery"), authorization->browser_,
          authorization->platform_, authorization->date_created_, authorization->date_active_, authorization->ip_,
          authorization->region_));
    }

    promise_.set_value(std::move(results));
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class ResetWebAuthorizationQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit ResetWebAuthorizationQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(int64 hash) {
    send_query(G()->net_query_creator().create(create_storer(telegram_api::account_resetWebAuthorization(hash))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::account_resetWebAuthorization>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    bool result = result_ptr.move_as_ok();
    LOG_IF(WARNING, !result) << "Failed to disconnect website";
    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class ResetWebAuthorizationsQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit ResetWebAuthorizationsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create(create_storer(telegram_api::account_resetWebAuthorizations())));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::account_resetWebAuthorizations>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    bool result = result_ptr.move_as_ok();
    LOG_IF(WARNING, !result) << "Failed to disconnect all websites";
    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class BlockUserQuery : public Td::ResultHandler {
 public:
  void send(tl_object_ptr<telegram_api::InputUser> &&user) {
    send_query(G()->net_query_creator().create(create_storer(telegram_api::contacts_block(std::move(user)))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::contacts_block>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    bool result = result_ptr.ok();
    LOG_IF(WARNING, !result) << "Block user has failed";
  }

  void on_error(uint64 id, Status status) override {
    LOG(WARNING) << "Receive error for blockUser: " << status;
    status.ignore();
  }
};

class UnblockUserQuery : public Td::ResultHandler {
 public:
  void send(tl_object_ptr<telegram_api::InputUser> &&user) {
    send_query(G()->net_query_creator().create(create_storer(telegram_api::contacts_unblock(std::move(user)))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::contacts_unblock>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    bool result = result_ptr.ok();
    LOG_IF(WARNING, !result) << "Unblock user has failed";
  }

  void on_error(uint64 id, Status status) override {
    LOG(WARNING) << "Receive error for unblockUser: " << status;
    status.ignore();
  }
};

class GetBlockedUsersQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  int32 offset_;
  int32 limit_;
  int64 random_id_;

 public:
  explicit GetBlockedUsersQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(int32 offset, int32 limit, int64 random_id) {
    offset_ = offset;
    limit_ = limit;
    random_id_ = random_id;

    send_query(G()->net_query_creator().create(create_storer(telegram_api::contacts_getBlocked(offset, limit))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::contacts_getBlocked>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetBlockedUsersQuery: " << to_string(ptr);

    int32 constructor_id = ptr->get_id();
    switch (constructor_id) {
      case telegram_api::contacts_blocked::ID: {
        auto blocked_users = move_tl_object_as<telegram_api::contacts_blocked>(ptr);

        td->contacts_manager_->on_get_users(std::move(blocked_users->users_));
        td->contacts_manager_->on_get_blocked_users_result(offset_, limit_, random_id_,
                                                           narrow_cast<int32>(blocked_users->blocked_.size()),
                                                           std::move(blocked_users->blocked_));
        break;
      }
      case telegram_api::contacts_blockedSlice::ID: {
        auto blocked_users = move_tl_object_as<telegram_api::contacts_blockedSlice>(ptr);

        td->contacts_manager_->on_get_users(std::move(blocked_users->users_));
        td->contacts_manager_->on_get_blocked_users_result(offset_, limit_, random_id_, blocked_users->count_,
                                                           std::move(blocked_users->blocked_));
        break;
      }
      default:
        UNREACHABLE();
    }

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    td->contacts_manager_->on_failed_get_blocked_users(random_id_);
    promise_.set_error(std::move(status));
  }
};

class GetContactsQuery : public Td::ResultHandler {
 public:
  void send(int32 hash) {
    LOG(INFO) << "Reload contacts with hash " << hash;
    send_query(G()->net_query_creator().create(create_storer(telegram_api::contacts_getContacts(hash))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::contacts_getContacts>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for getContacts: " << to_string(ptr);
    td->contacts_manager_->on_get_contacts(std::move(ptr));
  }

  void on_error(uint64 id, Status status) override {
    td->contacts_manager_->on_get_contacts_failed(std::move(status));
    td->updates_manager_->get_difference("GetContactsQuery");
  }
};

class GetContactsStatusesQuery : public Td::ResultHandler {
 public:
  void send() {
    LOG(INFO) << "Reload contacts statuses";
    send_query(G()->net_query_creator().create(create_storer(telegram_api::contacts_getStatuses())));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::contacts_getStatuses>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    td->contacts_manager_->on_get_contacts_statuses(result_ptr.move_as_ok());
  }

  void on_error(uint64 id, Status status) override {
    LOG(ERROR) << "Receive error for getContactsStatuses: " << status;
  }
};

class ImportContactsQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  vector<Contact> input_contacts_;
  vector<UserId> imported_user_ids_;
  vector<int32> unimported_contact_invites_;
  int64 random_id_;

 public:
  explicit ImportContactsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(vector<Contact> input_contacts, int64 random_id) {
    random_id_ = random_id;

    size_t size = input_contacts.size();
    if (size == 0) {
      td->contacts_manager_->on_imported_contacts(random_id, std::move(imported_user_ids_),
                                                  std::move(unimported_contact_invites_));
      promise_.set_value(Unit());
      return;
    }

    imported_user_ids_.resize(size);
    unimported_contact_invites_.resize(size);
    input_contacts_ = std::move(input_contacts);

    vector<tl_object_ptr<telegram_api::inputPhoneContact>> contacts;
    contacts.reserve(size);
    for (size_t i = 0; i < size; i++) {
      contacts.push_back(input_contacts_[i].get_input_phone_contact(static_cast<int64>(i)));
    }

    send_query(
        G()->net_query_creator().create(create_storer(telegram_api::contacts_importContacts(std::move(contacts)))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::contacts_importContacts>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for importContacts: " << to_string(ptr);

    td->contacts_manager_->on_get_users(std::move(ptr->users_));
    for (auto &imported_contact : ptr->imported_) {
      int64 client_id = imported_contact->client_id_;
      if (client_id < 0 || client_id >= static_cast<int64>(imported_user_ids_.size())) {
        LOG(ERROR) << "Wrong client_id " << client_id << " returned";
        continue;
      }

      imported_user_ids_[static_cast<size_t>(client_id)] = UserId(imported_contact->user_id_);
    }
    for (auto &popular_contact : ptr->popular_invites_) {
      int64 client_id = popular_contact->client_id_;
      if (client_id < 0 || client_id >= static_cast<int64>(unimported_contact_invites_.size())) {
        LOG(ERROR) << "Wrong client_id " << client_id << " returned";
        continue;
      }
      if (popular_contact->importers_ < 0) {
        LOG(ERROR) << "Wrong number of importers " << popular_contact->importers_ << " returned";
        continue;
      }

      unimported_contact_invites_[static_cast<size_t>(client_id)] = popular_contact->importers_;
    }

    if (!ptr->retry_contacts_.empty()) {
      int64 total_size = static_cast<int64>(input_contacts_.size());
      vector<tl_object_ptr<telegram_api::inputPhoneContact>> contacts;
      contacts.reserve(ptr->retry_contacts_.size());
      for (auto &client_id : ptr->retry_contacts_) {
        if (client_id < 0 || client_id >= total_size) {
          LOG(ERROR) << "Wrong client_id " << client_id << " returned";
          continue;
        }
        size_t i = static_cast<size_t>(client_id);
        contacts.push_back(input_contacts_[i].get_input_phone_contact(client_id));
      }

      send_query(
          G()->net_query_creator().create(create_storer(telegram_api::contacts_importContacts(std::move(contacts)))));
      return;
    }

    td->contacts_manager_->on_imported_contacts(random_id_, std::move(imported_user_ids_),
                                                std::move(unimported_contact_invites_));
    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
    td->contacts_manager_->reload_contacts(true);
  }
};

class DeleteContactsQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  vector<UserId> user_ids_;

 public:
  explicit DeleteContactsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(vector<UserId> &&user_ids, vector<tl_object_ptr<telegram_api::InputUser>> &&input_users) {
    user_ids_ = std::move(user_ids);
    send_query(
        G()->net_query_creator().create(create_storer(telegram_api::contacts_deleteContacts(std::move(input_users)))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::contacts_deleteContacts>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    bool result = result_ptr.ok();
    if (!result) {
      return on_error(id, Status::Error(500, "Some contacts can't be deleted"));
    }

    td->contacts_manager_->on_deleted_contacts(user_ids_);
    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
    td->contacts_manager_->reload_contacts(true);
  }
};

class ResetContactsQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit ResetContactsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create(create_storer(telegram_api::contacts_resetSaved())));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::contacts_resetSaved>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    bool result = result_ptr.ok();
    if (!result) {
      LOG(ERROR) << "Failed to delete imported contacts";
      td->contacts_manager_->reload_contacts(true);
    } else {
      td->contacts_manager_->on_update_contacts_reset();
    }

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
    td->contacts_manager_->reload_contacts(true);
  }
};

class UploadProfilePhotoQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  FileId file_id_;

 public:
  explicit UploadProfilePhotoQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(FileId file_id, tl_object_ptr<telegram_api::InputFile> &&input_file) {
    CHECK(input_file != nullptr);
    CHECK(file_id.is_valid());

    file_id_ = file_id;

    send_query(
        G()->net_query_creator().create(create_storer(telegram_api::photos_uploadProfilePhoto(std::move(input_file)))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::photos_uploadProfilePhoto>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for uploadProfilePhoto: " << to_string(ptr);
    td->contacts_manager_->on_get_users(std::move(ptr->users_));

    td->file_manager_->delete_partial_remote_location(file_id_);

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
    td->file_manager_->delete_partial_remote_location(file_id_);
    td->updates_manager_->get_difference("UploadProfilePhotoQuery");
  }
};

class UpdateProfilePhotoQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit UpdateProfilePhotoQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(tl_object_ptr<telegram_api::InputPhoto> &&input_photo) {
    send_query(G()->net_query_creator().create(
        create_storer(telegram_api::photos_updateProfilePhoto(std::move(input_photo)))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::photos_updateProfilePhoto>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    LOG(DEBUG) << "Receive result for updateProfilePhoto " << to_string(result_ptr.ok());
    td->contacts_manager_->on_update_user_photo(td->contacts_manager_->get_my_id("UpdateProfilePhotoQuery"),
                                                result_ptr.move_as_ok());

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class DeleteProfilePhotoQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  int64 profile_photo_id_;

 public:
  explicit DeleteProfilePhotoQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(int64 profile_photo_id) {
    profile_photo_id_ = profile_photo_id;
    vector<tl_object_ptr<telegram_api::InputPhoto>> input_photo_ids;
    input_photo_ids.push_back(make_tl_object<telegram_api::inputPhoto>(profile_photo_id, 0));
    send_query(
        G()->net_query_creator().create(create_storer(telegram_api::photos_deletePhotos(std::move(input_photo_ids)))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::photos_deletePhotos>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for deleteProfilePhoto " << format::as_array(result);
    if (result.size() != 1u) {
      LOG(WARNING) << "Photo can't be deleted";
      return on_error(id, Status::Error(7, "Photo can't be deleted"));
    }

    td->contacts_manager_->on_delete_profile_photo(profile_photo_id_, std::move(promise_));
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class UpdateProfileQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  int32 flags_;
  string first_name_;
  string last_name_;
  string about_;

 public:
  explicit UpdateProfileQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(int32 flags, const string &first_name, const string &last_name, const string &about) {
    flags_ = flags;
    first_name_ = first_name;
    last_name_ = last_name;
    about_ = about;
    send_query(G()->net_query_creator().create(
        create_storer(telegram_api::account_updateProfile(flags, first_name, last_name, about))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::account_updateProfile>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    LOG(DEBUG) << "Receive result for updateProfile " << to_string(result_ptr.ok());
    td->contacts_manager_->on_get_user(result_ptr.move_as_ok());
    td->contacts_manager_->on_update_profile_success(flags_, first_name_, last_name_, about_);

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class CheckUsernameQuery : public Td::ResultHandler {
  Promise<bool> promise_;

 public:
  explicit CheckUsernameQuery(Promise<bool> &&promise) : promise_(std::move(promise)) {
  }

  void send(const string &username) {
    send_query(G()->net_query_creator().create(create_storer(telegram_api::account_checkUsername(username))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::account_checkUsername>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    promise_.set_value(result_ptr.move_as_ok());
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class UpdateUsernameQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit UpdateUsernameQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(const string &username) {
    send_query(G()->net_query_creator().create(create_storer(telegram_api::account_updateUsername(username))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::account_updateUsername>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    LOG(DEBUG) << "Receive result for updateUsername " << to_string(result_ptr.ok());
    td->contacts_manager_->on_get_user(result_ptr.move_as_ok());
    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    if (status.message() == "USERNAME_NOT_MODIFIED" && !td->auth_manager_->is_bot()) {
      promise_.set_value(Unit());
      return;
    }
    promise_.set_error(std::move(status));
  }
};

class ToggleChatAdminsQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit ToggleChatAdminsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(ChatId chat_id, bool everyone_is_administrator) {
    send_query(G()->net_query_creator().create(
        create_storer(telegram_api::messages_toggleChatAdmins(chat_id.get(), !everyone_is_administrator))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_toggleChatAdmins>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for toggleChatAdmins: " << to_string(ptr);
    td->updates_manager_->on_get_updates(std::move(ptr));

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    if (status.message() == "CHAT_NOT_MODIFIED" && !td->auth_manager_->is_bot()) {
      promise_.set_value(Unit());
      return;
    }
    promise_.set_error(std::move(status));
  }
};

class CheckChannelUsernameQuery : public Td::ResultHandler {
  Promise<bool> promise_;
  ChannelId channel_id_;
  string username_;

 public:
  explicit CheckChannelUsernameQuery(Promise<bool> &&promise) : promise_(std::move(promise)) {
  }

  void send(ChannelId channel_id, const string &username) {
    channel_id_ = channel_id;
    tl_object_ptr<telegram_api::InputChannel> input_channel;
    if (channel_id.is_valid()) {
      input_channel = td->contacts_manager_->get_input_channel(channel_id);
    } else {
      input_channel = make_tl_object<telegram_api::inputChannelEmpty>();
    }
    CHECK(input_channel != nullptr);
    send_query(G()->net_query_creator().create(
        create_storer(telegram_api::channels_checkUsername(std::move(input_channel), username))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::channels_checkUsername>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    promise_.set_value(result_ptr.move_as_ok());
  }

  void on_error(uint64 id, Status status) override {
    if (channel_id_.is_valid()) {
      td->contacts_manager_->on_get_channel_error(channel_id_, status, "CheckChannelUsernameQuery");
    }
    promise_.set_error(std::move(status));
  }
};

class UpdateChannelUsernameQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  ChannelId channel_id_;
  string username_;

 public:
  explicit UpdateChannelUsernameQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(ChannelId channel_id, const string &username) {
    channel_id_ = channel_id;
    username_ = username;
    auto input_channel = td->contacts_manager_->get_input_channel(channel_id);
    CHECK(input_channel != nullptr);
    send_query(G()->net_query_creator().create(
        create_storer(telegram_api::channels_updateUsername(std::move(input_channel), username))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::channels_updateUsername>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    bool result = result_ptr.ok();
    LOG(DEBUG) << "Receive result for updateChannelUsername " << result;
    if (!result) {
      return on_error(id, Status::Error(500, "Supergroup username is not updated"));
    }

    td->contacts_manager_->on_update_channel_username(channel_id_, std::move(username_));
    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    if (status.message() == "USERNAME_NOT_MODIFIED" || status.message() == "CHAT_NOT_MODIFIED") {
      td->contacts_manager_->on_update_channel_username(channel_id_, std::move(username_));
      if (!td->auth_manager_->is_bot()) {
        promise_.set_value(Unit());
        return;
      }
    } else {
      td->contacts_manager_->on_get_channel_error(channel_id_, status, "UpdateChannelUsernameQuery");
    }
    promise_.set_error(std::move(status));
  }
};

class SetChannelStickerSetQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  ChannelId channel_id_;
  int64 sticker_set_id_;

 public:
  explicit SetChannelStickerSetQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(ChannelId channel_id, int64 sticker_set_id,
            telegram_api::object_ptr<telegram_api::InputStickerSet> &&input_sticker_set) {
    channel_id_ = channel_id;
    sticker_set_id_ = sticker_set_id;
    auto input_channel = td->contacts_manager_->get_input_channel(channel_id);
    CHECK(input_channel != nullptr);
    send_query(G()->net_query_creator().create(
        create_storer(telegram_api::channels_setStickers(std::move(input_channel), std::move(input_sticker_set)))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::channels_setStickers>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    bool result = result_ptr.ok();
    LOG(DEBUG) << "Receive result for setChannelStickerSet " << result;
    if (!result) {
      return on_error(id, Status::Error(500, "Supergroup sticker set not updated"));
    }

    td->contacts_manager_->on_update_channel_sticker_set(channel_id_, sticker_set_id_);
    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    if (status.message() == "CHAT_NOT_MODIFIED") {
      td->contacts_manager_->on_update_channel_sticker_set(channel_id_, sticker_set_id_);
      if (!td->auth_manager_->is_bot()) {
        promise_.set_value(Unit());
        return;
      }
    } else {
      td->contacts_manager_->on_get_channel_error(channel_id_, status, "SetChannelStickerSetQuery");
    }
    promise_.set_error(std::move(status));
  }
};

class ToggleChannelInvitesQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  ChannelId channel_id_;

 public:
  explicit ToggleChannelInvitesQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(ChannelId channel_id, bool anyone_can_invite) {
    channel_id_ = channel_id;
    auto input_channel = td->contacts_manager_->get_input_channel(channel_id);
    CHECK(input_channel != nullptr);
    send_query(G()->net_query_creator().create(
        create_storer(telegram_api::channels_toggleInvites(std::move(input_channel), anyone_can_invite))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::channels_toggleInvites>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for toggleChannelInvites: " << to_string(ptr);
    td->updates_manager_->on_get_updates(std::move(ptr));

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    if (status.message() == "CHAT_NOT_MODIFIED") {
      if (!td->auth_manager_->is_bot()) {
        promise_.set_value(Unit());
        return;
      }
    } else {
      td->contacts_manager_->on_get_channel_error(channel_id_, status, "ToggleChannelInvitesQuery");
    }
    promise_.set_error(std::move(status));
  }
};

class ToggleChannelSignaturesQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  ChannelId channel_id_;

 public:
  explicit ToggleChannelSignaturesQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(ChannelId channel_id, bool sign_messages) {
    channel_id_ = channel_id;
    auto input_channel = td->contacts_manager_->get_input_channel(channel_id);
    CHECK(input_channel != nullptr);
    send_query(G()->net_query_creator().create(
        create_storer(telegram_api::channels_toggleSignatures(std::move(input_channel), sign_messages))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::channels_toggleSignatures>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for toggleChannelSignatures: " << to_string(ptr);
    td->updates_manager_->on_get_updates(std::move(ptr));

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    if (status.message() == "CHAT_NOT_MODIFIED") {
      if (!td->auth_manager_->is_bot()) {
        promise_.set_value(Unit());
        return;
      }
    } else {
      td->contacts_manager_->on_get_channel_error(channel_id_, status, "ToggleChannelSignaturesQuery");
    }
    promise_.set_error(std::move(status));
  }
};

class ToggleChannelIsAllHistoryAvailableQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  ChannelId channel_id_;
  bool is_all_history_available_;

 public:
  explicit ToggleChannelIsAllHistoryAvailableQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(ChannelId channel_id, bool is_all_history_available) {
    channel_id_ = channel_id;
    is_all_history_available_ = is_all_history_available;

    auto input_channel = td->contacts_manager_->get_input_channel(channel_id);
    CHECK(input_channel != nullptr);
    send_query(G()->net_query_creator().create(create_storer(
        telegram_api::channels_togglePreHistoryHidden(std::move(input_channel), !is_all_history_available))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::channels_togglePreHistoryHidden>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for togglePreHistoryHidden: " << to_string(ptr);
    td->updates_manager_->on_get_updates(std::move(ptr));
    td->contacts_manager_->on_update_channel_is_all_history_available(channel_id_, is_all_history_available_);

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    if (status.message() == "CHAT_NOT_MODIFIED") {
      if (!td->auth_manager_->is_bot()) {
        promise_.set_value(Unit());
        return;
      }
    } else {
      td->contacts_manager_->on_get_channel_error(channel_id_, status, "ToggleChannelIsAllHistoryAvailableQuery");
    }
    promise_.set_error(std::move(status));
  }
};

class EditChannelAboutQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  ChannelId channel_id_;
  string about_;

 public:
  explicit EditChannelAboutQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(ChannelId channel_id, const string &about) {
    channel_id_ = channel_id;
    about_ = about;
    auto input_channel = td->contacts_manager_->get_input_channel(channel_id);
    CHECK(input_channel != nullptr);
    send_query(G()->net_query_creator().create(
        create_storer(telegram_api::channels_editAbout(std::move(input_channel), about))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::channels_editAbout>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    bool result = result_ptr.ok();
    LOG(DEBUG) << "Receive result for editChannelAbout " << result;
    if (!result) {
      return on_error(id, Status::Error(500, "Supergroup description is not updated"));
    }

    td->contacts_manager_->on_update_channel_description(channel_id_, std::move(about_));
    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    if (status.message() == "CHAT_ABOUT_NOT_MODIFIED" || status.message() == "CHAT_NOT_MODIFIED") {
      td->contacts_manager_->on_update_channel_description(channel_id_, std::move(about_));
      if (!td->auth_manager_->is_bot()) {
        promise_.set_value(Unit());
        return;
      }
    } else {
      td->contacts_manager_->on_get_channel_error(channel_id_, status, "EditChannelAboutQuery");
    }
    promise_.set_error(std::move(status));
  }
};

class UpdateChannelPinnedMessageQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  ChannelId channel_id_;
  MessageId message_id_;

 public:
  explicit UpdateChannelPinnedMessageQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(ChannelId channel_id, MessageId message_id, bool disable_notification) {
    channel_id_ = channel_id;
    message_id_ = message_id;
    auto input_channel = td->contacts_manager_->get_input_channel(channel_id);
    CHECK(input_channel != nullptr);

    int32 flags = 0;
    if (disable_notification) {
      flags |= telegram_api::channels_updatePinnedMessage::SILENT_MASK;
    }

    send_query(G()->net_query_creator().create(create_storer(telegram_api::channels_updatePinnedMessage(
        flags, false /*ignored*/, std::move(input_channel), message_id.get_server_message_id().get()))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::channels_updatePinnedMessage>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for updateChannelPinnedMessage: " << to_string(ptr);
    td->updates_manager_->on_get_updates(std::move(ptr));

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    if (status.message() == "CHAT_NOT_MODIFIED") {
      td->contacts_manager_->on_update_channel_pinned_message(channel_id_, message_id_);
      if (!td->auth_manager_->is_bot()) {
        promise_.set_value(Unit());
        return;
      }
    } else {
      td->contacts_manager_->on_get_channel_error(channel_id_, status, "UpdateChannelPinnedMessageQuery");
    }
    promise_.set_error(std::move(status));
  }
};

class ReportChannelSpamQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  ChannelId channel_id_;

 public:
  explicit ReportChannelSpamQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(ChannelId channel_id, UserId user_id, const vector<MessageId> &message_ids) {
    LOG(INFO) << "Send reportChannelSpamQuery in " << channel_id << " with messages " << format::as_array(message_ids)
              << " and " << user_id;
    channel_id_ = channel_id;

    auto input_channel = td->contacts_manager_->get_input_channel(channel_id);
    CHECK(input_channel != nullptr);

    auto input_user = td->contacts_manager_->get_input_user(user_id);
    CHECK(input_user != nullptr);

    send_query(G()->net_query_creator().create(create_storer(telegram_api::channels_reportSpam(
        std::move(input_channel), std::move(input_user), MessagesManager::get_server_message_ids(message_ids)))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::channels_reportSpam>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    bool result = result_ptr.move_as_ok();
    LOG_IF(INFO, !result) << "Report spam has failed";

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    td->contacts_manager_->on_get_channel_error(channel_id_, status, "ReportChannelSpamQuery");
    status.ignore();
  }
};

class DeleteChannelQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  ChannelId channel_id_;

 public:
  explicit DeleteChannelQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(ChannelId channel_id) {
    channel_id_ = channel_id;
    auto input_channel = td->contacts_manager_->get_input_channel(channel_id);
    CHECK(input_channel != nullptr);
    send_query(
        G()->net_query_creator().create(create_storer(telegram_api::channels_deleteChannel(std::move(input_channel)))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::channels_deleteChannel>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for deleteChannel: " << to_string(ptr);
    td->updates_manager_->on_get_updates(std::move(ptr));

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    td->contacts_manager_->on_get_channel_error(channel_id_, status, "DeleteChannelQuery");
    promise_.set_error(std::move(status));
  }
};

class AddChatUserQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit AddChatUserQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(ChatId chat_id, tl_object_ptr<telegram_api::InputUser> &&input_user, int32 forward_limit) {
    send_query(G()->net_query_creator().create(
        create_storer(telegram_api::messages_addChatUser(chat_id.get(), std::move(input_user), forward_limit))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_addChatUser>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for addChatUser: " << to_string(ptr);
    td->updates_manager_->on_get_updates(std::move(ptr));

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
    td->updates_manager_->get_difference("AddChatUserQuery");
  }
};

class EditChatAdminQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  ChatId chat_id_;

 public:
  explicit EditChatAdminQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(ChatId chat_id, tl_object_ptr<telegram_api::InputUser> &&input_user, bool is_administrator) {
    chat_id_ = chat_id;
    send_query(G()->net_query_creator().create(
        create_storer(telegram_api::messages_editChatAdmin(chat_id.get(), std::move(input_user), is_administrator))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_editChatAdmin>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    bool result = result_ptr.move_as_ok();
    if (!result) {
      LOG(ERROR) << "Receive false as result of messages.editChatAdmin";
      return on_error(id, Status::Error(400, "Can't edit chat administrators"));
    }

    // result will come in the updates
    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
    td->updates_manager_->get_difference("EditChatAdminQuery");
  }
};

class ExportChatInviteLinkQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  ChatId chat_id_;

 public:
  explicit ExportChatInviteLinkQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(ChatId chat_id) {
    chat_id_ = chat_id;
    send_query(G()->net_query_creator().create(create_storer(telegram_api::messages_exportChatInvite(chat_id.get()))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_exportChatInvite>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for exportChatInvite: " << to_string(ptr);

    td->contacts_manager_->on_get_chat_invite_link(chat_id_, std::move(ptr));
    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
    td->updates_manager_->get_difference("ExportChatInviteLinkQuery");
  }
};

class ExportChannelInviteLinkQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  ChannelId channel_id_;

 public:
  explicit ExportChannelInviteLinkQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(ChannelId channel_id) {
    channel_id_ = channel_id;
    auto input_channel = td->contacts_manager_->get_input_channel(channel_id);
    CHECK(input_channel != nullptr);
    send_query(
        G()->net_query_creator().create(create_storer(telegram_api::channels_exportInvite(std::move(input_channel)))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::channels_exportInvite>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for exportChannelInvite: " << to_string(ptr);

    td->contacts_manager_->on_get_channel_invite_link(channel_id_, std::move(ptr));
    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    td->contacts_manager_->on_get_channel_error(channel_id_, status, "ExportChannelInviteLinkQuery");
    promise_.set_error(std::move(status));
    td->updates_manager_->get_difference("ExportChannelInviteLinkQuery");
  }
};

class CheckDialogInviteLinkQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  string invite_link_;

 public:
  explicit CheckDialogInviteLinkQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(const string &invite_link) {
    invite_link_ = invite_link;
    send_query(G()->net_query_creator().create(create_storer(
        telegram_api::messages_checkChatInvite(ContactsManager::get_dialog_invite_link_hash(invite_link_).str()))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_checkChatInvite>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for checkChatInvite: " << to_string(ptr);

    td->contacts_manager_->on_get_dialog_invite_link_info(invite_link_, std::move(ptr));
    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class ImportDialogInviteLinkQuery : public Td::ResultHandler {
  Promise<DialogId> promise_;

  string invite_link_;

 public:
  explicit ImportDialogInviteLinkQuery(Promise<DialogId> &&promise) : promise_(std::move(promise)) {
  }

  void send(const string &invite_link) {
    invite_link_ = invite_link;
    send_query(G()->net_query_creator().create(create_storer(
        telegram_api::messages_importChatInvite(ContactsManager::get_dialog_invite_link_hash(invite_link).str()))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_importChatInvite>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for importChatInvite: " << to_string(ptr);

    auto dialog_ids = td->updates_manager_->get_chats(ptr.get());
    if (dialog_ids.size() != 1u) {
      LOG(ERROR) << "Receive wrong result for ImportDialogInviteLinkQuery: " << to_string(ptr);
      return on_error(id, Status::Error(500, "Internal Server Error"));
    }

    td->updates_manager_->on_get_updates(std::move(ptr));
    td->contacts_manager_->invalidate_invite_link(invite_link_);
    promise_.set_value(std::move(dialog_ids[0]));
  }

  void on_error(uint64 id, Status status) override {
    td->contacts_manager_->invalidate_invite_link(invite_link_);
    promise_.set_error(std::move(status));
  }
};

class DeleteChatUserQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit DeleteChatUserQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(ChatId chat_id, tl_object_ptr<telegram_api::InputUser> &&input_user) {
    send_query(G()->net_query_creator().create(
        create_storer(telegram_api::messages_deleteChatUser(chat_id.get(), std::move(input_user)))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_deleteChatUser>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for deleteChatUser: " << to_string(ptr);
    td->updates_manager_->on_get_updates(std::move(ptr));

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
    td->updates_manager_->get_difference("DeleteChatUserQuery");
  }
};

class JoinChannelQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  ChannelId channel_id_;

 public:
  explicit JoinChannelQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(ChannelId channel_id) {
    channel_id_ = channel_id;
    auto input_channel = td->contacts_manager_->get_input_channel(channel_id);
    CHECK(input_channel != nullptr);
    send_query(
        G()->net_query_creator().create(create_storer(telegram_api::channels_joinChannel(std::move(input_channel)))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::channels_joinChannel>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for joinChannel: " << to_string(ptr);
    td->updates_manager_->on_get_updates(std::move(ptr));

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    td->contacts_manager_->on_get_channel_error(channel_id_, status, "JoinChannelQuery");
    promise_.set_error(std::move(status));
    td->updates_manager_->get_difference("JoinChannelQuery");
  }
};

class InviteToChannelQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  ChannelId channel_id_;

 public:
  explicit InviteToChannelQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(ChannelId channel_id, vector<tl_object_ptr<telegram_api::InputUser>> &&input_users) {
    channel_id_ = channel_id;
    auto input_channel = td->contacts_manager_->get_input_channel(channel_id);
    CHECK(input_channel != nullptr);
    send_query(G()->net_query_creator().create(
        create_storer(telegram_api::channels_inviteToChannel(std::move(input_channel), std::move(input_users)))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::channels_inviteToChannel>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for inviteToChannel: " << to_string(ptr);
    td->updates_manager_->on_get_updates(std::move(ptr));
    td->contacts_manager_->invalidate_channel_full(channel_id_);

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    td->contacts_manager_->on_get_channel_error(channel_id_, status, "InviteToChannelQuery");
    promise_.set_error(std::move(status));
    td->updates_manager_->get_difference("InviteToChannelQuery");
  }
};

class EditChannelAdminQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  ChannelId channel_id_;

 public:
  explicit EditChannelAdminQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(ChannelId channel_id, tl_object_ptr<telegram_api::InputUser> &&input_user, DialogParticipantStatus status) {
    channel_id_ = channel_id;
    auto input_channel = td->contacts_manager_->get_input_channel(channel_id);
    CHECK(input_channel != nullptr);
    send_query(G()->net_query_creator().create(create_storer(telegram_api::channels_editAdmin(
        std::move(input_channel), std::move(input_user), status.get_channel_admin_rights()))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::channels_editAdmin>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for editChannelAdmin: " << to_string(ptr);
    td->updates_manager_->on_get_updates(std::move(ptr));
    td->contacts_manager_->invalidate_channel_full(channel_id_);

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    td->contacts_manager_->on_get_channel_error(channel_id_, status, "EditChannelAdminQuery");
    promise_.set_error(std::move(status));
    td->updates_manager_->get_difference("EditChannelAdminQuery");
  }
};

class EditChannelBannedQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  ChannelId channel_id_;

 public:
  explicit EditChannelBannedQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(ChannelId channel_id, tl_object_ptr<telegram_api::InputUser> &&input_user, DialogParticipantStatus status) {
    channel_id_ = channel_id;
    auto input_channel = td->contacts_manager_->get_input_channel(channel_id);
    CHECK(input_channel != nullptr);
    send_query(G()->net_query_creator().create(create_storer(telegram_api::channels_editBanned(
        std::move(input_channel), std::move(input_user), status.get_channel_banned_rights()))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::channels_editBanned>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for editChannelBanned: " << to_string(ptr);
    td->updates_manager_->on_get_updates(std::move(ptr));
    td->contacts_manager_->invalidate_channel_full(channel_id_);

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    td->contacts_manager_->on_get_channel_error(channel_id_, status, "EditChannelBannedQuery");
    promise_.set_error(std::move(status));
    td->updates_manager_->get_difference("EditChannelBannedQuery");
  }
};

class LeaveChannelQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  ChannelId channel_id_;

 public:
  explicit LeaveChannelQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(ChannelId channel_id) {
    channel_id_ = channel_id;
    auto input_channel = td->contacts_manager_->get_input_channel(channel_id);
    CHECK(input_channel != nullptr);
    send_query(
        G()->net_query_creator().create(create_storer(telegram_api::channels_leaveChannel(std::move(input_channel)))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::channels_leaveChannel>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for leaveChannel: " << to_string(ptr);
    td->updates_manager_->on_get_updates(std::move(ptr));

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    td->contacts_manager_->on_get_channel_error(channel_id_, status, "LeaveChannelQuery");
    promise_.set_error(std::move(status));
    td->updates_manager_->get_difference("LeaveChannelQuery");
  }
};

class MigrateChatQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit MigrateChatQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(ChatId chat_id) {
    send_query(G()->net_query_creator().create(create_storer(telegram_api::messages_migrateChat(chat_id.get()))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_migrateChat>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for migrateChat: " << to_string(ptr);
    td->updates_manager_->on_get_updates(std::move(ptr));

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
    td->updates_manager_->get_difference("MigrateChatQuery");
  }
};

class GetCreatedPublicChannelsQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit GetCreatedPublicChannelsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create(create_storer(telegram_api::channels_getAdminedPublicChannels())));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::channels_getAdminedPublicChannels>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto chats_ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetCreatedPublicChannelsQuery " << to_string(chats_ptr);
    int32 constructor_id = chats_ptr->get_id();
    switch (constructor_id) {
      case telegram_api::messages_chats::ID: {
        auto chats = move_tl_object_as<telegram_api::messages_chats>(chats_ptr);
        td->contacts_manager_->on_get_created_public_channels(std::move(chats->chats_));
        break;
      }
      case telegram_api::messages_chatsSlice::ID: {
        auto chats = move_tl_object_as<telegram_api::messages_chatsSlice>(chats_ptr);
        LOG(ERROR) << "Receive chatsSlice in result of GetCreatedPublicChannelsQuery";
        td->contacts_manager_->on_get_created_public_channels(std::move(chats->chats_));
        break;
      }
      default:
        UNREACHABLE();
    }

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class GetUsersQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit GetUsersQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(vector<tl_object_ptr<telegram_api::InputUser>> &&input_users) {
    send_query(G()->net_query_creator().create(create_storer(telegram_api::users_getUsers(std::move(input_users)))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::users_getUsers>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    td->contacts_manager_->on_get_users(result_ptr.move_as_ok());

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class GetFullUserQuery : public Td::ResultHandler {
  UserId user_id_;

 public:
  void send(UserId user_id, tl_object_ptr<telegram_api::InputUser> &&input_user) {
    user_id_ = user_id;
    send_query(G()->net_query_creator().create(create_storer(telegram_api::users_getFullUser(std::move(input_user)))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::users_getFullUser>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    LOG(DEBUG) << "Receive result for getFullUser " << to_string(result_ptr.ok());
    td->contacts_manager_->on_get_user_full(result_ptr.move_as_ok());
    td->contacts_manager_->on_get_user_full_success(user_id_);
  }

  void on_error(uint64 id, Status status) override {
    td->contacts_manager_->on_get_user_full_fail(user_id_, std::move(status));
  }
};

class GetUserPhotosQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  UserId user_id_;
  int32 offset_;
  int32 limit_;

 public:
  explicit GetUserPhotosQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(UserId user_id, tl_object_ptr<telegram_api::InputUser> &&input_user, int32 offset, int32 limit) {
    user_id_ = user_id;
    offset_ = offset;
    limit_ = limit;
    LOG(INFO) << "Get " << user_id << " profile photos with offset " << offset << " and limit " << limit;
    send_query(G()->net_query_creator().create(
        create_storer(telegram_api::photos_getUserPhotos(std::move(input_user), offset, 0, limit))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::photos_getUserPhotos>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();

    LOG(INFO) << "Receive result for GetUserPhotosQuery " << to_string(ptr);
    int32 constructor_id = ptr->get_id();
    if (constructor_id == telegram_api::photos_photos::ID) {
      auto photos = move_tl_object_as<telegram_api::photos_photos>(ptr);

      td->contacts_manager_->on_get_users(std::move(photos->users_));
      int32 photos_size = narrow_cast<int32>(photos->photos_.size());
      td->contacts_manager_->on_get_user_photos(user_id_, offset_, limit_, photos_size, std::move(photos->photos_));
    } else {
      CHECK(constructor_id == telegram_api::photos_photosSlice::ID);
      auto photos = move_tl_object_as<telegram_api::photos_photosSlice>(ptr);

      td->contacts_manager_->on_get_users(std::move(photos->users_));
      td->contacts_manager_->on_get_user_photos(user_id_, offset_, limit_, photos->count_, std::move(photos->photos_));
    }

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class GetChatsQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit GetChatsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(vector<int32> &&chat_ids) {
    send_query(G()->net_query_creator().create(create_storer(telegram_api::messages_getChats(std::move(chat_ids)))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_getChats>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto chats_ptr = result_ptr.move_as_ok();
    int32 constructor_id = chats_ptr->get_id();
    switch (constructor_id) {
      case telegram_api::messages_chats::ID: {
        auto chats = move_tl_object_as<telegram_api::messages_chats>(chats_ptr);
        td->contacts_manager_->on_get_chats(std::move(chats->chats_));
        break;
      }
      case telegram_api::messages_chatsSlice::ID: {
        auto chats = move_tl_object_as<telegram_api::messages_chatsSlice>(chats_ptr);
        LOG(ERROR) << "Receive chatsSlice in result of GetChatsQuery";
        td->contacts_manager_->on_get_chats(std::move(chats->chats_));
        break;
      }
      default:
        UNREACHABLE();
    }

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class GetFullChatQuery : public Td::ResultHandler {
  ChatId chat_id_;

 public:
  void send(ChatId chat_id) {
    chat_id_ = chat_id;
    LOG(INFO) << "Send getFullChat query to get " << chat_id;
    send_query(G()->net_query_creator().create(create_storer(telegram_api::messages_getFullChat(chat_id.get()))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_getFullChat>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    //    LOG(INFO) << "Receive result for getFullChat query: " << to_string(ptr);
    td->contacts_manager_->on_get_users(std::move(ptr->users_));
    td->contacts_manager_->on_get_chats(std::move(ptr->chats_));
    td->contacts_manager_->on_get_chat_full(std::move(ptr->full_chat_));

    td->contacts_manager_->on_get_chat_full_success(chat_id_);
  }

  void on_error(uint64 id, Status status) override {
    td->contacts_manager_->on_get_chat_full_fail(chat_id_, std::move(status));
  }
};

class GetChannelsQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  ChannelId channel_id_;

 public:
  explicit GetChannelsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(tl_object_ptr<telegram_api::InputChannel> &&input_channel) {
    CHECK(input_channel != nullptr);
    if (input_channel->get_id() == telegram_api::inputChannel::ID) {
      channel_id_ = ChannelId(static_cast<const telegram_api::inputChannel *>(input_channel.get())->channel_id_);
    }

    vector<tl_object_ptr<telegram_api::InputChannel>> input_channels;
    input_channels.push_back(std::move(input_channel));
    send_query(
        G()->net_query_creator().create(create_storer(telegram_api::channels_getChannels(std::move(input_channels)))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::channels_getChannels>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    //    LOG(INFO) << "Receive result for getChannels query: " << to_string(result_ptr.ok());
    auto chats_ptr = result_ptr.move_as_ok();
    int32 constructor_id = chats_ptr->get_id();
    switch (constructor_id) {
      case telegram_api::messages_chats::ID: {
        auto chats = move_tl_object_as<telegram_api::messages_chats>(chats_ptr);
        td->contacts_manager_->on_get_chats(std::move(chats->chats_));
        break;
      }
      case telegram_api::messages_chatsSlice::ID: {
        auto chats = move_tl_object_as<telegram_api::messages_chatsSlice>(chats_ptr);
        LOG(ERROR) << "Receive chatsSlice in result of GetChannelsQuery";
        td->contacts_manager_->on_get_chats(std::move(chats->chats_));
        break;
      }
      default:
        UNREACHABLE();
    }

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    td->contacts_manager_->on_get_channel_error(channel_id_, status, "GetChannelsQuery");
    promise_.set_error(std::move(status));
  }
};

class GetFullChannelQuery : public Td::ResultHandler {
  ChannelId channel_id_;

 public:
  void send(ChannelId channel_id, tl_object_ptr<telegram_api::InputChannel> &&input_channel) {
    channel_id_ = channel_id;
    send_query(G()->net_query_creator().create(
        create_storer(telegram_api::channels_getFullChannel(std::move(input_channel)))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::channels_getFullChannel>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    //    LOG(INFO) << "Receive result for getFullChannel query: " << to_string(ptr);
    td->contacts_manager_->on_get_users(std::move(ptr->users_));
    td->contacts_manager_->on_get_chats(std::move(ptr->chats_));
    td->contacts_manager_->on_get_chat_full(std::move(ptr->full_chat_));

    td->contacts_manager_->on_get_channel_full_success(channel_id_);
  }

  void on_error(uint64 id, Status status) override {
    td->contacts_manager_->on_get_channel_error(channel_id_, status, "GetFullChannelQuery");
    td->contacts_manager_->on_get_channel_full_fail(channel_id_, std::move(status));
  }
};

class GetChannelParticipantQuery : public Td::ResultHandler {
  Promise<DialogParticipant> promise_;
  ChannelId channel_id_;
  UserId user_id_;

 public:
  explicit GetChannelParticipantQuery(Promise<DialogParticipant> &&promise) : promise_(std::move(promise)) {
  }

  void send(ChannelId channel_id, UserId user_id, tl_object_ptr<telegram_api::InputUser> &&input_user) {
    auto input_channel = td->contacts_manager_->get_input_channel(channel_id);
    if (input_channel == nullptr) {
      return promise_.set_error(Status::Error(3, "Supergroup not found"));
    }

    CHECK(input_user != nullptr);

    channel_id_ = channel_id;
    user_id_ = user_id;
    send_query(G()->net_query_creator().create(
        create_storer(telegram_api::channels_getParticipant(std::move(input_channel), std::move(input_user)))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::channels_getParticipant>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto participant = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetChannelParticipantQuery: " << to_string(participant);

    td->contacts_manager_->on_get_users(std::move(participant->users_));
    promise_.set_value(
        td->contacts_manager_->get_dialog_participant(channel_id_, std::move(participant->participant_)));
  }

  void on_error(uint64 id, Status status) override {
    if (status.message() == "USER_NOT_PARTICIPANT") {
      promise_.set_value({user_id_, UserId(), 0, DialogParticipantStatus::Left()});
      return;
    }

    td->contacts_manager_->on_get_channel_error(channel_id_, status, "GetChannelParticipantQuery");
    promise_.set_error(std::move(status));
  }
};

class GetChannelParticipantsQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  ChannelId channel_id_;
  ChannelParticipantsFilter filter_{nullptr};
  int32 offset_;
  int32 limit_;
  int64 random_id_;

 public:
  explicit GetChannelParticipantsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(ChannelId channel_id, ChannelParticipantsFilter filter, int32 offset, int32 limit, int64 random_id) {
    auto input_channel = td->contacts_manager_->get_input_channel(channel_id);
    if (input_channel == nullptr) {
      return promise_.set_error(Status::Error(3, "Supergroup not found"));
    }

    channel_id_ = channel_id;
    filter_ = std::move(filter);
    offset_ = offset;
    limit_ = limit;
    random_id_ = random_id;
    send_query(G()->net_query_creator().create(create_storer(telegram_api::channels_getParticipants(
        std::move(input_channel), filter_.get_input_channel_participants_filter(), offset, limit, 0))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::channels_getParticipants>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto participants_ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetChannelParticipantsQuery with filter "
              << to_string(filter_.get_input_channel_participants_filter()) << ": " << to_string(participants_ptr);
    switch (participants_ptr->get_id()) {
      case telegram_api::channels_channelParticipants::ID: {
        auto participants = telegram_api::move_object_as<telegram_api::channels_channelParticipants>(participants_ptr);
        td->contacts_manager_->on_get_users(std::move(participants->users_));
        td->contacts_manager_->on_get_channel_participants_success(channel_id_, std::move(filter_), offset_, limit_,
                                                                   random_id_, participants->count_,
                                                                   std::move(participants->participants_));
        break;
      }
      case telegram_api::channels_channelParticipantsNotModified::ID:
        LOG(ERROR) << "Receive channelParticipantsNotModified";
        break;
      default:
        UNREACHABLE();
    }

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    td->contacts_manager_->on_get_channel_error(channel_id_, status, "GetChannelParticipantsQuery");
    td->contacts_manager_->on_get_channel_participants_fail(channel_id_, std::move(filter_), offset_, limit_,
                                                            random_id_);
    promise_.set_error(std::move(status));
  }
};

class GetChannelAdministratorsQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  ChannelId channel_id_;

 public:
  explicit GetChannelAdministratorsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(ChannelId channel_id, int32 hash) {
    auto input_channel = td->contacts_manager_->get_input_channel(channel_id);
    if (input_channel == nullptr) {
      return promise_.set_error(Status::Error(3, "Supergroup not found"));
    }

    channel_id_ = channel_id;
    send_query(G()->net_query_creator().create(create_storer(telegram_api::channels_getParticipants(
        std::move(input_channel), telegram_api::make_object<telegram_api::channelParticipantsAdmins>(), 0,
        std::numeric_limits<int32>::max(), hash))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::channels_getParticipants>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto participants_ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetChannelAdministratorsQuery: " << to_string(participants_ptr);
    switch (participants_ptr->get_id()) {
      case telegram_api::channels_channelParticipants::ID: {
        auto participants = telegram_api::move_object_as<telegram_api::channels_channelParticipants>(participants_ptr);
        td->contacts_manager_->on_get_users(std::move(participants->users_));
        vector<UserId> administrator_user_ids;
        administrator_user_ids.reserve(participants->participants_.size());
        for (auto &participant : participants->participants_) {
          UserId user_id;
          downcast_call(*participant, [&user_id](auto &participant) { user_id = UserId(participant.user_id_); });
          if (user_id.is_valid()) {
            administrator_user_ids.push_back(user_id);
          }
        }

        td->contacts_manager_->on_update_dialog_administrators(DialogId(channel_id_), std::move(administrator_user_ids),
                                                               true);
        break;
      }
      case telegram_api::channels_channelParticipantsNotModified::ID:
        break;
      default:
        UNREACHABLE();
    }

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    td->contacts_manager_->on_get_channel_error(channel_id_, status, "GetChannelAdministratorsQuery");
    promise_.set_error(std::move(status));
  }
};

class GetSupportUserQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit GetSupportUserQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create(create_storer(telegram_api::help_getSupport())));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::help_getSupport>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetSupportUserQuery: " << to_string(ptr);

    td->contacts_manager_->on_get_user(std::move(ptr->user_), false, true);

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

StringBuilder &operator<<(StringBuilder &string_builder, ContactsManager::LinkState link_state) {
  switch (link_state) {
    case ContactsManager::LinkState::Unknown:
      string_builder << "unknown";
      break;
    case ContactsManager::LinkState::None:
      string_builder << "none";
      break;
    case ContactsManager::LinkState::KnowsPhoneNumber:
      string_builder << "knows phone number";
      break;
    case ContactsManager::LinkState::Contact:
      string_builder << "contact";
      break;
  }
  return string_builder;
}

bool ContactsManager::UserFull::is_bot_info_expired(int32 bot_info_version) const {
  return bot_info_version != -1 && (bot_info == nullptr || bot_info->version != bot_info_version);
}

bool ContactsManager::UserFull::is_expired() const {
  return expires_at < Time::now();
}

bool ContactsManager::ChannelFull::is_expired() const {
  return expires_at < Time::now();
}

class ContactsManager::OnChatUpdate {
  ContactsManager *manager_;

 public:
  explicit OnChatUpdate(ContactsManager *manager) : manager_(manager) {
  }

  template <class T>
  void operator()(T &func) const {
    manager_->on_chat_update(func);
  }
};

class ContactsManager::UploadProfilePhotoCallback : public FileManager::UploadCallback {
 public:
  void on_upload_ok(FileId file_id, tl_object_ptr<telegram_api::InputFile> input_file) override {
    send_closure_later(G()->contacts_manager(), &ContactsManager::on_upload_profile_photo, file_id,
                       std::move(input_file));
  }
  void on_upload_encrypted_ok(FileId file_id, tl_object_ptr<telegram_api::InputEncryptedFile> input_file) override {
    UNREACHABLE();
  }
  void on_upload_secure_ok(FileId file_id, tl_object_ptr<telegram_api::InputSecureFile> input_file) override {
    UNREACHABLE();
  }
  void on_upload_error(FileId file_id, Status error) override {
    send_closure_later(G()->contacts_manager(), &ContactsManager::on_upload_profile_photo_error, file_id,
                       std::move(error));
  }
};

const CSlice ContactsManager::INVITE_LINK_URLS[3] = {"t.me/joinchat/", "telegram.me/joinchat/",
                                                     "telegram.dog/joinchat/"};

ContactsManager::ContactsManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
  upload_profile_photo_callback_ = std::make_shared<UploadProfilePhotoCallback>();

  my_id_ = load_my_id();

  if (G()->parameters().use_chat_info_db) {
    auto next_contacts_sync_date_string = G()->td_db()->get_binlog_pmc()->get("next_contacts_sync_date");
    if (!next_contacts_sync_date_string.empty()) {
      next_contacts_sync_date_ = min(to_integer<int32>(next_contacts_sync_date_string), G()->unix_time() + 100000);
    }

    auto saved_contact_count_string = G()->td_db()->get_binlog_pmc()->get("saved_contact_count");
    if (!saved_contact_count_string.empty()) {
      saved_contact_count_ = to_integer<int32>(saved_contact_count_string);
    }
  } else {
    G()->td_db()->get_binlog_pmc()->erase("next_contacts_sync_date");
    G()->td_db()->get_binlog_pmc()->erase("saved_contact_count");
  }

  user_online_timeout_.set_callback(on_user_online_timeout_callback);
  user_online_timeout_.set_callback_data(static_cast<void *>(this));

  channel_unban_timeout_.set_callback(on_channel_unban_timeout_callback);
  channel_unban_timeout_.set_callback_data(static_cast<void *>(this));
}

void ContactsManager::tear_down() {
  parent_.reset();
}

UserId ContactsManager::load_my_id() {
  auto id_string = G()->td_db()->get_binlog_pmc()->get("my_id");
  if (!id_string.empty()) {
    UserId my_id(to_integer<int32>(id_string));
    if (my_id.is_valid()) {
      return my_id;
    }

    my_id = UserId(to_integer<int32>(Slice(id_string).substr(5)));
    if (my_id.is_valid()) {
      G()->td_db()->get_binlog_pmc()->set("my_id", to_string(my_id.get()));
      return my_id;
    }

    LOG(ERROR) << "Wrong my id = \"" << id_string << "\" stored in database";
  }
  return UserId();
}

void ContactsManager::on_user_online_timeout_callback(void *contacts_manager_ptr, int64 user_id_long) {
  auto contacts_manager = static_cast<ContactsManager *>(contacts_manager_ptr);
  UserId user_id(narrow_cast<int32>(user_id_long));
  auto u = contacts_manager->get_user(user_id);
  CHECK(u != nullptr);

  LOG(INFO) << "Update " << user_id << " online status to offline";
  send_closure_later(
      G()->td(), &Td::send_update,
      make_tl_object<td_api::updateUserStatus>(user_id.get(), contacts_manager->get_user_status_object(user_id, u)));
}

void ContactsManager::on_channel_unban_timeout_callback(void *contacts_manager_ptr, int64 channel_id_long) {
  auto td = static_cast<ContactsManager *>(contacts_manager_ptr)->td_;
  send_closure_later(td->actor_id(td), &Td::on_channel_unban_timeout, channel_id_long);
}

void ContactsManager::on_channel_unban_timeout(ChannelId channel_id) {
  auto c = get_channel(channel_id);
  CHECK(c != nullptr);

  auto old_status = c->status;
  c->status.update_restrictions();
  LOG_IF(ERROR, c->status == old_status && (c->status.is_restricted() || c->status.is_banned()))
      << "Status of " << channel_id << " wasn't updated: " << c->status;

  LOG(INFO) << "Update " << channel_id << " status";
  c->is_status_changed = true;
  invalidate_channel_full(channel_id);
  update_channel(c, channel_id);  // always call, because in case of failure we need to reactivate timeout
}

template <class StorerT>
void ContactsManager::store_link_state(const LinkState &link_state, StorerT &storer) {
  store(static_cast<uint32>(link_state), storer);
}

template <class ParserT>
void ContactsManager::parse_link_state(LinkState &link_state, ParserT &parser) {
  uint32 link_state_uint32;
  parse(link_state_uint32, parser);
  link_state = static_cast<LinkState>(static_cast<uint8>(link_state_uint32));
}

template <class StorerT>
void ContactsManager::User::store(StorerT &storer) const {
  using td::store;
  bool has_last_name = !last_name.empty();
  bool has_username = !username.empty();
  bool has_photo = photo.small_file_id.is_valid();
  bool is_restricted = !restriction_reason.empty();
  bool has_language_code = !language_code.empty();
  bool have_access_hash = access_hash != -1;
  BEGIN_STORE_FLAGS();
  STORE_FLAG(is_received);
  STORE_FLAG(is_verified);
  STORE_FLAG(is_deleted);
  STORE_FLAG(is_bot);
  STORE_FLAG(can_join_groups);
  STORE_FLAG(can_read_all_group_messages);
  STORE_FLAG(is_inline_bot);
  STORE_FLAG(need_location_bot);
  STORE_FLAG(has_last_name);
  STORE_FLAG(has_username);
  STORE_FLAG(has_photo);
  STORE_FLAG(is_restricted);
  STORE_FLAG(has_language_code);
  STORE_FLAG(have_access_hash);
  END_STORE_FLAGS();
  store(first_name, storer);
  if (has_last_name) {
    store(last_name, storer);
  }
  if (has_username) {
    store(username, storer);
  }
  store(phone_number, storer);
  if (have_access_hash) {
    store(access_hash, storer);
  }
  if (has_photo) {
    store(photo, storer);
  }
  store_link_state(inbound, storer);
  store_link_state(outbound, storer);
  store(was_online, storer);
  if (is_restricted) {
    store(restriction_reason, storer);
  }
  if (is_inline_bot) {
    store(inline_query_placeholder, storer);
  }
  if (is_bot) {
    store(bot_info_version, storer);
  }
  if (has_language_code) {
    store(language_code, storer);
  }
}

template <class ParserT>
void ContactsManager::User::parse(ParserT &parser) {
  using td::parse;
  bool has_last_name;
  bool has_username;
  bool has_photo;
  bool is_restricted;
  bool has_language_code;
  bool have_access_hash;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(is_received);
  PARSE_FLAG(is_verified);
  PARSE_FLAG(is_deleted);
  PARSE_FLAG(is_bot);
  PARSE_FLAG(can_join_groups);
  PARSE_FLAG(can_read_all_group_messages);
  PARSE_FLAG(is_inline_bot);
  PARSE_FLAG(need_location_bot);
  PARSE_FLAG(has_last_name);
  PARSE_FLAG(has_username);
  PARSE_FLAG(has_photo);
  PARSE_FLAG(is_restricted);
  PARSE_FLAG(has_language_code);
  PARSE_FLAG(have_access_hash);
  END_PARSE_FLAGS();
  parse(first_name, parser);
  if (has_last_name) {
    parse(last_name, parser);
  }
  if (has_username) {
    parse(username, parser);
  }
  parse(phone_number, parser);
  if (parser.version() < static_cast<int32>(Version::FixMinUsers)) {
    have_access_hash = is_received;
  }
  if (have_access_hash) {
    parse(access_hash, parser);
  }
  if (has_photo) {
    parse(photo, parser);
  }
  parse_link_state(inbound, parser);
  parse_link_state(outbound, parser);
  parse(was_online, parser);
  if (is_restricted) {
    parse(restriction_reason, parser);
  }
  if (is_inline_bot) {
    parse(inline_query_placeholder, parser);
  }
  if (is_bot) {
    parse(bot_info_version, parser);
  }
  if (has_language_code) {
    parse(language_code, parser);
  }
  if (first_name.empty() && last_name.empty()) {
    first_name = phone_number;
  }
}

template <class StorerT>
void ContactsManager::Chat::store(StorerT &storer) const {
  using td::store;
  bool has_photo = photo.small_file_id.is_valid();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(left);
  STORE_FLAG(kicked);
  STORE_FLAG(is_creator);
  STORE_FLAG(is_administrator);
  STORE_FLAG(everyone_is_administrator);
  STORE_FLAG(can_edit);
  STORE_FLAG(is_active);
  STORE_FLAG(has_photo);
  END_STORE_FLAGS();

  store(title, storer);
  if (has_photo) {
    store(photo, storer);
  }
  store(participant_count, storer);
  store(date, storer);
  store(migrated_to_channel_id, storer);
  store(version, storer);
}

template <class ParserT>
void ContactsManager::Chat::parse(ParserT &parser) {
  using td::parse;
  bool has_photo;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(left);
  PARSE_FLAG(kicked);
  PARSE_FLAG(is_creator);
  PARSE_FLAG(is_administrator);
  PARSE_FLAG(everyone_is_administrator);
  PARSE_FLAG(can_edit);
  PARSE_FLAG(is_active);
  PARSE_FLAG(has_photo);
  END_PARSE_FLAGS();

  parse(title, parser);
  if (has_photo) {
    parse(photo, parser);
  }
  parse(participant_count, parser);
  parse(date, parser);
  parse(migrated_to_channel_id, parser);
  parse(version, parser);
}

template <class StorerT>
void ContactsManager::Channel::store(StorerT &storer) const {
  using td::store;
  bool has_photo = photo.small_file_id.is_valid();
  bool has_username = !username.empty();
  bool is_restricted = !restriction_reason.empty();
  bool use_new_rights = true;
  bool have_participant_count = participant_count != 0;
  BEGIN_STORE_FLAGS();
  STORE_FLAG(false);
  STORE_FLAG(false);
  STORE_FLAG(anyone_can_invite);
  STORE_FLAG(sign_messages);
  STORE_FLAG(false);
  STORE_FLAG(false);
  STORE_FLAG(false);
  STORE_FLAG(is_megagroup);
  STORE_FLAG(is_verified);
  STORE_FLAG(has_photo);
  STORE_FLAG(has_username);
  STORE_FLAG(is_restricted);
  STORE_FLAG(use_new_rights);
  STORE_FLAG(have_participant_count);
  END_STORE_FLAGS();

  store(status, storer);
  store(access_hash, storer);
  store(title, storer);
  if (has_photo) {
    store(photo, storer);
  }
  if (has_username) {
    store(username, storer);
  }
  store(date, storer);
  if (is_restricted) {
    store(restriction_reason, storer);
  }
  if (have_participant_count) {
    store(participant_count, storer);
  }
}

template <class ParserT>
void ContactsManager::Channel::parse(ParserT &parser) {
  using td::parse;
  bool has_photo;
  bool has_username;
  bool is_restricted;
  bool left;
  bool kicked;
  bool is_creator;
  bool can_edit;
  bool can_moderate;
  bool use_new_rights;
  bool have_participant_count;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(left);
  PARSE_FLAG(kicked);
  PARSE_FLAG(anyone_can_invite);
  PARSE_FLAG(sign_messages);
  PARSE_FLAG(is_creator);
  PARSE_FLAG(can_edit);
  PARSE_FLAG(can_moderate);
  PARSE_FLAG(is_megagroup);
  PARSE_FLAG(is_verified);
  PARSE_FLAG(has_photo);
  PARSE_FLAG(has_username);
  PARSE_FLAG(is_restricted);
  PARSE_FLAG(use_new_rights);
  PARSE_FLAG(have_participant_count);
  END_PARSE_FLAGS();

  if (use_new_rights) {
    parse(status, parser);
  } else {
    if (kicked) {
      status = DialogParticipantStatus::Banned(0);
    } else if (left) {
      status = DialogParticipantStatus::Left();
    } else if (is_creator) {
      status = DialogParticipantStatus::Creator(true);
    } else if (can_edit || can_moderate) {
      status = DialogParticipantStatus::ChannelAdministrator(false, is_megagroup);
    } else {
      status = DialogParticipantStatus::Member();
    }
  }
  parse(access_hash, parser);
  parse(title, parser);
  if (has_photo) {
    parse(photo, parser);
  }
  if (has_username) {
    parse(username, parser);
  }
  parse(date, parser);
  if (is_restricted) {
    parse(restriction_reason, parser);
  }
  if (have_participant_count) {
    parse(participant_count, parser);
  }
}

template <class StorerT>
void ContactsManager::SecretChat::store(StorerT &storer) const {
  using td::store;
  bool has_layer = layer > SecretChatActor::DEFAULT_LAYER;
  BEGIN_STORE_FLAGS();
  STORE_FLAG(is_outbound);
  STORE_FLAG(has_layer);
  END_STORE_FLAGS();

  store(access_hash, storer);
  store(user_id, storer);
  store(state, storer);
  store(ttl, storer);
  store(date, storer);
  store(key_hash, storer);
  if (has_layer) {
    store(layer, storer);
  }
}

template <class ParserT>
void ContactsManager::SecretChat::parse(ParserT &parser) {
  using td::parse;
  bool has_layer;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(is_outbound);
  PARSE_FLAG(has_layer);
  END_PARSE_FLAGS();

  if (parser.version() >= static_cast<int32>(Version::AddAccessHashToSecretChat)) {
    parse(access_hash, parser);
  }
  parse(user_id, parser);
  parse(state, parser);
  parse(ttl, parser);
  parse(date, parser);
  if (parser.version() >= static_cast<int32>(Version::AddKeyHashToSecretChat)) {
    parse(key_hash, parser);
  }
  if (has_layer) {
    parse(layer, parser);
  } else {
    layer = SecretChatActor::DEFAULT_LAYER;
  }
}

tl_object_ptr<telegram_api::InputUser> ContactsManager::get_input_user(UserId user_id) const {
  if (user_id == get_my_id("get_input_user")) {
    return make_tl_object<telegram_api::inputUserSelf>();
  }

  const User *u = get_user(user_id);
  if (u == nullptr || u->access_hash == -1) {
    if (td_->auth_manager_->is_bot() && user_id.is_valid()) {
      return make_tl_object<telegram_api::inputUser>(user_id.get(), 0);
    }
    return nullptr;
  }

  return make_tl_object<telegram_api::inputUser>(user_id.get(), u->access_hash);
}

bool ContactsManager::have_input_user(UserId user_id) const {
  if (user_id == get_my_id("have_input_user")) {
    return true;
  }

  const User *u = get_user(user_id);
  if (u == nullptr || u->access_hash == -1) {
    if (td_->auth_manager_->is_bot() && user_id.is_valid()) {
      return true;
    }
    return false;
  }

  return true;
}

tl_object_ptr<telegram_api::InputChannel> ContactsManager::get_input_channel(ChannelId channel_id) const {
  const Channel *c = get_channel(channel_id);
  if (c == nullptr) {
    if (td_->auth_manager_->is_bot() && channel_id.is_valid()) {
      return make_tl_object<telegram_api::inputChannel>(channel_id.get(), 0);
    }
    return nullptr;
  }

  return make_tl_object<telegram_api::inputChannel>(channel_id.get(), c->access_hash);
}

bool ContactsManager::have_input_peer_user(UserId user_id, AccessRights access_rights) const {
  if (user_id == get_my_id("have_input_peer_user")) {
    return true;
  }
  return have_input_peer_user(get_user(user_id), access_rights);
}

bool ContactsManager::have_input_peer_user(const User *user, AccessRights access_rights) {
  if (user == nullptr) {
    return false;
  }
  if (user->access_hash == -1) {
    return false;
  }
  if (access_rights == AccessRights::Read) {
    return true;
  }
  if (user->is_deleted) {
    return false;
  }
  return true;
}

tl_object_ptr<telegram_api::InputPeer> ContactsManager::get_input_peer_user(UserId user_id,
                                                                            AccessRights access_rights) const {
  if (user_id == get_my_id("get_input_peer_user")) {
    return make_tl_object<telegram_api::inputPeerSelf>();
  }
  const User *u = get_user(user_id);
  if (!have_input_peer_user(u, access_rights)) {
    return nullptr;
  }

  return make_tl_object<telegram_api::inputPeerUser>(user_id.get(), u->access_hash);
}

bool ContactsManager::have_input_peer_chat(ChatId chat_id, AccessRights access_rights) const {
  return have_input_peer_chat(get_chat(chat_id), access_rights);
}

bool ContactsManager::have_input_peer_chat(const Chat *chat, AccessRights access_rights) {
  if (chat == nullptr) {
    return false;
  }
  if (access_rights == AccessRights::Read) {
    return true;
  }
  if (chat->left) {
    return false;
  }
  if (access_rights == AccessRights::Write && !chat->is_active) {
    return false;
  }
  return true;
}

tl_object_ptr<telegram_api::InputPeer> ContactsManager::get_input_peer_chat(ChatId chat_id,
                                                                            AccessRights access_rights) const {
  auto c = get_chat(chat_id);
  if (!have_input_peer_chat(c, access_rights)) {
    return nullptr;
  }

  return make_tl_object<telegram_api::inputPeerChat>(chat_id.get());
}

bool ContactsManager::have_input_peer_channel(ChannelId channel_id, AccessRights access_rights) const {
  const Channel *c = get_channel(channel_id);
  return have_input_peer_channel(c, access_rights);
}

tl_object_ptr<telegram_api::InputPeer> ContactsManager::get_input_peer_channel(ChannelId channel_id,
                                                                               AccessRights access_rights) const {
  const Channel *c = get_channel(channel_id);
  if (!have_input_peer_channel(c, access_rights)) {
    return nullptr;
  }

  return make_tl_object<telegram_api::inputPeerChannel>(channel_id.get(), c->access_hash);
}

bool ContactsManager::have_input_peer_channel(const Channel *c, AccessRights access_rights) {
  if (c == nullptr) {
    return false;
  }
  if (c->status.is_creator()) {
    return true;
  }
  if (c->status.is_banned()) {
    return false;
  }
  if (!c->username.empty() && access_rights == AccessRights::Read) {
    return true;
  }
  if (!c->status.is_member()) {
    return false;
  }
  return true;
}

bool ContactsManager::have_input_encrypted_peer(SecretChatId secret_chat_id, AccessRights access_rights) const {
  return have_input_encrypted_peer(get_secret_chat(secret_chat_id), access_rights);
}

bool ContactsManager::have_input_encrypted_peer(const SecretChat *secret_chat, AccessRights access_rights) {
  if (secret_chat == nullptr) {
    return false;
  }
  if (access_rights == AccessRights::Read) {
    return true;
  }
  return secret_chat->state == SecretChatState::Active;
}

tl_object_ptr<telegram_api::inputEncryptedChat> ContactsManager::get_input_encrypted_chat(
    SecretChatId secret_chat_id, AccessRights access_rights) const {
  auto sc = get_secret_chat(secret_chat_id);
  if (!have_input_encrypted_peer(sc, access_rights)) {
    return nullptr;
  }

  return make_tl_object<telegram_api::inputEncryptedChat>(secret_chat_id.get(), sc->access_hash);
}

const DialogPhoto *ContactsManager::get_user_dialog_photo(UserId user_id) {
  auto u = get_user(user_id);
  if (u == nullptr) {
    return nullptr;
  }

  auto it = pending_user_photos_.find(user_id);
  if (it != pending_user_photos_.end()) {
    do_update_user_photo(u, user_id, std::move(it->second));
    pending_user_photos_.erase(it);
    update_user(u, user_id);
  }
  return &u->photo;
}

const DialogPhoto *ContactsManager::get_chat_dialog_photo(ChatId chat_id) const {
  auto c = get_chat(chat_id);
  if (c == nullptr) {
    return nullptr;
  }
  return &c->photo;
}

const DialogPhoto *ContactsManager::get_channel_dialog_photo(ChannelId channel_id) const {
  auto c = get_channel(channel_id);
  if (c == nullptr) {
    return nullptr;
  }
  return &c->photo;
}

const DialogPhoto *ContactsManager::get_secret_chat_dialog_photo(SecretChatId secret_chat_id) {
  auto c = get_secret_chat(secret_chat_id);
  if (c == nullptr) {
    return nullptr;
  }
  return get_user_dialog_photo(c->user_id);
}

string ContactsManager::get_user_title(UserId user_id) const {
  auto u = get_user(user_id);
  if (u == nullptr) {
    return string();
  }
  if (u->last_name.empty()) {
    return u->first_name;
  }
  if (u->first_name.empty()) {
    return u->last_name;
  }
  return PSTRING() << u->first_name << ' ' << u->last_name;
}

string ContactsManager::get_chat_title(ChatId chat_id) const {
  auto c = get_chat(chat_id);
  if (c == nullptr) {
    return string();
  }
  return c->title;
}

string ContactsManager::get_channel_title(ChannelId channel_id) const {
  auto c = get_channel(channel_id);
  if (c == nullptr) {
    return string();
  }
  return c->title;
}

string ContactsManager::get_secret_chat_title(SecretChatId secret_chat_id) const {
  auto c = get_secret_chat(secret_chat_id);
  if (c == nullptr) {
    return string();
  }
  return get_user_title(c->user_id);
}

int32 ContactsManager::get_secret_chat_date(SecretChatId secret_chat_id) const {
  auto c = get_secret_chat(secret_chat_id);
  if (c == nullptr) {
    return 0;
  }
  return c->date;
}

int32 ContactsManager::get_secret_chat_ttl(SecretChatId secret_chat_id) const {
  auto c = get_secret_chat(secret_chat_id);
  if (c == nullptr) {
    return 0;
  }
  return c->ttl;
}

bool ContactsManager::default_can_report_spam_in_secret_chat(SecretChatId secret_chat_id) const {
  auto c = get_secret_chat(secret_chat_id);
  if (c == nullptr) {
    return true;
  }
  if (c->is_outbound) {
    return false;
  }
  auto u = get_user(c->user_id);
  if (u == nullptr) {
    return true;
  }
  if (u->outbound == LinkState::Contact) {
    return false;
  }
  return true;
}

string ContactsManager::get_user_username(UserId user_id) const {
  if (!user_id.is_valid()) {
    return string();
  }

  auto u = get_user(user_id);
  if (u == nullptr) {
    return string();
  }
  return u->username;
}

string ContactsManager::get_secret_chat_username(SecretChatId secret_chat_id) const {
  auto c = get_secret_chat(secret_chat_id);
  if (c == nullptr) {
    return string();
  }
  return get_user_username(c->user_id);
}

string ContactsManager::get_channel_username(ChannelId channel_id) const {
  auto c = get_channel(channel_id);
  if (c == nullptr) {
    return string();
  }
  return c->username;
}
UserId ContactsManager::get_secret_chat_user_id(SecretChatId secret_chat_id) const {
  auto c = get_secret_chat(secret_chat_id);
  if (c == nullptr) {
    return UserId();
  }
  return c->user_id;
}

SecretChatState ContactsManager::get_secret_chat_state(SecretChatId secret_chat_id) const {
  auto c = get_secret_chat(secret_chat_id);
  if (c == nullptr) {
    return SecretChatState::Unknown;
  }
  return c->state;
}

int32 ContactsManager::get_secret_chat_layer(SecretChatId secret_chat_id) const {
  auto c = get_secret_chat(secret_chat_id);
  if (c == nullptr) {
    return 0;
  }
  return c->layer;
}

UserId ContactsManager::get_my_id(const char *source) const {
  LOG_IF(ERROR, !my_id_.is_valid()) << "Wrong or unknown my id returned to " << source;
  return my_id_;
}

void ContactsManager::set_my_id(UserId my_id) {
  UserId my_old_id = my_id_;
  if (my_old_id.is_valid() && my_old_id != my_id) {
    LOG(ERROR) << "Already know that me is " << my_old_id << " but received userSelf with " << my_id;
  }
  if (!my_id.is_valid()) {
    LOG(ERROR) << "Receive invalid my id " << my_id;
    return;
  }
  if (my_old_id != my_id) {
    my_id_ = my_id;
    G()->td_db()->get_binlog_pmc()->set("my_id", to_string(my_id.get()));
    G()->shared_config().set_option_integer("my_id", my_id_.get());
  }
}

void ContactsManager::set_my_online_status(bool is_online, bool send_update, bool is_local) {
  auto my_id = get_my_id("set_my_online_status");
  User *u = get_user_force(my_id);
  if (u != nullptr) {
    int32 new_online;
    int32 now = G()->unix_time();
    if (is_online) {
      new_online = now + 300;
    } else {
      new_online = now - 1;
    }

    if (is_local) {
      LOG(INFO) << "Update my local online from " << my_was_online_local_ << " to " << new_online;
      if (!is_online) {
        new_online = min(new_online, u->was_online);
      }
      if (new_online != my_was_online_local_) {
        my_was_online_local_ = new_online;
        u->is_status_changed = true;
      }
    } else {
      if (my_was_online_local_ != 0 || new_online != u->was_online) {
        LOG(INFO) << "Update my online from " << u->was_online << " to " << new_online;
        my_was_online_local_ = 0;
        u->was_online = new_online;
        u->is_status_changed = true;
      }
    }

    if (send_update) {
      update_user(u, my_id);
    }
  }
}

UserId ContactsManager::get_service_notifications_user_id() {
  UserId user_id(777000);
  if (!have_user_force(user_id)) {
    LOG(FATAL) << "Failed to load service notification user";
  }
  return user_id;
}

void ContactsManager::check_dialog_username(DialogId dialog_id, const string &username,
                                            Promise<CheckDialogUsernameResult> &&promise) {
  if (dialog_id != DialogId() && !dialog_id.is_valid()) {
    return promise.set_error(Status::Error(3, "Chat not found"));
  }

  switch (dialog_id.get_type()) {
    case DialogType::User: {
      if (dialog_id.get_user_id() != get_my_id("check_dialog_username")) {
        return promise.set_error(Status::Error(3, "Can't check username for private chat with other user"));
      }
      break;
    }
    case DialogType::Channel: {
      auto c = get_channel(dialog_id.get_channel_id());
      if (c == nullptr) {
        return promise.set_error(Status::Error(6, "Chat not found"));
      }
      if (!get_channel_status(c).is_creator()) {
        return promise.set_error(Status::Error(6, "Not enough rights to change username"));
      }

      if (username == c->username) {
        return promise.set_value(CheckDialogUsernameResult::Ok);
      }
      break;
    }
    case DialogType::None:
      break;
    case DialogType::Chat:
    case DialogType::SecretChat:
      if (username.empty()) {
        return promise.set_value(CheckDialogUsernameResult::Ok);
      }
      return promise.set_error(Status::Error(3, "Chat can't have username"));
    default:
      UNREACHABLE();
      return;
  }

  if (username.empty()) {
    return promise.set_value(CheckDialogUsernameResult::Ok);
  }
  if (!is_valid_username(username)) {
    return promise.set_value(CheckDialogUsernameResult::Invalid);
  }

  auto request_promise = PromiseCreator::lambda([promise = std::move(promise)](Result<bool> result) mutable {
    if (result.is_error()) {
      auto error = result.move_as_error();
      if (error.message() == "CHANNEL_PUBLIC_GROUP_NA") {
        return promise.set_value(CheckDialogUsernameResult::PublicGroupsUnavailable);
      }
      if (error.message() == "CHANNELS_ADMIN_PUBLIC_TOO_MUCH") {
        return promise.set_value(CheckDialogUsernameResult::PublicDialogsTooMuch);
      }
      if (error.message() == "USERNAME_INVALID") {
        return promise.set_value(CheckDialogUsernameResult::Invalid);
      }
      return promise.set_error(std::move(error));
    }

    promise.set_value(result.ok() ? CheckDialogUsernameResult::Ok : CheckDialogUsernameResult::Occupied);
  });

  switch (dialog_id.get_type()) {
    case DialogType::User:
      return td_->create_handler<CheckUsernameQuery>(std::move(request_promise))->send(username);
    case DialogType::Channel:
      return td_->create_handler<CheckChannelUsernameQuery>(std::move(request_promise))
          ->send(dialog_id.get_channel_id(), username);
    case DialogType::None:
      return td_->create_handler<CheckChannelUsernameQuery>(std::move(request_promise))->send(ChannelId(), username);
    case DialogType::Chat:
    case DialogType::SecretChat:
    default:
      UNREACHABLE();
  }
}

td_api::object_ptr<td_api::CheckChatUsernameResult> ContactsManager::get_check_chat_username_result_object(
    CheckDialogUsernameResult result) {
  switch (result) {
    case CheckDialogUsernameResult::Ok:
      return td_api::make_object<td_api::checkChatUsernameResultOk>();
    case CheckDialogUsernameResult::Invalid:
      return td_api::make_object<td_api::checkChatUsernameResultUsernameInvalid>();
    case CheckDialogUsernameResult::Occupied:
      return td_api::make_object<td_api::checkChatUsernameResultUsernameOccupied>();
    case CheckDialogUsernameResult::PublicDialogsTooMuch:
      return td_api::make_object<td_api::checkChatUsernameResultPublicChatsTooMuch>();
    case CheckDialogUsernameResult::PublicGroupsUnavailable:
      return td_api::make_object<td_api::checkChatUsernameResultPublicGroupsUnavailable>();
    default:
      UNREACHABLE();
      return nullptr;
  }
}

void ContactsManager::set_account_ttl(int32 account_ttl, Promise<Unit> &&promise) const {
  td_->create_handler<SetAccountTtlQuery>(std::move(promise))->send(account_ttl);
}

void ContactsManager::get_account_ttl(Promise<int32> &&promise) const {
  td_->create_handler<GetAccountTtlQuery>(std::move(promise))->send();
}

void ContactsManager::get_active_sessions(Promise<tl_object_ptr<td_api::sessions>> &&promise) const {
  td_->create_handler<GetAuthorizationsQuery>(std::move(promise))->send();
}

void ContactsManager::terminate_session(int64 session_id, Promise<Unit> &&promise) const {
  td_->create_handler<ResetAuthorizationQuery>(std::move(promise))->send(session_id);
}

void ContactsManager::terminate_all_other_sessions(Promise<Unit> &&promise) const {
  td_->create_handler<ResetAuthorizationsQuery>(std::move(promise))->send();
}

void ContactsManager::get_connected_websites(Promise<tl_object_ptr<td_api::connectedWebsites>> &&promise) const {
  td_->create_handler<GetWebAuthorizationsQuery>(std::move(promise))->send();
}

void ContactsManager::disconnect_website(int64 website_id, Promise<Unit> &&promise) const {
  td_->create_handler<ResetWebAuthorizationQuery>(std::move(promise))->send(website_id);
}

void ContactsManager::disconnect_all_websites(Promise<Unit> &&promise) const {
  td_->create_handler<ResetWebAuthorizationsQuery>(std::move(promise))->send();
}

Status ContactsManager::block_user(UserId user_id) {
  if (user_id == get_my_id("block_user")) {
    return Status::Error(5, "Can't block self");
  }

  auto user = get_input_user(user_id);
  if (user == nullptr) {
    return Status::Error(5, "User not found");
  }

  td_->create_handler<BlockUserQuery>()->send(std::move(user));

  on_update_user_blocked(user_id, true);
  return Status::OK();
}

Status ContactsManager::unblock_user(UserId user_id) {
  if (user_id == get_my_id("unblock_user")) {
    return Status::Error(5, "Can't unblock self");
  }

  auto user = get_input_user(user_id);
  if (user == nullptr) {
    return Status::Error(5, "User not found");
  }

  td_->create_handler<UnblockUserQuery>()->send(std::move(user));

  on_update_user_blocked(user_id, false);
  return Status::OK();
}

bool ContactsManager::is_valid_username(const string &username) {
  if (username.size() < 5 || username.size() > 32) {
    return false;
  }
  if (!is_alpha(username[0])) {
    return false;
  }
  for (auto c : username) {
    if (!is_alpha(c) && !is_digit(c) && c != '_') {
      return false;
    }
  }
  if (username.back() == '_') {
    return false;
  }
  for (size_t i = 1; i < username.size(); i++) {
    if (username[i - 1] == '_' && username[i] == '_') {
      return false;
    }
  }
  if (username.find("admin") == 0 || username.find("telegram") == 0 || username.find("support") == 0 ||
      username.find("security") == 0 || username.find("settings") == 0 || username.find("contacts") == 0 ||
      username.find("service") == 0 || username.find("telegraph") == 0) {
    return false;
  }
  return true;
}

int64 ContactsManager::get_blocked_users(int32 offset, int32 limit, Promise<Unit> &&promise) {
  LOG(INFO) << "Get blocked users with offset = " << offset << " and limit = " << limit;

  if (offset < 0) {
    promise.set_error(Status::Error(3, "Parameter offset must be non-negative"));
    return 0;
  }

  if (limit <= 0) {
    promise.set_error(Status::Error(3, "Parameter limit must be positive"));
    return 0;
  }

  int64 random_id;
  do {
    random_id = Random::secure_int64();
  } while (random_id == 0 || found_blocked_users_.find(random_id) != found_blocked_users_.end());
  found_blocked_users_[random_id];  // reserve place for result

  td_->create_handler<GetBlockedUsersQuery>(std::move(promise))->send(offset, limit, random_id);
  return random_id;
}

void ContactsManager::on_get_blocked_users_result(int32 offset, int32 limit, int64 random_id, int32 total_count,
                                                  vector<tl_object_ptr<telegram_api::contactBlocked>> &&blocked_users) {
  LOG(INFO) << "Receive " << blocked_users.size() << " blocked users out of " << total_count;
  auto it = found_blocked_users_.find(random_id);
  CHECK(it != found_blocked_users_.end());

  auto &result = it->second.second;
  CHECK(result.empty());
  for (auto &blocked_user : blocked_users) {
    CHECK(blocked_user != nullptr);
    UserId user_id(blocked_user->user_id_);
    if (have_user(user_id)) {
      result.push_back(user_id);
    } else {
      LOG(ERROR) << "Have no info about " << user_id;
    }
  }
  it->second.first = total_count;
}

void ContactsManager::on_failed_get_blocked_users(int64 random_id) {
  auto it = found_blocked_users_.find(random_id);
  CHECK(it != found_blocked_users_.end());
  found_blocked_users_.erase(it);
}

tl_object_ptr<td_api::users> ContactsManager::get_blocked_users_object(int64 random_id) {
  auto it = found_blocked_users_.find(random_id);
  CHECK(it != found_blocked_users_.end());
  auto result = get_users_object(it->second.first, it->second.second);
  found_blocked_users_.erase(it);
  return result;
}

void ContactsManager::load_contacts(Promise<Unit> &&promise) {
  if (td_->auth_manager_->is_bot()) {
    are_contacts_loaded_ = true;
    saved_contact_count_ = 0;
  }
  if (are_contacts_loaded_ && saved_contact_count_ != -1) {
    LOG(INFO) << "Contacts are already loaded";
    promise.set_value(Unit());
    return;
  }
  load_contacts_queries_.push_back(std::move(promise));
  if (load_contacts_queries_.size() == 1u) {
    if (G()->parameters().use_chat_info_db && next_contacts_sync_date_ > 0 && saved_contact_count_ != -1) {
      LOG(INFO) << "Load contacts from database";
      G()->td_db()->get_sqlite_pmc()->get(
          "user_contacts", PromiseCreator::lambda([](string value) {
            send_closure(G()->contacts_manager(), &ContactsManager::on_load_contacts_from_database, std::move(value));
          }));
    } else {
      LOG(INFO) << "Load contacts from server";
      reload_contacts(true);
    }
  } else {
    LOG(INFO) << "Load contacts request has already been sent";
  }
}

int32 ContactsManager::get_contacts_hash() {
  if (!are_contacts_loaded_) {
    return 0;
  }

  vector<int64> user_ids = contacts_hints_.search_empty(100000).second;
  CHECK(std::is_sorted(user_ids.begin(), user_ids.end()));
  auto my_id = get_my_id("get_contacts_hash");
  const User *u = get_user_force(my_id);
  if (u != nullptr && u->outbound == LinkState::Contact) {
    user_ids.insert(std::upper_bound(user_ids.begin(), user_ids.end(), my_id.get()), my_id.get());
  }

  vector<uint32> numbers;
  numbers.reserve(user_ids.size() + 1);
  numbers.push_back(saved_contact_count_);
  for (auto user_id : user_ids) {
    numbers.push_back(narrow_cast<uint32>(user_id));
  }
  return get_vector_hash(numbers);
}

void ContactsManager::reload_contacts(bool force) {
  if (!td_->auth_manager_->is_bot() && next_contacts_sync_date_ != std::numeric_limits<int32>::max() &&
      (next_contacts_sync_date_ < G()->unix_time() || force)) {
    next_contacts_sync_date_ = std::numeric_limits<int32>::max();
    td_->create_handler<GetContactsQuery>()->send(get_contacts_hash());
  }
}

std::pair<vector<UserId>, vector<int32>> ContactsManager::import_contacts(
    const vector<tl_object_ptr<td_api::contact>> &contacts, int64 &random_id, Promise<Unit> &&promise) {
  if (!are_contacts_loaded_) {
    load_contacts(std::move(promise));
    return {};
  }

  LOG(INFO) << "Asked to import " << contacts.size() << " contacts with random_id = " << random_id;
  if (random_id != 0) {
    // request has already been sent before
    auto it = imported_contacts_.find(random_id);
    CHECK(it != imported_contacts_.end());
    auto result = std::move(it->second);
    imported_contacts_.erase(it);

    promise.set_value(Unit());
    return result;
  }
  for (auto &contact : contacts) {
    if (contact == nullptr) {
      promise.set_error(Status::Error(400, "Imported contacts should not be empty"));
      return {};
    }
  }

  do {
    random_id = Random::secure_int64();
  } while (random_id == 0 || imported_contacts_.find(random_id) != imported_contacts_.end());
  imported_contacts_[random_id];  // reserve place for result

  td_->create_handler<ImportContactsQuery>(std::move(promise))
      ->send(transform(contacts,
                       [](const tl_object_ptr<td_api::contact> &contact) {
                         return Contact(contact->phone_number_, contact->first_name_, contact->last_name_, string(), 0);
                       }),
             random_id);
  return {};
}

void ContactsManager::remove_contacts(vector<UserId> user_ids, Promise<Unit> &&promise) {
  LOG(INFO) << "Delete contacts: " << format::as_array(user_ids);
  if (!are_contacts_loaded_) {
    load_contacts(std::move(promise));
    return;
  }

  vector<UserId> to_delete_user_ids;
  vector<tl_object_ptr<telegram_api::InputUser>> input_users;
  for (auto &user_id : user_ids) {
    const User *u = get_user(user_id);
    if (u != nullptr && u->outbound == LinkState::Contact) {
      auto input_user = get_input_user(user_id);
      if (input_user != nullptr) {
        to_delete_user_ids.push_back(user_id);
        input_users.push_back(std::move(input_user));
      }
    }
  }

  if (input_users.empty()) {
    return promise.set_value(Unit());
  }

  // TODO DeleteContactQuery
  td_->create_handler<DeleteContactsQuery>(std::move(promise))
      ->send(std::move(to_delete_user_ids), std::move(input_users));
}

int32 ContactsManager::get_imported_contact_count(Promise<Unit> &&promise) {
  LOG(INFO) << "Get imported contact count";

  if (!are_contacts_loaded_ || saved_contact_count_ == -1) {
    load_contacts(std::move(promise));
    return 0;
  }
  reload_contacts(false);

  promise.set_value(Unit());
  return saved_contact_count_;
}

void ContactsManager::load_imported_contacts(Promise<Unit> &&promise) {
  if (td_->auth_manager_->is_bot()) {
    are_imported_contacts_loaded_ = true;
  }
  if (are_imported_contacts_loaded_) {
    LOG(INFO) << "Imported contacts are already loaded";
    promise.set_value(Unit());
    return;
  }
  load_imported_contacts_queries_.push_back(std::move(promise));
  if (load_imported_contacts_queries_.size() == 1u) {
    if (G()->parameters().use_chat_info_db) {
      LOG(INFO) << "Load imported contacts from database";
      G()->td_db()->get_sqlite_pmc()->get(
          "user_imported_contacts", PromiseCreator::lambda([](string value) {
            send_closure_later(G()->contacts_manager(), &ContactsManager::on_load_imported_contacts_from_database,
                               std::move(value));
          }));
    } else {
      LOG(INFO) << "Have no previously imported contacts";
      send_closure_later(G()->contacts_manager(), &ContactsManager::on_load_imported_contacts_from_database, string());
    }
  } else {
    LOG(INFO) << "Load imported contacts request has already been sent";
  }
}

void ContactsManager::on_load_imported_contacts_from_database(string value) {
  CHECK(!are_imported_contacts_loaded_);
  if (need_clear_imported_contacts_) {
    need_clear_imported_contacts_ = false;
    value.clear();
  }
  if (value.empty()) {
    CHECK(all_imported_contacts_.empty());
  } else {
    log_event_parse(all_imported_contacts_, value).ensure();
    LOG(INFO) << "Successfully loaded " << all_imported_contacts_.size() << " imported contacts from database";
  }

  load_imported_contact_users_multipromise_.add_promise(PromiseCreator::lambda([](Result<> result) {
    if (result.is_ok()) {
      send_closure_later(G()->contacts_manager(), &ContactsManager::on_load_imported_contacts_finished);
    }
  }));

  auto lock_promise = load_imported_contact_users_multipromise_.get_promise();

  for (const auto &contact : all_imported_contacts_) {
    auto user_id = contact.get_user_id();
    if (user_id.is_valid()) {
      get_user(user_id, 3, load_imported_contact_users_multipromise_.get_promise());
    }
  }

  lock_promise.set_value(Unit());
}

void ContactsManager::on_load_imported_contacts_finished() {
  LOG(INFO) << "Finished to load " << all_imported_contacts_.size() << " imported contacts";

  for (const auto &contact : all_imported_contacts_) {
    get_user_id_object(contact.get_user_id(), "on_load_imported_contacts_finished");  // to ensure updateUser
  }

  if (need_clear_imported_contacts_) {
    need_clear_imported_contacts_ = false;
    all_imported_contacts_.clear();
  }
  are_imported_contacts_loaded_ = true;
  auto promises = std::move(load_imported_contacts_queries_);
  load_imported_contacts_queries_.clear();
  for (auto &promise : promises) {
    promise.set_value(Unit());
  }
}

std::pair<vector<UserId>, vector<int32>> ContactsManager::change_imported_contacts(
    vector<tl_object_ptr<td_api::contact>> &&contacts, int64 &random_id, Promise<Unit> &&promise) {
  if (!are_contacts_loaded_) {
    load_contacts(std::move(promise));
    return {};
  }
  if (!are_imported_contacts_loaded_) {
    load_imported_contacts(std::move(promise));
    return {};
  }

  LOG(INFO) << "Asked to change imported contacts to a list of " << contacts.size()
            << " contacts with random_id = " << random_id;
  if (random_id != 0) {
    // request has already been sent before
    if (need_clear_imported_contacts_) {
      need_clear_imported_contacts_ = false;
      all_imported_contacts_.clear();
      if (G()->parameters().use_chat_info_db) {
        G()->td_db()->get_sqlite_pmc()->erase("user_imported_contacts", Auto());
      }
      reload_contacts(true);
    }

    CHECK(are_imported_contacts_changing_);
    are_imported_contacts_changing_ = false;

    auto unimported_contact_invites = std::move(unimported_contact_invites_);
    unimported_contact_invites_.clear();

    auto imported_contact_user_ids = std::move(imported_contact_user_ids_);
    imported_contact_user_ids_.clear();

    promise.set_value(Unit());
    return {std::move(imported_contact_user_ids), std::move(unimported_contact_invites)};
  }

  if (are_imported_contacts_changing_) {
    promise.set_error(Status::Error(400, "ChangeImportedContacts can be called only once at the same time"));
    return {};
  }

  for (auto &contact : contacts) {
    if (contact == nullptr) {
      promise.set_error(Status::Error(400, "Contacts should not be empty"));
      return {};
    }
  }

  auto new_contacts = transform(std::move(contacts), [](tl_object_ptr<td_api::contact> &&contact) {
    return Contact(std::move(contact->phone_number_), std::move(contact->first_name_), std::move(contact->last_name_),
                   string(), 0);
  });

  vector<size_t> new_contacts_unique_id(new_contacts.size());
  vector<Contact> unique_new_contacts;
  unique_new_contacts.reserve(new_contacts.size());
  std::unordered_map<Contact, size_t, ContactHash, ContactEqual> different_new_contacts;
  std::unordered_set<string> different_new_phone_numbers;
  size_t unique_size = 0;
  for (size_t i = 0; i < new_contacts.size(); i++) {
    auto it_success = different_new_contacts.emplace(std::move(new_contacts[i]), unique_size);
    new_contacts_unique_id[i] = it_success.first->second;
    if (it_success.second) {
      unique_new_contacts.push_back(it_success.first->first);
      different_new_phone_numbers.insert(unique_new_contacts.back().get_phone_number());
      unique_size++;
    }
  }

  vector<UserId> to_delete;
  for (auto &old_contact : all_imported_contacts_) {
    auto user_id = old_contact.get_user_id();
    auto it = different_new_contacts.find(old_contact);
    if (it == different_new_contacts.end()) {
      if (user_id.is_valid() && different_new_phone_numbers.count(old_contact.get_phone_number()) == 0) {
        to_delete.push_back(user_id);
      }
    } else {
      unique_new_contacts[it->second].set_user_id(user_id);
      different_new_contacts.erase(it);
    }
  }
  std::pair<vector<size_t>, vector<Contact>> to_add;
  for (auto &new_contact : different_new_contacts) {
    to_add.first.push_back(new_contact.second);
    to_add.second.push_back(std::move(new_contact.first));
  }

  if (to_add.first.empty() && to_delete.empty()) {
    for (size_t i = 0; i < new_contacts.size(); i++) {
      auto unique_id = new_contacts_unique_id[i];
      new_contacts[i].set_user_id(unique_new_contacts[unique_id].get_user_id());
    }

    promise.set_value(Unit());
    return {transform(new_contacts, [&](const Contact &contact) { return contact.get_user_id(); }),
            vector<int32>(new_contacts.size())};
  }

  are_imported_contacts_changing_ = true;
  random_id = 1;

  remove_contacts(
      std::move(to_delete),
      PromiseCreator::lambda([new_contacts = std::move(unique_new_contacts),
                              new_contacts_unique_id = std::move(new_contacts_unique_id), to_add = std::move(to_add),
                              promise = std::move(promise)](Result<> result) mutable {
        if (result.is_ok()) {
          send_closure_later(G()->contacts_manager(), &ContactsManager::on_clear_imported_contacts,
                             std::move(new_contacts), std::move(new_contacts_unique_id), std::move(to_add),
                             std::move(promise));
        } else {
          promise.set_error(result.move_as_error());
        }
      }));
  return {};
}

void ContactsManager::on_clear_imported_contacts(vector<Contact> &&contacts, vector<size_t> contacts_unique_id,
                                                 std::pair<vector<size_t>, vector<Contact>> &&to_add,
                                                 Promise<Unit> &&promise) {
  LOG(INFO) << "Add " << to_add.first.size() << " contacts";
  next_all_imported_contacts_ = std::move(contacts);
  imported_contacts_unique_id_ = std::move(contacts_unique_id);
  imported_contacts_pos_ = std::move(to_add.first);

  td_->create_handler<ImportContactsQuery>(std::move(promise))->send(std::move(to_add.second), 0);
}

void ContactsManager::clear_imported_contacts(Promise<Unit> &&promise) {
  LOG(INFO) << "Delete imported contacts";

  if (saved_contact_count_ == 0) {
    promise.set_value(Unit());
    return;
  }

  td_->create_handler<ResetContactsQuery>(std::move(promise))->send();
}

void ContactsManager::on_update_contacts_reset() {
  /*
  UserId my_id = get_my_id("on_update_contacts_reset");
  for (auto &p : users_) {
    UserId user_id = p.first;
    User &u = p.second;
    bool is_contact = u.outbound == LinkState::Contact;
    if (is_contact) {
      LOG(INFO) << "Drop contact with " << user_id;
      if (user_id != my_id) {
        CHECK(contacts_hints_.has_key(user_id.get()));
      }
      on_update_user_links(&u, user_id, LinkState::KnowsPhoneNumber, u.inbound);
      update_user(&u, user_id);
      CHECK(u.outbound != LinkState::Contact);
      if (user_id != my_id) {
        CHECK(!contacts_hints_.has_key(user_id.get()));
      }
    }
  }
  */

  saved_contact_count_ = 0;
  if (G()->parameters().use_chat_info_db) {
    G()->td_db()->get_binlog_pmc()->set("saved_contact_count", "0");
    G()->td_db()->get_sqlite_pmc()->erase("user_imported_contacts", Auto());
  }
  if (!are_imported_contacts_loaded_) {
    CHECK(all_imported_contacts_.empty());
    if (load_imported_contacts_queries_.empty()) {
      LOG(INFO) << "Imported contacts was never loaded, just clear them";
    } else {
      LOG(INFO) << "Imported contacts are being loaded, clear them also when they will be loaded";
      need_clear_imported_contacts_ = true;
    }
  } else {
    if (!are_imported_contacts_changing_) {
      LOG(INFO) << "Imported contacts was loaded, but aren't changing now, just clear them";
      all_imported_contacts_.clear();
    } else {
      LOG(INFO) << "Imported contacts are changing now, clear them also after they will be loaded";
      need_clear_imported_contacts_ = true;
    }
  }
  reload_contacts(true);
}

std::pair<int32, vector<UserId>> ContactsManager::search_contacts(const string &query, int32 limit,
                                                                  Promise<Unit> &&promise) {
  LOG(INFO) << "Search contacts with query = \"" << query << "\" and limit = " << limit;

  if (limit < 0) {
    promise.set_error(Status::Error(400, "Limit must be non-negative"));
    return {};
  }

  if (!are_contacts_loaded_) {
    load_contacts(std::move(promise));
    return {};
  }
  reload_contacts(false);

  std::pair<size_t, vector<int64>> result;
  if (query.empty()) {
    result = contacts_hints_.search_empty(limit);
  } else {
    result = contacts_hints_.search(query, limit);
  }

  vector<UserId> user_ids;
  user_ids.reserve(result.second.size());
  for (auto key : result.second) {
    user_ids.emplace_back(narrow_cast<int32>(key));
  }

  promise.set_value(Unit());
  return {narrow_cast<int32>(result.first), std::move(user_ids)};
}

void ContactsManager::set_profile_photo(const tl_object_ptr<td_api::InputFile> &input_photo, Promise<Unit> &&promise) {
  auto r_file_id = td_->file_manager_->get_input_file_id(FileType::Photo, input_photo,
                                                         DialogId(get_my_id("set_profile_photo")), false, false);
  if (r_file_id.is_error()) {
    // TODO promise.set_error(std::move(status));
    return promise.set_error(Status::Error(7, r_file_id.error().message()));
  }
  FileId file_id = r_file_id.ok();
  CHECK(file_id.is_valid());

  FileView file_view = td_->file_manager_->get_file_view(file_id);
  CHECK(!file_view.is_encrypted());
  if (file_view.has_remote_location() && !file_view.remote_location().is_web()) {
    td_->create_handler<UpdateProfilePhotoQuery>(std::move(promise))
        ->send(file_view.remote_location().as_input_photo());
    return;
  }

  auto upload_file_id = td_->file_manager_->dup_file_id(file_id);
  CHECK(upload_file_id.is_valid());
  CHECK(uploaded_profile_photos_.find(upload_file_id) == uploaded_profile_photos_.end());
  uploaded_profile_photos_.emplace(upload_file_id, std::move(promise));
  LOG(INFO) << "Ask to upload profile photo " << upload_file_id;
  td_->file_manager_->upload(upload_file_id, upload_profile_photo_callback_, 1, 0);
}

void ContactsManager::delete_profile_photo(int64 profile_photo_id, Promise<Unit> &&promise) {
  const User *u = get_user(get_my_id("delete_profile_photo"));
  if (u != nullptr && u->photo.id == profile_photo_id) {
    td_->create_handler<UpdateProfilePhotoQuery>(std::move(promise))
        ->send(make_tl_object<telegram_api::inputPhotoEmpty>());
    return;
  }

  td_->create_handler<DeleteProfilePhotoQuery>(std::move(promise))->send(profile_photo_id);
}

void ContactsManager::set_name(const string &first_name, const string &last_name, Promise<Unit> &&promise) {
  auto new_first_name = clean_name(first_name, MAX_NAME_LENGTH);
  auto new_last_name = clean_name(last_name, MAX_NAME_LENGTH);
  if (new_first_name.empty()) {
    return promise.set_error(Status::Error(7, "First name must be non-empty"));
  }

  const User *u = get_user(get_my_id("set_name"));
  int32 flags = 0;
  // TODO we can already send request for changing first_name and last_name and wanting to set initial values
  // TODO need to be rewritten using invoke after and cancelling previous request
  if (u == nullptr || u->first_name != new_first_name) {
    flags |= ACCOUNT_UPDATE_FIRST_NAME;
  }
  if (u == nullptr || u->last_name != new_last_name) {
    flags |= ACCOUNT_UPDATE_LAST_NAME;
  }
  if (flags == 0) {
    return promise.set_value(Unit());
  }

  td_->create_handler<UpdateProfileQuery>(std::move(promise))->send(flags, new_first_name, new_last_name, "");
}

void ContactsManager::set_bio(const string &bio, Promise<Unit> &&promise) {
  auto new_bio = strip_empty_characters(bio, MAX_BIO_LENGTH);
  for (auto &c : new_bio) {
    if (c == '\n') {
      c = ' ';
    }
  }

  const UserFull *user_full = get_user_full(get_my_id("set_bio"));
  int32 flags = 0;
  // TODO we can already send request for changing bio and wanting to set initial values
  // TODO need to be rewritten using invoke after and cancelling previous request
  if (user_full == nullptr || user_full->about != new_bio) {
    flags |= ACCOUNT_UPDATE_ABOUT;
  }
  if (flags == 0) {
    return promise.set_value(Unit());
  }

  td_->create_handler<UpdateProfileQuery>(std::move(promise))->send(flags, "", "", new_bio);
}

void ContactsManager::on_update_profile_success(int32 flags, const string &first_name, const string &last_name,
                                                const string &about) {
  CHECK(flags != 0);

  auto my_user_id = get_my_id("on_update_profile_success");
  const User *u = get_user(my_user_id);
  if (u == nullptr) {
    LOG(ERROR) << "Doesn't receive info about me during update profile";
    return;
  }
  LOG_IF(ERROR, (flags & ACCOUNT_UPDATE_FIRST_NAME) != 0 && u->first_name != first_name)
      << "Wrong first name \"" << u->first_name << "\", expected \"" << first_name << '"';
  LOG_IF(ERROR, (flags & ACCOUNT_UPDATE_LAST_NAME) != 0 && u->last_name != last_name)
      << "Wrong last name \"" << u->last_name << "\", expected \"" << last_name << '"';

  if ((flags & ACCOUNT_UPDATE_ABOUT) != 0) {
    UserFull *user_full = get_user_full(my_user_id);
    if (user_full != nullptr && user_full->is_inited) {
      user_full->about = about;
      user_full->is_changed = true;
      update_user_full(user_full, my_user_id);
    }
  }
}

void ContactsManager::set_username(const string &username, Promise<Unit> &&promise) {
  if (!username.empty() && !is_valid_username(username)) {
    return promise.set_error(Status::Error(400, "Username is invalid"));
  }
  td_->create_handler<UpdateUsernameQuery>(std::move(promise))->send(username);
}

void ContactsManager::toggle_chat_administrators(ChatId chat_id, bool everyone_is_administrator,
                                                 Promise<Unit> &&promise) {
  auto c = get_chat(chat_id);
  if (c == nullptr) {
    return promise.set_error(Status::Error(6, "Group not found"));
  }
  if (!get_chat_status(c).is_creator()) {
    return promise.set_error(Status::Error(6, "Not enough rights to toggle basic group administrators"));
  }

  td_->create_handler<ToggleChatAdminsQuery>(std::move(promise))->send(chat_id, everyone_is_administrator);
}

void ContactsManager::set_channel_username(ChannelId channel_id, const string &username, Promise<Unit> &&promise) {
  auto c = get_channel(channel_id);
  if (c == nullptr) {
    return promise.set_error(Status::Error(6, "Supergroup not found"));
  }
  if (!get_channel_status(c).is_creator()) {
    return promise.set_error(Status::Error(6, "Not enough rights to change supergroup username"));
  }

  if (!username.empty() && !is_valid_username(username)) {
    return promise.set_error(Status::Error(400, "Username is invalid"));
  }

  if (!username.empty() && c->username.empty()) {
    auto channel_full = get_channel_full(channel_id);
    if (channel_full != nullptr && !channel_full->can_set_username) {
      return promise.set_error(Status::Error(3, "Can't set supergroup username"));
    }
  }

  td_->create_handler<UpdateChannelUsernameQuery>(std::move(promise))->send(channel_id, username);
}

void ContactsManager::set_channel_sticker_set(ChannelId channel_id, int64 sticker_set_id, Promise<Unit> &&promise) {
  auto c = get_channel(channel_id);
  if (c == nullptr) {
    return promise.set_error(Status::Error(6, "Supergroup not found"));
  }
  if (!c->is_megagroup) {
    return promise.set_error(Status::Error(6, "Chat sticker set can be set only for supergroups"));
  }
  if (!get_channel_status(c).can_change_info_and_settings()) {
    return promise.set_error(Status::Error(6, "Not enough rights to change supergroup sticker set"));
  }

  telegram_api::object_ptr<telegram_api::InputStickerSet> input_sticker_set;
  if (sticker_set_id == 0) {
    input_sticker_set = telegram_api::make_object<telegram_api::inputStickerSetEmpty>();
  } else {
    input_sticker_set = td_->stickers_manager_->get_input_sticker_set(sticker_set_id);
    if (input_sticker_set == nullptr) {
      return promise.set_error(Status::Error(3, "Sticker set not found"));
    }
  }

  auto channel_full = get_channel_full(channel_id);
  if (channel_full != nullptr && !channel_full->can_set_sticker_set) {
    return promise.set_error(Status::Error(3, "Can't set supergroup sticker set"));
  }

  td_->create_handler<SetChannelStickerSetQuery>(std::move(promise))
      ->send(channel_id, sticker_set_id, std::move(input_sticker_set));
}

void ContactsManager::toggle_channel_invites(ChannelId channel_id, bool anyone_can_invite, Promise<Unit> &&promise) {
  auto c = get_channel(channel_id);
  if (c == nullptr) {
    return promise.set_error(Status::Error(6, "Supergroup not found"));
  }
  if (!get_channel_status(c).can_change_info_and_settings()) {
    return promise.set_error(Status::Error(6, "Not enough rights to toggle supergroup invites"));
  }
  if (get_channel_type(c) != ChannelType::Megagroup) {
    return promise.set_error(Status::Error(6, "Invites by any member can be enabled in the supergroups only"));
  }

  td_->create_handler<ToggleChannelInvitesQuery>(std::move(promise))->send(channel_id, anyone_can_invite);
}

void ContactsManager::toggle_channel_sign_messages(ChannelId channel_id, bool sign_messages, Promise<Unit> &&promise) {
  auto c = get_channel(channel_id);
  if (c == nullptr) {
    return promise.set_error(Status::Error(6, "Supergroup not found"));
  }
  if (get_channel_type(c) == ChannelType::Megagroup) {
    return promise.set_error(Status::Error(6, "Message signatures can't be toggled in supergroups"));
  }
  if (!get_channel_status(c).can_change_info_and_settings()) {
    return promise.set_error(Status::Error(6, "Not enough rights to toggle channel sign messages"));
  }

  td_->create_handler<ToggleChannelSignaturesQuery>(std::move(promise))->send(channel_id, sign_messages);
}

void ContactsManager::toggle_channel_is_all_history_available(ChannelId channel_id, bool is_all_history_available,
                                                              Promise<Unit> &&promise) {
  auto c = get_channel(channel_id);
  if (c == nullptr) {
    return promise.set_error(Status::Error(6, "Supergroup not found"));
  }
  if (!get_channel_status(c).can_change_info_and_settings()) {
    return promise.set_error(Status::Error(6, "Not enough rights to toggle all supergroup history availability"));
  }
  if (get_channel_type(c) != ChannelType::Megagroup) {
    return promise.set_error(Status::Error(6, "Message history can be hidden in the supergroups only"));
  }

  td_->create_handler<ToggleChannelIsAllHistoryAvailableQuery>(std::move(promise))
      ->send(channel_id, is_all_history_available);
}

void ContactsManager::set_channel_description(ChannelId channel_id, const string &description,
                                              Promise<Unit> &&promise) {
  auto new_description = strip_empty_characters(description, MAX_NAME_LENGTH);
  auto c = get_channel(channel_id);
  if (c == nullptr) {
    return promise.set_error(Status::Error(6, "Supergroup not found"));
  }
  if (!get_channel_status(c).can_change_info_and_settings()) {
    return promise.set_error(Status::Error(6, "Not enough rights to set supergroup description"));
  }

  td_->create_handler<EditChannelAboutQuery>(std::move(promise))->send(channel_id, new_description);
}

void ContactsManager::pin_channel_message(ChannelId channel_id, MessageId message_id, bool disable_notification,
                                          Promise<Unit> &&promise) {
  auto c = get_channel(channel_id);
  if (c == nullptr) {
    return promise.set_error(Status::Error(6, "Supergroup not found"));
  }
  auto channel_status = get_channel_status(c);
  bool can_pin = c->is_megagroup ? channel_status.can_pin_messages() : channel_status.can_edit_messages();
  if (!can_pin) {
    return promise.set_error(Status::Error(6, "Not enough rights to pin a message"));
  }

  if (!td_->messages_manager_->have_message({DialogId(channel_id), message_id})) {
    return promise.set_error(Status::Error(6, "Message not found"));
  }

  if (!message_id.is_server()) {
    return promise.set_error(Status::Error(6, "Message can't be pinned"));
  }

  td_->create_handler<UpdateChannelPinnedMessageQuery>(std::move(promise))
      ->send(channel_id, message_id, disable_notification);
}

void ContactsManager::unpin_channel_message(ChannelId channel_id, Promise<Unit> &&promise) {
  auto c = get_channel(channel_id);
  if (c == nullptr) {
    return promise.set_error(Status::Error(6, "Supergroup not found"));
  }
  auto channel_status = get_channel_status(c);
  bool can_unpin = c->is_megagroup ? channel_status.can_pin_messages() : channel_status.can_edit_messages();
  if (!can_unpin) {
    return promise.set_error(Status::Error(6, "Not enough rights to unpin a message"));
  }

  td_->create_handler<UpdateChannelPinnedMessageQuery>(std::move(promise))->send(channel_id, MessageId(), false);
}

void ContactsManager::report_channel_spam(ChannelId channel_id, UserId user_id, const vector<MessageId> &message_ids,
                                          Promise<Unit> &&promise) {
  auto c = get_channel(channel_id);
  if (c == nullptr) {
    return promise.set_error(Status::Error(6, "Supergroup not found"));
  }
  if (!c->is_megagroup) {
    return promise.set_error(Status::Error(6, "Spam can be reported only in supergroups"));
  }

  if (!have_input_user(user_id)) {
    return promise.set_error(Status::Error(6, "Have no access to the user"));
  }
  if (user_id == get_my_id("report_channel_spam")) {
    return promise.set_error(Status::Error(6, "Can't report self"));
  }

  if (message_ids.empty()) {
    return promise.set_error(Status::Error(6, "Message list is empty"));
  }

  vector<MessageId> server_message_ids;
  for (auto &message_id : message_ids) {
    if (!message_id.is_valid()) {
      return promise.set_error(Status::Error(6, "Message not found"));
    }

    if (message_id.is_server()) {
      server_message_ids.push_back(message_id);
    }
  }
  if (server_message_ids.empty()) {
    return promise.set_value(Unit());
  }

  td_->create_handler<ReportChannelSpamQuery>(std::move(promise))->send(channel_id, user_id, server_message_ids);
}

void ContactsManager::delete_channel(ChannelId channel_id, Promise<Unit> &&promise) {
  auto c = get_channel(channel_id);
  if (c == nullptr) {
    return promise.set_error(Status::Error(6, "Supergroup not found"));
  }
  if (!get_channel_status(c).is_creator()) {
    return promise.set_error(Status::Error(6, "Not enough rights to delete the supergroup"));
  }

  td_->create_handler<DeleteChannelQuery>(std::move(promise))->send(channel_id);
}

void ContactsManager::add_chat_participant(ChatId chat_id, UserId user_id, int32 forward_limit,
                                           Promise<Unit> &&promise) {
  const Chat *c = get_chat(chat_id);
  if (c == nullptr) {
    return promise.set_error(Status::Error(3, "Chat info not found"));
  }
  if (!c->is_active) {
    return promise.set_error(Status::Error(3, "Chat is deactivated"));
  }
  if (forward_limit < 0) {
    return promise.set_error(Status::Error(3, "Can't forward negative number of messages"));
  }
  if (user_id != get_my_id("add_chat_participant")) {
    if (!get_chat_status(c).can_invite_users()) {
      return promise.set_error(Status::Error(3, "Not enough rights to invite members to the group chat"));
    }
  } else if (c->kicked) {
    return promise.set_error(Status::Error(3, "User was kicked from the chat"));
  }
  // TODO upper bound on forward_limit

  auto input_user = get_input_user(user_id);
  if (input_user == nullptr) {
    return promise.set_error(Status::Error(3, "User not found"));
  }

  // TODO invoke after
  td_->create_handler<AddChatUserQuery>(std::move(promise))->send(chat_id, std::move(input_user), forward_limit);
}

void ContactsManager::add_channel_participant(ChannelId channel_id, UserId user_id, Promise<Unit> &&promise,
                                              DialogParticipantStatus old_status) {
  if (td_->auth_manager_->is_bot()) {
    return promise.set_error(Status::Error(400, "Bots can't add new chat members"));
  }

  const Channel *c = get_channel(channel_id);
  if (c == nullptr) {
    return promise.set_error(Status::Error(3, "Chat info not found"));
  }
  auto input_user = get_input_user(user_id);
  if (input_user == nullptr) {
    return promise.set_error(Status::Error(3, "User not found"));
  }

  if (user_id == get_my_id("add_channel_participant")) {
    // join the channel
    if (get_channel_status(c).is_banned()) {
      return promise.set_error(Status::Error(3, "Can't return to kicked from chat"));
    }

    td_->create_handler<JoinChannelQuery>(std::move(promise))->send(channel_id);
    return;
  }

  if (!(c->anyone_can_invite && get_channel_status(c).is_member()) && !get_channel_status(c).can_invite_users()) {
    return promise.set_error(Status::Error(3, "Not enough rights to invite members to the supergroup chat"));
  }

  speculative_add_channel_users(channel_id, DialogParticipantStatus::Member(), old_status);
  vector<tl_object_ptr<telegram_api::InputUser>> input_users;
  input_users.push_back(std::move(input_user));
  td_->create_handler<InviteToChannelQuery>(std::move(promise))->send(channel_id, std::move(input_users));
}

void ContactsManager::add_channel_participants(ChannelId channel_id, const vector<UserId> &user_ids,
                                               Promise<Unit> &&promise) {
  if (td_->auth_manager_->is_bot()) {
    return promise.set_error(Status::Error(400, "Bots can't add new chat members"));
  }

  const Channel *c = get_channel(channel_id);
  if (c == nullptr) {
    return promise.set_error(Status::Error(3, "Chat info not found"));
  }

  if (!(c->anyone_can_invite && get_channel_status(c).is_member()) && !get_channel_status(c).can_invite_users()) {
    return promise.set_error(Status::Error(3, "Not enough rights to invite members to the supergroup chat"));
  }

  vector<tl_object_ptr<telegram_api::InputUser>> input_users;
  for (auto user_id : user_ids) {
    auto input_user = get_input_user(user_id);
    if (input_user == nullptr) {
      return promise.set_error(Status::Error(3, "User not found"));
    }

    if (user_id == get_my_id("add_channel_participants")) {
      // can't invite self
      continue;
    }
    input_users.push_back(std::move(input_user));
  }

  if (input_users.empty()) {
    return promise.set_value(Unit());
  }

  td_->create_handler<InviteToChannelQuery>(std::move(promise))->send(channel_id, std::move(input_users));
}

void ContactsManager::change_channel_participant_status(ChannelId channel_id, UserId user_id,
                                                        DialogParticipantStatus status, Promise<Unit> &&promise) {
  auto c = get_channel(channel_id);
  if (c == nullptr) {
    return promise.set_error(Status::Error(6, "Chat info not found"));
  }

  auto input_user = get_input_user(user_id);
  if (input_user == nullptr) {
    return promise.set_error(Status::Error(6, "User not found"));
  }

  if (user_id == get_my_id("change_channel_participant_status")) {
    // fast path is needed, because get_channel_status may return Creator, while GetChannelParticipantQuery returning Left
    return change_channel_participant_status_impl(channel_id, user_id, std::move(status), get_channel_status(c),
                                                  std::move(promise));
  }

  auto on_result_promise =
      PromiseCreator::lambda([actor_id = actor_id(this), channel_id, user_id, status,
                              promise = std::move(promise)](Result<DialogParticipant> r_dialog_participant) mutable {
        // ResultHandlers are cleared before managers, so it is safe to capture this
        if (r_dialog_participant.is_error()) {
          return promise.set_error(r_dialog_participant.move_as_error());
        }

        send_closure(actor_id, &ContactsManager::change_channel_participant_status_impl, channel_id, user_id,
                     std::move(status), r_dialog_participant.ok().status, std::move(promise));
      });

  td_->create_handler<GetChannelParticipantQuery>(std::move(on_result_promise))
      ->send(channel_id, user_id, std::move(input_user));
}

void ContactsManager::change_channel_participant_status_impl(ChannelId channel_id, UserId user_id,
                                                             DialogParticipantStatus status,
                                                             DialogParticipantStatus old_status,
                                                             Promise<Unit> &&promise) {
  if (old_status == status) {
    return promise.set_value(Unit());
  }

  LOG(INFO) << "Change status of " << user_id << " in " << channel_id << " from " << old_status << " to " << status;
  bool need_add = false;
  bool need_promote = false;
  bool need_restrict = false;
  if (status.is_creator() || old_status.is_creator()) {
    if (!old_status.is_creator()) {
      return promise.set_error(Status::Error(3, "Can't add creator to the chat"));
    }
    if (status.is_member()) {
      // creator member -> not creator member
      // creator not member -> creator member
      // creator not member -> not creator member
      if (old_status.is_member()) {
        return promise.set_error(Status::Error(3, "Can't demote chat creator"));
      }
      need_add = true;
    } else {
      // creator member -> creator not member
      // creator member -> not creator not member
      // creator not member -> not creator not member
      if (!old_status.is_member()) {
        return promise.set_error(Status::Error(3, "Can't restrict chat creator"));
      }
      need_restrict = true;
    }
  } else if (status.is_administrator()) {
    need_promote = true;
  } else if (!status.is_member() || status.is_restricted()) {
    need_restrict = true;
  } else {
    // regular member
    if (old_status.is_administrator()) {
      need_promote = true;
    } else if (old_status.is_restricted() || old_status.is_banned()) {
      need_restrict = true;
    } else {
      CHECK(!old_status.is_member());
      need_add = true;
    }
  }

  if (need_promote) {
    return promote_channel_participant(channel_id, user_id, std::move(status), std::move(old_status),
                                       std::move(promise));
  } else if (need_restrict) {
    return restrict_channel_participant(channel_id, user_id, std::move(status), std::move(old_status),
                                        std::move(promise));
  } else {
    CHECK(need_add);
    return add_channel_participant(channel_id, user_id, std::move(promise), std::move(old_status));
  }
}

void ContactsManager::promote_channel_participant(ChannelId channel_id, UserId user_id, DialogParticipantStatus status,
                                                  DialogParticipantStatus old_status, Promise<Unit> &&promise) {
  LOG(INFO) << "Promote " << user_id << " in " << channel_id << " from " << old_status << " to " << status;
  const Channel *c = get_channel(channel_id);
  CHECK(c != nullptr);

  if (user_id == get_my_id("change_channel_participant_status")) {
    if (status.is_administrator()) {
      return promise.set_error(Status::Error(3, "Can't promote self"));
    }
    CHECK(status.is_member());
    // allow to demote self. TODO is it allowed server-side?
  } else {
    if (!get_channel_status(c).can_promote_members()) {
      return promise.set_error(Status::Error(3, "Not enough rights"));
    }
  }

  auto input_user = get_input_user(user_id);
  if (input_user == nullptr) {
    return promise.set_error(Status::Error(3, "User not found"));
  }

  speculative_add_channel_users(channel_id, status, old_status);
  td_->create_handler<EditChannelAdminQuery>(std::move(promise))->send(channel_id, std::move(input_user), status);
}

void ContactsManager::change_chat_participant_status(ChatId chat_id, UserId user_id, DialogParticipantStatus status,
                                                     Promise<Unit> &&promise) {
  if (!status.is_member()) {
    return delete_chat_participant(chat_id, user_id, std::move(promise));
  }

  auto c = get_chat(chat_id);
  if (c == nullptr) {
    return promise.set_error(Status::Error(6, "Chat info not found"));
  }

  if (!get_chat_status(c).can_promote_members()) {
    return promise.set_error(Status::Error(3, "Need creator rights in the group chat"));
  }

  if (c->everyone_is_administrator) {
    return promise.set_error(Status::Error(3, "Administrators editing is disabled in the group chat"));
  }

  if (user_id == get_my_id("change_chat_participant_status")) {
    return promise.set_error(Status::Error(3, "Can't change chat member status of self"));
  }

  auto input_user = get_input_user(user_id);
  if (input_user == nullptr) {
    return promise.set_error(Status::Error(3, "User not found"));
  }

  if (status.is_creator()) {
    return promise.set_error(Status::Error(3, "Can't add creator to the group chat"));
  }
  if (status.is_restricted()) {
    return promise.set_error(Status::Error(3, "Can't restrict users in a basic group chat"));
  }

  td_->create_handler<EditChatAdminQuery>(std::move(promise))
      ->send(chat_id, std::move(input_user), status.is_administrator());
}

void ContactsManager::export_chat_invite_link(ChatId chat_id, Promise<Unit> &&promise) {
  const Chat *c = get_chat(chat_id);
  if (c == nullptr) {
    return promise.set_error(Status::Error(3, "Chat info not found"));
  }
  if (!c->is_active) {
    return promise.set_error(Status::Error(3, "Chat is deactivated"));
  }

  if (!get_chat_status(c).can_export_dialog_invite_link()) {
    return promise.set_error(Status::Error(3, "Not enough rights to export chat invite link"));
  }

  td_->create_handler<ExportChatInviteLinkQuery>(std::move(promise))->send(chat_id);
}

void ContactsManager::export_channel_invite_link(ChannelId channel_id, Promise<Unit> &&promise) {
  const Channel *c = get_channel(channel_id);
  if (c == nullptr) {
    return promise.set_error(Status::Error(3, "Chat info not found"));
  }

  if (!get_channel_status(c).can_export_dialog_invite_link()) {
    return promise.set_error(Status::Error(3, "Not enough rights to export chat invite link"));
  }

  td_->create_handler<ExportChannelInviteLinkQuery>(std::move(promise))->send(channel_id);
}

void ContactsManager::check_dialog_invite_link(const string &invite_link, Promise<Unit> &&promise) const {
  auto it = invite_link_infos_.find(invite_link);
  if (it != invite_link_infos_.end()) {
    return promise.set_value(Unit());
  }

  if (!is_valid_invite_link(invite_link)) {
    return promise.set_error(Status::Error(3, "Wrong invite link"));
  }

  td_->create_handler<CheckDialogInviteLinkQuery>(std::move(promise))->send(invite_link);
}

void ContactsManager::import_dialog_invite_link(const string &invite_link, Promise<DialogId> &&promise) {
  if (!is_valid_invite_link(invite_link)) {
    return promise.set_error(Status::Error(3, "Wrong invite link"));
  }

  td_->create_handler<ImportDialogInviteLinkQuery>(std::move(promise))->send(invite_link);
}

string ContactsManager::get_chat_invite_link(ChatId chat_id) const {
  auto chat_full = get_chat_full(chat_id);
  if (chat_full == nullptr) {
    auto it = chat_invite_links_.find(chat_id);
    return it == chat_invite_links_.end() ? string() : it->second;
  }
  return chat_full->invite_link;
}

string ContactsManager::get_channel_invite_link(
    ChannelId channel_id) {  // should be non-const to update ChannelFull cache
  auto channel_full = get_channel_full(channel_id);
  if (channel_full == nullptr) {
    auto it = channel_invite_links_.find(channel_id);
    return it == channel_invite_links_.end() ? string() : it->second;
  }
  return channel_full->invite_link;
}

MessageId ContactsManager::get_channel_pinned_message_id(
    ChannelId channel_id) {  // should be non-const to update ChannelFull cache
  auto channel_full = get_channel_full(channel_id);
  if (channel_full == nullptr) {
    return MessageId();
  }
  return channel_full->pinned_message_id;
}

void ContactsManager::delete_chat_participant(ChatId chat_id, UserId user_id, Promise<Unit> &&promise) {
  const Chat *c = get_chat(chat_id);
  if (c == nullptr) {
    return promise.set_error(Status::Error(3, "Chat info not found"));
  }
  if (!c->is_active) {
    return promise.set_error(Status::Error(3, "Chat is deactivated"));
  }
  auto my_id = get_my_id("delete_chat_participant");
  if (c->left) {
    if (user_id == my_id) {
      return promise.set_value(Unit());
    } else {
      return promise.set_error(Status::Error(3, "Not in the chat"));
    }
  }
  if (user_id != my_id) {
    auto my_status = get_chat_status(c);
    if (!my_status.is_creator()) {  // creator can delete anyone
      auto participant = get_chat_participant(chat_id, user_id);
      if (participant != nullptr) {  // if have no information about participant, just send request to the server
        if (c->everyone_is_administrator) {
          // if all are administrators, only invited by me participants can be deleted
          if (participant->inviter_user_id != my_id) {
            return promise.set_error(Status::Error(3, "Need to be inviter of a user to kick it from a basic group"));
          }
        } else {
          // otherwise, only creator can kick administrators
          if (participant->status.is_administrator()) {
            return promise.set_error(
                Status::Error(3, "Only the creator of a basic group can kick group administrators"));
          }
          // regular users can be kicked by administrators and their inviters
          if (!my_status.is_administrator() && participant->inviter_user_id != my_id) {
            return promise.set_error(Status::Error(3, "Need to be inviter of a user to kick it from a basic group"));
          }
        }
      }
    }
  }
  auto input_user = get_input_user(user_id);
  if (input_user == nullptr) {
    return promise.set_error(Status::Error(3, "User not found"));
  }

  // TODO invoke after
  td_->create_handler<DeleteChatUserQuery>(std::move(promise))->send(chat_id, std::move(input_user));
}

void ContactsManager::restrict_channel_participant(ChannelId channel_id, UserId user_id, DialogParticipantStatus status,
                                                   DialogParticipantStatus old_status, Promise<Unit> &&promise) {
  LOG(INFO) << "Restrict " << user_id << " in " << channel_id << " from " << old_status << " to " << status;
  const Channel *c = get_channel(channel_id);
  if (c == nullptr) {
    return promise.set_error(Status::Error(3, "Chat info not found"));
  }
  if (!c->status.is_member()) {
    if (user_id == get_my_id("restrict_channel_participant")) {
      if (status.is_member()) {
        return promise.set_error(Status::Error(3, "Can't unrestrict self"));
      }
      return promise.set_value(Unit());
    } else {
      return promise.set_error(Status::Error(3, "Not in the chat"));
    }
  }
  auto input_user = get_input_user(user_id);
  if (input_user == nullptr) {
    return promise.set_error(Status::Error(3, "User not found"));
  }

  if (user_id == get_my_id("restrict_channel_participant")) {
    if (status.is_restricted() || status.is_banned()) {
      return promise.set_error(Status::Error(3, "Can't restrict self"));
    }
    if (status.is_member()) {
      return promise.set_error(Status::Error(3, "Can't unrestrict self"));
    }

    // leave the channel
    td_->create_handler<LeaveChannelQuery>(std::move(promise))->send(channel_id);
    return;
  }

  if (status.is_creator()) {
    return promise.set_error(Status::Error(3, "Not enough rights to restrict chat creator"));
  }

  if (!get_channel_status(c).can_restrict_members()) {
    return promise.set_error(Status::Error(3, "Not enough rights to restrict/unrestrict chat member"));
  }

  if (old_status.is_member() && !status.is_member() && !status.is_banned()) {
    // we can't make participant Left without kicking it first
    auto on_result_promise = PromiseCreator::lambda([channel_id, user_id, status,
                                                     promise = std::move(promise)](Result<> result) mutable {
      if (result.is_error()) {
        return promise.set_error(result.move_as_error());
      }

      create_actor<SleepActor>(
          "RestrictChannelParticipantSleepActor", 1.0,
          PromiseCreator::lambda([channel_id, user_id, status, promise = std::move(promise)](Result<> result) mutable {
            if (result.is_error()) {
              return promise.set_error(result.move_as_error());
            }

            send_closure(G()->contacts_manager(), &ContactsManager::restrict_channel_participant, channel_id, user_id,
                         status, DialogParticipantStatus::Banned(0), std::move(promise));
          }))
          .release();
    });

    promise = std::move(on_result_promise);
    status = DialogParticipantStatus::Banned(0);
  }

  speculative_add_channel_users(channel_id, status, old_status);
  td_->create_handler<EditChannelBannedQuery>(std::move(promise))->send(channel_id, std::move(input_user), status);
}

ChannelId ContactsManager::migrate_chat_to_megagroup(ChatId chat_id, Promise<Unit> &promise) {
  auto c = get_chat(chat_id);
  if (c == nullptr) {
    promise.set_error(Status::Error(3, "Chat info not found"));
    return ChannelId();
  }

  if (!c->is_creator) {
    promise.set_error(Status::Error(3, "Need creator rights in the chat"));
    return ChannelId();
  }

  if (c->migrated_to_channel_id.is_valid()) {
    return c->migrated_to_channel_id;
  }

  td_->create_handler<MigrateChatQuery>(std::move(promise))->send(chat_id);
  return ChannelId();
}

vector<DialogId> ContactsManager::get_created_public_dialogs(Promise<Unit> &&promise) {
  if (created_public_channels_inited_) {
    promise.set_value(Unit());
    return transform(created_public_channels_, [&](ChannelId channel_id) {
      DialogId dialog_id(channel_id);
      td_->messages_manager_->force_create_dialog(dialog_id, "get_created_public_dialogs");
      return dialog_id;
    });
  }

  td_->create_handler<GetCreatedPublicChannelsQuery>(std::move(promise))->send();
  return {};
}

void ContactsManager::on_get_created_public_channels(vector<tl_object_ptr<telegram_api::Chat>> &&chats) {
  created_public_channels_inited_ = true;
  created_public_channels_.clear();

  for (auto &chat : chats) {
    switch (chat->get_id()) {
      case telegram_api::chatEmpty::ID:
        LOG(ERROR) << "Receive chatEmpty as created public channel";
        break;
      case telegram_api::chat::ID:
        LOG(ERROR) << "Receive chat as created public channel";
        break;
      case telegram_api::chatForbidden::ID:
        LOG(ERROR) << "Receive chatForbidden as created public channel";
        break;
      case telegram_api::channel::ID: {
        auto c = static_cast<const telegram_api::channel *>(chat.get());
        ChannelId channel_id(c->id_);
        if (!channel_id.is_valid()) {
          LOG(ERROR) << "Receive invalid " << channel_id;
          continue;
        }
        created_public_channels_.push_back(channel_id);
        break;
      }
      case telegram_api::channelForbidden::ID: {
        auto c = static_cast<const telegram_api::channelForbidden *>(chat.get());
        ChannelId channel_id(c->id_);
        if (!channel_id.is_valid()) {
          LOG(ERROR) << "Receive invalid " << channel_id;
          continue;
        }
        created_public_channels_.push_back(channel_id);
        break;
      }
      default:
        UNREACHABLE();
    }
    on_get_chat(std::move(chat));
  }
}

void ContactsManager::on_imported_contacts(int64 random_id, vector<UserId> imported_contact_user_ids,
                                           vector<int32> unimported_contact_invites) {
  LOG(INFO) << "Contacts import with random_id " << random_id
            << " has finished: " << format::as_array(imported_contact_user_ids);
  if (random_id == 0) {
    // import from change_imported_contacts
    all_imported_contacts_ = std::move(next_all_imported_contacts_);
    next_all_imported_contacts_.clear();

    auto result_size = imported_contacts_unique_id_.size();
    auto unique_size = all_imported_contacts_.size();
    auto add_size = imported_contacts_pos_.size();

    imported_contact_user_ids_.resize(result_size);
    unimported_contact_invites_.resize(result_size);

    CHECK(imported_contact_user_ids.size() == add_size);
    CHECK(unimported_contact_invites.size() == add_size);
    CHECK(imported_contacts_unique_id_.size() == result_size);

    std::unordered_map<size_t, int32> unique_id_to_unimported_contact_invites;
    for (size_t i = 0; i < add_size; i++) {
      auto unique_id = imported_contacts_pos_[i];
      get_user_id_object(imported_contact_user_ids[i], "on_imported_contacts");  // to ensure updateUser
      all_imported_contacts_[unique_id].set_user_id(imported_contact_user_ids[i]);
      unique_id_to_unimported_contact_invites[unique_id] = unimported_contact_invites[i];
    }

    if (G()->parameters().use_chat_info_db) {
      G()->td_db()->get_binlog()->force_sync(PromiseCreator::lambda(
          [log_event = log_event_store(all_imported_contacts_).as_slice().str()](Result<> result) mutable {
            if (result.is_ok()) {
              LOG(INFO) << "Save imported contacts to database";
              G()->td_db()->get_sqlite_pmc()->set("user_imported_contacts", std::move(log_event), Auto());
            }
          }));
    }

    for (size_t i = 0; i < result_size; i++) {
      auto unique_id = imported_contacts_unique_id_[i];
      CHECK(unique_id < unique_size);
      imported_contact_user_ids_[i] = all_imported_contacts_[unique_id].get_user_id();
      auto it = unique_id_to_unimported_contact_invites.find(unique_id);
      if (it == unique_id_to_unimported_contact_invites.end()) {
        unimported_contact_invites_[i] = 0;
      } else {
        unimported_contact_invites_[i] = it->second;
      }
    }
    return;
  }

  auto it = imported_contacts_.find(random_id);
  CHECK(it != imported_contacts_.end());
  CHECK(it->second.first.empty());
  CHECK(it->second.second.empty());
  imported_contacts_[random_id] = {std::move(imported_contact_user_ids), std::move(unimported_contact_invites)};
}

void ContactsManager::on_deleted_contacts(const vector<UserId> &deleted_contact_user_ids) {
  LOG(INFO) << "Contacts deletion has finished";

  for (auto user_id : deleted_contact_user_ids) {
    LOG(INFO) << "Drop contact with " << user_id;
    auto u = get_user(user_id);
    CHECK(u != nullptr);
    on_update_user_links(u, user_id, LinkState::KnowsPhoneNumber, u->inbound);
    update_user(u, user_id);
    CHECK(u->outbound != LinkState::Contact);
    CHECK(!contacts_hints_.has_key(user_id.get()));
  }
}

void ContactsManager::save_next_contacts_sync_date() {
  if (!G()->parameters().use_chat_info_db) {
    return;
  }
  G()->td_db()->get_binlog_pmc()->set("next_contacts_sync_date", to_string(next_contacts_sync_date_));
}

void ContactsManager::on_get_contacts(tl_object_ptr<telegram_api::contacts_Contacts> &&new_contacts) {
  next_contacts_sync_date_ = G()->unix_time() + Random::fast(70000, 100000);

  CHECK(new_contacts != nullptr);
  if (new_contacts->get_id() == telegram_api::contacts_contactsNotModified::ID) {
    if (saved_contact_count_ == -1) {
      saved_contact_count_ = 0;
    }
    on_get_contacts_finished(contacts_hints_.size());
    td_->create_handler<GetContactsStatusesQuery>()->send();
    return;
  }

  auto contacts = move_tl_object_as<telegram_api::contacts_contacts>(new_contacts);
  std::unordered_set<UserId, UserIdHash> contact_user_ids;
  for (auto &user : contacts->users_) {
    auto user_id = get_user_id(user);
    if (!user_id.is_valid()) {
      LOG(ERROR) << "Receive invalid " << user_id;
      continue;
    }
    contact_user_ids.insert(user_id);
  }
  on_get_users(std::move(contacts->users_));

  UserId my_id = get_my_id("on_get_contacts");
  for (auto &p : users_) {
    UserId user_id = p.first;
    User &u = p.second;
    bool is_contact = u.outbound == LinkState::Contact;
    bool should_be_contact = contact_user_ids.count(user_id) == 1;
    if (is_contact != should_be_contact) {
      if (is_contact) {
        LOG(INFO) << "Drop contact with " << user_id;
        if (user_id != my_id) {
          CHECK(contacts_hints_.has_key(user_id.get()));
        }
        on_update_user_links(&u, user_id, LinkState::KnowsPhoneNumber, u.inbound);
        update_user(&u, user_id);
        CHECK(u.outbound != LinkState::Contact);
        if (user_id != my_id) {
          CHECK(!contacts_hints_.has_key(user_id.get()));
        }
      } else {
        LOG(ERROR) << "Receive non-contact " << user_id << " in the list of contacts";
      }
    }
  }

  saved_contact_count_ = contacts->saved_count_;
  on_get_contacts_finished(std::numeric_limits<size_t>::max());
}

void ContactsManager::save_contacts_to_database() {
  if (!G()->parameters().use_chat_info_db || !are_contacts_loaded_) {
    return;
  }

  LOG(INFO) << "Schedule save contacts to database";
  vector<UserId> user_ids =
      transform(contacts_hints_.search_empty(100000).second, [](int64 key) { return UserId(narrow_cast<int32>(key)); });

  G()->td_db()->get_binlog_pmc()->set("saved_contact_count", to_string(saved_contact_count_));
  G()->td_db()->get_binlog()->force_sync(PromiseCreator::lambda([user_ids = std::move(user_ids)](Result<> result) {
    if (result.is_ok()) {
      LOG(INFO) << "Save contacts to database";
      G()->td_db()->get_sqlite_pmc()->set(
          "user_contacts", log_event_store(user_ids).as_slice().str(), PromiseCreator::lambda([](Result<> result) {
            if (result.is_ok()) {
              send_closure(G()->contacts_manager(), &ContactsManager::save_next_contacts_sync_date);
            }
          }));
    }
  }));
}

void ContactsManager::on_get_contacts_failed(Status error) {
  CHECK(error.is_error());
  next_contacts_sync_date_ = G()->unix_time() + Random::fast(5, 10);
  auto promises = std::move(load_contacts_queries_);
  load_contacts_queries_.clear();
  for (auto &promise : promises) {
    promise.set_error(error.clone());
  }
}

void ContactsManager::on_load_contacts_from_database(string value) {
  if (value.empty()) {
    reload_contacts(true);
    return;
  }

  vector<UserId> user_ids;
  log_event_parse(user_ids, value).ensure();

  LOG(INFO) << "Successfully loaded " << user_ids.size() << " contacts from database";

  load_contact_users_multipromise_.add_promise(
      PromiseCreator::lambda([expected_contact_count = user_ids.size()](Result<> result) {
        if (result.is_ok()) {
          send_closure(G()->contacts_manager(), &ContactsManager::on_get_contacts_finished, expected_contact_count);
        }
      }));

  auto lock_promise = load_contact_users_multipromise_.get_promise();

  for (auto user_id : user_ids) {
    get_user(user_id, 3, load_contact_users_multipromise_.get_promise());
  }

  lock_promise.set_value(Unit());
}

void ContactsManager::on_get_contacts_finished(size_t expected_contact_count) {
  LOG(INFO) << "Finished to get " << contacts_hints_.size() << " contacts out of " << expected_contact_count;
  are_contacts_loaded_ = true;
  auto promises = std::move(load_contacts_queries_);
  load_contacts_queries_.clear();
  for (auto &promise : promises) {
    promise.set_value(Unit());
  }
  if (expected_contact_count != contacts_hints_.size()) {
    save_contacts_to_database();
  }
}

void ContactsManager::on_get_contacts_statuses(vector<tl_object_ptr<telegram_api::contactStatus>> &&statuses) {
  for (auto &status : statuses) {
    on_update_user_online(UserId(status->user_id_), std::move(status->status_));
  }
  save_next_contacts_sync_date();
}

void ContactsManager::on_update_online_status_privacy() {
  td_->create_handler<GetContactsStatusesQuery>()->send();
}

void ContactsManager::on_get_contacts_link(tl_object_ptr<telegram_api::contacts_link> &&link) {
  UserId user_id = get_user_id(link->user_);
  on_get_user(std::move(link->user_));
  on_update_user_links(user_id, std::move(link->my_link_), std::move(link->foreign_link_));
}

UserId ContactsManager::get_user_id(const tl_object_ptr<telegram_api::User> &user) {
  CHECK(user != nullptr);
  switch (user->get_id()) {
    case telegram_api::userEmpty::ID:
      return UserId(static_cast<const telegram_api::userEmpty *>(user.get())->id_);
    case telegram_api::user::ID:
      return UserId(static_cast<const telegram_api::user *>(user.get())->id_);
    default:
      UNREACHABLE();
      return UserId();
  }
}

ChatId ContactsManager::get_chat_id(const tl_object_ptr<telegram_api::Chat> &chat) {
  CHECK(chat != nullptr);
  switch (chat->get_id()) {
    case telegram_api::chatEmpty::ID:
      return ChatId(static_cast<const telegram_api::chatEmpty *>(chat.get())->id_);
    case telegram_api::chat::ID:
      return ChatId(static_cast<const telegram_api::chat *>(chat.get())->id_);
    case telegram_api::chatForbidden::ID:
      return ChatId(static_cast<const telegram_api::chatForbidden *>(chat.get())->id_);
    default:
      return ChatId();
  }
}

ChannelId ContactsManager::get_channel_id(const tl_object_ptr<telegram_api::Chat> &chat) {
  CHECK(chat != nullptr);
  switch (chat->get_id()) {
    case telegram_api::channel::ID:
      return ChannelId(static_cast<const telegram_api::channel *>(chat.get())->id_);
    case telegram_api::channelForbidden::ID:
      return ChannelId(static_cast<const telegram_api::channelForbidden *>(chat.get())->id_);
    default:
      return ChannelId();
  }
}

void ContactsManager::on_get_user(tl_object_ptr<telegram_api::User> &&user_ptr, bool is_me, bool is_support) {
  LOG(DEBUG) << "Receive " << to_string(user_ptr);
  int32 constructor_id = user_ptr->get_id();
  if (constructor_id == telegram_api::userEmpty::ID) {
    auto user = move_tl_object_as<telegram_api::userEmpty>(user_ptr);
    UserId user_id(user->id_);
    if (!user_id.is_valid()) {
      LOG(ERROR) << "Receive invalid " << user_id;
      return;
    }
    LOG(INFO) << "Receive empty " << user_id;

    User *u = get_user_force(user_id);
    if (u == nullptr) {
      LOG(ERROR) << "Have no information about " << user_id << ", but received userEmpty";
    }
    return;
  }

  CHECK(constructor_id == telegram_api::user::ID);
  auto user = move_tl_object_as<telegram_api::user>(user_ptr);
  UserId user_id(user->id_);
  if (!user_id.is_valid()) {
    LOG(ERROR) << "Receive invalid " << user_id;
    return;
  }
  int32 flags = user->flags_;
  LOG(INFO) << "Receive " << user_id << " with flags " << flags;
  if (is_me && (flags & USER_FLAG_IS_ME) == 0) {
    LOG(ERROR) << user_id << " doesn't have flag IS_ME, but must have it";
    flags |= USER_FLAG_IS_ME;
  }

  bool is_bot = (flags & USER_FLAG_IS_BOT) != 0;
  if (flags & USER_FLAG_IS_ME) {
    set_my_id(user_id);
    td_->auth_manager_->set_is_bot(is_bot);
    if (!is_bot) {
      G()->shared_config().set_option_string("my_phone_number", user->phone_);
    }
  } else {
    /*
    if (!(flags & USER_FLAG_HAS_ACCESS_HASH) && !(flags & USER_FLAG_IS_DELETED) &&
        !(flags & USER_FLAG_IS_INACCESSIBLE)) {
      LOG(ERROR) << user_id << " has no access_hash";
      return;
    }
    */
  }

  if (is_support) {
    support_user_id_ = user_id;
  }

  bool have_access_hash = (flags & USER_FLAG_HAS_ACCESS_HASH) != 0;
  bool is_received = (flags & USER_FLAG_IS_INACCESSIBLE) == 0;

  User *u = add_user(user_id, "on_get_user");
  if ((have_access_hash || u->access_hash == -1) && u->access_hash != user->access_hash_) {
    LOG(DEBUG) << "Access hash has changed for " << user_id << " from " << u->access_hash << " to "
               << user->access_hash_;
    u->access_hash = user->access_hash_;
    u->is_changed = true;
  }
  if (is_received) {
    on_update_user_phone_number(u, user_id, std::move(user->phone_));
  }
  on_update_user_photo(u, user_id, std::move(user->photo_));
  if (is_received) {
    on_update_user_online(u, user_id, std::move(user->status_));
  }

  LinkState out, in;
  if (flags & USER_FLAG_IS_MUTUAL_CONTACT) {
    out = LinkState::Contact;
    in = LinkState::Contact;
  } else if (flags & USER_FLAG_IS_CONTACT) {
    out = LinkState::Contact;
    in = LinkState::Unknown;
  } else if (flags & USER_FLAG_HAS_PHONE_NUMBER) {
    out = LinkState::KnowsPhoneNumber;
    in = LinkState::Unknown;
  } else {
    out = LinkState::None;
    in = LinkState::Unknown;
  }
  on_update_user_links(u, user_id, out, in);

  if (is_received || !u->is_received) {
    on_update_user_name(u, user_id, std::move(user->first_name_), std::move(user->last_name_),
                        std::move(user->username_));
  }

  bool is_verified = (flags & USER_FLAG_IS_VERIFIED) != 0;
  bool is_deleted = (flags & USER_FLAG_IS_DELETED) != 0;
  bool can_join_groups = (flags & USER_FLAG_IS_PRIVATE_BOT) == 0;
  bool can_read_all_group_messages = (flags & USER_FLAG_IS_BOT_WITH_PRIVACY_DISABLED) != 0;
  string restriction_reason = std::move(user->restriction_reason_);
  bool is_inline_bot = (flags & USER_FLAG_IS_INLINE_BOT) != 0;
  string inline_query_placeholder = user->bot_inline_placeholder_;
  bool need_location_bot = (flags & USER_FLAG_NEED_LOCATION_BOT) != 0;
  bool has_bot_info_version = (flags & USER_FLAG_HAS_BOT_INFO_VERSION) != 0;

  LOG_IF(ERROR, !can_join_groups && !is_bot) << "Receive not bot which can't join groups";
  LOG_IF(ERROR, can_read_all_group_messages && !is_bot) << "Receive not bot which can read all group messages";
  LOG_IF(ERROR, is_inline_bot && !is_bot) << "Receive not bot which is inline bot";
  LOG_IF(ERROR, need_location_bot && !is_inline_bot) << "Receive not inline bot which needs user location";

  if (is_received && !u->is_received) {
    u->is_received = true;

    LOG(DEBUG) << "Receive " << user_id;
    u->need_send_update = true;
  }

  if (is_deleted) {
    // just in case
    is_verified = false;
    is_bot = false;
    can_join_groups = false;
    can_read_all_group_messages = false;
    is_inline_bot = false;
    inline_query_placeholder = string();
    need_location_bot = false;
    has_bot_info_version = false;
  }

  LOG_IF(ERROR, has_bot_info_version && !is_bot) << "Receive not bot which has bot info version";

  int32 bot_info_version = has_bot_info_version ? user->bot_info_version_ : -1;
  if (is_verified != u->is_verified || is_bot != u->is_bot || can_join_groups != u->can_join_groups ||
      can_read_all_group_messages != u->can_read_all_group_messages || restriction_reason != u->restriction_reason ||
      is_inline_bot != u->is_inline_bot || inline_query_placeholder != u->inline_query_placeholder ||
      need_location_bot != u->need_location_bot) {
    LOG_IF(ERROR, is_bot != u->is_bot && !is_deleted && !u->is_deleted) << "User.is_bot has changed";
    u->is_verified = is_verified;
    u->is_bot = is_bot;
    u->can_join_groups = can_join_groups;
    u->can_read_all_group_messages = can_read_all_group_messages;
    u->restriction_reason = std::move(restriction_reason);
    u->is_inline_bot = is_inline_bot;
    u->inline_query_placeholder = std::move(inline_query_placeholder);
    u->need_location_bot = need_location_bot;

    LOG(DEBUG) << "Info has changed for " << user_id;
    u->need_send_update = true;
  }

  if (u->bot_info_version != bot_info_version) {
    u->bot_info_version = bot_info_version;
    LOG(DEBUG) << "Bot info version has changed for " << user_id;
    u->is_changed = true;
  }

  if (is_deleted != u->is_deleted) {
    u->is_deleted = is_deleted;

    if (u->is_deleted) {
      invalidate_user_full(user_id);
    }

    LOG(DEBUG) << "is_deleted has changed for " << user_id;
    u->need_send_update = true;
  }

  bool has_language_code = (flags & USER_FLAG_HAS_LANGUAGE_CODE) != 0;
  LOG_IF(ERROR, has_language_code && !td_->auth_manager_->is_bot()) << "Receive language code";
  if (u->language_code != user->lang_code_ && !user->lang_code_.empty()) {
    u->language_code = user->lang_code_;

    LOG(DEBUG) << "Language code has changed for " << user_id;
    u->need_send_update = true;
  }

  update_user(u, user_id);
}

class ContactsManager::UserLogEvent {
 public:
  UserId user_id;
  User u;

  UserLogEvent() = default;

  UserLogEvent(UserId user_id, const User &u) : user_id(user_id), u(u) {
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    td::store(user_id, storer);
    td::store(u, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    td::parse(user_id, parser);
    td::parse(u, parser);
  }
};

void ContactsManager::save_user(User *u, UserId user_id, bool from_binlog) {
  if (!G()->parameters().use_chat_info_db) {
    return;
  }
  CHECK(u != nullptr);
  if (!u->is_saved || !u->is_status_saved) {  // TODO more effective handling of !u->is_status_saved
    if (!from_binlog) {
      auto logevent = UserLogEvent(user_id, *u);
      auto storer = LogEventStorerImpl<UserLogEvent>(logevent);
      if (u->logevent_id == 0) {
        u->logevent_id = binlog_add(G()->td_db()->get_binlog(), LogEvent::HandlerType::Users, storer);
      } else {
        binlog_rewrite(G()->td_db()->get_binlog(), u->logevent_id, LogEvent::HandlerType::Users, storer);
      }
    }

    save_user_to_database(u, user_id);
  }
}

void ContactsManager::on_binlog_user_event(BinlogEvent &&event) {
  if (!G()->parameters().use_chat_info_db) {
    binlog_erase(G()->td_db()->get_binlog(), event.id_);
    return;
  }

  UserLogEvent log_event;
  log_event_parse(log_event, event.data_).ensure();

  auto user_id = log_event.user_id;
  LOG(INFO) << "Add " << user_id << " from binlog";
  User *u = add_user(user_id, "on_binlog_user_event");
  if (!(u->first_name.empty() && u->last_name.empty()) && Slice(u->debug_source) == Slice("on_binlog_user_event")) {
    LOG(ERROR) << "Skip adding already added " << user_id;
    binlog_erase(G()->td_db()->get_binlog(), event.id_);
    return;  // TODO fix bug in Binlog and remove that fix
  }
  CHECK(u->first_name.empty() && u->last_name.empty()) << user_id << " " << u->debug_source;
  *u = std::move(log_event.u);  // users come from binlog before all other events, so just add them

  u->logevent_id = event.id_;

  update_user(u, user_id, true, false);
}

string ContactsManager::get_user_database_key(UserId user_id) {
  return PSTRING() << "us" << user_id.get();
}

string ContactsManager::get_user_database_value(const User *u) {
  return log_event_store(*u).as_slice().str();
}

void ContactsManager::save_user_to_database(User *u, UserId user_id) {
  CHECK(u != nullptr);
  if (u->is_being_saved) {
    return;
  }
  if (loaded_from_database_users_.count(user_id)) {
    save_user_to_database_impl(u, user_id, get_user_database_value(u));
    return;
  }
  if (load_user_from_database_queries_.count(user_id) != 0) {
    return;
  }

  load_user_from_database_impl(user_id, Auto());
}

void ContactsManager::save_user_to_database_impl(User *u, UserId user_id, string value) {
  CHECK(u != nullptr);
  CHECK(load_user_from_database_queries_.count(user_id) == 0);
  CHECK(!u->is_being_saved);
  u->is_being_saved = true;
  u->is_saved = true;
  u->is_status_saved = true;
  LOG(INFO) << "Trying to save to database " << user_id;
  G()->td_db()->get_sqlite_pmc()->set(
      get_user_database_key(user_id), std::move(value), PromiseCreator::lambda([user_id](Result<> result) {
        send_closure(G()->contacts_manager(), &ContactsManager::on_save_user_to_database, user_id, result.is_ok());
      }));
}

void ContactsManager::on_save_user_to_database(UserId user_id, bool success) {
  User *u = get_user(user_id);
  CHECK(u != nullptr);
  CHECK(u->is_being_saved) << user_id << " " << u->is_saved << " " << u->is_status_saved << " "
                           << load_user_from_database_queries_.count(user_id) << " " << u->is_received << " "
                           << u->is_deleted << " " << u->is_bot << " " << u->is_changed << " " << u->need_send_update
                           << " " << u->is_status_changed << " " << u->is_name_changed << " " << u->is_username_changed
                           << " " << u->is_photo_changed << " " << u->is_outbound_link_changed;
  CHECK(load_user_from_database_queries_.count(user_id) == 0);
  u->is_being_saved = false;

  if (!success) {
    LOG(ERROR) << "Failed to save " << user_id << " to database";
    u->is_saved = false;
    u->is_status_saved = false;
  } else {
    LOG(INFO) << "Successfully saved " << user_id << " to database";
  }
  if (u->is_saved && u->is_status_saved) {
    if (u->logevent_id != 0) {
      binlog_erase(G()->td_db()->get_binlog(), u->logevent_id);
      u->logevent_id = 0;
    }
  } else {
    save_user(u, user_id, u->logevent_id != 0);
  }
}

void ContactsManager::load_user_from_database(User *u, UserId user_id, Promise<Unit> promise) {
  if (loaded_from_database_users_.count(user_id)) {
    promise.set_value(Unit());
    return;
  }

  CHECK(u == nullptr || !u->is_being_saved);
  load_user_from_database_impl(user_id, std::move(promise));
}

void ContactsManager::load_user_from_database_impl(UserId user_id, Promise<Unit> promise) {
  LOG(INFO) << "Load " << user_id << " from database";
  auto &load_user_queries = load_user_from_database_queries_[user_id];
  load_user_queries.push_back(std::move(promise));
  if (load_user_queries.size() == 1u) {
    G()->td_db()->get_sqlite_pmc()->get(get_user_database_key(user_id), PromiseCreator::lambda([user_id](string value) {
                                          send_closure(G()->contacts_manager(),
                                                       &ContactsManager::on_load_user_from_database, user_id,
                                                       std::move(value));
                                        }));
  }
}

void ContactsManager::on_load_user_from_database(UserId user_id, string value) {
  if (!loaded_from_database_users_.insert(user_id).second) {
    return;
  }

  auto it = load_user_from_database_queries_.find(user_id);
  vector<Promise<Unit>> promises;
  if (it != load_user_from_database_queries_.end()) {
    promises = std::move(it->second);
    CHECK(!promises.empty());
    load_user_from_database_queries_.erase(it);
  }

  LOG(INFO) << "Successfully loaded " << user_id << " of size " << value.size() << " from database";
  //  G()->td_db()->get_sqlite_pmc()->erase(get_user_database_key(user_id), Auto());
  //  return;

  User *u = get_user(user_id);
  if (u == nullptr) {
    if (!value.empty()) {
      u = add_user(user_id, "on_load_user_from_database");

      log_event_parse(*u, value).ensure();

      u->is_saved = true;
      u->is_status_saved = true;
      update_user(u, user_id, true, true);
    }
  } else {
    CHECK(!u->is_saved);  // user can't be saved before load completes
    CHECK(!u->is_being_saved);
    auto new_value = get_user_database_value(u);
    if (value != new_value) {
      save_user_to_database_impl(u, user_id, std::move(new_value));
    } else if (u->logevent_id != 0) {
      binlog_erase(G()->td_db()->get_binlog(), u->logevent_id);
      u->logevent_id = 0;
    }
  }

  for (auto &promise : promises) {
    promise.set_value(Unit());
  }
}

bool ContactsManager::have_user_force(UserId user_id) {
  return get_user_force(user_id) != nullptr;
}

ContactsManager::User *ContactsManager::get_user_force(UserId user_id) {
  auto u = get_user_force_impl(user_id);
  if (user_id == UserId(777000) && (u == nullptr || !u->is_received)) {
    int32 flags = telegram_api::user::ACCESS_HASH_MASK | telegram_api::user::FIRST_NAME_MASK |
                  telegram_api::user::PHONE_MASK | telegram_api::user::PHOTO_MASK | telegram_api::user::VERIFIED_MASK;
    auto profile_photo = telegram_api::make_object<telegram_api::userProfilePhoto>(
        3337190045231018,
        telegram_api::make_object<telegram_api::fileLocation>(1, 702229962, 26779, 5859320227133863146),
        telegram_api::make_object<telegram_api::fileLocation>(1, 702229962, 26781, -3695031185685824216));
    if (G()->is_test_dc()) {
      profile_photo = nullptr;
      flags -= telegram_api::user::PHOTO_MASK;
    }

    auto user = telegram_api::make_object<telegram_api::user>(
        flags, false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/,
        false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/,
        false /*ignored*/, 777000, 1, "Telegram", "Updates", string(), "42777", std::move(profile_photo), nullptr, 0,
        string(), string(), string());
    on_get_user(std::move(user));
    u = get_user(user_id);
    CHECK(u != nullptr && u->is_received);
  }
  return u;
}

ContactsManager::User *ContactsManager::get_user_force_impl(UserId user_id) {
  if (!user_id.is_valid()) {
    return nullptr;
  }

  User *u = get_user(user_id);
  if (u != nullptr) {
    return u;
  }
  if (!G()->parameters().use_chat_info_db) {
    return nullptr;
  }
  if (loaded_from_database_users_.count(user_id)) {
    return nullptr;
  }

  LOG(INFO) << "Try load " << user_id << " from database";
  on_load_user_from_database(user_id, G()->td_db()->get_sqlite_sync_pmc()->get(get_user_database_key(user_id)));
  return get_user(user_id);
}

class ContactsManager::ChatLogEvent {
 public:
  ChatId chat_id;
  Chat c;

  ChatLogEvent() = default;

  ChatLogEvent(ChatId chat_id, const Chat &c) : chat_id(chat_id), c(c) {
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    td::store(chat_id, storer);
    td::store(c, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    td::parse(chat_id, parser);
    td::parse(c, parser);
  }
};

void ContactsManager::save_chat(Chat *c, ChatId chat_id, bool from_binlog) {
  if (!G()->parameters().use_chat_info_db) {
    return;
  }
  CHECK(c != nullptr);
  if (!c->is_saved) {
    if (!from_binlog) {
      auto logevent = ChatLogEvent(chat_id, *c);
      auto storer = LogEventStorerImpl<ChatLogEvent>(logevent);
      if (c->logevent_id == 0) {
        c->logevent_id = binlog_add(G()->td_db()->get_binlog(), LogEvent::HandlerType::Chats, storer);
      } else {
        binlog_rewrite(G()->td_db()->get_binlog(), c->logevent_id, LogEvent::HandlerType::Chats, storer);
      }
    }

    save_chat_to_database(c, chat_id);
    return;
  }
}

void ContactsManager::on_binlog_chat_event(BinlogEvent &&event) {
  if (!G()->parameters().use_chat_info_db) {
    binlog_erase(G()->td_db()->get_binlog(), event.id_);
    return;
  }

  ChatLogEvent log_event;
  log_event_parse(log_event, event.data_).ensure();

  auto chat_id = log_event.chat_id;
  LOG(INFO) << "Add " << chat_id << " from binlog";
  Chat *c = add_chat(chat_id);
  CHECK(!c->left && c->kicked);
  *c = std::move(log_event.c);  // chats come from binlog before all other events, so just add them

  c->logevent_id = event.id_;

  update_chat(c, chat_id, true, false);
}

string ContactsManager::get_chat_database_key(ChatId chat_id) {
  return PSTRING() << "gr" << chat_id.get();
}

string ContactsManager::get_chat_database_value(const Chat *c) {
  return log_event_store(*c).as_slice().str();
}

void ContactsManager::save_chat_to_database(Chat *c, ChatId chat_id) {
  CHECK(c != nullptr);
  if (c->is_being_saved) {
    return;
  }
  if (loaded_from_database_chats_.count(chat_id)) {
    save_chat_to_database_impl(c, chat_id, get_chat_database_value(c));
    return;
  }
  if (load_chat_from_database_queries_.count(chat_id) != 0) {
    return;
  }

  load_chat_from_database_impl(chat_id, Auto());
}

void ContactsManager::save_chat_to_database_impl(Chat *c, ChatId chat_id, string value) {
  CHECK(c != nullptr);
  CHECK(load_chat_from_database_queries_.count(chat_id) == 0);
  c->is_being_saved = true;
  c->is_saved = true;
  LOG(INFO) << "Trying to save to database " << chat_id;
  G()->td_db()->get_sqlite_pmc()->set(
      get_chat_database_key(chat_id), std::move(value), PromiseCreator::lambda([chat_id](Result<> result) {
        send_closure(G()->contacts_manager(), &ContactsManager::on_save_chat_to_database, chat_id, result.is_ok());
      }));
}

void ContactsManager::on_save_chat_to_database(ChatId chat_id, bool success) {
  Chat *c = get_chat(chat_id);
  CHECK(c != nullptr);
  CHECK(c->is_being_saved);
  CHECK(load_chat_from_database_queries_.count(chat_id) == 0);
  c->is_being_saved = false;

  if (!success) {
    LOG(ERROR) << "Failed to save " << chat_id << " to database";
    c->is_saved = false;
  } else {
    LOG(INFO) << "Successfully saved " << chat_id << " to database";
  }
  if (c->is_saved) {
    if (c->logevent_id != 0) {
      binlog_erase(G()->td_db()->get_binlog(), c->logevent_id);
      c->logevent_id = 0;
    }
  } else {
    save_chat(c, chat_id, c->logevent_id != 0);
  }
}

void ContactsManager::load_chat_from_database(Chat *c, ChatId chat_id, Promise<Unit> promise) {
  if (loaded_from_database_chats_.count(chat_id)) {
    promise.set_value(Unit());
    return;
  }

  CHECK(c == nullptr || !c->is_being_saved);
  load_chat_from_database_impl(chat_id, std::move(promise));
}

void ContactsManager::load_chat_from_database_impl(ChatId chat_id, Promise<Unit> promise) {
  LOG(INFO) << "Load " << chat_id << " from database";
  auto &load_chat_queries = load_chat_from_database_queries_[chat_id];
  load_chat_queries.push_back(std::move(promise));
  if (load_chat_queries.size() == 1u) {
    G()->td_db()->get_sqlite_pmc()->get(get_chat_database_key(chat_id), PromiseCreator::lambda([chat_id](string value) {
                                          send_closure(G()->contacts_manager(),
                                                       &ContactsManager::on_load_chat_from_database, chat_id,
                                                       std::move(value));
                                        }));
  }
}

void ContactsManager::on_load_chat_from_database(ChatId chat_id, string value) {
  if (!loaded_from_database_chats_.insert(chat_id).second) {
    return;
  }

  auto it = load_chat_from_database_queries_.find(chat_id);
  vector<Promise<Unit>> promises;
  if (it != load_chat_from_database_queries_.end()) {
    promises = std::move(it->second);
    CHECK(!promises.empty());
    load_chat_from_database_queries_.erase(it);
  }

  LOG(INFO) << "Successfully loaded " << chat_id << " of size " << value.size() << " from database";
  //  G()->td_db()->get_sqlite_pmc()->erase(get_chat_database_key(chat_id), Auto());
  //  return;

  Chat *c = get_chat(chat_id);
  if (c == nullptr) {
    if (!value.empty()) {
      c = add_chat(chat_id);

      log_event_parse(*c, value).ensure();

      c->is_saved = true;
      update_chat(c, chat_id, true, true);
    }
  } else {
    CHECK(!c->is_saved);  // chat can't be saved before load completes
    CHECK(!c->is_being_saved);
    auto new_value = get_chat_database_value(c);
    if (value != new_value) {
      save_chat_to_database_impl(c, chat_id, std::move(new_value));
    } else if (c->logevent_id != 0) {
      binlog_erase(G()->td_db()->get_binlog(), c->logevent_id);
      c->logevent_id = 0;
    }
  }

  if (c != nullptr && c->migrated_to_channel_id.is_valid() && !have_channel_force(c->migrated_to_channel_id)) {
    LOG(ERROR) << "Can't find " << c->migrated_to_channel_id << " from " << chat_id;
  }

  for (auto &promise : promises) {
    promise.set_value(Unit());
  }
}

bool ContactsManager::have_chat_force(ChatId chat_id) {
  return get_chat_force(chat_id) != nullptr;
}

ContactsManager::Chat *ContactsManager::get_chat_force(ChatId chat_id) {
  if (!chat_id.is_valid()) {
    return nullptr;
  }

  Chat *c = get_chat(chat_id);
  if (c != nullptr) {
    if (c->migrated_to_channel_id.is_valid() && !have_channel_force(c->migrated_to_channel_id)) {
      LOG(ERROR) << "Can't find " << c->migrated_to_channel_id << " from " << chat_id;
    }

    return c;
  }
  if (!G()->parameters().use_chat_info_db) {
    return nullptr;
  }
  if (loaded_from_database_chats_.count(chat_id)) {
    return nullptr;
  }

  LOG(INFO) << "Try load " << chat_id << " from database";
  on_load_chat_from_database(chat_id, G()->td_db()->get_sqlite_sync_pmc()->get(get_chat_database_key(chat_id)));
  return get_chat(chat_id);
}

class ContactsManager::ChannelLogEvent {
 public:
  ChannelId channel_id;
  Channel c;

  ChannelLogEvent() = default;

  ChannelLogEvent(ChannelId channel_id, const Channel &c) : channel_id(channel_id), c(c) {
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    td::store(channel_id, storer);
    td::store(c, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    td::parse(channel_id, parser);
    td::parse(c, parser);
  }
};

void ContactsManager::save_channel(Channel *c, ChannelId channel_id, bool from_binlog) {
  if (!G()->parameters().use_chat_info_db) {
    return;
  }
  CHECK(c != nullptr);
  if (!c->is_saved) {
    if (!from_binlog) {
      auto logevent = ChannelLogEvent(channel_id, *c);
      auto storer = LogEventStorerImpl<ChannelLogEvent>(logevent);
      if (c->logevent_id == 0) {
        c->logevent_id = binlog_add(G()->td_db()->get_binlog(), LogEvent::HandlerType::Channels, storer);
      } else {
        binlog_rewrite(G()->td_db()->get_binlog(), c->logevent_id, LogEvent::HandlerType::Channels, storer);
      }
    }

    save_channel_to_database(c, channel_id);
    return;
  }
}

void ContactsManager::on_binlog_channel_event(BinlogEvent &&event) {
  if (!G()->parameters().use_chat_info_db) {
    binlog_erase(G()->td_db()->get_binlog(), event.id_);
    return;
  }

  ChannelLogEvent log_event;
  log_event_parse(log_event, event.data_).ensure();

  auto channel_id = log_event.channel_id;
  LOG(INFO) << "Add " << channel_id << " from binlog";
  Channel *c = add_channel(channel_id, "on_binlog_channel_event");
  if (!c->status.is_banned() && Slice(c->debug_source) == Slice("on_binlog_channel_event")) {
    LOG(ERROR) << "Skip adding already added " << channel_id;
    binlog_erase(G()->td_db()->get_binlog(), event.id_);
    return;  // TODO fix bug in Binlog and remove that fix
  }
  CHECK(c->status.is_banned()) << channel_id << " " << c->debug_source;
  *c = std::move(log_event.c);  // channels come from binlog before all other events, so just add them

  c->logevent_id = event.id_;

  update_channel(c, channel_id, true, false);
}

string ContactsManager::get_channel_database_key(ChannelId channel_id) {
  return PSTRING() << "ch" << channel_id.get();
}

string ContactsManager::get_channel_database_value(const Channel *c) {
  return log_event_store(*c).as_slice().str();
}

void ContactsManager::save_channel_to_database(Channel *c, ChannelId channel_id) {
  CHECK(c != nullptr);
  if (c->is_being_saved) {
    return;
  }
  if (loaded_from_database_channels_.count(channel_id)) {
    save_channel_to_database_impl(c, channel_id, get_channel_database_value(c));
    return;
  }
  if (load_channel_from_database_queries_.count(channel_id) != 0) {
    return;
  }

  load_channel_from_database_impl(channel_id, Auto());
}

void ContactsManager::save_channel_to_database_impl(Channel *c, ChannelId channel_id, string value) {
  CHECK(c != nullptr);
  CHECK(load_channel_from_database_queries_.count(channel_id) == 0);
  c->is_being_saved = true;
  c->is_saved = true;
  LOG(INFO) << "Trying to save to database " << channel_id;
  G()->td_db()->get_sqlite_pmc()->set(
      get_channel_database_key(channel_id), std::move(value), PromiseCreator::lambda([channel_id](Result<> result) {
        send_closure(G()->contacts_manager(), &ContactsManager::on_save_channel_to_database, channel_id,
                     result.is_ok());
      }));
}

void ContactsManager::on_save_channel_to_database(ChannelId channel_id, bool success) {
  Channel *c = get_channel(channel_id);
  CHECK(c != nullptr);
  CHECK(c->is_being_saved);
  CHECK(load_channel_from_database_queries_.count(channel_id) == 0);
  c->is_being_saved = false;

  if (!success) {
    LOG(ERROR) << "Failed to save " << channel_id << " to database";
    c->is_saved = false;
  } else {
    LOG(INFO) << "Successfully saved " << channel_id << " to database";
  }
  if (c->is_saved) {
    if (c->logevent_id != 0) {
      binlog_erase(G()->td_db()->get_binlog(), c->logevent_id);
      c->logevent_id = 0;
    }
  } else {
    save_channel(c, channel_id, c->logevent_id != 0);
  }
}

void ContactsManager::load_channel_from_database(Channel *c, ChannelId channel_id, Promise<Unit> promise) {
  if (loaded_from_database_channels_.count(channel_id)) {
    promise.set_value(Unit());
    return;
  }

  CHECK(c == nullptr || !c->is_being_saved);
  load_channel_from_database_impl(channel_id, std::move(promise));
}

void ContactsManager::load_channel_from_database_impl(ChannelId channel_id, Promise<Unit> promise) {
  LOG(INFO) << "Load " << channel_id << " from database";
  auto &load_channel_queries = load_channel_from_database_queries_[channel_id];
  load_channel_queries.push_back(std::move(promise));
  if (load_channel_queries.size() == 1u) {
    G()->td_db()->get_sqlite_pmc()->get(
        get_channel_database_key(channel_id), PromiseCreator::lambda([channel_id](string value) {
          send_closure(G()->contacts_manager(), &ContactsManager::on_load_channel_from_database, channel_id,
                       std::move(value));
        }));
  }
}

void ContactsManager::on_load_channel_from_database(ChannelId channel_id, string value) {
  if (!loaded_from_database_channels_.insert(channel_id).second) {
    return;
  }

  auto it = load_channel_from_database_queries_.find(channel_id);
  vector<Promise<Unit>> promises;
  if (it != load_channel_from_database_queries_.end()) {
    promises = std::move(it->second);
    CHECK(!promises.empty());
    load_channel_from_database_queries_.erase(it);
  }

  LOG(INFO) << "Successfully loaded " << channel_id << " of size " << value.size() << " from database";
  //  G()->td_db()->get_sqlite_pmc()->erase(get_channel_database_key(channel_id), Auto());
  //  return;

  Channel *c = get_channel(channel_id);
  if (c == nullptr) {
    if (!value.empty()) {
      c = add_channel(channel_id, "on_load_channel_from_database");

      log_event_parse(*c, value).ensure();

      c->is_saved = true;
      update_channel(c, channel_id, true, true);
    }
  } else {
    CHECK(!c->is_saved);  // channel can't be saved before load completes
    CHECK(!c->is_being_saved);
    auto new_value = get_channel_database_value(c);
    if (value != new_value) {
      save_channel_to_database_impl(c, channel_id, std::move(new_value));
    } else if (c->logevent_id != 0) {
      binlog_erase(G()->td_db()->get_binlog(), c->logevent_id);
      c->logevent_id = 0;
    }
  }

  for (auto &promise : promises) {
    promise.set_value(Unit());
  }
}

bool ContactsManager::have_channel_force(ChannelId channel_id) {
  return get_channel_force(channel_id) != nullptr;
}

ContactsManager::Channel *ContactsManager::get_channel_force(ChannelId channel_id) {
  if (!channel_id.is_valid()) {
    return nullptr;
  }

  Channel *c = get_channel(channel_id);
  if (c != nullptr) {
    return c;
  }
  if (!G()->parameters().use_chat_info_db) {
    return nullptr;
  }
  if (loaded_from_database_channels_.count(channel_id)) {
    return nullptr;
  }

  LOG(INFO) << "Try load " << channel_id << " from database";
  on_load_channel_from_database(channel_id,
                                G()->td_db()->get_sqlite_sync_pmc()->get(get_channel_database_key(channel_id)));
  return get_channel(channel_id);
}

class ContactsManager::SecretChatLogEvent {
 public:
  SecretChatId secret_chat_id;
  SecretChat c;

  SecretChatLogEvent() = default;

  SecretChatLogEvent(SecretChatId secret_chat_id, const SecretChat &c) : secret_chat_id(secret_chat_id), c(c) {
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    td::store(secret_chat_id, storer);
    td::store(c, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    td::parse(secret_chat_id, parser);
    td::parse(c, parser);
  }
};

void ContactsManager::save_secret_chat(SecretChat *c, SecretChatId secret_chat_id, bool from_binlog) {
  if (!G()->parameters().use_chat_info_db) {
    return;
  }
  CHECK(c != nullptr);
  if (!c->is_saved) {
    if (!from_binlog) {
      auto logevent = SecretChatLogEvent(secret_chat_id, *c);
      auto storer = LogEventStorerImpl<SecretChatLogEvent>(logevent);
      if (c->logevent_id == 0) {
        c->logevent_id = binlog_add(G()->td_db()->get_binlog(), LogEvent::HandlerType::SecretChatInfos, storer);
      } else {
        binlog_rewrite(G()->td_db()->get_binlog(), c->logevent_id, LogEvent::HandlerType::SecretChatInfos, storer);
      }
    }

    save_secret_chat_to_database(c, secret_chat_id);
    return;
  }
}

void ContactsManager::on_binlog_secret_chat_event(BinlogEvent &&event) {
  if (!G()->parameters().use_chat_info_db) {
    binlog_erase(G()->td_db()->get_binlog(), event.id_);
    return;
  }

  SecretChatLogEvent log_event;
  log_event_parse(log_event, event.data_).ensure();

  auto secret_chat_id = log_event.secret_chat_id;
  LOG(INFO) << "Add " << secret_chat_id << " from binlog";
  SecretChat *c = add_secret_chat(secret_chat_id);
  CHECK(c->date == 0);
  *c = std::move(log_event.c);  // secret chats come from binlog before all other events, so just add them

  c->logevent_id = event.id_;

  update_secret_chat(c, secret_chat_id, true, false);
}

string ContactsManager::get_secret_chat_database_key(SecretChatId secret_chat_id) {
  return PSTRING() << "sc" << secret_chat_id.get();
}

string ContactsManager::get_secret_chat_database_value(const SecretChat *c) {
  return log_event_store(*c).as_slice().str();
}

void ContactsManager::save_secret_chat_to_database(SecretChat *c, SecretChatId secret_chat_id) {
  CHECK(c != nullptr);
  if (c->is_being_saved) {
    return;
  }
  if (loaded_from_database_secret_chats_.count(secret_chat_id)) {
    save_secret_chat_to_database_impl(c, secret_chat_id, get_secret_chat_database_value(c));
    return;
  }
  if (load_secret_chat_from_database_queries_.count(secret_chat_id) != 0) {
    return;
  }

  load_secret_chat_from_database_impl(secret_chat_id, Auto());
}

void ContactsManager::save_secret_chat_to_database_impl(SecretChat *c, SecretChatId secret_chat_id, string value) {
  CHECK(c != nullptr);
  CHECK(load_secret_chat_from_database_queries_.count(secret_chat_id) == 0);
  c->is_being_saved = true;
  c->is_saved = true;
  LOG(INFO) << "Trying to save to database " << secret_chat_id;
  G()->td_db()->get_sqlite_pmc()->set(get_secret_chat_database_key(secret_chat_id), std::move(value),
                                      PromiseCreator::lambda([secret_chat_id](Result<> result) {
                                        send_closure(G()->contacts_manager(),
                                                     &ContactsManager::on_save_secret_chat_to_database, secret_chat_id,
                                                     result.is_ok());
                                      }));
}

void ContactsManager::on_save_secret_chat_to_database(SecretChatId secret_chat_id, bool success) {
  SecretChat *c = get_secret_chat(secret_chat_id);
  CHECK(c != nullptr);
  CHECK(c->is_being_saved);
  CHECK(load_secret_chat_from_database_queries_.count(secret_chat_id) == 0);
  c->is_being_saved = false;

  if (!success) {
    LOG(ERROR) << "Failed to save " << secret_chat_id << " to database";
    c->is_saved = false;
  } else {
    LOG(INFO) << "Successfully saved " << secret_chat_id << " to database";
  }
  if (c->is_saved) {
    if (c->logevent_id != 0) {
      binlog_erase(G()->td_db()->get_binlog(), c->logevent_id);
      c->logevent_id = 0;
    }
  } else {
    save_secret_chat(c, secret_chat_id, c->logevent_id != 0);
  }
}

void ContactsManager::load_secret_chat_from_database(SecretChat *c, SecretChatId secret_chat_id,
                                                     Promise<Unit> promise) {
  if (loaded_from_database_secret_chats_.count(secret_chat_id)) {
    promise.set_value(Unit());
    return;
  }

  CHECK(c == nullptr || !c->is_being_saved);
  load_secret_chat_from_database_impl(secret_chat_id, std::move(promise));
}

void ContactsManager::load_secret_chat_from_database_impl(SecretChatId secret_chat_id, Promise<Unit> promise) {
  LOG(INFO) << "Load " << secret_chat_id << " from database";
  auto &load_secret_chat_queries = load_secret_chat_from_database_queries_[secret_chat_id];
  load_secret_chat_queries.push_back(std::move(promise));
  if (load_secret_chat_queries.size() == 1u) {
    G()->td_db()->get_sqlite_pmc()->get(
        get_secret_chat_database_key(secret_chat_id), PromiseCreator::lambda([secret_chat_id](string value) {
          send_closure(G()->contacts_manager(), &ContactsManager::on_load_secret_chat_from_database, secret_chat_id,
                       std::move(value));
        }));
  }
}

void ContactsManager::on_load_secret_chat_from_database(SecretChatId secret_chat_id, string value) {
  if (!loaded_from_database_secret_chats_.insert(secret_chat_id).second) {
    return;
  }

  auto it = load_secret_chat_from_database_queries_.find(secret_chat_id);
  vector<Promise<Unit>> promises;
  if (it != load_secret_chat_from_database_queries_.end()) {
    promises = std::move(it->second);
    CHECK(!promises.empty());
    load_secret_chat_from_database_queries_.erase(it);
  }

  LOG(INFO) << "Successfully loaded " << secret_chat_id << " of size " << value.size() << " from database";
  //  G()->td_db()->get_sqlite_pmc()->erase(get_secret_chat_database_key(secret_chat_id), Auto());
  //  return;

  SecretChat *c = get_secret_chat(secret_chat_id);
  if (c == nullptr) {
    if (!value.empty()) {
      c = add_secret_chat(secret_chat_id);

      log_event_parse(*c, value).ensure();

      c->is_saved = true;
      update_secret_chat(c, secret_chat_id, true, true);
    }
  } else {
    CHECK(!c->is_saved);  // secret chat can't be saved before load completes
    CHECK(!c->is_being_saved);
    auto new_value = get_secret_chat_database_value(c);
    if (value != new_value) {
      save_secret_chat_to_database_impl(c, secret_chat_id, std::move(new_value));
    } else if (c->logevent_id != 0) {
      binlog_erase(G()->td_db()->get_binlog(), c->logevent_id);
      c->logevent_id = 0;
    }
  }

  // TODO load users asynchronously
  if (c != nullptr && !have_user_force(c->user_id)) {
    LOG(ERROR) << "Can't find " << c->user_id << " from " << secret_chat_id;
  }

  for (auto &promise : promises) {
    promise.set_value(Unit());
  }
}

bool ContactsManager::have_secret_chat_force(SecretChatId secret_chat_id) {
  return get_secret_chat_force(secret_chat_id) != nullptr;
}

ContactsManager::SecretChat *ContactsManager::get_secret_chat_force(SecretChatId secret_chat_id) {
  if (!secret_chat_id.is_valid()) {
    return nullptr;
  }

  SecretChat *c = get_secret_chat(secret_chat_id);
  if (c != nullptr) {
    if (!have_user_force(c->user_id)) {
      LOG(ERROR) << "Can't find " << c->user_id << " from " << secret_chat_id;
    }
    return c;
  }
  if (!G()->parameters().use_chat_info_db) {
    return nullptr;
  }
  if (loaded_from_database_secret_chats_.count(secret_chat_id)) {
    return nullptr;
  }

  LOG(INFO) << "Try load " << secret_chat_id << " from database";
  on_load_secret_chat_from_database(
      secret_chat_id, G()->td_db()->get_sqlite_sync_pmc()->get(get_secret_chat_database_key(secret_chat_id)));
  return get_secret_chat(secret_chat_id);
}

void ContactsManager::update_user(User *u, UserId user_id, bool from_binlog, bool from_database) {
  CHECK(u != nullptr);
  if (u->is_name_changed || u->is_username_changed || u->is_outbound_link_changed) {
    update_contacts_hints(u, user_id, from_database);
  }
  if (u->is_name_changed) {
    td_->messages_manager_->on_dialog_title_updated(DialogId(user_id));
    auto it = secret_chats_with_user_.find(user_id);
    if (it != secret_chats_with_user_.end()) {
      for (auto secret_chat_id : it->second) {
        td_->messages_manager_->on_dialog_title_updated(DialogId(secret_chat_id));
      }
    }
  }
  if (u->is_photo_changed) {
    td_->messages_manager_->on_dialog_photo_updated(DialogId(user_id));
    auto it = secret_chats_with_user_.find(user_id);
    if (it != secret_chats_with_user_.end()) {
      for (auto secret_chat_id : it->second) {
        td_->messages_manager_->on_dialog_photo_updated(DialogId(secret_chat_id));
      }
    }

    UserFull *user_full = get_user_full(user_id);
    if (user_full != nullptr) {
      user_full->photos.clear();
      if (u->photo.id == 0) {
        user_full->photo_count = 0;
      } else {
        user_full->photo_count = -1;
      }
      user_full->photos_offset = user_full->photo_count;
    }
  }
  if (u->is_status_changed && user_id != get_my_id("update_user")) {
    auto left_time = u->was_online - G()->server_time_cached();
    if (left_time >= 0 && left_time < 30 * 86400) {
      left_time += 2.0;  // to guarantee expiration
      LOG(DEBUG) << "Set online timeout for " << user_id << " in " << left_time;
      user_online_timeout_.set_timeout_in(user_id.get(), left_time);
    } else {
      LOG(DEBUG) << "Cancel online timeout for " << user_id;
      user_online_timeout_.cancel_timeout(user_id.get());
    }
  }

  u->is_name_changed = false;
  u->is_username_changed = false;
  u->is_photo_changed = false;
  u->is_outbound_link_changed = false;

  if (u->is_deleted) {
    td_->inline_queries_manager_->remove_recent_inline_bot(user_id, Promise<>());
    /*
    DialogId dialog_id(user_id);
    for (auto category : {TopDialogCategory::Correspondent, TopDialogCategory::BotPM, TopDialogCategory::BotInline}) {
      send_closure(G()->top_dialog_manager(), &TopDialogManager::delete_dialog, category, dialog_id,
                   get_input_peer_user(user_id, AccessRights::Read));
    }
    */
  }

  LOG(DEBUG) << "Update " << user_id << ": is_changed = " << u->is_changed
             << ", need_send_update = " << u->need_send_update << ", is_status_chaned = " << u->is_status_changed;
  if (u->is_changed || u->need_send_update) {
    if (!from_database) {
      u->is_saved = false;
    }
    if (u->need_send_update) {
      send_closure(G()->td(), &Td::send_update, make_tl_object<td_api::updateUser>(get_user_object(user_id, u)));
      u->need_send_update = false;
      u->is_status_changed = false;
    }
    u->is_changed = false;
  }
  if (u->is_status_changed) {
    if (!from_database) {
      u->is_status_saved = false;
    }
    send_closure(G()->td(), &Td::send_update,
                 make_tl_object<td_api::updateUserStatus>(user_id.get(), get_user_status_object(user_id, u)));
    u->is_status_changed = false;
  }

  if (!from_database) {
    save_user(u, user_id, from_binlog);
  }
}

void ContactsManager::update_chat(Chat *c, ChatId chat_id, bool from_binlog, bool from_database) {
  CHECK(c != nullptr);
  if (c->is_photo_changed) {
    td_->messages_manager_->on_dialog_photo_updated(DialogId(chat_id));
  }
  if (c->is_title_changed) {
    td_->messages_manager_->on_dialog_title_updated(DialogId(chat_id));
  }
  c->is_photo_changed = false;
  c->is_title_changed = false;

  LOG(DEBUG) << "Update " << chat_id << ": is_changed = " << c->is_changed
             << ", need_send_update = " << c->need_send_update;
  if (c->is_changed || c->need_send_update) {
    if (!from_database) {
      c->is_saved = false;
    }
    c->is_changed = false;
    if (c->need_send_update) {
      send_closure(G()->td(), &Td::send_update,
                   make_tl_object<td_api::updateBasicGroup>(get_basic_group_object(chat_id, c)));
      c->need_send_update = false;
    }
  }

  if (!from_database) {
    save_chat(c, chat_id, from_binlog);
  }
}

void ContactsManager::update_channel(Channel *c, ChannelId channel_id, bool from_binlog, bool from_database) {
  CHECK(c != nullptr);
  if (c->is_photo_changed) {
    td_->messages_manager_->on_dialog_photo_updated(DialogId(channel_id));
  }
  if (c->is_title_changed) {
    td_->messages_manager_->on_dialog_title_updated(DialogId(channel_id));
  }
  if (c->is_status_changed) {
    c->status.update_restrictions();
    auto until_date = c->status.get_until_date();
    int32 left_time = 0;
    if (until_date > 0) {
      left_time = until_date - G()->unix_time_cached() + 1;
      CHECK(left_time > 0);
    }
    if (left_time > 0 && left_time < 366 * 86400) {
      channel_unban_timeout_.set_timeout_in(channel_id.get(), left_time);
    } else {
      channel_unban_timeout_.cancel_timeout(channel_id.get());
    }
  }
  if (c->is_username_changed) {
    if (c->status.is_creator() && created_public_channels_inited_) {
      if (c->username.empty()) {
        created_public_channels_.erase(
            std::remove(created_public_channels_.begin(), created_public_channels_.end(), channel_id),
            created_public_channels_.end());
      } else {
        if (std::find(created_public_channels_.begin(), created_public_channels_.end(), channel_id) ==
            created_public_channels_.end()) {
          created_public_channels_.push_back(channel_id);
        }
      }
    }
  }
  c->is_photo_changed = false;
  c->is_title_changed = false;
  c->is_status_changed = false;
  c->is_username_changed = false;

  LOG(DEBUG) << "Update " << channel_id << ": is_changed = " << c->is_changed
             << ", need_send_update = " << c->need_send_update;
  if (c->is_changed || c->need_send_update) {
    if (!from_database) {
      c->is_saved = false;
    }
    c->is_changed = false;
    if (c->need_send_update) {
      send_closure(G()->td(), &Td::send_update,
                   make_tl_object<td_api::updateSupergroup>(get_supergroup_object(channel_id, c)));
      c->need_send_update = false;
    }
  }

  if (!from_database) {
    save_channel(c, channel_id, from_binlog);
  }

  bool have_read_access = have_input_peer_channel(c, AccessRights::Read);
  bool is_member = c->status.is_member();
  if (c->had_read_access && !have_read_access) {
    send_closure_later(G()->messages_manager(), &MessagesManager::delete_dialog, DialogId(channel_id));
  } else if (!from_database && c->was_member != is_member) {
    DialogId dialog_id(channel_id);
    send_closure_later(G()->messages_manager(), &MessagesManager::force_create_dialog, dialog_id, "update channel",
                       true);
  }
  c->had_read_access = have_read_access;
  c->was_member = is_member;
}

void ContactsManager::update_secret_chat(SecretChat *c, SecretChatId secret_chat_id, bool from_binlog,
                                         bool from_database) {
  CHECK(c != nullptr);
  LOG(DEBUG) << "Update " << secret_chat_id << ": is_changed = " << c->is_changed
             << ", need_send_update = " << c->need_send_update;
  if (c->is_changed || c->need_send_update) {
    if (!from_database) {
      c->is_saved = false;
    }
    c->is_changed = false;
    if (c->need_send_update) {
      send_closure(G()->td(), &Td::send_update,
                   make_tl_object<td_api::updateSecretChat>(get_secret_chat_object(secret_chat_id, c)));
      c->need_send_update = false;
    }

    DialogId dialog_id(secret_chat_id);
    send_closure_later(G()->messages_manager(), &MessagesManager::force_create_dialog, dialog_id, "update secret chat",
                       true);
  }

  if (!from_database) {
    save_secret_chat(c, secret_chat_id, from_binlog);
  }
}

void ContactsManager::update_user_full(UserFull *user_full, UserId user_id) {
  CHECK(user_full != nullptr);
  if (user_full->is_changed) {
    user_full->is_changed = false;
    if (user_full->is_inited) {
      send_closure(G()->td(), &Td::send_update,
                   make_tl_object<td_api::updateUserFullInfo>(get_user_id_object(user_id, "updateUserFullInfo"),
                                                              get_user_full_info_object(user_id, user_full)));
    }
  }
}

void ContactsManager::update_chat_full(ChatFull *chat_full, ChatId chat_id) {
  CHECK(chat_full != nullptr);
  if (chat_full->is_changed) {
    vector<UserId> administrator_user_ids;
    for (auto &participant : chat_full->participants) {
      if (participant.status.is_administrator()) {
        administrator_user_ids.push_back(participant.user_id);
      }
    }
    on_update_dialog_administrators(DialogId(chat_id), std::move(administrator_user_ids), chat_full->version != -1);

    chat_full->is_changed = false;
    send_closure(
        G()->td(), &Td::send_update,
        make_tl_object<td_api::updateBasicGroupFullInfo>(get_basic_group_id_object(chat_id, "update_chat_full"),
                                                         get_basic_group_full_info_object(chat_full)));
  }
}

void ContactsManager::update_channel_full(ChannelFull *channel_full, ChannelId channel_id) {
  CHECK(channel_full != nullptr);
  if (channel_full->is_changed) {
    if (channel_full->participant_count < channel_full->administrator_count) {
      channel_full->administrator_count = channel_full->participant_count;
    }
    channel_full->is_changed = false;
    send_closure(
        G()->td(), &Td::send_update,
        make_tl_object<td_api::updateSupergroupFullInfo>(get_supergroup_id_object(channel_id, "update_channel_full"),
                                                         get_channel_full_info_object(channel_full)));
  }
}

void ContactsManager::on_get_users(vector<tl_object_ptr<telegram_api::User>> &&users) {
  for (auto &user : users) {
    on_get_user(std::move(user));
  }
}

void ContactsManager::on_get_user_full(tl_object_ptr<telegram_api::userFull> &&user_full) {
  UserId user_id = get_user_id(user_full->user_);
  if (!user_id.is_valid()) {
    LOG(ERROR) << "Receive invalid " << user_id;
    return;
  }

  on_get_user(std::move(user_full->user_));
  const User *u = get_user(user_id);
  if (u == nullptr) {
    return;
  }

  on_update_user_links(user_id, std::move(user_full->link_->my_link_), std::move(user_full->link_->foreign_link_));
  td_->messages_manager_->on_update_dialog_notify_settings(DialogId(user_id), std::move(user_full->notify_settings_));

  UserFull *user = &users_full_[user_id];
  user->expires_at = Time::now() + USER_FULL_EXPIRE_TIME;
  user->is_inited = true;

  on_update_user_full_is_blocked(user, user_id, (user_full->flags_ & USER_FULL_FLAG_IS_BLOCKED) != 0);

  bool can_be_called = user_full->phone_calls_available_ && !user_full->phone_calls_private_;
  bool has_private_calls = user_full->phone_calls_private_;
  if (user->can_be_called != can_be_called || user->has_private_calls != has_private_calls ||
      user->about != user_full->about_ || user->common_chat_count != user_full->common_chats_count_) {
    user->can_be_called = can_be_called;
    user->has_private_calls = has_private_calls;
    user->about = std::move(user_full->about_);
    user->common_chat_count = user_full->common_chats_count_;

    user->is_changed = true;
  }

  int32 photo_id =
      user_full->profile_photo_ == nullptr ? telegram_api::photoEmpty::ID : user_full->profile_photo_->get_id();
  if (photo_id == telegram_api::photoEmpty::ID) {
    user->photo_count = 0;
    user->photos_offset = 0;
    user->photos.clear();
  } else {
    CHECK(photo_id == telegram_api::photo::ID);

    //    Photo profile_photo =
    //        get_photo(td_->file_manager_.get(), move_tl_object_as<telegram_api::photo>(user_full->profile_photo_));
  }

  if ((user_full->flags_ & USER_FULL_FLAG_HAS_BOT_INFO) != 0 && !u->is_deleted) {
    on_update_user_full_bot_info(user, user_id, u->bot_info_version, std::move(user_full->bot_info_));
  }
  update_user_full(user, user_id);
}

void ContactsManager::on_get_user_photos(UserId user_id, int32 offset, int32 limit, int32 total_count,
                                         vector<tl_object_ptr<telegram_api::Photo>> photos) {
  int32 photo_count = narrow_cast<int32>(photos.size());
  if (total_count < 0 || total_count < photo_count) {
    LOG(ERROR) << "Wrong photos total_count " << total_count << ". Receive " << photo_count << " photos";
    total_count = photo_count;
  }
  LOG_IF(ERROR, limit < photo_count) << "Requested not more than " << limit << " photos, but " << photo_count
                                     << " returned";

  UserFull *user = &users_full_[user_id];
  user->photo_count = total_count;
  CHECK(user->getting_photos_now);
  user->getting_photos_now = false;

  if (user->photos_offset == -1) {
    user->photos_offset = 0;
    CHECK(user->photos.empty());
  }

  if (offset != narrow_cast<int32>(user->photos.size()) + user->photos_offset) {
    LOG(INFO) << "Inappropriate offset to append " << user_id << " profile photos to cache: offset = " << offset
              << ", current_offset = " << user->photos_offset << ", photo_count = " << user->photos.size();
    user->photos.clear();
    user->photos_offset = offset;
  }

  for (auto &photo : photos) {
    int32 photo_id = photo->get_id();
    if (photo_id == telegram_api::photoEmpty::ID) {
      LOG(ERROR) << "Have got empty profile photo in getUserPhotos request for " << user_id << " with offset " << offset
                 << " and limit " << limit << ". Receive " << photo_count << " photos out of " << total_count
                 << " photos";
      continue;
    }
    CHECK(photo_id == telegram_api::photo::ID);

    user->photos.push_back(
        get_photo(td_->file_manager_.get(), move_tl_object_as<telegram_api::photo>(photo), DialogId()));
  }
}

bool ContactsManager::on_update_bot_info(tl_object_ptr<telegram_api::botInfo> &&bot_info) {
  CHECK(bot_info != nullptr);
  UserId user_id(bot_info->user_id_);
  if (!user_id.is_valid()) {
    LOG(ERROR) << "Receive invalid " << user_id;
    return false;
  }

  const User *u = get_user_force(user_id);
  if (u == nullptr) {
    LOG(ERROR) << "Have no " << user_id;
    return false;
  }

  if (u->is_deleted) {
    return false;
  }

  UserFull *user_full = &users_full_[user_id];
  bool result = on_update_user_full_bot_info(user_full, user_id, u->bot_info_version, std::move(bot_info));
  update_user_full(user_full, user_id);
  return result;
}

bool ContactsManager::on_update_user_full_bot_info(UserFull *user_full, UserId user_id, int32 bot_info_version,
                                                   tl_object_ptr<telegram_api::botInfo> &&bot_info) {
  CHECK(user_full != nullptr);
  CHECK(bot_info != nullptr);

  if (user_full->bot_info != nullptr && user_full->bot_info->version > bot_info_version) {
    LOG(WARNING) << "Ignore outdated version of BotInfo for " << user_id << " with version " << bot_info_version
                 << ", current version is " << user_full->bot_info->version;
    return false;
  }
  if (user_full->bot_info != nullptr && user_full->bot_info->version == bot_info_version) {
    LOG(DEBUG) << "Ignore already known version of BotInfo for " << user_id << " with version " << bot_info_version;
    return false;
  }

  vector<std::pair<string, string>> commands;
  commands.reserve(bot_info->commands_.size());
  for (auto &command : bot_info->commands_) {
    commands.emplace_back(std::move(command->command_), std::move(command->description_));
  }
  user_full->bot_info = make_unique<BotInfo>(bot_info_version, std::move(bot_info->description_), std::move(commands));
  user_full->is_changed = true;
  return true;
}

void ContactsManager::on_get_chat(tl_object_ptr<telegram_api::Chat> &&chat) {
  LOG(DEBUG) << "Receive " << to_string(chat);
  downcast_call(*chat, OnChatUpdate(this));
}

void ContactsManager::on_get_chats(vector<tl_object_ptr<telegram_api::Chat>> &&chats) {
  for (auto &chat : chats) {
    auto constuctor_id = chat->get_id();
    if (constuctor_id == telegram_api::channel::ID || constuctor_id == telegram_api::channelForbidden::ID) {
      // apply info about megagroups before corresponding chats
      on_get_chat(std::move(chat));
      chat = nullptr;
    }
  }
  for (auto &chat : chats) {
    if (chat != nullptr) {
      on_get_chat(std::move(chat));
      chat = nullptr;
    }
  }
}

void ContactsManager::on_get_chat_full(tl_object_ptr<telegram_api::ChatFull> &&chat_full_ptr) {
  LOG(INFO) << "Receive " << to_string(chat_full_ptr);
  if (chat_full_ptr->get_id() == telegram_api::chatFull::ID) {
    auto chat_full = move_tl_object_as<telegram_api::chatFull>(chat_full_ptr);
    ChatId chat_id(chat_full->id_);
    if (!chat_id.is_valid()) {
      LOG(ERROR) << "Receive invalid " << chat_id;
      return;
    }

    ChatFull *chat = &chats_full_[chat_id];
    on_update_chat_full_invite_link(chat, std::move(chat_full->exported_invite_));

    // Ignoring chat_full->photo

    for (auto &bot_info : chat_full->bot_info_) {
      if (on_update_bot_info(std::move(bot_info))) {
        chat->is_changed = true;
      }
    }

    on_get_chat_participants(std::move(chat_full->participants_));
    td_->messages_manager_->on_update_dialog_notify_settings(DialogId(chat_id), std::move(chat_full->notify_settings_));

    update_chat_full(chat, chat_id);
  } else {
    CHECK(chat_full_ptr->get_id() == telegram_api::channelFull::ID);
    auto channel_full = move_tl_object_as<telegram_api::channelFull>(chat_full_ptr);
    ChannelId channel_id(channel_full->id_);
    if (!channel_id.is_valid()) {
      LOG(ERROR) << "Receive invalid " << channel_id;
      return;
    }

    td_->messages_manager_->on_update_dialog_notify_settings(DialogId(channel_id),
                                                             std::move(channel_full->notify_settings_));

    // Ignoring channel_full->photo

    if (!have_channel(channel_id)) {
      LOG(ERROR) << channel_id << " not found";
      return;
    }

    auto participant_count =
        (channel_full->flags_ & CHANNEL_FULL_FLAG_HAS_PARTICIPANT_COUNT) != 0 ? channel_full->participants_count_ : 0;
    auto administrator_count =
        (channel_full->flags_ & CHANNEL_FULL_FLAG_HAS_ADMINISTRATOR_COUNT) != 0 ? channel_full->admins_count_ : 0;
    auto restricted_count =
        (channel_full->flags_ & CHANNEL_FULL_FLAG_HAS_BANNED_COUNT) != 0 ? channel_full->banned_count_ : 0;
    auto banned_count =
        (channel_full->flags_ & CHANNEL_FULL_FLAG_HAS_BANNED_COUNT) != 0 ? channel_full->kicked_count_ : 0;
    auto can_get_participants = (channel_full->flags_ & CHANNEL_FULL_FLAG_CAN_GET_PARTICIPANTS) != 0;
    auto can_set_username = (channel_full->flags_ & CHANNEL_FULL_FLAG_CAN_SET_USERNAME) != 0;
    auto can_set_sticker_set = (channel_full->flags_ & CHANNEL_FULL_FLAG_CAN_SET_STICKERS) != 0;
    auto is_all_history_available = (channel_full->flags_ & CHANNEL_FULL_FLAG_IS_ALL_HISTORY_HIDDEN) == 0;
    int64 sticker_set_id = channel_full->stickerset_ == nullptr
                               ? 0
                               : td_->stickers_manager_->on_get_sticker_set(std::move(channel_full->stickerset_), true);

    ChannelFull *channel = &channels_full_[channel_id];
    channel->expires_at = Time::now() + CHANNEL_FULL_EXPIRE_TIME;
    if (channel->description != channel_full->about_ || channel->participant_count != participant_count ||
        channel->administrator_count != administrator_count || channel->restricted_count != restricted_count ||
        channel->banned_count != banned_count || channel->can_get_participants != can_get_participants ||
        channel->can_set_username != can_set_username || channel->can_set_sticker_set != can_set_sticker_set ||
        channel->sticker_set_id != sticker_set_id || channel->is_all_history_available != is_all_history_available) {
      channel->description = std::move(channel_full->about_);
      channel->participant_count = participant_count;
      channel->administrator_count = administrator_count;
      channel->restricted_count = restricted_count;
      channel->banned_count = banned_count;
      channel->can_get_participants = can_get_participants;
      channel->can_set_username = can_set_username;
      channel->can_set_sticker_set = can_set_sticker_set;
      channel->sticker_set_id = sticker_set_id;
      channel->is_all_history_available = is_all_history_available;

      channel->is_changed = true;

      if (participant_count != 0) {
        auto c = get_channel(channel_id);
        if (c != nullptr && c->participant_count != participant_count) {
          c->participant_count = participant_count;
          c->need_send_update = true;
          update_channel(c, channel_id);
        }
      }
    }

    td_->messages_manager_->on_read_channel_outbox(channel_id,
                                                   MessageId(ServerMessageId(channel_full->read_outbox_max_id_)));
    if ((channel_full->flags_ & CHANNEL_FULL_FLAG_HAS_AVAILABLE_MIN_MESSAGE_ID) != 0) {
      td_->messages_manager_->on_update_channel_max_unavailable_message_id(
          channel_id, MessageId(ServerMessageId(channel_full->available_min_id_)));
    }
    td_->messages_manager_->on_read_channel_inbox(
        channel_id, MessageId(ServerMessageId(channel_full->read_inbox_max_id_)), channel_full->unread_count_);

    on_update_channel_full_invite_link(channel, std::move(channel_full->exported_invite_));

    if ((channel_full->flags_ & CHANNEL_FULL_FLAG_HAS_PINNED_MESSAGE) != 0) {
      on_update_channel_full_pinned_message(channel, MessageId(ServerMessageId(channel_full->pinned_msg_id_)));
    }

    for (auto &bot_info : channel_full->bot_info_) {
      on_update_bot_info(std::move(bot_info));
    }

    ChatId migrated_from_chat_id;
    MessageId migrated_from_max_message_id;

    if (channel_full->flags_ & CHANNEL_FULL_FLAG_MIGRATED_FROM) {
      migrated_from_chat_id = ChatId(channel_full->migrated_from_chat_id_);
      migrated_from_max_message_id = MessageId(ServerMessageId(channel_full->migrated_from_max_id_));
    }

    if (channel->migrated_from_chat_id != migrated_from_chat_id ||
        channel->migrated_from_max_message_id != migrated_from_max_message_id) {
      channel->migrated_from_chat_id = migrated_from_chat_id;
      channel->migrated_from_max_message_id = migrated_from_max_message_id;
      channel->is_changed = true;
    }

    update_channel_full(channel, channel_id);
  }
}

bool ContactsManager::is_update_about_username_change_received(UserId user_id) const {
  const User *u = get_user(user_id);
  if (u != nullptr) {
    return u->inbound == LinkState::Contact;
  } else {
    return false;
  }
}

void ContactsManager::on_update_user_name(UserId user_id, string &&first_name, string &&last_name, string &&username) {
  if (!user_id.is_valid()) {
    LOG(ERROR) << "Receive invalid " << user_id;
    return;
  }

  User *u = get_user_force(user_id);
  if (u != nullptr) {
    on_update_user_name(u, user_id, std::move(first_name), std::move(last_name), std::move(username));
    update_user(u, user_id);
  } else {
    LOG(INFO) << "Ignore update user name about unknown " << user_id;
  }
}

void ContactsManager::on_update_user_name(User *u, UserId user_id, string &&first_name, string &&last_name,
                                          string &&username) {
  if (first_name.empty() && last_name.empty()) {
    first_name = u->phone_number;
  }
  if (u->first_name != first_name || u->last_name != last_name) {
    u->first_name = std::move(first_name);
    u->last_name = std::move(last_name);
    u->is_name_changed = true;
    LOG(DEBUG) << "Name has changed for " << user_id;
    u->need_send_update = true;
  }
  td_->messages_manager_->on_dialog_username_updated(DialogId(user_id), u->username, username);
  if (u->username != username) {
    u->username = std::move(username);
    u->is_username_changed = true;
    LOG(DEBUG) << "Username has changed for " << user_id;
    u->need_send_update = true;
  }
}

void ContactsManager::on_update_user_phone_number(UserId user_id, string &&phone_number) {
  if (!user_id.is_valid()) {
    LOG(ERROR) << "Receive invalid " << user_id;
    return;
  }

  User *u = get_user_force(user_id);
  if (u != nullptr) {
    on_update_user_phone_number(u, user_id, std::move(phone_number));
    update_user(u, user_id);
  } else {
    LOG(INFO) << "Ignore update user phone number about unknown " << user_id;
  }
}

void ContactsManager::on_update_user_phone_number(User *u, UserId user_id, string &&phone_number) {
  if (u->phone_number != phone_number) {
    u->phone_number = std::move(phone_number);
    LOG(DEBUG) << "Phone number has changed for " << user_id;
    u->need_send_update = true;
  }
}

void ContactsManager::on_update_user_photo(UserId user_id, tl_object_ptr<telegram_api::UserProfilePhoto> &&photo_ptr) {
  if (!user_id.is_valid()) {
    LOG(ERROR) << "Receive invalid " << user_id;
    return;
  }

  User *u = get_user_force(user_id);
  if (u != nullptr) {
    on_update_user_photo(u, user_id, std::move(photo_ptr));
    update_user(u, user_id);
  } else {
    LOG(INFO) << "Ignore update user photo about unknown " << user_id;
  }
}

void ContactsManager::on_update_user_photo(User *u, UserId user_id,
                                           tl_object_ptr<telegram_api::UserProfilePhoto> &&photo) {
  if (td_->auth_manager_->is_bot() && !G()->parameters().use_file_db && !u->is_photo_inited) {
    bool is_empty = photo == nullptr || photo->get_id() == telegram_api::userProfilePhotoEmpty::ID;
    pending_user_photos_[user_id] = std::move(photo);

    UserFull *user_full = get_user_full(user_id);
    if (user_full != nullptr) {
      user_full->photos.clear();
      if (is_empty) {
        user_full->photo_count = 0;
      } else {
        user_full->photo_count = -1;
      }
      user_full->photos_offset = user_full->photo_count;
    }
    return;
  }

  do_update_user_photo(u, user_id, std::move(photo));
}

void ContactsManager::do_update_user_photo(User *u, UserId user_id,
                                           tl_object_ptr<telegram_api::UserProfilePhoto> &&photo) {
  u->is_photo_inited = true;
  ProfilePhoto new_photo = get_profile_photo(td_->file_manager_.get(), std::move(photo));

  if (new_photo != u->photo) {
    u->photo = new_photo;
    u->is_photo_changed = true;
    LOG(DEBUG) << "Photo has changed for " << user_id;
    u->need_send_update = true;
  }
}

void ContactsManager::on_update_user_online(UserId user_id, tl_object_ptr<telegram_api::UserStatus> &&status) {
  if (!user_id.is_valid()) {
    LOG(ERROR) << "Receive invalid " << user_id;
    return;
  }

  User *u = get_user_force(user_id);
  if (u != nullptr) {
    on_update_user_online(u, user_id, std::move(status));
    update_user(u, user_id);
  } else {
    LOG(INFO) << "Ignore update user online about unknown " << user_id;
  }
}

void ContactsManager::on_update_user_online(User *u, UserId user_id, tl_object_ptr<telegram_api::UserStatus> &&status) {
  int32 id = status == nullptr ? telegram_api::userStatusEmpty::ID : status->get_id();
  int32 new_online;
  bool is_offline = false;
  if (id == telegram_api::userStatusOnline::ID) {
    int32 now = G()->unix_time();

    auto st = move_tl_object_as<telegram_api::userStatusOnline>(status);
    new_online = st->expires_;
    LOG_IF(ERROR, new_online < now - 86400)
        << "Receive userStatusOnline expired more than one day in past " << new_online;
  } else if (id == telegram_api::userStatusOffline::ID) {
    int32 now = G()->unix_time();

    auto st = move_tl_object_as<telegram_api::userStatusOffline>(status);
    new_online = st->was_online_;
    if (new_online >= now) {
      LOG_IF(ERROR, new_online > now + 10)
          << "Receive userStatusOffline but was online points to future time " << new_online << ", now is " << now;
      new_online = now - 1;
    }
    is_offline = true;
  } else if (id == telegram_api::userStatusRecently::ID) {
    new_online = -1;
  } else if (id == telegram_api::userStatusLastWeek::ID) {
    new_online = -2;
    is_offline = true;
  } else if (id == telegram_api::userStatusLastMonth::ID) {
    new_online = -3;
    is_offline = true;
  } else {
    CHECK(id == telegram_api::userStatusEmpty::ID);
    new_online = 0;
  }

  if (new_online != u->was_online) {
    LOG(DEBUG) << "Update " << user_id << " online from " << u->was_online << " to " << new_online;
    u->was_online = new_online;
    u->is_status_changed = true;

    if (user_id == get_my_id("on_update_user_online")) {
      my_was_online_local_ = 0;
      if (is_offline) {
        td_->on_online_updated(false, false);
      }
    }
  }
}

void ContactsManager::on_update_user_blocked(UserId user_id, bool is_blocked) {
  LOG(INFO) << "Receive update user blocked with " << user_id << " and is_blocked = " << is_blocked;
  if (!user_id.is_valid()) {
    LOG(ERROR) << "Receive invalid " << user_id;
    return;
  }

  UserFull *user_full = get_user_full(user_id);
  if (user_full == nullptr) {
    return;
  }
  on_update_user_full_is_blocked(user_full, user_id, is_blocked);
  update_user_full(user_full, user_id);
}

void ContactsManager::on_update_user_full_is_blocked(UserFull *user_full, UserId user_id, bool is_blocked) {
  CHECK(user_full != nullptr);
  if (user_full->is_inited && user_full->is_blocked != is_blocked) {
    user_full->is_blocked = is_blocked;
    user_full->is_changed = true;
  }
}

void ContactsManager::on_delete_profile_photo(int64 profile_photo_id, Promise<Unit> promise) {
  UserId my_id = get_my_id("on_delete_profile_photo");

  UserFull *user_full = get_user_full(my_id);
  if (user_full != nullptr) {
    // drop photo cache
    user_full->photos.clear();
    user_full->photo_count = -1;
    user_full->photos_offset = -1;
  }

  auto input_user = get_input_user(my_id);
  CHECK(input_user != nullptr);
  vector<tl_object_ptr<telegram_api::InputUser>> users;
  users.push_back(std::move(input_user));
  td_->create_handler<GetUsersQuery>(std::move(promise))->send(std::move(users));
}

ContactsManager::LinkState ContactsManager::get_link_state(tl_object_ptr<telegram_api::ContactLink> &&link) {
  int32 id = link->get_id();
  switch (id) {
    case telegram_api::contactLinkUnknown::ID:
      return LinkState::Unknown;
    case telegram_api::contactLinkNone::ID:
      return LinkState::None;
    case telegram_api::contactLinkHasPhone::ID:
      return LinkState::KnowsPhoneNumber;
    case telegram_api::contactLinkContact::ID:
      return LinkState::Contact;
    default:
      UNREACHABLE();
  }
  return LinkState::Unknown;
}

void ContactsManager::on_update_user_links(UserId user_id, tl_object_ptr<telegram_api::ContactLink> &&outbound,
                                           tl_object_ptr<telegram_api::ContactLink> &&inbound) {
  if (!user_id.is_valid()) {
    LOG(ERROR) << "Receive invalid " << user_id;
    return;
  }

  User *u = get_user_force(user_id);
  if (u != nullptr) {
    on_update_user_links(u, user_id, get_link_state(std::move(outbound)), get_link_state(std::move(inbound)));
    update_user(u, user_id);
  } else {
    LOG(INFO) << "Ignore update user links about unknown " << user_id;
  }
}

void ContactsManager::on_update_user_links(User *u, UserId user_id, LinkState outbound, LinkState inbound) {
  UserId my_id = get_my_id("on_update_user_links");
  if (user_id == my_id) {
    if (outbound == LinkState::None && !td_->auth_manager_->is_bot()) {
      outbound = LinkState::KnowsPhoneNumber;
    }
    inbound = outbound;
  }

  LOG(DEBUG) << "Update " << user_id << " links from (" << u->outbound << ", " << u->inbound << ") to (" << outbound
             << ", " << inbound << ")";
  bool need_send_update = false;
  if (outbound != u->outbound && outbound != LinkState::Unknown) {
    need_send_update |= outbound != LinkState::None || u->outbound != LinkState::Unknown;
    LOG(DEBUG) << "Set outbound link to " << outbound << ", need_send_update = " << need_send_update;
    u->outbound = outbound;
    u->is_outbound_link_changed = true;
    u->is_changed = true;
  }
  if (inbound != u->inbound && inbound != LinkState::Unknown) {
    need_send_update |= inbound != LinkState::None || u->inbound != LinkState::Unknown;
    LOG(DEBUG) << "Set inbound link to " << inbound << ", need_send_update = " << need_send_update;
    u->inbound = inbound;
    u->is_changed = true;
  }
  if (u->inbound == LinkState::Contact && u->outbound != LinkState::Contact) {
    u->inbound = LinkState::KnowsPhoneNumber;
    u->is_changed = true;
    need_send_update = true;
  }

  if (need_send_update) {
    LOG(DEBUG) << "Links have changed for " << user_id;
    u->need_send_update = true;
  }
}

void ContactsManager::invalidate_user_full(UserId user_id) {
  auto user_full = get_user_full(user_id);
  if (user_full == nullptr) {
    return;
  }

  user_full->expires_at = 0.0;

  user_full->photos.clear();
  user_full->photo_count = -1;
  user_full->photos_offset = -1;
  user_full->is_inited = true;
  user_full->is_blocked = false;
  user_full->can_be_called = false;
  user_full->has_private_calls = false;
  user_full->about = string();
  user_full->common_chat_count = 0;
  user_full->bot_info = nullptr;
  user_full->is_changed = true;

  update_user_full(user_full, user_id);
}

void ContactsManager::on_get_chat_participants(tl_object_ptr<telegram_api::ChatParticipants> &&participants_ptr) {
  switch (participants_ptr->get_id()) {
    case telegram_api::chatParticipantsForbidden::ID: {
      auto participants = move_tl_object_as<telegram_api::chatParticipantsForbidden>(participants_ptr);
      ChatId chat_id(participants->chat_id_);
      if (!chat_id.is_valid()) {
        LOG(ERROR) << "Receive invalid " << chat_id;
        return;
      }

      if (!have_chat_force(chat_id)) {
        LOG(ERROR) << chat_id << " not found";
        return;
      }

      invalidate_chat_full(chat_id);
      break;
    }
    case telegram_api::chatParticipants::ID: {
      auto participants = move_tl_object_as<telegram_api::chatParticipants>(participants_ptr);
      ChatId chat_id(participants->chat_id_);
      if (!chat_id.is_valid()) {
        LOG(ERROR) << "Receive invalid " << chat_id;
        return;
      }

      const Chat *c = get_chat_force(chat_id);
      if (c == nullptr) {
        LOG(ERROR) << chat_id << " not found";
        return;
      }

      ChatFull *chat_full = get_chat_full(chat_id);
      if (chat_full == nullptr) {
        LOG(INFO) << "Ignore update of members for unknown " << chat_id;
        return;
      }

      UserId new_creator_user_id;
      vector<DialogParticipant> new_participants;
      new_participants.reserve(participants->participants_.size());

      for (auto &participant_ptr : participants->participants_) {
        DialogParticipant dialog_participant;
        switch (participant_ptr->get_id()) {
          case telegram_api::chatParticipant::ID: {
            auto participant = move_tl_object_as<telegram_api::chatParticipant>(participant_ptr);
            dialog_participant = {UserId(participant->user_id_), UserId(participant->inviter_id_), participant->date_,
                                  DialogParticipantStatus::Member()};
            break;
          }
          case telegram_api::chatParticipantCreator::ID: {
            auto participant = move_tl_object_as<telegram_api::chatParticipantCreator>(participant_ptr);
            new_creator_user_id = UserId(participant->user_id_);
            dialog_participant = {new_creator_user_id, new_creator_user_id, c->date,
                                  DialogParticipantStatus::Creator(true)};
            break;
          }
          case telegram_api::chatParticipantAdmin::ID: {
            auto participant = move_tl_object_as<telegram_api::chatParticipantAdmin>(participant_ptr);
            dialog_participant = {UserId(participant->user_id_), UserId(participant->inviter_id_), participant->date_,
                                  DialogParticipantStatus::GroupAdministrator(c->is_creator)};
            break;
          }
          default:
            UNREACHABLE();
        }

        LOG_IF(ERROR, !have_user(dialog_participant.user_id))
            << "Have no information about " << dialog_participant.user_id;
        LOG_IF(ERROR, !have_user(dialog_participant.inviter_user_id))
            << "Have no information about " << dialog_participant.inviter_user_id;
        if (dialog_participant.joined_date < c->date) {
          LOG_IF(ERROR, dialog_participant.joined_date < c->date - 30 && c->date >= 1486000000)
              << "Wrong join date = " << dialog_participant.joined_date << " for " << dialog_participant.user_id << ", "
              << chat_id << " was created at " << c->date;
          dialog_participant.joined_date = c->date;
        }
        new_participants.push_back(std::move(dialog_participant));
      }

      if (new_creator_user_id.is_valid()) {
        LOG_IF(ERROR, !have_user(new_creator_user_id))
            << "Have no information about group creator " << new_creator_user_id;
        if (chat_full->creator_user_id.is_valid() && chat_full->creator_user_id != new_creator_user_id) {
          LOG(ERROR) << "Group creator has changed from " << chat_full->creator_user_id << " to "
                     << new_creator_user_id;
        }
      }
      if (chat_full->creator_user_id != new_creator_user_id) {
        chat_full->creator_user_id = new_creator_user_id;
        chat_full->is_changed = true;
      }

      on_update_chat_full_participants(chat_full, chat_id, std::move(new_participants), participants->version_);
      update_chat_full(chat_full, chat_id);
      break;
    }
    default:
      UNREACHABLE();
  }
}

const DialogParticipant *ContactsManager::get_chat_participant(ChatId chat_id, UserId user_id) const {
  auto chat_full = get_chat_full(chat_id);
  if (chat_full == nullptr) {
    return nullptr;
  }
  return get_chat_participant(chat_full, user_id);
}

const DialogParticipant *ContactsManager::get_chat_participant(const ChatFull *chat_full, UserId user_id) const {
  for (auto &dialog_participant : chat_full->participants) {
    if (dialog_participant.user_id == user_id) {
      return &dialog_participant;
    }
  }
  return nullptr;
}

DialogParticipant ContactsManager::get_dialog_participant(
    ChannelId channel_id, tl_object_ptr<telegram_api::ChannelParticipant> &&participant_ptr) const {
  switch (participant_ptr->get_id()) {
    case telegram_api::channelParticipant::ID: {
      auto participant = move_tl_object_as<telegram_api::channelParticipant>(participant_ptr);
      return {UserId(participant->user_id_), UserId(), participant->date_, DialogParticipantStatus::Member()};
    }
    case telegram_api::channelParticipantSelf::ID: {
      auto participant = move_tl_object_as<telegram_api::channelParticipantSelf>(participant_ptr);
      return {UserId(participant->user_id_), UserId(participant->inviter_id_), participant->date_,
              get_channel_status(channel_id)};
    }
    case telegram_api::channelParticipantCreator::ID: {
      auto participant = move_tl_object_as<telegram_api::channelParticipantCreator>(participant_ptr);
      return {UserId(participant->user_id_), UserId(), 0, DialogParticipantStatus::Creator(true)};
    }
    case telegram_api::channelParticipantAdmin::ID: {
      auto participant = move_tl_object_as<telegram_api::channelParticipantAdmin>(participant_ptr);
      bool can_be_edited = (participant->flags_ & telegram_api::channelParticipantAdmin::CAN_EDIT_MASK) != 0;
      return {UserId(participant->user_id_), UserId(participant->promoted_by_), participant->date_,
              get_dialog_participant_status(can_be_edited, std::move(participant->admin_rights_))};
    }
    case telegram_api::channelParticipantBanned::ID: {
      auto participant = move_tl_object_as<telegram_api::channelParticipantBanned>(participant_ptr);
      auto is_member = (participant->flags_ & telegram_api::channelParticipantBanned::LEFT_MASK) == 0;
      return {UserId(participant->user_id_), UserId(participant->kicked_by_), participant->date_,
              get_dialog_participant_status(is_member, std::move(participant->banned_rights_))};
    }
    default:
      UNREACHABLE();
      return DialogParticipant();
  }
}

tl_object_ptr<td_api::chatMember> ContactsManager::get_chat_member_object(
    const DialogParticipant &dialog_participant) const {
  UserId participant_user_id = dialog_participant.user_id;
  return make_tl_object<td_api::chatMember>(
      get_user_id_object(participant_user_id, "chatMember.user_id"),
      get_user_id_object(dialog_participant.inviter_user_id, "chatMember.inviter_user_id"),
      dialog_participant.joined_date, dialog_participant.status.get_chat_member_status_object(),
      get_bot_info_object(participant_user_id));
}

bool ContactsManager::on_get_channel_error(ChannelId channel_id, const Status &status, const string &source) {
  LOG(INFO) << "Receive " << status << " in " << channel_id << " from " << source;
  if (status.message() == CSlice("SESSION_REVOKED") || status.message() == CSlice("USER_DEACTIVATED")) {
    // authorization is lost
    return true;
  }
  if (status.code() == 420 || status.code() == 429) {
    // flood wait
    return true;
  }
  if (status.message() == CSlice("BOT_METHOD_INVALID")) {
    LOG(ERROR) << "Receive BOT_METHOD_INVALID from " << source;
    return true;
  }
  if (status.message() == "CHANNEL_PRIVATE" || status.message() == "CHANNEL_PUBLIC_GROUP_NA") {
    if (!channel_id.is_valid()) {
      LOG(ERROR) << "Receive " << status.message() << " in invalid " << channel_id << " from " << source;
      return false;
    }

    auto c = get_channel(channel_id);
    if (c == nullptr) {
      if (td_->auth_manager_->is_bot() && source == "GetChannelsQuery") {
        // get channel from server by its identifier
        return true;
      }
      LOG(ERROR) << "Receive " << status.message() << " in not found " << channel_id << " from " << source;
      return false;
    }

    auto debug_channel_object = oneline(to_string(get_supergroup_object(channel_id, c)));
    if (c->status.is_member()) {
      LOG(INFO) << "Emulate leaving " << channel_id;
      // TODO we also may try to write to public channel
      int32 flags = 0;
      if (c->is_megagroup) {
        flags |= CHANNEL_FLAG_IS_MEGAGROUP;
      } else {
        flags |= CHANNEL_FLAG_IS_BROADCAST;
      }
      telegram_api::channelForbidden update(flags, false /*ignored*/, false /*ignored*/, channel_id.get(),
                                            c->access_hash, c->title, 0);
      on_chat_update(update);
    } else if (!c->username.empty()) {
      LOG(INFO) << "Drop username of " << channel_id;
      on_update_channel_username(c, channel_id, "");
      update_channel(c, channel_id);
    }
    LOG_IF(ERROR, have_input_peer_channel(c, AccessRights::Read))
        << "Have read access to channel after receiving CHANNEL_PRIVATE. Channel state: "
        << oneline(to_string(get_supergroup_object(channel_id, c)))
        << ". Previous channel state: " << debug_channel_object;

    return true;
  }
  return false;
}

void ContactsManager::on_get_channel_participants_success(
    ChannelId channel_id, ChannelParticipantsFilter filter, int32 offset, int32 limit, int64 random_id,
    int32 total_count, vector<tl_object_ptr<telegram_api::ChannelParticipant>> &&participants) {
  LOG(INFO) << "Receive " << participants.size() << " members in " << channel_id;
  auto it = received_channel_participants_.find(random_id);
  CHECK(it != received_channel_participants_.end());

  it->second.first = total_count;

  auto &result = it->second.second;
  CHECK(result.empty());
  for (auto &participant_ptr : participants) {
    result.push_back(get_dialog_participant(channel_id, std::move(participant_ptr)));
  }

  if (filter.is_administrators() && offset == 0 && static_cast<int32>(participants.size()) < limit) {
    on_update_dialog_administrators(
        DialogId(channel_id),
        transform(result, [](const DialogParticipant &participant) { return participant.user_id; }), true);
  }
}

void ContactsManager::on_get_channel_participants_fail(ChannelId channel_id, ChannelParticipantsFilter filter,
                                                       int32 offset, int32 limit, int64 random_id) {
  // clean up
  received_channel_participants_.erase(random_id);
}

bool ContactsManager::speculative_add_count(int32 &count, int32 new_count) {
  new_count += count;
  if (new_count < 0) {
    new_count = 0;
  }
  if (new_count == count) {
    return false;
  }

  count = new_count;
  return true;
}

void ContactsManager::speculative_add_channel_participants(ChannelId channel_id, int32 new_participant_count,
                                                           bool by_me) {
  if (by_me) {
    // Currently ignore all changes made by the current user, because they may be already counted
    invalidate_channel_full(channel_id);  // just in case
    return;
  }

  auto c = get_channel(channel_id);
  if (c != nullptr && c->participant_count != 0 && speculative_add_count(c->participant_count, new_participant_count)) {
    c->need_send_update = true;
    update_channel(c, channel_id);
  }

  auto channel_full = get_channel_full(channel_id);
  if (channel_full == nullptr) {
    return;
  }

  channel_full->is_changed |= speculative_add_count(channel_full->participant_count, new_participant_count);

  update_channel_full(channel_full, channel_id);
}

void ContactsManager::speculative_add_channel_users(ChannelId channel_id, DialogParticipantStatus status,
                                                    DialogParticipantStatus old_status) {
  auto c = get_channel(channel_id);
  if (c != nullptr && c->participant_count != 0 &&
      speculative_add_count(c->participant_count, status.is_member() - old_status.is_member())) {
    c->need_send_update = true;
    update_channel(c, channel_id);
  }

  auto channel_full = get_channel_full(channel_id);
  if (channel_full == nullptr) {
    return;
  }

  channel_full->is_changed |=
      speculative_add_count(channel_full->participant_count, status.is_member() - old_status.is_member());
  channel_full->is_changed |= speculative_add_count(channel_full->administrator_count,
                                                    status.is_administrator() - old_status.is_administrator());
  channel_full->is_changed |=
      speculative_add_count(channel_full->restricted_count, status.is_restricted() - old_status.is_restricted());
  channel_full->is_changed |=
      speculative_add_count(channel_full->banned_count, status.is_banned() - old_status.is_banned());

  update_channel_full(channel_full, channel_id);
}

void ContactsManager::invalidate_channel_full(ChannelId channel_id) {
  LOG(INFO) << "Invalidate channel full for " << channel_id;
  // drop channel full cache
  // TODO at least need to invalidate channel invite link
  auto channel_full = get_channel_full(channel_id);
  if (channel_full != nullptr) {
    channel_full->expires_at = 0.0;
  }

  // channel_full->is_changed = true;
  // update_channel_full(channel_full, channel_id);
}

void ContactsManager::on_get_chat_invite_link(ChatId chat_id,
                                              tl_object_ptr<telegram_api::ExportedChatInvite> &&invite_link_ptr) {
  CHECK(chat_id.is_valid());
  if (!have_chat(chat_id)) {
    LOG(ERROR) << chat_id << " not found";
    return;
  }

  auto chat_full = get_chat_full(chat_id);
  if (chat_full == nullptr) {
    update_invite_link(chat_invite_links_[chat_id], std::move(invite_link_ptr));
    return;
  }
  on_update_chat_full_invite_link(chat_full, std::move(invite_link_ptr));
  update_chat_full(chat_full, chat_id);
}

void ContactsManager::on_update_chat_full_invite_link(
    ChatFull *chat_full, tl_object_ptr<telegram_api::ExportedChatInvite> &&invite_link_ptr) {
  CHECK(chat_full != nullptr);
  if (update_invite_link(chat_full->invite_link, std::move(invite_link_ptr))) {
    chat_full->is_changed = true;
  }
}

void ContactsManager::on_get_channel_invite_link(ChannelId channel_id,
                                                 tl_object_ptr<telegram_api::ExportedChatInvite> &&invite_link_ptr) {
  CHECK(channel_id.is_valid());
  if (!have_channel(channel_id)) {
    LOG(ERROR) << channel_id << " not found";
    return;
  }

  auto channel_full = get_channel_full(channel_id);
  if (channel_full == nullptr) {
    update_invite_link(channel_invite_links_[channel_id], std::move(invite_link_ptr));
    return;
  }
  on_update_channel_full_invite_link(channel_full, std::move(invite_link_ptr));
  update_channel_full(channel_full, channel_id);
}

void ContactsManager::on_update_channel_full_invite_link(
    ChannelFull *channel_full, tl_object_ptr<telegram_api::ExportedChatInvite> &&invite_link_ptr) {
  CHECK(channel_full != nullptr);
  if (update_invite_link(channel_full->invite_link, std::move(invite_link_ptr))) {
    channel_full->is_changed = true;
  }
}

void ContactsManager::on_get_dialog_invite_link_info(const string &invite_link,
                                                     tl_object_ptr<telegram_api::ChatInvite> &&chat_invite_ptr) {
  auto &invite_link_info = invite_link_infos_[invite_link];
  if (invite_link_info == nullptr) {
    invite_link_info = make_unique<InviteLinkInfo>();
  }

  CHECK(chat_invite_ptr != nullptr);
  switch (chat_invite_ptr->get_id()) {
    case telegram_api::chatInviteAlready::ID: {
      auto chat_invite_already = move_tl_object_as<telegram_api::chatInviteAlready>(chat_invite_ptr);
      auto chat_id = get_chat_id(chat_invite_already->chat_);
      if (chat_id != ChatId() && !chat_id.is_valid()) {
        LOG(ERROR) << "Receive invalid " << chat_id;
        chat_id = ChatId();
      }
      auto channel_id = get_channel_id(chat_invite_already->chat_);
      if (channel_id != ChannelId() && !channel_id.is_valid()) {
        LOG(ERROR) << "Receive invalid " << channel_id;
        channel_id = ChannelId();
      }
      on_get_chat(std::move(chat_invite_already->chat_));

      CHECK(chat_id == ChatId() || channel_id == ChannelId());
      invite_link_info->chat_id = chat_id;
      invite_link_info->channel_id = channel_id;

      if (chat_id.is_valid()) {
        on_get_chat_invite_link(chat_id, make_tl_object<telegram_api::chatInviteExported>(invite_link));
      }
      if (channel_id.is_valid()) {
        on_get_channel_invite_link(channel_id, make_tl_object<telegram_api::chatInviteExported>(invite_link));
      }
      break;
    }
    case telegram_api::chatInvite::ID: {
      auto chat_invite = move_tl_object_as<telegram_api::chatInvite>(chat_invite_ptr);
      invite_link_info->chat_id = ChatId();
      invite_link_info->channel_id = ChannelId();
      invite_link_info->title = chat_invite->title_;
      invite_link_info->photo = get_dialog_photo(td_->file_manager_.get(), std::move(chat_invite->photo_));
      invite_link_info->participant_count = chat_invite->participants_count_;
      invite_link_info->participant_user_ids.clear();
      for (auto &user : chat_invite->participants_) {
        auto user_id = get_user_id(user);
        if (!user_id.is_valid()) {
          LOG(ERROR) << "Receive invalid " << user_id;
        } else {
          on_get_user(std::move(user));
        }
        invite_link_info->participant_user_ids.push_back(user_id);
      }
      invite_link_info->is_chat = (chat_invite->flags_ & CHAT_INVITE_FLAG_IS_CHANNEL) == 0;
      invite_link_info->is_channel = (chat_invite->flags_ & CHAT_INVITE_FLAG_IS_CHANNEL) != 0;

      bool is_broadcast = (chat_invite->flags_ & CHAT_INVITE_FLAG_IS_BROADCAST) != 0;
      bool is_public = (chat_invite->flags_ & CHAT_INVITE_FLAG_IS_PUBLIC) != 0;
      bool is_megagroup = (chat_invite->flags_ & CHAT_INVITE_FLAG_IS_MEGAGROUP) != 0;

      if (!invite_link_info->is_channel) {
        if (is_broadcast || is_public || is_megagroup) {
          LOG(ERROR) << "Receive wrong chat invite: " << to_string(chat_invite);
          is_public = is_megagroup = false;
        }
      } else {
        LOG_IF(ERROR, is_broadcast == is_megagroup) << "Receive wrong chat invite: " << to_string(chat_invite);
      }

      invite_link_info->is_public = is_public;
      invite_link_info->is_megagroup = is_megagroup;
      break;
    }
    default:
      UNREACHABLE();
  }
}

bool ContactsManager::is_valid_invite_link(const string &invite_link) {
  return !get_dialog_invite_link_hash(invite_link).empty();
}

Slice ContactsManager::get_dialog_invite_link_hash(const string &invite_link) {
  auto lower_cased_invite_link_str = to_lower(invite_link);
  Slice lower_cased_invite_link = lower_cased_invite_link_str;
  size_t offset = 0;
  if (begins_with(lower_cased_invite_link, "https://")) {
    offset = 8;
  } else if (begins_with(lower_cased_invite_link, "http://")) {
    offset = 7;
  }
  lower_cased_invite_link.remove_prefix(offset);

  for (auto &url : INVITE_LINK_URLS) {
    if (begins_with(lower_cased_invite_link, url)) {
      return Slice(invite_link).substr(url.size() + offset);
    }
  }
  return Slice();
}

bool ContactsManager::update_invite_link(string &invite_link,
                                         tl_object_ptr<telegram_api::ExportedChatInvite> &&invite_link_ptr) {
  string new_invite_link;
  if (invite_link_ptr != nullptr) {
    switch (invite_link_ptr->get_id()) {
      case telegram_api::chatInviteEmpty::ID:
        // link is empty
        break;
      case telegram_api::chatInviteExported::ID: {
        auto chat_invite_exported = move_tl_object_as<telegram_api::chatInviteExported>(invite_link_ptr);
        new_invite_link = std::move(chat_invite_exported->link_);
        break;
      }
      default:
        UNREACHABLE();
    }
  }

  if (new_invite_link != invite_link) {
    if (!invite_link.empty()) {
      invite_link_infos_.erase(invite_link);
    }
    LOG_IF(ERROR, !new_invite_link.empty() && !is_valid_invite_link(new_invite_link))
        << "Unsupported invite link " << new_invite_link;

    invite_link = std::move(new_invite_link);
    return true;
  }
  return false;
}

void ContactsManager::invalidate_invite_link(const string &invite_link) {
  invite_link_infos_.erase(invite_link);
}

void ContactsManager::repair_chat_participants(ChatId chat_id) {
  send_get_chat_full_query(chat_id, Auto());
}

void ContactsManager::on_update_chat_add_user(ChatId chat_id, UserId inviter_user_id, UserId user_id, int32 date,
                                              int32 version) {
  if (!chat_id.is_valid()) {
    LOG(ERROR) << "Receive invalid " << chat_id;
    return;
  }
  if (!have_user(user_id)) {
    LOG(ERROR) << "Can't find " << user_id;
    return;
  }
  if (!have_user(inviter_user_id)) {
    LOG(ERROR) << "Can't find " << inviter_user_id;
    return;
  }
  LOG(INFO) << "Receive updateChatParticipantAdd to " << chat_id << " with " << user_id << " invited by "
            << inviter_user_id << " at " << date << " with version " << version;

  ChatFull *chat_full = get_chat_full(chat_id);
  if (chat_full == nullptr) {
    LOG(INFO) << "Ignoring update about members of " << chat_id;
    return;
  }
  auto c = get_chat(chat_id);
  if (c == nullptr) {
    LOG(ERROR) << "Receive updateChatParticipantAdd for unknown " << chat_id << ". Couldn't apply it";
    repair_chat_participants(chat_id);
    return;
  }
  if (c->left) {
    // possible if updates come out of order
    LOG(WARNING) << "Receive updateChatParticipantAdd for left " << chat_id << ". Couldn't apply it";

    repair_chat_participants(chat_id);  // just in case
    return;
  }
  if (on_update_chat_full_participants_short(chat_full, chat_id, version)) {
    for (auto &participant : chat_full->participants) {
      if (participant.user_id == user_id) {
        if (participant.inviter_user_id != inviter_user_id) {
          LOG(ERROR) << user_id << " was readded to " << chat_id << " by " << inviter_user_id
                     << ", previously invited by " << participant.inviter_user_id;
          participant.inviter_user_id = inviter_user_id;
          participant.joined_date = date;
          repair_chat_participants(chat_id);
        } else {
          // Possible if update comes twice
          LOG(INFO) << user_id << " was readded to " << chat_id;
        }
        return;
      }
    }
    chat_full->participants.push_back(DialogParticipant{user_id, inviter_user_id, date,
                                                        user_id == chat_full->creator_user_id
                                                            ? DialogParticipantStatus::Creator(true)
                                                            : DialogParticipantStatus::Member()});
    chat_full->is_changed = true;
    update_chat_full(chat_full, chat_id);

    // Chat is already updated
    if (chat_full->version == c->version &&
        narrow_cast<int32>(chat_full->participants.size()) != c->participant_count) {
      LOG(ERROR) << "Number of members of " << chat_id << " with version " << c->version << " is "
                 << c->participant_count << " but there are " << chat_full->participants.size()
                 << " members in the ChatFull";
      repair_chat_participants(chat_id);
    }
  }
}

void ContactsManager::on_update_chat_edit_administrator(ChatId chat_id, UserId user_id, bool is_administrator,
                                                        int32 version) {
  if (!chat_id.is_valid()) {
    LOG(ERROR) << "Receive invalid " << chat_id;
    return;
  }
  if (!have_user(user_id)) {
    LOG(ERROR) << "Can't find " << user_id;
    return;
  }
  LOG(INFO) << "Receive updateChatParticipantAdmin in " << chat_id << " with " << user_id << ", administrator rights "
            << (is_administrator ? "enabled" : "disabled") << " with version " << version;

  auto c = get_chat_force(chat_id);
  if (c == nullptr) {
    LOG(INFO) << "Ignoring update about members of unknown " << chat_id;
    return;
  }

  if (c->left) {
    // possible if updates come out of order
    LOG(WARNING) << "Receive updateChatParticipantAdmin for left " << chat_id << ". Couldn't apply it";

    repair_chat_participants(chat_id);  // just in case
    return;
  }
  if (version <= -1) {
    LOG(ERROR) << "Receive wrong version " << version << " for " << chat_id;
    return;
  }
  CHECK(c->version >= 0);

  if (version > c->version) {
    if (version != c->version + 1) {
      LOG(ERROR) << "Administrators of " << chat_id << " with version " << c->version
                 << " has changed but new version is " << version;
      repair_chat_participants(chat_id);
      return;
    }

    c->version = version;
    c->is_changed = true;
    if (user_id == get_my_id("on_update_chat_edit_administrator")) {
      on_update_chat_rights(c, chat_id, c->is_creator, is_administrator, c->everyone_is_administrator);
    }
    update_chat(c, chat_id);
  }

  ChatFull *chat_full = get_chat_full(chat_id);
  if (chat_full != nullptr) {
    if (chat_full->version + 1 == version) {
      for (auto &participant : chat_full->participants) {
        if (participant.user_id == user_id) {
          participant.status = is_administrator ? DialogParticipantStatus::GroupAdministrator(c->is_creator)
                                                : DialogParticipantStatus::Member();
          chat_full->is_changed = true;
          update_chat_full(chat_full, chat_id);
          return;
        }
      }
    }

    // can't find chat member or version have increased too much
    repair_chat_participants(chat_id);
  }
}

void ContactsManager::on_update_chat_delete_user(ChatId chat_id, UserId user_id, int32 version) {
  if (!chat_id.is_valid()) {
    LOG(ERROR) << "Receive invalid " << chat_id;
    return;
  }
  if (!have_user(user_id)) {
    LOG(ERROR) << "Can't find " << user_id;
    return;
  }
  LOG(INFO) << "Receive updateChatParticipantDelete from " << chat_id << " with " << user_id << " and version "
            << version;

  ChatFull *chat_full = get_chat_full(chat_id);
  if (chat_full == nullptr) {
    LOG(INFO) << "Ignoring update about members of " << chat_id;
    return;
  }
  const Chat *c = get_chat_force(chat_id);
  if (c == nullptr) {
    LOG(ERROR) << "Receive updateChatParticipantDelete for unknown " << chat_id;
    repair_chat_participants(chat_id);
    return;
  }
  if (user_id == get_my_id("on_update_chat_delete_user")) {
    LOG_IF(WARNING, !c->left) << "User was removed from " << chat_id
                              << " but it is not left the group. Possible if updates comes out of order";
    return;
  }
  if (c->left) {
    // possible if updates come out of order
    LOG(INFO) << "Receive updateChatParticipantDelete for left " << chat_id;

    repair_chat_participants(chat_id);
    return;
  }
  if (on_update_chat_full_participants_short(chat_full, chat_id, version)) {
    for (size_t i = 0; i < chat_full->participants.size(); i++) {
      if (chat_full->participants[i].user_id == user_id) {
        chat_full->participants[i] = chat_full->participants.back();
        chat_full->participants.resize(chat_full->participants.size() - 1);
        chat_full->is_changed = true;
        update_chat_full(chat_full, chat_id);

        if (static_cast<int>(chat_full->participants.size()) != c->participant_count) {
          repair_chat_participants(chat_id);
        }
        return;
      }
    }
    LOG(ERROR) << "Can't find group member " << user_id << " in " << chat_id << " to delete him";
    repair_chat_participants(chat_id);
  }
}

void ContactsManager::on_update_chat_everyone_is_administrator(ChatId chat_id, bool everyone_is_administrator,
                                                               int32 version) {
  if (!chat_id.is_valid()) {
    LOG(ERROR) << "Receive invalid " << chat_id;
    return;
  }
  LOG(INFO) << "Receive updateChatAdmins in " << chat_id << " with version " << version
            << " and everyone_is_administrator = " << everyone_is_administrator << ". Current version is " << version;

  auto c = get_chat_force(chat_id);
  if (c == nullptr) {
    LOG(INFO) << "Ignoring update about unknown " << chat_id;
    return;
  }

  if (c->left) {
    // possible if updates come out of order
    LOG(WARNING) << "Receive updateChatAdmins for left " << chat_id << ". Couldn't apply it";

    repair_chat_participants(chat_id);  // just in case
    return;
  }
  if (version <= -1) {
    LOG(ERROR) << "Receive wrong version " << version << " for " << chat_id;
    return;
  }
  CHECK(c->version >= 0);

  if (version > c->version) {
    if (version != c->version + 1) {
      LOG(WARNING) << "Anyone can edit of " << chat_id << " with version " << c->version
                   << " has changed but new version is " << version;
      repair_chat_participants(chat_id);
      return;
    }

    LOG_IF(ERROR, everyone_is_administrator == c->everyone_is_administrator)
        << "Receive updateChatAdmins in " << chat_id << " with version " << version
        << " and everyone_is_administrator = " << everyone_is_administrator
        << ", but everyone_is_administrator is not changed. Current version is " << c->version;
    c->version = version;
    c->is_changed = true;
    on_update_chat_rights(c, chat_id, c->is_creator, c->is_administrator, everyone_is_administrator);
    update_chat(c, chat_id);
  }
}

void ContactsManager::on_update_chat_left(Chat *c, ChatId chat_id, bool left, bool kicked) {
  if (c->left != left || c->kicked != kicked) {
    c->left = left;
    c->kicked = kicked;

    if (c->left) {
      c->participant_count = 0;
      c->version = -1;

      invalidate_chat_full(chat_id);
    }

    c->need_send_update = true;
  }
}

void ContactsManager::on_update_chat_rights(Chat *c, ChatId chat_id, bool is_creator, bool is_administrator,
                                            bool everyone_is_administrator) {
  if (c->is_creator != is_creator || c->is_administrator != is_administrator ||
      c->everyone_is_administrator != everyone_is_administrator) {
    c->is_creator = is_creator;
    c->is_administrator = is_administrator;
    c->everyone_is_administrator = everyone_is_administrator;
    c->can_edit = is_creator || is_administrator || everyone_is_administrator;
    c->need_send_update = true;
  }
}

void ContactsManager::on_update_chat_participant_count(Chat *c, ChatId chat_id, int32 participant_count, int32 version,
                                                       const string &debug_str) {
  if (version <= -1) {
    LOG(ERROR) << "Receive wrong version " << version << " from " << debug_str;
    return;
  }

  if (version < c->version) {
    // some outdated data
    LOG(INFO) << "Receive member count of " << chat_id << " with version " << version << " from " << debug_str
              << ", but current version is " << c->version;
    return;
  }

  if (c->participant_count != participant_count) {
    if (version == c->version) {
      // version is not changed when deleted user is removed from the chat
      LOG_IF(ERROR, c->participant_count != participant_count + 1)
          << "Member count of " << chat_id << " has changed from " << c->participant_count << " to "
          << participant_count << ", but version " << c->version << " remains unchanged in " << debug_str;
      repair_chat_participants(chat_id);
    }

    c->participant_count = participant_count;
    c->version = version;
    c->need_send_update = true;
    return;
  }

  if (version > c->version) {
    c->version = version;
    c->is_changed = true;
  }
}

void ContactsManager::on_update_chat_photo(Chat *c, ChatId chat_id,
                                           tl_object_ptr<telegram_api::ChatPhoto> &&chat_photo_ptr) {
  DialogPhoto new_chat_photo = get_dialog_photo(td_->file_manager_.get(), std::move(chat_photo_ptr));

  if (new_chat_photo != c->photo) {
    c->photo = new_chat_photo;
    c->is_photo_changed = true;
    c->is_changed = true;
  }
}

void ContactsManager::on_update_chat_title(Chat *c, ChatId chat_id, string &&title) {
  if (c->title != title) {
    c->title = std::move(title);
    c->is_title_changed = true;
    c->is_changed = true;
  }
}

void ContactsManager::on_update_chat_active(Chat *c, ChatId chat_id, bool is_active) {
  if (c->is_active != is_active) {
    c->is_active = is_active;
    c->need_send_update = true;
  }
}

void ContactsManager::on_update_chat_migrated_to_channel_id(Chat *c, ChatId chat_id, ChannelId migrated_to_channel_id) {
  if (c->migrated_to_channel_id != migrated_to_channel_id && migrated_to_channel_id.is_valid()) {
    LOG_IF(ERROR, c->migrated_to_channel_id.is_valid())
        << "Group upgraded to supergroup has changed from " << c->migrated_to_channel_id << " to "
        << migrated_to_channel_id;
    c->migrated_to_channel_id = migrated_to_channel_id;
    c->need_send_update = true;
  }
}

bool ContactsManager::on_update_chat_full_participants_short(ChatFull *chat_full, ChatId chat_id, int32 version) {
  if (version <= -1) {
    LOG(ERROR) << "Receive wrong version " << version << " for " << chat_id;
    return false;
  }
  if (chat_full->version == -1) {
    // chat members are unknown, nothing to update
    return false;
  }

  if (chat_full->version + 1 == version) {
    chat_full->version = version;
    return true;
  }

  LOG(ERROR) << "Member count of " << chat_id << " with version " << chat_full->version
             << " has changed but new version is " << version;
  repair_chat_participants(chat_id);
  return false;
}

void ContactsManager::on_update_chat_full_participants(ChatFull *chat_full, ChatId chat_id,
                                                       vector<DialogParticipant> participants, int32 version) {
  if (version <= -1) {
    LOG(ERROR) << "Receive members with wrong version " << version;
    return;
  }

  if (version < chat_full->version) {
    // some outdated data
    LOG(WARNING) << "Receive members of " << chat_id << " with version " << version << " but current version is "
                 << chat_full->version;
    return;
  }

  if (chat_full->participants.size() != participants.size() && version == chat_full->version) {
    LOG(INFO) << "Members of " << chat_id << " has changed";
    // this is possible in very rare situations
    repair_chat_participants(chat_id);
  }

  chat_full->participants = std::move(participants);
  chat_full->version = version;
  chat_full->is_changed = true;
}

void ContactsManager::invalidate_chat_full(ChatId chat_id) {
  ChatFull *chat_full = get_chat_full(chat_id);
  if (chat_full == nullptr) {
    return;
  }

  LOG(INFO) << "Invalidate groupFull of " << chat_id;
  //chat_full->creator_user_id = UserId();
  chat_full->participants.clear();
  chat_full->version = -1;
  update_invite_link(chat_full->invite_link, nullptr);
  chat_full->is_changed = true;
  update_chat_full(chat_full, chat_id);
}

void ContactsManager::on_update_channel_photo(Channel *c, ChannelId channel_id,
                                              tl_object_ptr<telegram_api::ChatPhoto> &&chat_photo_ptr) {
  DialogPhoto new_chat_photo = get_dialog_photo(td_->file_manager_.get(), std::move(chat_photo_ptr));

  if (new_chat_photo != c->photo) {
    c->photo = new_chat_photo;
    c->is_photo_changed = true;
    c->is_changed = true;
  }
}

void ContactsManager::on_update_channel_title(Channel *c, ChannelId channel_id, string &&title) {
  if (c->title != title) {
    c->title = std::move(title);
    c->is_title_changed = true;
    c->is_changed = true;
  }
}

void ContactsManager::on_update_channel_status(Channel *c, ChannelId channel_id, DialogParticipantStatus &&status) {
  if (c->status != status) {
    LOG(INFO) << "Update " << channel_id << " status from " << c->status << " to " << status;
    c->status = status;
    c->is_status_changed = true;
    c->need_send_update = true;
    invalidate_channel_full(channel_id);
  }
}

void ContactsManager::on_update_channel_username(ChannelId channel_id, string &&username) {
  if (!channel_id.is_valid()) {
    LOG(ERROR) << "Receive invalid " << channel_id;
    return;
  }

  Channel *c = get_channel_force(channel_id);
  if (c != nullptr) {
    on_update_channel_username(c, channel_id, std::move(username));
    update_channel(c, channel_id);
  } else {
    LOG(ERROR) << "Ignore update channel username about unknown " << channel_id;
  }
}

void ContactsManager::on_update_channel_username(Channel *c, ChannelId channel_id, string &&username) {
  td_->messages_manager_->on_dialog_username_updated(DialogId(channel_id), c->username, username);
  if (c->username != username) {
    if (c->username.empty() || username.empty()) {
      // moving channel from private to public can change availability of chat members
      invalidate_channel_full(channel_id);
    }

    c->username = std::move(username);
    c->is_username_changed = true;
    c->need_send_update = true;
  }
}

void ContactsManager::on_update_channel_full_pinned_message(ChannelFull *channel_full, MessageId message_id) {
  if (!message_id.is_valid() && message_id != MessageId()) {
    LOG(ERROR) << "Receive " << message_id << " as pinned message";
    return;
  }
  CHECK(channel_full != nullptr);

  if (channel_full->pinned_message_id != message_id) {
    channel_full->pinned_message_id = message_id;
    channel_full->is_changed = true;
  }
}

void ContactsManager::on_update_channel_description(ChannelId channel_id, string &&description) {
  if (!channel_id.is_valid()) {
    LOG(ERROR) << "Receive invalid " << channel_id;
    return;
  }

  auto channel_full = get_channel_full(channel_id);
  if (channel_full == nullptr) {
    return;
  }
  if (channel_full->description != description) {
    channel_full->description = std::move(description);
    channel_full->is_changed = true;
    update_channel_full(channel_full, channel_id);
  }
}

void ContactsManager::on_update_channel_sticker_set(ChannelId channel_id, int64 sticker_set_id) {
  if (!channel_id.is_valid()) {
    LOG(ERROR) << "Receive invalid " << channel_id;
    return;
  }

  auto channel_full = get_channel_full(channel_id);
  if (channel_full == nullptr) {
    return;
  }
  if (channel_full->sticker_set_id != sticker_set_id) {
    channel_full->sticker_set_id = sticker_set_id;
    channel_full->is_changed = true;
    update_channel_full(channel_full, channel_id);
  }
}

void ContactsManager::on_update_channel_pinned_message(ChannelId channel_id, MessageId message_id) {
  if (!channel_id.is_valid()) {
    LOG(ERROR) << "Receive invalid " << channel_id;
    return;
  }

  auto channel_full = get_channel_full(channel_id);
  if (channel_full == nullptr) {
    return;
  }
  on_update_channel_full_pinned_message(channel_full, message_id);
  update_channel_full(channel_full, channel_id);
}

void ContactsManager::on_update_channel_is_all_history_available(ChannelId channel_id, bool is_all_history_available) {
  if (!channel_id.is_valid()) {
    LOG(ERROR) << "Receive invalid " << channel_id;
    return;
  }

  auto channel_full = get_channel_full(channel_id);
  if (channel_full == nullptr) {
    return;
  }
  if (channel_full->is_all_history_available != is_all_history_available) {
    channel_full->is_all_history_available = is_all_history_available;
    channel_full->is_changed = true;
    update_channel_full(channel_full, channel_id);
  }
}

void ContactsManager::update_contacts_hints(const User *u, UserId user_id, bool from_database) {
  bool is_contact = u->outbound == LinkState::Contact && user_id != get_my_id("update_contacts_hints");
  if (td_->auth_manager_->is_bot()) {
    LOG_IF(ERROR, is_contact) << "Bot has " << user_id << " in the contacts list";
    return;
  }

  int64 key = user_id.get();
  string old_value = contacts_hints_.key_to_string(key);
  string new_value = is_contact ? u->first_name + " " + u->last_name + " " + u->username : "";

  if (new_value != old_value) {
    if (is_contact) {
      contacts_hints_.add(key, new_value);
    } else {
      contacts_hints_.remove(key);
    }
  }

  if (G()->parameters().use_chat_info_db) {
    // update contacts database
    if (!are_contacts_loaded_) {
      if (!from_database && load_contacts_queries_.empty()) {
        search_contacts("", std::numeric_limits<int32>::max(), Auto());
      }
    } else {
      if (old_value.empty() == is_contact) {
        save_contacts_to_database();
      }
    }
  }
}

bool ContactsManager::have_user(UserId user_id) const {
  auto u = get_user(user_id);
  return u != nullptr && u->is_received;
}

bool ContactsManager::have_min_user(UserId user_id) const {
  return users_.count(user_id) > 0;
}

bool ContactsManager::is_user_deleted(UserId user_id) const {
  auto u = get_user(user_id);
  return u == nullptr || u->is_deleted;
}

bool ContactsManager::is_user_bot(UserId user_id) const {
  auto u = get_user(user_id);
  return u != nullptr && !u->is_deleted && u->is_bot;
}

Result<BotData> ContactsManager::get_bot_data(UserId user_id) const {
  auto p = users_.find(user_id);
  if (p == users_.end()) {
    return Status::Error(5, "Bot not found");
  }

  auto bot = &p->second;
  if (!bot->is_bot) {
    return Status::Error(5, "User is not a bot");
  }
  if (bot->is_deleted) {
    return Status::Error(5, "Bot is deleted");
  }
  if (!bot->is_received) {
    return Status::Error(5, "Bot is inaccessible");
  }

  BotData bot_data;
  bot_data.username = bot->username;
  bot_data.can_join_groups = bot->can_join_groups;
  bot_data.can_read_all_group_messages = bot->can_read_all_group_messages;
  bot_data.is_inline = bot->is_inline_bot;
  bot_data.need_location = bot->need_location_bot;
  return bot_data;
}

const ContactsManager::User *ContactsManager::get_user(UserId user_id) const {
  auto p = users_.find(user_id);
  if (p == users_.end()) {
    return nullptr;
  } else {
    return &p->second;
  }
}

ContactsManager::User *ContactsManager::get_user(UserId user_id) {
  auto p = users_.find(user_id);
  if (p == users_.end()) {
    return nullptr;
  } else {
    return &p->second;
  }
}

void ContactsManager::send_get_me_query(Td *td, Promise<Unit> &&promise) {
  vector<tl_object_ptr<telegram_api::InputUser>> users;
  users.push_back(make_tl_object<telegram_api::inputUserSelf>());
  td->create_handler<GetUsersQuery>(std::move(promise))->send(std::move(users));
}

UserId ContactsManager::get_me(Promise<Unit> &&promise) {
  auto my_id = get_my_id("get_me");
  if (!have_user_force(my_id)) {
    send_get_me_query(td_, std::move(promise));
    return UserId();
  }

  promise.set_value(Unit());
  return my_id;
}

bool ContactsManager::get_user(UserId user_id, int left_tries, Promise<Unit> &&promise) {
  if (!user_id.is_valid()) {
    promise.set_error(Status::Error(6, "Invalid user id"));
    return false;
  }

  if (user_id == UserId(777000)) {
    get_user_force(user_id);  // preload 777000 synchronously
  }

  // TODO support loading user from database and merging it with min-user in memory
  if (!have_min_user(user_id)) {
    // TODO UserLoader
    if (left_tries > 2 && G()->parameters().use_chat_info_db) {
      send_closure_later(actor_id(this), &ContactsManager::load_user_from_database, nullptr, user_id,
                         std::move(promise));
      return false;
    }
    auto input_user = get_input_user(user_id);
    if (left_tries == 1 || input_user == nullptr) {
      promise.set_error(Status::Error(6, "User not found"));
      return false;
    }

    vector<tl_object_ptr<telegram_api::InputUser>> users;
    users.push_back(std::move(input_user));
    td_->create_handler<GetUsersQuery>(std::move(promise))->send(std::move(users));
    return false;
  }

  promise.set_value(Unit());
  return true;
}

ContactsManager::User *ContactsManager::add_user(UserId user_id, const char *source) {
  CHECK(user_id.is_valid());
  User *u = &users_[user_id];
  if (u->debug_source == nullptr) {
    u->debug_source = source;
  }
  return u;
}

const ContactsManager::UserFull *ContactsManager::get_user_full(UserId user_id) const {
  auto p = users_full_.find(user_id);
  if (p == users_full_.end()) {
    return nullptr;
  } else {
    return &p->second;
  }
}

ContactsManager::UserFull *ContactsManager::get_user_full(UserId user_id) {
  auto p = users_full_.find(user_id);
  if (p == users_full_.end()) {
    return nullptr;
  } else {
    return &p->second;
  }
}

bool ContactsManager::get_user_full(UserId user_id, Promise<Unit> &&promise) {
  auto user = get_user(user_id);
  if (user == nullptr) {
    promise.set_error(Status::Error(6, "User not found"));
    return false;
  }

  auto user_full = get_user_full(user_id);
  if (user_full == nullptr || !user_full->is_inited) {
    auto input_user = get_input_user(user_id);
    if (input_user == nullptr) {
      promise.set_error(Status::Error(6, "Can't get info about unaccessible user"));
      return false;
    }

    send_get_user_full_query(user_id, std::move(input_user), std::move(promise));
    return false;
  }
  if (user_full->is_expired() || user_full->is_bot_info_expired(user->bot_info_version)) {
    auto input_user = get_input_user(user_id);
    CHECK(input_user != nullptr);
    if (td_->auth_manager_->is_bot()) {
      send_get_user_full_query(user_id, std::move(input_user), std::move(promise));
      return false;
    } else {
      send_get_user_full_query(user_id, std::move(input_user), Auto());
    }
  }

  promise.set_value(Unit());
  return true;
}

void ContactsManager::send_get_user_full_query(UserId user_id, tl_object_ptr<telegram_api::InputUser> &&input_user,
                                               Promise<Unit> &&promise) {
  auto &promises = get_user_full_queries_[user_id];
  promises.push_back(std::move(promise));
  if (promises.size() != 1) {
    // query has already been sent, just wait for the result
    return;
  }

  td_->create_handler<GetFullUserQuery>()->send(user_id, std::move(input_user));
}

void ContactsManager::on_get_user_full_success(UserId user_id) {
  auto it = get_user_full_queries_.find(user_id);
  CHECK(it != get_user_full_queries_.end());
  CHECK(!it->second.empty());
  auto promises = std::move(it->second);
  get_user_full_queries_.erase(it);

  for (auto &promise : promises) {
    promise.set_value(Unit());
  }
}

void ContactsManager::on_get_user_full_fail(UserId user_id, Status &&error) {
  auto it = get_user_full_queries_.find(user_id);
  CHECK(it != get_user_full_queries_.end());
  CHECK(!it->second.empty());
  auto promises = std::move(it->second);
  get_user_full_queries_.erase(it);

  for (auto &promise : promises) {
    promise.set_error(error.clone());
  }
}

std::pair<int32, vector<const Photo *>> ContactsManager::get_user_profile_photos(UserId user_id, int32 offset,
                                                                                 int32 limit, Promise<Unit> &&promise) {
  std::pair<int32, vector<const Photo *>> result;
  result.first = -1;

  if (offset < 0) {
    promise.set_error(Status::Error(3, "Parameter offset must be non-negative"));
    return result;
  }
  if (limit <= 0) {
    promise.set_error(Status::Error(3, "Parameter limit must be positive"));
    return result;
  }
  if (limit > MAX_GET_PROFILE_PHOTOS) {
    limit = MAX_GET_PROFILE_PHOTOS;
  }

  auto input_user = get_input_user(user_id);
  if (input_user == nullptr) {
    promise.set_error(Status::Error(6, "User not found"));
    return result;
  }

  auto user_full = &users_full_[user_id];
  if (user_full->getting_photos_now) {
    promise.set_error(Status::Error(400, "Request for new profile photos has already been sent"));
    return result;
  }

  if (user_full->photo_count != -1) {  // know photo count
    CHECK(user_full->photos_offset != -1);
    result.first = user_full->photo_count;

    if (offset >= user_full->photo_count) {
      // offset if too big
      promise.set_value(Unit());
      return result;
    }

    if (limit > user_full->photo_count - offset) {
      limit = user_full->photo_count - offset;
    }

    int32 cache_begin = user_full->photos_offset;
    int32 cache_end = cache_begin + narrow_cast<int32>(user_full->photos.size());
    if (cache_begin <= offset && offset + limit <= cache_end) {
      // answer query from cache
      for (int i = 0; i < limit; i++) {
        result.second.push_back(&user_full->photos[i + offset - cache_begin]);
      }
      promise.set_value(Unit());
      return result;
    }

    if (cache_begin <= offset && offset < cache_end) {
      // adjust offset to the end of cache
      limit = offset + limit - cache_end;
      offset = cache_end;
    }
  }

  user_full->getting_photos_now = true;

  if (limit < MAX_GET_PROFILE_PHOTOS / 5) {
    limit = MAX_GET_PROFILE_PHOTOS / 5;  // make limit reasonable
  }

  td_->create_handler<GetUserPhotosQuery>(std::move(promise))->send(user_id, std::move(input_user), offset, limit);
  return result;
}

bool ContactsManager::have_chat(ChatId chat_id) const {
  return chats_.count(chat_id) > 0;
}

const ContactsManager::Chat *ContactsManager::get_chat(ChatId chat_id) const {
  auto p = chats_.find(chat_id);
  if (p == chats_.end()) {
    return nullptr;
  } else {
    return &p->second;
  }
}

ContactsManager::Chat *ContactsManager::get_chat(ChatId chat_id) {
  auto p = chats_.find(chat_id);
  if (p == chats_.end()) {
    return nullptr;
  } else {
    return &p->second;
  }
}

ContactsManager::Chat *ContactsManager::add_chat(ChatId chat_id) {
  CHECK(chat_id.is_valid());
  return &chats_[chat_id];
}

bool ContactsManager::get_chat(ChatId chat_id, int left_tries, Promise<Unit> &&promise) {
  if (!chat_id.is_valid()) {
    promise.set_error(Status::Error(6, "Invalid basic group id"));
    return false;
  }

  if (!have_chat(chat_id)) {
    if (left_tries > 2 && G()->parameters().use_chat_info_db) {
      send_closure_later(actor_id(this), &ContactsManager::load_chat_from_database, nullptr, chat_id,
                         std::move(promise));
      return false;
    }

    if (left_tries > 1) {
      td_->create_handler<GetChatsQuery>(std::move(promise))->send(vector<int32>{chat_id.get()});
      return false;
    }

    promise.set_error(Status::Error(6, "Group not found"));
    return false;
  }

  promise.set_value(Unit());
  return true;
}

const ContactsManager::ChatFull *ContactsManager::get_chat_full(ChatId chat_id) const {
  auto p = chats_full_.find(chat_id);
  if (p == chats_full_.end()) {
    return nullptr;
  } else {
    return &p->second;
  }
}

ContactsManager::ChatFull *ContactsManager::get_chat_full(ChatId chat_id) {
  auto p = chats_full_.find(chat_id);
  if (p == chats_full_.end()) {
    return nullptr;
  } else {
    return &p->second;
  }
}

bool ContactsManager::is_chat_full_outdated(ChatFull *chat_full, Chat *c, ChatId chat_id) {
  CHECK(c != nullptr);
  CHECK(chat_full != nullptr);
  if (chat_full->version != c->version) {
    LOG(INFO) << "Have outdated ChatFull " << chat_id << " with current version "
              << (chat_full ? chat_full->version : -123456789) << " and chat version " << c->version;
    return true;
  }

  for (auto &participant : chat_full->participants) {
    auto user = get_user(participant.user_id);
    if (user != nullptr && user->bot_info_version != -1) {
      auto user_full = get_user_full(participant.user_id);
      if (user_full == nullptr || user_full->is_bot_info_expired(user->bot_info_version)) {
        LOG(INFO) << "Have outdated botInfo for " << participant.user_id << " with version "
                  << (user_full && user_full->bot_info ? user_full->bot_info->version : -123456789)
                  << ", but current version is " << user->bot_info_version;
        return true;
      }
    }
  }

  return false;
}

bool ContactsManager::get_chat_full(ChatId chat_id, Promise<Unit> &&promise) {
  auto chat = get_chat(chat_id);
  if (chat == nullptr) {
    promise.set_error(Status::Error(6, "Group not found"));
    return false;
  }

  auto chat_full = get_chat_full(chat_id);
  if (chat_full == nullptr) {
    LOG(INFO) << "Full " << chat_id << " not found";
    send_get_chat_full_query(chat_id, std::move(promise));
    return false;
  }

  if (is_chat_full_outdated(chat_full, chat, chat_id)) {
    LOG(INFO) << "Have outdated full " << chat_id;
    if (td_->auth_manager_->is_bot()) {
      send_get_chat_full_query(chat_id, std::move(promise));
      return false;
    } else {
      send_get_chat_full_query(chat_id, Auto());
    }
  }

  promise.set_value(Unit());
  return true;
}

void ContactsManager::send_get_chat_full_query(ChatId chat_id, Promise<Unit> &&promise) {
  auto &promises = get_chat_full_queries_[chat_id];
  promises.push_back(std::move(promise));
  if (promises.size() != 1) {
    // query has already been sent, just wait for the result
    return;
  }

  td_->create_handler<GetFullChatQuery>()->send(chat_id);
}

void ContactsManager::on_get_chat_full_success(ChatId chat_id) {
  auto it = get_chat_full_queries_.find(chat_id);
  CHECK(it != get_chat_full_queries_.end());
  CHECK(!it->second.empty());
  auto promises = std::move(it->second);
  get_chat_full_queries_.erase(it);

  for (auto &promise : promises) {
    promise.set_value(Unit());
  }
}

void ContactsManager::on_get_chat_full_fail(ChatId chat_id, Status &&error) {
  auto it = get_chat_full_queries_.find(chat_id);
  CHECK(it != get_chat_full_queries_.end());
  CHECK(!it->second.empty());
  auto promises = std::move(it->second);
  get_chat_full_queries_.erase(it);

  for (auto &promise : promises) {
    promise.set_error(error.clone());
  }
}

bool ContactsManager::get_chat_is_active(ChatId chat_id) const {
  auto c = get_chat(chat_id);
  if (c == nullptr) {
    return false;
  }
  return c->is_active;
}

DialogParticipantStatus ContactsManager::get_chat_status(ChatId chat_id) const {
  auto c = get_chat(chat_id);
  if (c == nullptr) {
    return DialogParticipantStatus::Banned(0);
  }
  return get_chat_status(c);
}

DialogParticipantStatus ContactsManager::get_chat_status(const Chat *c) {
  if (c->kicked || !c->is_active) {
    return DialogParticipantStatus::Banned(0);
  }
  if (c->left) {
    return DialogParticipantStatus::Left();
  }
  if (c->is_creator) {
    return DialogParticipantStatus::Creator(true);
  }
  if (c->can_edit) {
    return DialogParticipantStatus::GroupAdministrator(false);
  }
  return DialogParticipantStatus::Member();
}

bool ContactsManager::is_appointed_chat_administrator(ChatId chat_id) const {
  auto c = get_chat(chat_id);
  if (c == nullptr) {
    return false;
  }
  if (c->everyone_is_administrator) {
    return c->is_creator;
  } else {
    return c->can_edit;
  }
}

ChannelType ContactsManager::get_channel_type(ChannelId channel_id) const {
  auto c = get_channel(channel_id);
  if (c == nullptr) {
    return ChannelType::Unknown;
  }
  return get_channel_type(c);
}

ChannelType ContactsManager::get_channel_type(const Channel *c) {
  if (c->is_megagroup) {
    return ChannelType::Megagroup;
  }
  return ChannelType::Broadcast;
}

int32 ContactsManager::get_channel_date(ChannelId channel_id) const {
  auto c = get_channel(channel_id);
  if (c == nullptr) {
    return 0;
  }
  return c->date;
}

DialogParticipantStatus ContactsManager::get_channel_status(ChannelId channel_id) const {
  auto c = get_channel(channel_id);
  if (c == nullptr) {
    return DialogParticipantStatus::Banned(0);
  }
  return get_channel_status(c);
}

DialogParticipantStatus ContactsManager::get_channel_status(const Channel *c) {
  c->status.update_restrictions();
  return c->status;
}

bool ContactsManager::get_channel_sign_messages(ChannelId channel_id) const {
  auto c = get_channel(channel_id);
  if (c == nullptr) {
    return false;
  }
  return get_channel_sign_messages(c);
}

bool ContactsManager::get_channel_sign_messages(const Channel *c) {
  return c->sign_messages;
}

bool ContactsManager::have_channel(ChannelId channel_id) const {
  return channels_.count(channel_id) > 0;
}

bool ContactsManager::have_min_channel(ChannelId channel_id) const {
  return min_channels_.count(channel_id) > 0;
}

const ContactsManager::Channel *ContactsManager::get_channel(ChannelId channel_id) const {
  auto p = channels_.find(channel_id);
  if (p == channels_.end()) {
    return nullptr;
  } else {
    return &p->second;
  }
}

ContactsManager::Channel *ContactsManager::get_channel(ChannelId channel_id) {
  auto p = channels_.find(channel_id);
  if (p == channels_.end()) {
    return nullptr;
  } else {
    return &p->second;
  }
}

ContactsManager::Channel *ContactsManager::add_channel(ChannelId channel_id, const char *source) {
  CHECK(channel_id.is_valid());
  Channel *c = &channels_[channel_id];
  if (c->debug_source == nullptr) {
    c->debug_source = source;
  }
  return c;
}

bool ContactsManager::get_channel(ChannelId channel_id, int left_tries, Promise<Unit> &&promise) {
  if (!channel_id.is_valid()) {
    promise.set_error(Status::Error(6, "Invalid supergroup id"));
    return false;
  }

  if (!have_channel(channel_id)) {
    if (left_tries > 2 && G()->parameters().use_chat_info_db) {
      send_closure_later(actor_id(this), &ContactsManager::load_channel_from_database, nullptr, channel_id,
                         std::move(promise));
      return false;
    }

    if (left_tries > 1 && td_->auth_manager_->is_bot()) {
      td_->create_handler<GetChannelsQuery>(std::move(promise))->send(get_input_channel(channel_id));
      return false;
    }

    promise.set_error(Status::Error(6, "Supergroup not found"));
    return false;
  }

  promise.set_value(Unit());
  return true;
}

const ContactsManager::ChannelFull *ContactsManager::get_channel_full(ChannelId channel_id) const {
  auto p = channels_full_.find(channel_id);
  if (p == channels_full_.end()) {
    return nullptr;
  } else {
    return &p->second;
  }
}

ContactsManager::ChannelFull *ContactsManager::get_channel_full(ChannelId channel_id) {
  auto p = channels_full_.find(channel_id);
  if (p == channels_full_.end()) {
    return nullptr;
  }

  auto channel_full = &p->second;
  if (channel_full->is_expired()) {
    auto input_channel = get_input_channel(channel_id);
    CHECK(input_channel != nullptr);
    send_get_channel_full_query(channel_id, std::move(input_channel), Auto());
  }

  return channel_full;
}

bool ContactsManager::get_channel_full(ChannelId channel_id, Promise<Unit> &&promise) {
  auto channel_full = get_channel_full(channel_id);
  if (channel_full == nullptr) {
    auto input_channel = get_input_channel(channel_id);
    if (input_channel == nullptr) {
      promise.set_error(Status::Error(6, "Supergroup not found"));
      return false;
    }

    send_get_channel_full_query(channel_id, std::move(input_channel), std::move(promise));
    return false;
  }
  if (channel_full->is_expired()) {
    if (td_->auth_manager_->is_bot()) {
      auto input_channel = get_input_channel(channel_id);
      CHECK(input_channel != nullptr);
      send_get_channel_full_query(channel_id, std::move(input_channel), std::move(promise));
      return false;
    } else {
      // request has already been sent in get_channel_full
      // send_get_channel_full_query(channel_id, std::move(input_channel), Auto());
    }
  }

  promise.set_value(Unit());
  return true;
}

void ContactsManager::send_get_channel_full_query(ChannelId channel_id,
                                                  tl_object_ptr<telegram_api::InputChannel> &&input_channel,
                                                  Promise<Unit> &&promise) {
  auto &promises = get_channel_full_queries_[channel_id];
  promises.push_back(std::move(promise));
  if (promises.size() != 1) {
    // query has already been sent, just wait for the result
    return;
  }

  td_->create_handler<GetFullChannelQuery>()->send(channel_id, std::move(input_channel));
}

void ContactsManager::on_get_channel_full_success(ChannelId channel_id) {
  auto it = get_channel_full_queries_.find(channel_id);
  CHECK(it != get_channel_full_queries_.end());
  CHECK(!it->second.empty());
  auto promises = std::move(it->second);
  get_channel_full_queries_.erase(it);

  for (auto &promise : promises) {
    promise.set_value(Unit());
  }
}

void ContactsManager::on_get_channel_full_fail(ChannelId channel_id, Status &&error) {
  auto it = get_channel_full_queries_.find(channel_id);
  CHECK(it != get_channel_full_queries_.end());
  CHECK(!it->second.empty());
  auto promises = std::move(it->second);
  get_channel_full_queries_.erase(it);

  for (auto &promise : promises) {
    promise.set_error(error.clone());
  }
}

bool ContactsManager::have_secret_chat(SecretChatId secret_chat_id) const {
  return secret_chats_.count(secret_chat_id) > 0;
}

ContactsManager::SecretChat *ContactsManager::add_secret_chat(SecretChatId secret_chat_id) {
  CHECK(secret_chat_id.is_valid());
  return &secret_chats_[secret_chat_id];
}

const ContactsManager::SecretChat *ContactsManager::get_secret_chat(SecretChatId secret_chat_id) const {
  auto it = secret_chats_.find(secret_chat_id);
  if (it == secret_chats_.end()) {
    return nullptr;
  }
  return &it->second;
}

ContactsManager::SecretChat *ContactsManager::get_secret_chat(SecretChatId secret_chat_id) {
  auto it = secret_chats_.find(secret_chat_id);
  if (it == secret_chats_.end()) {
    return nullptr;
  }
  return &it->second;
}

bool ContactsManager::get_secret_chat(SecretChatId secret_chat_id, bool force, Promise<Unit> &&promise) {
  if (!secret_chat_id.is_valid()) {
    promise.set_error(Status::Error(6, "Invalid secret chat id"));
    return false;
  }

  if (!have_secret_chat(secret_chat_id)) {
    if (!force && G()->parameters().use_chat_info_db) {
      send_closure_later(actor_id(this), &ContactsManager::load_secret_chat_from_database, nullptr, secret_chat_id,
                         std::move(promise));
      return false;
    }

    promise.set_error(Status::Error(6, "Secret chat not found"));
    return false;
  }

  promise.set_value(Unit());
  return true;
}

void ContactsManager::on_update_secret_chat(SecretChatId secret_chat_id, int64 access_hash, UserId user_id,
                                            SecretChatState state, bool is_outbound, int32 ttl, int32 date,
                                            string key_hash, int32 layer) {
  LOG(INFO) << "Update " << secret_chat_id << " with " << user_id << " and access_hash " << access_hash;
  auto *secret_chat = add_secret_chat(secret_chat_id);
  if (access_hash != secret_chat->access_hash) {
    secret_chat->access_hash = access_hash;
    secret_chat->is_changed = true;
  }
  if (user_id.is_valid() && user_id != secret_chat->user_id) {
    if (secret_chat->user_id.is_valid()) {
      LOG(ERROR) << "Secret chat user has changed from " << secret_chat->user_id << " to " << user_id;
      auto &old_secret_chat_ids = secret_chats_with_user_[secret_chat->user_id];
      old_secret_chat_ids.erase(std::remove(old_secret_chat_ids.begin(), old_secret_chat_ids.end(), secret_chat_id),
                                old_secret_chat_ids.end());
    }
    secret_chat->user_id = user_id;
    secret_chats_with_user_[secret_chat->user_id].push_back(secret_chat_id);
    secret_chat->need_send_update = true;
  }
  if (state != SecretChatState::Unknown && state != secret_chat->state) {
    secret_chat->state = state;
    secret_chat->need_send_update = true;
  }
  if (is_outbound != secret_chat->is_outbound) {
    secret_chat->is_outbound = is_outbound;
    secret_chat->need_send_update = true;
  }

  if (ttl != -1 && ttl != secret_chat->ttl) {
    secret_chat->ttl = ttl;
    secret_chat->need_send_update = true;
  }
  if (date != 0 && date != secret_chat->date) {
    secret_chat->date = date;
    secret_chat->is_changed = true;
  }
  if (!key_hash.empty() && key_hash != secret_chat->key_hash) {
    secret_chat->key_hash = std::move(key_hash);
    secret_chat->need_send_update = true;
  }
  if (layer != 0 && layer != secret_chat->layer) {
    secret_chat->layer = layer;
    secret_chat->need_send_update = true;
  }

  update_secret_chat(secret_chat, secret_chat_id);
}

std::pair<int32, vector<UserId>> ContactsManager::search_among_users(const vector<UserId> &user_ids,
                                                                     const string &query, int32 limit) {
  Hints hints;  // TODO cache Hints

  UserId my_user_id = get_my_id("search_among_users");
  for (auto user_id : user_ids) {
    auto u = get_user(user_id);
    if (u == nullptr) {
      continue;
    }
    hints.add(user_id.get(), u->first_name + " " + u->last_name + " " + u->username);
    auto was_online = u->was_online;
    if (user_id == my_user_id && my_was_online_local_ != 0) {
      was_online = my_was_online_local_;
    }
    hints.set_rating(user_id.get(), -was_online);
  }

  auto result = hints.search(query, limit, true);
  return {narrow_cast<int32>(result.first),
          transform(result.second, [](int64 key) { return UserId(narrow_cast<int32>(key)); })};
}

DialogParticipant ContactsManager::get_chat_participant(ChatId chat_id, UserId user_id, bool force,
                                                        Promise<Unit> &&promise) {
  LOG(INFO) << "Trying to get " << user_id << " as member of " << chat_id;
  if (force) {
    promise.set_value(Unit());
  } else if (!get_chat_full(chat_id, std::move(promise))) {
    return DialogParticipant();
  }
  // promise is already set

  auto result = get_chat_participant(chat_id, user_id);
  if (result == nullptr) {
    return {user_id, UserId(), 0, DialogParticipantStatus::Left()};
  }

  return *result;
}

std::pair<int32, vector<DialogParticipant>> ContactsManager::search_chat_participants(ChatId chat_id,
                                                                                      const string &query, int32 limit,
                                                                                      DialogParticipantsFilter filter,
                                                                                      bool force,
                                                                                      Promise<Unit> &&promise) {
  if (limit < 0) {
    promise.set_error(Status::Error(3, "Parameter limit must be non-negative"));
    return {};
  }

  if (force) {
    promise.set_value(Unit());
  } else if (!get_chat_full(chat_id, std::move(promise))) {
    return {};
  }
  // promise is already set

  auto chat_full = get_chat_full(chat_id);
  if (chat_full == nullptr) {
    return {};
  }

  auto is_dialog_participant_suitable = [this](const DialogParticipant &participant, DialogParticipantsFilter filter) {
    switch (filter) {
      case DialogParticipantsFilter::Administrators:
        return participant.status.is_administrator();
      case DialogParticipantsFilter::Members:
        return participant.status.is_member();  // should be always true
      case DialogParticipantsFilter::Restricted:
        return participant.status.is_restricted();  // should be always false
      case DialogParticipantsFilter::Banned:
        return participant.status.is_banned();  // should be always false
      case DialogParticipantsFilter::Bots:
        return is_user_bot(participant.user_id);
      default:
        UNREACHABLE();
        return false;
    }
  };

  vector<UserId> user_ids;
  for (auto &participant : chat_full->participants) {
    if (is_dialog_participant_suitable(participant, filter)) {
      user_ids.push_back(participant.user_id);
    }
  }

  int32 total_count;
  std::tie(total_count, user_ids) = search_among_users(user_ids, query, limit);
  return {total_count, transform(user_ids, [&](UserId user_id) { return *get_chat_participant(chat_full, user_id); })};
}

DialogParticipant ContactsManager::get_channel_participant(ChannelId channel_id, UserId user_id, int64 &random_id,
                                                           bool force, Promise<Unit> &&promise) {
  LOG(INFO) << "Trying to get " << user_id << " as member of " << channel_id;
  if (random_id != 0) {
    // request has already been sent before
    auto it = received_channel_participant_.find(random_id);
    CHECK(it != received_channel_participant_.end());
    auto result = std::move(it->second);
    received_channel_participant_.erase(it);
    promise.set_value(Unit());
    return result;
  }

  auto input_user = get_input_user(user_id);
  if (input_user == nullptr) {
    promise.set_error(Status::Error(6, "User not found"));
    return DialogParticipant();
  }

  if (!td_->auth_manager_->is_bot() && is_user_bot(user_id)) {
    // get BotInfo through UserFull
    auto user = get_user(user_id);
    auto user_full = get_user_full(user_id);
    if (user_full == nullptr || user_full->is_bot_info_expired(user->bot_info_version)) {
      if (force) {
        LOG(ERROR) << "Can't find cached UserFull";
      } else {
        send_get_user_full_query(user_id, std::move(input_user), std::move(promise));
        return DialogParticipant();
      }
    }
  }

  do {
    random_id = Random::secure_int64();
  } while (random_id == 0 || received_channel_participant_.find(random_id) != received_channel_participant_.end());
  received_channel_participant_[random_id];  // reserve place for result

  LOG(DEBUG) << "Get info about " << user_id << " membership in the " << channel_id;

  auto on_result_promise = PromiseCreator::lambda(
      [this, random_id, promise = std::move(promise)](Result<DialogParticipant> r_dialog_participant) mutable {
        // ResultHandlers are cleared before managers, so it is safe to capture this
        auto it = received_channel_participant_.find(random_id);
        CHECK(it != received_channel_participant_.end());

        if (r_dialog_participant.is_error()) {
          received_channel_participant_.erase(it);
          promise.set_error(r_dialog_participant.move_as_error());
        } else {
          it->second = r_dialog_participant.move_as_ok();
          promise.set_value(Unit());
        }
      });

  td_->create_handler<GetChannelParticipantQuery>(std::move(on_result_promise))
      ->send(channel_id, user_id, std::move(input_user));
  return DialogParticipant();
}

std::pair<int32, vector<DialogParticipant>> ContactsManager::get_channel_participants(
    ChannelId channel_id, const tl_object_ptr<td_api::SupergroupMembersFilter> &filter, const string &additional_query,
    int32 offset, int32 limit, int32 additional_limit, int64 &random_id, bool force, Promise<Unit> &&promise) {
  if (random_id != 0) {
    // request has already been sent before
    auto it = received_channel_participants_.find(random_id);
    CHECK(it != received_channel_participants_.end());
    auto result = std::move(it->second);
    received_channel_participants_.erase(it);
    promise.set_value(Unit());

    if (additional_query.empty()) {
      return result;
    }

    auto user_ids = transform(result.second, [](const auto &participant) { return participant.user_id; });
    std::pair<int32, vector<UserId>> result_user_ids = search_among_users(user_ids, additional_query, additional_limit);

    result.first = result_user_ids.first;
    std::unordered_set<UserId, UserIdHash> result_user_ids_set(result_user_ids.second.begin(),
                                                               result_user_ids.second.end());
    auto all_participants = std::move(result.second);
    result.second.clear();
    for (auto &participant : all_participants) {
      if (result_user_ids_set.count(participant.user_id)) {
        result.second.push_back(std::move(participant));
        result_user_ids_set.erase(participant.user_id);
      }
    }
    return result;
  }

  std::pair<int32, vector<DialogParticipant>> result;
  if (limit <= 0) {
    promise.set_error(Status::Error(3, "Parameter limit must be positive"));
    return result;
  }

  if (offset < 0) {
    promise.set_error(Status::Error(3, "Parameter offset must be non-negative"));
    return result;
  }

  auto channel_full = get_channel_full(channel_id);
  if (channel_full == nullptr || (!force && channel_full->is_expired())) {
    if (force) {
      LOG(ERROR) << "Can't find cached ChannelFull";
    } else {
      auto input_channel = get_input_channel(channel_id);
      if (input_channel == nullptr) {
        promise.set_error(Status::Error(6, "Supergroup not found"));
      } else {
        send_get_channel_full_query(channel_id, std::move(input_channel), std::move(promise));
      }
      return result;
    }
  }

  if (channel_full != nullptr && !channel_full->is_expired() && !channel_full->can_get_participants) {
    promise.set_error(Status::Error(3, "Supergroup members are unavailable"));
    return result;
  }

  do {
    random_id = Random::secure_int64();
  } while (random_id == 0 || received_channel_participants_.find(random_id) != received_channel_participants_.end());
  received_channel_participants_[random_id];  // reserve place for result

  LOG(DEBUG) << "Get members of the " << channel_id << " with offset = " << offset << " and limit = " << limit;

  td_->create_handler<GetChannelParticipantsQuery>(std::move(promise))
      ->send(channel_id, ChannelParticipantsFilter(filter), offset, limit, random_id);
  return result;
}

vector<UserId> ContactsManager::get_dialog_administrators(DialogId dialog_id, int left_tries, Promise<Unit> &&promise) {
  auto it = dialog_administrators_.find(dialog_id);
  if (it != dialog_administrators_.end()) {
    promise.set_value(Unit());
    if (left_tries >= 2) {
      auto hash =
          get_vector_hash(transform(it->second, [](UserId user_id) { return static_cast<uint32>(user_id.get()); }));
      reload_dialog_administrators(dialog_id, hash, Auto());  // update administrators cache
    }
    return it->second;
  }

  if (left_tries >= 3) {
    load_dialog_administrators(dialog_id, std::move(promise));
    return {};
  }

  if (left_tries >= 2) {
    reload_dialog_administrators(dialog_id, 0, std::move(promise));
    return {};
  }

  LOG(ERROR) << "Have no known administrators in " << dialog_id;
  promise.set_value(Unit());
  return {};
}

string ContactsManager::get_dialog_administrators_database_key(DialogId dialog_id) {
  return PSTRING() << "admin" << (-dialog_id.get());
}

void ContactsManager::load_dialog_administrators(DialogId dialog_id, Promise<Unit> &&promise) {
  if (G()->parameters().use_chat_info_db) {
    LOG(INFO) << "Load administrators of " << dialog_id << " from database";
    G()->td_db()->get_sqlite_pmc()->get(
        get_dialog_administrators_database_key(dialog_id),
        PromiseCreator::lambda([dialog_id, promise = std::move(promise)](string value) mutable {
          send_closure(G()->contacts_manager(), &ContactsManager::on_load_dialog_administrators_from_database,
                       dialog_id, std::move(value), std::move(promise));
        }));
  } else {
    promise.set_value(Unit());
  }
}

void ContactsManager::on_load_dialog_administrators_from_database(DialogId dialog_id, string value,
                                                                  Promise<Unit> &&promise) {
  if (value.empty()) {
    promise.set_value(Unit());
    return;
  }

  vector<UserId> user_ids;
  log_event_parse(user_ids, value).ensure();

  LOG(INFO) << "Successfully loaded " << user_ids.size() << " administrators in " << dialog_id << " from database";

  MultiPromiseActorSafe load_users_multipromise;
  load_users_multipromise.add_promise(
      PromiseCreator::lambda([dialog_id, user_ids, promise = std::move(promise)](Result<> result) mutable {
        send_closure(G()->contacts_manager(), &ContactsManager::on_load_administrator_users_finished, dialog_id,
                     std::move(user_ids), std::move(result), std::move(promise));
      }));

  auto lock_promise = load_users_multipromise.get_promise();

  for (auto user_id : user_ids) {
    get_user(user_id, 3, load_users_multipromise.get_promise());
  }

  lock_promise.set_value(Unit());
}

void ContactsManager::on_load_administrator_users_finished(DialogId dialog_id, vector<UserId> user_ids, Result<> result,
                                                           Promise<Unit> promise) {
  if (result.is_ok()) {
    dialog_administrators_.emplace(dialog_id, std::move(user_ids));
  }
  promise.set_value(Unit());
}

void ContactsManager::on_update_dialog_administrators(DialogId dialog_id, vector<UserId> administrator_user_ids,
                                                      bool have_access) {
  LOG(INFO) << "Update administrators in " << dialog_id << " to " << format::as_array(administrator_user_ids);
  if (have_access) {
    std::sort(administrator_user_ids.begin(), administrator_user_ids.end(),
              [](UserId lhs, UserId rhs) { return lhs.get() < rhs.get(); });

    auto it = dialog_administrators_.find(dialog_id);
    if (it != dialog_administrators_.end()) {
      if (it->second == administrator_user_ids) {
        return;
      }
      it->second = std::move(administrator_user_ids);
    } else {
      it = dialog_administrators_.emplace(dialog_id, std::move(administrator_user_ids)).first;
    }

    if (G()->parameters().use_chat_info_db) {
      LOG(INFO) << "Save administrators of " << dialog_id << " to database";
      G()->td_db()->get_sqlite_pmc()->set(get_dialog_administrators_database_key(dialog_id),
                                          log_event_store(it->second).as_slice().str(), Auto());
    }
  } else {
    if (G()->parameters().use_chat_info_db) {
      G()->td_db()->get_sqlite_pmc()->erase(get_dialog_administrators_database_key(dialog_id), Auto());
    }
  }
}

void ContactsManager::reload_dialog_administrators(DialogId dialog_id, int32 hash, Promise<Unit> &&promise) {
  switch (dialog_id.get_type()) {
    case DialogType::Chat:
      get_chat_full(dialog_id.get_chat_id(), std::move(promise));
      break;
    case DialogType::Channel:
      td_->create_handler<GetChannelAdministratorsQuery>(std::move(promise))->send(dialog_id.get_channel_id(), hash);
      break;
    default:
      UNREACHABLE();
  }
}

void ContactsManager::on_chat_update(telegram_api::chatEmpty &chat) {
  ChatId chat_id(chat.id_);
  if (!chat_id.is_valid()) {
    LOG(ERROR) << "Receive invalid " << chat_id;
    return;
  }

  if (!have_chat(chat_id)) {
    LOG(ERROR) << "Have no information about " << chat_id << " but received chatEmpty";
  }
}

void ContactsManager::on_chat_update(telegram_api::chat &chat) {
  auto debug_str = oneline(to_string(chat));
  ChatId chat_id(chat.id_);
  if (!chat_id.is_valid()) {
    LOG(ERROR) << "Receive invalid " << chat_id << " in " << debug_str;
    return;
  }

  bool has_left = 0 != (chat.flags_ & CHAT_FLAG_USER_HAS_LEFT);
  bool was_kicked = 0 != (chat.flags_ & CHAT_FLAG_USER_WAS_KICKED);
  if (was_kicked) {
    LOG_IF(ERROR, has_left) << "Kicked and left in " << debug_str;  // only one of the flags can be set
    has_left = true;
  }

  bool is_creator = 0 != (chat.flags_ & CHAT_FLAG_USER_IS_CREATOR);
  bool is_administrator = 0 != (chat.flags_ & CHAT_FLAG_IS_ADMINISTRATOR);
  bool everyone_is_administrator = 0 == (chat.flags_ & CHAT_FLAG_ADMINISTRATORS_ENABLED);

  bool is_active = 0 == (chat.flags_ & CHAT_FLAG_IS_DEACTIVATED);

  ChannelId migrated_to_channel_id;
  if (chat.flags_ & CHAT_FLAG_WAS_MIGRATED) {
    switch (chat.migrated_to_->get_id()) {
      case telegram_api::inputChannelEmpty::ID: {
        LOG(ERROR) << "Receive empty upgraded to supergroup for " << chat_id << " in " << debug_str;
        break;
      }
      case telegram_api::inputChannel::ID: {
        auto input_channel = move_tl_object_as<telegram_api::inputChannel>(chat.migrated_to_);
        migrated_to_channel_id = ChannelId(input_channel->channel_id_);
        if (!have_channel(migrated_to_channel_id)) {
          if (!migrated_to_channel_id.is_valid()) {
            LOG(ERROR) << "Receive invalid " << migrated_to_channel_id << " in " << debug_str;
          } else {
            // temporarily create the channel
            Channel *c = add_channel(migrated_to_channel_id, "on_chat_update");
            c->access_hash = input_channel->access_hash_;
            c->title = chat.title_;
            c->status = DialogParticipantStatus::Left();
            c->is_megagroup = true;

            // we definitely need to call update_channel, because client should know about every added channel
            update_channel(c, migrated_to_channel_id);

            // get info about the channel
            td_->create_handler<GetChannelsQuery>(Promise<>())->send(std::move(input_channel));
          }
        }
        break;
      }
      default:
        UNREACHABLE();
    }
  }

  Chat *c = add_chat(chat_id);
  on_update_chat_title(c, chat_id, std::move(chat.title_));
  if (!has_left) {
    on_update_chat_participant_count(c, chat_id, chat.participants_count_, chat.version_, debug_str);
  }
  if (c->date != chat.date_) {
    LOG_IF(ERROR, c->date != 0) << "Chat creation date has changed from " << c->date << " to " << chat.date_ << " in "
                                << debug_str;
    c->date = chat.date_;
    c->is_changed = true;
  }
  on_update_chat_left(c, chat_id, has_left, was_kicked);
  on_update_chat_rights(c, chat_id, is_creator, is_administrator, everyone_is_administrator);
  on_update_chat_photo(c, chat_id, std::move(chat.photo_));
  on_update_chat_active(c, chat_id, is_active);
  on_update_chat_migrated_to_channel_id(c, chat_id, migrated_to_channel_id);
  LOG_IF(INFO, !is_active && !migrated_to_channel_id.is_valid()) << chat_id << " is deactivated in " << debug_str;
  update_chat(c, chat_id);
}

void ContactsManager::on_chat_update(telegram_api::chatForbidden &chat) {
  ChatId chat_id(chat.id_);
  if (!chat_id.is_valid()) {
    LOG(ERROR) << "Receive invalid " << chat_id;
    return;
  }

  Chat *c = add_chat(chat_id);
  bool is_uninited = c->left == false && c->kicked == true;

  on_update_chat_title(c, chat_id, std::move(chat.title_));
  // chat participant count will be updated in on_update_chat_left
  // leave rights as is
  on_update_chat_photo(c, chat_id, nullptr);
  if (c->date != 0) {
    c->date = 0;  // removed in 38-th layer
    c->is_changed = true;
  }
  on_update_chat_left(c, chat_id, true, true);
  if (is_uninited) {
    on_update_chat_active(c, chat_id, true);
    on_update_chat_migrated_to_channel_id(c, chat_id, ChannelId());
  } else {
    // leave active and migrated to as is
  }
  update_chat(c, chat_id);
}

void ContactsManager::on_chat_update(telegram_api::channel &channel) {
  ChannelId channel_id(channel.id_);
  if (!channel_id.is_valid()) {
    LOG(ERROR) << "Receive invalid " << channel_id << ": " << to_string(channel);
    return;
  }

  if (channel.flags_ == 0 && channel.access_hash_ == 0 && channel.title_.empty()) {
    Channel *c = get_channel_force(channel_id);
    LOG(ERROR) << "Receive empty " << to_string(channel) << ", have "
               << to_string(get_supergroup_object(channel_id, c));
    if (c == nullptr) {
      min_channels_.insert(channel_id);
    }
    return;
  }

  bool is_min = (channel.flags_ & CHANNEL_FLAG_IS_MIN) != 0;
  bool has_access_hash = (channel.flags_ & CHANNEL_FLAG_HAS_ACCESS_HASH) != 0;
  auto access_hash = has_access_hash ? channel.access_hash_ : 0;

  bool anyone_can_invite = (channel.flags_ & CHANNEL_FLAG_ANYONE_CAN_INVITE) != 0;
  bool sign_messages = (channel.flags_ & CHANNEL_FLAG_SIGN_MESSAGES) != 0;
  bool is_megagroup = (channel.flags_ & CHANNEL_FLAG_IS_MEGAGROUP) != 0;
  bool is_verified = (channel.flags_ & CHANNEL_FLAG_IS_VERIFIED) != 0;
  string restriction_reason = std::move(channel.restriction_reason_);
  int32 participant_count =
      (channel.flags_ & CHANNEL_FLAG_HAS_PARTICIPANT_COUNT) != 0 ? channel.participants_count_ : 0;

  {
    bool is_broadcast = (channel.flags_ & CHANNEL_FLAG_IS_BROADCAST) != 0;
    LOG_IF(ERROR, is_broadcast == is_megagroup)
        << "Receive wrong channel flag is_broadcast == is_megagroup == " << is_megagroup << ": "
        << oneline(to_string(channel));
  }

  if (!is_megagroup && anyone_can_invite) {
    LOG(ERROR) << "Anyone can invite new members to the " << channel_id;
    anyone_can_invite = false;
  }

  if (is_megagroup) {
    LOG_IF(ERROR, sign_messages) << "Need to sign messages in the supergroup " << channel_id;
    sign_messages = true;
  }

  DialogParticipantStatus status = [&]() {
    bool has_left = (channel.flags_ & CHANNEL_FLAG_USER_HAS_LEFT) != 0;
    bool is_creator = (channel.flags_ & CHANNEL_FLAG_USER_IS_CREATOR) != 0;

    if (is_creator) {
      return DialogParticipantStatus::Creator(!has_left);
    } else if (channel.admin_rights_ != nullptr) {
      return get_dialog_participant_status(false, std::move(channel.admin_rights_));
    } else if (channel.banned_rights_ != nullptr) {
      return get_dialog_participant_status(!has_left, std::move(channel.banned_rights_));
    } else if (has_left) {
      return DialogParticipantStatus::Left();
    } else {
      return DialogParticipantStatus::Member();
    }
  }();

  if (is_min) {
    // TODO there can be better support for min channels
    Channel *c = get_channel_force(channel_id);
    if (c != nullptr) {
      LOG(DEBUG) << "Receive known min " << channel_id;
      on_update_channel_title(c, channel_id, std::move(channel.title_));
      on_update_channel_username(c, channel_id, std::move(channel.username_));
      on_update_channel_photo(c, channel_id, std::move(channel.photo_));

      if (c->anyone_can_invite != anyone_can_invite || c->is_megagroup != is_megagroup ||
          c->is_verified != is_verified) {
        c->anyone_can_invite = anyone_can_invite;
        c->is_megagroup = is_megagroup;
        c->is_verified = is_verified;

        c->need_send_update = true;
        invalidate_channel_full(channel_id);
      }

      update_channel(c, channel_id);
    } else {
      min_channels_.insert(channel_id);
    }
    return;
  }
  if (!has_access_hash) {
    LOG(ERROR) << "Receive non-min " << channel_id << " without access_hash";
    return;
  }

  Channel *c = add_channel(channel_id, "on_channel");
  if (c->status.is_banned()) {  // possibly uninited channel
    min_channels_.erase(channel_id);
  }
  if (c->access_hash != access_hash) {
    c->access_hash = access_hash;
    c->is_changed = true;
  }
  on_update_channel_title(c, channel_id, std::move(channel.title_));
  if (c->date != channel.date_) {
    c->date = channel.date_;
    c->need_send_update = true;
  }
  on_update_channel_photo(c, channel_id, std::move(channel.photo_));
  on_update_channel_status(c, channel_id, std::move(status));
  on_update_channel_username(c, channel_id, std::move(channel.username_));  // uses status, must be called after

  if (participant_count != 0 && participant_count != c->participant_count) {
    c->participant_count = participant_count;
    c->need_send_update = true;
  }

  if (c->anyone_can_invite != anyone_can_invite || c->sign_messages != sign_messages ||
      c->is_megagroup != is_megagroup || c->is_verified != is_verified || c->restriction_reason != restriction_reason) {
    c->anyone_can_invite = anyone_can_invite;
    c->sign_messages = sign_messages;
    c->is_megagroup = is_megagroup;
    c->is_verified = is_verified;
    c->restriction_reason = std::move(restriction_reason);

    c->need_send_update = true;
    invalidate_channel_full(channel_id);
  }

  update_channel(c, channel_id);
}

void ContactsManager::on_chat_update(telegram_api::channelForbidden &channel) {
  ChannelId channel_id(channel.id_);
  if (!channel_id.is_valid()) {
    LOG(ERROR) << "Receive invalid " << channel_id << ": " << to_string(channel);
    return;
  }

  if (channel.flags_ == 0 && channel.access_hash_ == 0 && channel.title_.empty()) {
    Channel *c = get_channel_force(channel_id);
    LOG(ERROR) << "Receive empty " << to_string(channel) << ", have "
               << to_string(get_supergroup_object(channel_id, c));
    if (c == nullptr) {
      min_channels_.insert(channel_id);
    }
    return;
  }

  Channel *c = add_channel(channel_id, "on_channel_forbidden");
  if (c->status.is_banned()) {  // possibly uninited channel
    min_channels_.erase(channel_id);
  }
  if (c->access_hash != channel.access_hash_) {
    c->access_hash = channel.access_hash_;
    c->is_changed = true;
  }
  on_update_channel_title(c, channel_id, std::move(channel.title_));
  on_update_channel_photo(c, channel_id, nullptr);
  if (c->date != 0) {
    c->date = 0;
    c->need_send_update = true;
  }
  int32 unban_date = (channel.flags_ & CHANNEL_FLAG_HAS_UNBAN_DATE) != 0 ? channel.until_date_ : 0;
  on_update_channel_status(c, channel_id, DialogParticipantStatus::Banned(unban_date));
  on_update_channel_username(c, channel_id, "");  // don't know if channel username is empty, but update it anyway

  bool anyone_can_invite = false;
  bool sign_messages = false;
  bool is_megagroup = (channel.flags_ & CHANNEL_FLAG_IS_MEGAGROUP) != 0;
  bool is_verified = false;
  string restriction_reason;

  {
    bool is_broadcast = (channel.flags_ & CHANNEL_FLAG_IS_BROADCAST) != 0;
    LOG_IF(ERROR, is_broadcast == is_megagroup)
        << "Receive wrong channel flag is_broadcast == is_megagroup == " << is_megagroup << ": "
        << oneline(to_string(channel));
  }

  if (is_megagroup) {
    sign_messages = true;
  }

  if (c->participant_count != 0) {
    c->participant_count = 0;
    c->need_send_update = true;
  }

  if (c->anyone_can_invite != anyone_can_invite || c->sign_messages != sign_messages ||
      c->is_megagroup != is_megagroup || c->is_verified != is_verified || c->restriction_reason != restriction_reason) {
    c->anyone_can_invite = anyone_can_invite;
    c->sign_messages = sign_messages;
    c->is_megagroup = is_megagroup;
    c->is_verified = is_verified;
    c->restriction_reason = std::move(restriction_reason);

    c->need_send_update = true;
    invalidate_channel_full(channel_id);
  }

  update_channel(c, channel_id);
}

void ContactsManager::on_upload_profile_photo(FileId file_id, tl_object_ptr<telegram_api::InputFile> input_file) {
  LOG(INFO) << "File " << file_id << " has been uploaded";

  auto it = uploaded_profile_photos_.find(file_id);
  CHECK(it != uploaded_profile_photos_.end());

  auto promise = std::move(it->second);

  uploaded_profile_photos_.erase(it);

  FileView file_view = td_->file_manager_->get_file_view(file_id);
  if (file_view.has_remote_location()) {
    if (file_view.remote_location().is_web()) {
      // TODO reupload
      promise.set_error(Status::Error(400, "Can't use web photo as profile photo"));
      return;
    }

    td_->create_handler<UpdateProfilePhotoQuery>(std::move(promise))
        ->send(file_view.remote_location().as_input_photo());
    return;
  }
  CHECK(input_file != nullptr);

  td_->create_handler<UploadProfilePhotoQuery>(std::move(promise))->send(file_id, std::move(input_file));
}

void ContactsManager::on_upload_profile_photo_error(FileId file_id, Status status) {
  LOG(INFO) << "File " << file_id << " has upload error " << status;
  CHECK(status.is_error());

  auto it = uploaded_profile_photos_.find(file_id);
  CHECK(it != uploaded_profile_photos_.end());

  auto promise = std::move(it->second);

  uploaded_profile_photos_.erase(it);

  promise.set_error(std::move(status));  // TODO check that status has valid error code
}

tl_object_ptr<td_api::UserStatus> ContactsManager::get_user_status_object(UserId user_id, const User *u) const {
  if (u->is_bot) {
    return make_tl_object<td_api::userStatusOnline>(std::numeric_limits<int32>::max());
  }

  int32 was_online = u->was_online;
  if (user_id == get_my_id("get_user_status_object") && my_was_online_local_ != 0) {
    was_online = my_was_online_local_;
  }
  switch (was_online) {
    case -3:
      return make_tl_object<td_api::userStatusLastMonth>();
    case -2:
      return make_tl_object<td_api::userStatusLastWeek>();
    case -1:
      return make_tl_object<td_api::userStatusRecently>();
    case 0:
      return make_tl_object<td_api::userStatusEmpty>();
    default: {
      int32 time = G()->unix_time();
      if (was_online > time) {
        return make_tl_object<td_api::userStatusOnline>(was_online);
      } else {
        return make_tl_object<td_api::userStatusOffline>(was_online);
      }
    }
  }
}

int32 ContactsManager::get_user_id_object(UserId user_id, const char *source) const {
  if (user_id.is_valid() && get_user(user_id) == nullptr && unknown_users_.count(user_id) == 0) {
    LOG(ERROR) << "Have no info about " << user_id << " from " << source;
    unknown_users_.insert(user_id);
    send_closure(G()->td(), &Td::send_update,
                 td_api::make_object<td_api::updateUser>(td_api::make_object<td_api::user>(
                     user_id.get(), "", "", "", "", td_api::make_object<td_api::userStatusEmpty>(),
                     get_profile_photo_object(td_->file_manager_.get(), nullptr),
                     get_link_state_object(LinkState::Unknown), get_link_state_object(LinkState::Unknown), false, "",
                     false, td_api::make_object<td_api::userTypeUnknown>(), "")));
  }
  return user_id.get();
}

tl_object_ptr<td_api::user> ContactsManager::get_user_object(UserId user_id) const {
  return get_user_object(user_id, get_user(user_id));
}

tl_object_ptr<td_api::user> ContactsManager::get_user_object(UserId user_id, const User *u) const {
  if (u == nullptr) {
    return nullptr;
  }
  tl_object_ptr<td_api::UserType> type;
  if (u->is_deleted) {
    type = make_tl_object<td_api::userTypeDeleted>();
  } else if (u->is_bot) {
    type = make_tl_object<td_api::userTypeBot>(u->can_join_groups, u->can_read_all_group_messages, u->is_inline_bot,
                                               u->inline_query_placeholder, u->need_location_bot);
  } else {
    type = make_tl_object<td_api::userTypeRegular>();
  }

  return make_tl_object<td_api::user>(
      user_id.get(), u->first_name, u->last_name, u->username, u->phone_number, get_user_status_object(user_id, u),
      get_profile_photo_object(td_->file_manager_.get(), &u->photo), get_link_state_object(u->outbound),
      get_link_state_object(u->inbound), u->is_verified, u->restriction_reason, u->is_received, std::move(type),
      u->language_code);
}

vector<int32> ContactsManager::get_user_ids_object(const vector<UserId> &user_ids) const {
  return transform(user_ids, [this](UserId user_id) { return get_user_id_object(user_id, "get_user_ids_object"); });
}

tl_object_ptr<td_api::users> ContactsManager::get_users_object(int32 total_count,
                                                               const vector<UserId> &user_ids) const {
  if (total_count == -1) {
    total_count = narrow_cast<int32>(user_ids.size());
  }
  return td_api::make_object<td_api::users>(total_count, get_user_ids_object(user_ids));
}

tl_object_ptr<td_api::userFullInfo> ContactsManager::get_user_full_info_object(UserId user_id) const {
  return get_user_full_info_object(user_id, get_user_full(user_id));
}

tl_object_ptr<td_api::userFullInfo> ContactsManager::get_user_full_info_object(UserId user_id,
                                                                               const UserFull *user_full) const {
  CHECK(user_full != nullptr);
  bool is_bot = is_user_bot(user_id);
  return make_tl_object<td_api::userFullInfo>(user_full->is_blocked, user_full->can_be_called,
                                              user_full->has_private_calls, is_bot ? string() : user_full->about,
                                              is_bot ? user_full->about : string(), user_full->common_chat_count,
                                              get_bot_info_object(user_full->bot_info.get()));
}

int32 ContactsManager::get_basic_group_id_object(ChatId chat_id, const char *source) const {
  if (chat_id.is_valid() && get_chat(chat_id) == nullptr && unknown_chats_.count(chat_id) == 0) {
    LOG(ERROR) << "Have no info about " << chat_id << " from " << source;
    unknown_chats_.insert(chat_id);
    send_closure(
        G()->td(), &Td::send_update,
        td_api::make_object<td_api::updateBasicGroup>(td_api::make_object<td_api::basicGroup>(
            chat_id.get(), 0, DialogParticipantStatus::Banned(0).get_chat_member_status_object(), true, true, 0)));
  }
  return chat_id.get();
}

tl_object_ptr<td_api::basicGroup> ContactsManager::get_basic_group_object(ChatId chat_id) {
  return get_basic_group_object(chat_id, get_chat(chat_id));
}

tl_object_ptr<td_api::basicGroup> ContactsManager::get_basic_group_object(ChatId chat_id, const Chat *chat) {
  if (chat == nullptr) {
    return nullptr;
  }
  if (chat->migrated_to_channel_id.is_valid()) {
    get_channel_force(chat->migrated_to_channel_id);
  }
  return make_tl_object<td_api::basicGroup>(
      chat_id.get(), chat->participant_count, get_chat_status(chat).get_chat_member_status_object(),
      chat->everyone_is_administrator, chat->is_active,
      get_supergroup_id_object(chat->migrated_to_channel_id, "get_basic_group_object"));
}

tl_object_ptr<td_api::basicGroupFullInfo> ContactsManager::get_basic_group_full_info_object(ChatId chat_id) const {
  return get_basic_group_full_info_object(get_chat_full(chat_id));
}

tl_object_ptr<td_api::basicGroupFullInfo> ContactsManager::get_basic_group_full_info_object(
    const ChatFull *chat_full) const {
  CHECK(chat_full != nullptr);
  return make_tl_object<td_api::basicGroupFullInfo>(
      get_user_id_object(chat_full->creator_user_id, "basicGroupFullInfo"),
      transform(chat_full->participants,
                [this](const DialogParticipant &chat_participant) { return get_chat_member_object(chat_participant); }),
      chat_full->invite_link);
}

int32 ContactsManager::get_supergroup_id_object(ChannelId channel_id, const char *source) const {
  if (channel_id.is_valid() && get_channel(channel_id) == nullptr && unknown_channels_.count(channel_id) == 0) {
    LOG(ERROR) << "Have no info about " << channel_id << " received from " << source;
    unknown_channels_.insert(channel_id);
    send_closure(G()->td(), &Td::send_update,
                 td_api::make_object<td_api::updateSupergroup>(td_api::make_object<td_api::supergroup>(
                     channel_id.get(), string(), 0, DialogParticipantStatus::Banned(0).get_chat_member_status_object(),
                     0, false, false, true, false, "")));
  }
  return channel_id.get();
}

tl_object_ptr<td_api::supergroup> ContactsManager::get_supergroup_object(ChannelId channel_id) const {
  return get_supergroup_object(channel_id, get_channel(channel_id));
}

tl_object_ptr<td_api::supergroup> ContactsManager::get_supergroup_object(ChannelId channel_id,
                                                                         const Channel *channel) const {
  if (channel == nullptr) {
    return nullptr;
  }
  return make_tl_object<td_api::supergroup>(
      channel_id.get(), channel->username, channel->date, get_channel_status(channel).get_chat_member_status_object(),
      channel->participant_count, channel->anyone_can_invite, channel->sign_messages, !channel->is_megagroup,
      channel->is_verified, channel->restriction_reason);
}

tl_object_ptr<td_api::supergroupFullInfo> ContactsManager::get_channel_full_info_object(ChannelId channel_id) const {
  return get_channel_full_info_object(get_channel_full(channel_id));
}

tl_object_ptr<td_api::supergroupFullInfo> ContactsManager::get_channel_full_info_object(
    const ChannelFull *channel_full) const {
  CHECK(channel_full != nullptr);
  return make_tl_object<td_api::supergroupFullInfo>(
      channel_full->description, channel_full->participant_count, channel_full->administrator_count,
      channel_full->restricted_count, channel_full->banned_count, channel_full->can_get_participants,
      channel_full->can_set_username, channel_full->can_set_sticker_set, channel_full->is_all_history_available,
      channel_full->sticker_set_id, channel_full->invite_link, channel_full->pinned_message_id.get(),
      get_basic_group_id_object(channel_full->migrated_from_chat_id, "get_channel_full_info_object"),
      channel_full->migrated_from_max_message_id.get());
}

tl_object_ptr<td_api::SecretChatState> ContactsManager::get_secret_chat_state_object(SecretChatState state) {
  switch (state) {
    case SecretChatState::Waiting:
      return make_tl_object<td_api::secretChatStatePending>();
    case SecretChatState::Active:
      return make_tl_object<td_api::secretChatStateReady>();
    case SecretChatState::Closed:
    case SecretChatState::Unknown:
      return make_tl_object<td_api::secretChatStateClosed>();
    default:
      UNREACHABLE();
      return nullptr;
  }
}

int32 ContactsManager::get_secret_chat_id_object(SecretChatId secret_chat_id, const char *source) const {
  if (secret_chat_id.is_valid() && get_secret_chat(secret_chat_id) == nullptr &&
      unknown_secret_chats_.count(secret_chat_id) == 0) {
    LOG(ERROR) << "Have no info about " << secret_chat_id << " from " << source;
    unknown_secret_chats_.insert(secret_chat_id);
    send_closure(
        G()->td(), &Td::send_update,
        td_api::make_object<td_api::updateSecretChat>(td_api::make_object<td_api::secretChat>(
            secret_chat_id.get(), 0, get_secret_chat_state_object(SecretChatState::Unknown), false, 0, string(), 0)));
  }
  return secret_chat_id.get();
}

tl_object_ptr<td_api::secretChat> ContactsManager::get_secret_chat_object(SecretChatId secret_chat_id) {
  return get_secret_chat_object(secret_chat_id, get_secret_chat(secret_chat_id));
}

tl_object_ptr<td_api::secretChat> ContactsManager::get_secret_chat_object(SecretChatId secret_chat_id,
                                                                          const SecretChat *secret_chat) {
  if (secret_chat == nullptr) {
    return nullptr;
  }
  get_user_force(secret_chat->user_id);
  return td_api::make_object<td_api::secretChat>(
      secret_chat_id.get(), get_user_id_object(secret_chat->user_id, "secretChat"),
      get_secret_chat_state_object(secret_chat->state), secret_chat->is_outbound, secret_chat->ttl,
      secret_chat->key_hash, secret_chat->layer);
}

tl_object_ptr<td_api::LinkState> ContactsManager::get_link_state_object(LinkState link) {
  switch (link) {
    case LinkState::Unknown:
    case LinkState::None:
      return make_tl_object<td_api::linkStateNone>();
    case LinkState::KnowsPhoneNumber:
      return make_tl_object<td_api::linkStateKnowsPhoneNumber>();
    case LinkState::Contact:
      return make_tl_object<td_api::linkStateIsContact>();
    default:
      UNREACHABLE();
  }
  return make_tl_object<td_api::linkStateNone>();
}

tl_object_ptr<td_api::botInfo> ContactsManager::get_bot_info_object(const BotInfo *bot_info) {
  if (bot_info == nullptr) {
    return nullptr;
  }

  vector<tl_object_ptr<td_api::botCommand>> commands;
  for (auto &command : bot_info->commands) {
    commands.push_back(make_tl_object<td_api::botCommand>(command.first, command.second));
  }

  return make_tl_object<td_api::botInfo>(bot_info->description, std::move(commands));
}

tl_object_ptr<td_api::botInfo> ContactsManager::get_bot_info_object(UserId user_id) const {
  auto user_full = get_user_full(user_id);
  if (user_full == nullptr || user_full->bot_info == nullptr) {
    return nullptr;
  }
  return get_bot_info_object(user_full->bot_info.get());
}

tl_object_ptr<td_api::chatInviteLinkInfo> ContactsManager::get_chat_invite_link_info_object(
    const string &invite_link) const {
  auto it = invite_link_infos_.find(invite_link);
  if (it == invite_link_infos_.end()) {
    return nullptr;
  }

  auto invite_link_info = it->second.get();
  CHECK(invite_link_info != nullptr);

  DialogId dialog_id;
  string title;
  const DialogPhoto *photo = nullptr;
  int32 participant_count = 0;
  vector<int32> member_user_ids;
  bool is_public = false;
  td_api::object_ptr<td_api::ChatType> chat_type;

  if (invite_link_info->chat_id != ChatId()) {
    CHECK(invite_link_info->channel_id == ChannelId());
    auto chat_id = invite_link_info->chat_id;
    const Chat *c = get_chat(chat_id);

    dialog_id = DialogId(invite_link_info->chat_id);

    if (c != nullptr) {
      title = c->title;
      photo = &c->photo;
      participant_count = c->participant_count;
    } else {
      LOG(ERROR) << "Have no information about " << chat_id;
    }
    chat_type = td_api::make_object<td_api::chatTypeBasicGroup>(
        get_basic_group_id_object(chat_id, "get_chat_invite_link_info_object"));
  } else if (invite_link_info->channel_id != ChannelId()) {
    CHECK(invite_link_info->chat_id == ChatId());
    auto channel_id = invite_link_info->channel_id;
    const Channel *c = get_channel(channel_id);

    dialog_id = DialogId(invite_link_info->channel_id);

    bool is_megagroup = false;
    if (c != nullptr) {
      title = c->title;
      photo = &c->photo;
      is_public = !c->username.empty();
      is_megagroup = c->is_megagroup;
      participant_count = c->participant_count;
    } else {
      LOG(ERROR) << "Have no information about " << channel_id;
    }
    chat_type = td_api::make_object<td_api::chatTypeSupergroup>(
        get_supergroup_id_object(channel_id, "get_chat_invite_link_info_object"), !is_megagroup);
  } else {
    title = invite_link_info->title;
    photo = &invite_link_info->photo;
    participant_count = invite_link_info->participant_count;
    member_user_ids = get_user_ids_object(invite_link_info->participant_user_ids);
    is_public = invite_link_info->is_public;

    if (invite_link_info->is_chat) {
      chat_type = td_api::make_object<td_api::chatTypeBasicGroup>(0);
    } else {
      chat_type = td_api::make_object<td_api::chatTypeSupergroup>(0, !invite_link_info->is_megagroup);
    }
  }

  if (dialog_id != DialogId()) {
    td_->messages_manager_->force_create_dialog(dialog_id, "get_chat_invite_link_info_object");
  }

  return make_tl_object<td_api::chatInviteLinkInfo>(dialog_id.get(), std::move(chat_type), title,
                                                    get_chat_photo_object(td_->file_manager_.get(), photo),
                                                    participant_count, std::move(member_user_ids), is_public);
}

UserId ContactsManager::get_support_user(Promise<Unit> &&promise) {
  if (support_user_id_.is_valid()) {
    promise.set_value(Unit());
    return support_user_id_;
  }

  td_->create_handler<GetSupportUserQuery>(std::move(promise))->send();
  return UserId();
}

}  // namespace td
