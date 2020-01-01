//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/telegram/MessageEntity.h"
#include "td/telegram/MessageEntity.hpp"

#include "td/actor/PromiseFuture.h"

#include "td/utils/common.h"
#include "td/utils/Slice.h"
#include "td/utils/tl_helpers.h"

#include <utility>

namespace td {

class Td;

class TermsOfService {
  string id_;
  FormattedText text_;
  int32 min_user_age_ = 0;
  bool show_popup_ = true;

 public:
  explicit TermsOfService(telegram_api::object_ptr<telegram_api::help_termsOfService> terms = nullptr);

  Slice get_id() const {
    return id_;
  }

  td_api::object_ptr<td_api::termsOfService> get_terms_of_service_object() const {
    if (id_.empty()) {
      return nullptr;
    }

    return td_api::make_object<td_api::termsOfService>(get_formatted_text_object(text_), min_user_age_, show_popup_);
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    using td::store;
    BEGIN_STORE_FLAGS();
    STORE_FLAG(show_popup_);
    END_STORE_FLAGS();
    store(id_, storer);
    store(text_, storer);
    store(min_user_age_, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using td::parse;
    BEGIN_PARSE_FLAGS();
    PARSE_FLAG(show_popup_);
    END_PARSE_FLAGS();
    parse(id_, parser);
    parse(text_, parser);
    parse(min_user_age_, parser);
  }
};

void get_terms_of_service(Td *td, Promise<std::pair<int32, TermsOfService>> promise);

void accept_terms_of_service(Td *td, string &&terms_of_service_id, Promise<Unit> &&promise);

}  // namespace td
