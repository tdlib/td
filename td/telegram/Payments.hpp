//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/Payments.h"

#include "td/telegram/MessageExtendedMedia.hpp"
#include "td/telegram/Photo.hpp"
#include "td/telegram/Version.h"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void store(const LabeledPricePart &labeled_price_part, StorerT &storer) {
  store(labeled_price_part.label, storer);
  store(labeled_price_part.amount, storer);
}

template <class ParserT>
void parse(LabeledPricePart &labeled_price_part, ParserT &parser) {
  parse(labeled_price_part.label, parser);
  parse(labeled_price_part.amount, parser);
}

template <class StorerT>
void store(const Invoice &invoice, StorerT &storer) {
  bool has_tip = invoice.max_tip_amount != 0;
  bool is_recurring = !invoice.recurring_payment_terms_of_service_url.empty();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(invoice.is_test);
  STORE_FLAG(invoice.need_name);
  STORE_FLAG(invoice.need_phone_number);
  STORE_FLAG(invoice.need_email_address);
  STORE_FLAG(invoice.need_shipping_address);
  STORE_FLAG(invoice.is_flexible);
  STORE_FLAG(invoice.send_phone_number_to_provider);
  STORE_FLAG(invoice.send_email_address_to_provider);
  STORE_FLAG(has_tip);
  STORE_FLAG(is_recurring);
  END_STORE_FLAGS();
  store(invoice.currency, storer);
  store(invoice.price_parts, storer);
  if (has_tip) {
    store(invoice.max_tip_amount, storer);
    store(invoice.suggested_tip_amounts, storer);
  }
  if (is_recurring) {
    store(invoice.recurring_payment_terms_of_service_url, storer);
  }
}

template <class ParserT>
void parse(Invoice &invoice, ParserT &parser) {
  bool has_tip;
  bool is_recurring;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(invoice.is_test);
  PARSE_FLAG(invoice.need_name);
  PARSE_FLAG(invoice.need_phone_number);
  PARSE_FLAG(invoice.need_email_address);
  PARSE_FLAG(invoice.need_shipping_address);
  PARSE_FLAG(invoice.is_flexible);
  PARSE_FLAG(invoice.send_phone_number_to_provider);
  PARSE_FLAG(invoice.send_email_address_to_provider);
  PARSE_FLAG(has_tip);
  PARSE_FLAG(is_recurring);
  END_PARSE_FLAGS();
  parse(invoice.currency, parser);
  parse(invoice.price_parts, parser);
  if (has_tip) {
    parse(invoice.max_tip_amount, parser);
    parse(invoice.suggested_tip_amounts, parser);
  }
  if (is_recurring) {
    parse(invoice.recurring_payment_terms_of_service_url, parser);
  }
}

template <class StorerT>
void store(const InputInvoice &input_invoice, StorerT &storer) {
  bool has_description = !input_invoice.description.empty();
  bool has_photo = !input_invoice.photo.is_empty();
  bool has_start_parameter = !input_invoice.start_parameter.empty();
  bool has_payload = !input_invoice.payload.empty();
  bool has_provider_token = !input_invoice.provider_token.empty();
  bool has_provider_data = !input_invoice.provider_data.empty();
  bool has_total_amount = input_invoice.total_amount != 0;
  bool has_receipt_message_id = input_invoice.receipt_message_id.is_valid();
  bool has_extended_media = input_invoice.extended_media.is_empty();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(has_description);
  STORE_FLAG(has_photo);
  STORE_FLAG(has_start_parameter);
  STORE_FLAG(has_payload);
  STORE_FLAG(has_provider_token);
  STORE_FLAG(has_provider_data);
  STORE_FLAG(has_total_amount);
  STORE_FLAG(has_receipt_message_id);
  STORE_FLAG(has_extended_media);
  END_STORE_FLAGS();
  store(input_invoice.title, storer);
  if (has_description) {
    store(input_invoice.description, storer);
  }
  if (has_photo) {
    store(input_invoice.photo, storer);
  }
  if (has_start_parameter) {
    store(input_invoice.start_parameter, storer);
  }
  store(input_invoice.invoice, storer);
  if (has_payload) {
    store(input_invoice.payload, storer);
  }
  if (has_provider_token) {
    store(input_invoice.provider_token, storer);
  }
  if (has_provider_data) {
    store(input_invoice.provider_data, storer);
  }
  if (has_total_amount) {
    store(input_invoice.total_amount, storer);
  }
  if (has_receipt_message_id) {
    store(input_invoice.receipt_message_id, storer);
  }
  if (has_extended_media) {
    store(input_invoice.extended_media, storer);
  }
}

template <class ParserT>
void parse(InputInvoice &input_invoice, ParserT &parser) {
  bool has_description;
  bool has_photo;
  bool has_start_parameter;
  bool has_payload;
  bool has_provider_token;
  bool has_provider_data;
  bool has_total_amount;
  bool has_receipt_message_id;
  bool has_extended_media;
  if (parser.version() >= static_cast<int32>(Version::AddInputInvoiceFlags)) {
    BEGIN_PARSE_FLAGS();
    PARSE_FLAG(has_description);
    PARSE_FLAG(has_photo);
    PARSE_FLAG(has_start_parameter);
    PARSE_FLAG(has_payload);
    PARSE_FLAG(has_provider_token);
    PARSE_FLAG(has_provider_data);
    PARSE_FLAG(has_total_amount);
    PARSE_FLAG(has_receipt_message_id);
    PARSE_FLAG(has_extended_media);
    END_PARSE_FLAGS();
  } else {
    has_description = true;
    has_photo = true;
    has_start_parameter = true;
    has_payload = true;
    has_provider_token = true;
    has_provider_data = parser.version() >= static_cast<int32>(Version::AddMessageInvoiceProviderData);
    has_total_amount = true;
    has_receipt_message_id = true;
    has_extended_media = false;
  }
  parse(input_invoice.title, parser);
  if (has_description) {
    parse(input_invoice.description, parser);
  }
  if (has_photo) {
    parse(input_invoice.photo, parser);
  }
  if (has_start_parameter) {
    parse(input_invoice.start_parameter, parser);
  }
  parse(input_invoice.invoice, parser);
  if (has_payload) {
    parse(input_invoice.payload, parser);
  }
  if (has_provider_token) {
    parse(input_invoice.provider_token, parser);
  }
  if (has_provider_data) {
    parse(input_invoice.provider_data, parser);
  }
  if (has_total_amount) {
    parse(input_invoice.total_amount, parser);
  }
  if (has_receipt_message_id) {
    parse(input_invoice.receipt_message_id, parser);
  }
  if (has_extended_media) {
    parse(input_invoice.extended_media, parser);
  }
}

template <class StorerT>
void store(const Address &address, StorerT &storer) {
  store(address.country_code, storer);
  store(address.state, storer);
  store(address.city, storer);
  store(address.street_line1, storer);
  store(address.street_line2, storer);
  store(address.postal_code, storer);
}

template <class ParserT>
void parse(Address &address, ParserT &parser) {
  parse(address.country_code, parser);
  parse(address.state, parser);
  parse(address.city, parser);
  parse(address.street_line1, parser);
  parse(address.street_line2, parser);
  parse(address.postal_code, parser);
}

template <class StorerT>
void store(const OrderInfo &order_info, StorerT &storer) {
  bool has_name = !order_info.name.empty();
  bool has_phone_number = !order_info.phone_number.empty();
  bool has_email_address = !order_info.email_address.empty();
  bool has_shipping_address = order_info.shipping_address != nullptr;
  BEGIN_STORE_FLAGS();
  STORE_FLAG(has_name);
  STORE_FLAG(has_phone_number);
  STORE_FLAG(has_email_address);
  STORE_FLAG(has_shipping_address);
  END_STORE_FLAGS();
  if (has_name) {
    store(order_info.name, storer);
  }
  if (has_phone_number) {
    store(order_info.phone_number, storer);
  }
  if (has_email_address) {
    store(order_info.email_address, storer);
  }
  if (has_shipping_address) {
    store(order_info.shipping_address, storer);
  }
}

template <class ParserT>
void parse(OrderInfo &order_info, ParserT &parser) {
  bool has_name;
  bool has_phone_number;
  bool has_email_address;
  bool has_shipping_address;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(has_name);
  PARSE_FLAG(has_phone_number);
  PARSE_FLAG(has_email_address);
  PARSE_FLAG(has_shipping_address);
  END_PARSE_FLAGS();
  if (has_name) {
    parse(order_info.name, parser);
  }
  if (has_phone_number) {
    parse(order_info.phone_number, parser);
  }
  if (has_email_address) {
    parse(order_info.email_address, parser);
  }
  if (has_shipping_address) {
    parse(order_info.shipping_address, parser);
  }
}

}  // namespace td
