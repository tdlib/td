//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/Payments.h"

#include "td/telegram/AccessRights.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/files/FileManager.h"
#include "td/telegram/files/FileType.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/misc.h"
#include "td/telegram/PasswordManager.h"
#include "td/telegram/ServerMessageId.h"
#include "td/telegram/Td.h"
#include "td/telegram/UpdatesManager.h"

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/format.h"
#include "td/utils/HttpUrl.h"
#include "td/utils/JsonBuilder.h"
#include "td/utils/logging.h"
#include "td/utils/MimeType.h"
#include "td/utils/PathView.h"
#include "td/utils/Status.h"

namespace td {

class SetBotShippingAnswerQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit SetBotShippingAnswerQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(int64 shipping_query_id, const string &error_message,
            vector<tl_object_ptr<telegram_api::shippingOption>> &&shipping_options) {
    int32 flags = 0;
    if (!error_message.empty()) {
      flags |= telegram_api::messages_setBotShippingResults::ERROR_MASK;
    }
    if (!shipping_options.empty()) {
      flags |= telegram_api::messages_setBotShippingResults::SHIPPING_OPTIONS_MASK;
    }
    send_query(G()->net_query_creator().create(telegram_api::messages_setBotShippingResults(
        flags, shipping_query_id, error_message, std::move(shipping_options))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_setBotShippingResults>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    bool result = result_ptr.ok();
    if (!result) {
      LOG(INFO) << "Sending answer to a shipping query has failed";
    }
    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class SetBotPreCheckoutAnswerQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit SetBotPreCheckoutAnswerQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(int64 pre_checkout_query_id, const string &error_message) {
    int32 flags = 0;
    if (!error_message.empty()) {
      flags |= telegram_api::messages_setBotPrecheckoutResults::ERROR_MASK;
    } else {
      flags |= telegram_api::messages_setBotPrecheckoutResults::SUCCESS_MASK;
    }

    send_query(G()->net_query_creator().create(telegram_api::messages_setBotPrecheckoutResults(
        flags, false /*ignored*/, pre_checkout_query_id, error_message)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_setBotPrecheckoutResults>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    bool result = result_ptr.ok();
    if (!result) {
      LOG(INFO) << "Sending answer to a pre-checkout query has failed";
    }
    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

static tl_object_ptr<td_api::invoice> convert_invoice(tl_object_ptr<telegram_api::invoice> invoice) {
  CHECK(invoice != nullptr);

  vector<tl_object_ptr<td_api::labeledPricePart>> labeled_prices;
  labeled_prices.reserve(invoice->prices_.size());
  for (auto &labeled_price : invoice->prices_) {
    labeled_prices.push_back(
        make_tl_object<td_api::labeledPricePart>(std::move(labeled_price->label_), labeled_price->amount_));
  }

  bool is_test = (invoice->flags_ & telegram_api::invoice::TEST_MASK) != 0;
  bool need_name = (invoice->flags_ & telegram_api::invoice::NAME_REQUESTED_MASK) != 0;
  bool need_phone_number = (invoice->flags_ & telegram_api::invoice::PHONE_REQUESTED_MASK) != 0;
  bool need_email_address = (invoice->flags_ & telegram_api::invoice::EMAIL_REQUESTED_MASK) != 0;
  bool need_shipping_address = (invoice->flags_ & telegram_api::invoice::SHIPPING_ADDRESS_REQUESTED_MASK) != 0;
  bool send_phone_number_to_provider = (invoice->flags_ & telegram_api::invoice::PHONE_TO_PROVIDER_MASK) != 0;
  bool send_email_address_to_provider = (invoice->flags_ & telegram_api::invoice::EMAIL_TO_PROVIDER_MASK) != 0;
  bool is_flexible = (invoice->flags_ & telegram_api::invoice::FLEXIBLE_MASK) != 0;
  if (send_phone_number_to_provider) {
    need_phone_number = true;
  }
  if (send_email_address_to_provider) {
    need_email_address = true;
  }
  if (is_flexible) {
    need_shipping_address = true;
  }

  return make_tl_object<td_api::invoice>(
      std::move(invoice->currency_), std::move(labeled_prices), invoice->max_tip_amount_,
      vector<int64>(invoice->suggested_tip_amounts_), is_test, need_name, need_phone_number, need_email_address,
      need_shipping_address, send_phone_number_to_provider, send_email_address_to_provider, is_flexible);
}

static tl_object_ptr<td_api::paymentsProviderStripe> convert_payment_provider(
    const string &native_provider_name, tl_object_ptr<telegram_api::dataJSON> native_parameters) {
  if (native_parameters == nullptr) {
    return nullptr;
  }

  if (native_provider_name == "stripe") {
    string data = native_parameters->data_;
    auto r_value = json_decode(data);
    if (r_value.is_error()) {
      LOG(ERROR) << "Can't parse JSON object \"" << native_parameters->data_ << "\": " << r_value.error();
      return nullptr;
    }

    auto value = r_value.move_as_ok();
    if (value.type() != JsonValue::Type::Object) {
      LOG(ERROR) << "Wrong JSON data \"" << native_parameters->data_ << '"';
      return nullptr;
    }

    auto r_need_country = get_json_object_bool_field(value.get_object(), "need_country", false);
    auto r_need_postal_code = get_json_object_bool_field(value.get_object(), "need_zip", false);
    auto r_need_cardholder_name = get_json_object_bool_field(value.get_object(), "need_cardholder_name", false);
    auto r_publishable_key = get_json_object_string_field(value.get_object(), "publishable_key", false);
    // TODO support "gpay_parameters":{"gateway":"stripe","stripe:publishableKey":"...","stripe:version":"..."}

    if (r_need_country.is_error() || r_need_postal_code.is_error() || r_need_cardholder_name.is_error() ||
        r_publishable_key.is_error()) {
      LOG(ERROR) << "Unsupported JSON data \"" << native_parameters->data_ << '"';
      return nullptr;
    }
    if (value.get_object().size() != 5) {
      LOG(ERROR) << "Unsupported JSON data \"" << native_parameters->data_ << '"';
    }

    return make_tl_object<td_api::paymentsProviderStripe>(r_publishable_key.move_as_ok(), r_need_country.move_as_ok(),
                                                          r_need_postal_code.move_as_ok(),
                                                          r_need_cardholder_name.move_as_ok());
  }

  return nullptr;
}

static tl_object_ptr<td_api::address> convert_address(tl_object_ptr<telegram_api::postAddress> address) {
  if (address == nullptr) {
    return nullptr;
  }
  return make_tl_object<td_api::address>(std::move(address->country_iso2_), std::move(address->state_),
                                         std::move(address->city_), std::move(address->street_line1_),
                                         std::move(address->street_line2_), std::move(address->post_code_));
}

static tl_object_ptr<telegram_api::postAddress> convert_address(tl_object_ptr<td_api::address> address) {
  if (address == nullptr) {
    return nullptr;
  }
  return make_tl_object<telegram_api::postAddress>(std::move(address->street_line1_), std::move(address->street_line2_),
                                                   std::move(address->city_), std::move(address->state_),
                                                   std::move(address->country_code_), std::move(address->postal_code_));
}

static tl_object_ptr<td_api::orderInfo> convert_order_info(
    tl_object_ptr<telegram_api::paymentRequestedInfo> order_info) {
  if (order_info == nullptr) {
    return nullptr;
  }
  return make_tl_object<td_api::orderInfo>(std::move(order_info->name_), std::move(order_info->phone_),
                                           std::move(order_info->email_),
                                           convert_address(std::move(order_info->shipping_address_)));
}

static tl_object_ptr<td_api::labeledPricePart> convert_labeled_price(
    tl_object_ptr<telegram_api::labeledPrice> labeled_price) {
  CHECK(labeled_price != nullptr);
  return make_tl_object<td_api::labeledPricePart>(std::move(labeled_price->label_), labeled_price->amount_);
}

static tl_object_ptr<td_api::shippingOption> convert_shipping_option(
    tl_object_ptr<telegram_api::shippingOption> shipping_option) {
  if (shipping_option == nullptr) {
    return nullptr;
  }

  return make_tl_object<td_api::shippingOption>(std::move(shipping_option->id_), std::move(shipping_option->title_),
                                                transform(std::move(shipping_option->prices_), convert_labeled_price));
}

static tl_object_ptr<telegram_api::paymentRequestedInfo> convert_order_info(
    tl_object_ptr<td_api::orderInfo> order_info) {
  if (order_info == nullptr) {
    return nullptr;
  }
  int32 flags = 0;
  if (!order_info->name_.empty()) {
    flags |= telegram_api::paymentRequestedInfo::NAME_MASK;
  }
  if (!order_info->phone_number_.empty()) {
    flags |= telegram_api::paymentRequestedInfo::PHONE_MASK;
  }
  if (!order_info->email_address_.empty()) {
    flags |= telegram_api::paymentRequestedInfo::EMAIL_MASK;
  }
  if (order_info->shipping_address_ != nullptr) {
    flags |= telegram_api::paymentRequestedInfo::SHIPPING_ADDRESS_MASK;
  }
  return make_tl_object<telegram_api::paymentRequestedInfo>(
      flags, std::move(order_info->name_), std::move(order_info->phone_number_), std::move(order_info->email_address_),
      convert_address(std::move(order_info->shipping_address_)));
}

static tl_object_ptr<td_api::savedCredentials> convert_saved_credentials(
    tl_object_ptr<telegram_api::paymentSavedCredentialsCard> saved_credentials) {
  if (saved_credentials == nullptr) {
    return nullptr;
  }
  return make_tl_object<td_api::savedCredentials>(std::move(saved_credentials->id_),
                                                  std::move(saved_credentials->title_));
}

class GetPaymentFormQuery final : public Td::ResultHandler {
  Promise<tl_object_ptr<td_api::paymentForm>> promise_;
  DialogId dialog_id_;

 public:
  explicit GetPaymentFormQuery(Promise<tl_object_ptr<td_api::paymentForm>> &&promise) : promise_(std::move(promise)) {
  }

  void send(DialogId dialog_id, ServerMessageId server_message_id,
            tl_object_ptr<telegram_api::dataJSON> &&theme_parameters) {
    dialog_id_ = dialog_id;
    auto input_peer = td_->messages_manager_->get_input_peer(dialog_id, AccessRights::Read);
    if (input_peer == nullptr) {
      return on_error(Status::Error(400, "Can't access the chat"));
    }

    int32 flags = 0;
    if (theme_parameters != nullptr) {
      flags |= telegram_api::payments_getPaymentForm::THEME_PARAMS_MASK;
    }
    send_query(G()->net_query_creator().create(telegram_api::payments_getPaymentForm(
        flags, std::move(input_peer), server_message_id.get(), std::move(theme_parameters))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_getPaymentForm>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto payment_form = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetPaymentFormQuery: " << to_string(payment_form);

    td_->contacts_manager_->on_get_users(std::move(payment_form->users_), "GetPaymentFormQuery");

    UserId payments_provider_user_id(payment_form->provider_id_);
    if (!payments_provider_user_id.is_valid()) {
      LOG(ERROR) << "Receive invalid payments provider " << payments_provider_user_id;
      return on_error(Status::Error(500, "Receive invalid payments provider identifier"));
    }
    UserId seller_bot_user_id(payment_form->bot_id_);
    if (!seller_bot_user_id.is_valid()) {
      LOG(ERROR) << "Receive invalid seller " << seller_bot_user_id;
      return on_error(Status::Error(500, "Receive invalid seller identifier"));
    }
    bool can_save_credentials = payment_form->can_save_credentials_;
    bool need_password = payment_form->password_missing_;
    promise_.set_value(make_tl_object<td_api::paymentForm>(
        payment_form->form_id_, convert_invoice(std::move(payment_form->invoice_)), std::move(payment_form->url_),
        td_->contacts_manager_->get_user_id_object(seller_bot_user_id, "paymentForm seller"),
        td_->contacts_manager_->get_user_id_object(payments_provider_user_id, "paymentForm provider"),
        convert_payment_provider(payment_form->native_provider_, std::move(payment_form->native_params_)),
        convert_order_info(std::move(payment_form->saved_info_)),
        convert_saved_credentials(std::move(payment_form->saved_credentials_)), can_save_credentials, need_password));
  }

  void on_error(Status status) final {
    td_->messages_manager_->on_get_dialog_error(dialog_id_, status, "GetPaymentFormQuery");
    promise_.set_error(std::move(status));
  }
};

class ValidateRequestedInfoQuery final : public Td::ResultHandler {
  Promise<tl_object_ptr<td_api::validatedOrderInfo>> promise_;
  DialogId dialog_id_;

 public:
  explicit ValidateRequestedInfoQuery(Promise<tl_object_ptr<td_api::validatedOrderInfo>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(DialogId dialog_id, ServerMessageId server_message_id,
            tl_object_ptr<telegram_api::paymentRequestedInfo> requested_info, bool allow_save) {
    dialog_id_ = dialog_id;
    auto input_peer = td_->messages_manager_->get_input_peer(dialog_id, AccessRights::Read);
    if (input_peer == nullptr) {
      return on_error(Status::Error(400, "Can't access the chat"));
    }

    int32 flags = 0;
    if (allow_save) {
      flags |= telegram_api::payments_validateRequestedInfo::SAVE_MASK;
    }
    if (requested_info == nullptr) {
      requested_info = make_tl_object<telegram_api::paymentRequestedInfo>();
      requested_info->flags_ = 0;
    }
    send_query(G()->net_query_creator().create(telegram_api::payments_validateRequestedInfo(
        flags, false /*ignored*/, std::move(input_peer), server_message_id.get(), std::move(requested_info))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_validateRequestedInfo>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto validated_order_info = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for ValidateRequestedInfoQuery: " << to_string(validated_order_info);

    promise_.set_value(make_tl_object<td_api::validatedOrderInfo>(
        std::move(validated_order_info->id_),
        transform(std::move(validated_order_info->shipping_options_), convert_shipping_option)));
  }

  void on_error(Status status) final {
    td_->messages_manager_->on_get_dialog_error(dialog_id_, status, "ValidateRequestedInfoQuery");
    promise_.set_error(std::move(status));
  }
};

class SendPaymentFormQuery final : public Td::ResultHandler {
  Promise<tl_object_ptr<td_api::paymentResult>> promise_;
  DialogId dialog_id_;

 public:
  explicit SendPaymentFormQuery(Promise<tl_object_ptr<td_api::paymentResult>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(DialogId dialog_id, ServerMessageId server_message_id, int64 payment_form_id, const string &order_info_id,
            const string &shipping_option_id, tl_object_ptr<telegram_api::InputPaymentCredentials> input_credentials,
            int64 tip_amount) {
    CHECK(input_credentials != nullptr);

    dialog_id_ = dialog_id;
    auto input_peer = td_->messages_manager_->get_input_peer(dialog_id, AccessRights::Read);
    if (input_peer == nullptr) {
      return on_error(Status::Error(400, "Can't access the chat"));
    }

    int32 flags = 0;
    if (!order_info_id.empty()) {
      flags |= telegram_api::payments_sendPaymentForm::REQUESTED_INFO_ID_MASK;
    }
    if (!shipping_option_id.empty()) {
      flags |= telegram_api::payments_sendPaymentForm::SHIPPING_OPTION_ID_MASK;
    }
    if (tip_amount != 0) {
      flags |= telegram_api::payments_sendPaymentForm::TIP_AMOUNT_MASK;
    }
    send_query(G()->net_query_creator().create(telegram_api::payments_sendPaymentForm(
        flags, payment_form_id, std::move(input_peer), server_message_id.get(), order_info_id, shipping_option_id,
        std::move(input_credentials), tip_amount)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_sendPaymentForm>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto payment_result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for SendPaymentFormQuery: " << to_string(payment_result);

    switch (payment_result->get_id()) {
      case telegram_api::payments_paymentResult::ID: {
        auto result = move_tl_object_as<telegram_api::payments_paymentResult>(payment_result);
        td_->updates_manager_->on_get_updates(
            std::move(result->updates_), PromiseCreator::lambda([promise = std::move(promise_)](Unit) mutable {
              promise.set_value(make_tl_object<td_api::paymentResult>(true, string()));
            }));
        return;
      }
      case telegram_api::payments_paymentVerificationNeeded::ID: {
        auto result = move_tl_object_as<telegram_api::payments_paymentVerificationNeeded>(payment_result);
        promise_.set_value(make_tl_object<td_api::paymentResult>(false, std::move(result->url_)));
        return;
      }
      default:
        UNREACHABLE();
    }
  }

  void on_error(Status status) final {
    td_->messages_manager_->on_get_dialog_error(dialog_id_, status, "SendPaymentFormQuery");
    promise_.set_error(std::move(status));
  }
};

class GetPaymentReceiptQuery final : public Td::ResultHandler {
  Promise<tl_object_ptr<td_api::paymentReceipt>> promise_;
  DialogId dialog_id_;

 public:
  explicit GetPaymentReceiptQuery(Promise<tl_object_ptr<td_api::paymentReceipt>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(DialogId dialog_id, ServerMessageId server_message_id) {
    dialog_id_ = dialog_id;
    auto input_peer = td_->messages_manager_->get_input_peer(dialog_id, AccessRights::Read);
    if (input_peer == nullptr) {
      return on_error(Status::Error(400, "Can't access the chat"));
    }

    send_query(G()->net_query_creator().create(
        telegram_api::payments_getPaymentReceipt(std::move(input_peer), server_message_id.get())));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_getPaymentReceipt>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto payment_receipt = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetPaymentReceiptQuery: " << to_string(payment_receipt);

    td_->contacts_manager_->on_get_users(std::move(payment_receipt->users_), "GetPaymentReceiptQuery");

    UserId payments_provider_user_id(payment_receipt->provider_id_);
    if (!payments_provider_user_id.is_valid()) {
      LOG(ERROR) << "Receive invalid payments provider " << payments_provider_user_id;
      return on_error(Status::Error(500, "Receive invalid payments provider identifier"));
    }
    UserId seller_bot_user_id(payment_receipt->bot_id_);
    if (!seller_bot_user_id.is_valid()) {
      LOG(ERROR) << "Receive invalid seller " << seller_bot_user_id;
      return on_error(Status::Error(500, "Receive invalid seller identifier"));
    }
    auto photo = get_web_document_photo(td_->file_manager_.get(), std::move(payment_receipt->photo_), dialog_id_);

    promise_.set_value(make_tl_object<td_api::paymentReceipt>(
        payment_receipt->title_, payment_receipt->description_, get_photo_object(td_->file_manager_.get(), photo),
        payment_receipt->date_, td_->contacts_manager_->get_user_id_object(seller_bot_user_id, "paymentReceipt seller"),
        td_->contacts_manager_->get_user_id_object(payments_provider_user_id, "paymentReceipt provider"),
        convert_invoice(std::move(payment_receipt->invoice_)), convert_order_info(std::move(payment_receipt->info_)),
        convert_shipping_option(std::move(payment_receipt->shipping_)), std::move(payment_receipt->credentials_title_),
        payment_receipt->tip_amount_));
  }

  void on_error(Status status) final {
    td_->messages_manager_->on_get_dialog_error(dialog_id_, status, "GetPaymentReceiptQuery");
    promise_.set_error(std::move(status));
  }
};

class GetSavedInfoQuery final : public Td::ResultHandler {
  Promise<tl_object_ptr<td_api::orderInfo>> promise_;

 public:
  explicit GetSavedInfoQuery(Promise<tl_object_ptr<td_api::orderInfo>> &&promise) : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create(telegram_api::payments_getSavedInfo()));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_getSavedInfo>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto saved_info = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetSavedInfoQuery: " << to_string(saved_info);
    promise_.set_value(convert_order_info(std::move(saved_info->saved_info_)));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class ClearSavedInfoQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit ClearSavedInfoQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(bool clear_credentials, bool clear_order_info) {
    CHECK(clear_credentials || clear_order_info);
    int32 flags = 0;
    if (clear_credentials) {
      flags |= telegram_api::payments_clearSavedInfo::CREDENTIALS_MASK;
    }
    if (clear_order_info) {
      flags |= telegram_api::payments_clearSavedInfo::INFO_MASK;
    }
    send_query(G()->net_query_creator().create(
        telegram_api::payments_clearSavedInfo(flags, false /*ignored*/, false /*ignored*/)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_clearSavedInfo>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetBankCardInfoQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::bankCardInfo>> promise_;

 public:
  explicit GetBankCardInfoQuery(Promise<td_api::object_ptr<td_api::bankCardInfo>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(const string &bank_card_number) {
    send_query(G()->net_query_creator().create(telegram_api::payments_getBankCardData(bank_card_number), {},
                                               G()->get_webfile_dc_id()));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_getBankCardData>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto response = result_ptr.move_as_ok();
    auto actions = transform(response->open_urls_, [](auto &open_url) {
      return td_api::make_object<td_api::bankCardActionOpenUrl>(open_url->name_, open_url->url_);
    });
    promise_.set_value(td_api::make_object<td_api::bankCardInfo>(response->title_, std::move(actions)));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

bool operator==(const LabeledPricePart &lhs, const LabeledPricePart &rhs) {
  return lhs.label == rhs.label && lhs.amount == rhs.amount;
}

bool operator!=(const LabeledPricePart &lhs, const LabeledPricePart &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const LabeledPricePart &labeled_price_part) {
  return string_builder << "[" << labeled_price_part.label << ": " << labeled_price_part.amount << "]";
}

bool operator==(const Invoice &lhs, const Invoice &rhs) {
  return lhs.is_test == rhs.is_test && lhs.need_name == rhs.need_name &&
         lhs.need_phone_number == rhs.need_phone_number && lhs.need_email_address == rhs.need_email_address &&
         lhs.need_shipping_address == rhs.need_shipping_address &&
         lhs.send_phone_number_to_provider == rhs.send_phone_number_to_provider &&
         lhs.send_email_address_to_provider == rhs.send_email_address_to_provider &&
         lhs.is_flexible == rhs.is_flexible && lhs.currency == rhs.currency && lhs.price_parts == rhs.price_parts &&
         lhs.max_tip_amount == rhs.max_tip_amount && lhs.suggested_tip_amounts == rhs.suggested_tip_amounts;
}

bool operator!=(const Invoice &lhs, const Invoice &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const Invoice &invoice) {
  return string_builder << "[" << (invoice.is_flexible ? "Flexible" : "") << (invoice.is_test ? "Test" : "")
                        << "Invoice" << (invoice.need_name ? ", needs name" : "")
                        << (invoice.need_phone_number ? ", needs phone number" : "")
                        << (invoice.need_email_address ? ", needs email address" : "")
                        << (invoice.need_shipping_address ? ", needs shipping address" : "")
                        << (invoice.send_phone_number_to_provider ? ", sends phone number to provider" : "")
                        << (invoice.send_email_address_to_provider ? ", sends email address to provider" : "") << " in "
                        << invoice.currency << " with price parts " << format::as_array(invoice.price_parts)
                        << " and suggested tip amounts " << invoice.suggested_tip_amounts << " up to "
                        << invoice.max_tip_amount << "]";
}

bool operator==(const InputInvoice &lhs, const InputInvoice &rhs) {
  return lhs.title == rhs.title && lhs.description == rhs.description && lhs.photo == rhs.photo &&
         lhs.start_parameter == rhs.start_parameter && lhs.invoice == rhs.invoice &&
         lhs.total_amount == rhs.total_amount && lhs.receipt_message_id == rhs.receipt_message_id &&
         lhs.payload == rhs.payload && lhs.provider_token == rhs.provider_token &&
         lhs.provider_data == rhs.provider_data;
}

bool operator!=(const InputInvoice &lhs, const InputInvoice &rhs) {
  return !(lhs == rhs);
}

InputInvoice get_input_invoice(tl_object_ptr<telegram_api::messageMediaInvoice> &&message_invoice, Td *td,
                               DialogId owner_dialog_id) {
  InputInvoice result;
  result.title = std::move(message_invoice->title_);
  result.description = std::move(message_invoice->description_);
  result.photo = get_web_document_photo(td->file_manager_.get(), std::move(message_invoice->photo_), owner_dialog_id);
  result.start_parameter = std::move(message_invoice->start_param_);
  result.invoice.currency = std::move(message_invoice->currency_);
  result.invoice.is_test = message_invoice->test_;
  result.invoice.need_shipping_address = message_invoice->shipping_address_requested_;
  // result.payload = string();
  // result.provider_token = string();
  // result.provider_data = string();
  result.total_amount = message_invoice->total_amount_;
  if ((message_invoice->flags_ & telegram_api::messageMediaInvoice::RECEIPT_MSG_ID_MASK) != 0) {
    result.receipt_message_id = MessageId(ServerMessageId(message_invoice->receipt_msg_id_));
    if (!result.receipt_message_id.is_valid()) {
      LOG(ERROR) << "Receive as receipt message " << result.receipt_message_id << " in " << owner_dialog_id;
      result.receipt_message_id = MessageId();
    }
  }
  return result;
}

InputInvoice get_input_invoice(tl_object_ptr<telegram_api::botInlineMessageMediaInvoice> &&message_invoice, Td *td,
                               DialogId owner_dialog_id) {
  InputInvoice result;
  result.title = std::move(message_invoice->title_);
  result.description = std::move(message_invoice->description_);
  result.photo = get_web_document_photo(td->file_manager_.get(), std::move(message_invoice->photo_), owner_dialog_id);
  // result.start_parameter = string();
  result.invoice.currency = std::move(message_invoice->currency_);
  result.invoice.is_test = message_invoice->test_;
  result.invoice.need_shipping_address = message_invoice->shipping_address_requested_;
  // result.payload = string();
  // result.provider_token = string();
  // result.provider_data = string();
  result.total_amount = message_invoice->total_amount_;
  // result.receipt_message_id = MessageId();
  return result;
}

Result<InputInvoice> process_input_message_invoice(
    td_api::object_ptr<td_api::InputMessageContent> &&input_message_content, Td *td) {
  CHECK(input_message_content != nullptr);
  CHECK(input_message_content->get_id() == td_api::inputMessageInvoice::ID);
  auto input_invoice = move_tl_object_as<td_api::inputMessageInvoice>(input_message_content);
  if (input_invoice->invoice_ == nullptr) {
    return Status::Error(400, "Invoice must be non-empty");
  }

  if (!clean_input_string(input_invoice->title_)) {
    return Status::Error(400, "Invoice title must be encoded in UTF-8");
  }
  if (!clean_input_string(input_invoice->description_)) {
    return Status::Error(400, "Invoice description must be encoded in UTF-8");
  }
  if (!clean_input_string(input_invoice->photo_url_)) {
    return Status::Error(400, "Invoice photo URL must be encoded in UTF-8");
  }
  if (!clean_input_string(input_invoice->start_parameter_)) {
    return Status::Error(400, "Invoice bot start parameter must be encoded in UTF-8");
  }
  if (!clean_input_string(input_invoice->provider_token_)) {
    return Status::Error(400, "Invoice provider token must be encoded in UTF-8");
  }
  if (!clean_input_string(input_invoice->provider_data_)) {
    return Status::Error(400, "Invoice provider data must be encoded in UTF-8");
  }
  if (!clean_input_string(input_invoice->invoice_->currency_)) {
    return Status::Error(400, "Invoice currency must be encoded in UTF-8");
  }

  InputInvoice result;
  result.title = std::move(input_invoice->title_);
  result.description = std::move(input_invoice->description_);

  auto r_http_url = parse_url(input_invoice->photo_url_);
  if (r_http_url.is_error()) {
    if (!input_invoice->photo_url_.empty()) {
      LOG(INFO) << "Can't register url " << input_invoice->photo_url_;
    }
  } else {
    auto url = r_http_url.ok().get_url();
    auto r_invoice_file_id = td->file_manager_->from_persistent_id(url, FileType::Temp);
    if (r_invoice_file_id.is_error()) {
      LOG(INFO) << "Can't register url " << url;
    } else {
      auto invoice_file_id = r_invoice_file_id.move_as_ok();

      PhotoSize s;
      s.type = 'n';
      s.dimensions =
          get_dimensions(input_invoice->photo_width_, input_invoice->photo_height_, "process_input_message_invoice");
      s.size = input_invoice->photo_size_;  // TODO use invoice_file_id size
      s.file_id = invoice_file_id;

      result.photo.id = 0;
      result.photo.photos.push_back(s);
    }
  }
  result.start_parameter = std::move(input_invoice->start_parameter_);

  result.invoice.currency = std::move(input_invoice->invoice_->currency_);
  result.invoice.price_parts.reserve(input_invoice->invoice_->price_parts_.size());
  int64 total_amount = 0;
  const int64 MAX_AMOUNT = 9999'9999'9999;
  for (auto &price : input_invoice->invoice_->price_parts_) {
    if (!clean_input_string(price->label_)) {
      return Status::Error(400, "Invoice price label must be encoded in UTF-8");
    }
    result.invoice.price_parts.emplace_back(std::move(price->label_), price->amount_);
    if (price->amount_ < -MAX_AMOUNT || price->amount_ > MAX_AMOUNT) {
      return Status::Error(400, "Too big amount of the currency specified");
    }
    total_amount += price->amount_;
  }
  if (total_amount <= 0) {
    return Status::Error(400, "Total price must be positive");
  }
  if (total_amount > MAX_AMOUNT) {
    return Status::Error(400, "Total price is too big");
  }
  result.total_amount = total_amount;

  if (input_invoice->invoice_->max_tip_amount_ < 0 || input_invoice->invoice_->max_tip_amount_ > MAX_AMOUNT) {
    return Status::Error(400, "Invalid max_tip_amount of the currency specified");
  }
  for (auto tip_amount : input_invoice->invoice_->suggested_tip_amounts_) {
    if (tip_amount <= 0) {
      return Status::Error(400, "Suggested tip amount must be positive");
    }
    if (tip_amount > input_invoice->invoice_->max_tip_amount_) {
      return Status::Error(400, "Suggested tip amount can't be bigger than max_tip_amount");
    }
  }
  if (input_invoice->invoice_->suggested_tip_amounts_.size() > 4) {
    return Status::Error(400, "There can be at most 4 suggested tip amounts");
  }

  result.invoice.max_tip_amount = input_invoice->invoice_->max_tip_amount_;
  result.invoice.suggested_tip_amounts = std::move(input_invoice->invoice_->suggested_tip_amounts_);
  result.invoice.is_test = input_invoice->invoice_->is_test_;
  result.invoice.need_name = input_invoice->invoice_->need_name_;
  result.invoice.need_phone_number = input_invoice->invoice_->need_phone_number_;
  result.invoice.need_email_address = input_invoice->invoice_->need_email_address_;
  result.invoice.need_shipping_address = input_invoice->invoice_->need_shipping_address_;
  result.invoice.send_phone_number_to_provider = input_invoice->invoice_->send_phone_number_to_provider_;
  result.invoice.send_email_address_to_provider = input_invoice->invoice_->send_email_address_to_provider_;
  result.invoice.is_flexible = input_invoice->invoice_->is_flexible_;
  if (result.invoice.send_phone_number_to_provider) {
    result.invoice.need_phone_number = true;
  }
  if (result.invoice.send_email_address_to_provider) {
    result.invoice.need_email_address = true;
  }
  if (result.invoice.is_flexible) {
    result.invoice.need_shipping_address = true;
  }

  result.payload = std::move(input_invoice->payload_);
  result.provider_token = std::move(input_invoice->provider_token_);
  result.provider_data = std::move(input_invoice->provider_data_);
  return result;
}

tl_object_ptr<td_api::messageInvoice> get_message_invoice_object(const InputInvoice &input_invoice, Td *td) {
  return make_tl_object<td_api::messageInvoice>(
      input_invoice.title, input_invoice.description, get_photo_object(td->file_manager_.get(), input_invoice.photo),
      input_invoice.invoice.currency, input_invoice.total_amount, input_invoice.start_parameter,
      input_invoice.invoice.is_test, input_invoice.invoice.need_shipping_address,
      input_invoice.receipt_message_id.get());
}

static tl_object_ptr<telegram_api::invoice> get_input_invoice(const Invoice &invoice) {
  int32 flags = 0;
  if (invoice.is_test) {
    flags |= telegram_api::invoice::TEST_MASK;
  }
  if (invoice.need_name) {
    flags |= telegram_api::invoice::NAME_REQUESTED_MASK;
  }
  if (invoice.need_phone_number) {
    flags |= telegram_api::invoice::PHONE_REQUESTED_MASK;
  }
  if (invoice.need_email_address) {
    flags |= telegram_api::invoice::EMAIL_REQUESTED_MASK;
  }
  if (invoice.need_shipping_address) {
    flags |= telegram_api::invoice::SHIPPING_ADDRESS_REQUESTED_MASK;
  }
  if (invoice.send_phone_number_to_provider) {
    flags |= telegram_api::invoice::PHONE_TO_PROVIDER_MASK;
  }
  if (invoice.send_email_address_to_provider) {
    flags |= telegram_api::invoice::EMAIL_TO_PROVIDER_MASK;
  }
  if (invoice.is_flexible) {
    flags |= telegram_api::invoice::FLEXIBLE_MASK;
  }
  if (invoice.max_tip_amount != 0) {
    flags |= telegram_api::invoice::MAX_TIP_AMOUNT_MASK;
  }

  auto prices = transform(invoice.price_parts, [](const LabeledPricePart &price) {
    return telegram_api::make_object<telegram_api::labeledPrice>(price.label, price.amount);
  });
  return make_tl_object<telegram_api::invoice>(
      flags, false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/,
      false /*ignored*/, false /*ignored*/, false /*ignored*/, invoice.currency, std::move(prices),
      invoice.max_tip_amount, vector<int64>(invoice.suggested_tip_amounts));
}

static tl_object_ptr<telegram_api::inputWebDocument> get_input_web_document(const FileManager *file_manager,
                                                                            const Photo &photo) {
  if (photo.is_empty()) {
    return nullptr;
  }

  CHECK(photo.photos.size() == 1);
  const PhotoSize &size = photo.photos[0];
  CHECK(size.file_id.is_valid());

  vector<tl_object_ptr<telegram_api::DocumentAttribute>> attributes;
  if (size.dimensions.width != 0 && size.dimensions.height != 0) {
    attributes.push_back(
        make_tl_object<telegram_api::documentAttributeImageSize>(size.dimensions.width, size.dimensions.height));
  }

  auto file_view = file_manager->get_file_view(size.file_id);
  CHECK(file_view.has_url());

  auto file_name = get_url_file_name(file_view.url());
  return make_tl_object<telegram_api::inputWebDocument>(
      file_view.url(), size.size, MimeType::from_extension(PathView(file_name).extension(), "image/jpeg"),
      std::move(attributes));
}

tl_object_ptr<telegram_api::inputMediaInvoice> get_input_media_invoice(const InputInvoice &input_invoice, Td *td) {
  int32 flags = 0;
  if (!input_invoice.start_parameter.empty()) {
    flags |= telegram_api::inputMediaInvoice::START_PARAM_MASK;
  }
  auto input_web_document = get_input_web_document(td->file_manager_.get(), input_invoice.photo);
  if (input_web_document != nullptr) {
    flags |= telegram_api::inputMediaInvoice::PHOTO_MASK;
  }

  return make_tl_object<telegram_api::inputMediaInvoice>(
      flags, input_invoice.title, input_invoice.description, std::move(input_web_document),
      get_input_invoice(input_invoice.invoice), BufferSlice(input_invoice.payload), input_invoice.provider_token,
      telegram_api::make_object<telegram_api::dataJSON>(
          input_invoice.provider_data.empty() ? "null" : input_invoice.provider_data),
      input_invoice.start_parameter);
}

tl_object_ptr<telegram_api::inputBotInlineMessageMediaInvoice> get_input_bot_inline_message_media_invoice(
    const InputInvoice &input_invoice, tl_object_ptr<telegram_api::ReplyMarkup> &&reply_markup, Td *td) {
  int32 flags = 0;
  if (reply_markup != nullptr) {
    flags |= telegram_api::inputBotInlineMessageMediaInvoice::REPLY_MARKUP_MASK;
  }
  auto input_web_document = get_input_web_document(td->file_manager_.get(), input_invoice.photo);
  if (input_web_document != nullptr) {
    flags |= telegram_api::inputBotInlineMessageMediaInvoice::PHOTO_MASK;
  }
  return make_tl_object<telegram_api::inputBotInlineMessageMediaInvoice>(
      flags, input_invoice.title, input_invoice.description, std::move(input_web_document),
      get_input_invoice(input_invoice.invoice), BufferSlice(input_invoice.payload), input_invoice.provider_token,
      telegram_api::make_object<telegram_api::dataJSON>(
          input_invoice.provider_data.empty() ? "null" : input_invoice.provider_data),
      std::move(reply_markup));
}

vector<FileId> get_input_invoice_file_ids(const InputInvoice &input_invoice) {
  return photo_get_file_ids(input_invoice.photo);
}

bool operator==(const Address &lhs, const Address &rhs) {
  return lhs.country_code == rhs.country_code && lhs.state == rhs.state && lhs.city == rhs.city &&
         lhs.street_line1 == rhs.street_line1 && lhs.street_line2 == rhs.street_line2 &&
         lhs.postal_code == rhs.postal_code;
}

bool operator!=(const Address &lhs, const Address &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const Address &address) {
  return string_builder << "[Address " << tag("country_code", address.country_code) << tag("state", address.state)
                        << tag("city", address.city) << tag("street_line1", address.street_line1)
                        << tag("street_line2", address.street_line2) << tag("postal_code", address.postal_code) << "]";
}

unique_ptr<Address> get_address(tl_object_ptr<telegram_api::postAddress> &&address) {
  if (address == nullptr) {
    return nullptr;
  }
  return td::make_unique<Address>(std::move(address->country_iso2_), std::move(address->state_),
                                  std::move(address->city_), std::move(address->street_line1_),
                                  std::move(address->street_line2_), std::move(address->post_code_));
}

static bool is_capital_alpha(char c) {
  return 'A' <= c && c <= 'Z';
}

Status check_country_code(string &country_code) {
  if (!clean_input_string(country_code)) {
    return Status::Error(400, "Country code must be encoded in UTF-8");
  }
  if (country_code.size() != 2 || !is_capital_alpha(country_code[0]) || !is_capital_alpha(country_code[1])) {
    return Status::Error(400, "Wrong country code specified");
  }
  return Status::OK();
}

static Status check_state(string &state) {
  if (!clean_input_string(state)) {
    return Status::Error(400, "State must be encoded in UTF-8");
  }
  return Status::OK();
}

static Status check_city(string &city) {
  if (!clean_input_string(city)) {
    return Status::Error(400, "City must be encoded in UTF-8");
  }
  return Status::OK();
}

static Status check_street_line(string &street_line) {
  if (!clean_input_string(street_line)) {
    return Status::Error(400, "Street line must be encoded in UTF-8");
  }
  return Status::OK();
}

static Status check_postal_code(string &postal_code) {
  if (!clean_input_string(postal_code)) {
    return Status::Error(400, "Postal code must be encoded in UTF-8");
  }
  return Status::OK();
}

Result<Address> get_address(td_api::object_ptr<td_api::address> &&address) {
  if (address == nullptr) {
    return Status::Error(400, "Address must be non-empty");
  }
  TRY_STATUS(check_country_code(address->country_code_));
  TRY_STATUS(check_state(address->state_));
  TRY_STATUS(check_city(address->city_));
  TRY_STATUS(check_street_line(address->street_line1_));
  TRY_STATUS(check_street_line(address->street_line2_));
  TRY_STATUS(check_postal_code(address->postal_code_));

  return Address(std::move(address->country_code_), std::move(address->state_), std::move(address->city_),
                 std::move(address->street_line1_), std::move(address->street_line2_),
                 std::move(address->postal_code_));
}

tl_object_ptr<td_api::address> get_address_object(const unique_ptr<Address> &address) {
  if (address == nullptr) {
    return nullptr;
  }
  return get_address_object(*address);
}

tl_object_ptr<td_api::address> get_address_object(const Address &address) {
  return make_tl_object<td_api::address>(address.country_code, address.state, address.city, address.street_line1,
                                         address.street_line2, address.postal_code);
}

string address_to_json(const Address &address) {
  return json_encode<std::string>(json_object([&](auto &o) {
    o("country_code", address.country_code);
    o("state", address.state);
    o("city", address.city);
    o("street_line1", address.street_line1);
    o("street_line2", address.street_line2);
    o("post_code", address.postal_code);
  }));
}

Result<Address> address_from_json(Slice json) {
  auto json_copy = json.str();
  auto r_value = json_decode(json_copy);
  if (r_value.is_error()) {
    return Status::Error(400, "Can't parse address JSON object");
  }

  auto value = r_value.move_as_ok();
  if (value.type() != JsonValue::Type::Object) {
    return Status::Error(400, "Address must be an Object");
  }

  auto &object = value.get_object();
  TRY_RESULT(country_code, get_json_object_string_field(object, "country_code", true));
  TRY_RESULT(state, get_json_object_string_field(object, "state", true));
  TRY_RESULT(city, get_json_object_string_field(object, "city", true));
  TRY_RESULT(street_line1, get_json_object_string_field(object, "street_line1", true));
  TRY_RESULT(street_line2, get_json_object_string_field(object, "street_line2", true));
  TRY_RESULT(postal_code, get_json_object_string_field(object, "post_code", true));

  TRY_STATUS(check_country_code(country_code));
  TRY_STATUS(check_state(state));
  TRY_STATUS(check_city(city));
  TRY_STATUS(check_street_line(street_line1));
  TRY_STATUS(check_street_line(street_line2));
  TRY_STATUS(check_postal_code(postal_code));

  return Address(std::move(country_code), std::move(state), std::move(city), std::move(street_line1),
                 std::move(street_line2), std::move(postal_code));
}

bool operator==(const OrderInfo &lhs, const OrderInfo &rhs) {
  return lhs.name == rhs.name && lhs.phone_number == rhs.phone_number && lhs.email_address == rhs.email_address &&
         ((lhs.shipping_address == nullptr && rhs.shipping_address == nullptr) ||
          (lhs.shipping_address != nullptr && rhs.shipping_address != nullptr &&
           *lhs.shipping_address == *rhs.shipping_address));
}

bool operator!=(const OrderInfo &lhs, const OrderInfo &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const OrderInfo &order_info) {
  string_builder << "[OrderInfo " << tag("name", order_info.name) << tag("phone_number", order_info.phone_number)
                 << tag("email_address", order_info.email_address);
  if (order_info.shipping_address != nullptr) {
    string_builder << *order_info.shipping_address;
  }
  return string_builder << "]";
}

unique_ptr<OrderInfo> get_order_info(tl_object_ptr<telegram_api::paymentRequestedInfo> order_info) {
  if (order_info == nullptr || order_info->flags_ == 0) {
    return nullptr;
  }
  return td::make_unique<OrderInfo>(std::move(order_info->name_), std::move(order_info->phone_),
                                    std::move(order_info->email_),
                                    get_address(std::move(order_info->shipping_address_)));
}

tl_object_ptr<td_api::orderInfo> get_order_info_object(const unique_ptr<OrderInfo> &order_info) {
  if (order_info == nullptr) {
    return nullptr;
  }
  return make_tl_object<td_api::orderInfo>(order_info->name, order_info->phone_number, order_info->email_address,
                                           get_address_object(order_info->shipping_address));
}

bool operator==(const ShippingOption &lhs, const ShippingOption &rhs) {
  return lhs.id == rhs.id && lhs.title == rhs.title && lhs.price_parts == rhs.price_parts;
}

bool operator!=(const ShippingOption &lhs, const ShippingOption &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const ShippingOption &shipping_option) {
  return string_builder << "[ShippingOption " << shipping_option.id << " " << shipping_option.title
                        << " with price parts " << format::as_array(shipping_option.price_parts) << "]";
}

void answer_shipping_query(Td *td, int64 shipping_query_id,
                           vector<tl_object_ptr<td_api::shippingOption>> &&shipping_options,
                           const string &error_message, Promise<Unit> &&promise) {
  vector<tl_object_ptr<telegram_api::shippingOption>> options;
  for (auto &option : shipping_options) {
    if (option == nullptr) {
      return promise.set_error(Status::Error(400, "Shipping option must be non-empty"));
    }
    if (!clean_input_string(option->id_)) {
      return promise.set_error(Status::Error(400, "Shipping option identifier must be encoded in UTF-8"));
    }
    if (!clean_input_string(option->title_)) {
      return promise.set_error(Status::Error(400, "Shipping option title must be encoded in UTF-8"));
    }

    vector<tl_object_ptr<telegram_api::labeledPrice>> prices;
    for (auto &price_part : option->price_parts_) {
      if (price_part == nullptr) {
        return promise.set_error(Status::Error(400, "Shipping option price part must be non-empty"));
      }
      if (!clean_input_string(price_part->label_)) {
        return promise.set_error(Status::Error(400, "Shipping option price part label must be encoded in UTF-8"));
      }

      prices.push_back(make_tl_object<telegram_api::labeledPrice>(std::move(price_part->label_), price_part->amount_));
    }

    options.push_back(make_tl_object<telegram_api::shippingOption>(std::move(option->id_), std::move(option->title_),
                                                                   std::move(prices)));
  }

  td->create_handler<SetBotShippingAnswerQuery>(std::move(promise))
      ->send(shipping_query_id, error_message, std::move(options));
}

void answer_pre_checkout_query(Td *td, int64 pre_checkout_query_id, const string &error_message,
                               Promise<Unit> &&promise) {
  td->create_handler<SetBotPreCheckoutAnswerQuery>(std::move(promise))->send(pre_checkout_query_id, error_message);
}

void get_payment_form(Td *td, FullMessageId full_message_id, const td_api::object_ptr<td_api::paymentFormTheme> &theme,
                      Promise<tl_object_ptr<td_api::paymentForm>> &&promise) {
  TRY_RESULT_PROMISE(promise, server_message_id, td->messages_manager_->get_invoice_message_id(full_message_id));

  tl_object_ptr<telegram_api::dataJSON> theme_parameters;
  if (theme != nullptr) {
    theme_parameters = make_tl_object<telegram_api::dataJSON>(string());
    theme_parameters->data_ = json_encode<string>(json_object([&theme](auto &o) {
      auto get_color = [](int32 color) {
        return static_cast<int64>(static_cast<uint32>(color) | 0x000000FF);
      };
      o("bg_color", get_color(theme->background_color_));
      o("text_color", get_color(theme->text_color_));
      o("hint_color", get_color(theme->hint_color_));
      o("link_color", get_color(theme->link_color_));
      o("button_color", get_color(theme->button_color_));
      o("button_text_color", get_color(theme->button_text_color_));
    }));
  }
  td->create_handler<GetPaymentFormQuery>(std::move(promise))
      ->send(full_message_id.get_dialog_id(), server_message_id, std::move(theme_parameters));
}

void validate_order_info(Td *td, FullMessageId full_message_id, tl_object_ptr<td_api::orderInfo> order_info,
                         bool allow_save, Promise<tl_object_ptr<td_api::validatedOrderInfo>> &&promise) {
  TRY_RESULT_PROMISE(promise, server_message_id, td->messages_manager_->get_invoice_message_id(full_message_id));

  if (order_info != nullptr) {
    if (!clean_input_string(order_info->name_)) {
      return promise.set_error(Status::Error(400, "Name must be encoded in UTF-8"));
    }
    if (!clean_input_string(order_info->phone_number_)) {
      return promise.set_error(Status::Error(400, "Phone number must be encoded in UTF-8"));
    }
    if (!clean_input_string(order_info->email_address_)) {
      return promise.set_error(Status::Error(400, "Email address must be encoded in UTF-8"));
    }
    if (order_info->shipping_address_ != nullptr) {
      if (!clean_input_string(order_info->shipping_address_->country_code_)) {
        return promise.set_error(Status::Error(400, "Country code must be encoded in UTF-8"));
      }
      if (!clean_input_string(order_info->shipping_address_->state_)) {
        return promise.set_error(Status::Error(400, "State must be encoded in UTF-8"));
      }
      if (!clean_input_string(order_info->shipping_address_->city_)) {
        return promise.set_error(Status::Error(400, "City must be encoded in UTF-8"));
      }
      if (!clean_input_string(order_info->shipping_address_->street_line1_)) {
        return promise.set_error(Status::Error(400, "Street address must be encoded in UTF-8"));
      }
      if (!clean_input_string(order_info->shipping_address_->street_line2_)) {
        return promise.set_error(Status::Error(400, "Street address must be encoded in UTF-8"));
      }
      if (!clean_input_string(order_info->shipping_address_->postal_code_)) {
        return promise.set_error(Status::Error(400, "Postal code must be encoded in UTF-8"));
      }
    }
  }

  td->create_handler<ValidateRequestedInfoQuery>(std::move(promise))
      ->send(full_message_id.get_dialog_id(), server_message_id, convert_order_info(std::move(order_info)), allow_save);
}

void send_payment_form(Td *td, FullMessageId full_message_id, int64 payment_form_id, const string &order_info_id,
                       const string &shipping_option_id, const tl_object_ptr<td_api::InputCredentials> &credentials,
                       int64 tip_amount, Promise<tl_object_ptr<td_api::paymentResult>> &&promise) {
  TRY_RESULT_PROMISE(promise, server_message_id, td->messages_manager_->get_invoice_message_id(full_message_id));

  if (credentials == nullptr) {
    return promise.set_error(Status::Error(400, "Input payment credentials must be non-empty"));
  }

  tl_object_ptr<telegram_api::InputPaymentCredentials> input_credentials;
  switch (credentials->get_id()) {
    case td_api::inputCredentialsSaved::ID: {
      auto credentials_saved = static_cast<const td_api::inputCredentialsSaved *>(credentials.get());
      auto credentials_id = credentials_saved->saved_credentials_id_;
      if (!clean_input_string(credentials_id)) {
        return promise.set_error(Status::Error(400, "Credentials identifier must be encoded in UTF-8"));
      }
      auto temp_password_state = PasswordManager::get_temp_password_state_sync();
      if (!temp_password_state.has_temp_password) {
        return promise.set_error(Status::Error(400, "Temporary password required to use saved credentials"));
      }

      input_credentials = make_tl_object<telegram_api::inputPaymentCredentialsSaved>(
          std::move(credentials_id), BufferSlice(temp_password_state.temp_password));
      break;
    }
    case td_api::inputCredentialsNew::ID: {
      auto credentials_new = static_cast<const td_api::inputCredentialsNew *>(credentials.get());
      int32 flags = 0;
      if (credentials_new->allow_save_) {
        flags |= telegram_api::inputPaymentCredentials::SAVE_MASK;
      }

      input_credentials = make_tl_object<telegram_api::inputPaymentCredentials>(
          flags, false /*ignored*/, make_tl_object<telegram_api::dataJSON>(credentials_new->data_));
      break;
    }
    case td_api::inputCredentialsGooglePay::ID: {
      auto credentials_google_pay = static_cast<const td_api::inputCredentialsGooglePay *>(credentials.get());
      input_credentials = make_tl_object<telegram_api::inputPaymentCredentialsGooglePay>(
          make_tl_object<telegram_api::dataJSON>(credentials_google_pay->data_));
      break;
    }
    case td_api::inputCredentialsApplePay::ID: {
      auto credentials_apple_pay = static_cast<const td_api::inputCredentialsApplePay *>(credentials.get());
      input_credentials = make_tl_object<telegram_api::inputPaymentCredentialsApplePay>(
          make_tl_object<telegram_api::dataJSON>(credentials_apple_pay->data_));
      break;
    }
    default:
      UNREACHABLE();
  }

  td->create_handler<SendPaymentFormQuery>(std::move(promise))
      ->send(full_message_id.get_dialog_id(), server_message_id, payment_form_id, order_info_id, shipping_option_id,
             std::move(input_credentials), tip_amount);
}

void get_payment_receipt(Td *td, FullMessageId full_message_id,
                         Promise<tl_object_ptr<td_api::paymentReceipt>> &&promise) {
  TRY_RESULT_PROMISE(promise, server_message_id,
                     td->messages_manager_->get_payment_successful_message_id(full_message_id));
  td->create_handler<GetPaymentReceiptQuery>(std::move(promise))
      ->send(full_message_id.get_dialog_id(), server_message_id);
}

void get_saved_order_info(Td *td, Promise<tl_object_ptr<td_api::orderInfo>> &&promise) {
  td->create_handler<GetSavedInfoQuery>(std::move(promise))->send();
}

void delete_saved_order_info(Td *td, Promise<Unit> &&promise) {
  td->create_handler<ClearSavedInfoQuery>(std::move(promise))->send(false, true);
}

void delete_saved_credentials(Td *td, Promise<Unit> &&promise) {
  td->create_handler<ClearSavedInfoQuery>(std::move(promise))->send(true, false);
}

void get_bank_card_info(Td *td, const string &bank_card_number,
                        Promise<td_api::object_ptr<td_api::bankCardInfo>> &&promise) {
  td->create_handler<GetBankCardInfoQuery>(std::move(promise))->send(bank_card_number);
}

}  // namespace td
