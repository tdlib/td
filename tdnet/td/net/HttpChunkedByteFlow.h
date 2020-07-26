//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/ByteFlow.h"

#include <limits>

namespace td {

class HttpChunkedByteFlow final : public ByteFlowBase {
 public:
  bool loop() override;

 private:
  static constexpr int MAX_CHUNK_SIZE = 15 << 20;                     // some reasonable limit
  static constexpr int MAX_SIZE = std::numeric_limits<int32>::max();  // some reasonable limit
  static constexpr size_t MIN_UPDATE_SIZE = 1 << 14;
  enum class State { ReadChunkLength, ReadChunkContent, OK };
  State state_ = State::ReadChunkLength;
  size_t len_ = 0;
  size_t save_len_ = 0;
  size_t total_size_ = 0;
  size_t uncommited_size_ = 0;
};

}  // namespace td
