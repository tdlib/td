//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/PasswordManager.h"

#include "td/telegram/DhCache.h"
#include "td/telegram/Global.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/net/NetQueryDispatcher.h"
#include "td/telegram/TdDb.h"

#include "td/mtproto/DhHandshake.h"

#include "td/utils/BigNum.h"
#include "td/utils/buffer.h"
#include "td/utils/crypto.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Random.h"
#include "td/utils/Slice.h"
#include "td/utils/Time.h"

namespace td {

tl_object_ptr<td_api::temporaryPasswordState> TempPasswordState::get_temporary_password_state_object() const {
  if (!has_temp_password || valid_until <= G()->unix_time()) {
    return make_tl_object<td_api::temporaryPasswordState>(false, 0);
  }
  return make_tl_object<td_api::temporaryPasswordState>(true, valid_until - G()->unix_time_cached());
}

static void hash_sha256(Slice data, Slice salt, MutableSlice dest) {
  sha256(PSLICE() << salt << data << salt, dest);
}

BufferSlice PasswordManager::calc_password_hash(Slice password, Slice client_salt, Slice server_salt) {
  LOG(INFO) << "Begin password hash calculation";
  BufferSlice buf(32);
  hash_sha256(password, client_salt, buf.as_slice());
  hash_sha256(buf.as_slice(), server_salt, buf.as_slice());
  BufferSlice hash(64);
  pbkdf2_sha512(buf.as_slice(), client_salt, 100000, hash.as_slice());
  hash_sha256(hash.as_slice(), server_salt, buf.as_slice());
  LOG(INFO) << "End password hash calculation";
  return buf;
}

Result<BufferSlice> PasswordManager::calc_password_srp_hash(Slice password, Slice client_salt, Slice server_salt,
                                                            int32 g, Slice p) {
  LOG(INFO) << "Begin password SRP hash calculation";
  TRY_STATUS(DhHandshake::check_config(g, p, DhCache::instance()));

  auto hash = calc_password_hash(password, client_salt, server_salt);
  auto p_bn = BigNum::from_binary(p);
  BigNum g_bn;
  g_bn.set_value(g);
  auto x_bn = BigNum::from_binary(hash.as_slice());

  BigNumContext ctx;
  BigNum v_bn;
  BigNum::mod_exp(v_bn, g_bn, x_bn, p_bn, ctx);

  BufferSlice result(v_bn.to_binary(256));
  LOG(INFO) << "End password SRP hash calculation";
  return std::move(result);
}

tl_object_ptr<telegram_api::InputCheckPasswordSRP> PasswordManager::get_input_check_password(
    Slice password, Slice client_salt, Slice server_salt, int32 g, Slice p, Slice B, int64 id) {
  if (password.empty()) {
    return make_tl_object<telegram_api::inputCheckPasswordEmpty>();
  }

  if (DhHandshake::check_config(g, p, DhCache::instance()).is_error()) {
    LOG(ERROR) << "Receive invalid config " << g << " " << format::escaped(p);
    return make_tl_object<telegram_api::inputCheckPasswordEmpty>();
  }

  auto p_bn = BigNum::from_binary(p);
  auto B_bn = BigNum::from_binary(B);
  auto zero = BigNum::from_decimal("0").move_as_ok();
  if (BigNum::compare(zero, B_bn) != -1 || BigNum::compare(B_bn, p_bn) != -1 || B.size() < 248 || B.size() > 256) {
    LOG(ERROR) << "Receive invalid value of B(" << B.size() << "): " << B_bn << " " << p_bn;
    return make_tl_object<telegram_api::inputCheckPasswordEmpty>();
  }

  LOG(INFO) << "Begin input password SRP hash calculation";
  BigNum g_bn;
  g_bn.set_value(g);
  auto g_padded = g_bn.to_binary(256);

  auto x = calc_password_hash(password, client_salt, server_salt);
  auto x_bn = BigNum::from_binary(x.as_slice());

  BufferSlice a(2048 / 8);
  Random::secure_bytes(a.as_slice());
  auto a_bn = BigNum::from_binary(a.as_slice());

  BigNumContext ctx;
  BigNum A_bn;
  BigNum::mod_exp(A_bn, g_bn, a_bn, p_bn, ctx);
  string A = A_bn.to_binary(256);

  string B_pad(256 - B.size(), '\0');
  string u = sha256(PSLICE() << A << B_pad << B);
  auto u_bn = BigNum::from_binary(u);
  string k = sha256(PSLICE() << p << g_padded);
  auto k_bn = BigNum::from_binary(k);

  BigNum v_bn;
  BigNum::mod_exp(v_bn, g_bn, x_bn, p_bn, ctx);
  BigNum kv_bn;
  BigNum::mod_mul(kv_bn, k_bn, v_bn, p_bn, ctx);
  BigNum t_bn;
  BigNum::sub(t_bn, B_bn, kv_bn);
  if (BigNum::compare(t_bn, zero) == -1) {
    BigNum::add(t_bn, t_bn, p_bn);
  }
  BigNum exp_bn;
  BigNum::mul(exp_bn, u_bn, x_bn, ctx);
  BigNum::add(exp_bn, exp_bn, a_bn);

  BigNum S_bn;
  BigNum::mod_exp(S_bn, t_bn, exp_bn, p_bn, ctx);
  string S = S_bn.to_binary(256);
  auto K = sha256(S);

  auto h1 = sha256(p);
  auto h2 = sha256(g_padded);
  for (size_t i = 0; i < h1.size(); i++) {
    h1[i] = static_cast<char>(static_cast<unsigned char>(h1[i]) ^ static_cast<unsigned char>(h2[i]));
  }
  auto M = sha256(PSLICE() << h1 << sha256(client_salt) << sha256(server_salt) << A << B_pad << B << K);

  LOG(INFO) << "End input password SRP hash calculation";
  return make_tl_object<telegram_api::inputCheckPasswordSRP>(id, BufferSlice(A), BufferSlice(M));
}

tl_object_ptr<telegram_api::InputCheckPasswordSRP> PasswordManager::get_input_check_password(
    Slice password, const PasswordState &state) {
  return get_input_check_password(password, state.current_client_salt, state.current_server_salt, state.current_srp_g,
                                  state.current_srp_p, state.current_srp_B, state.current_srp_id);
}

void PasswordManager::get_input_check_password_srp(
    string password, Promise<tl_object_ptr<telegram_api::InputCheckPasswordSRP>> &&promise) {
  do_get_state(PromiseCreator::lambda(
      [promise = std::move(promise), password = std::move(password)](Result<PasswordState> r_state) mutable {
        if (r_state.is_error()) {
          return promise.set_error(r_state.move_as_error());
        }
        promise.set_value(PasswordManager::get_input_check_password(password, r_state.move_as_ok()));
      }));
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

void PasswordManager::get_secure_secret(string password, Promise<secure_storage::Secret> promise) {
  return do_get_secure_secret(true, std::move(password), std::move(promise));
}

void PasswordManager::do_get_secure_secret(bool allow_recursive, string password,
                                           Promise<secure_storage::Secret> promise) {
  if (secret_) {
    return promise.set_value(secret_.value().clone());
  }
  if (password.empty()) {
    return promise.set_error(Status::Error(400, "PASSWORD_HASH_INVALID"));
  }
  get_full_state(
      password, PromiseCreator::lambda([password, allow_recursive, promise = std::move(promise),
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
        if (!allow_recursive) {
          return promise.set_error(Status::Error(400, "Failed to get Telegram Passport secret"));
        }

        auto new_promise =
            PromiseCreator::lambda([password, promise = std::move(promise), actor_id](Result<bool> r_ok) mutable {
              if (r_ok.is_error()) {
                return promise.set_error(r_ok.move_as_error());
              }
              send_closure(actor_id, &PasswordManager::do_get_secure_secret, false, std::move(password),
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
  promise.set_value(temp_password_state_.get_temporary_password_state_object());
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
  auto hash = get_input_check_password(password, password_state);
  send_with_promise(G()->net_query_creator().create(telegram_api::account_getTmpPassword(std::move(hash), timeout)),
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
  create_temp_password_promise_.set_value(temp_password_state_.get_temporary_password_state_object());
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
  if (!state.has_password) {
    PasswordFullState result;
    result.state = std::move(state);
    return promise.set_value(std::move(result));
  }

  auto hash = get_input_check_password(password, state);
  send_with_promise(G()->net_query_creator().create(telegram_api::account_getPasswordSettings(std::move(hash))),
                    PromiseCreator::lambda([promise = std::move(promise), state = std::move(state),
                                            password](Result<NetQueryPtr> r_query) mutable {
                      promise.set_result([&]() -> Result<PasswordFullState> {
                        TRY_RESULT(result, fetch_result<telegram_api::account_getPasswordSettings>(std::move(r_query)));
                        LOG(INFO) << "Receive password settings: " << to_string(result);
                        PasswordPrivateState private_state;
                        private_state.email = std::move(result->email_);

                        if (result->secure_settings_ != nullptr) {
                          auto r_secret =
                              decrypt_secure_secret(password, std::move(result->secure_settings_->secure_algo_),
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

void PasswordManager::check_recovery_email_address_code(string code, Promise<State> promise) {
  auto query = G()->net_query_creator().create(telegram_api::account_confirmPasswordEmail(std::move(code)));
  send_with_promise(std::move(query), PromiseCreator::lambda([actor_id = actor_id(this), promise = std::move(promise)](
                                                                 Result<NetQueryPtr> r_query) mutable {
                      auto r_result = fetch_result<telegram_api::account_confirmPasswordEmail>(std::move(r_query));
                      if (r_result.is_error() && r_result.error().message() != "EMAIL_HASH_EXPIRED" &&
                          r_result.error().message() != "CODE_INVALID") {
                        return promise.set_error(r_result.move_as_error());
                      }
                      send_closure(actor_id, &PasswordManager::get_state, std::move(promise));
                    }));
}

void PasswordManager::resend_recovery_email_address_code(Promise<State> promise) {
  auto query = G()->net_query_creator().create(telegram_api::account_resendPasswordEmail());
  send_with_promise(std::move(query), PromiseCreator::lambda([actor_id = actor_id(this), promise = std::move(promise)](
                                                                 Result<NetQueryPtr> r_query) mutable {
                      auto r_result = fetch_result<telegram_api::account_resendPasswordEmail>(std::move(r_query));
                      if (r_result.is_error() && r_result.error().message() != "EMAIL_HASH_EXPIRED") {
                        return promise.set_error(r_result.move_as_error());
                      }
                      send_closure(actor_id, &PasswordManager::get_state, std::move(promise));
                    }));
}

void PasswordManager::send_email_address_verification_code(
    string email, Promise<td_api::object_ptr<td_api::emailAddressAuthenticationCodeInfo>> promise) {
  last_verified_email_address_ = email;
  auto query = G()->net_query_creator().create(telegram_api::account_sendVerifyEmailCode(std::move(email)));
  send_with_promise(
      std::move(query), PromiseCreator::lambda([promise = std::move(promise)](Result<NetQueryPtr> r_query) mutable {
        auto r_result = fetch_result<telegram_api::account_sendVerifyEmailCode>(std::move(r_query));
        if (r_result.is_error()) {
          return promise.set_error(r_result.move_as_error());
        }
        auto result = r_result.move_as_ok();
        if (result->length_ < 0 || result->length_ >= 100) {
          LOG(ERROR) << "Receive wrong code length " << result->length_;
          result->length_ = 0;
        }
        return promise.set_value(
            make_tl_object<td_api::emailAddressAuthenticationCodeInfo>(result->email_pattern_, result->length_));
      }));
}

void PasswordManager::resend_email_address_verification_code(
    Promise<td_api::object_ptr<td_api::emailAddressAuthenticationCodeInfo>> promise) {
  if (last_verified_email_address_.empty()) {
    return promise.set_error(Status::Error(400, "No email address verification was sent"));
  }
  send_email_address_verification_code(last_verified_email_address_, std::move(promise));
}

void PasswordManager::check_email_address_verification_code(string code, Promise<Unit> promise) {
  if (last_verified_email_address_.empty()) {
    return promise.set_error(Status::Error(400, "No email address verification was sent"));
  }
  auto query =
      G()->net_query_creator().create(telegram_api::account_verifyEmail(last_verified_email_address_, std::move(code)));
  send_with_promise(std::move(query),
                    PromiseCreator::lambda([promise = std::move(promise)](Result<NetQueryPtr> r_query) mutable {
                      auto r_result = fetch_result<telegram_api::account_verifyEmail>(std::move(r_query));
                      if (r_result.is_error()) {
                        return promise.set_error(r_result.move_as_error());
                      }
                      return promise.set_value(Unit());
                    }));
}

void PasswordManager::request_password_recovery(
    Promise<td_api::object_ptr<td_api::emailAddressAuthenticationCodeInfo>> promise) {
  // is called only after authoriation
  send_with_promise(
      G()->net_query_creator().create(telegram_api::auth_requestPasswordRecovery()),
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
  // is called only after authoriation
  send_with_promise(G()->net_query_creator().create(telegram_api::auth_recoverPassword(std::move(code))),
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
          return promise.set_error(Status::Error(5, "account_updatePasswordSettings returned false"));
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
  // PasswordState has already been used to get PasswordPrivateState and need to be reget
  do_get_state(PromiseCreator::lambda([actor_id = actor_id(this), update_settings = std::move(update_settings),
                                       private_state = std::move(full_state.private_state),
                                       promise = std::move(promise)](Result<PasswordState> r_state) mutable {
    if (r_state.is_error()) {
      return promise.set_error(r_state.move_as_error());
    }
    send_closure(actor_id, &PasswordManager::do_update_password_settings_impl, std::move(update_settings),
                 r_state.move_as_ok(), std::move(private_state), std::move(promise));
  }));
}

void PasswordManager::do_update_password_settings_impl(UpdateSettings update_settings, PasswordState state,
                                                       PasswordPrivateState private_state, Promise<bool> promise) {
  auto new_settings = make_tl_object<telegram_api::account_passwordInputSettings>();
  if (update_settings.update_password) {
    new_settings->flags_ |= telegram_api::account_passwordInputSettings::NEW_PASSWORD_HASH_MASK;
    new_settings->flags_ |= telegram_api::account_passwordInputSettings::NEW_ALGO_MASK;
    new_settings->flags_ |= telegram_api::account_passwordInputSettings::HINT_MASK;
    if (!update_settings.new_password.empty()) {
      auto new_client_salt = create_salt(state.new_client_salt);

      auto new_hash = calc_password_srp_hash(update_settings.new_password, new_client_salt.as_slice(),
                                             state.new_server_salt, state.new_srp_g, state.new_srp_p);
      if (new_hash.is_error()) {
        return promise.set_error(Status::Error(400, "Unable to change password, because it may be unsafe"));
      }
      new_settings->new_password_hash_ = new_hash.move_as_ok();
      new_settings->new_algo_ =
          make_tl_object<telegram_api::passwordKdfAlgoSHA256SHA256PBKDF2HMACSHA512iter100000SHA256ModPow>(
              std::move(new_client_salt), BufferSlice(state.new_server_salt), state.new_srp_g,
              BufferSlice(state.new_srp_p));
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
  auto current_hash = get_input_check_password(state.has_password ? update_settings.current_password : Slice(), state);
  auto query = G()->net_query_creator().create(
      telegram_api::account_updatePasswordSettings(std::move(current_hash), std::move(new_settings)));

  send_with_promise(std::move(query), PromiseCreator::lambda([actor_id = actor_id(this), promise = std::move(promise)](
                                                                 Result<NetQueryPtr> r_query) mutable {
                      auto r_result = fetch_result<telegram_api::account_updatePasswordSettings>(std::move(r_query));
                      if (r_result.is_error()) {
                        Slice prefix("EMAIL_UNCONFIRMED");
                        Slice message = r_result.error().message();
                        if (r_result.error().code() == 400 && begins_with(message, prefix)) {
                          if (message.size() >= prefix.size() + 2 && message[prefix.size()] == '_') {
                            send_closure(actor_id, &PasswordManager::on_get_code_length,
                                         to_integer<int32>(message.substr(prefix.size() + 1)));
                          }
                          return promise.set_value(true);
                        }
                        return promise.set_error(r_result.move_as_error());
                      }
                      return promise.set_value(r_result.move_as_ok());
                    }));
}

void PasswordManager::on_get_code_length(int32 code_length) {
  if (code_length <= 0 || code_length > 100) {
    LOG(ERROR) << "Receive invalid code length " << code_length;
    return;
  }

  LOG(INFO) << "Set code length to " << code_length;
  last_code_length_ = code_length;
}

void PasswordManager::get_state(Promise<State> promise) {
  do_get_state(PromiseCreator::lambda([promise = std::move(promise)](Result<PasswordState> r_state) mutable {
    if (r_state.is_error()) {
      return promise.set_error(r_state.move_as_error());
    }
    promise.set_value(r_state.move_as_ok().get_password_state_object());
  }));
}

void PasswordManager::do_get_state(Promise<PasswordState> promise) {
  auto query = G()->net_query_creator().create(telegram_api::account_getPassword());
  send_with_promise(
      std::move(query), PromiseCreator::lambda([actor_id = actor_id(this), code_length = last_code_length_,
                                                promise = std::move(promise)](Result<NetQueryPtr> r_query) mutable {
        auto r_result = fetch_result<telegram_api::account_getPassword>(std::move(r_query));
        if (r_result.is_error()) {
          return promise.set_error(r_result.move_as_error());
        }
        auto password = r_result.move_as_ok();
        LOG(INFO) << "Receive password info: " << to_string(password);
        Random::add_seed(password->secure_random_.as_slice());

        PasswordState state;
        if (password->current_algo_ != nullptr) {
          state.has_password = true;

          switch (password->current_algo_->get_id()) {
            case telegram_api::passwordKdfAlgoUnknown::ID:
              return promise.set_error(Status::Error(400, "Please update client to continue"));
            case telegram_api::passwordKdfAlgoSHA256SHA256PBKDF2HMACSHA512iter100000SHA256ModPow::ID: {
              auto algo =
                  move_tl_object_as<telegram_api::passwordKdfAlgoSHA256SHA256PBKDF2HMACSHA512iter100000SHA256ModPow>(
                      password->current_algo_);
              state.current_client_salt = algo->salt1_.as_slice().str();
              state.current_server_salt = algo->salt2_.as_slice().str();
              state.current_srp_g = algo->g_;
              state.current_srp_p = algo->p_.as_slice().str();
              break;
            }
            default:
              UNREACHABLE();
          }
          state.current_srp_B = password->srp_B_.as_slice().str();
          state.current_srp_id = password->srp_id_;
          state.password_hint = std::move(password->hint_);
          state.has_recovery_email_address =
              (password->flags_ & telegram_api::account_password::HAS_RECOVERY_MASK) != 0;
          state.has_secure_values = (password->flags_ & telegram_api::account_password::HAS_SECURE_VALUES_MASK) != 0;
        } else {
          state.has_password = false;
          send_closure(actor_id, &PasswordManager::drop_cached_secret);
        }
        state.unconfirmed_recovery_email_address_pattern = std::move(password->email_unconfirmed_pattern_);
        state.code_length = code_length;

        CHECK(password->new_algo_ != nullptr);
        switch (password->new_algo_->get_id()) {
          case telegram_api::passwordKdfAlgoUnknown::ID:
            return promise.set_error(Status::Error(400, "Please update client to continue"));
          case telegram_api::passwordKdfAlgoSHA256SHA256PBKDF2HMACSHA512iter100000SHA256ModPow::ID: {
            auto algo =
                move_tl_object_as<telegram_api::passwordKdfAlgoSHA256SHA256PBKDF2HMACSHA512iter100000SHA256ModPow>(
                    password->new_algo_);
            state.new_client_salt = algo->salt1_.as_slice().str();
            state.new_server_salt = algo->salt2_.as_slice().str();
            state.new_srp_g = algo->g_;
            state.new_srp_p = algo->p_.as_slice().str();
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
  LOG(INFO) << "Cache passport secret";
  secret_ = std::move(secret);

  const int32 max_cache_time = 3600;
  secret_expire_date_ = Time::now() + max_cache_time;
  set_timeout_at(secret_expire_date_);
}

void PasswordManager::drop_cached_secret() {
  LOG(INFO) << "Drop passport secret";
  secret_ = optional<secure_storage::Secret>();
}

void PasswordManager::timeout_expired() {
  if (Time::now() >= secret_expire_date_) {
    drop_cached_secret();
  }
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
