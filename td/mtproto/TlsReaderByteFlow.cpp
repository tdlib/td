//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/mtproto/TlsReaderByteFlow.h"

#include "td/utils/Slice.h"
#include "td/utils/Status.h"

namespace td {
namespace mtproto {

namespace {

const Slice kFakeChangeCipherSpec("\x14\x03\x03\x00\x01\x01", 6);

}  // namespace

bool TlsReaderByteFlow::loop() {
  if (!first_change_cipher_spec_checked_) {
    auto input_size = input_->size();
    if (input_size != 0 && input_size < kFakeChangeCipherSpec.size()) {
      auto it = input_->clone();
      uint8 prefix[6] = {0};
      it.advance(input_size, MutableSlice(prefix, input_size));
      if (Slice(reinterpret_cast<const char *>(prefix), input_size) == kFakeChangeCipherSpec.substr(0, input_size)) {
        set_need_size(kFakeChangeCipherSpec.size());
        return false;
      }
    }

    if (input_size >= kFakeChangeCipherSpec.size()) {
      auto it = input_->clone();
      uint8 prefix[6];
      it.advance(kFakeChangeCipherSpec.size(), MutableSlice(prefix, kFakeChangeCipherSpec.size()));
      if (Slice(reinterpret_cast<const char *>(prefix), kFakeChangeCipherSpec.size()) == kFakeChangeCipherSpec) {
        *input_ = std::move(it);
      }
    }
    first_change_cipher_spec_checked_ = true;
  }

  if (input_->size() < 5) {
    set_need_size(5);
    return false;
  }

  auto it = input_->clone();
  uint8 buf[5];
  it.advance(5, MutableSlice(buf, 5));
  if (Slice(buf, 3) != Slice("\x17\x03\x03")) {
    close_input(Status::Error("Invalid bytes at the beginning of a packet (emulated tls)"));
    return false;
  }
  size_t len = (buf[3] << 8) | buf[4];
  if (it.size() < len) {
    set_need_size(5 + len);
    return false;
  }

  output_.append(it.cut_head(len));
  *input_ = std::move(it);
  return true;
}

}  // namespace mtproto
}  // namespace td
