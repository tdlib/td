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
#include "td/telegram/SecureStorage.h"

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

static void hash_sha256(Slice data, Slice salt, MutableSlice dest) {
  sha256(PSLICE() << salt << data << salt, dest);
}

BufferSlice PasswordManager::calc_password_hash(Slice password, Slice client_salt, Slice server_salt) {
  if (password.empty()) {
    return BufferSlice();
  }

  BufferSlice buf(32);
  hash_sha256(password, client_salt, buf.as_slice());
  hash_sha256(buf.as_slice(), server_salt, buf.as_slice());
  BufferSlice hash(64);
  pbkdf2_sha512(buf.as_slice(), client_salt, 100000, hash.as_slice());
  return hash;
}

BufferSlice PasswordManager::calc_password_hash(Slice password, const PasswordState &state) const {
  return calc_password_hash(password, state.current_client_salt, state.current_server_salt);
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
          return promise.set_error(Status::Error(400, "2-step verification is disabled"));
        }
        if (state.private_state.secret) {
          send_closure(actor_id, &PasswordManager::cache_secret, state.private_state.secret.value().clone());
          return promise.set_value(std::move(state.private_state.secret.value()));
        }
        if (!recursive) {
          return promise.set_error(Status::Error(400, "Failed to get Telegram Passport secret"));
        }

        auto new_promise = PromiseCreator::lambda([password, hash = std::move(hash), promise = std::move(promise),
                                                   actor_id = actor_id](Result<bool> r_ok) mutable {
          if (r_ok.is_error()) {
            return promise.set_error(r_ok.move_as_error());
          }
          send_closure(actor_id, &PasswordManager::do_get_secure_secret, false, std::move(password), std::move(hash),
                       std::move(promise));
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
  auto hash = calc_password_hash(password, password_state);
  send_with_promise(
      G()->net_query_creator().create(create_storer(telegram_api::account_getTmpPassword(std::move(hash), timeout))),
      PromiseCreator::lambda([promise = std::move(promise)](Result<NetQueryPtr> r_query) mutable {
        auto r_result = fetch_result<telegram_api::account_getTmpPassword>(std::move(r_query));
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

Result<secure_storage::Secret> PasswordManager::decrypt_secure_secret(
    Slice password, tl_object_ptr<telegram_api::SecurePasswordKdfAlgo> algo_ptr, Slice secret, int64 secret_id) {
  TRY_RESULT(encrypted_secret, secure_storage::EncryptedSecret::create(secret));

  CHECK(algo_ptr != nullptr);
  BufferSlice salt;
  secure_storage::EnryptionAlgorithm algorithm = secure_storage::EnryptionAlgorithm::Pbkdf2;
  switch (algo_ptr->get_id()) {
    case telegram_api::securePasswordKdfAlgoUnknown::ID:
      return Status::Error(400, "Unsupported algorithm");
    case telegram_api::securePasswordKdfAlgoSHA512::ID: {
      auto algo = move_tl_object_as<telegram_api::securePasswordKdfAlgoSHA512>(algo_ptr);
      salt = std::move(algo->salt_);
      algorithm = secure_storage::EnryptionAlgorithm::Sha512;
      break;
    }
    case telegram_api::securePasswordKdfAlgoPBKDF2HMACSHA512iter100000::ID: {
      auto algo = move_tl_object_as<telegram_api::securePasswordKdfAlgoPBKDF2HMACSHA512iter100000>(algo_ptr);
      salt = std::move(algo->salt_);
      break;
    }
    default:
      UNREACHABLE();
  }
  TRY_RESULT(result, encrypted_secret.decrypt(password, salt.as_slice(), algorithm));
  if (secret_id != result.get_hash()) {
    return Status::Error("Secret hash mismatch");
  }
  return result;
}

void PasswordManager::do_get_full_state(string password, PasswordState state, Promise<PasswordFullState> promise) {
  auto hash = calc_password_hash(password, state);
  send_with_promise(
      G()->net_query_creator().create(create_storer(telegram_api::account_getPasswordSettings(std::move(hash)))),
      PromiseCreator::lambda(
          [promise = std::move(promise), state = std::move(state), password](Result<NetQueryPtr> r_query) mutable {
            promise.set_result([&]() -> Result<PasswordFullState> {
              TRY_RESULT(result, fetch_result<telegram_api::account_getPasswordSettings>(std::move(r_query)));
              PasswordPrivateState private_state;
              private_state.email = std::move(result->email_);

              if (result->secure_settings_ != nullptr) {
                auto r_secret = decrypt_secure_secret(password, std::move(result->secure_settings_->secure_algo_),
                                                      result->secure_settings_->secure_secret_.as_slice(),
                                                      result->secure_settings_->secure_secret_id_);
                if (r_secret.is_ok()) {
                  private_state.secret = r_secret.move_as_ok();
                }
              }

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

void PasswordManager::send_email_address_verification_code(
    string email, Promise<td_api::object_ptr<td_api::emailAddressAuthenticationCodeInfo>> promise) {
  last_verified_email_address_ = email;
  auto query =
      G()->net_query_creator().create(create_storer(telegram_api::account_sendVerifyEmailCode(std::move(email))));
  send_with_promise(std::move(query), PromiseCreator::lambda([actor_id = actor_id(this), promise = std::move(promise)](
                                                                 Result<NetQueryPtr> r_query) mutable {
                      auto r_result = fetch_result<telegram_api::account_sendVerifyEmailCode>(std::move(r_query));
                      if (r_result.is_error()) {
                        return promise.set_error(r_result.move_as_error());
                      }
                      auto result = r_result.move_as_ok();
                      if (result->length_ < 0 || result->length_ >= 100) {
                        LOG(ERROR) << "Receive wrong code length " << result->length_;
                        result->length_ = 0;
                      }
                      return promise.set_value(make_tl_object<td_api::emailAddressAuthenticationCodeInfo>(
                          result->email_pattern_, result->length_));
                    }));
}

void PasswordManager::resend_email_address_verification_code(
    Promise<td_api::object_ptr<td_api::emailAddressAuthenticationCodeInfo>> promise) {
  if (last_verified_email_address_.empty()) {
    return promise.set_error(Status::Error(400, "No email address verification was sent"));
  }
  send_email_address_verification_code(last_verified_email_address_, std::move(promise));
}

void PasswordManager::check_email_address_verification_code(string code,
                                                            Promise<td_api::object_ptr<td_api::ok>> promise) {
  if (last_verified_email_address_.empty()) {
    return promise.set_error(Status::Error(400, "No email address verification was sent"));
  }
  auto query = G()->net_query_creator().create(
      create_storer(telegram_api::account_verifyEmail(last_verified_email_address_, std::move(code))));
  send_with_promise(std::move(query), PromiseCreator::lambda([actor_id = actor_id(this), promise = std::move(promise)](
                                                                 Result<NetQueryPtr> r_query) mutable {
                      auto r_result = fetch_result<telegram_api::account_updatePasswordSettings>(std::move(r_query));
                      if (r_result.is_error()) {
                        return promise.set_error(r_result.move_as_error());
                      }
                      return promise.set_value(td_api::make_object<td_api::ok>());
                    }));
}

void PasswordManager::request_password_recovery(
    Promise<td_api::object_ptr<td_api::emailAddressAuthenticationCodeInfo>> promise) {
  send_with_promise(
      G()->net_query_creator().create(create_storer(telegram_api::auth_requestPasswordRecovery())),
      PromiseCreator::lambda([promise = std::move(promise)](Result<NetQueryPtr> r_query) mutable {
        auto r_result = fetch_result<telegram_api::auth_requestPasswordRecovery>(std::move(r_query));
        if (r_result.is_error()) {
          return promise.set_error(r_result.move_as_error());
        }
        auto result = r_result.move_as_ok();
        return promise.set_value(make_tl_object<td_api::emailAddressAuthenticationCodeInfo>(result->email_pattern_, 0));
      }));
}

void PasswordManager::recover_password(string code, Promise<State> promise) {
  send_with_promise(G()->net_query_creator().create(create_storer(telegram_api::auth_recoverPassword(std::move(code)))),
                    PromiseCreator::lambda(
                        [actor_id = actor_id(this), promise = std::move(promise)](Result<NetQueryPtr> r_query) mutable {
                          auto r_result = fetch_result<telegram_api::auth_recoverPassword>(std::move(r_query));
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
          return promise.set_error(r_update_settings.move_as_error());
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
          return result_promise.set_error(r_state.move_as_error());
        }
        send_closure(actor_id, &PasswordManager::do_update_password_settings, std::move(update_settings),
                     r_state.move_as_ok(), std::move(result_promise));
      }));
}

static BufferSlice create_salt(Slice salt_prefix) {
  static constexpr size_t ADDED_SALT_SIZE = 32;
  BufferSlice new_salt(salt_prefix.size() + ADDED_SALT_SIZE);
  new_salt.as_slice().copy_from(salt_prefix);
  Random::secure_bytes(new_salt.as_slice().substr(salt_prefix.size()));
  return new_salt;
}

void PasswordManager::do_update_password_settings(UpdateSettings update_settings, PasswordFullState full_state,
                                                  Promise<bool> promise) {
  auto state = std::move(full_state.state);
  auto private_state = std::move(full_state.private_state);
  auto new_settings = make_tl_object<telegram_api::account_passwordInputSettings>();
  if (update_settings.update_password) {
    new_settings->flags_ |= telegram_api::account_passwordInputSettings::NEW_PASSWORD_HASH_MASK;
    new_settings->flags_ |= telegram_api::account_passwordInputSettings::NEW_ALGO_MASK;
    new_settings->flags_ |= telegram_api::account_passwordInputSettings::HINT_MASK;
    if (!update_settings.new_password.empty()) {
      auto new_client_salt = create_salt(state.new_client_salt);

      new_settings->new_password_hash_ =
          calc_password_hash(update_settings.new_password, new_client_salt.as_slice(), state.new_server_salt);
      new_settings->new_algo_ = make_tl_object<telegram_api::passwordKdfAlgoSHA256SHA256PBKDF2HMACSHA512iter100000>(
          std::move(new_client_salt), BufferSlice(state.new_server_salt));
      new_settings->hint_ = std::move(update_settings.new_hint);
      if (private_state.secret) {
        update_settings.update_secure_secret = true;
      }
    } else {
      new_settings->new_algo_ = make_tl_object<telegram_api::passwordKdfAlgoUnknown>();
    }
  }

  // Has no password and not setting one.
  if (!update_settings.update_password && !state.has_password) {
    update_settings.update_secure_secret = false;
  }

  // Setting an empty password
  if (update_settings.update_password && update_settings.new_password.empty()) {
    update_settings.update_secure_secret = false;
  }

  if (update_settings.update_secure_secret) {
    auto secret = private_state.secret ? std::move(private_state.secret.value()) : secure_storage::Secret::create_new();
    auto algorithm = make_tl_object<telegram_api::securePasswordKdfAlgoPBKDF2HMACSHA512iter100000>(
        create_salt(state.new_secure_salt));
    auto encrypted_secret = secret.encrypt(
        update_settings.update_password ? update_settings.new_password : update_settings.current_password,
        algorithm->salt_.as_slice(), secure_storage::EnryptionAlgorithm::Pbkdf2);

    new_settings->flags_ |= telegram_api::account_passwordInputSettings::NEW_SECURE_SETTINGS_MASK;
    new_settings->new_secure_settings_ = make_tl_object<telegram_api::secureSecretSettings>(
        std::move(algorithm), BufferSlice(encrypted_secret.as_slice()), secret.get_hash());
  }
  if (update_settings.update_recovery_email_address) {
    new_settings->flags_ |= telegram_api::account_passwordInputSettings::EMAIL_MASK;
    new_settings->email_ = std::move(update_settings.recovery_email_address);
  }
  BufferSlice current_hash;
  if (state.has_password) {
    current_hash = calc_password_hash(update_settings.current_password, state);
  }
  auto query = G()->net_query_creator().create(
      create_storer(telegram_api::account_updatePasswordSettings(std::move(current_hash), std::move(new_settings))));

  send_with_promise(std::move(query), PromiseCreator::lambda([actor_id = actor_id(this), promise = std::move(promise)](
                                                                 Result<NetQueryPtr> r_query) mutable {
                      auto r_result = fetch_result<telegram_api::account_updatePasswordSettings>(std::move(r_query));
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
      return promise.set_error(r_state.move_as_error());
    }
    promise.set_value(r_state.move_as_ok().as_td_api());
  }));
}

void PasswordManager::do_get_state(Promise<PasswordState> promise) {
  auto query = G()->net_query_creator().create(create_storer(telegram_api::account_getPassword()));
  send_with_promise(
      std::move(query), PromiseCreator::lambda([actor_id = actor_id(this),
                                                promise = std::move(promise)](Result<NetQueryPtr> r_query) mutable {
        auto r_result = fetch_result<telegram_api::account_getPassword>(std::move(r_query));
        if (r_result.is_error()) {
          return promise.set_error(r_result.move_as_error());
        }
        auto password = r_result.move_as_ok();
        Random::add_seed(password->secure_random_.as_slice());

        PasswordState state;
        if (password->current_algo_ != nullptr) {
          state.has_password = true;

          switch (password->current_algo_->get_id()) {
            case telegram_api::passwordKdfAlgoUnknown::ID:
              return promise.set_error(Status::Error(400, "Please update client to continue"));
            case telegram_api::passwordKdfAlgoSHA256SHA256PBKDF2HMACSHA512iter100000::ID: {
              auto algo = move_tl_object_as<telegram_api::passwordKdfAlgoSHA256SHA256PBKDF2HMACSHA512iter100000>(
                  password->current_algo_);
              state.current_client_salt = algo->salt1_.as_slice().str();
              state.current_server_salt = algo->salt2_.as_slice().str();
              break;
            }
            default:
              UNREACHABLE();
          }
          state.password_hint = std::move(password->hint_);
          state.has_recovery_email_address =
              (password->flags_ & telegram_api::account_password::HAS_RECOVERY_MASK) != 0;
          state.has_secure_values = (password->flags_ & telegram_api::account_password::HAS_SECURE_VALUES_MASK) != 0;
        } else {
          state.has_password = false;
        }
        state.unconfirmed_recovery_email_address_pattern = std::move(password->email_unconfirmed_pattern_);

        CHECK(password->new_algo_ != nullptr);
        switch (password->new_algo_->get_id()) {
          case telegram_api::passwordKdfAlgoUnknown::ID:
            return promise.set_error(Status::Error(400, "Please update client to continue"));
          case telegram_api::passwordKdfAlgoSHA256SHA256PBKDF2HMACSHA512iter100000::ID: {
            auto algo = move_tl_object_as<telegram_api::passwordKdfAlgoSHA256SHA256PBKDF2HMACSHA512iter100000>(
                password->new_algo_);
            state.new_client_salt = algo->salt1_.as_slice().str();
            state.new_server_salt = algo->salt2_.as_slice().str();
            break;
          }
          default:
            UNREACHABLE();
        }

        CHECK(password->new_secure_algo_ != nullptr);
        switch (password->new_secure_algo_->get_id()) {
          case telegram_api::securePasswordKdfAlgoUnknown::ID:
            return promise.set_error(Status::Error(400, "Please update client to continue"));
          case telegram_api::securePasswordKdfAlgoSHA512::ID:
            return promise.set_error(Status::Error(500, "Server has sent outdated secret encryption mode"));
          case telegram_api::securePasswordKdfAlgoPBKDF2HMACSHA512iter100000::ID: {
            auto algo = move_tl_object_as<telegram_api::securePasswordKdfAlgoPBKDF2HMACSHA512iter100000>(
                password->new_secure_algo_);
            state.new_secure_salt = algo->salt_.as_slice().str();
            break;
          }
          default:
            UNREACHABLE();
        }

        if (state.new_secure_salt.size() < MIN_NEW_SECURE_SALT_SIZE) {
          return promise.set_error(Status::Error(500, "New secure salt length too small"));
        }
        if (state.new_client_salt.size() < MIN_NEW_SALT_SIZE) {
          return promise.set_error(Status::Error(500, "New salt length too small"));
        }
        promise.set_value(std::move(state));
      }));
}

void PasswordManager::cache_secret(secure_storage::Secret secret) {
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

void PasswordManager::hangup() {
  container_.for_each(
      [](auto id, Promise<NetQueryPtr> &promise) { promise.set_error(Status::Error(500, "Request aborted")); });
  stop();
}

}  // namespace td
