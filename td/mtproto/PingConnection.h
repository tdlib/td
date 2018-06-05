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
#include "td/utils/Time.h"

#include "td/mtproto/mtproto_api.h"

namespace td {
namespace mtproto {

class PingConnection : private RawConnection::Callback {
 public:
  PingConnection(std::unique_ptr<RawConnection> raw_connection, size_t ping_count)
      : raw_connection_(std::move(raw_connection)), ping_count_(ping_count) {
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
      raw_connection_->send_no_crypto(PacketStorer<NoCryptoImpl>(1, create_storer(mtproto_api::req_pq_multi(nonce))));
      was_ping_ = true;
      if (ping_count_ == 1) {
        start_time_ = Time::now();
      }
    }
    return raw_connection_->flush(AuthKey(), *this);
  }
  bool was_pong() const {
    return finish_time_ > 0;
  }
  double rtt() const {
    return finish_time_ - start_time_;
  }

  Status on_raw_packet(const PacketInfo &packet_info, BufferSlice packet) override {
    if (packet.size() < 12) {
      return Status::Error("Result is too small");
    }
    packet.confirm_read(12);
    // TODO: fetch_result

    if (--ping_count_ > 0) {
      was_ping_ = false;
      return flush();
    } else {
      finish_time_ = Time::now();
      return Status::OK();
    }
  }

 private:
  std::unique_ptr<RawConnection> raw_connection_;
  size_t ping_count_ = 1;
  double start_time_ = 0.0;
  double finish_time_ = 0.0;
  bool was_ping_ = false;
};

}  // namespace mtproto
}  // namespace td
