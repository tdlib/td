//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/TonWalletManager.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/ChatManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/misc.h"
#include "td/telegram/Td.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/ThemeManager.h"
#include "td/telegram/UserManager.h"

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"
#include "td/utils/Random.h"
#include "td/utils/Time.h"

#include <type_traits>

namespace td {

class GetWalletStateQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit GetWalletStateQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create(telegram_api::wallet_getState()));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::wallet_getState>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    td_->ton_wallet_manager_->on_update_wallet_state(result_ptr.move_as_ok());
    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetUserWalletAddressesQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::userTonWalletAddresses>> promise_;

 public:
  explicit GetUserWalletAddressesQuery(Promise<td_api::object_ptr<td_api::userTonWalletAddresses>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(vector<telegram_api::object_ptr<telegram_api::InputUser>> &&input_users) {
    send_query(G()->net_query_creator().create(
        telegram_api::wallet_getUserAddresses(0, false, std::move(input_users), vector<string>())));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::wallet_getUserAddresses>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetUserWalletAddressesQuery: " << to_string(result);

    td_->user_manager_->on_get_users(std::move(result->users_), "GetUserWalletAddressesQuery");

    vector<td_api::object_ptr<td_api::userTonWalletAddress>> addresses;
    for (auto &address : result->addresses_) {
      auto user_id = UserId(address->user_id_);
      td_->user_manager_->on_update_user_gram_address(user_id, address->address_);
      addresses.push_back(td_api::make_object<td_api::userTonWalletAddress>(
          td_->user_manager_->get_user_id_object(user_id, "userTonWalletAddress"), address->address_));
    }
    promise_.set_value(td_api::make_object<td_api::userTonWalletAddresses>(std::move(addresses)));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class CreateUserWalletAddressQuery final : public Td::ResultHandler {
  Promise<string> promise_;

 public:
  explicit CreateUserWalletAddressQuery(Promise<string> &&promise) : promise_(std::move(promise)) {
  }

  void send(telegram_api::object_ptr<telegram_api::InputUser> &&input_user) {
    vector<telegram_api::object_ptr<telegram_api::InputUser>> input_users;
    input_users.push_back(std::move(input_user));
    send_query(G()->net_query_creator().create(
        telegram_api::wallet_getUserAddresses(0, true, std::move(input_users), vector<string>())));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::wallet_getUserAddresses>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for CreateUserWalletAddressQuery: " << to_string(result);

    td_->user_manager_->on_get_users(std::move(result->users_), "GetUserWalletAddressesQuery");

    if (result->addresses_.size() != 1u) {
      return on_error(Status::Error(400, "Failed to create TON wallet address"));
    }

    auto user_id = UserId(result->addresses_[0]->user_id_);
    td_->user_manager_->on_update_user_gram_address(user_id, result->addresses_[0]->address_);
    promise_.set_value(std::move(result->addresses_[0]->address_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetWalletGaslessInfoQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::tonWalletGaslessTransfersInfo>> promise_;

 public:
  explicit GetWalletGaslessInfoQuery(Promise<td_api::object_ptr<td_api::tonWalletGaslessTransfersInfo>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create(telegram_api::wallet_getGaslessInfo()));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::wallet_getGaslessInfo>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetWalletGaslessInfoQuery: " << to_string(result);
    promise_.set_value(nullptr);
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class SendWalletTransferQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::tonWalletTransferResult>> promise_;

 public:
  explicit SendWalletTransferQuery(Promise<td_api::object_ptr<td_api::tonWalletTransferResult>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(const string &data_normal, const string &data_gasless) {
    int32 flags = 0;
    if (!data_gasless.empty()) {
      flags |= telegram_api::wallet_sendTransfer::DATA_GASLESS_MASK;
    }
    send_query(G()->net_query_creator().create(telegram_api::wallet_sendTransfer(
        flags, BufferSlice(data_normal), BufferSlice(data_gasless),
        telegram_api::make_object<telegram_api::inputUserEmpty>(), Random::secure_int64())));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::wallet_sendTransfer>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for SendWalletTransferQuery: " << to_string(result);
    promise_.set_value(nullptr);
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

static td_api::object_ptr<td_api::tonWalletTransaction> get_ton_wallet_transaction_object(
    const Td *td, telegram_api::object_ptr<telegram_api::walletTransaction> &&transaction) {
  string peer_address;
  UserId peer_user_id;
  string peer_domain;
  switch (transaction->peer_->get_id()) {
    case telegram_api::walletTransactionPeerUser::ID: {
      auto peer = telegram_api::move_object_as<telegram_api::walletTransactionPeerUser>(transaction->peer_);
      peer_user_id = UserId(peer->user_id_);
      if (!peer_user_id.is_valid()) {
        LOG(ERROR) << "Receive " << peer_user_id;
        peer_user_id = UserId();
      }
      peer_address = std::move(peer->address_);
      peer_domain = std::move(peer->domain_);
      break;
    }
    case telegram_api::walletTransactionPeerAddress::ID: {
      auto peer = telegram_api::move_object_as<telegram_api::walletTransactionPeerAddress>(transaction->peer_);
      peer_address = std::move(peer->address_);
      peer_domain = std::move(peer->domain_);
      break;
    }
    case telegram_api::walletTransactionPeerOnramp::ID:
      break;
    case telegram_api::walletTransactionPeerUnsupported::ID:
      break;
    default:
      UNREACHABLE();
  }
  auto amount = transaction->amount_;
  if (!transaction->incoming_ && amount > 0) {
    amount = -amount;
  }
  auto type = [&]() -> td_api::object_ptr<td_api::TonWalletTransactionState> {
    if (transaction->failed_) {
      return td_api::make_object<td_api::tonWalletTransactionStateFailed>();
    }
    return td_api::make_object<td_api::tonWalletTransactionStateSucceeded>(transaction->tx_hash_);
  }();
  return td_api::make_object<td_api::tonWalletTransaction>(
      transaction->id_, peer_address, td->user_manager_->get_user_id_object(peer_user_id, "tonWalletTransaction"),
      peer_domain, amount, transaction->fee_, transaction->comment_, transaction->comment_encrypted_,
      transaction->date_, std::move(type));
}

class GetTonWalletTransactionsQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::tonWalletTransactions>> promise_;

 public:
  explicit GetTonWalletTransactionsQuery(Promise<td_api::object_ptr<td_api::tonWalletTransactions>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(const string &offset, int32 limit, td_api::object_ptr<td_api::TransactionDirection> &&direction) {
    bool inbound = false;
    bool outbound = false;
    if (direction != nullptr) {
      switch (direction->get_id()) {
        case td_api::transactionDirectionIncoming::ID:
          inbound = true;
          break;
        case td_api::transactionDirectionOutgoing::ID:
          outbound = true;
          break;
        default:
          UNREACHABLE();
      }
    }
    send_query(
        G()->net_query_creator().create(telegram_api::wallet_getTransactions(0, inbound, outbound, offset, limit)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::wallet_getTransactions>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetTonWalletTransactionsQuery: " << to_string(result);

    td_->user_manager_->on_get_users(std::move(result->users_), "GetTonWalletTransactionsQuery");
    td_->chat_manager_->on_get_chats(std::move(result->chats_), "GetTonWalletTransactionsQuery");

    vector<td_api::object_ptr<td_api::tonWalletTransaction>> transactions;
    for (auto &transaction : result->transactions_) {
      transactions.push_back(get_ton_wallet_transaction_object(td_, std::move(transaction)));
    }
    promise_.set_value(td_api::make_object<td_api::tonWalletTransactions>(result->balance_, std::move(transactions),
                                                                          result->next_offset_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetTonWalletTransactionQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::tonWalletTransaction>> promise_;

 public:
  explicit GetTonWalletTransactionQuery(Promise<td_api::object_ptr<td_api::tonWalletTransaction>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(bool by_msg_hash, const string &id) {
    vector<string> ids;
    ids.push_back(std::move(id));
    if (by_msg_hash) {
      send_query(G()->net_query_creator().create(telegram_api::wallet_getTransactionsByMsgHash(std::move(ids))));
    } else {
      send_query(G()->net_query_creator().create(telegram_api::wallet_getTransactionsByIDs(std::move(ids))));
    }
  }

  void on_result(BufferSlice packet) final {
    static_assert(std::is_same<telegram_api::wallet_getTransactionsByMsgHash::ReturnType,
                               telegram_api::wallet_getTransactionsByIDs::ReturnType>::value);
    auto result_ptr = fetch_result<telegram_api::wallet_getTransactionsByIDs>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetTonWalletTransactionQuery: " << to_string(result);

    td_->user_manager_->on_get_users(std::move(result->users_), "GetTonWalletTransactionQuery");
    td_->chat_manager_->on_get_chats(std::move(result->chats_), "GetTonWalletTransactionQuery");
    if (result->transactions_.empty()) {
      return promise_.set_value(nullptr);
    }
    if (result->transactions_.size() != 1u) {
      LOG(ERROR) << "Receive " << to_string(result);
      return on_error(Status::Error(500, "Receive invalid respomse"));
    }

    promise_.set_value(get_ton_wallet_transaction_object(td_, std::move(result->transactions_[0])));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetCurrencyRatesQuery final : public Td::ResultHandler {
  Promise<telegram_api::object_ptr<telegram_api::payments_currencyRates>> promise_;

 public:
  explicit GetCurrencyRatesQuery(Promise<telegram_api::object_ptr<telegram_api::payments_currencyRates>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create(telegram_api::payments_getCurrencyRates()));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_getCurrencyRates>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    promise_.set_value(std::move(result));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetOnRampProvidersQuery final : public Td::ResultHandler {
  Promise<vector<telegram_api::object_ptr<telegram_api::onrampProviderInfo>>> promise_;

 public:
  explicit GetOnRampProvidersQuery(
      Promise<vector<telegram_api::object_ptr<telegram_api::onrampProviderInfo>>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(const string &cryptocurrency) {
    int32 flags = 0;
    if (!cryptocurrency.empty()) {
      flags |= telegram_api::payments_getOnrampProviders::CRYPTO_CURRENCY_MASK;
    }
    send_query(G()->net_query_creator().create(telegram_api::payments_getOnrampProviders(flags, cryptocurrency)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_getOnrampProviders>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    promise_.set_value(std::move(result));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetOnRampBaseCurrenciesQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::currencies>> promise_;

 public:
  explicit GetOnRampBaseCurrenciesQuery(Promise<td_api::object_ptr<td_api::currencies>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(const string &provider, const string &cryptocurrency) {
    send_query(
        G()->net_query_creator().create(telegram_api::payments_getOnrampBaseCurrencies(provider, cryptocurrency)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_getOnrampBaseCurrencies>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    promise_.set_value(td_api::make_object<td_api::currencies>(std::move(result)));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetOnRampAvailabilityQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::onRampPaymentAvailability>> promise_;

 public:
  explicit GetOnRampAvailabilityQuery(Promise<td_api::object_ptr<td_api::onRampPaymentAvailability>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(const string &provider, const string &cryptocurrency, const string &base_currency) {
    int32 flags = 0;
    if (!base_currency.empty()) {
      flags |= telegram_api::payments_getOnrampAvailability::BASE_CURRENCY_MASK;
    }
    send_query(G()->net_query_creator().create(
        telegram_api::payments_getOnrampAvailability(flags, provider, cryptocurrency, base_currency)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_getOnrampAvailability>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    vector<td_api::object_ptr<td_api::onRampPaymentMethod>> methods;
    for (auto &method : result->methods_) {
      methods.push_back(td_api::make_object<td_api::onRampPaymentMethod>(method->payment_method_, method->available_));
    }
    promise_.set_value(td_api::make_object<td_api::onRampPaymentAvailability>(
        result->allowed_, result->buy_allowed_, std::move(methods), result->country_code_, result->state_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class TonWalletManager::GetOnRampLimitsQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::onRampPaymentLimits>> promise_;

 public:
  explicit GetOnRampLimitsQuery(Promise<td_api::object_ptr<td_api::onRampPaymentLimits>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(const string &provider, const string &cryptocurrency, const string &base_currency,
            const string &payment_method) {
    int32 flags = 0;
    if (!payment_method.empty()) {
      flags |= telegram_api::payments_getOnrampLimits::PAYMENT_METHOD_MASK;
    }
    send_query(G()->net_query_creator().create(
        telegram_api::payments_getOnrampLimits(flags, provider, cryptocurrency, base_currency, payment_method)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_getOnrampLimits>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto on_ramp_limits = OnRampLimits(result_ptr.move_as_ok());
    promise_.set_value(on_ramp_limits.get_on_ramp_payment_limits_object());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class TonWalletManager::GetOnRampQuoteQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::onRampPaymentQuote>> promise_;

 public:
  explicit GetOnRampQuoteQuery(Promise<td_api::object_ptr<td_api::onRampPaymentQuote>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(const string &provider, const string &cryptocurrency, const string &base_currency,
            const string &cryptocurrency_amount, const string &base_currency_amount, const string &payment_method) {
    int32 flags = 0;
    if (!base_currency_amount.empty()) {
      flags |= telegram_api::payments_getOnrampQuote::BASE_AMOUNT_MASK;
    }
    if (!cryptocurrency_amount.empty()) {
      flags |= telegram_api::payments_getOnrampQuote::CRYPTO_AMOUNT_MASK;
    }
    if (!payment_method.empty()) {
      flags |= telegram_api::payments_getOnrampQuote::PAYMENT_METHOD_MASK;
    }
    send_query(G()->net_query_creator().create(telegram_api::payments_getOnrampQuote(
        flags, provider, cryptocurrency, base_currency, base_currency_amount, cryptocurrency_amount, payment_method)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_getOnrampQuote>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto on_ramp_quote = OnRampQuote(result_ptr.move_as_ok());
    promise_.set_value(on_ramp_quote.get_on_ramp_payment_quote_object());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class TonWalletManager::CreateOnRampSessionQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::onRampPaymentSession>> promise_;

 public:
  explicit CreateOnRampSessionQuery(Promise<td_api::object_ptr<td_api::onRampPaymentSession>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(const string &provider, const string &cryptocurrency, const string &address, const string &payment_method,
            const string &base_currency, const string &cryptocurrency_amount, const string &base_currency_amount,
            const string &memo, const string &theme, const string &success_return_url, const string &fail_return_url) {
    int32 flags = 0;
    if (!payment_method.empty()) {
      flags |= telegram_api::payments_createOnrampSession::PAYMENT_METHOD_MASK;
    }
    if (!base_currency.empty()) {
      flags |= telegram_api::payments_createOnrampSession::BASE_CURRENCY_MASK;
    }
    if (!cryptocurrency_amount.empty()) {
      flags |= telegram_api::payments_createOnrampSession::CRYPTO_AMOUNT_MASK;
    }
    if (!base_currency_amount.empty()) {
      flags |= telegram_api::payments_createOnrampSession::BASE_AMOUNT_MASK;
    }
    if (!memo.empty()) {
      flags |= telegram_api::payments_createOnrampSession::MEMO_MASK;
    }
    if (!theme.empty()) {
      flags |= telegram_api::payments_createOnrampSession::THEME_MASK;
    }
    if (!success_return_url.empty()) {
      flags |= telegram_api::payments_createOnrampSession::SUCCESS_RETURN_URL_MASK;
    }
    if (!fail_return_url.empty()) {
      flags |= telegram_api::payments_createOnrampSession::FAIL_RETURN_URL_MASK;
    }
    send_query(G()->net_query_creator().create(telegram_api::payments_createOnrampSession(
        flags, provider, cryptocurrency, address, payment_method, base_currency, base_currency_amount, memo, theme,
        success_return_url, fail_return_url, cryptocurrency_amount)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_createOnrampSession>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto on_ramp_session = OnRampSession(result_ptr.move_as_ok());
    promise_.set_value(on_ramp_session.get_on_ramp_payment_session_object());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class PerformTonCenterApiRequestQuery final : public Td::ResultHandler {
  Promise<string> promise_;

 public:
  explicit PerformTonCenterApiRequestQuery(Promise<string> &&promise) : promise_(std::move(promise)) {
  }

  void send(const string &endpoint, bool is_post, const string &query, const string &payload) {
    int32 flags = 0;
    if (!query.empty()) {
      flags |= telegram_api::toncenter_performApiRequest::QUERY_MASK;
    }
    if (!payload.empty()) {
      flags |= telegram_api::toncenter_performApiRequest::PAYLOAD_MASK;
    }
    send_query(G()->net_query_creator().create(
        telegram_api::toncenter_performApiRequest(flags, is_post, endpoint, query, payload), {},
        G()->get_webfile_dc_id()));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::toncenter_performApiRequest>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    promise_.set_value(std::move(result->response_->data_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetTonCenterStreamingApiUrlQuery final : public Td::ResultHandler {
  Promise<telegram_api::object_ptr<telegram_api::toncenter_streamingUrl>> promise_;

 public:
  explicit GetTonCenterStreamingApiUrlQuery(
      Promise<telegram_api::object_ptr<telegram_api::toncenter_streamingUrl>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create(telegram_api::toncenter_getStreamingUrl()));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::toncenter_getStreamingUrl>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    promise_.set_value(std::move(result));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

TonWalletManager::WalletState::WalletState(telegram_api::object_ptr<telegram_api::WalletState> &&wallet_state) {
  CHECK(wallet_state != nullptr);
  switch (wallet_state->get_id()) {
    case telegram_api::walletStateEmpty::ID: {
      auto state = telegram_api::move_object_as<telegram_api::walletStateEmpty>(wallet_state);
      is_being_created_ = state->creating_;
      break;
    }
    case telegram_api::walletState::ID: {
      auto state = telegram_api::move_object_as<telegram_api::walletState>(wallet_state);
      is_backup_enabled_ = state->backup_enabled_;
      can_export_phrase_ = state->can_export_phrase_;
      can_enable_backup_ = state->can_enable_backup_;
      address_ = std::move(state->address_);
      public_key_ = state->public_key_.as_slice().str();
      balance_ = state->balance_;
      break;
    }
    default:
      UNREACHABLE();
  }
}

td_api::object_ptr<td_api::tonWalletState> TonWalletManager::WalletState::get_ton_wallet_state_object() const {
  return td_api::make_object<td_api::tonWalletState>(address_, public_key_, balance_, is_being_created_,
                                                     is_backup_enabled_, can_enable_backup_, can_export_phrase_);
}

bool operator==(const TonWalletManager::WalletState &lhs, const TonWalletManager::WalletState &rhs) {
  return lhs.is_being_created_ == rhs.is_being_created_ && lhs.is_backup_enabled_ == rhs.is_backup_enabled_ &&
         lhs.can_export_phrase_ == rhs.can_export_phrase_ && lhs.can_enable_backup_ == rhs.can_enable_backup_ &&
         lhs.address_ == rhs.address_ && lhs.public_key_ == rhs.public_key_ && lhs.balance_ == rhs.balance_;
}

TonWalletManager::OnRampProvider::OnRampProvider(telegram_api::object_ptr<telegram_api::onrampProviderInfo> &&info)
    : id_(std::move(info->id_))
    , name_(std::move(info->name_))
    , cryptocurrencies_(std::move(info->crypto_currencies_))
    , supports_base_currencies_(info->supports_base_currencies_)
    , supports_limits_(info->supports_limits_)
    , supports_quote_(info->supports_quote_) {
}

td_api::object_ptr<td_api::onRampProvider> TonWalletManager::OnRampProvider::get_on_ramp_provider_object() const {
  return td_api::make_object<td_api::onRampProvider>(id_, name_, vector<string>(cryptocurrencies_),
                                                     supports_base_currencies_, supports_limits_, supports_quote_);
}

TonWalletManager::OnRampLimits::OnRampLimits(telegram_api::object_ptr<telegram_api::onrampLimits> &&limits)
    : payment_method_(std::move(limits->payment_method_))
    , base_min_amount_(std::move(limits->base_min_amount_))
    , base_max_amount_(std::move(limits->base_max_amount_))
    , crypto_min_amount_(std::move(limits->crypto_min_amount_))
    , crypto_max_amount_(std::move(limits->crypto_max_amount_)) {
}

td_api::object_ptr<td_api::onRampPaymentLimits> TonWalletManager::OnRampLimits::get_on_ramp_payment_limits_object()
    const {
  return td_api::make_object<td_api::onRampPaymentLimits>(payment_method_, base_min_amount_, base_max_amount_,
                                                          crypto_min_amount_, crypto_max_amount_);
}

TonWalletManager::OnRampQuote::OnRampQuote(telegram_api::object_ptr<telegram_api::onrampQuote> &&quote)
    : payment_method_(std::move(quote->payment_method_))
    , expires_date_(quote->expires_date_)
    , base_amount_(std::move(quote->base_amount_))
    , crypto_amount_(std::move(quote->crypto_amount_))
    , crypto_price_(std::move(quote->crypto_price_))
    , fee_amount_(std::move(quote->fee_amount_))
    , extra_fee_amount_(std::move(quote->extra_fee_amount_))
    , network_fee_amount_(std::move(quote->network_fee_amount_))
    , total_amount_(std::move(quote->total_amount_)) {
}

td_api::object_ptr<td_api::onRampPaymentQuote> TonWalletManager::OnRampQuote::get_on_ramp_payment_quote_object() const {
  return td_api::make_object<td_api::onRampPaymentQuote>(payment_method_, expires_date_, base_amount_, crypto_amount_,
                                                         crypto_price_, fee_amount_, extra_fee_amount_,
                                                         network_fee_amount_, total_amount_);
}

TonWalletManager::OnRampSession::OnRampSession(telegram_api::object_ptr<telegram_api::onrampSession> &&session)
    : session_id_(std::move(session->session_id_))
    , expires_date_(session->expires_date_)
    , url_(std::move(session->url_)) {
}

td_api::object_ptr<td_api::onRampPaymentSession> TonWalletManager::OnRampSession::get_on_ramp_payment_session_object()
    const {
  return td_api::make_object<td_api::onRampPaymentSession>(session_id_, expires_date_, url_);
}

TonWalletManager::TonWalletManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

void TonWalletManager::tear_down() {
  parent_.reset();
}

void TonWalletManager::on_update_wallet_state(telegram_api::object_ptr<telegram_api::WalletState> &&wallet_state) {
  LOG(INFO) << "Receive " << to_string(wallet_state);
  if (td_->auth_manager_->is_bot()) {
    LOG(ERROR) << "Receive WalletState";
    return;
  }
  auto state = WalletState(std::move(wallet_state));
  if (is_wallet_state_inited_ && state == wallet_state_) {
    return;
  }
  is_wallet_state_inited_ = true;
  next_wallet_state_reload_at_ = Time::now() + 3500;
  wallet_state_ = std::move(state);
  send_update_ton_wallet_state();
}

td_api::object_ptr<td_api::updateTonWalletState> TonWalletManager::get_update_ton_wallet_state() const {
  return td_api::make_object<td_api::updateTonWalletState>(wallet_state_.get_ton_wallet_state_object());
}

void TonWalletManager::send_update_ton_wallet_state() const {
  send_closure(G()->td(), &Td::send_update, get_update_ton_wallet_state());
}

void TonWalletManager::get_wallet_state(Promise<Unit> &&promise) {
  if (is_wallet_state_inited_) {
    promise.set_value(Unit());
    if (Time::now() < next_wallet_state_reload_at_) {
      return;
    }
  }
  get_wallet_state_queries_.push_back(std::move(promise));
  if (get_wallet_state_queries_.size() == 1u) {
    auto query_promise = PromiseCreator::lambda([actor_id = actor_id(this)](Result<Unit> result) {
      send_closure(actor_id, &TonWalletManager::on_get_wallet_state, std::move(result));
    });
    td_->create_handler<GetWalletStateQuery>(std::move(query_promise))->send();
  }
}

void TonWalletManager::get_user_addresses(vector<UserId> user_ids,
                                          Promise<td_api::object_ptr<td_api::userTonWalletAddresses>> &&promise) {
  vector<telegram_api::object_ptr<telegram_api::InputUser>> input_users;
  for (auto user_id : user_ids) {
    TRY_RESULT_PROMISE(promise, input_user, td_->user_manager_->get_input_user(user_id));
    input_users.push_back(std::move(input_user));
  }
  td_->create_handler<GetUserWalletAddressesQuery>(std::move(promise))->send(std::move(input_users));
}

void TonWalletManager::create_user_ton_wallet(UserId user_id, Promise<string> &&promise) {
  TRY_RESULT_PROMISE(promise, input_user, td_->user_manager_->get_input_user(user_id));
  td_->create_handler<CreateUserWalletAddressQuery>(std::move(promise))->send(std::move(input_user));
}

void TonWalletManager::get_ton_wallet_gasless_info(
    Promise<td_api::object_ptr<td_api::tonWalletGaslessTransfersInfo>> &&promise) {
  td_->create_handler<GetWalletGaslessInfoQuery>(std::move(promise))->send();
}

void TonWalletManager::send_ton_wallet_transfer(
    const string &data_normal, const string &data_gasless,
    Promise<td_api::object_ptr<td_api::tonWalletTransferResult>> &&promise) {
  td_->create_handler<SendWalletTransferQuery>(std::move(promise))->send(data_normal, data_gasless);
}

void TonWalletManager::on_get_wallet_state(Result<Unit> &&result) {
  auto promises = std::move(get_wallet_state_queries_);
  CHECK(!promises.empty());
  get_wallet_state_queries_.clear();
  if (result.is_ok()) {
    set_promises(promises);
  } else {
    fail_promises(promises, result.move_as_error());
  }
}

void TonWalletManager::get_ton_wallet_transactions(
    const string &offset, int32 limit, td_api::object_ptr<td_api::TransactionDirection> &&direction,
    Promise<td_api::object_ptr<td_api::tonWalletTransactions>> &&promise) {
  td_->create_handler<GetTonWalletTransactionsQuery>(std::move(promise))->send(offset, limit, std::move(direction));
}

void TonWalletManager::get_ton_wallet_transaction(const string &transaction_id,
                                                  Promise<td_api::object_ptr<td_api::tonWalletTransaction>> &&promise) {
  td_->create_handler<GetTonWalletTransactionQuery>(std::move(promise))->send(false, transaction_id);
}

void TonWalletManager::get_ton_wallet_transaction_by_msg_hash(
    const string &msg_hash, Promise<td_api::object_ptr<td_api::tonWalletTransaction>> &&promise) {
  td_->create_handler<GetTonWalletTransactionQuery>(std::move(promise))->send(true, msg_hash);
}

td_api::object_ptr<td_api::currencyExchangeRates> TonWalletManager::get_currency_exchange_rates_object() const {
  return td_api::make_object<td_api::currencyExchangeRates>(
      transform(currency_rates_.rates_, [](const CurrencyRate &rate) {
        return td_api::make_object<td_api::currencyExchangeRate>(rate.currency_, rate.rate_);
      }));
}

void TonWalletManager::get_currency_rates(Promise<td_api::object_ptr<td_api::currencyExchangeRates>> &&promise) {
  if (Time::now() < currency_rates_.expires_at_) {
    return promise.set_value(get_currency_exchange_rates_object());
  }
  get_currency_rates_queries_.push_back(std::move(promise));
  if (get_currency_rates_queries_.size() == 1u) {
    auto query_promise = PromiseCreator::lambda(
        [actor_id = actor_id(this)](Result<telegram_api::object_ptr<telegram_api::payments_currencyRates>> r_rates) {
          send_closure(actor_id, &TonWalletManager::on_get_currency_rates, std::move(r_rates));
        });
    td_->create_handler<GetCurrencyRatesQuery>(std::move(query_promise))->send();
  }
}

void TonWalletManager::on_get_currency_rates(
    Result<telegram_api::object_ptr<telegram_api::payments_currencyRates>> r_rates) {
  G()->ignore_result_if_closing(r_rates);
  auto promises = std::move(get_currency_rates_queries_);
  CHECK(!promises.empty());
  get_currency_rates_queries_.clear();

  if (r_rates.is_error()) {
    return fail_promises(promises, r_rates.move_as_error());
  }
  auto rates = r_rates.move_as_ok();
  currency_rates_.rates_.clear();
  for (auto &rate : rates->rates_) {
    CurrencyRate currency_rate;
    currency_rate.currency_ = std::move(rate->currency_);
    currency_rate.rate_ = rate->rate_;
    currency_rates_.rates_.push_back(std::move(currency_rate));
  }
  currency_rates_.expires_at_ = Time::now() + 60;
  for (auto &promise : promises) {
    promise.set_value(get_currency_exchange_rates_object());
  }
}

void TonWalletManager::get_on_ramp_providers(const string &cryptocurrency,
                                             Promise<td_api::object_ptr<td_api::onRampProviders>> &&promise) {
  auto query_promise = PromiseCreator::lambda(
      [actor_id = actor_id(this), promise = std::move(promise)](
          Result<vector<telegram_api::object_ptr<telegram_api::onrampProviderInfo>>> r_providers) mutable {
        send_closure(actor_id, &TonWalletManager::on_get_on_ramp_providers, std::move(r_providers), std::move(promise));
      });
  td_->create_handler<GetOnRampProvidersQuery>(std::move(query_promise))->send(cryptocurrency);
}

void TonWalletManager::on_get_on_ramp_providers(
    Result<vector<telegram_api::object_ptr<telegram_api::onrampProviderInfo>>> r_providers,
    Promise<td_api::object_ptr<td_api::onRampProviders>> &&promise) {
  G()->ignore_result_if_closing(r_providers);
  if (r_providers.is_error()) {
    return promise.set_error(r_providers.move_as_error());
  }
  auto providers = r_providers.move_as_ok();
  vector<td_api::object_ptr<td_api::onRampProvider>> on_ramp_providers;
  for (auto &provider : providers) {
    auto on_ramp_provider = OnRampProvider(std::move(provider));
    on_ramp_providers.push_back(on_ramp_provider.get_on_ramp_provider_object());
  }
  promise.set_value(td_api::make_object<td_api::onRampProviders>(std::move(on_ramp_providers)));
}

void TonWalletManager::get_on_ramp_base_currencies(const string &provider, const string &cryptocurrency,
                                                   Promise<td_api::object_ptr<td_api::currencies>> &&promise) {
  td_->create_handler<GetOnRampBaseCurrenciesQuery>(std::move(promise))->send(provider, cryptocurrency);
}

void TonWalletManager::get_on_ramp_availability(
    const string &provider, const string &cryptocurrency, const string &base_currency,
    Promise<td_api::object_ptr<td_api::onRampPaymentAvailability>> &&promise) {
  td_->create_handler<GetOnRampAvailabilityQuery>(std::move(promise))->send(provider, cryptocurrency, base_currency);
}

void TonWalletManager::get_on_ramp_limits(const string &provider, const string &cryptocurrency,
                                          const string &base_currency, const string &payment_method,
                                          Promise<td_api::object_ptr<td_api::onRampPaymentLimits>> &&promise) {
  td_->create_handler<GetOnRampLimitsQuery>(std::move(promise))
      ->send(provider, cryptocurrency, base_currency, payment_method);
}

void TonWalletManager::get_on_ramp_quote(const string &provider, const string &cryptocurrency,
                                         const string &base_currency, const string &cryptocurrency_amount,
                                         const string &base_currency_amount, const string &payment_method,
                                         Promise<td_api::object_ptr<td_api::onRampPaymentQuote>> &&promise) {
  td_->create_handler<GetOnRampQuoteQuery>(std::move(promise))
      ->send(provider, cryptocurrency, base_currency, cryptocurrency_amount, base_currency_amount, payment_method);
}

void TonWalletManager::create_on_ramp_session(const string &provider, const string &cryptocurrency,
                                              const string &address, const string &payment_method,
                                              const string &base_currency, const string &cryptocurrency_amount,
                                              const string &base_currency_amount, const string &memo,
                                              td_api::object_ptr<td_api::themeParameters> theme,
                                              const string &success_return_url, const string &fail_return_url,
                                              Promise<td_api::object_ptr<td_api::onRampPaymentSession>> &&promise) {
  td_->create_handler<CreateOnRampSessionQuery>(std::move(promise))
      ->send(provider, cryptocurrency, address, payment_method, base_currency, cryptocurrency_amount,
             base_currency_amount, memo, ThemeManager::get_theme_parameters_json_string(theme), success_return_url,
             fail_return_url);
}

void TonWalletManager::perform_ton_center_api_request(const string &endpoint,
                                                      td_api::object_ptr<td_api::TonCenterApiRequestType> &&type,
                                                      Promise<string> &&promise) {
  if (type == nullptr) {
    return promise.set_error(400, "Item type must be non-empty");
  }
  switch (type->get_id()) {
    case td_api::tonCenterApiRequestTypeGet::ID: {
      auto get = td_api::move_object_as<td_api::tonCenterApiRequestTypeGet>(type);
      if (!clean_input_string(get->query_)) {
        return promise.set_error(400, "Query must be encoded in UTF-8");
      }
      td_->create_handler<PerformTonCenterApiRequestQuery>(std::move(promise))
          ->send(endpoint, false, get->query_, string());
      break;
    }
    case td_api::tonCenterApiRequestTypePost::ID: {
      auto post = td_api::move_object_as<td_api::tonCenterApiRequestTypePost>(type);
      if (!clean_input_string(post->payload_)) {
        return promise.set_error(400, "Phone number must be encoded in UTF-8");
      }
      td_->create_handler<PerformTonCenterApiRequestQuery>(std::move(promise))
          ->send(endpoint, true, string(), post->payload_);
      break;
    }
    default:
      UNREACHABLE();
  }
}

void TonWalletManager::get_ton_center_streaming_api_url(
    Promise<td_api::object_ptr<td_api::tonCenterStreamingApiUrl>> &&promise) {
  auto cache_expires_in = streaming_api_url_.expiration_date_ - G()->unix_time();
  if (cache_expires_in >= 60) {
    return promise.set_value(
        td_api::make_object<td_api::tonCenterStreamingApiUrl>(streaming_api_url_.url_, cache_expires_in));
  }
  get_streaming_api_url_queries_.push_back(std::move(promise));
  if (get_streaming_api_url_queries_.size() == 1u) {
    auto query_promise = PromiseCreator::lambda(
        [actor_id = actor_id(this)](Result<telegram_api::object_ptr<telegram_api::toncenter_streamingUrl>> r_url) {
          send_closure(actor_id, &TonWalletManager::on_get_ton_center_streaming_api_url, std::move(r_url));
        });
    td_->create_handler<GetTonCenterStreamingApiUrlQuery>(std::move(query_promise))->send();
  }
}

void TonWalletManager::on_get_ton_center_streaming_api_url(
    Result<telegram_api::object_ptr<telegram_api::toncenter_streamingUrl>> r_url) {
  G()->ignore_result_if_closing(r_url);
  auto promises = std::move(get_streaming_api_url_queries_);
  CHECK(!promises.empty());
  get_streaming_api_url_queries_.clear();

  if (r_url.is_error()) {
    return fail_promises(promises, r_url.move_as_error());
  }
  auto url = r_url.move_as_ok();

  auto expires_in = url->expires_ - G()->unix_time();
  if (expires_in <= 0 || url->url_.empty()) {
    LOG(ERROR) << "Receive " << to_string(url);
    fail_promises(promises, Status::Error(500, "Receive invalid response"));
    return;
  }
  streaming_api_url_.url_ = url->url_;
  streaming_api_url_.expiration_date_ = url->expires_;
  for (auto &promise : promises) {
    promise.set_value(td_api::make_object<td_api::tonCenterStreamingApiUrl>(url->url_, expires_in));
  }
}

void TonWalletManager::get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const {
  if (!td_->auth_manager_->is_authorized()) {
    return;
  }

  if (is_wallet_state_inited_) {
    updates.push_back(get_update_ton_wallet_state());
  }
}

}  // namespace td
