//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/HashTableUtils.h"
#include "td/utils/StringBuilder.h"

#include <type_traits>

namespace td {

class MessageContentUploadId {
  uint64 id_ = 0;

 public:
  MessageContentUploadId() = default;

  explicit constexpr MessageContentUploadId(uint64 message_content_upload_id) : id_(message_content_upload_id) {
  }
  template <class T, typename = std::enable_if_t<std::is_convertible<T, uint64>::value>>
  MessageContentUploadId(T message_content_upload_id) = delete;

  uint64 get() const {
    return id_;
  }

  bool operator==(const MessageContentUploadId &other) const {
    return id_ == other.id_;
  }

  bool operator!=(const MessageContentUploadId &other) const {
    return id_ != other.id_;
  }
};

struct MessageContentUploadIdHash {
  uint32 operator()(MessageContentUploadId message_content_upload_id) const {
    return Hash<uint64>()(message_content_upload_id.get());
  }
};

inline StringBuilder &operator<<(StringBuilder &string_builder, MessageContentUploadId message_content_upload_id) {
  return string_builder << "upload " << message_content_upload_id.get();
}

}  // namespace td
