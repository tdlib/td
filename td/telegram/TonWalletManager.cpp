//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/TonWalletManager.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/ChatManager.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/Document.h"
#include "td/telegram/DocumentsManager.h"
#include "td/telegram/files/FileType.h"
#include "td/telegram/Global.h"
#include "td/telegram/JsonValue.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/misc.h"
#include "td/telegram/OptionManager.h"
#include "td/telegram/PasswordManager.h"
#include "td/telegram/PhotoFormat.h"
#include "td/telegram/ServerMessageId.h"
#include "td/telegram/StickersManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UpdatesManager.h"
#include "td/telegram/UserManager.h"

#include "td/e2e/e2e_api.h"

#include "td/utils/algorithm.h"
#include "td/utils/base64.h"
#include "td/utils/buffer.h"
#include "td/utils/crypto.h"
#include "td/utils/Ed25519.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Random.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/StringBuilder.h"
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

class GetExistingWalletBalanceQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::walletBotBalance>> promise_;

 public:
  explicit GetExistingWalletBalanceQuery(Promise<td_api::object_ptr<td_api::walletBotBalance>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create(telegram_api::wallet_getExistingWaltBalance()));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::wallet_getExistingWaltBalance>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetExistingWalletBalanceQuery: " << to_string(result);
    promise_.set_value(td_api::make_object<td_api::walletBotBalance>(result->has_balance_, result->url_));
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
      if (!user_id.is_valid()) {
        LOG(ERROR) << "Receive invalid " << user_id;
        continue;
      }
      addresses.push_back(td_api::make_object<td_api::userTonWalletAddress>(
          td_->user_manager_->get_user_id_object(user_id, "userTonWalletAddress"), address->address_,
          address->public_key_.as_slice().str()));
    }
    promise_.set_value(td_api::make_object<td_api::userTonWalletAddresses>(std::move(addresses)));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class CreateUserWalletAddressQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::userTonWalletAddress>> promise_;
  UserId user_id_;

 public:
  explicit CreateUserWalletAddressQuery(Promise<td_api::object_ptr<td_api::userTonWalletAddress>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(UserId user_id, telegram_api::object_ptr<telegram_api::InputUser> &&input_user) {
    user_id_ = user_id;
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

    td_->user_manager_->on_get_users(std::move(result->users_), "CreateUserWalletAddressQuery");

    if (result->addresses_.size() != 1u) {
      return on_error(Status::Error(400, "Failed to create TON wallet address"));
    }

    auto address = std::move(result->addresses_[0]);
    promise_.set_value(td_api::make_object<td_api::userTonWalletAddress>(
        td_->user_manager_->get_user_id_object(user_id_, "userTonWalletAddress"), address->address_,
        address->public_key_.as_slice().str()));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetAddressWalletQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::userTonWalletAddress>> promise_;

 public:
  explicit GetAddressWalletQuery(Promise<td_api::object_ptr<td_api::userTonWalletAddress>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(const string &address) {
    send_query(G()->net_query_creator().create(telegram_api::wallet_getUserAddresses(
        0, false, vector<telegram_api::object_ptr<telegram_api::InputUser>>(), vector<string>{address})));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::wallet_getUserAddresses>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetAddressWalletQuery: " << to_string(result);

    td_->user_manager_->on_get_users(std::move(result->users_), "GetAddressWalletQuery");

    if (result->addresses_.size() != 1u) {
      return on_error(Status::Error(400, "Address not found"));
    }

    auto address = std::move(result->addresses_[0]);
    auto user_id = UserId(address->user_id_);
    if (!user_id.is_valid()) {
      user_id = UserId();
    }
    promise_.set_value(td_api::make_object<td_api::userTonWalletAddress>(
        td_->user_manager_->get_user_id_object(user_id, "userTonWalletAddress"), address->address_,
        address->public_key_.as_slice().str()));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class ExportWalletSecretPhraseQuery final : public Td::ResultHandler {
  Promise<telegram_api::object_ptr<telegram_api::wallet_secretPhraseParts>> promise_;

 public:
  explicit ExportWalletSecretPhraseQuery(
      Promise<telegram_api::object_ptr<telegram_api::wallet_secretPhraseParts>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(telegram_api::object_ptr<telegram_api::InputCheckPasswordSRP> &&input_password) {
    int32 flags = 0;
    if (input_password != nullptr) {
      flags |= telegram_api::wallet_exportSecretPhrase::PASSWORD_MASK;
    }
    send_query(
        G()->net_query_creator().create(telegram_api::wallet_exportSecretPhrase(flags, std::move(input_password))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::wallet_exportSecretPhrase>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for ExportWalletSecretPhraseQuery: " << to_string(result);
    promise_.set_value(std::move(result));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class FetchWalletEncryptedSecretPhrasePartQuery final : public Td::ResultHandler {
  Promise<telegram_api::object_ptr<telegram_api::wallet_encryptedSecretPhrasePart>> promise_;

 public:
  explicit FetchWalletEncryptedSecretPhrasePartQuery(
      Promise<telegram_api::object_ptr<telegram_api::wallet_encryptedSecretPhrasePart>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(const string &token, const string &public_key, DcId dc_id) {
    send_query(G()->net_query_creator().create(
        telegram_api::wallet_fetchEncryptedSecretPhrasePart(token, BufferSlice(public_key)), {}, dc_id));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::wallet_fetchEncryptedSecretPhrasePart>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for FetchWalletEncryptedSecretPhrasePartQuery: " << to_string(result);
    promise_.set_value(std::move(result));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class EnableWalletBackupQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit EnableWalletBackupQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(vector<BufferSlice> parts, BufferSlice public_key,
            telegram_api::object_ptr<telegram_api::walletOwnershipProof> proof) {
    send_query(G()->net_query_creator().create(telegram_api::wallet_enableBackup(
        telegram_api::wallet_enableBackup::NEW_PUBLIC_KEY_MASK | telegram_api::wallet_enableBackup::PROOF_MASK,
        std::move(parts), std::move(public_key), std::move(proof))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::wallet_enableBackup>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    td_->ton_wallet_manager_->on_update_wallet_state(std::move(result));
    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class DisableWalletBackupQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit DisableWalletBackupQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(telegram_api::object_ptr<telegram_api::InputCheckPasswordSRP> &&input_password) {
    int32 flags = 0;
    if (input_password != nullptr) {
      flags |= telegram_api::wallet_disableBackup::PASSWORD_MASK;
    }
    send_query(G()->net_query_creator().create(
        telegram_api::wallet_disableBackup(flags, std::move(input_password), BufferSlice(), nullptr)));
  }

  void send(BufferSlice public_key, telegram_api::object_ptr<telegram_api::walletOwnershipProof> &&proof) {
    int32 flags =
        telegram_api::wallet_disableBackup::NEW_PUBLIC_KEY_MASK | telegram_api::wallet_disableBackup::PROOF_MASK;
    send_query(G()->net_query_creator().create(
        telegram_api::wallet_disableBackup(flags, nullptr, std::move(public_key), std::move(proof))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::wallet_disableBackup>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    td_->ton_wallet_manager_->on_update_wallet_state(std::move(result));
    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetWalletGaslessInfoQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit GetWalletGaslessInfoQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
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
    td_->updates_manager_->on_get_updates(std::move(result), std::move(promise_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetWalletProofChallengeQuery final : public Td::ResultHandler {
  Promise<telegram_api::object_ptr<telegram_api::wallet_proofChallenge>> promise_;

 public:
  explicit GetWalletProofChallengeQuery(
      Promise<telegram_api::object_ptr<telegram_api::wallet_proofChallenge>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create(telegram_api::wallet_getProofChallenge()));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::wallet_getProofChallenge>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetWalletProofChallengeQuery: " << to_string(result);
    promise_.set_value(std::move(result));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class ReplaceWalletQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit ReplaceWalletQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(telegram_api::object_ptr<telegram_api::InputWalletReplacement> &&new_wallet,
            telegram_api::object_ptr<telegram_api::InputCheckPasswordSRP> &&input_password) {
    int32 flags = 0;
    if (input_password != nullptr) {
      flags |= telegram_api::wallet_replaceWallet::PASSWORD_MASK;
    }
    send_query(G()->net_query_creator().create(
        telegram_api::wallet_replaceWallet(flags, std::move(new_wallet), std::move(input_password))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::wallet_replaceWallet>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    td_->ton_wallet_manager_->on_update_wallet_state(std::move(result));
    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

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
      transactions.push_back(TonWalletManager::get_ton_wallet_transaction_object(td_, std::move(transaction)));
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

    promise_.set_value(TonWalletManager::get_ton_wallet_transaction_object(td_, std::move(result->transactions_[0])));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class SendWalletTransferQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::tonWalletTransferResult>> promise_;
  int64 random_id_;

 public:
  explicit SendWalletTransferQuery(Promise<td_api::object_ptr<td_api::tonWalletTransferResult>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(const string &data_normal, const string &data_gasless, UserId user_id, int64 random_id) {
    random_id_ = random_id;
    int32 flags = 0;
    if (!data_gasless.empty()) {
      flags |= telegram_api::wallet_sendTransfer::DATA_GASLESS_MASK;
    }
    auto r_input_user = td_->user_manager_->get_input_user(user_id);
    send_query(G()->net_query_creator().create(telegram_api::wallet_sendTransfer(
        flags, BufferSlice(data_normal), BufferSlice(data_gasless),
        r_input_user.is_ok() ? r_input_user.move_as_ok() : telegram_api::make_object<telegram_api::inputUserEmpty>(),
        random_id)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::wallet_sendTransfer>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for SendWalletTransferQuery: " << to_string(result);
    auto sent_wallet_transaction = UpdatesManager::extract_sent_wallet_transaction(result.get());
    if (sent_wallet_transaction == nullptr) {
      LOG(ERROR) << "Receive " << to_string(result);
      return on_error(Status::Error(500, "Receive no sent wallet transaction"));
    }
    auto transfer_result = td_api::make_object<td_api::tonWalletTransferResult>(
        sent_wallet_transaction->gasless_, sent_wallet_transaction->msg_hash_,
        TonWalletManager::get_ton_wallet_transaction_object(td_, std::move(sent_wallet_transaction->transaction_)));
    td_->updates_manager_->on_get_updates(
        std::move(result),
        PromiseCreator::lambda([transfer_result = std::move(transfer_result), promise = std::move(promise_)](
                                   Result<Unit>) mutable { return promise.set_value(std::move(transfer_result)); }));
  }

  void on_error(Status status) final {
    if (status.message() == "WALLET_KEY_MISMATCH") {
      td_->ton_wallet_manager_->reload_wallet_state(Promise<Unit>());
    }
    td_->messages_manager_->on_send_message_fail(random_id_, status.clone());
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

class TonWalletManager::GetTonNftsQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::tonNfts>> promise_;

 public:
  explicit GetTonNftsQuery(Promise<td_api::object_ptr<td_api::tonNfts>> &&promise) : promise_(std::move(promise)) {
  }

  void send(const string &offset, int32 limit) {
    send_query(G()->net_query_creator().create(telegram_api::wallet_getNfts(offset, limit)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::wallet_getNfts>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetTonNftsQuery: " << to_string(result);
    auto nfts = transform(std::move(result->items_),
                          [td = td_](auto &&item) { return Nft(td, std::move(item)).get_ton_nft_object(td); });
    promise_.set_value(td_api::make_object<td_api::tonNfts>(std::move(nfts), result->next_offset_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class TonWalletManager::GetTonConnectSessionsQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::tonConnectSessions>> promise_;

 public:
  explicit GetTonConnectSessionsQuery(Promise<td_api::object_ptr<td_api::tonConnectSessions>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create(telegram_api::wallet_tonConnectGetSessions()));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::wallet_tonConnectGetSessions>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetTonConnectSessionsQuery: " << to_string(result);
    auto sessions = transform(std::move(result->sessions_), [td = td_](auto &&session) {
      return TonConnectSession(td, std::move(session)).get_ton_connect_session_object(td);
    });
    promise_.set_value(td_api::make_object<td_api::tonConnectSessions>(std::move(sessions)));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class TonWalletManager::CreateTonConnectSessionQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::tonConnectSession>> promise_;

 public:
  explicit CreateTonConnectSessionQuery(Promise<td_api::object_ptr<td_api::tonConnectSession>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(const string &dapp_client_id, const string &manifest_url) {
    send_query(
        G()->net_query_creator().create(telegram_api::wallet_tonConnectCreateSession(dapp_client_id, manifest_url)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::wallet_tonConnectCreateSession>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for CreateTonConnectSessionQuery: " << to_string(result);
    auto session = TonConnectSession(td_, std::move(result));
    send_closure(
        G()->td(), &Td::send_update,
        td_api::make_object<td_api::updateTonWalletTonConnectSession>(session.get_ton_connect_session_object(td_)));
    promise_.set_value(session.get_ton_connect_session_object(td_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class TonWalletManager::RegisterTonConnectKeyQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::tonConnectChallenge>> promise_;

 public:
  explicit RegisterTonConnectKeyQuery(Promise<td_api::object_ptr<td_api::tonConnectChallenge>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(int64 session_id, const string &client_id) {
    send_query(G()->net_query_creator().create(telegram_api::wallet_tonConnectRegisterKey(session_id, client_id)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::wallet_tonConnectRegisterKey>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for RegisterTonConnectKeyQuery: " << to_string(result);
    auto challenge = TonConnectChallenge(std::move(result));
    promise_.set_value(challenge.get_ton_connect_challenge_object());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class SubmitTonConnectResultQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit SubmitTonConnectResultQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(int64 session_id, const string &challenge_answer, bool is_error, const string &body,
            const string &trace_id) {
    send_query(G()->net_query_creator().create(telegram_api::wallet_tonConnectSubmitConnectResult(
        0, is_error, session_id, BufferSlice(challenge_answer), BufferSlice(body), trace_id)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::wallet_tonConnectSubmitConnectResult>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class TonWalletManager::GetTonConnectPendingQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::tonConnectRequests>> promise_;

 public:
  explicit GetTonConnectPendingQuery(Promise<td_api::object_ptr<td_api::tonConnectRequests>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(bool by_dapp, int64 session_id, const string &dapp_client_id) {
    int32 flags = 0;
    if (by_dapp) {
      flags |= telegram_api::wallet_tonConnectGetPending::DAPP_CLIENT_ID_MASK;
    } else {
      flags |= telegram_api::wallet_tonConnectGetPending::SESSION_ID_MASK;
    }
    send_query(
        G()->net_query_creator().create(telegram_api::wallet_tonConnectGetPending(flags, dapp_client_id, session_id)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::wallet_tonConnectGetPending>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetTonConnectPendingQuery: " << to_string(result);
    auto requests = TonConnectRequests(td_, std::move(result));
    promise_.set_value(requests.get_ton_connect_requests_object(td_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class ClaimTonConnectRequestQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit ClaimTonConnectRequestQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(int64 session_id, MessageId message_id, const string &dapp_request_id, bool is_rejected) {
    send_query(G()->net_query_creator().create(telegram_api::wallet_tonConnectClaimRequest(
        0, is_rejected, session_id, message_id.get_server_message_id().get(), dapp_request_id, BufferSlice())));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::wallet_tonConnectClaimRequest>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class SubmitTonConnectResponseQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit SubmitTonConnectResponseQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(int64 session_id, MessageId message_id, const string &trace_id, const string &body) {
    int32 flags = 0;
    if (!trace_id.empty()) {
      flags |= telegram_api::wallet_tonConnectSubmitResponse::TRACE_ID_MASK;
    }
    send_query(G()->net_query_creator().create(telegram_api::wallet_tonConnectSubmitResponse(
        flags, session_id, message_id.get_server_message_id().get(), BufferSlice(body), trace_id)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::wallet_tonConnectSubmitResponse>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetTonConnectNextEventIdQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::tonConnectSessionEventId>> promise_;

 public:
  explicit GetTonConnectNextEventIdQuery(Promise<td_api::object_ptr<td_api::tonConnectSessionEventId>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(int64 session_id) {
    send_query(G()->net_query_creator().create(telegram_api::wallet_tonConnectNextEventId(session_id)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::wallet_tonConnectNextEventId>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    promise_.set_value(td_api::make_object<td_api::tonConnectSessionEventId>(result->event_id_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class CloseTonConnectSessionQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit CloseTonConnectSessionQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(int64 session_id, const string &body) {
    int32 flags = 0;
    if (!body.empty()) {
      flags |= telegram_api::wallet_tonConnectCloseSession::BODY_MASK;
    }
    send_query(G()->net_query_creator().create(
        telegram_api::wallet_tonConnectCloseSession(flags, session_id, BufferSlice(body))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::wallet_tonConnectCloseSession>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    promise_.set_value(Unit());
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

class GetWalletBackupHolderDcsQuery final : public Td::ResultHandler {
  Promise<vector<telegram_api::object_ptr<telegram_api::wallet_holderDc>>> promise_;

 public:
  explicit GetWalletBackupHolderDcsQuery(
      Promise<vector<telegram_api::object_ptr<telegram_api::wallet_holderDc>>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create(telegram_api::wallet_getBackupHolderDcs()));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::wallet_getBackupHolderDcs>(packet);
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

TonWalletManager::WalletGaslessInfo::WalletGaslessInfo(
    telegram_api::object_ptr<telegram_api::updateWalletGaslessInfo> &&wallet_info)
    : is_available_(wallet_info->available_)
    , left_(wallet_info->left_)
    , reset_date_(wallet_info->reset_at_)
    , min_amount_(wallet_info->min_amount_)
    , relayer_address_(std::move(wallet_info->relayer_address_)) {
}

int32 TonWalletManager::WalletGaslessInfo::try_reset() {
  if (reset_date_ == 0) {
    return 0;
  }
  auto now = G()->unix_time();
  if (reset_date_ > now) {
    return reset_date_ - now + 1;
  }
  reset_date_ = 0;
  left_ = static_cast<int32>(
      min(G()->get_option_integer("ton_wallet_gasless_transfer_daily_count_max", 0), static_cast<int64>(1000000)));
  return 0;
}

td_api::object_ptr<td_api::tonWalletGaslessTransfersInfo>
TonWalletManager::WalletGaslessInfo::get_ton_wallet_gasless_transfers_info_object() const {
  return td_api::make_object<td_api::tonWalletGaslessTransfersInfo>(is_available_ ? left_ : 0, reset_date_,
                                                                    is_available_ ? relayer_address_ : string());
}

bool operator==(const TonWalletManager::WalletGaslessInfo &lhs, const TonWalletManager::WalletGaslessInfo &rhs) {
  return lhs.is_available_ == rhs.is_available_ && lhs.left_ == rhs.left_ && lhs.reset_date_ == rhs.reset_date_ &&
         lhs.min_amount_ == rhs.min_amount_ && lhs.relayer_address_ == rhs.relayer_address_;
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

TonWalletManager::NftAttribute::NftAttribute(telegram_api::object_ptr<telegram_api::wallet_nftAttribute> &&attribute)
    : trait_type_(std::move(attribute->trait_type_)), value_(std::move(attribute->value_)) {
}

td_api::object_ptr<td_api::tonNftAttribute> TonWalletManager::NftAttribute::get_ton_nft_attribute_object() const {
  return td_api::make_object<td_api::tonNftAttribute>(trait_type_, value_);
}

TonWalletManager::Nft::Nft(Td *td, telegram_api::object_ptr<telegram_api::wallet_nftItem> &&item)
    : collection_address_(std::move(item->collection_address_))
    , address_(std::move(item->address_))
    , owner_address_(std::move(item->owner_address_))
    , index_(std::move(item->index_))
    , name_(std::move(item->name_))
    , description_(std::move(item->description_))
    , image_(get_web_document_photo_size(td->file_manager_.get(), FileType::Photo, DialogId(), std::move(item->image_)))
    , image_small_(get_web_document_photo_size(td->file_manager_.get(), FileType::Photo, DialogId(),
                                               std::move(item->image_small_)))
    , attributes_(transform(std::move(item->attributes_),
                            [](auto &&attribute) { return NftAttribute(std::move(attribute)); })) {
  if (item->content_url_ != nullptr) {
    auto parsed_document = td->documents_manager_->on_get_document({std::move(item->content_url_)}, DialogId(), false,
                                                                   false, nullptr, Document::Type::General);
    if (parsed_document.file_id.is_valid() && parsed_document.type == Document::Type::General) {
      content_url_file_id_ = parsed_document.file_id;
    }
  }
  if (item->lottie_ != nullptr) {
    auto parsed_document = td->documents_manager_->on_get_document({std::move(item->lottie_)}, DialogId(), false, false,
                                                                   nullptr, Document::Type::Sticker);
    if (parsed_document.file_id.is_valid() && parsed_document.type == Document::Type::Sticker) {
      lottie_file_id_ = parsed_document.file_id;
    }
  }
  if (item->extra_ != nullptr) {
    extra_ = std::move(item->extra_->data_);
  }
}

td_api::object_ptr<td_api::tonNft> TonWalletManager::Nft::get_ton_nft_object(Td *td) const {
  string extra = extra_;
  auto r_json_value = get_json_value(extra);
  return td_api::make_object<td_api::tonNft>(
      collection_address_, address_, owner_address_, index_, name_, description_,
      image_.file_id.is_valid() ? get_photo_size_object(td->file_manager_.get(), &image_) : nullptr,
      image_small_.file_id.is_valid() ? get_photo_size_object(td->file_manager_.get(), &image_small_) : nullptr,
      td->documents_manager_->get_document_object(content_url_file_id_, PhotoFormat::Jpeg),
      td->stickers_manager_->get_sticker_object(lottie_file_id_),
      transform(attributes_, [](const auto &attribute) { return attribute.get_ton_nft_attribute_object(); }),
      r_json_value.is_ok() ? r_json_value.move_as_ok() : nullptr);
}

TonWalletManager::TonConnectManifest::TonConnectManifest(
    Td *td, telegram_api::object_ptr<telegram_api::tonConnectManifest> &&manifest)
    : url_(std::move(manifest->url_)), name_(std::move(manifest->name_)) {
  if (manifest->icon_ != nullptr) {
    auto parsed_document = td->documents_manager_->on_get_document({std::move(manifest->icon_)}, DialogId(), false,
                                                                   false, nullptr, Document::Type::General);
    if (parsed_document.file_id.is_valid() && parsed_document.type == Document::Type::General) {
      icon_file_id_ = parsed_document.file_id;
    }
  }
}

td_api::object_ptr<td_api::TonConnectManifest> TonWalletManager::TonConnectManifest::get_ton_connect_manifest_object(
    Td *td) const {
  return td_api::make_object<td_api::tonConnectManifestInfo>(
      url_, name_, td->documents_manager_->get_document_object(icon_file_id_, PhotoFormat::Jpeg));
}

TonWalletManager::TonConnectSession::TonConnectSession(
    Td *td, telegram_api::object_ptr<telegram_api::tonConnectSession> &&session)
    : id_(session->id_)
    , dapp_client_id_(std::move(session->dapp_client_id_))
    , client_id_(std::move(session->client_id_))
    , nonce_(session->nonce_.as_slice().str())
    , manifest_error_(session->manifest_error_)
    , date_(session->date_)
    , is_pending_(session->pending_)
    , is_closing_(session->closing_)
    , is_closed_(session->closed_) {
  if (session->manifest_ != nullptr) {
    manifest_ = make_unique<TonConnectManifest>(td, std::move(session->manifest_));
  }
}

td_api::object_ptr<td_api::tonConnectSession> TonWalletManager::TonConnectSession::get_ton_connect_session_object(
    Td *td) const {
  auto state = [&]() -> td_api::object_ptr<td_api::TonConnectSessionState> {
    if (is_pending_) {
      return td_api::make_object<td_api::tonConnectSessionStatePending>();
    }
    if (is_closing_) {
      return td_api::make_object<td_api::tonConnectSessionStateClosing>();
    }
    if (is_closed_) {
      return td_api::make_object<td_api::tonConnectSessionStateClosed>();
    }
    return td_api::make_object<td_api::tonConnectSessionStateReady>();
  }();
  return td_api::make_object<td_api::tonConnectSession>(
      id_, dapp_client_id_, client_id_, date_, get_ton_connect_manifest_object(td, manifest_.get(), manifest_error_),
      nonce_, std::move(state));
}

TonWalletManager::TonConnectRequest::TonConnectRequest(
    telegram_api::object_ptr<telegram_api::tonConnectRequest> &&request)
    : session_id_(request->session_id_)
    , message_id_(ServerMessageId(request->msg_id_))
    , body_(request->body_.as_slice().str())
    , expire_date_(request->expires_)
    , topic_(std::move(request->topic_))
    , trace_id_(std::move(request->trace_id_)) {
}

td_api::object_ptr<td_api::tonConnectRequest> TonWalletManager::TonConnectRequest::get_ton_connect_request_object()
    const {
  return td_api::make_object<td_api::tonConnectRequest>(session_id_, message_id_.get(), body_, expire_date_, topic_,
                                                        trace_id_);
}

TonWalletManager::TonConnectRequests::TonConnectRequests(
    Td *td, telegram_api::object_ptr<telegram_api::wallet_tonConnectPending> &&requests)
    : session_(td, std::move(requests->session_)) {
  for (auto &request : requests->requests_) {
    requests_.emplace_back(std::move(request));
  }
}

td_api::object_ptr<td_api::tonConnectRequests> TonWalletManager::TonConnectRequests::get_ton_connect_requests_object(
    Td *td) const {
  return td_api::make_object<td_api::tonConnectRequests>(
      session_.get_ton_connect_session_object(td),
      transform(requests_, [](const TonConnectRequest &request) { return request.get_ton_connect_request_object(); }));
}

TonWalletManager::TonConnectChallenge::TonConnectChallenge(
    telegram_api::object_ptr<telegram_api::wallet_tonConnectChallenge> &&challenge)
    : challenge_(challenge->challenge_.as_slice().str()), event_id_(challenge->event_id_) {
}

td_api::object_ptr<td_api::tonConnectChallenge>
TonWalletManager::TonConnectChallenge::get_ton_connect_challenge_object() const {
  return td_api::make_object<td_api::tonConnectChallenge>(challenge_, event_id_);
}

Result<TonWalletManager::WalletOwnershipProof> TonWalletManager::WalletOwnershipProof::get_wallet_ownership_proof(
    td_api::object_ptr<td_api::tonWalletOwnershipProof> &&proof) {
  if (proof == nullptr) {
    return Status::Error(400, "Proof must be non-empty");
  }
  if (proof->public_key_.size() != Ed25519::PrivateKey::LENGTH) {
    return Status::Error(400, "Invalid public key specified");
  }
  if (proof->signature_.size() != 64u) {
    return Status::Error(400, "Invalid signature specified");
  }
  WalletOwnershipProof result;
  result.public_key_ = BufferSlice(proof->public_key_);
  result.proof_ =
      telegram_api::make_object<telegram_api::walletOwnershipProof>(proof->timestamp_, BufferSlice(proof->signature_));
  return std::move(result);
}

TonWalletManager::TonWalletManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

void TonWalletManager::timeout_expired() {
  if (is_wallet_gasless_info_inited_) {
    auto old_info = wallet_gasless_info_;
    auto reset_in = wallet_gasless_info_.try_reset();
    if (reset_in > 0) {
      set_timeout_in(reset_in);
    }
    if (!(old_info == wallet_gasless_info_)) {
      send_update_ton_wallet_gasless_transfers_info();
    }
  }
}

void TonWalletManager::tear_down() {
  parent_.reset();
}

Status TonWalletManager::check_ton_address(Slice address) {
  if (address.size() != 48u) {
    return Status::Error(400, "Invalid address length");
  }
  auto r_decoded = base64url_decode(address);
  if (r_decoded.is_error()) {
    r_decoded = base64_decode(address);
  }
  if (r_decoded.is_error()) {
    return Status::Error(400, "Invalid address format");
  }
  auto buffer = r_decoded.move_as_ok();
  CHECK(buffer.size() == 36u);
  auto actual_crc = crc16(Slice(buffer).substr(0, 34));
  auto expected_crc = (static_cast<unsigned char>(buffer[34]) << 8) | static_cast<unsigned char>(buffer[35]);
  if (actual_crc != expected_crc) {
    return Status::Error(400, "Invalid address checksum");
  }
  if ((static_cast<unsigned char>(buffer[0]) & 0x3F) != 0x11) {
    return Status::Error(400, "Invalid address first byte");
  }
  return Status::OK();
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

td_api::object_ptr<td_api::updateTonWalletState> TonWalletManager::get_update_ton_wallet_state_object() const {
  return td_api::make_object<td_api::updateTonWalletState>(wallet_state_.get_ton_wallet_state_object());
}

void TonWalletManager::send_update_ton_wallet_state() const {
  send_closure(G()->td(), &Td::send_update, get_update_ton_wallet_state_object());
}

void TonWalletManager::get_wallet_state(Promise<Unit> &&promise) {
  if (is_wallet_state_inited_) {
    promise.set_value(Unit());
    if (Time::now() < next_wallet_state_reload_at_) {
      return;
    }
  }
  reload_wallet_state(std::move(promise));
}

void TonWalletManager::reload_wallet_state(Promise<Unit> &&promise) {
  get_wallet_state_queries_.push_back(std::move(promise));
  if (get_wallet_state_queries_.size() == 1u) {
    auto query_promise = PromiseCreator::lambda([actor_id = actor_id(this)](Result<Unit> result) {
      send_closure(actor_id, &TonWalletManager::on_get_wallet_state, std::move(result));
    });
    td_->create_handler<GetWalletStateQuery>(std::move(query_promise))->send();
  }
}

void TonWalletManager::on_update_wallet_gasless_info(
    telegram_api::object_ptr<telegram_api::updateWalletGaslessInfo> &&wallet_info) {
  LOG(INFO) << "Receive " << to_string(wallet_info);
  if (td_->auth_manager_->is_bot()) {
    LOG(ERROR) << "Receive updateWalletGaslessInfo";
    return;
  }
  auto info = WalletGaslessInfo(std::move(wallet_info));
  if (is_wallet_gasless_info_inited_ && info == wallet_gasless_info_) {
    return;
  }
  is_wallet_gasless_info_inited_ = true;
  wallet_gasless_info_ = std::move(info);
  auto reset_in = wallet_gasless_info_.try_reset();
  if (reset_in > 0) {
    set_timeout_in(reset_in);
  }
  send_update_ton_wallet_gasless_transfers_info();
}

td_api::object_ptr<td_api::updateTonWalletGaslessTransfersInfo>
TonWalletManager::get_update_ton_wallet_gasless_transfers_info_object() const {
  return td_api::make_object<td_api::updateTonWalletGaslessTransfersInfo>(
      wallet_gasless_info_.get_ton_wallet_gasless_transfers_info_object());
}

void TonWalletManager::send_update_ton_wallet_gasless_transfers_info() const {
  send_closure(G()->td(), &Td::send_update, get_update_ton_wallet_gasless_transfers_info_object());
}

void TonWalletManager::on_update_wallet_ton_connect_session(
    telegram_api::object_ptr<telegram_api::tonConnectSession> &&session) {
  LOG(INFO) << "Receive " << to_string(session);
  if (td_->auth_manager_->is_bot()) {
    LOG(ERROR) << "Receive TonConnectSession";
    return;
  }
  send_closure(G()->td(), &Td::send_update,
               td_api::make_object<td_api::updateTonWalletTonConnectSession>(
                   TonConnectSession(td_, std::move(session)).get_ton_connect_session_object(td_)));
}

void TonWalletManager::get_existing_wallet_balance(Promise<td_api::object_ptr<td_api::walletBotBalance>> &&promise) {
  td_->create_handler<GetExistingWalletBalanceQuery>(std::move(promise))->send();
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

void TonWalletManager::create_user_ton_wallet(UserId user_id,
                                              Promise<td_api::object_ptr<td_api::userTonWalletAddress>> &&promise) {
  TRY_RESULT_PROMISE(promise, input_user, td_->user_manager_->get_input_user(user_id));
  td_->create_handler<CreateUserWalletAddressQuery>(std::move(promise))->send(user_id, std::move(input_user));
}

void TonWalletManager::get_address_ton_wallet(const string &address,
                                              Promise<td_api::object_ptr<td_api::userTonWalletAddress>> &&promise) {
  TRY_STATUS_PROMISE(promise, check_ton_address(address));
  td_->create_handler<GetAddressWalletQuery>(std::move(promise))->send(address);
}

void TonWalletManager::get_ton_wallet_secret_phrase(const string &password, Promise<string> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  if (backup_holder_dcs_.dcs_.empty()) {
    return load_backup_holder_dcs(PromiseCreator::lambda(
        [actor_id = actor_id(this), password, promise = std::move(promise)](Result<Unit> result) mutable {
          if (result.is_error()) {
            return promise.set_error(result.move_as_error());
          }
          send_closure(actor_id, &TonWalletManager::get_ton_wallet_secret_phrase, password, std::move(promise));
        }));
  }
  if (password.empty()) {
    return do_get_ton_wallet_secret_phrase(nullptr, std::move(promise));
  }
  send_closure(G()->password_manager(), &PasswordManager::get_input_check_password_srp, password,
               PromiseCreator::lambda(
                   [actor_id = actor_id(this), promise = std::move(promise)](
                       Result<telegram_api::object_ptr<telegram_api::InputCheckPasswordSRP>> r_input_password) mutable {
                     if (r_input_password.is_error()) {
                       return promise.set_error(r_input_password.move_as_error());
                     }
                     send_closure(actor_id, &TonWalletManager::do_get_ton_wallet_secret_phrase,
                                  r_input_password.move_as_ok(), std::move(promise));
                   }));
}

void TonWalletManager::do_get_ton_wallet_secret_phrase(
    telegram_api::object_ptr<telegram_api::InputCheckPasswordSRP> &&input_password, Promise<string> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  auto query_promise = PromiseCreator::lambda(
      [actor_id = actor_id(this), promise = std::move(promise)](
          Result<telegram_api::object_ptr<telegram_api::wallet_secretPhraseParts>> r_parts) mutable {
        if (r_parts.is_error()) {
          return promise.set_error(r_parts.move_as_error());
        }
        send_closure(actor_id, &TonWalletManager::do_get_ton_wallet_secret_phrase_with_parts, r_parts.move_as_ok(),
                     std::move(promise));
      });
  td_->create_handler<ExportWalletSecretPhraseQuery>(std::move(query_promise))->send(std::move(input_password));
}

void TonWalletManager::do_get_ton_wallet_secret_phrase_with_parts(
    telegram_api::object_ptr<telegram_api::wallet_secretPhraseParts> &&parts, Promise<string> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  CHECK(!backup_holder_dcs_.dcs_.empty());
  auto query_id = ++current_get_secret_phrase_query_id_;
  for (auto dc : backup_holder_dcs_.dcs_) {
    auto r_private_key_id = tde2e_api::key_generate_temporary_private_key();
    if (r_private_key_id.is_error()) {
      return promise.set_error(400, "Failed to generate encryption key");
    }
    auto private_key_id = r_private_key_id.value();
    auto public_key = tde2e_api::key_to_public_key(private_key_id).value();
    auto query_promise = PromiseCreator::lambda(
        [actor_id = actor_id(this), query_id, private_key_id](
            Result<telegram_api::object_ptr<telegram_api::wallet_encryptedSecretPhrasePart>> r_part) mutable {
          send_closure(actor_id, &TonWalletManager::on_get_ton_wallet_secret_phrase_part, std::move(r_part), query_id,
                       private_key_id);
        });
    td_->create_handler<FetchWalletEncryptedSecretPhrasePartQuery>(std::move(query_promise))
        ->send(parts->token_, public_key, dc.dc_id_);
  }
  auto &query = get_secret_phrase_queries_[query_id];
  query.promise_ = std::move(promise);
  query.left_responses_ = backup_holder_dcs_.dcs_.size();
}

Result<BufferSlice> TonWalletManager::encrypt_secret_phrase_part(Slice data, tde2e_api::PublicKeyId dc_public_key_id) {
  CHECK(data.size() == MAX_MNEMONIC_BACKUP_SIZE);
  auto r_private_key_id = tde2e_api::key_generate_temporary_private_key();
  if (r_private_key_id.is_error()) {
    return Status::Error(400, "Failed to generate encryption key");
  }
  auto private_key_id = r_private_key_id.value();
  auto public_key = tde2e_api::key_to_public_key(private_key_id).value();
  auto r_key_id = tde2e_api::key_from_ecdh(private_key_id, dc_public_key_id);
  if (r_key_id.is_error()) {
    return Status::Error(400, "Failed to generate shared key");
  }
  auto r_encrypted_part =
      tde2e_api::encrypt_message_for_one(r_key_id.value(), PSTRING() << "\x08\xdd\x90\x8b\xd7" << data);
  if (r_encrypted_part.is_error()) {
    return Status::Error(400, "Failed to encrypt phrase part");
  }
  return BufferSlice(PSLICE() << public_key << r_encrypted_part.value());
}

Result<string> TonWalletManager::descrypt_secret_phrase_part(Slice data, tde2e_api::PrivateKeyId private_key_id) {
  if (data.size() != 288) {
    return Status::Error(400, "Receive secret phrase of invalid length");
  }
  auto r_public_key_id = tde2e_api::key_from_public_key(data.substr(0, 32).str());
  if (r_public_key_id.is_error()) {
    return Status::Error(400, "Receive invalid secret phrase public key");
  }
  auto r_key_id = tde2e_api::key_from_ecdh(private_key_id, r_public_key_id.value());
  if (r_key_id.is_error()) {
    return Status::Error(400, "Failed to generate shared key");
  }
  auto r_data = tde2e_api::decrypt_message_for_one(r_key_id.value(), data.substr(32).str());
  if (r_data.is_error()) {
    return Status::Error(400, "Failed to decrypt phrase part");
  }
  return r_data.value().substr(5);
}

void TonWalletManager::on_get_ton_wallet_secret_phrase_part(
    Result<telegram_api::object_ptr<telegram_api::wallet_encryptedSecretPhrasePart>> r_part, uint64 query_id,
    tde2e_api::PrivateKeyId private_key_id) {
  auto it = get_secret_phrase_queries_.find(query_id);
  if (it == get_secret_phrase_queries_.end()) {
    return;
  }
  auto &query = it->second;
  string data;
  if (r_part.is_ok()) {
    auto r_data = descrypt_secret_phrase_part(r_part.ok()->data_.as_slice(), private_key_id);
    if (r_data.is_error()) {
      r_part = r_data.move_as_error();
    } else {
      data = r_data.move_as_ok();
    }
  }
  if (r_part.is_error()) {
    auto promise = std::move(query.promise_);
    get_secret_phrase_queries_.erase(it);
    promise.set_error(r_part.move_as_error());
    return;
  }
  if (query.result_.empty()) {
    query.result_.resize(MAX_MNEMONIC_BACKUP_SIZE);
  }
  CHECK(data.size() == MAX_MNEMONIC_BACKUP_SIZE);
  for (size_t i = 0; i < MAX_MNEMONIC_BACKUP_SIZE; i++) {
    query.result_[i] = static_cast<char>(
        static_cast<unsigned char>(static_cast<unsigned char>(query.result_[i]) ^ static_cast<unsigned char>(data[i])));
  }
  CHECK(query.left_responses_ > 0);
  query.left_responses_--;
  if (query.left_responses_ == 0) {
    auto promise = std::move(query.promise_);
    auto result = std::move(query.result_);
    get_secret_phrase_queries_.erase(it);
    while (!result.empty() && result.back() == ' ') {
      result.pop_back();
    }
    for (auto c : result) {
      if (!is_alpha(c) && c != ' ') {
        return promise.set_error(400, "Receive invalid secret phrase");
      }
    }
    promise.set_value(std::move(result));
  }
}

void TonWalletManager::get_ton_wallet_proof_challenge(
    Promise<td_api::object_ptr<td_api::tonWalletOwnershipProofChallenge>> &&promise) {
  auto query_promise = PromiseCreator::lambda(
      [promise = std::move(promise)](
          Result<telegram_api::object_ptr<telegram_api::wallet_proofChallenge>> r_challenge) mutable {
        if (r_challenge.is_error()) {
          return promise.set_error(r_challenge.move_as_error());
        }
        auto challenge = r_challenge.move_as_ok();
        promise.set_value(
            td_api::make_object<td_api::tonWalletOwnershipProofChallenge>(challenge->payload_, challenge->domain_));
      });
  td_->create_handler<GetWalletProofChallengeQuery>(std::move(query_promise))->send();
}

void TonWalletManager::enable_ton_wallet_backup(const string &secret_phrase,
                                                td_api::object_ptr<td_api::tonWalletOwnershipProof> &&ownership_proof,
                                                Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  if (backup_holder_dcs_.dcs_.empty()) {
    return load_backup_holder_dcs(
        PromiseCreator::lambda([actor_id = actor_id(this), secret_phrase, ownership_proof = std::move(ownership_proof),
                                promise = std::move(promise)](Result<Unit> result) mutable {
          if (result.is_error()) {
            return promise.set_error(result.move_as_error());
          }
          send_closure(actor_id, &TonWalletManager::enable_ton_wallet_backup, secret_phrase, std::move(ownership_proof),
                       std::move(promise));
        }));
  }
  TRY_RESULT_PROMISE(promise, proof, WalletOwnershipProof::get_wallet_ownership_proof(std::move(ownership_proof)));
  if (secret_phrase.size() > MAX_MNEMONIC_BACKUP_SIZE) {
    return promise.set_error(400, "Invalid secret phrase specified");
  }
  for (auto c : secret_phrase) {
    if (!is_alpha(c) && c != ' ') {
      return promise.set_error(400, "Receive invalid secret phrase");
    }
  }

  string a(MAX_MNEMONIC_BACKUP_SIZE, '\0');
  string b(MAX_MNEMONIC_BACKUP_SIZE, '\0');
  string c = rpad(secret_phrase, MAX_MNEMONIC_BACKUP_SIZE, ' ');
  Random::secure_bytes(a);
  Random::secure_bytes(b);
  for (size_t i = 0; i < MAX_MNEMONIC_BACKUP_SIZE; i++) {
    c[i] = static_cast<char>(static_cast<unsigned char>(
        static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]) ^ static_cast<unsigned char>(c[i])));
  }
  CHECK(backup_holder_dcs_.dcs_.size() == 3u);
  TRY_RESULT_PROMISE(promise, part_a, encrypt_secret_phrase_part(a, backup_holder_dcs_.dcs_[0].public_key_id_));
  TRY_RESULT_PROMISE(promise, part_b, encrypt_secret_phrase_part(b, backup_holder_dcs_.dcs_[1].public_key_id_));
  TRY_RESULT_PROMISE(promise, part_c, encrypt_secret_phrase_part(c, backup_holder_dcs_.dcs_[2].public_key_id_));
  vector<BufferSlice> parts;
  parts.push_back(std::move(part_a));
  parts.push_back(std::move(part_b));
  parts.push_back(std::move(part_c));
  td_->create_handler<EnableWalletBackupQuery>(std::move(promise))
      ->send(std::move(parts), std::move(proof.public_key_), std::move(proof.proof_));
}

void TonWalletManager::disable_ton_wallet_backup(const string &password, Promise<Unit> &&promise) {
  if (password.empty()) {
    return do_disable_ton_wallet_backup(nullptr, std::move(promise));
  }
  send_closure(G()->password_manager(), &PasswordManager::get_input_check_password_srp, password,
               PromiseCreator::lambda(
                   [actor_id = actor_id(this), promise = std::move(promise)](
                       Result<telegram_api::object_ptr<telegram_api::InputCheckPasswordSRP>> r_input_password) mutable {
                     if (r_input_password.is_error()) {
                       return promise.set_error(r_input_password.move_as_error());
                     }
                     send_closure(actor_id, &TonWalletManager::do_disable_ton_wallet_backup,
                                  r_input_password.move_as_ok(), std::move(promise));
                   }));
}

void TonWalletManager::do_disable_ton_wallet_backup(
    telegram_api::object_ptr<telegram_api::InputCheckPasswordSRP> &&input_password, Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  td_->create_handler<DisableWalletBackupQuery>(std::move(promise))->send(std::move(input_password));
}

void TonWalletManager::disable_ton_wallet_backup_with_proof(
    td_api::object_ptr<td_api::tonWalletOwnershipProof> &&ownership_proof, Promise<Unit> &&promise) {
  TRY_RESULT_PROMISE(promise, proof, WalletOwnershipProof::get_wallet_ownership_proof(std::move(ownership_proof)));
  td_->create_handler<DisableWalletBackupQuery>(std::move(promise))
      ->send(std::move(proof.public_key_), std::move(proof.proof_));
}

void TonWalletManager::get_ton_wallet_gasless_info(Promise<Unit> &&promise) {
  td_->create_handler<GetWalletGaslessInfoQuery>(std::move(promise))->send();
}

void TonWalletManager::send_ton_wallet_transfer(
    const string &data_normal, const string &data_gasless, UserId peer_user_id, const string &peer_address,
    int64 amount, const string &comment, bool is_comment_encrypted, int32 sending_id,
    Promise<td_api::object_ptr<td_api::tonWalletTransferResult>> &&promise) {
  TRY_STATUS_PROMISE(promise, check_ton_address(peer_address));
  if (amount <= 0 || amount < td_->option_manager_->get_option_integer("ton_wallet_transfer_amount_min")) {
    return promise.set_error(400, "Invalid transfer amount specified");
  }
  if (peer_user_id != UserId()) {
    TRY_STATUS_PROMISE(promise, td_->user_manager_->get_input_user(peer_user_id));
  }
  td_->user_manager_->get_me([actor_id = actor_id(this), data_normal, data_gasless, peer_user_id, peer_address, amount,
                              comment, is_comment_encrypted, sending_id, promise = std::move(promise)](Unit) mutable {
    send_closure(actor_id, &TonWalletManager::do_send_ton_wallet_transfer, data_normal, data_gasless, peer_user_id,
                 peer_address, amount, comment, is_comment_encrypted, sending_id, std::move(promise));
  });
}

void TonWalletManager::do_send_ton_wallet_transfer(
    const string &data_normal, const string &data_gasless, UserId peer_user_id, const string &peer_address,
    int64 amount, const string &comment, bool is_comment_encrypted, int32 sending_id,
    Promise<td_api::object_ptr<td_api::tonWalletTransferResult>> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  auto random_id = td_->messages_manager_->send_ton_wallet_transfer(peer_user_id, peer_address, amount, comment,
                                                                    is_comment_encrypted, sending_id);
  td_->create_handler<SendWalletTransferQuery>(std::move(promise))
      ->send(data_normal, data_gasless, peer_user_id, random_id);
}

void TonWalletManager::delete_ton_wallet(const string &password, Promise<Unit> &&promise) {
  if (password.empty()) {
    return do_delete_ton_wallet(nullptr, std::move(promise));
  }
  send_closure(G()->password_manager(), &PasswordManager::get_input_check_password_srp, password,
               PromiseCreator::lambda(
                   [actor_id = actor_id(this), promise = std::move(promise)](
                       Result<telegram_api::object_ptr<telegram_api::InputCheckPasswordSRP>> r_input_password) mutable {
                     if (r_input_password.is_error()) {
                       return promise.set_error(r_input_password.move_as_error());
                     }
                     send_closure(actor_id, &TonWalletManager::do_delete_ton_wallet, r_input_password.move_as_ok(),
                                  std::move(promise));
                   }));
}

void TonWalletManager::do_delete_ton_wallet(
    telegram_api::object_ptr<telegram_api::InputCheckPasswordSRP> &&input_password, Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  td_->create_handler<ReplaceWalletQuery>(std::move(promise))
      ->send(telegram_api::make_object<telegram_api::inputWalletNew>(), std::move(input_password));
}

void TonWalletManager::replace_ton_wallet(const string &password, const string &anchor_public_key,
                                          td_api::object_ptr<td_api::tonWalletOwnershipProof> &&ownership_proof,
                                          Promise<Unit> &&promise) {
  TRY_RESULT_PROMISE(promise, proof, WalletOwnershipProof::get_wallet_ownership_proof(std::move(ownership_proof)));
  if (password.empty()) {
    return do_replace_ton_wallet_with_proof(nullptr, anchor_public_key, std::move(proof), std::move(promise));
  }
  send_closure(
      G()->password_manager(), &PasswordManager::get_input_check_password_srp, password,
      PromiseCreator::lambda(
          [actor_id = actor_id(this), anchor_public_key, proof = std::move(proof), promise = std::move(promise)](
              Result<telegram_api::object_ptr<telegram_api::InputCheckPasswordSRP>> r_input_password) mutable {
            if (r_input_password.is_error()) {
              return promise.set_error(r_input_password.move_as_error());
            }
            send_closure(actor_id, &TonWalletManager::do_replace_ton_wallet_with_proof, r_input_password.move_as_ok(),
                         anchor_public_key, std::move(proof), std::move(promise));
          }));
}

void TonWalletManager::do_replace_ton_wallet_with_proof(
    telegram_api::object_ptr<telegram_api::InputCheckPasswordSRP> &&input_password, const string &anchor_public_key,
    WalletOwnershipProof &&proof, Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  int32 flags = 0;
  if (!anchor_public_key.empty()) {
    flags |= telegram_api::inputWalletImported::ANCHOR_PUBLIC_KEY_MASK;
  }
  auto input_wallet = telegram_api::make_object<telegram_api::inputWalletImported>(
      flags, std::move(proof.public_key_), BufferSlice(anchor_public_key), std::move(proof.proof_));
  td_->create_handler<ReplaceWalletQuery>(std::move(promise))->send(std::move(input_wallet), std::move(input_password));
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
  td_->user_manager_->get_me(
      PromiseCreator::lambda([actor_id = actor_id(this), offset, limit, direction = std::move(direction),
                              promise = std::move(promise)](Unit) mutable {
        send_closure(actor_id, &TonWalletManager::do_get_ton_wallet_transactions, offset, limit, std::move(direction),
                     std::move(promise));
      }));
}

void TonWalletManager::do_get_ton_wallet_transactions(
    const string &offset, int32 limit, td_api::object_ptr<td_api::TransactionDirection> &&direction,
    Promise<td_api::object_ptr<td_api::tonWalletTransactions>> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  td_->create_handler<GetTonWalletTransactionsQuery>(std::move(promise))->send(offset, limit, std::move(direction));
}

void TonWalletManager::get_ton_wallet_transaction(const string &transaction_id,
                                                  Promise<td_api::object_ptr<td_api::tonWalletTransaction>> &&promise) {
  td_->user_manager_->get_me(
      PromiseCreator::lambda([actor_id = actor_id(this), transaction_id, promise = std::move(promise)](Unit) mutable {
        send_closure(actor_id, &TonWalletManager::do_get_ton_wallet_transaction, transaction_id, std::move(promise));
      }));
}

void TonWalletManager::do_get_ton_wallet_transaction(
    const string &transaction_id, Promise<td_api::object_ptr<td_api::tonWalletTransaction>> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  td_->create_handler<GetTonWalletTransactionQuery>(std::move(promise))->send(false, transaction_id);
}

void TonWalletManager::get_ton_wallet_transaction_by_msg_hash(
    const string &msg_hash, Promise<td_api::object_ptr<td_api::tonWalletTransaction>> &&promise) {
  td_->user_manager_->get_me(PromiseCreator::lambda([actor_id = actor_id(this), msg_hash,
                                                     promise = std::move(promise)](Unit) mutable {
    send_closure(actor_id, &TonWalletManager::do_get_ton_wallet_transaction_by_msg_hash, msg_hash, std::move(promise));
  }));
}

void TonWalletManager::do_get_ton_wallet_transaction_by_msg_hash(
    const string &msg_hash, Promise<td_api::object_ptr<td_api::tonWalletTransaction>> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
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

void TonWalletManager::get_nfts(const string &offset, int32 limit,
                                Promise<td_api::object_ptr<td_api::tonNfts>> &&promise) {
  td_->create_handler<GetTonNftsQuery>(std::move(promise))->send(offset, limit);
}

void TonWalletManager::get_ton_connect_sessions(Promise<td_api::object_ptr<td_api::tonConnectSessions>> &&promise) {
  td_->create_handler<GetTonConnectSessionsQuery>(std::move(promise))->send();
}

void TonWalletManager::create_ton_connect_session(const string &dapp_client_id, const string &manifest_url,
                                                  Promise<td_api::object_ptr<td_api::tonConnectSession>> &&promise) {
  td_->create_handler<CreateTonConnectSessionQuery>(std::move(promise))->send(dapp_client_id, manifest_url);
}

void TonWalletManager::register_ton_connect_key(int64 session_id, const string &client_id,
                                                Promise<td_api::object_ptr<td_api::tonConnectChallenge>> &&promise) {
  td_->create_handler<RegisterTonConnectKeyQuery>(std::move(promise))->send(session_id, client_id);
}

void TonWalletManager::submit_ton_connect_result(int64 session_id, const string &challenge_answer, bool is_error,
                                                 const string &body, const string &trace_id, Promise<Unit> &&promise) {
  td_->create_handler<SubmitTonConnectResultQuery>(std::move(promise))
      ->send(session_id, challenge_answer, is_error, body, trace_id);
}

void TonWalletManager::get_ton_connect_next_event_id(
    int64 session_id, Promise<td_api::object_ptr<td_api::tonConnectSessionEventId>> &&promise) {
  td_->create_handler<GetTonConnectNextEventIdQuery>(std::move(promise))->send(session_id);
}

void TonWalletManager::close_ton_connect_session(int64 session_id, const string &body, Promise<Unit> &&promise) {
  td_->create_handler<CloseTonConnectSessionQuery>(std::move(promise))->send(session_id, body);
}

void TonWalletManager::get_ton_connect_requests(bool by_dapp, int64 session_id, const string &dapp_client_id,
                                                Promise<td_api::object_ptr<td_api::tonConnectRequests>> &&promise) {
  td_->create_handler<GetTonConnectPendingQuery>(std::move(promise))->send(by_dapp, session_id, dapp_client_id);
}

void TonWalletManager::claim_ton_connect_request(int64 session_id, MessageId message_id, const string &dapp_request_id,
                                                 bool is_rejected, Promise<Unit> &&promise) {
  if (!message_id.is_server()) {
    return promise.set_error(400, "Invalid message identifier specified");
  }
  td_->create_handler<ClaimTonConnectRequestQuery>(std::move(promise))
      ->send(session_id, message_id, dapp_request_id, is_rejected);
}

void TonWalletManager::submit_ton_connect_response(int64 session_id, MessageId message_id, const string &trace_id,
                                                   const string &body, Promise<Unit> &&promise) {
  if (!message_id.is_server()) {
    return promise.set_error(400, "Invalid message identifier specified");
  }
  td_->create_handler<SubmitTonConnectResponseQuery>(std::move(promise))->send(session_id, message_id, trace_id, body);
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
                                              const string &theme, const string &success_return_url,
                                              const string &fail_return_url,
                                              Promise<td_api::object_ptr<td_api::onRampPaymentSession>> &&promise) {
  td_->create_handler<CreateOnRampSessionQuery>(std::move(promise))
      ->send(provider, cryptocurrency, address, payment_method, base_currency, cryptocurrency_amount,
             base_currency_amount, memo, theme, success_return_url, fail_return_url);
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

void TonWalletManager::load_backup_holder_dcs(Promise<Unit> &&promise) {
  if (!backup_holder_dcs_.dcs_.empty()) {
    return promise.set_value(Unit());
  }
  get_backup_holder_dcs_queries_.push_back(std::move(promise));
  if (get_backup_holder_dcs_queries_.size() == 1u) {
    auto query_promise = PromiseCreator::lambda(
        [actor_id = actor_id(this)](Result<vector<telegram_api::object_ptr<telegram_api::wallet_holderDc>>> r_dcs) {
          send_closure(actor_id, &TonWalletManager::on_get_backup_holder_dcs, std::move(r_dcs));
        });
    td_->create_handler<GetWalletBackupHolderDcsQuery>(std::move(query_promise))->send();
  }
}

void TonWalletManager::on_get_backup_holder_dcs(
    Result<vector<telegram_api::object_ptr<telegram_api::wallet_holderDc>>> r_dcs) {
  auto promises = std::move(get_backup_holder_dcs_queries_);
  CHECK(!promises.empty());
  get_backup_holder_dcs_queries_.clear();

  if (r_dcs.is_error()) {
    return fail_promises(promises, r_dcs.move_as_error());
  }
  auto dcs = r_dcs.move_as_ok();

  vector<BackupHolderDc> holder_dcs;
  for (auto &dc : dcs) {
    LOG(INFO) << "Receive " << to_string(dc);
    if (!DcId::is_valid(dc->dc_)) {
      LOG(ERROR) << "Receive " << to_string(dc);
      return fail_promises(promises, Status::Error(400, "Receive invalid DC identifier"));
    }
    BackupHolderDc holder_dc;
    holder_dc.dc_id_ = DcId::internal(dc->dc_);
    holder_dc.public_key_ = dc->public_key_.as_slice().str();
    auto r_public_key_id = tde2e_api::key_from_public_key(holder_dc.public_key_);
    if (r_public_key_id.is_error()) {
      return fail_promises(promises, Status::Error(400, "Failed to parse public key"));
    }
    holder_dc.public_key_id_ = r_public_key_id.value();
    holder_dcs.push_back(std::move(holder_dc));
  }
  if (holder_dcs.size() != 3u) {
    return fail_promises(promises, Status::Error(400, "Failed to get backup holder DCs"));
  }

  CHECK(backup_holder_dcs_.dcs_.empty());
  backup_holder_dcs_.dcs_ = std::move(holder_dcs);
  set_promises(promises);
}

td_api::object_ptr<td_api::tonWalletTransaction> TonWalletManager::get_ton_wallet_transaction_object(
    Td *td, telegram_api::object_ptr<telegram_api::walletTransaction> &&transaction) {
  if (transaction == nullptr) {
    return nullptr;
  }
  string peer_address;
  UserId peer_user_id;
  string peer_domain;
  string peer_provider_name;
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
    case telegram_api::walletTransactionPeerOnramp::ID: {
      auto peer = telegram_api::move_object_as<telegram_api::walletTransactionPeerOnramp>(transaction->peer_);
      peer_address = std::move(peer->address_);
      peer_domain = std::move(peer->domain_);
      peer_provider_name = std::move(peer->provider_name_);
      break;
    }
    case telegram_api::walletTransactionPeerUnsupported::ID:
      break;
    default:
      UNREACHABLE();
  }
  auto amount = transaction->amount_;
  if (!transaction->incoming_ && amount > 0) {
    amount = -amount;
  }
  auto state = [&]() -> td_api::object_ptr<td_api::TonWalletTransactionState> {
    if (transaction->failed_) {
      return td_api::make_object<td_api::tonWalletTransactionStateFailed>();
    }
    return td_api::make_object<td_api::tonWalletTransactionStateSucceeded>(transaction->tx_hash_);
  }();
  auto type = [&]() -> td_api::object_ptr<td_api::TonWalletTransactionType> {
    if (transaction->nft_ != nullptr) {
      return td_api::make_object<td_api::tonWalletTransactionTypeNftTransfer>(
          Nft(td, std::move(transaction->nft_)).get_ton_nft_object(td), !transaction->incoming_, transaction->comment_,
          transaction->comment_encrypted_);
    }
    if (!peer_provider_name.empty()) {
      return td_api::make_object<td_api::tonWalletTransactionTypeOnRampDeposit>(amount, peer_provider_name);
    }
    if (transaction->key_change_) {
      peer_user_id = td->user_manager_->get_my_id();
      return td_api::make_object<td_api::tonWalletTransactionTypeKeyChange>();
    }
    return td_api::make_object<td_api::tonWalletTransactionTypeTransfer>(
        amount, transaction->gasless_, transaction->comment_, transaction->comment_encrypted_);
  }();
  return td_api::make_object<td_api::tonWalletTransaction>(
      transaction->id_, peer_address, td->user_manager_->get_user_id_object(peer_user_id, "tonWalletTransaction"),
      peer_domain, transaction->date_, transaction->fee_, std::move(state), std::move(type));
}

td_api::object_ptr<td_api::TonConnectManifest> TonWalletManager::get_ton_connect_manifest_object(
    Td *td, const TonConnectManifest *manifest, int32 manifest_error) {
  if (manifest != nullptr) {
    LOG_IF(ERROR, manifest_error != 0) << "Have error " << manifest_error << " with a manifest";
    return manifest->get_ton_connect_manifest_object(td);
  }
  switch (manifest_error) {
    case 0:
      return td_api::make_object<td_api::tonConnectManifestPending>();
    case 3:
      return td_api::make_object<td_api::tonConnectManifestInvalid>();
    default:
      LOG_IF(ERROR, manifest_error != 2) << "Have manifest error " << manifest_error;
      return td_api::make_object<td_api::tonConnectManifestFailed>();
  }
}

void TonWalletManager::get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const {
  if (!td_->auth_manager_->is_authorized()) {
    return;
  }

  if (is_wallet_state_inited_) {
    updates.push_back(get_update_ton_wallet_state_object());
  }

  if (is_wallet_gasless_info_inited_) {
    updates.push_back(get_update_ton_wallet_gasless_transfers_info_object());
  }
}

}  // namespace td
