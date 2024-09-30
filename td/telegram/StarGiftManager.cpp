//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/StarGiftManager.h"

#include "td/telegram/DialogManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/MessageQuote.h"
#include "td/telegram/ServerMessageId.h"
#include "td/telegram/StarGift.h"
#include "td/telegram/StarManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UpdatesManager.h"
#include "td/telegram/UserManager.h"

#include "td/utils/buffer.h"
#include "td/utils/logging.h"
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
    FlatHashMap<int64, int64> gift_prices;
    for (auto &gift : results->gifts_) {
      StarGift star_gift(td_, std::move(gift));
      if (!star_gift.is_valid()) {
        continue;
      }
      auto gift_id = star_gift.get_id();
      if (gift_prices.count(gift_id) != 0) {
        LOG(ERROR) << "Receive again gift " << gift_id;
        continue;
      }
      gift_prices[gift_id] = star_gift.get_star_count();
      options.push_back(star_gift.get_gift_object(td_));
    }

    td_->star_gift_manager_->on_get_gift_prices(std::move(gift_prices));
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

  void send(telegram_api::object_ptr<telegram_api::InputUser> input_user, MessageId message_id) {
    send_query(G()->net_query_creator().create(
        telegram_api::payments_convertStarGift(std::move(input_user), message_id.get_server_message_id().get())));
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

  void send(telegram_api::object_ptr<telegram_api::InputUser> input_user, MessageId message_id, bool is_saved) {
    is_saved_ = is_saved;
    int32 flags = 0;
    if (!is_saved) {
      flags |= telegram_api::payments_saveStarGift::UNSAVE_MASK;
    }
    send_query(G()->net_query_creator().create(telegram_api::payments_saveStarGift(
        flags, false /*ignored*/, std::move(input_user), message_id.get_server_message_id().get())));
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
      UserId sender_user_id(gift->from_id_);
      if (sender_user_id != UserId() && !sender_user_id.is_valid()) {
        LOG(ERROR) << "Receive " << sender_user_id << " as sender of a gift";
        sender_user_id = UserId();
      }
      if (sender_user_id == UserId() && !gift->name_hidden_) {
        LOG(ERROR) << "Receive a gift without a sender";
        continue;
      }
      if (gift->unsaved_ && !is_me) {
        LOG(ERROR) << "Receive unsaved gift for " << user_id_;
        gift->unsaved_ = false;
      }
      StarGift star_gift(td_, std::move(gift->gift_));
      if (!star_gift.is_valid()) {
        continue;
      }
      FormattedText text =
          get_formatted_text(td_->user_manager_.get(), std::move(gift->message_), true, false, "userStarGift");
      auto message_id = MessageId(ServerMessageId(gift->msg_id_));
      if (message_id != MessageId() && !message_id.is_valid()) {
        LOG(ERROR) << "Receive " << message_id;
        message_id = MessageId();
      }
      gifts.push_back(td_api::make_object<td_api::userGift>(
          td_->user_manager_->get_user_id_object(sender_user_id, "userGift"),
          get_formatted_text_object(td_->user_manager_.get(), text, true, -1), gift->name_hidden_, !gift->unsaved_,
          gift->date_, star_gift.get_gift_object(td_), message_id.get(),
          StarManager::get_star_count(gift->convert_stars_)));
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

StarGiftManager::StarGiftManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

void StarGiftManager::tear_down() {
  parent_.reset();
}

void StarGiftManager::get_gift_payment_options(Promise<td_api::object_ptr<td_api::gifts>> &&promise) {
  td_->create_handler<GetStarGiftsQuery>(std::move(promise))->send();
}

void StarGiftManager::on_get_gift_prices(FlatHashMap<int64, int64> gift_prices) {
  gift_prices_ = std::move(gift_prices);
}

void StarGiftManager::send_gift(int64 gift_id, UserId user_id, td_api::object_ptr<td_api::formattedText> text,
                                bool is_private, Promise<Unit> &&promise) {
  auto it = gift_prices_.find(gift_id);
  if (it == gift_prices_.end()) {
    return promise.set_error(Status::Error(400, "Gift not found"));
  }
  auto star_count = it->second;
  if (!td_->star_manager_->has_owned_star_count(star_count)) {
    return promise.set_error(Status::Error(400, "Have not enough Telegram Stars"));
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
  auto input_invoice = telegram_api::make_object<telegram_api::inputInvoiceStarGift>(
      flags, false /*ignored*/, std::move(input_user), gift_id, nullptr);
  auto send_input_invoice = telegram_api::make_object<telegram_api::inputInvoiceStarGift>(
      flags, false /*ignored*/, td_->user_manager_->get_input_user(user_id).move_as_ok(), gift_id, nullptr);
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
  td_->create_handler<ConvertStarGiftQuery>(std::move(promise))->send(std::move(input_user), message_id);
}

void StarGiftManager::save_gift(UserId user_id, MessageId message_id, bool is_saved, Promise<Unit> &&promise) {
  TRY_RESULT_PROMISE(promise, input_user, td_->user_manager_->get_input_user(user_id));
  if (!message_id.is_valid() || !message_id.is_server()) {
    return promise.set_error(Status::Error(400, "Invalid message identifier specified"));
  }
  td_->create_handler<SaveStarGiftQuery>(std::move(promise))->send(std::move(input_user), message_id, is_saved);
}

void StarGiftManager::get_user_gifts(UserId user_id, const string &offset, int32 limit,
                                     Promise<td_api::object_ptr<td_api::userGifts>> &&promise) {
  TRY_RESULT_PROMISE(promise, input_user, td_->user_manager_->get_input_user(user_id));
  if (limit < 0) {
    return promise.set_error(Status::Error(400, "Limit must be non-negative"));
  }
  td_->create_handler<GetUserGiftsQuery>(std::move(promise))->send(user_id, std::move(input_user), offset, limit);
}

}  // namespace td
