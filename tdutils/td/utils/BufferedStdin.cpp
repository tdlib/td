//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/BufferedStdin.h"
#include "td/utils/buffer.h"
#include "td/utils/port/detail/PollableFd.h"

namespace td {
class BufferedStdin {
 public:
 private:
  PollableFdInfo info_;
  ChainBufferWriter writer_;
  ChainBufferReader reader_ = writer_.extract_reader();
};
}  // namespace td

