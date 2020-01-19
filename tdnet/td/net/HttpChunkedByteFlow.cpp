//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/net/HttpChunkedByteFlow.h"

#include "td/utils/find_boundary.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Status.h"

namespace td {

void HttpChunkedByteFlow::loop() {
  bool was_updated = false;
  size_t need_size;
  while (true) {
    if (state_ == State::ReadChunkLength) {
      bool ok = find_boundary(input_->clone(), "\r\n", len_);
      if (len_ > 10) {
        return finish(Status::Error(PSLICE() << "Too long length in chunked "
                                             << input_->cut_head(len_).move_as_buffer_slice().as_slice()));
      }
      if (!ok) {
        need_size = input_->size() + 1;
        break;
      }
      auto s_len = input_->cut_head(len_).move_as_buffer_slice();
      input_->advance(2);
      len_ = hex_to_integer<size_t>(s_len.as_slice());
      if (len_ > MAX_CHUNK_SIZE) {
        return finish(Status::Error(PSLICE() << "Invalid chunk size " << tag("size", len_)));
      }
      save_len_ = len_;
      state_ = State::ReadChunkContent;
    }

    auto size = input_->size();
    auto ready = min(len_, size);
    need_size = min(MIN_UPDATE_SIZE, len_ + 2);
    if (size < need_size) {
      break;
    }
    total_size_ += ready;
    uncommited_size_ += ready;
    if (total_size_ > MAX_SIZE) {
      return finish(Status::Error(PSLICE() << "Too big query " << tag("size", input_->size())));
    }

    output_.append(input_->cut_head(ready));
    len_ -= ready;
    if (uncommited_size_ >= MIN_UPDATE_SIZE) {
      uncommited_size_ = 0;
      was_updated = true;
    }

    if (len_ == 0) {
      if (input_->size() < 2) {
        need_size = 2;
        break;
      }
      input_->advance(2);
      total_size_ += 2;
      if (save_len_ == 0) {
        return finish(Status::OK());
      }
      state_ = State::ReadChunkLength;
      len_ = 0;
    }
  }
  if (was_updated) {
    on_output_updated();
  }
  if (!is_input_active_) {
    return finish(Status::Error("Unexpected end of stream"));
  }
  set_need_size(need_size);
}

}  // namespace td
