//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/ByteFlow.h"
#include "td/utils/common.h"
#include "td/utils/crypto.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"

namespace td {

#if TD_HAVE_OPENSSL
class AesCtrByteFlow : public ByteFlowInplaceBase {
 public:
  void init(const UInt256 &key, const UInt128 &iv) {
    state_.init(key, iv);
  }
  void init(AesCtrState &&state) {
    state_ = std::move(state);
  }
  AesCtrState move_aes_ctr_state() {
    return std::move(state_);
  }
  void loop() override {
    bool was_updated = false;
    while (true) {
      auto ready = input_->prepare_read();
      if (ready.empty()) {
        break;
      }
      state_.encrypt(ready, MutableSlice(const_cast<char *>(ready.data()), ready.size()));
      input_->confirm_read(ready.size());
      output_.advance_end(ready.size());
      was_updated = true;
    }
    if (was_updated) {
      on_output_updated();
    }
    if (!is_input_active_) {
      finish(Status::OK());  // End of input stream.
    }
    set_need_size(1);
  }

 private:
  AesCtrState state_;
};
#endif

}  // namespace td
