//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/StarManager.h"

#include "td/telegram/ChatManager.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/Global.h"
#include "td/telegram/InputInvoice.h"
#include "td/telegram/Photo.h"
#include "td/telegram/Td.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UpdatesManager.h"
#include "td/telegram/UserManager.h"

#include "td/utils/buffer.h"
#include "td/utils/logging.h"

namespace td {

class GetStarsTopupOptionsQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::starPaymentOptions>> promise_;

 public:
  explicit GetStarsTopupOptionsQuery(Promise<td_api::object_ptr<td_api::starPaymentOptions>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create(telegram_api::payments_getStarsTopupOptions()));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_getStarsTopupOptions>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto results = result_ptr.move_as_ok();
    vector<td_api::object_ptr<td_api::starPaymentOption>> options;
    for (auto &result : results) {
      options.push_back(td_api::make_object<td_api::starPaymentOption>(
          result->currency_, result->amount_, result->stars_, result->store_product_, result->extended_));
    }

    promise_.set_value(td_api::make_object<td_api::starPaymentOptions>(std::move(options)));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetStarsTransactionsQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::starTransactions>> promise_;

 public:
  explicit GetStarsTransactionsQuery(Promise<td_api::object_ptr<td_api::starTransactions>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(const string &offset, td_api::object_ptr<td_api::StarTransactionDirection> &&direction) {
    int32 flags = 0;
    if (direction != nullptr) {
      switch (direction->get_id()) {
        case td_api::starTransactionDirectionIncoming::ID:
          flags |= telegram_api::payments_getStarsTransactions::INBOUND_MASK;
          break;
        case td_api::starTransactionDirectionOutgoing::ID:
          flags |= telegram_api::payments_getStarsTransactions::OUTBOUND_MASK;
          break;
        default:
          UNREACHABLE();
      }
    }
    send_query(G()->net_query_creator().create(telegram_api::payments_getStarsTransactions(
        flags, false /*ignored*/, false /*ignored*/, false /*ignored*/,
        telegram_api::make_object<telegram_api::inputPeerSelf>(), offset, 100)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_getStarsTransactions>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    td_->user_manager_->on_get_users(std::move(result->users_), "GetStarsTransactionsQuery");
    td_->chat_manager_->on_get_chats(std::move(result->chats_), "GetStarsTransactionsQuery");

    vector<td_api::object_ptr<td_api::starTransaction>> transactions;
    for (auto &transaction : result->history_) {
      td_api::object_ptr<td_api::productInfo> product_info;
      if (!transaction->title_.empty() || !transaction->description_.empty() || transaction->photo_ != nullptr) {
        auto photo = get_web_document_photo(td_->file_manager_.get(), std::move(transaction->photo_), DialogId());
        product_info = get_product_info_object(td_, transaction->title_, transaction->description_, photo);
      }
      auto source = [&]() -> td_api::object_ptr<td_api::StarTransactionSource> {
        switch (transaction->peer_->get_id()) {
          case telegram_api::starsTransactionPeerUnsupported::ID:
            return td_api::make_object<td_api::starTransactionSourceUnsupported>();
          case telegram_api::starsTransactionPeerPremiumBot::ID:
            return td_api::make_object<td_api::starTransactionSourceTelegram>();
          case telegram_api::starsTransactionPeerAppStore::ID:
            return td_api::make_object<td_api::starTransactionSourceAppStore>();
          case telegram_api::starsTransactionPeerPlayMarket::ID:
            return td_api::make_object<td_api::starTransactionSourceGooglePlay>();
          case telegram_api::starsTransactionPeerFragment::ID: {
            auto state = [&]() -> td_api::object_ptr<td_api::RevenueWithdrawalState> {
              if (transaction->transaction_date_ > 0) {
                return td_api::make_object<td_api::revenueWithdrawalStateCompleted>(transaction->transaction_date_,
                                                                                    transaction->transaction_url_);
              }
              if (transaction->pending_) {
                return td_api::make_object<td_api::revenueWithdrawalStatePending>();
              }
              if (transaction->failed_) {
                return td_api::make_object<td_api::revenueWithdrawalStateFailed>();
              }
              if (!transaction->refund_) {
                LOG(ERROR) << "Receive " << to_string(transaction);
              }
              return nullptr;
            }();
            return td_api::make_object<td_api::starTransactionSourceFragment>(std::move(state));
          }
          case telegram_api::starsTransactionPeer::ID: {
            DialogId dialog_id(
                static_cast<const telegram_api::starsTransactionPeer *>(transaction->peer_.get())->peer_);
            if (dialog_id.get_type() == DialogType::User) {
              return td_api::make_object<td_api::starTransactionSourceUser>(
                  td_->user_manager_->get_user_id_object(dialog_id.get_user_id(), "starTransactionSourceUser"),
                  std::move(product_info));
            }
            return td_api::make_object<td_api::starTransactionSourceUnsupported>();
          }
          default:
            UNREACHABLE();
        }
      }();
      transactions.push_back(td_api::make_object<td_api::starTransaction>(
          transaction->id_, transaction->stars_, transaction->refund_, transaction->date_, std::move(source)));
    }

    promise_.set_value(
        td_api::make_object<td_api::starTransactions>(result->balance_, std::move(transactions), result->next_offset_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class RefundStarsChargeQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit RefundStarsChargeQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(telegram_api::object_ptr<telegram_api::InputUser> &&input_user, const string &telegram_payment_charge_id) {
    send_query(G()->net_query_creator().create(
        telegram_api::payments_refundStarsCharge(std::move(input_user), telegram_payment_charge_id)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_refundStarsCharge>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(DEBUG) << "Receive result for RefundStarsChargeQuery: " << to_string(ptr);
    td_->updates_manager_->on_get_updates(std::move(ptr), std::move(promise_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

StarManager::StarManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

void StarManager::tear_down() {
  parent_.reset();
}

void StarManager::get_star_payment_options(Promise<td_api::object_ptr<td_api::starPaymentOptions>> &&promise) {
  td_->create_handler<GetStarsTopupOptionsQuery>(std::move(promise))->send();
}

void StarManager::get_star_transactions(const string &offset,
                                        td_api::object_ptr<td_api::StarTransactionDirection> &&direction,
                                        Promise<td_api::object_ptr<td_api::starTransactions>> &&promise) {
  td_->create_handler<GetStarsTransactionsQuery>(std::move(promise))->send(offset, std::move(direction));
}

void StarManager::refund_star_payment(UserId user_id, const string &telegram_payment_charge_id,
                                      Promise<Unit> &&promise) {
  TRY_RESULT_PROMISE(promise, input_user, td_->user_manager_->get_input_user(user_id));
  td_->create_handler<RefundStarsChargeQuery>(std::move(promise))
      ->send(std::move(input_user), telegram_payment_charge_id);
}

}  // namespace td
