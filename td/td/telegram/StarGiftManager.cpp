//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/StarGiftManager.h"

#include "td/telegram/AccessRights.h"
#include "td/telegram/AuthManager.h"
#include "td/telegram/BusinessConnectionManager.h"
#include "td/telegram/ChatManager.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/DialogManager.h"
#include "td/telegram/EmojiStatus.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/MessageQuote.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/OnlineManager.h"
#include "td/telegram/OptionManager.h"
#include "td/telegram/PasswordManager.h"
#include "td/telegram/StarGift.h"
#include "td/telegram/StarGiftAttribute.h"
#include "td/telegram/StarGiftAttributeId.h"
#include "td/telegram/StarManager.h"
#include "td/telegram/StateManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UpdatesManager.h"
#include "td/telegram/UserManager.h"
#include "td/telegram/UserStarGift.h"

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Random.h"
#include "td/utils/Status.h"

#include <algorithm>

namespace td {

class GetStarGiftsQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::availableGifts>> promise_;

 public:
  explicit GetStarGiftsQuery(Promise<td_api::object_ptr<td_api::availableGifts>> &&promise)
      : promise_(std::move(promise)) {
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
    vector<td_api::object_ptr<td_api::availableGift>> options;
    for (auto &gift : results->gifts_) {
      int64 availability_resale = 0;
      int64 resell_min_stars = 0;
      string title;
      if (gift->get_id() == telegram_api::starGift::ID) {
        auto star_gift = static_cast<const telegram_api::starGift *>(gift.get());
        availability_resale = star_gift->availability_resale_;
        resell_min_stars = StarManager::get_star_count(star_gift->resell_min_stars_);
        title = star_gift->title_;
        if (availability_resale < 0 || availability_resale > 1000000000) {
          LOG(ERROR) << "Receive " << availability_resale << " available gifts";
          availability_resale = 0;
        } else if (resell_min_stars == 0 && availability_resale > 0) {
          LOG(ERROR) << "Receive " << availability_resale << " available gifts with the minimum price of "
                     << resell_min_stars;
          availability_resale = 0;
        }
        if (availability_resale == 0) {
          resell_min_stars = 0;
          title.clear();
        }
      }

      StarGift star_gift(td_, std::move(gift), false);
      if (!star_gift.is_valid()) {
        continue;
      }
      td_->star_gift_manager_->on_get_star_gift(star_gift, true);
      options.push_back(td_api::make_object<td_api::availableGift>(
          star_gift.get_gift_object(td_), narrow_cast<int32>(availability_resale), resell_min_stars, title));
    }

    promise_.set_value(td_api::make_object<td_api::availableGifts>(std::move(options)));
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
    if (status.message() == "FORM_SUBMIT_DUPLICATE") {
      LOG(ERROR) << "Receive FORM_SUBMIT_DUPLICATE";
    }
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
  DialogId dialog_id_;

 public:
  explicit ConvertStarGiftQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(BusinessConnectionId business_connection_id, StarGiftId star_gift_id, DialogId dialog_id) {
    dialog_id_ = dialog_id;
    auto input_gift = star_gift_id.get_input_saved_star_gift(td_);
    CHECK(input_gift != nullptr);
    send_query(G()->net_query_creator().create_with_prefix(
        business_connection_id.get_invoke_prefix(), telegram_api::payments_convertStarGift(std::move(input_gift)),
        td_->business_connection_manager_->get_business_connection_dc_id(business_connection_id)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_convertStarGift>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    if (dialog_id_ == td_->dialog_manager_->get_my_dialog_id()) {
      td_->user_manager_->reload_user_full(td_->user_manager_->get_my_id(), std::move(promise_),
                                           "ConvertStarGiftQuery");
    } else if (dialog_id_.get_type() == DialogType::Channel) {
      td_->chat_manager_->reload_channel_full(dialog_id_.get_channel_id(), std::move(promise_), "ConvertStarGiftQuery");
    } else {
      promise_.set_value(Unit());
    }
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class SaveStarGiftQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  DialogId dialog_id_;
  bool is_saved_;

 public:
  explicit SaveStarGiftQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(StarGiftId star_gift_id, bool is_saved) {
    dialog_id_ = star_gift_id.get_dialog_id(td_);
    is_saved_ = is_saved;
    send_query(G()->net_query_creator().create(
        telegram_api::payments_saveStarGift(0, !is_saved, star_gift_id.get_input_saved_star_gift(td_)),
        {{dialog_id_}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_saveStarGift>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class ToggleStarGiftsPinnedToTopQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  DialogId dialog_id_;

 public:
  explicit ToggleStarGiftsPinnedToTopQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(DialogId dialog_id, const vector<StarGiftId> &star_gift_ids) {
    dialog_id_ = dialog_id;
    auto input_peer = td_->dialog_manager_->get_input_peer(dialog_id_, AccessRights::Read);
    CHECK(input_peer != nullptr);
    auto input_star_gifts = transform(star_gift_ids, [td = td_](const StarGiftId &star_gift_id) {
      return star_gift_id.get_input_saved_star_gift(td);
    });
    send_query(G()->net_query_creator().create(
        telegram_api::payments_toggleStarGiftsPinnedToTop(std::move(input_peer), std::move(input_star_gifts)),
        {{dialog_id_}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_toggleStarGiftsPinnedToTop>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    td_->dialog_manager_->on_get_dialog_error(dialog_id_, status, "ToggleStarGiftsPinnedToTopQuery");
    promise_.set_error(std::move(status));
  }
};

class ToggleChatStarGiftNotificationsQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  DialogId dialog_id_;

 public:
  explicit ToggleChatStarGiftNotificationsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(DialogId dialog_id, bool are_enabled) {
    dialog_id_ = dialog_id;
    auto input_peer = td_->dialog_manager_->get_input_peer(dialog_id_, AccessRights::Read);
    CHECK(input_peer != nullptr);
    send_query(G()->net_query_creator().create(
        telegram_api::payments_toggleChatStarGiftNotifications(0, are_enabled, std::move(input_peer)), {{dialog_id_}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_toggleChatStarGiftNotifications>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    td_->dialog_manager_->on_get_dialog_error(dialog_id_, status, "ToggleChatStarGiftNotificationsQuery");
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
  if (td->auth_manager_->is_bot()) {
    return PromiseCreator::lambda([promise = std::move(promise)](Result<Unit> result) mutable {
      if (result.is_error()) {
        return promise.set_error(result.move_as_error());
      }
      promise.set_value(td_api::make_object<td_api::upgradeGiftResult>());
    });
  }
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

  void send(BusinessConnectionId business_connection_id, StarGiftId star_gift_id, bool keep_original_details) {
    auto input_gift = star_gift_id.get_input_saved_star_gift(td_);
    CHECK(input_gift != nullptr);
    send_query(G()->net_query_creator().create_with_prefix(
        business_connection_id.get_invoke_prefix(),
        telegram_api::payments_upgradeStarGift(0, keep_original_details, std::move(input_gift)),
        td_->business_connection_manager_->get_business_connection_dc_id(business_connection_id)));
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
    get_upgraded_gift_emoji_statuses(td_, Auto());
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

  void send(BusinessConnectionId business_connection_id,
            telegram_api::object_ptr<telegram_api::inputInvoiceStarGiftUpgrade> input_invoice, int64 payment_form_id,
            int64 star_count) {
    star_count_ = star_count;
    send_query(G()->net_query_creator().create_with_prefix(
        business_connection_id.get_invoke_prefix(),
        telegram_api::payments_sendStarsForm(payment_form_id, std::move(input_invoice)),
        td_->business_connection_manager_->get_business_connection_dc_id(business_connection_id)));
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
    get_upgraded_gift_emoji_statuses(td_, Auto());
  }

  void on_error(Status status) final {
    if (status.message() == "FORM_SUBMIT_DUPLICATE") {
      LOG(ERROR) << "Receive FORM_SUBMIT_DUPLICATE";
    }
    td_->star_manager_->add_pending_owned_star_count(star_count_, false);
    promise_.set_error(std::move(status));
  }
};

class GetGiftUpgradePaymentFormQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::upgradeGiftResult>> promise_;
  BusinessConnectionId business_connection_id_;
  int64 star_count_;
  telegram_api::object_ptr<telegram_api::inputInvoiceStarGiftUpgrade> upgrade_input_invoice_;

 public:
  explicit GetGiftUpgradePaymentFormQuery(Promise<td_api::object_ptr<td_api::upgradeGiftResult>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(BusinessConnectionId business_connection_id,
            telegram_api::object_ptr<telegram_api::inputInvoiceStarGiftUpgrade> input_invoice,
            telegram_api::object_ptr<telegram_api::inputInvoiceStarGiftUpgrade> upgrade_input_invoice,
            int64 star_count) {
    business_connection_id_ = business_connection_id;
    upgrade_input_invoice_ = std::move(upgrade_input_invoice);
    star_count_ = star_count;
    td_->star_manager_->add_pending_owned_star_count(-star_count, false);
    send_query(G()->net_query_creator().create_with_prefix(
        business_connection_id.get_invoke_prefix(),
        telegram_api::payments_getPaymentForm(0, std::move(input_invoice), nullptr),
        td_->business_connection_manager_->get_business_connection_dc_id(business_connection_id)));
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
        if (payment_form->invoice_->prices_.size() != 1u ||
            payment_form->invoice_->prices_[0]->amount_ != star_count_) {
          td_->star_manager_->add_pending_owned_star_count(star_count_, false);
          return promise_.set_error(Status::Error(400, "Wrong upgrade price specified"));
        }
        td_->create_handler<UpgradeGiftQuery>(std::move(promise_))
            ->send(business_connection_id_, std::move(upgrade_input_invoice_), payment_form->form_id_, star_count_);
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

  void send(BusinessConnectionId business_connection_id, StarGiftId star_gift_id,
            telegram_api::object_ptr<telegram_api::InputPeer> receiver_input_peer) {
    auto input_gift = star_gift_id.get_input_saved_star_gift(td_);
    CHECK(input_gift != nullptr);
    send_query(G()->net_query_creator().create_with_prefix(
        business_connection_id.get_invoke_prefix(),
        telegram_api::payments_transferStarGift(std::move(input_gift), std::move(receiver_input_peer)),
        td_->business_connection_manager_->get_business_connection_dc_id(business_connection_id)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_transferStarGift>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for TransferStarGiftQuery: " << to_string(ptr);
    td_->updates_manager_->on_get_updates(std::move(ptr), std::move(promise_));
    get_upgraded_gift_emoji_statuses(td_, Auto());
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

  void send(BusinessConnectionId business_connection_id,
            telegram_api::object_ptr<telegram_api::inputInvoiceStarGiftTransfer> input_invoice, int64 payment_form_id,
            int64 star_count) {
    star_count_ = star_count;
    send_query(G()->net_query_creator().create_with_prefix(
        business_connection_id.get_invoke_prefix(),
        telegram_api::payments_sendStarsForm(payment_form_id, std::move(input_invoice)),
        td_->business_connection_manager_->get_business_connection_dc_id(business_connection_id)));
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
    get_upgraded_gift_emoji_statuses(td_, Auto());
  }

  void on_error(Status status) final {
    if (status.message() == "FORM_SUBMIT_DUPLICATE") {
      LOG(ERROR) << "Receive FORM_SUBMIT_DUPLICATE";
    }
    td_->star_manager_->add_pending_owned_star_count(star_count_, false);
    promise_.set_error(std::move(status));
  }
};

class GetGiftTransferPaymentFormQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  BusinessConnectionId business_connection_id_;
  int64 star_count_;
  telegram_api::object_ptr<telegram_api::inputInvoiceStarGiftTransfer> transfer_input_invoice_;

 public:
  explicit GetGiftTransferPaymentFormQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(BusinessConnectionId business_connection_id,
            telegram_api::object_ptr<telegram_api::inputInvoiceStarGiftTransfer> input_invoice,
            telegram_api::object_ptr<telegram_api::inputInvoiceStarGiftTransfer> transfer_input_invoice,
            int64 star_count) {
    business_connection_id_ = business_connection_id;
    transfer_input_invoice_ = std::move(transfer_input_invoice);
    star_count_ = star_count;
    td_->star_manager_->add_pending_owned_star_count(-star_count, false);
    send_query(G()->net_query_creator().create_with_prefix(
        business_connection_id.get_invoke_prefix(),
        telegram_api::payments_getPaymentForm(0, std::move(input_invoice), nullptr),
        td_->business_connection_manager_->get_business_connection_dc_id(business_connection_id)));
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
        if (payment_form->invoice_->prices_.size() != 1u ||
            payment_form->invoice_->prices_[0]->amount_ != star_count_) {
          td_->star_manager_->add_pending_owned_star_count(star_count_, false);
          return promise_.set_error(Status::Error(400, "Wrong transfer price specified"));
        }
        td_->create_handler<TransferGiftQuery>(std::move(promise_))
            ->send(business_connection_id_, std::move(transfer_input_invoice_), payment_form->form_id_, star_count_);
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

class ResaleGiftQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  int64 star_count_;

 public:
  explicit ResaleGiftQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(telegram_api::object_ptr<telegram_api::inputInvoiceStarGiftResale> input_invoice, int64 payment_form_id,
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
    LOG(INFO) << "Receive result for ResaleGiftQuery: " << to_string(payment_result);
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
    get_upgraded_gift_emoji_statuses(td_, Auto());
  }

  void on_error(Status status) final {
    if (status.message() == "FORM_SUBMIT_DUPLICATE") {
      LOG(ERROR) << "Receive FORM_SUBMIT_DUPLICATE";
    }
    td_->star_manager_->add_pending_owned_star_count(star_count_, false);
    promise_.set_error(std::move(status));
  }
};

class GetGiftResalePaymentFormQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  int64 star_count_;
  telegram_api::object_ptr<telegram_api::inputInvoiceStarGiftResale> resale_input_invoice_;

 public:
  explicit GetGiftResalePaymentFormQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(telegram_api::object_ptr<telegram_api::inputInvoiceStarGiftResale> input_invoice,
            telegram_api::object_ptr<telegram_api::inputInvoiceStarGiftResale> resale_input_invoice, int64 star_count) {
    resale_input_invoice_ = std::move(resale_input_invoice);
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
    LOG(INFO) << "Receive result for GetGiftResalePaymentFormQuery: " << to_string(payment_form_ptr);
    switch (payment_form_ptr->get_id()) {
      case telegram_api::payments_paymentForm::ID:
      case telegram_api::payments_paymentFormStars::ID:
        LOG(ERROR) << "Receive " << to_string(payment_form_ptr);
        td_->star_manager_->add_pending_owned_star_count(star_count_, false);
        promise_.set_error(Status::Error(500, "Unsupported"));
        break;
      case telegram_api::payments_paymentFormStarGift::ID: {
        auto payment_form = static_cast<const telegram_api::payments_paymentFormStarGift *>(payment_form_ptr.get());
        if (payment_form->invoice_->prices_.size() != 1u ||
            payment_form->invoice_->prices_[0]->amount_ != star_count_) {
          td_->star_manager_->add_pending_owned_star_count(star_count_, false);
          return promise_.set_error(Status::Error(400, "Wrong resale price specified"));
        }
        td_->create_handler<ResaleGiftQuery>(std::move(promise_))
            ->send(std::move(resale_input_invoice_), payment_form->form_id_, star_count_);
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

class GetSavedStarGiftsQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::receivedGifts>> promise_;
  DialogId dialog_id_;
  BusinessConnectionId business_connection_id_;

 public:
  explicit GetSavedStarGiftsQuery(Promise<td_api::object_ptr<td_api::receivedGifts>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(BusinessConnectionId business_connection_id, DialogId dialog_id, bool exclude_unsaved, bool exclude_saved,
            bool exclude_unlimited, bool exclude_limited, bool exclude_unique, bool sort_by_value, const string &offset,
            int32 limit) {
    business_connection_id_ = business_connection_id;
    dialog_id_ =
        business_connection_id.is_valid()
            ? DialogId(td_->business_connection_manager_->get_business_connection_user_id(business_connection_id))
            : dialog_id;
    auto input_peer = td_->dialog_manager_->get_input_peer(dialog_id_, AccessRights::Read);
    if (input_peer == nullptr) {
      return on_error(Status::Error(400, "Can't access the chat"));
    }
    send_query(G()->net_query_creator().create_with_prefix(
        business_connection_id.get_invoke_prefix(),
        telegram_api::payments_getSavedStarGifts(0, exclude_unsaved, exclude_saved, exclude_unlimited, exclude_limited,
                                                 exclude_unique, sort_by_value, std::move(input_peer), offset, limit),
        td_->business_connection_manager_->get_business_connection_dc_id(business_connection_id), {{dialog_id_}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_getSavedStarGifts>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetSavedStarGiftsQuery: " << to_string(ptr);
    td_->user_manager_->on_get_users(std::move(ptr->users_), "GetSavedStarGiftsQuery");
    td_->chat_manager_->on_get_chats(std::move(ptr->chats_), "GetSavedStarGiftsQuery");

    auto total_count = ptr->count_;
    if (total_count < static_cast<int32>(ptr->gifts_.size())) {
      LOG(ERROR) << "Receive " << ptr->gifts_.size() << " gifts with total count = " << total_count;
      total_count = static_cast<int32>(ptr->gifts_.size());
    }
    vector<td_api::object_ptr<td_api::receivedGift>> gifts;
    for (auto &gift : ptr->gifts_) {
      UserStarGift user_gift(td_, std::move(gift), dialog_id_);
      if (!user_gift.is_valid()) {
        LOG(ERROR) << "Receive invalid user gift";
        continue;
      }
      gifts.push_back(user_gift.get_received_gift_object(td_));
    }
    bool are_notifications_enabled = false;
    if (dialog_id_.get_type() == DialogType::User) {
      if (dialog_id_ != td_->dialog_manager_->get_my_dialog_id()) {
        td_->user_manager_->on_update_user_gift_count(dialog_id_.get_user_id(), total_count);
      } else {
        are_notifications_enabled = true;
      }
    } else if (dialog_id_.get_type() == DialogType::Channel) {
      td_->chat_manager_->on_update_channel_gift_count(dialog_id_.get_channel_id(), total_count, false);
      are_notifications_enabled = ptr->chat_notifications_enabled_;
    }
    promise_.set_value(td_api::make_object<td_api::receivedGifts>(total_count, std::move(gifts),
                                                                  are_notifications_enabled, ptr->next_offset_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetSavedStarGiftQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::receivedGift>> promise_;
  DialogId dialog_id_;

 public:
  explicit GetSavedStarGiftQuery(Promise<td_api::object_ptr<td_api::receivedGift>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(StarGiftId star_gift_id) {
    auto input_gift = star_gift_id.get_input_saved_star_gift(td_);
    if (input_gift == nullptr) {
      return on_error(Status::Error(400, "Gift not found"));
    }
    vector<telegram_api::object_ptr<telegram_api::InputSavedStarGift>> gifts;
    gifts.push_back(std::move(input_gift));
    dialog_id_ = star_gift_id.get_dialog_id(td_);
    send_query(G()->net_query_creator().create(telegram_api::payments_getSavedStarGift(std::move(gifts))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_getSavedStarGift>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetSavedStarGiftQuery: " << to_string(ptr);

    for (auto &gift : ptr->gifts_) {
      UserStarGift user_gift(td_, std::move(gift), dialog_id_);
      if (!user_gift.is_valid()) {
        LOG(ERROR) << "Receive invalid user gift";
        continue;
      }
      return promise_.set_value(user_gift.get_received_gift_object(td_));
    }
    promise_.set_error(Status::Error(400, "Gift not found"));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetUniqueStarGiftQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::upgradedGift>> promise_;

 public:
  explicit GetUniqueStarGiftQuery(Promise<td_api::object_ptr<td_api::upgradedGift>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(const string &name) {
    send_query(G()->net_query_creator().create(telegram_api::payments_getUniqueStarGift(name)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_getUniqueStarGift>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetUniqueStarGiftQuery: " << to_string(ptr);

    td_->user_manager_->on_get_users(std::move(ptr->users_), "GetUniqueStarGiftQuery");

    StarGift star_gift(td_, std::move(ptr->gift_), true);
    if (!star_gift.is_valid() || !star_gift.is_unique()) {
      LOG(ERROR) << "Receive invalid user gift";
      return promise_.set_error(Status::Error(400, "Gift not found"));
    }
    promise_.set_value(star_gift.get_upgraded_gift_object(td_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetStarGiftWithdrawalUrlQuery final : public Td::ResultHandler {
  Promise<string> promise_;

 public:
  explicit GetStarGiftWithdrawalUrlQuery(Promise<string> &&promise) : promise_(std::move(promise)) {
  }

  void send(StarGiftId star_gift_id,
            telegram_api::object_ptr<telegram_api::InputCheckPasswordSRP> input_check_password) {
    auto input_gift = star_gift_id.get_input_saved_star_gift(td_);
    if (input_gift == nullptr) {
      return on_error(Status::Error(400, "Gift not found"));
    }

    send_query(G()->net_query_creator().create(
        telegram_api::payments_getStarGiftWithdrawalUrl(std::move(input_gift), std::move(input_check_password))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_getStarGiftWithdrawalUrl>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    promise_.set_value(std::move(result_ptr.ok_ref()->url_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class UpdateStarGiftPriceQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit UpdateStarGiftPriceQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(StarGiftId star_gift_id, int64 resale_star_count) {
    auto input_gift = star_gift_id.get_input_saved_star_gift(td_);
    if (input_gift == nullptr) {
      return on_error(Status::Error(400, "Gift not found"));
    }

    send_query(G()->net_query_creator().create(
        telegram_api::payments_updateStarGiftPrice(std::move(input_gift), resale_star_count)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_updateStarGiftPrice>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for UpdateStarGiftPriceQuery: " << to_string(ptr);
    td_->updates_manager_->on_get_updates(std::move(ptr), std::move(promise_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetResaleStarGiftsQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::giftsForResale>> promise_;

 public:
  explicit GetResaleStarGiftsQuery(Promise<td_api::object_ptr<td_api::giftsForResale>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(int64 gift_id, const td_api::object_ptr<td_api::GiftForResaleOrder> &order,
            const vector<StarGiftAttributeId> &attribute_ids, const string &offset, int32 limit) {
    int32 flags = 0;
    auto order_id = order->get_id();
    auto attributes = StarGiftAttributeId::get_input_star_gift_attribute_ids_object(attribute_ids);
    if (!attributes.empty()) {
      flags |= telegram_api::payments_getResaleStarGifts::ATTRIBUTES_MASK;
    }
    if (offset.empty() && attributes.empty()) {
      flags |= telegram_api::payments_getResaleStarGifts::ATTRIBUTES_HASH_MASK;
    }
    send_query(G()->net_query_creator().create(telegram_api::payments_getResaleStarGifts(
        flags, order_id == td_api::giftForResaleOrderPrice::ID, order_id == td_api::giftForResaleOrderNumber::ID, 0,
        gift_id, std::move(attributes), offset, limit)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_getResaleStarGifts>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetResaleStarGiftsQuery: " << to_string(ptr);
    td_->user_manager_->on_get_users(std::move(ptr->users_), "GetResaleStarGiftsQuery");
    td_->chat_manager_->on_get_chats(std::move(ptr->chats_), "GetResaleStarGiftsQuery");

    auto total_count = ptr->count_;
    if (total_count < static_cast<int32>(ptr->gifts_.size())) {
      LOG(ERROR) << "Receive " << ptr->gifts_.size() << " gifts with total count = " << total_count;
      total_count = static_cast<int32>(ptr->gifts_.size());
    }
    vector<td_api::object_ptr<td_api::giftForResale>> gifts;
    for (auto &gift : ptr->gifts_) {
      StarGift star_gift(td_, std::move(gift), true);
      if (!star_gift.is_valid() || !star_gift.is_unique()) {
        LOG(ERROR) << "Receive invalid upgraded gift";
        continue;
      }
      gifts.push_back(star_gift.get_gift_for_resale_object(td_));
    }
    FlatHashMap<StarGiftAttributeId, int32, StarGiftAttributeIdHash> counters;
    for (auto &counter : ptr->counters_) {
      if (counter->count_ <= 0) {
        LOG(ERROR) << "Receive " << to_string(counter);
        continue;
      }
      StarGiftAttributeId attribute(std::move(counter->attribute_));
      CHECK(attribute != StarGiftAttributeId());
      counters[attribute] = counter->count_;
    }
    auto get_count = [&](StarGiftAttributeId attribute_id) {
      auto it = counters.find(attribute_id);
      if (it == counters.end()) {
        LOG(ERROR) << "Can't find counter for " << attribute_id;
        return 0;
      }
      auto count = it->second;
      counters.erase(it);
      return count;
    };
    vector<td_api::object_ptr<td_api::upgradedGiftModelCount>> models;
    vector<td_api::object_ptr<td_api::upgradedGiftSymbolCount>> symbols;
    vector<td_api::object_ptr<td_api::upgradedGiftBackdropCount>> backdrops;
    for (auto &attribute : ptr->attributes_) {
      switch (attribute->get_id()) {
        case telegram_api::starGiftAttributeModel::ID: {
          auto model = StarGiftAttributeSticker(
              td_, telegram_api::move_object_as<telegram_api::starGiftAttributeModel>(attribute));
          if (!model.is_valid()) {
            LOG(ERROR) << "Receive invalid model";
            break;
          }
          auto count = get_count(model.get_id(td_, true));
          if (count > 0) {
            models.push_back(
                td_api::make_object<td_api::upgradedGiftModelCount>(model.get_upgraded_gift_model_object(td_), count));
          }
          break;
        }
        case telegram_api::starGiftAttributePattern::ID: {
          auto pattern = StarGiftAttributeSticker(
              td_, telegram_api::move_object_as<telegram_api::starGiftAttributePattern>(attribute));
          if (!pattern.is_valid()) {
            LOG(ERROR) << "Receive invalid symbol";
            break;
          }
          auto count = get_count(pattern.get_id(td_, false));
          if (count > 0) {
            symbols.push_back(td_api::make_object<td_api::upgradedGiftSymbolCount>(
                pattern.get_upgraded_gift_symbol_object(td_), count));
          }
          break;
        }
        case telegram_api::starGiftAttributeBackdrop::ID: {
          auto backdrop = StarGiftAttributeBackdrop(
              telegram_api::move_object_as<telegram_api::starGiftAttributeBackdrop>(attribute));
          if (!backdrop.is_valid()) {
            LOG(ERROR) << "Receive invalid backdrop";
            break;
          }
          auto count = get_count(backdrop.get_id());
          if (count > 0) {
            backdrops.push_back(td_api::make_object<td_api::upgradedGiftBackdropCount>(
                backdrop.get_upgraded_gift_backdrop_object(), count));
          }
          break;
        }
        case telegram_api::starGiftAttributeOriginalDetails::ID:
          LOG(ERROR) << "Receive original details";
          break;
        default:
          UNREACHABLE();
      }
    }
    auto compare_by_count = [](auto &lhs, auto &rhs) {
      return lhs->total_count_ > rhs->total_count_;
    };
    std::sort(models.begin(), models.end(), compare_by_count);
    std::sort(symbols.begin(), symbols.end(), compare_by_count);
    std::sort(backdrops.begin(), backdrops.end(), compare_by_count);

    if (!counters.empty()) {
      LOG(ERROR) << "Receive " << counters.size() << " unused counters";
    }
    promise_.set_value(td_api::make_object<td_api::giftsForResale>(
        total_count, std::move(gifts), std::move(models), std::move(symbols), std::move(backdrops), ptr->next_offset_));
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
  Scheduler::instance()->destroy_on_scheduler(G()->get_gc_scheduler_id(), gift_message_full_ids_,
                                              gift_message_full_ids_by_id_, being_reloaded_gift_messages_);
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

void StarGiftManager::get_gift_payment_options(Promise<td_api::object_ptr<td_api::availableGifts>> &&promise) {
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

void StarGiftManager::send_gift(int64 gift_id, DialogId dialog_id, td_api::object_ptr<td_api::formattedText> text,
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
  auto input_peer = td_->dialog_manager_->get_input_peer(dialog_id, AccessRights::Read);
  auto send_input_peer = td_->dialog_manager_->get_input_peer(dialog_id, AccessRights::Read);
  if (input_peer == nullptr || send_input_peer == nullptr) {
    return promise.set_error(Status::Error(400, "Have no access to the gift receiver"));
  }
  TRY_RESULT_PROMISE(
      promise, message,
      get_formatted_text(td_, td_->dialog_manager_->get_my_dialog_id(), std::move(text), false, true, true, false));
  MessageQuote::remove_unallowed_quote_entities(message);

  auto input_invoice = telegram_api::make_object<telegram_api::inputInvoiceStarGift>(
      0, is_private, pay_for_upgrade, std::move(input_peer), gift_id, nullptr);
  auto send_input_invoice = telegram_api::make_object<telegram_api::inputInvoiceStarGift>(
      0, is_private, pay_for_upgrade, std::move(send_input_peer), gift_id, nullptr);
  if (!message.text.empty()) {
    input_invoice->flags_ |= telegram_api::inputInvoiceStarGift::MESSAGE_MASK;
    input_invoice->message_ = get_input_text_with_entities(td_->user_manager_.get(), message, "send_gift");

    send_input_invoice->flags_ |= telegram_api::inputInvoiceStarGift::MESSAGE_MASK;
    send_input_invoice->message_ = get_input_text_with_entities(td_->user_manager_.get(), message, "send_gift");
  }
  td_->create_handler<GetGiftPaymentFormQuery>(std::move(promise))
      ->send(std::move(input_invoice), std::move(send_input_invoice), star_count);
}

void StarGiftManager::convert_gift(BusinessConnectionId business_connection_id, StarGiftId star_gift_id,
                                   Promise<Unit> &&promise) {
  if (business_connection_id.is_valid()) {
    TRY_STATUS_PROMISE(promise, td_->business_connection_manager_->check_business_connection(business_connection_id));
  }
  if (star_gift_id.get_input_saved_star_gift(td_) == nullptr) {
    return promise.set_error(Status::Error(400, "Invalid gift identifier specified"));
  }
  auto dialog_id =
      business_connection_id.is_valid()
          ? DialogId(td_->business_connection_manager_->get_business_connection_user_id(business_connection_id))
          : star_gift_id.get_dialog_id(td_);

  auto query_promise = PromiseCreator::lambda(
      [actor_id = actor_id(this), dialog_id, promise = std::move(promise)](Result<Unit> &&result) mutable {
        if (result.is_error()) {
          return promise.set_error(result.move_as_error());
        }
        send_closure(actor_id, &StarGiftManager::on_dialog_gift_transferred, dialog_id, DialogId(), std::move(promise));
      });
  td_->create_handler<ConvertStarGiftQuery>(std::move(query_promise))
      ->send(business_connection_id, star_gift_id, dialog_id);
}

void StarGiftManager::save_gift(StarGiftId star_gift_id, bool is_saved, Promise<Unit> &&promise) {
  if (star_gift_id.get_input_saved_star_gift(td_) == nullptr) {
    return promise.set_error(Status::Error(400, "Invalid gift identifier specified"));
  }
  td_->create_handler<SaveStarGiftQuery>(std::move(promise))->send(star_gift_id, is_saved);
}

void StarGiftManager::set_dialog_pinned_gifts(DialogId dialog_id, const vector<StarGiftId> &star_gift_ids,
                                              Promise<Unit> &&promise) {
  for (const auto &star_gift_id : star_gift_ids) {
    if (star_gift_id.get_input_saved_star_gift(td_) == nullptr) {
      return promise.set_error(Status::Error(400, "Invalid gift identifier specified"));
    }
    if (star_gift_id.get_dialog_id(td_) != dialog_id) {
      return promise.set_error(Status::Error(400, "The gift is not from the chat"));
    }
  }
  td_->create_handler<ToggleStarGiftsPinnedToTopQuery>(std::move(promise))->send(dialog_id, star_gift_ids);
}

void StarGiftManager::toggle_chat_star_gift_notifications(DialogId dialog_id, bool are_enabled,
                                                          Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, td_->dialog_manager_->check_dialog_access(dialog_id, false, AccessRights::Read,
                                                                        "toggle_chat_star_gift_notifications"));
  if (!td_->dialog_manager_->is_broadcast_channel(dialog_id) ||
      !td_->chat_manager_->get_channel_status(dialog_id.get_channel_id()).can_post_messages()) {
    return promise.set_error(Status::Error(400, "Wrong chat specified"));
  }
  td_->create_handler<ToggleChatStarGiftNotificationsQuery>(std::move(promise))->send(dialog_id, are_enabled);
}

void StarGiftManager::get_gift_upgrade_preview(int64 gift_id,
                                               Promise<td_api::object_ptr<td_api::giftUpgradePreview>> &&promise) {
  td_->create_handler<GetUpgradeGiftPreviewQuery>(std::move(promise))->send(gift_id);
}

void StarGiftManager::upgrade_gift(BusinessConnectionId business_connection_id, StarGiftId star_gift_id,
                                   bool keep_original_details, int64 star_count,
                                   Promise<td_api::object_ptr<td_api::upgradeGiftResult>> &&promise) {
  bool as_business = business_connection_id.is_valid();
  if (as_business) {
    TRY_STATUS_PROMISE(promise, td_->business_connection_manager_->check_business_connection(business_connection_id));
  }
  auto input_saved_star_gift = star_gift_id.get_input_saved_star_gift(td_);
  if (input_saved_star_gift == nullptr) {
    return promise.set_error(Status::Error(400, "Invalid gift identifier specified"));
  }
  if (star_count < 0) {
    return promise.set_error(Status::Error(400, "Invalid amount of Telegram Stars specified"));
  }
  if (star_count != 0) {
    if (!as_business && !td_->star_manager_->has_owned_star_count(star_count)) {
      return promise.set_error(Status::Error(400, "Have not enough Telegram Stars"));
    }
    auto input_invoice = telegram_api::make_object<telegram_api::inputInvoiceStarGiftUpgrade>(
        0, keep_original_details, std::move(input_saved_star_gift));
    auto upgrade_input_invoice = telegram_api::make_object<telegram_api::inputInvoiceStarGiftUpgrade>(
        0, keep_original_details, star_gift_id.get_input_saved_star_gift(td_));
    td_->create_handler<GetGiftUpgradePaymentFormQuery>(std::move(promise))
        ->send(business_connection_id, std::move(input_invoice), std::move(upgrade_input_invoice), star_count);
  } else {
    td_->create_handler<UpgradeStarGiftQuery>(std::move(promise))
        ->send(business_connection_id, star_gift_id, keep_original_details);
  }
}

void StarGiftManager::transfer_gift(BusinessConnectionId business_connection_id, StarGiftId star_gift_id,
                                    DialogId receiver_dialog_id, int64 star_count, Promise<Unit> &&promise) {
  bool as_business = business_connection_id.is_valid();
  if (as_business) {
    TRY_STATUS_PROMISE(promise, td_->business_connection_manager_->check_business_connection(business_connection_id));
  }
  auto access_rights = as_business ? AccessRights::Know : AccessRights::Read;
  auto input_peer = td_->dialog_manager_->get_input_peer(receiver_dialog_id, access_rights);
  auto transfer_input_peer = td_->dialog_manager_->get_input_peer(receiver_dialog_id, access_rights);
  if (input_peer == nullptr || transfer_input_peer == nullptr) {
    return promise.set_error(Status::Error(400, "Have no access to the new gift owner"));
  }
  auto input_saved_star_gift = star_gift_id.get_input_saved_star_gift(td_);
  if (input_saved_star_gift == nullptr) {
    return promise.set_error(Status::Error(400, "Invalid gift identifier specified"));
  }
  if (star_count < 0) {
    return promise.set_error(Status::Error(400, "Invalid amount of Telegram Stars specified"));
  }
  auto dialog_id =
      as_business ? DialogId(td_->business_connection_manager_->get_business_connection_user_id(business_connection_id))
                  : star_gift_id.get_dialog_id(td_);
  auto query_promise = PromiseCreator::lambda([actor_id = actor_id(this), dialog_id, receiver_dialog_id,
                                               promise = std::move(promise)](Result<Unit> &&result) mutable {
    if (result.is_error()) {
      return promise.set_error(result.move_as_error());
    }
    send_closure(actor_id, &StarGiftManager::on_dialog_gift_transferred, dialog_id, receiver_dialog_id,
                 std::move(promise));
  });
  if (star_count != 0) {
    if (!as_business && !td_->star_manager_->has_owned_star_count(star_count)) {
      return query_promise.set_error(Status::Error(400, "Have not enough Telegram Stars"));
    }
    auto input_invoice = telegram_api::make_object<telegram_api::inputInvoiceStarGiftTransfer>(
        std::move(input_saved_star_gift), std::move(input_peer));
    auto transfer_input_invoice = telegram_api::make_object<telegram_api::inputInvoiceStarGiftTransfer>(
        star_gift_id.get_input_saved_star_gift(td_), std::move(transfer_input_peer));
    td_->create_handler<GetGiftTransferPaymentFormQuery>(std::move(query_promise))
        ->send(business_connection_id, std::move(input_invoice), std::move(transfer_input_invoice), star_count);
  } else {
    td_->create_handler<TransferStarGiftQuery>(std::move(query_promise))
        ->send(business_connection_id, star_gift_id, std::move(input_peer));
  }
}

void StarGiftManager::on_dialog_gift_transferred(DialogId from_dialog_id, DialogId to_dialog_id,
                                                 Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  if (from_dialog_id == td_->dialog_manager_->get_my_dialog_id()) {
    td_->user_manager_->on_update_my_gift_count(-1);
  } else if (from_dialog_id.get_type() == DialogType::Channel) {
    td_->chat_manager_->on_update_channel_gift_count(from_dialog_id.get_channel_id(), -1, true);
  }
  if (to_dialog_id == td_->dialog_manager_->get_my_dialog_id()) {
    td_->user_manager_->on_update_my_gift_count(1);
  } else if (to_dialog_id.get_type() == DialogType::Channel &&
             td_->chat_manager_->get_channel_status(to_dialog_id.get_channel_id()).can_post_messages()) {
    td_->chat_manager_->on_update_channel_gift_count(to_dialog_id.get_channel_id(), 1, true);
  }
  promise.set_value(Unit());
}

void StarGiftManager::send_resold_gift(const string &gift_name, DialogId receiver_dialog_id, int64 star_count,
                                       Promise<Unit> &&promise) {
  auto input_peer = td_->dialog_manager_->get_input_peer(receiver_dialog_id, AccessRights::Read);
  auto resale_input_peer = td_->dialog_manager_->get_input_peer(receiver_dialog_id, AccessRights::Read);
  if (input_peer == nullptr || resale_input_peer == nullptr) {
    return promise.set_error(Status::Error(400, "Have no access to the new gift owner"));
  }
  if (star_count < 0) {
    return promise.set_error(Status::Error(400, "Invalid amount of Telegram Stars specified"));
  }
  if (!td_->star_manager_->has_owned_star_count(star_count)) {
    return promise.set_error(Status::Error(400, "Have not enough Telegram Stars"));
  }
  auto input_invoice =
      telegram_api::make_object<telegram_api::inputInvoiceStarGiftResale>(gift_name, std::move(input_peer));
  auto resale_input_invoice =
      telegram_api::make_object<telegram_api::inputInvoiceStarGiftResale>(gift_name, std::move(resale_input_peer));
  td_->create_handler<GetGiftResalePaymentFormQuery>(std::move(promise))
      ->send(std::move(input_invoice), std::move(resale_input_invoice), star_count);
}

void StarGiftManager::get_saved_star_gifts(BusinessConnectionId business_connection_id, DialogId dialog_id,
                                           bool exclude_unsaved, bool exclude_saved, bool exclude_unlimited,
                                           bool exclude_limited, bool exclude_unique, bool sort_by_value,
                                           const string &offset, int32 limit,
                                           Promise<td_api::object_ptr<td_api::receivedGifts>> &&promise) {
  if (limit < 0) {
    return promise.set_error(Status::Error(400, "Limit must be non-negative"));
  }
  if (business_connection_id.is_valid()) {
    TRY_STATUS_PROMISE(promise, td_->business_connection_manager_->check_business_connection(business_connection_id));
  }
  td_->create_handler<GetSavedStarGiftsQuery>(std::move(promise))
      ->send(business_connection_id, dialog_id, exclude_unsaved, exclude_saved, exclude_unlimited, exclude_limited,
             exclude_unique, sort_by_value, offset, limit);
}

void StarGiftManager::get_saved_star_gift(StarGiftId star_gift_id,
                                          Promise<td_api::object_ptr<td_api::receivedGift>> &&promise) {
  if (!star_gift_id.is_valid()) {
    return promise.set_error(Status::Error(400, "Invalid gift identifier specified"));
  }
  td_->create_handler<GetSavedStarGiftQuery>(std::move(promise))->send(star_gift_id);
}

void StarGiftManager::get_upgraded_gift(const string &name,
                                        Promise<td_api::object_ptr<td_api::upgradedGift>> &&promise) {
  td_->create_handler<GetUniqueStarGiftQuery>(std::move(promise))->send(name);
}

void StarGiftManager::get_star_gift_withdrawal_url(StarGiftId star_gift_id, const string &password,
                                                   Promise<string> &&promise) {
  if (!star_gift_id.is_valid()) {
    return promise.set_error(Status::Error(400, "Invalid gift identifier specified"));
  }
  if (password.empty()) {
    return promise.set_error(Status::Error(400, "PASSWORD_HASH_INVALID"));
  }
  send_closure(
      td_->password_manager_, &PasswordManager::get_input_check_password_srp, password,
      PromiseCreator::lambda([actor_id = actor_id(this), star_gift_id, promise = std::move(promise)](
                                 Result<telegram_api::object_ptr<telegram_api::InputCheckPasswordSRP>> result) mutable {
        if (result.is_error()) {
          return promise.set_error(result.move_as_error());
        }
        send_closure(actor_id, &StarGiftManager::send_get_star_gift_withdrawal_url_query, star_gift_id,
                     result.move_as_ok(), std::move(promise));
      }));
}

void StarGiftManager::send_get_star_gift_withdrawal_url_query(
    StarGiftId star_gift_id, telegram_api::object_ptr<telegram_api::InputCheckPasswordSRP> input_check_password,
    Promise<string> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());

  td_->create_handler<GetStarGiftWithdrawalUrlQuery>(std::move(promise))
      ->send(star_gift_id, std::move(input_check_password));
}

void StarGiftManager::set_star_gift_price(StarGiftId star_gift_id, int64 resale_star_count, Promise<Unit> &&promise) {
  if (!star_gift_id.is_valid()) {
    return promise.set_error(Status::Error(400, "Invalid gift identifier specified"));
  }
  if ((resale_star_count != 0 &&
       resale_star_count < td_->option_manager_->get_option_integer("gift_resale_star_count_min")) ||
      resale_star_count > td_->option_manager_->get_option_integer("gift_resale_star_count_max")) {
    return promise.set_error(Status::Error(400, "Invalid resale price specified"));
  }

  td_->create_handler<UpdateStarGiftPriceQuery>(std::move(promise))->send(star_gift_id, resale_star_count);
}

void StarGiftManager::get_resale_star_gifts(
    int64 gift_id, const td_api::object_ptr<td_api::GiftForResaleOrder> &order,
    const vector<td_api::object_ptr<td_api::UpgradedGiftAttributeId>> &attributes, const string &offset, int32 limit,
    Promise<td_api::object_ptr<td_api::giftsForResale>> &&promise) {
  if (limit < 0) {
    return promise.set_error(Status::Error(400, "Limit must be non-negative"));
  }
  if (order == nullptr) {
    return promise.set_error(Status::Error(400, "Gift sort order must be non-empty"));
  }
  TRY_RESULT_PROMISE(promise, attribute_ids, StarGiftAttributeId::get_star_gift_attribute_ids(attributes));

  td_->create_handler<GetResaleStarGiftsQuery>(std::move(promise))->send(gift_id, order, attribute_ids, offset, limit);
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
  gift_message_full_ids_.set(message_full_id, gift_message_number);
  gift_message_full_ids_by_id_.set(gift_message_number, message_full_id);
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
  auto message_number = gift_message_full_ids_[message_full_id];
  LOG_CHECK(message_number != 0) << source << ' ' << message_full_id;
  gift_message_full_ids_by_id_.erase(message_number);
  if (!G()->close_flag()) {
    update_gift_message_timeout_.cancel_timeout(message_number);
  }
  gift_message_full_ids_.erase(message_full_id);
}

double StarGiftManager::get_gift_message_polling_timeout() const {
  double result = td_->online_manager_->is_online() ? 60 : 30 * 60;
  return result * Random::fast(70, 100) * 0.01;
}

void StarGiftManager::on_online() {
  if (td_->auth_manager_->is_bot()) {
    return;
  }

  gift_message_full_ids_.foreach([&](MessageFullId, int64 message_number) {
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
  auto message_full_id = gift_message_full_ids_by_id_.get(message_number);
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
  auto message_number = gift_message_full_ids_.get(message_full_id);
  if (message_number == 0) {
    return;
  }

  auto timeout = get_gift_message_polling_timeout();
  LOG(INFO) << "Schedule updating of gift in " << message_full_id << " in " << timeout;
  update_gift_message_timeout_.add_timeout_in(message_number, timeout);
}

}  // namespace td
