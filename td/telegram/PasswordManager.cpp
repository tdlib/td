//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/PasswordManager.h"

#include "td/telegram/Global.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/net/NetQueryDispatcher.h"

#include "td/utils/buffer.h"
#include "td/utils/crypto.h"
#include "td/utils/logging.h"
#include "td/utils/Random.h"
#include "td/utils/Slice.h"

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"  // TODO: this file is already included. Why?

namespace td {

tl_object_ptr<td_api::temporaryPasswordState> TempPasswordState::as_td_api() const {
  if (!has_temp_password || valid_until <= G()->unix_time()) {
    return make_tl_object<td_api::temporaryPasswordState>(false, 0);
  }
  return make_tl_object<td_api::temporaryPasswordState>(true, valid_until - G()->unix_time_cached());
}

static BufferSlice calc_password_hash(const string &password, const string &salt) {
  if (password.empty()) {
    return BufferSlice();
  }
  BufferSlice buf(32);
  string salted_password = salt + password + salt;
  sha256(salted_password, buf.as_slice());
  return buf;
}

void PasswordManager::set_password(string current_password, string new_password, string new_hint,
                                   bool set_recovery_email_address, string recovery_email_address,
                                   Promise<State> promise) {
  UpdateSettings update_settings;

  update_settings.current_password = std::move(current_password);
  update_settings.update_password = true;
  //update_settings.update_secure_secret = true;
  update_settings.new_password = std::move(new_password);
  update_settings.new_hint = std::move(new_hint);

  if (set_recovery_email_address) {
    update_settings.update_recovery_email_address = true;
    update_settings.recovery_email_address = std::move(recovery_email_address);
  }

  update_password_settings(std::move(update_settings), std::move(promise));
}
void PasswordManager::set_recovery_email_address(string password, string new_recovery_email_address,
                                                 Promise<State> promise) {
  UpdateSettings update_settings;
  update_settings.current_password = std::move(password);
  update_settings.update_recovery_email_address = true;
  update_settings.recovery_email_address = std::move(new_recovery_email_address);

  update_password_settings(std::move(update_settings), std::move(promise));
}

void PasswordManager::get_secure_secret(string password, optional<int64> hash,
                                        Promise<secure_storage::Secret> promise) {
  return do_get_secure_secret(true, std::move(password), std::move(hash), std::move(promise));
}

void PasswordManager::do_get_secure_secret(bool recursive, string password, optional<int64> hash,
                                           Promise<secure_storage::Secret> promise) {
  if (secret_ && (!hash || secret_.value().get_hash() == hash.value())) {
    return promise.set_value(secret_.value().clone());
  }
  get_full_state(
      password, PromiseCreator::lambda([password, recursive, hash = std::move(hash), promise = std::move(promise),
                                        actor_id = actor_id(this)](Result<PasswordFullState> r_state) mutable {
        if (r_state.is_error()) {
          return promise.set_error(r_state.move_as_error());
        }
        auto state = r_state.move_as_ok();
        if (!state.state.has_password) {
          return promise.set_error(Status::Error(400, "2fa is off"));
        }
        if (state.private_state.secret) {
          send_closure(actor_id, &PasswordManager::cache_secret, state.private_state.secret.value().clone());
          return promise.set_value(std::move(state.private_state.secret.value()));
        }
        if (!recursive) {
          return promise.set_error(Status::Error(400, "Failed to get secure secret"));
        }

        auto new_promise =
            PromiseCreator::lambda([recursive, password, hash = std::move(hash), promise = std::move(promise),
                                    actor_id = actor_id](Result<bool> r_ok) mutable {
              if (r_ok.is_error()) {
                return promise.set_error(r_ok.move_as_error());
              }
              send_closure(actor_id, &PasswordManager::do_get_secure_secret, false, std::move(password),
                           std::move(hash), std::move(promise));
            });

        UpdateSettings update_settings;
        update_settings.current_password = password;
        update_settings.update_secure_secret = true;
        send_closure(actor_id, &PasswordManager::do_update_password_settings, std::move(update_settings),
                     std::move(state), std::move(new_promise));
      }));
}

void PasswordManager::get_temp_password_state(Promise<TempState> promise) /*const*/ {
  promise.set_value(temp_password_state_.as_td_api());
}

TempPasswordState PasswordManager::get_temp_password_state_sync() {
  auto temp_password_str = G()->td_db()->get_binlog_pmc()->get("temp_password");
  TempPasswordState res;
  auto status = log_event_parse(res, temp_password_str);
  if (status.is_error() || res.valid_until <= G()->unix_time()) {
    res = TempPasswordState();
  }
  return res;
}

void PasswordManager::create_temp_password(string password, int32 timeout, Promise<TempState> promise) {
  if (create_temp_password_promise_) {
    return promise.set_error(Status::Error(400, "Another create_temp_password query is active"));
  }
  create_temp_password_promise_ = std::move(promise);

  auto new_promise = PromiseCreator::lambda([actor_id = actor_id(this)](Result<TempPasswordState> result) {
    send_closure(actor_id, &PasswordManager::on_finish_create_temp_password, std::move(result), false);
  });

  do_get_state(PromiseCreator::lambda([password = std::move(password), timeout, promise = std::move(new_promise),
                                       actor_id = actor_id(this)](Result<PasswordState> r_state) mutable {
    if (r_state.is_error()) {
      return promise.set_error(r_state.move_as_error());
    }
    send_closure(actor_id, &PasswordManager::do_create_temp_password, std::move(password), timeout,
                 r_state.move_as_ok(), std::move(promise));
  }));
}

void PasswordManager::drop_temp_password() {
  G()->td_db()->get_binlog_pmc()->erase("temp_password");
  temp_password_state_ = TempPasswordState();
}

void PasswordManager::do_create_temp_password(string password, int32 timeout, PasswordState &&password_state,
                                              Promise<TempPasswordState> promise) {
  send_with_promise(G()->net_query_creator().create(create_storer(telegram_api::account_getTmpPassword(
                        calc_password_hash(password, password_state.current_salt), timeout))),
                    PromiseCreator::lambda([promise = std::move(promise)](Result<NetQueryPtr> r_query) mutable {
                      if (r_query.is_error()) {
                        return promise.set_error(r_query.move_as_error());
                      }
                      auto r_result = fetch_result<telegram_api::account_getTmpPassword>(r_query.move_as_ok());
                      if (r_result.is_error()) {
                        return promise.set_error(r_result.move_as_error());
                      }
                      auto result = r_result.move_as_ok();
                      TempPasswordState res;
                      res.has_temp_password = true;
                      res.temp_password = result->tmp_password_.as_slice().str();
                      res.valid_until = result->valid_until_;
                      promise.set_value(std::move(res));
                    }));
}

void PasswordManager::on_finish_create_temp_password(Result<TempPasswordState> result, bool /*dummy*/) {
  CHECK(create_temp_password_promise_);
  if (result.is_error()) {
    drop_temp_password();
    return create_temp_password_promise_.set_error(result.move_as_error());
  }
  temp_password_state_ = result.move_as_ok();
  G()->td_db()->get_binlog_pmc()->set("temp_password", log_event_store(temp_password_state_).as_slice().str());
  create_temp_password_promise_.set_value(temp_password_state_.as_td_api());
}

void PasswordManager::get_full_state(string password, Promise<PasswordFullState> promise) {
  do_get_state(PromiseCreator::lambda([password = std::move(password), promise = std::move(promise),
                                       actor_id = actor_id(this)](Result<PasswordState> r_state) mutable {
    if (r_state.is_error()) {
      return promise.set_error(r_state.move_as_error());
    }
    send_closure(actor_id, &PasswordManager::do_get_full_state, std::move(password), r_state.move_as_ok(),
                 std::move(promise));
  }));
}

void PasswordManager::do_get_full_state(string password, PasswordState state, Promise<PasswordFullState> promise) {
  auto current_salt = state.current_salt;
  send_with_promise(G()->net_query_creator().create(create_storer(
                        telegram_api::account_getPasswordSettings(calc_password_hash(password, current_salt)))),
                    PromiseCreator::lambda([promise = std::move(promise), state = std::move(state),
                                            password](Result<NetQueryPtr> r_query) mutable {
                      promise.set_result([&]() -> Result<PasswordFullState> {
                        TRY_RESULT(query, std::move(r_query));
                        TRY_RESULT(result, fetch_result<telegram_api::account_getPasswordSettings>(std::move(query)));
                        PasswordPrivateState private_state;
                        private_state.email = result->email_;

                        namespace ss = secure_storage;
                        auto r_secret = [&]() -> Result<ss::Secret> {
                          TRY_RESULT(encrypted_secret, ss::EncryptedSecret::create(result->secure_secret_.as_slice()));
                          return encrypted_secret.decrypt(PSLICE() << result->secure_salt_.as_slice() << password
                                                                   << result->secure_salt_.as_slice());
                        }();

                        LOG_IF(ERROR, r_secret.is_error()) << r_secret.error();
                        LOG_IF(ERROR, r_secret.is_ok()) << "HAS SECRET";
                        private_state.secret = std::move(r_secret);
                        return PasswordFullState{std::move(state), std::move(private_state)};
                      }());
                    }));
}

void PasswordManager::get_recovery_email_address(string password,
                                                 Promise<tl_object_ptr<td_api::recoveryEmailAddress>> promise) {
  get_full_state(
      password,
      PromiseCreator::lambda([password, promise = std::move(promise)](Result<PasswordFullState> r_state) mutable {
        if (r_state.is_error()) {
          return promise.set_error(r_state.move_as_error());
        }
        auto state = r_state.move_as_ok();
        return promise.set_value(make_tl_object<td_api::recoveryEmailAddress>(state.private_state.email));
      }));
}

void PasswordManager::request_password_recovery(
    Promise<tl_object_ptr<td_api::emailAddressAuthenticationCodeInfo>> promise) {
  send_with_promise(
      G()->net_query_creator().create(create_storer(telegram_api::auth_requestPasswordRecovery())),
      PromiseCreator::lambda([promise = std::move(promise)](Result<NetQueryPtr> r_query) mutable {
        if (r_query.is_error()) {
          return promise.set_error(r_query.move_as_error());
        }
        auto r_result = fetch_result<telegram_api::auth_requestPasswordRecovery>(r_query.move_as_ok());
        if (r_result.is_error()) {
          return promise.set_error(r_result.move_as_error());
        }
        auto result = r_result.move_as_ok();
        return promise.set_value(make_tl_object<td_api::emailAddressAuthenticationCodeInfo>(result->email_pattern_));
      }));
}

void PasswordManager::recover_password(string code, Promise<State> promise) {
  send_with_promise(G()->net_query_creator().create(create_storer(telegram_api::auth_recoverPassword(std::move(code)))),
                    PromiseCreator::lambda(
                        [actor_id = actor_id(this), promise = std::move(promise)](Result<NetQueryPtr> r_query) mutable {
                          if (r_query.is_error()) {
                            return promise.set_error(r_query.move_as_error());
                          }
                          auto r_result = fetch_result<telegram_api::auth_recoverPassword>(r_query.move_as_ok());
                          if (r_result.is_error()) {
                            return promise.set_error(r_result.move_as_error());
                          }
                          send_closure(actor_id, &PasswordManager::get_state, std::move(promise));
                        }));
}

void PasswordManager::update_password_settings(UpdateSettings update_settings, Promise<State> promise) {
  auto result_promise = PromiseCreator::lambda(
      [actor_id = actor_id(this), promise = std::move(promise)](Result<bool> r_update_settings) mutable {
        if (r_update_settings.is_error()) {
          promise.set_error(r_update_settings.move_as_error());
          return;
        }
        if (!r_update_settings.ok()) {
          promise.set_error(Status::Error(5, "account_updatePasswordSettings returned false"));
          return;
        }
        send_closure(actor_id, &PasswordManager::get_state, std::move(promise));
      });

  auto password = update_settings.current_password;
  get_full_state(
      std::move(password),
      PromiseCreator::lambda([=, actor_id = actor_id(this), result_promise = std::move(result_promise),
                              update_settings = std::move(update_settings)](Result<PasswordFullState> r_state) mutable {
        if (r_state.is_error()) {
          result_promise.set_error(r_state.move_as_error());
          return;
        }
        send_closure(actor_id, &PasswordManager::do_update_password_settings, std::move(update_settings),
                     r_state.move_as_ok(), std::move(result_promise));
      }));
}

namespace {
BufferSlice create_salt(Slice server_salt) {
  BufferSlice new_salt(server_salt.size() + 32);
  new_salt.as_slice().copy_from(server_salt);
  Random::secure_bytes(new_salt.as_slice().remove_prefix(server_salt.size()));
  return new_salt;
}
}  // namespace
void PasswordManager::do_update_password_settings(UpdateSettings update_settings, PasswordFullState full_state,
                                                  Promise<bool> promise) {
  auto state = std::move(full_state.state);
  auto private_state = std::move(full_state.private_state);
  auto new_settings = make_tl_object<telegram_api::account_passwordInputSettings>();
  if (update_settings.update_password) {
    new_settings->flags_ |= telegram_api::account_passwordInputSettings::NEW_PASSWORD_HASH_MASK;
    new_settings->flags_ |= telegram_api::account_passwordInputSettings::NEW_SALT_MASK;
    new_settings->flags_ |= telegram_api::account_passwordInputSettings::HINT_MASK;
    if (!update_settings.new_password.empty()) {
      auto new_salt = create_salt(state.new_salt);

      new_settings->new_salt_ = std::move(new_salt);
      new_settings->new_password_hash_ =
          calc_password_hash(update_settings.new_password, new_settings->new_salt_.as_slice().str());
      new_settings->hint_ = std::move(update_settings.new_hint);
      if (private_state.secret) {
        update_settings.update_secure_secret = true;
      }
    }
  }

  if (!state.has_password || (update_settings.update_password && update_settings.new_password.empty())) {
    update_settings.update_secure_secret = false;
  }

  if (update_settings.update_secure_secret) {
    new_settings->flags_ |= telegram_api::account_passwordInputSettings::NEW_SECURE_SECRET_ID_MASK;
    new_settings->flags_ |= telegram_api::account_passwordInputSettings::NEW_SECURE_SALT_MASK;
    new_settings->flags_ |= telegram_api::account_passwordInputSettings::NEW_SECURE_SECRET_MASK;
    auto secret = [&]() {
      if (private_state.secret) {
        return std::move(private_state.secret.value());
      }
      return secure_storage::Secret::create_new();
    }();
    auto new_secure_salt = create_salt(state.new_secure_salt);
    auto encrypted_secret = secret.encrypt(
        PSLICE() << new_secure_salt.as_slice()
                 << (update_settings.update_password ? update_settings.new_password : update_settings.current_password)
                 << new_secure_salt.as_slice());

    new_settings->new_secure_salt_ = std::move(new_secure_salt);
    new_settings->new_secure_secret_ = BufferSlice(encrypted_secret.as_slice());
    new_settings->new_secure_secret_id_ = secret.get_hash();
  }
  if (update_settings.update_recovery_email_address) {
    new_settings->flags_ |= telegram_api::account_passwordInputSettings::EMAIL_MASK;
    new_settings->email_ = std::move(update_settings.recovery_email_address);
  }
  BufferSlice current_hash;
  if (state.has_password) {
    current_hash = calc_password_hash(update_settings.current_password, state.current_salt);
  }
  auto query = G()->net_query_creator().create(
      create_storer(telegram_api::account_updatePasswordSettings(std::move(current_hash), std::move(new_settings))));

  send_with_promise(std::move(query), PromiseCreator::lambda([actor_id = actor_id(this), promise = std::move(promise)](
                                                                 Result<NetQueryPtr> r_query) mutable {
                      if (r_query.is_error()) {
                        return promise.set_error(r_query.move_as_error());
                      }
                      auto r_result = fetch_result<telegram_api::account_updatePasswordSettings>(r_query.move_as_ok());
                      if (r_result.is_error()) {
                        if (r_result.error().code() == 400 && r_result.error().message() == "EMAIL_UNCONFIRMED") {
                          return promise.set_value(true);
                        }
                        return promise.set_error(r_result.move_as_error());
                      }
                      return promise.set_value(r_result.move_as_ok());
                    }));
}

void PasswordManager::get_state(Promise<State> promise) {
  do_get_state(PromiseCreator::lambda([promise = std::move(promise)](Result<PasswordState> r_state) mutable {
    if (r_state.is_error()) {
      promise.set_error(r_state.move_as_error());
      return;
    }
    promise.set_value(r_state.move_as_ok().as_td_api());
  }));
}

void PasswordManager::do_get_state(Promise<PasswordState> promise) {
  auto query = G()->net_query_creator().create(create_storer(telegram_api::account_getPassword()));
  send_with_promise(std::move(query), PromiseCreator::lambda([actor_id = actor_id(this), promise = std::move(promise)](
                                                                 Result<NetQueryPtr> r_query) mutable {
                      if (r_query.is_error()) {
                        promise.set_error(r_query.move_as_error());
                        return;
                      }
                      auto r_result = fetch_result<telegram_api::account_getPassword>(r_query.move_as_ok());
                      if (r_result.is_error()) {
                        promise.set_error(r_result.move_as_error());
                        return;
                      }
                      auto result = r_result.move_as_ok();

                      PasswordState state;
                      string secure_random;
                      if (result->get_id() == telegram_api::account_noPassword::ID) {
                        auto no_password = move_tl_object_as<telegram_api::account_noPassword>(result);
                        state.has_password = false;
                        state.password_hint = "";
                        state.current_salt = "";
                        state.new_salt = no_password->new_salt_.as_slice().str();
                        state.new_secure_salt = no_password->new_secure_salt_.as_slice().str();
                        secure_random = no_password->secure_random_.as_slice().str();
                        state.has_recovery_email_address = false;
                        state.unconfirmed_recovery_email_address_pattern = no_password->email_unconfirmed_pattern_;
                      } else if (result->get_id() == telegram_api::account_password::ID) {
                        auto password = move_tl_object_as<telegram_api::account_password>(result);
                        state.has_password = true;
                        state.password_hint = password->hint_;
                        state.current_salt = password->current_salt_.as_slice().str();
                        state.new_salt = password->new_salt_.as_slice().str();
                        state.new_secure_salt = password->new_secure_salt_.as_slice().str();
                        secure_random = password->secure_random_.as_slice().str();
                        state.has_recovery_email_address = password->has_recovery_;
                        state.unconfirmed_recovery_email_address_pattern = password->email_unconfirmed_pattern_;
                      } else {
                        UNREACHABLE();
                      }
                      Random::add_seed(secure_random);
                      promise.set_value(std::move(state));
                    }));
}
void PasswordManager::cache_secret(secure_storage::Secret secret) {
  LOG(ERROR) << "CACHE";
  secret_ = std::move(secret);
}

void PasswordManager::on_result(NetQueryPtr query) {
  auto token = get_link_token();
  container_.extract(token).set_value(std::move(query));
}
void PasswordManager::send_with_promise(NetQueryPtr query, Promise<NetQueryPtr> promise) {
  auto id = container_.create(std::move(promise));
  G()->net_query_dispatcher().dispatch_with_callback(std::move(query), actor_shared(this, id));
}

void PasswordManager::start_up() {
  temp_password_state_ = get_temp_password_state_sync();
}

}  // namespace td
