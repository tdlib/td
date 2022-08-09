//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/Account.h"

#include "td/telegram/ContactsManager.h"
#include "td/telegram/DeviceTokenManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/net/NetQueryCreator.h"
#include "td/telegram/Td.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UserId.h"

#include "td/actor/actor.h"

#include "td/utils/algorithm.h"
#include "td/utils/base64.h"
#include "td/utils/buffer.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"

#include <algorithm>

namespace td {

static td_api::object_ptr<td_api::SessionType> get_session_type_object(
    const tl_object_ptr<telegram_api::authorization> &authorization) {
  auto contains = [](const string &str, const char *substr) {
    return str.find(substr) != string::npos;
  };

  const string &app_name = authorization->app_name_;
  auto device_model = to_lower(authorization->device_model_);
  auto platform = to_lower(authorization->platform_);
  auto system_version = to_lower(authorization->system_version_);

  if (device_model.find("xbox") != string::npos) {
    return td_api::make_object<td_api::sessionTypeXbox>();
  }

  bool is_web = [&] {
    CSlice web_name("Web");
    auto pos = app_name.find(web_name.c_str());
    if (pos == string::npos) {
      return false;
    }

    auto next_character = app_name[pos + web_name.size()];
    return !('a' <= next_character && next_character <= 'z');
  }();

  if (is_web) {
    if (contains(device_model, "brave")) {
      return td_api::make_object<td_api::sessionTypeBrave>();
    } else if (contains(device_model, "vivaldi")) {
      return td_api::make_object<td_api::sessionTypeVivaldi>();
    } else if (contains(device_model, "opera") || contains(device_model, "opr")) {
      return td_api::make_object<td_api::sessionTypeOpera>();
    } else if (contains(device_model, "edg")) {
      return td_api::make_object<td_api::sessionTypeEdge>();
    } else if (contains(device_model, "chrome")) {
      return td_api::make_object<td_api::sessionTypeChrome>();
    } else if (contains(device_model, "firefox") || contains(device_model, "fxios")) {
      return td_api::make_object<td_api::sessionTypeFirefox>();
    } else if (contains(device_model, "safari")) {
      return td_api::make_object<td_api::sessionTypeSafari>();
    }
  }

  if (begins_with(platform, "android") || contains(system_version, "android")) {
    return td_api::make_object<td_api::sessionTypeAndroid>();
  } else if (begins_with(platform, "windows") || contains(system_version, "windows")) {
    return td_api::make_object<td_api::sessionTypeWindows>();
  } else if (begins_with(platform, "ubuntu") || contains(system_version, "ubuntu")) {
    return td_api::make_object<td_api::sessionTypeUbuntu>();
  } else if (begins_with(platform, "linux") || contains(system_version, "linux")) {
    return td_api::make_object<td_api::sessionTypeLinux>();
  }

  auto is_ios = begins_with(platform, "ios") || contains(system_version, "ios");
  auto is_macos = begins_with(platform, "macos") || contains(system_version, "macos");
  if (is_ios && contains(device_model, "iphone")) {
    return td_api::make_object<td_api::sessionTypeIphone>();
  } else if (is_ios && contains(device_model, "ipad")) {
    return td_api::make_object<td_api::sessionTypeIpad>();
  } else if (is_macos && contains(device_model, "mac")) {
    return td_api::make_object<td_api::sessionTypeMac>();
  } else if (is_ios || is_macos) {
    return td_api::make_object<td_api::sessionTypeApple>();
  }

  return td_api::make_object<td_api::sessionTypeUnknown>();
}

static td_api::object_ptr<td_api::session> convert_authorization_object(
    tl_object_ptr<telegram_api::authorization> &&authorization) {
  CHECK(authorization != nullptr);
  return td_api::make_object<td_api::session>(
      authorization->hash_, authorization->current_, authorization->password_pending_,
      !authorization->encrypted_requests_disabled_, !authorization->call_requests_disabled_,
      get_session_type_object(authorization), authorization->api_id_, authorization->app_name_,
      authorization->app_version_, authorization->official_app_, authorization->device_model_, authorization->platform_,
      authorization->system_version_, authorization->date_created_, authorization->date_active_, authorization->ip_,
      authorization->country_, authorization->region_);
}

class SetAccountTtlQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit SetAccountTtlQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(int32 account_ttl) {
    send_query(G()->net_query_creator().create(
        telegram_api::account_setAccountTTL(make_tl_object<telegram_api::accountDaysTTL>(account_ttl))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::account_setAccountTTL>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    bool result = result_ptr.move_as_ok();
    if (!result) {
      return on_error(Status::Error(500, "Internal Server Error: failed to set account TTL"));
    }

    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetAccountTtlQuery final : public Td::ResultHandler {
  Promise<int32> promise_;

 public:
  explicit GetAccountTtlQuery(Promise<int32> &&promise) : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create(telegram_api::account_getAccountTTL()));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::account_getAccountTTL>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetAccountTtlQuery: " << to_string(ptr);

    promise_.set_value(std::move(ptr->days_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class AcceptLoginTokenQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::session>> promise_;

 public:
  explicit AcceptLoginTokenQuery(Promise<td_api::object_ptr<td_api::session>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(const string &login_token) {
    send_query(G()->net_query_creator().create(telegram_api::auth_acceptLoginToken(BufferSlice(login_token))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::auth_acceptLoginToken>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    LOG(DEBUG) << "Receive result for AcceptLoginTokenQuery: " << to_string(result_ptr.ok());
    promise_.set_value(convert_authorization_object(result_ptr.move_as_ok()));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetAuthorizationsQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::sessions>> promise_;

 public:
  explicit GetAuthorizationsQuery(Promise<td_api::object_ptr<td_api::sessions>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create(telegram_api::account_getAuthorizations()));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::account_getAuthorizations>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetAuthorizationsQuery: " << to_string(ptr);

    auto ttl_days = ptr->authorization_ttl_days_;
    if (ttl_days <= 0 || ttl_days > 366) {
      LOG(ERROR) << "Receive invalid inactive sessions TTL " << ttl_days;
      ttl_days = 180;
    }

    auto results = td_api::make_object<td_api::sessions>(
        transform(std::move(ptr->authorizations_), convert_authorization_object), ttl_days);
    std::sort(results->sessions_.begin(), results->sessions_.end(),
              [](const td_api::object_ptr<td_api::session> &lhs, const td_api::object_ptr<td_api::session> &rhs) {
                if (lhs->is_current_ != rhs->is_current_) {
                  return lhs->is_current_;
                }
                if (lhs->is_password_pending_ != rhs->is_password_pending_) {
                  return lhs->is_password_pending_;
                }
                return lhs->last_active_date_ > rhs->last_active_date_;
              });

    promise_.set_value(std::move(results));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class ResetAuthorizationQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit ResetAuthorizationQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(int64 authorization_id) {
    send_query(G()->net_query_creator().create(telegram_api::account_resetAuthorization(authorization_id)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::account_resetAuthorization>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    bool result = result_ptr.move_as_ok();
    LOG_IF(WARNING, !result) << "Failed to terminate session";
    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class ResetAuthorizationsQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit ResetAuthorizationsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create(telegram_api::auth_resetAuthorizations()));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::auth_resetAuthorizations>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    bool result = result_ptr.move_as_ok();
    LOG_IF(WARNING, !result) << "Failed to terminate all sessions";
    send_closure(td_->device_token_manager_, &DeviceTokenManager::reregister_device);
    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class ChangeAuthorizationSettingsQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit ChangeAuthorizationSettingsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(int64 hash, bool set_encrypted_requests_disabled, bool encrypted_requests_disabled,
            bool set_call_requests_disabled, bool call_requests_disabled) {
    int32 flags = 0;
    if (set_encrypted_requests_disabled) {
      flags |= telegram_api::account_changeAuthorizationSettings::ENCRYPTED_REQUESTS_DISABLED_MASK;
    }
    if (set_call_requests_disabled) {
      flags |= telegram_api::account_changeAuthorizationSettings::CALL_REQUESTS_DISABLED_MASK;
    }
    send_query(G()->net_query_creator().create(telegram_api::account_changeAuthorizationSettings(
        flags, hash, encrypted_requests_disabled, call_requests_disabled)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::account_changeAuthorizationSettings>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    bool result = result_ptr.move_as_ok();
    LOG_IF(WARNING, !result) << "Failed to change session settings";
    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class SetAuthorizationTtlQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit SetAuthorizationTtlQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(int32 authorization_ttl_days) {
    send_query(G()->net_query_creator().create(telegram_api::account_setAuthorizationTTL(authorization_ttl_days)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::account_setAuthorizationTTL>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    bool result = result_ptr.move_as_ok();
    LOG_IF(WARNING, !result) << "Failed to set inactive session TTL";
    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetWebAuthorizationsQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::connectedWebsites>> promise_;

 public:
  explicit GetWebAuthorizationsQuery(Promise<td_api::object_ptr<td_api::connectedWebsites>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create(telegram_api::account_getWebAuthorizations()));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::account_getWebAuthorizations>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetWebAuthorizationsQuery: " << to_string(ptr);

    td_->contacts_manager_->on_get_users(std::move(ptr->users_), "GetWebAuthorizationsQuery");

    auto results = td_api::make_object<td_api::connectedWebsites>();
    results->websites_.reserve(ptr->authorizations_.size());
    for (auto &authorization : ptr->authorizations_) {
      CHECK(authorization != nullptr);
      UserId bot_user_id(authorization->bot_id_);
      if (!bot_user_id.is_valid()) {
        LOG(ERROR) << "Receive invalid bot " << bot_user_id;
        bot_user_id = UserId();
      }

      results->websites_.push_back(td_api::make_object<td_api::connectedWebsite>(
          authorization->hash_, authorization->domain_,
          td_->contacts_manager_->get_user_id_object(bot_user_id, "GetWebAuthorizationsQuery"), authorization->browser_,
          authorization->platform_, authorization->date_created_, authorization->date_active_, authorization->ip_,
          authorization->region_));
    }

    promise_.set_value(std::move(results));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class ResetWebAuthorizationQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit ResetWebAuthorizationQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(int64 hash) {
    send_query(G()->net_query_creator().create(telegram_api::account_resetWebAuthorization(hash)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::account_resetWebAuthorization>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    bool result = result_ptr.move_as_ok();
    LOG_IF(WARNING, !result) << "Failed to disconnect website";
    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class ResetWebAuthorizationsQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit ResetWebAuthorizationsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create(telegram_api::account_resetWebAuthorizations()));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::account_resetWebAuthorizations>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    bool result = result_ptr.move_as_ok();
    LOG_IF(WARNING, !result) << "Failed to disconnect all websites";
    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class SetBotGroupDefaultAdminRightsQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit SetBotGroupDefaultAdminRightsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(AdministratorRights administrator_rights) {
    send_query(G()->net_query_creator().create(
        telegram_api::bots_setBotGroupDefaultAdminRights(administrator_rights.get_chat_admin_rights())));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::bots_setBotGroupDefaultAdminRights>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    bool result = result_ptr.move_as_ok();
    LOG_IF(WARNING, !result) << "Failed to set group default administrator rights";
    td_->contacts_manager_->invalidate_user_full(td_->contacts_manager_->get_my_id());
    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    if (status.message() == "RIGHTS_NOT_MODIFIED") {
      return promise_.set_value(Unit());
    }
    td_->contacts_manager_->invalidate_user_full(td_->contacts_manager_->get_my_id());
    promise_.set_error(std::move(status));
  }
};

class SetBotBroadcastDefaultAdminRightsQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit SetBotBroadcastDefaultAdminRightsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(AdministratorRights administrator_rights) {
    send_query(G()->net_query_creator().create(
        telegram_api::bots_setBotBroadcastDefaultAdminRights(administrator_rights.get_chat_admin_rights())));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::bots_setBotBroadcastDefaultAdminRights>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    bool result = result_ptr.move_as_ok();
    LOG_IF(WARNING, !result) << "Failed to set channel default administrator rights";
    td_->contacts_manager_->invalidate_user_full(td_->contacts_manager_->get_my_id());
    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    if (status.message() == "RIGHTS_NOT_MODIFIED") {
      return promise_.set_value(Unit());
    }
    td_->contacts_manager_->invalidate_user_full(td_->contacts_manager_->get_my_id());
    promise_.set_error(std::move(status));
  }
};

void set_account_ttl(Td *td, int32 account_ttl, Promise<Unit> &&promise) {
  td->create_handler<SetAccountTtlQuery>(std::move(promise))->send(account_ttl);
}

void get_account_ttl(Td *td, Promise<int32> &&promise) {
  td->create_handler<GetAccountTtlQuery>(std::move(promise))->send();
}

void confirm_qr_code_authentication(Td *td, const string &link,
                                    Promise<td_api::object_ptr<td_api::session>> &&promise) {
  Slice prefix("tg://login?token=");
  if (!begins_with(to_lower(link), prefix)) {
    return promise.set_error(Status::Error(400, "AUTH_TOKEN_INVALID"));
  }
  auto r_token = base64url_decode(Slice(link).substr(prefix.size()));
  if (r_token.is_error()) {
    return promise.set_error(Status::Error(400, "AUTH_TOKEN_INVALID"));
  }
  td->create_handler<AcceptLoginTokenQuery>(std::move(promise))->send(r_token.ok());
}

void get_active_sessions(Td *td, Promise<td_api::object_ptr<td_api::sessions>> &&promise) {
  td->create_handler<GetAuthorizationsQuery>(std::move(promise))->send();
}

void terminate_session(Td *td, int64 session_id, Promise<Unit> &&promise) {
  td->create_handler<ResetAuthorizationQuery>(std::move(promise))->send(session_id);
}

void terminate_all_other_sessions(Td *td, Promise<Unit> &&promise) {
  td->create_handler<ResetAuthorizationsQuery>(std::move(promise))->send();
}

void toggle_session_can_accept_calls(Td *td, int64 session_id, bool can_accept_calls, Promise<Unit> &&promise) {
  td->create_handler<ChangeAuthorizationSettingsQuery>(std::move(promise))
      ->send(session_id, false, false, true, !can_accept_calls);
}

void toggle_session_can_accept_secret_chats(Td *td, int64 session_id, bool can_accept_secret_chats,
                                            Promise<Unit> &&promise) {
  td->create_handler<ChangeAuthorizationSettingsQuery>(std::move(promise))
      ->send(session_id, true, !can_accept_secret_chats, false, false);
}

void set_inactive_session_ttl_days(Td *td, int32 authorization_ttl_days, Promise<Unit> &&promise) {
  td->create_handler<SetAuthorizationTtlQuery>(std::move(promise))->send(authorization_ttl_days);
}

void get_connected_websites(Td *td, Promise<td_api::object_ptr<td_api::connectedWebsites>> &&promise) {
  td->create_handler<GetWebAuthorizationsQuery>(std::move(promise))->send();
}

void disconnect_website(Td *td, int64 website_id, Promise<Unit> &&promise) {
  td->create_handler<ResetWebAuthorizationQuery>(std::move(promise))->send(website_id);
}

void disconnect_all_websites(Td *td, Promise<Unit> &&promise) {
  td->create_handler<ResetWebAuthorizationsQuery>(std::move(promise))->send();
}

void set_default_group_administrator_rights(Td *td, AdministratorRights administrator_rights, Promise<Unit> &&promise) {
  td->contacts_manager_->invalidate_user_full(td->contacts_manager_->get_my_id());
  td->create_handler<SetBotGroupDefaultAdminRightsQuery>(std::move(promise))->send(administrator_rights);
}

void set_default_channel_administrator_rights(Td *td, AdministratorRights administrator_rights,
                                              Promise<Unit> &&promise) {
  td->contacts_manager_->invalidate_user_full(td->contacts_manager_->get_my_id());
  td->create_handler<SetBotBroadcastDefaultAdminRightsQuery>(std::move(promise))->send(administrator_rights);
}

}  // namespace td
