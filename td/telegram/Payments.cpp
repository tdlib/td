//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/Payments.h"

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/telegram/ContactsManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/misc.h"
#include "td/telegram/PasswordManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/UpdatesManager.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/format.h"
#include "td/utils/JsonBuilder.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Status.h"

namespace td {

class SetBotShippingAnswerQuery : public Td::ResultHandler {
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

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_setBotShippingResults>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    bool result = result_ptr.ok();
    if (!result) {
      LOG(INFO) << "Sending answer to a shipping query has failed";
    }
    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class SetBotPreCheckoutAnswerQuery : public Td::ResultHandler {
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

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_setBotPrecheckoutResults>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    bool result = result_ptr.ok();
    if (!result) {
      LOG(INFO) << "Sending answer to a pre-checkout query has failed";
    }
    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
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

  return make_tl_object<td_api::invoice>(std::move(invoice->currency_), std::move(labeled_prices), is_test, need_name,
                                         need_phone_number, need_email_address, need_shipping_address,
                                         send_phone_number_to_provider, send_email_address_to_provider, is_flexible);
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

    if (value.get_object().size() != 4 || r_need_country.is_error() || r_need_postal_code.is_error() ||
        r_need_cardholder_name.is_error() || r_publishable_key.is_error()) {
      LOG(WARNING) << "Unsupported JSON data \"" << native_parameters->data_ << '"';
      return nullptr;
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
  return make_tl_object<telegram_api::postAddress>(std::move(address->country_code_), std::move(address->state_),
                                                   std::move(address->city_), std::move(address->street_line1_),
                                                   std::move(address->street_line2_), std::move(address->postal_code_));
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

class GetPaymentFormQuery : public Td::ResultHandler {
  Promise<tl_object_ptr<td_api::paymentForm>> promise_;

 public:
  explicit GetPaymentFormQuery(Promise<tl_object_ptr<td_api::paymentForm>> &&promise) : promise_(std::move(promise)) {
  }

  void send(ServerMessageId server_message_id) {
    send_query(G()->net_query_creator().create(telegram_api::payments_getPaymentForm(server_message_id.get())));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::payments_getPaymentForm>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto payment_form = result_ptr.move_as_ok();
    LOG(INFO) << "Receive payment form: " << to_string(payment_form);

    td->contacts_manager_->on_get_users(std::move(payment_form->users_), "GetPaymentFormQuery");

    bool can_save_credentials =
        (payment_form->flags_ & telegram_api::payments_paymentForm::CAN_SAVE_CREDENTIALS_MASK) != 0;
    bool need_password = (payment_form->flags_ & telegram_api::payments_paymentForm::PASSWORD_MISSING_MASK) != 0;
    promise_.set_value(make_tl_object<td_api::paymentForm>(
        convert_invoice(std::move(payment_form->invoice_)), std::move(payment_form->url_),
        convert_payment_provider(payment_form->native_provider_, std::move(payment_form->native_params_)),
        convert_order_info(std::move(payment_form->saved_info_)),
        convert_saved_credentials(std::move(payment_form->saved_credentials_)), can_save_credentials, need_password));
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class ValidateRequestedInfoQuery : public Td::ResultHandler {
  Promise<tl_object_ptr<td_api::validatedOrderInfo>> promise_;

 public:
  explicit ValidateRequestedInfoQuery(Promise<tl_object_ptr<td_api::validatedOrderInfo>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(ServerMessageId server_message_id, tl_object_ptr<telegram_api::paymentRequestedInfo> requested_info,
            bool allow_save) {
    int32 flags = 0;
    if (allow_save) {
      flags |= telegram_api::payments_validateRequestedInfo::SAVE_MASK;
    }
    if (requested_info == nullptr) {
      requested_info = make_tl_object<telegram_api::paymentRequestedInfo>();
      requested_info->flags_ = 0;
    }
    send_query(G()->net_query_creator().create(telegram_api::payments_validateRequestedInfo(
        flags, false /*ignored*/, server_message_id.get(), std::move(requested_info))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::payments_validateRequestedInfo>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto validated_order_info = result_ptr.move_as_ok();
    LOG(INFO) << "Receive validated order info: " << to_string(validated_order_info);

    promise_.set_value(make_tl_object<td_api::validatedOrderInfo>(
        std::move(validated_order_info->id_),
        transform(std::move(validated_order_info->shipping_options_), convert_shipping_option)));
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class SendPaymentFormQuery : public Td::ResultHandler {
  Promise<tl_object_ptr<td_api::paymentResult>> promise_;

 public:
  explicit SendPaymentFormQuery(Promise<tl_object_ptr<td_api::paymentResult>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(ServerMessageId server_message_id, const string &order_info_id, const string &shipping_option_id,
            tl_object_ptr<telegram_api::InputPaymentCredentials> input_credentials) {
    CHECK(input_credentials != nullptr);
    int32 flags = 0;
    if (!order_info_id.empty()) {
      flags |= telegram_api::payments_sendPaymentForm::REQUESTED_INFO_ID_MASK;
    }
    if (!shipping_option_id.empty()) {
      flags |= telegram_api::payments_sendPaymentForm::SHIPPING_OPTION_ID_MASK;
    }
    send_query(G()->net_query_creator().create(telegram_api::payments_sendPaymentForm(
        flags, server_message_id.get(), order_info_id, shipping_option_id, std::move(input_credentials))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::payments_sendPaymentForm>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto payment_result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive payment result: " << to_string(payment_result);

    switch (payment_result->get_id()) {
      case telegram_api::payments_paymentResult::ID: {
        auto result = move_tl_object_as<telegram_api::payments_paymentResult>(payment_result);
        G()->td().get_actor_unsafe()->updates_manager_->on_get_updates(std::move(result->updates_));
        promise_.set_value(make_tl_object<td_api::paymentResult>(true, string()));
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

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class GetPaymentReceiptQuery : public Td::ResultHandler {
  Promise<tl_object_ptr<td_api::paymentReceipt>> promise_;

 public:
  explicit GetPaymentReceiptQuery(Promise<tl_object_ptr<td_api::paymentReceipt>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(ServerMessageId server_message_id) {
    send_query(G()->net_query_creator().create(telegram_api::payments_getPaymentReceipt(server_message_id.get())));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::payments_getPaymentReceipt>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto payment_receipt = result_ptr.move_as_ok();
    LOG(INFO) << "Receive payment receipt: " << to_string(payment_receipt);

    td->contacts_manager_->on_get_users(std::move(payment_receipt->users_), "GetPaymentReceiptQuery");

    UserId payments_provider_user_id(payment_receipt->provider_id_);
    if (!payments_provider_user_id.is_valid()) {
      LOG(ERROR) << "Receive invalid payments provider " << payments_provider_user_id;
      payments_provider_user_id = UserId();
    }

    promise_.set_value(make_tl_object<td_api::paymentReceipt>(
        payment_receipt->date_,
        G()->td().get_actor_unsafe()->contacts_manager_->get_user_id_object(payments_provider_user_id,
                                                                            "paymentReceipt"),
        convert_invoice(std::move(payment_receipt->invoice_)), convert_order_info(std::move(payment_receipt->info_)),
        convert_shipping_option(std::move(payment_receipt->shipping_)),
        std::move(payment_receipt->credentials_title_)));
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class GetSavedInfoQuery : public Td::ResultHandler {
  Promise<tl_object_ptr<td_api::orderInfo>> promise_;

 public:
  explicit GetSavedInfoQuery(Promise<tl_object_ptr<td_api::orderInfo>> &&promise) : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create(telegram_api::payments_getSavedInfo()));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::payments_getSavedInfo>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto saved_info = result_ptr.move_as_ok();
    LOG(INFO) << "Receive saved info: " << to_string(saved_info);
    promise_.set_value(convert_order_info(std::move(saved_info->saved_info_)));
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class ClearSavedInfoQuery : public Td::ResultHandler {
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

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::payments_clearSavedInfo>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class GetBankCardInfoQuery : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::bankCardInfo>> promise_;

 public:
  explicit GetBankCardInfoQuery(Promise<td_api::object_ptr<td_api::bankCardInfo>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(const string &bank_card_number) {
    send_query(G()->net_query_creator().create(telegram_api::payments_getBankCardData(bank_card_number),
                                               G()->get_webfile_dc_id()));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::payments_getBankCardData>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto response = result_ptr.move_as_ok();
    auto actions = transform(response->open_urls_, [](auto &open_url) {
      return td_api::make_object<td_api::bankCardActionOpenUrl>(open_url->name_, open_url->url_);
    });
    promise_.set_value(td_api::make_object<td_api::bankCardInfo>(response->title_, std::move(actions)));
  }

  void on_error(uint64 id, Status status) override {
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
         lhs.is_flexible == rhs.is_flexible && lhs.currency == rhs.currency && lhs.price_parts == rhs.price_parts;
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
                        << invoice.currency << " with price parts " << format::as_array(invoice.price_parts) << "]";
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

void answer_shipping_query(int64 shipping_query_id, vector<tl_object_ptr<td_api::shippingOption>> &&shipping_options,
                           const string &error_message, Promise<Unit> &&promise) {
  vector<tl_object_ptr<telegram_api::shippingOption>> options;
  for (auto &option : shipping_options) {
    if (option == nullptr) {
      return promise.set_error(Status::Error(400, "Shipping option must be non-empty"));
    }
    if (!clean_input_string(option->id_)) {
      return promise.set_error(Status::Error(400, "Shipping option id must be encoded in UTF-8"));
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

  G()->td()
      .get_actor_unsafe()
      ->create_handler<SetBotShippingAnswerQuery>(std::move(promise))
      ->send(shipping_query_id, error_message, std::move(options));
}

void answer_pre_checkout_query(int64 pre_checkout_query_id, const string &error_message, Promise<Unit> &&promise) {
  G()->td()
      .get_actor_unsafe()
      ->create_handler<SetBotPreCheckoutAnswerQuery>(std::move(promise))
      ->send(pre_checkout_query_id, error_message);
}

void get_payment_form(ServerMessageId server_message_id, Promise<tl_object_ptr<td_api::paymentForm>> &&promise) {
  G()->td().get_actor_unsafe()->create_handler<GetPaymentFormQuery>(std::move(promise))->send(server_message_id);
}

void validate_order_info(ServerMessageId server_message_id, tl_object_ptr<td_api::orderInfo> order_info,
                         bool allow_save, Promise<tl_object_ptr<td_api::validatedOrderInfo>> &&promise) {
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

  G()->td()
      .get_actor_unsafe()
      ->create_handler<ValidateRequestedInfoQuery>(std::move(promise))
      ->send(server_message_id, convert_order_info(std::move(order_info)), allow_save);
}

void send_payment_form(ServerMessageId server_message_id, const string &order_info_id, const string &shipping_option_id,
                       const tl_object_ptr<td_api::InputCredentials> &credentials,
                       Promise<tl_object_ptr<td_api::paymentResult>> &&promise) {
  CHECK(credentials != nullptr);

  tl_object_ptr<telegram_api::InputPaymentCredentials> input_credentials;
  switch (credentials->get_id()) {
    case td_api::inputCredentialsSaved::ID: {
      auto credentials_saved = static_cast<const td_api::inputCredentialsSaved *>(credentials.get());
      auto credentials_id = credentials_saved->saved_credentials_id_;
      if (!clean_input_string(credentials_id)) {
        return promise.set_error(Status::Error(400, "Credentials id must be encoded in UTF-8"));
      }
      auto temp_password_state =
          G()->td().get_actor_unsafe()->password_manager_->get_actor_unsafe()->get_temp_password_state_sync();
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
    case td_api::inputCredentialsAndroidPay::ID: {
      auto credentials_android_pay = static_cast<const td_api::inputCredentialsAndroidPay *>(credentials.get());
      input_credentials = make_tl_object<telegram_api::inputPaymentCredentialsAndroidPay>(
          make_tl_object<telegram_api::dataJSON>(credentials_android_pay->data_), string());
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

  G()->td()
      .get_actor_unsafe()
      ->create_handler<SendPaymentFormQuery>(std::move(promise))
      ->send(server_message_id, order_info_id, shipping_option_id, std::move(input_credentials));
}

void get_payment_receipt(ServerMessageId server_message_id, Promise<tl_object_ptr<td_api::paymentReceipt>> &&promise) {
  G()->td().get_actor_unsafe()->create_handler<GetPaymentReceiptQuery>(std::move(promise))->send(server_message_id);
}

void get_saved_order_info(Promise<tl_object_ptr<td_api::orderInfo>> &&promise) {
  G()->td().get_actor_unsafe()->create_handler<GetSavedInfoQuery>(std::move(promise))->send();
}

void delete_saved_order_info(Promise<Unit> &&promise) {
  G()->td().get_actor_unsafe()->create_handler<ClearSavedInfoQuery>(std::move(promise))->send(false, true);
}

void delete_saved_credentials(Promise<Unit> &&promise) {
  G()->td().get_actor_unsafe()->create_handler<ClearSavedInfoQuery>(std::move(promise))->send(true, false);
}

void get_bank_card_info(const string &bank_card_number, Promise<td_api::object_ptr<td_api::bankCardInfo>> &&promise) {
  G()->td().get_actor_unsafe()->create_handler<GetBankCardInfoQuery>(std::move(promise))->send(bank_card_number);
}

}  // namespace td
