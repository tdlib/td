//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/Photo.h"

#include "td/utils/common.h"
#include "td/utils/tl_helpers.h"

namespace td {

struct MinChannel {
  string title_;
  DialogPhoto photo_;

  template <class StorerT>
  void store(StorerT &storer) const {
    using td::store;
    bool has_title = !title_.empty();
    bool has_photo = photo_.small_file_id.is_valid();
    BEGIN_STORE_FLAGS();
    STORE_FLAG(has_title);
    STORE_FLAG(has_photo);
    END_STORE_FLAGS();
    if (has_title) {
      store(title_, storer);
    }
    if (has_photo) {
      store(photo_, storer);
    }
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using td::parse;
    bool has_title;
    bool has_photo;
    BEGIN_PARSE_FLAGS();
    PARSE_FLAG(has_title);
    PARSE_FLAG(has_photo);
    END_PARSE_FLAGS();
    if (has_title) {
      parse(title_, parser);
    }
    if (has_photo) {
      parse(photo_, parser);
    }
  }
};

}  // namespace td
