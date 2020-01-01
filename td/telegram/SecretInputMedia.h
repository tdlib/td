//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/secret_api.h"
#include "td/telegram/telegram_api.h"

namespace td {

struct SecretInputMedia {
  tl_object_ptr<telegram_api::InputEncryptedFile> input_file_;
  tl_object_ptr<secret_api::DecryptedMessageMedia> decrypted_media_;

  SecretInputMedia() = default;

  SecretInputMedia(tl_object_ptr<telegram_api::InputEncryptedFile> input_file,
                   tl_object_ptr<secret_api::DecryptedMessageMedia> decrypted_media)
      : input_file_(std::move(input_file)), decrypted_media_(std::move(decrypted_media)) {
  }

  bool empty() const {
    return decrypted_media_ == nullptr;
  }
};

}  // namespace td
