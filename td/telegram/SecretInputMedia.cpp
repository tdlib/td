//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/SecretInputMedia.h"

#include "td/telegram/files/FileManager.h"

namespace td {

SecretInputMedia::SecretInputMedia(tl_object_ptr<telegram_api::InputEncryptedFile> input_file, BufferSlice &&thumbnail,
                                   Dimensions thumbnail_dimensions, const string &mime_type, const FileView &file_view,
                                   vector<tl_object_ptr<secret_api::DocumentAttribute>> &&attributes,
                                   const string &caption)
    : input_file_(std::move(input_file)) {
  auto &encryption_key = file_view.encryption_key();
  decrypted_media_ = secret_api::make_object<secret_api::decryptedMessageMediaDocument>(
      std::move(thumbnail), thumbnail_dimensions.width, thumbnail_dimensions.height, mime_type,
      narrow_cast<int32>(file_view.size()), BufferSlice(encryption_key.key_slice()),
      BufferSlice(encryption_key.iv_slice()), std::move(attributes), caption);
}

}  // namespace td
