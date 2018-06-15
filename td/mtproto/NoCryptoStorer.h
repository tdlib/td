//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once
#include "td/mtproto/PacketStorer.h"

#include "td/utils/Random.h"

namespace td {
namespace mtproto {
class NoCryptoImpl {
 public:
  NoCryptoImpl(uint64 message_id, const Storer &data, bool need_pad = true) : message_id(message_id), data(data) {
    if (need_pad) {
      auto data_size = data.size();
      auto pad_size = (data_size + 15) / 16 * 16 - data_size;
      pad_size += 16 * (static_cast<size_t>(Random::secure_int32()) % 16);
      pad_.resize(pad_size);
      Random::secure_bytes(pad_);
    }
  }
  template <class T>
  void do_store(T &storer) const {
    storer.store_binary(message_id);
    storer.store_binary(static_cast<int32>(data.size() + pad_.size()));
    storer.store_storer(data);
    storer.store_slice(pad_);
  }

 private:
  uint64 message_id;
  const Storer &data;
  std::string pad_;
};
}  // namespace mtproto
}  // namespace td
