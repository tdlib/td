//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/mtproto/AuthKey.h"
#include "td/mtproto/NoCryptoStorer.h"
#include "td/mtproto/RawConnection.h"
#include "td/mtproto/utils.h"

#include "td/utils/buffer.h"
#include "td/utils/port/Fd.h"
#include "td/utils/Random.h"
#include "td/utils/Status.h"

#include "td/mtproto/mtproto_api.h"

namespace td {
namespace mtproto {
class PingConnection : private RawConnection::Callback {
 public:
  explicit PingConnection(std::unique_ptr<RawConnection> raw_connection) : raw_connection_(std::move(raw_connection)) {
  }

  Fd &get_pollable() {
    return raw_connection_->get_pollable();
  }

  std::unique_ptr<RawConnection> move_as_raw_connection() {
    return std::move(raw_connection_);
  }

  void close() {
    raw_connection_->close();
  }

  Status flush() {
    if (!was_ping_) {
      UInt128 nonce;
      Random::secure_bytes(nonce.raw, sizeof(nonce));
      raw_connection_->send_no_crypto(PacketStorer<NoCryptoImpl>(1, create_storer(mtproto_api::req_pq(nonce))));
      was_ping_ = true;
    }
    return raw_connection_->flush(AuthKey(), *this);
  }
  bool was_pong() const {
    return was_pong_;
  }

  Status on_raw_packet(const PacketInfo &packet_info, BufferSlice packet) override {
    if (packet.size() < 12) {
      return Status::Error("Result is too small");
    }
    packet.confirm_read(12);
    // TODO: fetch_result
    was_pong_ = true;
    return Status::OK();
  }

 private:
  std::unique_ptr<RawConnection> raw_connection_;
  bool was_ping_ = false;
  bool was_pong_ = false;
};
}  // namespace mtproto
}  // namespace td
