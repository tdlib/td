//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once
#include "td/mtproto/PacketStorer.h"

namespace td {
namespace mtproto {
class NoCryptoImpl {
 public:
  NoCryptoImpl(uint64 message_id, const Storer &data) : message_id(message_id), data(data) {
  }
  template <class T>
  void do_store(T &storer) const {
    storer.store_binary(message_id);
    storer.store_binary(static_cast<int32>(data.size()));
    storer.store_storer(data);
  }

 private:
  uint64 message_id;
  const Storer &data;
};
}  // namespace mtproto
}  // namespace td
