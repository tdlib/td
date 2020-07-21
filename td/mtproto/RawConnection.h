//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/mtproto/IStreamTransport.h"
#include "td/mtproto/PacketInfo.h"
#include "td/mtproto/TransportType.h"

#include "td/utils/buffer.h"
#include "td/utils/BufferedFd.h"
#include "td/utils/common.h"
#include "td/utils/port/detail/PollableFd.h"
#include "td/utils/port/SocketFd.h"
#include "td/utils/Status.h"
#include "td/utils/StorerBase.h"

#include "td/telegram/StateManager.h"

#include <map>

namespace td {
namespace mtproto {

class AuthKey;

class RawConnection {
 public:
  class StatsCallback {
   public:
    virtual ~StatsCallback() = default;
    virtual void on_read(uint64 bytes) = 0;
    virtual void on_write(uint64 bytes) = 0;

    virtual void on_pong() = 0;   // called when we know that connection is alive
    virtual void on_error() = 0;  // called on RawConnection error. Such error should be very rare on good connections.
    virtual void on_mtproto_error() = 0;
  };
  RawConnection() = default;
  RawConnection(SocketFd socket_fd, TransportType transport_type, unique_ptr<StatsCallback> stats_callback)
      : socket_fd_(std::move(socket_fd))
      , transport_(create_transport(transport_type))
      , stats_callback_(std::move(stats_callback)) {
    transport_->init(&socket_fd_.input_buffer(), &socket_fd_.output_buffer());
  }

  void set_connection_token(StateManager::ConnectionToken connection_token) {
    connection_token_ = std::move(connection_token);
  }

  bool can_send() const {
    return transport_->can_write();
  }
  TransportType get_transport_type() const {
    return transport_->get_type();
  }
  void send_crypto(const Storer &storer, int64 session_id, int64 salt, const AuthKey &auth_key,
                   uint64 quick_ack_token = 0);
  uint64 send_no_crypto(const Storer &storer);

  PollableFdInfo &get_poll_info() {
    return socket_fd_.get_poll_info();
  }
  StatsCallback *stats_callback() {
    return stats_callback_.get();
  }

  class Callback {
   public:
    Callback() = default;
    Callback(const Callback &) = delete;
    Callback &operator=(const Callback &) = delete;
    virtual ~Callback() = default;
    virtual Status on_raw_packet(const PacketInfo &info, BufferSlice packet) = 0;
    virtual Status on_quick_ack(uint64 quick_ack_token) {
      return Status::Error("Quick acks unsupported fully, but still used");
    }
    virtual Status before_write() {
      return Status::OK();
    }
    virtual void on_read(size_t size) {
    }
  };

  // NB: After first returned error, all subsequent calls will return error too.
  Status flush(const AuthKey &auth_key, Callback &callback) TD_WARN_UNUSED_RESULT {
    auto status = do_flush(auth_key, callback);
    if (status.is_error()) {
      if (stats_callback_ && status.code() != 2) {
        stats_callback_->on_error();
      }
      has_error_ = true;
    }
    return status;
  }

  bool has_error() const {
    return has_error_;
  }

  void close() {
    transport_.reset();
    socket_fd_.close();
  }

  uint32 extra_{0};
  string debug_str_;
  double rtt_{0};

 private:
  BufferedFd<SocketFd> socket_fd_;
  unique_ptr<IStreamTransport> transport_;
  std::map<uint32, uint64> quick_ack_to_token_;
  bool has_error_{false};

  unique_ptr<StatsCallback> stats_callback_;

  StateManager::ConnectionToken connection_token_;

  Status flush_read(const AuthKey &auth_key, Callback &callback);
  Status flush_write();

  Status on_quick_ack(uint32 quick_ack, Callback &callback);
  Status on_read_mtproto_error(int32 error_code);

  Status do_flush(const AuthKey &auth_key, Callback &callback) TD_WARN_UNUSED_RESULT {
    if (has_error_) {
      return Status::Error("Connection has already failed");
    }
    sync_with_poll(socket_fd_);

    // read/write
    // EINVAL may be returned in linux kernel < 2.6.28. And on some new kernels too.
    // just close connection and hope that read or write will not return this error too.
    TRY_STATUS(socket_fd_.get_pending_error());

    TRY_STATUS(flush_read(auth_key, callback));
    TRY_STATUS(callback.before_write());
    TRY_STATUS(flush_write());
    if (can_close_local(socket_fd_)) {
      return Status::Error("Connection closed");
    }
    return Status::OK();
  }
};

}  // namespace mtproto
}  // namespace td
