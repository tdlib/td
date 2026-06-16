//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"

#include <type_traits>

namespace td {

struct InputMedia {
  telegram_api::object_ptr<telegram_api::InputMedia> media_;
  telegram_api::object_ptr<telegram_api::InputRichMessage> rich_message_;

  InputMedia() = default;

  template <class T, class = std::enable_if_t<std::is_base_of<telegram_api::InputMedia, T>::value>>
  InputMedia(telegram_api::object_ptr<T> &&media) : media_(std::move(media)) {
  }

  InputMedia(telegram_api::object_ptr<telegram_api::InputMedia> &&media) : media_(std::move(media)) {
  }

  InputMedia(telegram_api::object_ptr<telegram_api::InputRichMessage> &&rich_message)
      : rich_message_(std::move(rich_message)) {
  }

  bool is_empty() const {
    return media_ == nullptr && rich_message_ == nullptr;
  }
};

}  // namespace td
