// SPDX-FileCopyrightText: Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: BSL-1.0 AND MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
#include "td/mtproto/TlsReaderByteFlow.h"

#include "td/utils/logging.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"

namespace td {
namespace mtproto {

namespace {

const Slice kFakeChangeCipherSpec("\x14\x03\x03\x00\x01\x01", 6);
constexpr size_t kMaxTlsRecordPayloadSize = 1u << 14;

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
    auto declared_payload_size = static_cast<size_t>((buf[3] << 8) | buf[4]);
    LOG(WARNING) << "TLS emulation reader rejected unexpected record header"
                 << " [content_type=" << static_cast<int>(buf[0]) << "]"
                 << " [version_major=" << static_cast<int>(buf[1]) << "]"
                 << " [version_minor=" << static_cast<int>(buf[2]) << "]"
                 << " [declared_payload_size=" << declared_payload_size << "]";
    close_input(Status::Error("Invalid bytes at the beginning of a packet (emulated tls)"));
    return false;
  }
  size_t len = (buf[3] << 8) | buf[4];
  if (len > kMaxTlsRecordPayloadSize) {
    LOG(WARNING) << "TLS emulation reader rejected oversized record"
                 << " [declared_payload_size=" << len << "]"
                 << " [max_allowed=" << kMaxTlsRecordPayloadSize << "]";
    close_input(Status::Error("Oversized packet in emulated tls reader"));
    return false;
  }
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
