//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/net/DcId.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UserId.h"

#include "td/actor/actor.h"

#include "td/e2e/e2e_api.h"

#include "td/utils/common.h"
#include "td/utils/Promise.h"

namespace td {

class Td;

class TonWalletManager final : public Actor {
 public:
  TonWalletManager(Td *td, ActorShared<> parent);

  void on_update_wallet_state(telegram_api::object_ptr<telegram_api::WalletState> &&wallet_state);

  void get_wallet_state(Promise<Unit> &&promise);

  void get_user_addresses(vector<UserId> user_ids,
                          Promise<td_api::object_ptr<td_api::userTonWalletAddresses>> &&promise);

  void create_user_ton_wallet(UserId user_id, Promise<string> &&promise);

  void get_ton_wallet_secret_phrase(Promise<string> &&promise);

  void enable_ton_wallet_backup(const string &password, const string &secret_phrase, Promise<Unit> &&promise);

  void disable_ton_wallet_backup(const string &password, Promise<Unit> &&promise);

  void get_ton_wallet_gasless_info(Promise<td_api::object_ptr<td_api::tonWalletGaslessTransfersInfo>> &&promise);

  void send_ton_wallet_transfer(const string &data_normal, const string &data_gasless,
                                Promise<td_api::object_ptr<td_api::tonWalletTransferResult>> &&promise);

  void delete_ton_wallet(const string &password, Promise<Unit> &&promise);

  void get_ton_wallet_transactions(const string &offset, int32 limit,
                                   td_api::object_ptr<td_api::TransactionDirection> &&direction,
                                   Promise<td_api::object_ptr<td_api::tonWalletTransactions>> &&promise);

  void get_ton_wallet_transaction(const string &transaction_id,
                                  Promise<td_api::object_ptr<td_api::tonWalletTransaction>> &&promise);

  void get_ton_wallet_transaction_by_msg_hash(const string &msg_hash,
                                              Promise<td_api::object_ptr<td_api::tonWalletTransaction>> &&promise);

  void get_currency_rates(Promise<td_api::object_ptr<td_api::currencyExchangeRates>> &&promise);

  void get_on_ramp_providers(const string &cryptocurrency,
                             Promise<td_api::object_ptr<td_api::onRampProviders>> &&promise);

  void get_on_ramp_base_currencies(const string &provider, const string &cryptocurrency,
                                   Promise<td_api::object_ptr<td_api::currencies>> &&promise);

  void get_on_ramp_availability(const string &provider, const string &cryptocurrency, const string &base_currency,
                                Promise<td_api::object_ptr<td_api::onRampPaymentAvailability>> &&promise);

  void get_on_ramp_limits(const string &provider, const string &cryptocurrency, const string &base_currency,
                          const string &payment_method,
                          Promise<td_api::object_ptr<td_api::onRampPaymentLimits>> &&promise);

  void get_on_ramp_quote(const string &provider, const string &cryptocurrency, const string &base_currency,
                         const string &cryptocurrency_amount, const string &base_currency_amount,
                         const string &payment_method,
                         Promise<td_api::object_ptr<td_api::onRampPaymentQuote>> &&promise);

  void create_on_ramp_session(const string &provider, const string &cryptocurrency, const string &address,
                              const string &payment_method, const string &base_currency,
                              const string &cryptocurrency_amount, const string &base_currency_amount,
                              const string &memo, td_api::object_ptr<td_api::themeParameters> theme,
                              const string &success_return_url, const string &fail_return_url,
                              Promise<td_api::object_ptr<td_api::onRampPaymentSession>> &&promise);

  void perform_ton_center_api_request(const string &endpoint,
                                      td_api::object_ptr<td_api::TonCenterApiRequestType> &&type,
                                      Promise<string> &&promise);

  void get_ton_center_streaming_api_url(Promise<td_api::object_ptr<td_api::tonCenterStreamingApiUrl>> &&promise);

  void get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const;

 private:
  class GetOnRampLimitsQuery;
  class GetOnRampQuoteQuery;
  class CreateOnRampSessionQuery;

  static constexpr size_t MAX_MNEMONIC_BACKUP_SIZE = 215u;

  class WalletState {
    bool is_being_created_ = false;
    bool is_backup_enabled_ = false;
    bool can_export_phrase_ = false;
    bool can_enable_backup_ = false;
    string address_;
    string public_key_;
    int64 balance_ = 0;

    friend bool operator==(const WalletState &lhs, const WalletState &rhs);

   public:
    WalletState() = default;

    explicit WalletState(telegram_api::object_ptr<telegram_api::WalletState> &&wallet_state);

    td_api::object_ptr<td_api::tonWalletState> get_ton_wallet_state_object() const;
  };

  friend bool operator==(const WalletState &lhs, const WalletState &rhs);

  void tear_down() final;

  td_api::object_ptr<td_api::updateTonWalletState> get_update_ton_wallet_state() const;

  void send_update_ton_wallet_state() const;

  void on_get_wallet_state(Result<Unit> &&result);

  void do_get_ton_wallet_secret_phrase(Promise<string> &&promise);

  void do_get_ton_wallet_secret_phrase_with_parts(
      telegram_api::object_ptr<telegram_api::wallet_secretPhraseParts> &&parts, Promise<string> &&promise);

  static Result<BufferSlice> encrypt_secret_phrase_part(Slice data, tde2e_api::PublicKeyId dc_public_key_id);

  static Result<string> descrypt_secret_phrase_part(Slice data, tde2e_api::PrivateKeyId private_key_id);

  void on_get_ton_wallet_secret_phrase_part(
      Result<telegram_api::object_ptr<telegram_api::wallet_encryptedSecretPhrasePart>> r_part, uint64 query_id,
      tde2e_api::PrivateKeyId private_key_id);

  void do_enable_ton_wallet_backup(telegram_api::object_ptr<telegram_api::InputCheckPasswordSRP> &&input_password,
                                   const string &secret_phrase, Promise<Unit> &&promise);

  void do_disable_ton_wallet_backup(telegram_api::object_ptr<telegram_api::InputCheckPasswordSRP> &&input_password,
                                    Promise<Unit> &&promise);

  void do_delete_ton_wallet(telegram_api::object_ptr<telegram_api::InputCheckPasswordSRP> &&input_password,
                            Promise<Unit> &&promise);

  void do_get_ton_wallet_transactions(const string &offset, int32 limit,
                                      td_api::object_ptr<td_api::TransactionDirection> &&direction,
                                      Promise<td_api::object_ptr<td_api::tonWalletTransactions>> &&promise);

  void do_get_ton_wallet_transaction(const string &transaction_id,
                                     Promise<td_api::object_ptr<td_api::tonWalletTransaction>> &&promise);

  void do_get_ton_wallet_transaction_by_msg_hash(const string &msg_hash,
                                                 Promise<td_api::object_ptr<td_api::tonWalletTransaction>> &&promise);

  void on_get_currency_rates(Result<telegram_api::object_ptr<telegram_api::payments_currencyRates>> r_rates);

  td_api::object_ptr<td_api::currencyExchangeRates> get_currency_exchange_rates_object() const;

  void on_get_on_ramp_providers(Result<vector<telegram_api::object_ptr<telegram_api::onrampProviderInfo>>> r_providers,
                                Promise<td_api::object_ptr<td_api::onRampProviders>> &&promise);

  void on_get_ton_center_streaming_api_url(
      Result<telegram_api::object_ptr<telegram_api::toncenter_streamingUrl>> r_url);

  void load_backup_holder_dcs(Promise<Unit> &&promise);

  void on_get_backup_holder_dcs(Result<vector<telegram_api::object_ptr<telegram_api::wallet_holderDc>>> r_dcs);

  Td *td_;
  ActorShared<> parent_;

  bool is_wallet_state_inited_ = false;
  double next_wallet_state_reload_at_ = 0;
  WalletState wallet_state_;

  vector<Promise<Unit>> get_wallet_state_queries_;

  struct CurrencyRate {
    string currency_;
    double rate_ = 0.0;
  };
  struct CurrencyRates {
    vector<CurrencyRate> rates_;
    double expires_at_ = 0.0;
  };
  CurrencyRates currency_rates_;
  vector<Promise<td_api::object_ptr<td_api::currencyExchangeRates>>> get_currency_rates_queries_;

  class OnRampProvider {
    string id_;
    string name_;
    vector<string> cryptocurrencies_;
    bool supports_base_currencies_ = false;
    bool supports_limits_ = false;
    bool supports_quote_ = false;

   public:
    explicit OnRampProvider(telegram_api::object_ptr<telegram_api::onrampProviderInfo> &&info);

    td_api::object_ptr<td_api::onRampProvider> get_on_ramp_provider_object() const;
  };

  class OnRampLimits {
    string payment_method_;
    string base_min_amount_;
    string base_max_amount_;
    string crypto_min_amount_;
    string crypto_max_amount_;

   public:
    explicit OnRampLimits(telegram_api::object_ptr<telegram_api::onrampLimits> &&limits);

    td_api::object_ptr<td_api::onRampPaymentLimits> get_on_ramp_payment_limits_object() const;
  };

  class OnRampQuote {
    string payment_method_;
    int32 expires_date_ = 0;
    string base_amount_;
    string crypto_amount_;
    string crypto_price_;
    string fee_amount_;
    string extra_fee_amount_;
    string network_fee_amount_;
    string total_amount_;

   public:
    explicit OnRampQuote(telegram_api::object_ptr<telegram_api::onrampQuote> &&quote);

    td_api::object_ptr<td_api::onRampPaymentQuote> get_on_ramp_payment_quote_object() const;
  };

  class OnRampSession {
    string session_id_;
    int32 expires_date_ = 0;
    string url_;

   public:
    explicit OnRampSession(telegram_api::object_ptr<telegram_api::onrampSession> &&session);

    td_api::object_ptr<td_api::onRampPaymentSession> get_on_ramp_payment_session_object() const;
  };

  struct StreamingApiUrl {
    string url_;
    int32 expiration_date_ = 0;
  };
  StreamingApiUrl streaming_api_url_;
  vector<Promise<td_api::object_ptr<td_api::tonCenterStreamingApiUrl>>> get_streaming_api_url_queries_;

  struct BackupHolderDc {
    DcId dc_id_;
    string public_key_;
    tde2e_api::PublicKeyId public_key_id_;
  };
  struct BackupHolderDcs {
    vector<BackupHolderDc> dcs_;
  };
  BackupHolderDcs backup_holder_dcs_;
  vector<Promise<Unit>> get_backup_holder_dcs_queries_;

  struct GetSecretPhraseQuery {
    Promise<string> promise_;
    string result_;
    size_t left_responses_ = 0;
  };
  uint64 current_get_secret_phrase_query_id_ = 0;
  FlatHashMap<uint64, GetSecretPhraseQuery> get_secret_phrase_queries_;
};

}  // namespace td
