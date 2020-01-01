//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/Payments.h"

#include "td/telegram/Photo.hpp"

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
  BEGIN_STORE_FLAGS();
  STORE_FLAG(invoice.is_test);
  STORE_FLAG(invoice.need_name);
  STORE_FLAG(invoice.need_phone_number);
  STORE_FLAG(invoice.need_email_address);
  STORE_FLAG(invoice.need_shipping_address);
  STORE_FLAG(invoice.is_flexible);
  STORE_FLAG(invoice.send_phone_number_to_provider);
  STORE_FLAG(invoice.send_email_address_to_provider);
  END_STORE_FLAGS();
  store(invoice.currency, storer);
  store(invoice.price_parts, storer);
}

template <class ParserT>
void parse(Invoice &invoice, ParserT &parser) {
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(invoice.is_test);
  PARSE_FLAG(invoice.need_name);
  PARSE_FLAG(invoice.need_phone_number);
  PARSE_FLAG(invoice.need_email_address);
  PARSE_FLAG(invoice.need_shipping_address);
  PARSE_FLAG(invoice.is_flexible);
  PARSE_FLAG(invoice.send_phone_number_to_provider);
  PARSE_FLAG(invoice.send_email_address_to_provider);
  END_PARSE_FLAGS();
  parse(invoice.currency, parser);
  parse(invoice.price_parts, parser);
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
