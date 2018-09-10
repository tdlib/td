//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/port/FileFd.h"
#include "td/utils/buffer.h"

namespace td {

FileFd &Stdin();
FileFd &Stdout();
FileFd &Stderr();

class BufferedStdin {
 public:
  BufferedStdin() {
    file_fd_ = FileFd::from_native_fd(NativeFd(Stdin().get_native_fd().raw()));
    file_fd_.get_native_fd().set_is_blocking(false);
  }

  ~BufferedStdin() {
    file_fd_.move_as_native_fd().release();
  }

  ChainBufferReader &input_buffer() {
    return reader_;
  }

  PollableFdInfo &get_poll_info() {
    return file_fd_.get_poll_info();
  }
  const PollableFdInfo &get_poll_info() const {
    return file_fd_.get_poll_info();
  }

  Result<size_t> flush_read(size_t max_read = std::numeric_limits<size_t>::max()) TD_WARN_UNUSED_RESULT {
    size_t result = 0;
    while (::td::can_read(*this) && max_read) {
      MutableSlice slice = writer_.prepare_append().truncate(max_read);
      TRY_RESULT(x, file_fd_.read(slice));
      slice.truncate(x);
      writer_.confirm_append(x);
      result += x;
      max_read -= x;
    }
    if (result) {
      reader_.sync_with_writer();
    }
    return result;
  }

 private:
  FileFd file_fd_;
  ChainBufferWriter writer_;
  ChainBufferReader reader_ = writer_.extract_reader();
};

}  // namespace td
