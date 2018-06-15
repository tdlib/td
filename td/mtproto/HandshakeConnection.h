//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/mtproto/AuthKey.h"
#include "td/mtproto/Handshake.h"
#include "td/mtproto/NoCryptoStorer.h"
#include "td/mtproto/RawConnection.h"
#include "td/mtproto/Transport.h"
#include "td/mtproto/utils.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/port/Fd.h"
#include "td/utils/Status.h"

namespace td {
namespace mtproto {
class HandshakeConnection
    : private RawConnection::Callback
    , private AuthKeyHandshake::Callback {
 public:
  HandshakeConnection(std::unique_ptr<RawConnection> raw_connection, AuthKeyHandshake *handshake,
                      std::unique_ptr<AuthKeyHandshakeContext> context)
      : raw_connection_(std::move(raw_connection)), handshake_(handshake), context_(std::move(context)) {
    handshake_->resume(this);
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
    auto status = raw_connection_->flush(AuthKey(), *this);
    if (status.code() == -404) {
      LOG(WARNING) << "Clear handshake " << tag("error", status);
      handshake_->clear();
    }
    return status;
  }

 private:
  std::unique_ptr<RawConnection> raw_connection_;
  AuthKeyHandshake *handshake_;
  std::unique_ptr<AuthKeyHandshakeContext> context_;

  void send_no_crypto(const Storer &storer) override {
    raw_connection_->send_no_crypto(PacketStorer<NoCryptoImpl>(0, storer));
  }

  Status on_raw_packet(const PacketInfo &packet_info, BufferSlice packet) override {
    if (packet_info.no_crypto_flag == false) {
      return Status::Error("Expected not encrypted packet");
    }

    // skip header
    if (packet.size() < 12) {
      return Status::Error("Result is too small");
    }
    packet.confirm_read(12);

    auto fixed_packet_size = packet.size() & ~3;  // remove some padded data
    TRY_STATUS(handshake_->on_message(packet.as_slice().truncate(fixed_packet_size), this, context_.get()));
    return Status::OK();
  }
};
}  // namespace mtproto
}  // namespace td
