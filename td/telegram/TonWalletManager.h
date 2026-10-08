//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/files/FileId.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/net/DcId.h"
#include "td/telegram/PhotoSize.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UserId.h"

#include "td/actor/actor.h"

#include "td/e2e/e2e_api.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/Promise.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"

namespace td {

class Td;

class TonWalletManager final : public Actor {
 public:
  TonWalletManager(Td *td, ActorShared<> parent);

  static Status check_ton_address(Slice address);

  void on_update_wallet_state(telegram_api::object_ptr<telegram_api::WalletState> &&wallet_state);

  void on_update_wallet_gasless_info(telegram_api::object_ptr<telegram_api::updateWalletGaslessInfo> &&wallet_info);

  void on_update_wallet_ton_connect_session(telegram_api::object_ptr<telegram_api::tonConnectSession> &&session);

  void get_wallet_state(Promise<Unit> &&promise);

  void reload_wallet_state(Promise<Unit> &&promise);

  void get_existing_wallet_balance(Promise<td_api::object_ptr<td_api::walletBotBalance>> &&promise);

  void get_user_addresses(vector<UserId> user_ids,
                          Promise<td_api::object_ptr<td_api::userTonWalletAddresses>> &&promise);

  void create_user_ton_wallet(UserId user_id, Promise<td_api::object_ptr<td_api::userTonWalletAddress>> &&promise);

  void get_address_ton_wallet(const string &address,
                              Promise<td_api::object_ptr<td_api::userTonWalletAddress>> &&promise);

  void get_ton_wallet_secret_phrase(const string &password, Promise<string> &&promise);

  void get_ton_wallet_proof_challenge(Promise<td_api::object_ptr<td_api::tonWalletOwnershipProofChallenge>> &&promise);

  void enable_ton_wallet_backup(const string &secret_phrase,
                                td_api::object_ptr<td_api::tonWalletOwnershipProof> &&ownership_proof,
                                Promise<Unit> &&promise);

  void disable_ton_wallet_backup(const string &password, Promise<Unit> &&promise);

  void disable_ton_wallet_backup_with_proof(td_api::object_ptr<td_api::tonWalletOwnershipProof> &&ownership_proof,
                                            Promise<Unit> &&promise);

  void get_ton_wallet_gasless_info(Promise<Unit> &&promise);

  void send_ton_wallet_transfer(const string &data_normal, const string &data_gasless, UserId peer_user_id,
                                const string &peer_address, int64 amount, const string &comment,
                                bool is_comment_encrypted, int32 sending_id,
                                Promise<td_api::object_ptr<td_api::tonWalletTransferResult>> &&promise);

  void delete_ton_wallet(const string &password, Promise<Unit> &&promise);

  void replace_ton_wallet(const string &password, const string &anchor_public_key,
                          td_api::object_ptr<td_api::tonWalletOwnershipProof> &&ownership_proof,
                          Promise<Unit> &&promise);

  void get_ton_wallet_transactions(const string &offset, int32 limit,
                                   td_api::object_ptr<td_api::TransactionDirection> &&direction,
                                   Promise<td_api::object_ptr<td_api::tonWalletTransactions>> &&promise);

  void get_ton_wallet_transaction(const string &transaction_id,
                                  Promise<td_api::object_ptr<td_api::tonWalletTransaction>> &&promise);

  void get_ton_wallet_transaction_by_msg_hash(const string &msg_hash,
                                              Promise<td_api::object_ptr<td_api::tonWalletTransaction>> &&promise);

  void get_currency_rates(Promise<td_api::object_ptr<td_api::currencyExchangeRates>> &&promise);

  void get_nfts(const string &offset, int32 limit, Promise<td_api::object_ptr<td_api::tonNfts>> &&promise);

  void get_ton_connect_sessions(Promise<td_api::object_ptr<td_api::tonConnectSessions>> &&promise);

  void create_ton_connect_session(const string &dapp_client_id, const string &manifest_url,
                                  Promise<td_api::object_ptr<td_api::tonConnectSession>> &&promise);

  void register_ton_connect_key(int64 session_id, const string &client_id,
                                Promise<td_api::object_ptr<td_api::tonConnectChallenge>> &&promise);

  void submit_ton_connect_result(int64 session_id, const string &challenge_answer, bool is_error, const string &body,
                                 const string &trace_id, Promise<Unit> &&promise);

  void get_ton_connect_requests(bool by_dapp, int64 session_id, const string &dapp_client_id,
                                Promise<td_api::object_ptr<td_api::tonConnectRequests>> &&promise);

  void claim_ton_connect_request(int64 session_id, MessageId message_id, const string &dapp_request_id,
                                 bool is_rejected, Promise<Unit> &&promise);

  void submit_ton_connect_response(int64 session_id, MessageId message_id, const string &trace_id, const string &body,
                                   Promise<Unit> &&promise);

  void get_ton_connect_next_event_id(int64 session_id,
                                     Promise<td_api::object_ptr<td_api::tonConnectSessionEventId>> &&promise);

  void close_ton_connect_session(int64 session_id, const string &body, Promise<Unit> &&promise);

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
                              const string &memo, const string &theme, const string &success_return_url,
                              const string &fail_return_url,
                              Promise<td_api::object_ptr<td_api::onRampPaymentSession>> &&promise);

  void perform_ton_center_api_request(const string &endpoint,
                                      td_api::object_ptr<td_api::TonCenterApiRequestType> &&type,
                                      Promise<string> &&promise);

  void get_ton_center_streaming_api_url(Promise<td_api::object_ptr<td_api::tonCenterStreamingApiUrl>> &&promise);

  static td_api::object_ptr<td_api::tonWalletTransaction> get_ton_wallet_transaction_object(
      Td *td, telegram_api::object_ptr<telegram_api::walletTransaction> &&transaction);

  void get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const;

 private:
  class GetTonNftsQuery;
  class GetTonConnectSessionsQuery;
  class CreateTonConnectSessionQuery;
  class RegisterTonConnectKeyQuery;
  class GetTonConnectPendingQuery;
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

  class WalletGaslessInfo {
    bool is_available_ = false;
    int32 left_ = 0;
    int32 reset_date_ = 0;
    int64 min_amount_ = 0;
    string relayer_address_;

    friend bool operator==(const WalletGaslessInfo &lhs, const WalletGaslessInfo &rhs);

   public:
    WalletGaslessInfo() = default;

    explicit WalletGaslessInfo(telegram_api::object_ptr<telegram_api::updateWalletGaslessInfo> &&wallet_info);

    int32 try_reset();

    td_api::object_ptr<td_api::tonWalletGaslessTransfersInfo> get_ton_wallet_gasless_transfers_info_object() const;
  };

  friend bool operator==(const WalletGaslessInfo &lhs, const WalletGaslessInfo &rhs);

  class NftAttribute {
    string trait_type_;
    string value_;

   public:
    explicit NftAttribute(telegram_api::object_ptr<telegram_api::wallet_nftAttribute> &&attribute);

    td_api::object_ptr<td_api::tonNftAttribute> get_ton_nft_attribute_object() const;
  };

  class Nft {
    string collection_address_;
    string address_;
    string owner_address_;
    string index_;
    string name_;
    string description_;
    PhotoSize image_;
    PhotoSize image_small_;
    FileId content_url_file_id_;
    FileId lottie_file_id_;
    vector<NftAttribute> attributes_;
    string extra_;

   public:
    Nft(Td *td, telegram_api::object_ptr<telegram_api::wallet_nftItem> &&item);

    td_api::object_ptr<td_api::tonNft> get_ton_nft_object(Td *td) const;
  };

  class TonConnectManifest {
    string url_;
    string name_;
    FileId icon_file_id_;

   public:
    TonConnectManifest(Td *td, telegram_api::object_ptr<telegram_api::tonConnectManifest> &&manifest);

    td_api::object_ptr<td_api::TonConnectManifest> get_ton_connect_manifest_object(Td *td) const;
  };

  class TonConnectSession {
    int64 id_ = 0;
    string dapp_client_id_;
    string client_id_;
    string nonce_;
    unique_ptr<TonConnectManifest> manifest_;
    int32 manifest_error_ = 0;
    int32 date_ = 0;
    bool is_pending_ = false;
    bool is_closing_ = false;
    bool is_closed_ = false;

   public:
    TonConnectSession(Td *td, telegram_api::object_ptr<telegram_api::tonConnectSession> &&session);

    td_api::object_ptr<td_api::tonConnectSession> get_ton_connect_session_object(Td *td) const;
  };

  class TonConnectRequest {
    int64 session_id_ = 0;
    MessageId message_id_;
    string body_;
    int32 expire_date_;
    string topic_;
    string trace_id_;

   public:
    explicit TonConnectRequest(telegram_api::object_ptr<telegram_api::tonConnectRequest> &&request);

    td_api::object_ptr<td_api::tonConnectRequest> get_ton_connect_request_object() const;
  };

  class TonConnectRequests {
    TonConnectSession session_;
    vector<TonConnectRequest> requests_;

   public:
    TonConnectRequests(Td *td, telegram_api::object_ptr<telegram_api::wallet_tonConnectPending> &&requests);

    td_api::object_ptr<td_api::tonConnectRequests> get_ton_connect_requests_object(Td *td) const;
  };

  class TonConnectChallenge {
    string challenge_;
    int64 event_id_ = 0;

   public:
    explicit TonConnectChallenge(telegram_api::object_ptr<telegram_api::wallet_tonConnectChallenge> &&challenge);

    td_api::object_ptr<td_api::tonConnectChallenge> get_ton_connect_challenge_object() const;
  };

  struct WalletOwnershipProof {
    BufferSlice public_key_;
    telegram_api::object_ptr<telegram_api::walletOwnershipProof> proof_;

    static Result<WalletOwnershipProof> get_wallet_ownership_proof(
        td_api::object_ptr<td_api::tonWalletOwnershipProof> &&proof);
  };

  void timeout_expired() final;

  void tear_down() final;

  td_api::object_ptr<td_api::updateTonWalletState> get_update_ton_wallet_state_object() const;

  void send_update_ton_wallet_state() const;

  void on_get_wallet_state(Result<Unit> &&result);

  td_api::object_ptr<td_api::updateTonWalletGaslessTransfersInfo> get_update_ton_wallet_gasless_transfers_info_object()
      const;

  void send_update_ton_wallet_gasless_transfers_info() const;

  void do_send_ton_wallet_transfer(const string &data_normal, const string &data_gasless, UserId peer_user_id,
                                   const string &peer_address, int64 amount, const string &comment,
                                   bool is_comment_encrypted, int32 sending_id,
                                   Promise<td_api::object_ptr<td_api::tonWalletTransferResult>> &&promise);

  void do_get_ton_wallet_secret_phrase(telegram_api::object_ptr<telegram_api::InputCheckPasswordSRP> &&input_password,
                                       Promise<string> &&promise);

  void do_get_ton_wallet_secret_phrase_with_parts(
      telegram_api::object_ptr<telegram_api::wallet_secretPhraseParts> &&parts, Promise<string> &&promise);

  static Result<BufferSlice> encrypt_secret_phrase_part(Slice data, tde2e_api::PublicKeyId dc_public_key_id);

  static Result<string> descrypt_secret_phrase_part(Slice data, tde2e_api::PrivateKeyId private_key_id);

  void on_get_ton_wallet_secret_phrase_part(
      Result<telegram_api::object_ptr<telegram_api::wallet_encryptedSecretPhrasePart>> r_part, uint64 query_id,
      tde2e_api::PrivateKeyId private_key_id);

  void do_disable_ton_wallet_backup(telegram_api::object_ptr<telegram_api::InputCheckPasswordSRP> &&input_password,
                                    Promise<Unit> &&promise);

  void do_delete_ton_wallet(telegram_api::object_ptr<telegram_api::InputCheckPasswordSRP> &&input_password,
                            Promise<Unit> &&promise);

  void do_replace_ton_wallet_with_proof(telegram_api::object_ptr<telegram_api::InputCheckPasswordSRP> &&input_password,
                                        const string &anchor_public_key, WalletOwnershipProof &&proof,
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

  static td_api::object_ptr<td_api::TonConnectManifest> get_ton_connect_manifest_object(
      Td *td, const TonConnectManifest *manifest, int32 manifest_error);

  Td *td_;
  ActorShared<> parent_;

  bool is_wallet_state_inited_ = false;
  double next_wallet_state_reload_at_ = 0;
  WalletState wallet_state_;

  vector<Promise<Unit>> get_wallet_state_queries_;

  bool is_wallet_gasless_info_inited_ = false;
  WalletGaslessInfo wallet_gasless_info_;

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
