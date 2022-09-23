//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/InputInvoice.h"

#include "td/telegram/MessageExtendedMedia.hpp"
#include "td/telegram/Photo.hpp"
#include "td/telegram/Version.h"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void store(const Invoice &invoice, StorerT &storer) {
  bool has_tip = invoice.max_tip_amount_ != 0;
  bool is_recurring = !invoice.recurring_payment_terms_of_service_url_.empty();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(invoice.is_test_);
  STORE_FLAG(invoice.need_name_);
  STORE_FLAG(invoice.need_phone_number_);
  STORE_FLAG(invoice.need_email_address_);
  STORE_FLAG(invoice.need_shipping_address_);
  STORE_FLAG(invoice.is_flexible_);
  STORE_FLAG(invoice.send_phone_number_to_provider_);
  STORE_FLAG(invoice.send_email_address_to_provider_);
  STORE_FLAG(has_tip);
  STORE_FLAG(is_recurring);
  END_STORE_FLAGS();
  store(invoice.currency_, storer);
  store(invoice.price_parts_, storer);
  if (has_tip) {
    store(invoice.max_tip_amount_, storer);
    store(invoice.suggested_tip_amounts_, storer);
  }
  if (is_recurring) {
    store(invoice.recurring_payment_terms_of_service_url_, storer);
  }
}

template <class ParserT>
void parse(Invoice &invoice, ParserT &parser) {
  bool has_tip;
  bool is_recurring;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(invoice.is_test_);
  PARSE_FLAG(invoice.need_name_);
  PARSE_FLAG(invoice.need_phone_number_);
  PARSE_FLAG(invoice.need_email_address_);
  PARSE_FLAG(invoice.need_shipping_address_);
  PARSE_FLAG(invoice.is_flexible_);
  PARSE_FLAG(invoice.send_phone_number_to_provider_);
  PARSE_FLAG(invoice.send_email_address_to_provider_);
  PARSE_FLAG(has_tip);
  PARSE_FLAG(is_recurring);
  END_PARSE_FLAGS();
  parse(invoice.currency_, parser);
  parse(invoice.price_parts_, parser);
  if (has_tip) {
    parse(invoice.max_tip_amount_, parser);
    parse(invoice.suggested_tip_amounts_, parser);
  }
  if (is_recurring) {
    parse(invoice.recurring_payment_terms_of_service_url_, parser);
  }
}

template <class StorerT>
void store(const InputInvoice &input_invoice, StorerT &storer) {
  bool has_description = !input_invoice.description_.empty();
  bool has_photo = !input_invoice.photo_.is_empty();
  bool has_start_parameter = !input_invoice.start_parameter_.empty();
  bool has_payload = !input_invoice.payload_.empty();
  bool has_provider_token = !input_invoice.provider_token_.empty();
  bool has_provider_data = !input_invoice.provider_data_.empty();
  bool has_total_amount = input_invoice.total_amount_ != 0;
  bool has_receipt_message_id = input_invoice.receipt_message_id_.is_valid();
  bool has_extended_media = input_invoice.extended_media_.is_empty();
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
  store(input_invoice.title_, storer);
  if (has_description) {
    store(input_invoice.description_, storer);
  }
  if (has_photo) {
    store(input_invoice.photo_, storer);
  }
  if (has_start_parameter) {
    store(input_invoice.start_parameter_, storer);
  }
  store(input_invoice.invoice_, storer);
  if (has_payload) {
    store(input_invoice.payload_, storer);
  }
  if (has_provider_token) {
    store(input_invoice.provider_token_, storer);
  }
  if (has_provider_data) {
    store(input_invoice.provider_data_, storer);
  }
  if (has_total_amount) {
    store(input_invoice.total_amount_, storer);
  }
  if (has_receipt_message_id) {
    store(input_invoice.receipt_message_id_, storer);
  }
  if (has_extended_media) {
    store(input_invoice.extended_media_, storer);
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
  parse(input_invoice.title_, parser);
  if (has_description) {
    parse(input_invoice.description_, parser);
  }
  if (has_photo) {
    parse(input_invoice.photo_, parser);
  }
  if (has_start_parameter) {
    parse(input_invoice.start_parameter_, parser);
  }
  parse(input_invoice.invoice_, parser);
  if (has_payload) {
    parse(input_invoice.payload_, parser);
  }
  if (has_provider_token) {
    parse(input_invoice.provider_token_, parser);
  }
  if (has_provider_data) {
    parse(input_invoice.provider_data_, parser);
  }
  if (has_total_amount) {
    parse(input_invoice.total_amount_, parser);
  }
  if (has_receipt_message_id) {
    parse(input_invoice.receipt_message_id_, parser);
  }
  if (has_extended_media) {
    parse(input_invoice.extended_media_, parser);
  }
}

}  // namespace td
