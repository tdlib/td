//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/buffer.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/Storer.h"
#include "td/utils/tl_parsers.h"
#include "td/utils/tl_storers.h"

#include <limits>

namespace td {

namespace mtproto {
struct Query {
  int64 message_id;
  int32 seq_no;
  BufferSlice packet;
  bool gzip_flag;
  uint64 invoke_after_id;
  bool use_quick_ack;
};
}  // namespace mtproto

template <class T>
Result<typename T::ReturnType> fetch_result(Slice message, bool check_end = true) {
  TlParser parser(message);
  auto result = T::fetch_result(parser);

  if (check_end) {
    parser.fetch_end();
  }
  const char *error = parser.get_error();
  if (error != nullptr) {
    LOG(ERROR) << "Can't parse: " << format::as_hex_dump<4>(message);
    return Status::Error(500, Slice(error));
  }

  return std::move(result);
}

template <class T>
Result<typename T::ReturnType> fetch_result(const BufferSlice &message, bool check_end = true) {
  TlBufferParser parser(&message);
  auto result = T::fetch_result(parser);

  if (check_end) {
    parser.fetch_end();
  }
  const char *error = parser.get_error();
  if (error != nullptr) {
    LOG(ERROR) << "Can't parse: " << format::as_hex_dump<4>(message.as_slice());
    return Status::Error(500, Slice(error));
  }

  return std::move(result);
}

template <class T>
using TLStorer = DefaultStorer<T>;

template <class T>
class TLObjectStorer : public Storer {
  mutable size_t size_ = std::numeric_limits<size_t>::max();
  const T &object_;

 public:
  explicit TLObjectStorer(const T &object) : object_(object) {
  }

  size_t size() const override {
    if (size_ == std::numeric_limits<size_t>::max()) {
      TlStorerCalcLength storer;
      storer.store_binary(object_.get_id());
      object_.store(storer);
      size_ = storer.get_length();
    }
    return size_;
  }
  size_t store(uint8 *ptr) const override {
    TlStorerUnsafe storer(ptr);
    storer.store_binary(object_.get_id());
    object_.store(storer);
    return static_cast<size_t>(storer.get_buf() - ptr);
  }
};

namespace mtproto_api {
class Object;
class Function;
}  // namespace mtproto_api

namespace telegram_api {
class Object;
class Function;
}  // namespace telegram_api

TLStorer<mtproto_api::Function> create_storer(const mtproto_api::Function &function);

TLStorer<telegram_api::Function> create_storer(const telegram_api::Function &function);

TLObjectStorer<mtproto_api::Object> create_storer(const mtproto_api::Object &object);

TLObjectStorer<telegram_api::Object> create_storer(const telegram_api::Object &object);

}  // namespace td
