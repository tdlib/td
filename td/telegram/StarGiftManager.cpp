//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/StarGiftManager.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/DialogManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/MessageQuote.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/OnlineManager.h"
#include "td/telegram/StarGift.h"
#include "td/telegram/StarGiftAttribute.h"
#include "td/telegram/StarManager.h"
#include "td/telegram/StateManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UpdatesManager.h"
#include "td/telegram/UserManager.h"
#include "td/telegram/UserStarGift.h"

#include "td/utils/buffer.h"
#include "td/utils/logging.h"
#include "td/utils/Random.h"
#include "td/utils/Status.h"

namespace td {

class GetStarGiftsQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::gifts>> promise_;

 public:
  explicit GetStarGiftsQuery(Promise<td_api::object_ptr<td_api::gifts>> &&promise) : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create(telegram_api::payments_getStarGifts(0)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_getStarGifts>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetStarGiftsQuery: " << to_string(ptr);
    if (ptr->get_id() != telegram_api::payments_starGifts::ID) {
      LOG(ERROR) << "Receive " << to_string(ptr);
      return promise_.set_error(Status::Error(500, "Receive unexpected response"));
    }
    auto results = telegram_api::move_object_as<telegram_api::payments_starGifts>(ptr);
    vector<td_api::object_ptr<td_api::gift>> options;
    for (auto &gift : results->gifts_) {
      StarGift star_gift(td_, std::move(gift), false);
      if (!star_gift.is_valid()) {
        continue;
      }
      td_->star_gift_manager_->on_get_star_gift(star_gift, true);
      options.push_back(star_gift.get_gift_object(td_));
    }

    promise_.set_value(td_api::make_object<td_api::gifts>(std::move(options)));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class SendGiftQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  int64 star_count_;

 public:
  explicit SendGiftQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(telegram_api::object_ptr<telegram_api::inputInvoiceStarGift> input_invoice, int64 payment_form_id,
            int64 star_count) {
    star_count_ = star_count;
    send_query(G()->net_query_creator().create(
        telegram_api::payments_sendStarsForm(payment_form_id, std::move(input_invoice))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_sendStarsForm>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto payment_result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for SendGiftQuery: " << to_string(payment_result);
    switch (payment_result->get_id()) {
      case telegram_api::payments_paymentResult::ID: {
        auto result = telegram_api::move_object_as<telegram_api::payments_paymentResult>(payment_result);
        td_->star_manager_->add_pending_owned_star_count(star_count_, true);
        td_->updates_manager_->on_get_updates(std::move(result->updates_), std::move(promise_));
        break;
      }
      case telegram_api::payments_paymentVerificationNeeded::ID:
        td_->star_manager_->add_pending_owned_star_count(star_count_, false);
        LOG(ERROR) << "Receive " << to_string(payment_result);
        break;
      default:
        UNREACHABLE();
    }
  }

  void on_error(Status status) final {
    td_->star_manager_->add_pending_owned_star_count(star_count_, false);
    promise_.set_error(std::move(status));
  }
};

class GetGiftPaymentFormQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  int64 star_count_;
  telegram_api::object_ptr<telegram_api::inputInvoiceStarGift> send_input_invoice_;

 public:
  explicit GetGiftPaymentFormQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(telegram_api::object_ptr<telegram_api::inputInvoiceStarGift> input_invoice,
            telegram_api::object_ptr<telegram_api::inputInvoiceStarGift> send_input_invoice, int64 star_count) {
    send_input_invoice_ = std::move(send_input_invoice);
    star_count_ = star_count;
    td_->star_manager_->add_pending_owned_star_count(-star_count, false);
    send_query(
        G()->net_query_creator().create(telegram_api::payments_getPaymentForm(0, std::move(input_invoice), nullptr)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_getPaymentForm>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto payment_form_ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetGiftPaymentFormQuery: " << to_string(payment_form_ptr);
    switch (payment_form_ptr->get_id()) {
      case telegram_api::payments_paymentForm::ID:
      case telegram_api::payments_paymentFormStars::ID:
        LOG(ERROR) << "Receive " << to_string(payment_form_ptr);
        td_->star_manager_->add_pending_owned_star_count(star_count_, false);
        promise_.set_error(Status::Error(500, "Unsupported"));
        break;
      case telegram_api::payments_paymentFormStarGift::ID: {
        auto payment_form = static_cast<const telegram_api::payments_paymentFormStarGift *>(payment_form_ptr.get());
        td_->create_handler<SendGiftQuery>(std::move(promise_))
            ->send(std::move(send_input_invoice_), payment_form->form_id_, star_count_);
        break;
      }
      default:
        UNREACHABLE();
    }
  }

  void on_error(Status status) final {
    td_->star_manager_->add_pending_owned_star_count(star_count_, false);
    promise_.set_error(std::move(status));
  }
};

class ConvertStarGiftQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit ConvertStarGiftQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(MessageId message_id) {
    send_query(G()->net_query_creator().create(
        telegram_api::payments_convertStarGift(message_id.get_server_message_id().get())));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_convertStarGift>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    td_->user_manager_->reload_user_full(td_->user_manager_->get_my_id(), std::move(promise_), "ConvertStarGiftQuery");
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class SaveStarGiftQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  bool is_saved_;

 public:
  explicit SaveStarGiftQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(MessageId message_id, bool is_saved) {
    is_saved_ = is_saved;
    int32 flags = 0;
    if (!is_saved) {
      flags |= telegram_api::payments_saveStarGift::UNSAVE_MASK;
    }
    send_query(G()->net_query_creator().create(
        telegram_api::payments_saveStarGift(flags, false /*ignored*/, message_id.get_server_message_id().get())));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_saveStarGift>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    td_->user_manager_->on_update_my_gift_count(is_saved_ ? 1 : -1);
    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetUpgradeGiftPreviewQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::giftUpgradePreview>> promise_;

 public:
  explicit GetUpgradeGiftPreviewQuery(Promise<td_api::object_ptr<td_api::giftUpgradePreview>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(int64 gift_id) {
    send_query(G()->net_query_creator().create(telegram_api::payments_getStarGiftUpgradePreview(gift_id)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_getStarGiftUpgradePreview>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetUpgradeGiftPreviewQuery: " << to_string(ptr);
    auto result = td_api::make_object<td_api::giftUpgradePreview>();
    for (auto &attribute : ptr->sample_attributes_) {
      switch (attribute->get_id()) {
        case telegram_api::starGiftAttributeModel::ID: {
          auto model = StarGiftAttributeSticker(
              td_, telegram_api::move_object_as<telegram_api::starGiftAttributeModel>(attribute));
          if (!model.is_valid()) {
            LOG(ERROR) << "Receive invalid model";
          } else {
            result->models_.push_back(model.get_upgraded_gift_model_object(td_));
          }
          break;
        }
        case telegram_api::starGiftAttributePattern::ID: {
          auto pattern = StarGiftAttributeSticker(
              td_, telegram_api::move_object_as<telegram_api::starGiftAttributePattern>(attribute));
          if (!pattern.is_valid()) {
            LOG(ERROR) << "Receive invalid symbol";
          } else {
            result->symbols_.push_back(pattern.get_upgraded_gift_symbol_object(td_));
          }
          break;
        }
        case telegram_api::starGiftAttributeBackdrop::ID: {
          auto backdrop = StarGiftAttributeBackdrop(
              telegram_api::move_object_as<telegram_api::starGiftAttributeBackdrop>(attribute));
          if (!backdrop.is_valid()) {
            LOG(ERROR) << "Receive invalid backdrop";
          } else {
            result->backdrops_.push_back(backdrop.get_upgraded_gift_backdrop_object());
          }
          break;
        }
        case telegram_api::starGiftAttributeOriginalDetails::ID:
          LOG(ERROR) << "Receive unexpected original details";
          break;
        default:
          UNREACHABLE();
      }
    }
    promise_.set_value(std::move(result));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

static Promise<Unit> get_gift_upgrade_promise(Td *td, const telegram_api::object_ptr<telegram_api::Updates> &updates,
                                              Promise<td_api::object_ptr<td_api::upgradeGiftResult>> &&promise) {
  vector<std::pair<const telegram_api::Message *, bool>> new_messages = UpdatesManager::get_new_messages(updates.get());
  if (new_messages.size() != 1u || new_messages[0].second ||
      new_messages[0].first->get_id() != telegram_api::messageService::ID) {
    promise.set_error(Status::Error(500, "Receive invalid server response"));
    return Auto();
  }
  auto message = static_cast<const telegram_api::messageService *>(new_messages[0].first);
  if (message->action_->get_id() != telegram_api::messageActionStarGiftUnique::ID) {
    promise.set_error(Status::Error(500, "Receive invalid server response"));
    return Auto();
  }
  auto action = static_cast<const telegram_api::messageActionStarGiftUnique *>(message->action_.get());
  if (!action->upgrade_ || action->transferred_ || action->refunded_ ||
      action->gift_->get_id() != telegram_api::starGiftUnique::ID) {
    promise.set_error(Status::Error(500, "Receive invalid server response"));
    return Auto();
  }
  auto message_full_id = MessageFullId::get_message_full_id(new_messages[0].first, false);
  return PromiseCreator::lambda([message_full_id, promise = std::move(promise)](Result<Unit> result) mutable {
    if (result.is_error()) {
      return promise.set_error(result.move_as_error());
    }
    send_closure(G()->messages_manager(), &MessagesManager::finish_gift_upgrade, message_full_id, std::move(promise));
  });
}

class UpgradeStarGiftQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::upgradeGiftResult>> promise_;

 public:
  explicit UpgradeStarGiftQuery(Promise<td_api::object_ptr<td_api::upgradeGiftResult>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(telegram_api::object_ptr<telegram_api::InputUser> input_user, MessageId message_id, int64 star_count,
            bool keep_original_details) {
    int32 flags = 0;
    if (keep_original_details) {
      flags |= telegram_api::payments_upgradeStarGift::KEEP_ORIGINAL_DETAILS_MASK;
    }
    send_query(G()->net_query_creator().create(
        telegram_api::payments_upgradeStarGift(flags, false /*ignored*/, message_id.get_server_message_id().get())));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_upgradeStarGift>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for UpgradeStarGiftQuery: " << to_string(ptr);
    auto promise = get_gift_upgrade_promise(td_, ptr, std::move(promise_));
    td_->updates_manager_->on_get_updates(std::move(ptr), std::move(promise));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class UpgradeGiftQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::upgradeGiftResult>> promise_;
  int64 star_count_;

 public:
  explicit UpgradeGiftQuery(Promise<td_api::object_ptr<td_api::upgradeGiftResult>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(telegram_api::object_ptr<telegram_api::inputInvoiceStarGiftUpgrade> input_invoice, int64 payment_form_id,
            int64 star_count) {
    star_count_ = star_count;
    send_query(G()->net_query_creator().create(
        telegram_api::payments_sendStarsForm(payment_form_id, std::move(input_invoice))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_sendStarsForm>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto payment_result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for UpgradeGiftQuery: " << to_string(payment_result);
    switch (payment_result->get_id()) {
      case telegram_api::payments_paymentResult::ID: {
        auto result = telegram_api::move_object_as<telegram_api::payments_paymentResult>(payment_result);
        td_->star_manager_->add_pending_owned_star_count(star_count_, true);
        auto promise = get_gift_upgrade_promise(td_, result->updates_, std::move(promise_));
        td_->updates_manager_->on_get_updates(std::move(result->updates_), std::move(promise));
        break;
      }
      case telegram_api::payments_paymentVerificationNeeded::ID:
        td_->star_manager_->add_pending_owned_star_count(star_count_, false);
        LOG(ERROR) << "Receive " << to_string(payment_result);
        break;
      default:
        UNREACHABLE();
    }
  }

  void on_error(Status status) final {
    td_->star_manager_->add_pending_owned_star_count(star_count_, false);
    promise_.set_error(std::move(status));
  }
};

class GetGiftUpgradePaymentFormQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::upgradeGiftResult>> promise_;
  int64 star_count_;
  telegram_api::object_ptr<telegram_api::inputInvoiceStarGiftUpgrade> upgrade_input_invoice_;

 public:
  explicit GetGiftUpgradePaymentFormQuery(Promise<td_api::object_ptr<td_api::upgradeGiftResult>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(telegram_api::object_ptr<telegram_api::inputInvoiceStarGiftUpgrade> input_invoice,
            telegram_api::object_ptr<telegram_api::inputInvoiceStarGiftUpgrade> upgrade_input_invoice,
            int64 star_count) {
    upgrade_input_invoice_ = std::move(upgrade_input_invoice);
    star_count_ = star_count;
    td_->star_manager_->add_pending_owned_star_count(-star_count, false);
    send_query(
        G()->net_query_creator().create(telegram_api::payments_getPaymentForm(0, std::move(input_invoice), nullptr)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_getPaymentForm>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto payment_form_ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetGiftUpgradePaymentFormQuery: " << to_string(payment_form_ptr);
    switch (payment_form_ptr->get_id()) {
      case telegram_api::payments_paymentForm::ID:
      case telegram_api::payments_paymentFormStars::ID:
        LOG(ERROR) << "Receive " << to_string(payment_form_ptr);
        td_->star_manager_->add_pending_owned_star_count(star_count_, false);
        promise_.set_error(Status::Error(500, "Unsupported"));
        break;
      case telegram_api::payments_paymentFormStarGift::ID: {
        auto payment_form = static_cast<const telegram_api::payments_paymentFormStarGift *>(payment_form_ptr.get());
        td_->create_handler<UpgradeGiftQuery>(std::move(promise_))
            ->send(std::move(upgrade_input_invoice_), payment_form->form_id_, star_count_);
        break;
      }
      default:
        UNREACHABLE();
    }
  }

  void on_error(Status status) final {
    td_->star_manager_->add_pending_owned_star_count(star_count_, false);
    promise_.set_error(std::move(status));
  }
};

class TransferStarGiftQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit TransferStarGiftQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(MessageId message_id, telegram_api::object_ptr<telegram_api::InputUser> receiver_input_user) {
    send_query(G()->net_query_creator().create(telegram_api::payments_transferStarGift(
        message_id.get_server_message_id().get(), std::move(receiver_input_user))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_transferStarGift>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for TransferStarGiftQuery: " << to_string(ptr);
    td_->updates_manager_->on_get_updates(std::move(ptr), std::move(promise_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class TransferGiftQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  int64 star_count_;

 public:
  explicit TransferGiftQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(telegram_api::object_ptr<telegram_api::inputInvoiceStarGiftTransfer> input_invoice, int64 payment_form_id,
            int64 star_count) {
    star_count_ = star_count;
    send_query(G()->net_query_creator().create(
        telegram_api::payments_sendStarsForm(payment_form_id, std::move(input_invoice))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_sendStarsForm>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto payment_result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for TransferGiftQuery: " << to_string(payment_result);
    switch (payment_result->get_id()) {
      case telegram_api::payments_paymentResult::ID: {
        auto result = telegram_api::move_object_as<telegram_api::payments_paymentResult>(payment_result);
        td_->star_manager_->add_pending_owned_star_count(star_count_, true);
        td_->updates_manager_->on_get_updates(std::move(result->updates_), std::move(promise_));
        break;
      }
      case telegram_api::payments_paymentVerificationNeeded::ID:
        td_->star_manager_->add_pending_owned_star_count(star_count_, false);
        LOG(ERROR) << "Receive " << to_string(payment_result);
        break;
      default:
        UNREACHABLE();
    }
  }

  void on_error(Status status) final {
    td_->star_manager_->add_pending_owned_star_count(star_count_, false);
    promise_.set_error(std::move(status));
  }
};

class GetGiftTransferPaymentFormQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  int64 star_count_;
  telegram_api::object_ptr<telegram_api::inputInvoiceStarGiftTransfer> transfer_input_invoice_;

 public:
  explicit GetGiftTransferPaymentFormQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(telegram_api::object_ptr<telegram_api::inputInvoiceStarGiftTransfer> input_invoice,
            telegram_api::object_ptr<telegram_api::inputInvoiceStarGiftTransfer> transfer_input_invoice,
            int64 star_count) {
    transfer_input_invoice_ = std::move(transfer_input_invoice);
    star_count_ = star_count;
    td_->star_manager_->add_pending_owned_star_count(-star_count, false);
    send_query(
        G()->net_query_creator().create(telegram_api::payments_getPaymentForm(0, std::move(input_invoice), nullptr)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_getPaymentForm>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto payment_form_ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetGiftTransferPaymentFormQuery: " << to_string(payment_form_ptr);
    switch (payment_form_ptr->get_id()) {
      case telegram_api::payments_paymentForm::ID:
      case telegram_api::payments_paymentFormStars::ID:
        LOG(ERROR) << "Receive " << to_string(payment_form_ptr);
        td_->star_manager_->add_pending_owned_star_count(star_count_, false);
        promise_.set_error(Status::Error(500, "Unsupported"));
        break;
      case telegram_api::payments_paymentFormStarGift::ID: {
        auto payment_form = static_cast<const telegram_api::payments_paymentFormStarGift *>(payment_form_ptr.get());
        td_->create_handler<TransferGiftQuery>(std::move(promise_))
            ->send(std::move(transfer_input_invoice_), payment_form->form_id_, star_count_);
        break;
      }
      default:
        UNREACHABLE();
    }
  }

  void on_error(Status status) final {
    td_->star_manager_->add_pending_owned_star_count(star_count_, false);
    promise_.set_error(std::move(status));
  }
};

class GetUserGiftsQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::userGifts>> promise_;
  UserId user_id_;

 public:
  explicit GetUserGiftsQuery(Promise<td_api::object_ptr<td_api::userGifts>> &&promise) : promise_(std::move(promise)) {
  }

  void send(UserId user_id, telegram_api::object_ptr<telegram_api::InputUser> input_user, const string &offset,
            int32 limit) {
    user_id_ = user_id;
    send_query(
        G()->net_query_creator().create(telegram_api::payments_getUserStarGifts(std::move(input_user), offset, limit)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_getUserStarGifts>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetUserGiftsQuery: " << to_string(ptr);
    td_->user_manager_->on_get_users(std::move(ptr->users_), "GetUserGiftsQuery");

    auto total_count = ptr->count_;
    if (total_count < static_cast<int32>(ptr->gifts_.size())) {
      LOG(ERROR) << "Receive " << ptr->gifts_.size() << " gifts with total count = " << total_count;
      total_count = static_cast<int32>(ptr->gifts_.size());
    }
    bool is_me = user_id_ == td_->user_manager_->get_my_id();
    vector<td_api::object_ptr<td_api::userGift>> gifts;
    for (auto &gift : ptr->gifts_) {
      UserStarGift user_gift(td_, std::move(gift), is_me);
      if (!user_gift.is_valid()) {
        LOG(ERROR) << "Receive invalid user gift";
        continue;
      }
      gifts.push_back(user_gift.get_user_gift_object(td_));
    }
    if (!is_me) {
      td_->user_manager_->on_update_user_gift_count(user_id_, total_count);
    }
    promise_.set_value(td_api::make_object<td_api::userGifts>(total_count, std::move(gifts), ptr->next_offset_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetUserStarGiftQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::userGift>> promise_;

 public:
  explicit GetUserStarGiftQuery(Promise<td_api::object_ptr<td_api::userGift>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(MessageId message_id) {
    send_query(G()->net_query_creator().create(
        telegram_api::payments_getUserStarGift({message_id.get_server_message_id().get()})));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_getUserStarGift>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetUserStarGiftQuery: " << to_string(ptr);

    for (auto &gift : ptr->gifts_) {
      UserStarGift user_gift(td_, std::move(gift), true);
      if (!user_gift.is_valid()) {
        LOG(ERROR) << "Receive invalid user gift";
        continue;
      }
      return promise_.set_value(user_gift.get_user_gift_object(td_));
    }
    promise_.set_error(Status::Error(400, "Gift not found"));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

StarGiftManager::StarGiftManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
  update_gift_message_timeout_.set_callback(on_update_gift_message_timeout_callback);
  update_gift_message_timeout_.set_callback_data(static_cast<void *>(this));
}

StarGiftManager::~StarGiftManager() {
  Scheduler::instance()->destroy_on_scheduler(G()->get_gc_scheduler_id(), gift_full_message_ids_,
                                              gift_full_message_ids_by_id_, being_reloaded_gift_messages_,
                                              user_gift_infos_);
}

void StarGiftManager::start_up() {
  if (!td_->auth_manager_->is_bot()) {
    class StateCallback final : public StateManager::Callback {
     public:
      explicit StateCallback(ActorId<StarGiftManager> parent) : parent_(std::move(parent)) {
      }
      bool on_online(bool is_online) final {
        if (is_online) {
          send_closure(parent_, &StarGiftManager::on_online);
        }
        return parent_.is_alive();
      }

     private:
      ActorId<StarGiftManager> parent_;
    };
    send_closure(G()->state_manager(), &StateManager::add_callback, make_unique<StateCallback>(actor_id(this)));
  }
}

void StarGiftManager::tear_down() {
  parent_.reset();
}

void StarGiftManager::get_gift_payment_options(Promise<td_api::object_ptr<td_api::gifts>> &&promise) {
  td_->create_handler<GetStarGiftsQuery>(std::move(promise))->send();
}

void StarGiftManager::on_get_star_gift(const StarGift &star_gift, bool from_server) {
  if (td_->auth_manager_->is_bot() || !star_gift.is_valid() || star_gift.is_unique()) {
    return;
  }
  if (!from_server && gift_prices_.count(star_gift.get_id())) {
    return;
  }
  gift_prices_[star_gift.get_id()] = {star_gift.get_star_count(), star_gift.get_upgrade_star_count()};
}

void StarGiftManager::on_get_user_star_gift(MessageFullId message_full_id, bool can_upgrade, int64 upgrade_star_count) {
  UserStarGiftInfo info;
  info.can_upgrade_ = can_upgrade;
  info.upgrade_star_count_ = upgrade_star_count;
  user_gift_infos_.set(message_full_id, info);
}

void StarGiftManager::send_gift(int64 gift_id, UserId user_id, td_api::object_ptr<td_api::formattedText> text,
                                bool is_private, bool pay_for_upgrade, Promise<Unit> &&promise) {
  int64 star_count = 0;
  if (!td_->auth_manager_->is_bot()) {
    auto it = gift_prices_.find(gift_id);
    if (it == gift_prices_.end()) {
      return promise.set_error(Status::Error(400, "Gift not found"));
    }
    star_count = it->second.first;
    if (pay_for_upgrade) {
      star_count += it->second.second;
    }
    if (!td_->star_manager_->has_owned_star_count(star_count)) {
      return promise.set_error(Status::Error(400, "Have not enough Telegram Stars"));
    }
  }
  TRY_RESULT_PROMISE(promise, input_user, td_->user_manager_->get_input_user(user_id));
  TRY_RESULT_PROMISE(
      promise, message,
      get_formatted_text(td_, td_->dialog_manager_->get_my_dialog_id(), std::move(text), false, true, true, false));
  MessageQuote::remove_unallowed_quote_entities(message);

  int32 flags = 0;
  if (is_private) {
    flags |= telegram_api::inputInvoiceStarGift::HIDE_NAME_MASK;
  }
  if (pay_for_upgrade) {
    flags |= telegram_api::inputInvoiceStarGift::INCLUDE_UPGRADE_MASK;
  }
  auto input_invoice = telegram_api::make_object<telegram_api::inputInvoiceStarGift>(
      flags, false /*ignored*/, false /*ignored*/, std::move(input_user), gift_id, nullptr);
  auto send_input_invoice = telegram_api::make_object<telegram_api::inputInvoiceStarGift>(
      flags, false /*ignored*/, false /*ignored*/, td_->user_manager_->get_input_user(user_id).move_as_ok(), gift_id,
      nullptr);
  if (!message.text.empty()) {
    input_invoice->flags_ |= telegram_api::inputInvoiceStarGift::MESSAGE_MASK;
    input_invoice->message_ = get_input_text_with_entities(td_->user_manager_.get(), message, "send_gift");

    send_input_invoice->flags_ |= telegram_api::inputInvoiceStarGift::MESSAGE_MASK;
    send_input_invoice->message_ = get_input_text_with_entities(td_->user_manager_.get(), message, "send_gift");
  }
  td_->create_handler<GetGiftPaymentFormQuery>(std::move(promise))
      ->send(std::move(input_invoice), std::move(send_input_invoice), star_count);
}

void StarGiftManager::convert_gift(UserId user_id, MessageId message_id, Promise<Unit> &&promise) {
  TRY_RESULT_PROMISE(promise, input_user, td_->user_manager_->get_input_user(user_id));
  if (!message_id.is_valid() || !message_id.is_server()) {
    return promise.set_error(Status::Error(400, "Invalid message identifier specified"));
  }
  td_->create_handler<ConvertStarGiftQuery>(std::move(promise))->send(message_id);
}

void StarGiftManager::save_gift(UserId user_id, MessageId message_id, bool is_saved, Promise<Unit> &&promise) {
  TRY_RESULT_PROMISE(promise, input_user, td_->user_manager_->get_input_user(user_id));
  if (!message_id.is_valid() || !message_id.is_server()) {
    return promise.set_error(Status::Error(400, "Invalid message identifier specified"));
  }
  td_->create_handler<SaveStarGiftQuery>(std::move(promise))->send(message_id, is_saved);
}

void StarGiftManager::get_gift_upgrade_preview(int64 gift_id,
                                               Promise<td_api::object_ptr<td_api::giftUpgradePreview>> &&promise) {
  td_->create_handler<GetUpgradeGiftPreviewQuery>(std::move(promise))->send(gift_id);
}

void StarGiftManager::upgrade_gift(UserId user_id, MessageId message_id, bool keep_original_details,
                                   Promise<td_api::object_ptr<td_api::upgradeGiftResult>> &&promise) {
  TRY_RESULT_PROMISE(promise, input_user, td_->user_manager_->get_input_user(user_id));
  if (!message_id.is_valid() || !message_id.is_server()) {
    return promise.set_error(Status::Error(400, "Invalid message identifier specified"));
  }
  auto message_full_id = MessageFullId{DialogId(user_id), message_id};
  auto star_count = td_->messages_manager_->get_message_gift_upgrade_star_count(message_full_id);
  if (star_count < 0) {
    if (user_gift_infos_.count(message_full_id) == 0) {
      return promise.set_error(Status::Error(400, "Gift not found"));
    }
    auto info = user_gift_infos_.get(message_full_id);
    if (!info.can_upgrade_) {
      return promise.set_error(Status::Error(400, "Gift can't be upgraded"));
    }
    star_count = info.upgrade_star_count_;
  }
  if (star_count != 0) {
    if (!td_->star_manager_->has_owned_star_count(star_count)) {
      return promise.set_error(Status::Error(400, "Have not enough Telegram Stars"));
    }
    int32 flags = 0;
    if (keep_original_details) {
      flags |= telegram_api::inputInvoiceStarGiftUpgrade::KEEP_ORIGINAL_DETAILS_MASK;
    }
    auto input_invoice = telegram_api::make_object<telegram_api::inputInvoiceStarGiftUpgrade>(
        flags, false /*ignored*/, message_id.get_server_message_id().get());
    auto upgrade_input_invoice = telegram_api::make_object<telegram_api::inputInvoiceStarGiftUpgrade>(
        flags, false /*ignored*/, message_id.get_server_message_id().get());
    td_->create_handler<GetGiftUpgradePaymentFormQuery>(std::move(promise))
        ->send(std::move(input_invoice), std::move(upgrade_input_invoice), star_count);
  } else {
    td_->create_handler<UpgradeStarGiftQuery>(std::move(promise))
        ->send(std::move(input_user), message_id, star_count, keep_original_details);
  }
}

void StarGiftManager::transfer_gift(UserId user_id, MessageId message_id, UserId receiver_user_id, int64 star_count,
                                    Promise<Unit> &&promise) {
  TRY_RESULT_PROMISE(promise, input_user, td_->user_manager_->get_input_user(user_id));
  TRY_RESULT_PROMISE(promise, receiver_input_user, td_->user_manager_->get_input_user(receiver_user_id));
  if (!message_id.is_valid() || !message_id.is_server()) {
    return promise.set_error(Status::Error(400, "Invalid message identifier specified"));
  }
  if (star_count < 0) {
    return promise.set_error(Status::Error(400, "Invalid amount of Telegram Stars specified"));
  }
  if (star_count != 0) {
    if (!td_->star_manager_->has_owned_star_count(star_count)) {
      return promise.set_error(Status::Error(400, "Have not enough Telegram Stars"));
    }
    auto input_invoice = telegram_api::make_object<telegram_api::inputInvoiceStarGiftTransfer>(
        message_id.get_server_message_id().get(), std::move(receiver_input_user));
    auto transfer_input_invoice = telegram_api::make_object<telegram_api::inputInvoiceStarGiftTransfer>(
        message_id.get_server_message_id().get(), td_->user_manager_->get_input_user(receiver_user_id).move_as_ok());
    td_->create_handler<GetGiftTransferPaymentFormQuery>(std::move(promise))
        ->send(std::move(input_invoice), std::move(transfer_input_invoice), star_count);
  } else {
    td_->create_handler<TransferStarGiftQuery>(std::move(promise))->send(message_id, std::move(receiver_input_user));
  }
}

void StarGiftManager::get_user_gifts(UserId user_id, const string &offset, int32 limit,
                                     Promise<td_api::object_ptr<td_api::userGifts>> &&promise) {
  TRY_RESULT_PROMISE(promise, input_user, td_->user_manager_->get_input_user(user_id));
  if (limit < 0) {
    return promise.set_error(Status::Error(400, "Limit must be non-negative"));
  }
  td_->create_handler<GetUserGiftsQuery>(std::move(promise))->send(user_id, std::move(input_user), offset, limit);
}

void StarGiftManager::get_user_gift(MessageId message_id, Promise<td_api::object_ptr<td_api::userGift>> &&promise) {
  if (!message_id.is_valid() || !message_id.is_server()) {
    return promise.set_error(Status::Error(400, "Invalid message identifier specified"));
  }
  td_->create_handler<GetUserStarGiftQuery>(std::move(promise))->send(message_id);
}

void StarGiftManager::register_gift(MessageFullId message_full_id, const char *source) {
  auto message_id = message_full_id.get_message_id();
  if (message_id.is_scheduled()) {
    return;
  }
  CHECK(!td_->auth_manager_->is_bot());
  CHECK(message_id.is_valid());
  CHECK(message_id.is_server());
  LOG(INFO) << "Register gift in " << message_full_id << " from " << source;
  auto gift_message_number = ++gift_message_count_;
  gift_full_message_ids_.set(message_full_id, gift_message_number);
  gift_full_message_ids_by_id_.set(gift_message_number, message_full_id);
  update_gift_message_timeout_.add_timeout_in(gift_message_number, 0);
}

void StarGiftManager::unregister_gift(MessageFullId message_full_id, const char *source) {
  auto message_id = message_full_id.get_message_id();
  if (message_id.is_scheduled()) {
    return;
  }
  CHECK(!td_->auth_manager_->is_bot());
  CHECK(message_id.is_valid());
  CHECK(message_id.is_server());
  LOG(INFO) << "Unregister gift in " << message_full_id << " from " << source;
  auto message_number = gift_full_message_ids_[message_full_id];
  LOG_CHECK(message_number != 0) << source << ' ' << message_full_id;
  gift_full_message_ids_by_id_.erase(message_number);
  if (!G()->close_flag()) {
    update_gift_message_timeout_.cancel_timeout(message_number);
  }
  gift_full_message_ids_.erase(message_full_id);
}

double StarGiftManager::get_gift_message_polling_timeout() const {
  double result = td_->online_manager_->is_online() ? 60 : 30 * 60;
  return result * Random::fast(70, 100) * 0.01;
}

void StarGiftManager::on_online() {
  if (td_->auth_manager_->is_bot()) {
    return;
  }

  gift_full_message_ids_.foreach([&](MessageFullId, int64 message_number) {
    if (update_gift_message_timeout_.has_timeout(message_number)) {
      update_gift_message_timeout_.set_timeout_in(message_number, Random::fast(3, 30));
    }
  });
}

void StarGiftManager::on_update_gift_message_timeout_callback(void *star_gift_manager_ptr, int64 message_number) {
  if (G()->close_flag()) {
    return;
  }

  auto star_gift_manager = static_cast<StarGiftManager *>(star_gift_manager_ptr);
  send_closure_later(star_gift_manager->actor_id(star_gift_manager), &StarGiftManager::on_update_gift_message_timeout,
                     message_number);
}

void StarGiftManager::on_update_gift_message_timeout(int64 message_number) {
  if (G()->close_flag()) {
    return;
  }
  CHECK(!td_->auth_manager_->is_bot());
  auto message_full_id = gift_full_message_ids_by_id_.get(message_number);
  if (message_full_id.get_message_id() == MessageId()) {
    return;
  }
  if (!being_reloaded_gift_messages_.insert(message_full_id).second) {
    return;
  }
  LOG(INFO) << "Fetching gift from " << message_full_id;
  auto promise = PromiseCreator::lambda([actor_id = actor_id(this), message_full_id](Unit result) {
    send_closure(actor_id, &StarGiftManager::on_update_gift_message, message_full_id);
  });
  td_->messages_manager_->get_message_from_server(message_full_id, std::move(promise),
                                                  "on_update_gift_message_timeout");
}

void StarGiftManager::on_update_gift_message(MessageFullId message_full_id) {
  if (G()->close_flag()) {
    return;
  }
  auto is_erased = being_reloaded_gift_messages_.erase(message_full_id) > 0;
  CHECK(is_erased);
  auto message_number = gift_full_message_ids_.get(message_full_id);
  if (message_number == 0) {
    return;
  }

  auto timeout = get_gift_message_polling_timeout();
  LOG(INFO) << "Schedule updating of gift in " << message_full_id << " in " << timeout;
  update_gift_message_timeout_.add_timeout_in(message_number, timeout);
}

}  // namespace td
