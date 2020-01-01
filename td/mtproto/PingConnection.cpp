//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/mtproto/PingConnection.h"

#include "td/mtproto/AuthData.h"
#include "td/mtproto/AuthKey.h"
#include "td/mtproto/mtproto_api.h"
#include "td/mtproto/NoCryptoStorer.h"
#include "td/mtproto/PacketInfo.h"
#include "td/mtproto/PacketStorer.h"
#include "td/mtproto/PingConnection.h"
#include "td/mtproto/RawConnection.h"
#include "td/mtproto/SessionConnection.h"
#include "td/mtproto/utils.h"

#include "td/utils/buffer.h"
#include "td/utils/logging.h"
#include "td/utils/Random.h"
#include "td/utils/Time.h"
#include "td/utils/UInt.h"

namespace td {
namespace mtproto {
namespace detail {

class PingConnectionReqPQ
    : public PingConnection
    , private RawConnection::Callback {
 public:
  PingConnectionReqPQ(unique_ptr<RawConnection> raw_connection, size_t ping_count)
      : raw_connection_(std::move(raw_connection)), ping_count_(ping_count) {
  }

  PollableFdInfo &get_poll_info() override {
    return raw_connection_->get_poll_info();
  }

  unique_ptr<RawConnection> move_as_raw_connection() override {
    return std::move(raw_connection_);
  }

  Status flush() override {
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
  bool was_pong() const override {
    return finish_time_ > 0;
  }
  double rtt() const override {
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
  unique_ptr<RawConnection> raw_connection_;
  size_t ping_count_ = 1;
  double start_time_ = 0.0;
  double finish_time_ = 0.0;
  bool was_ping_ = false;
};

class PingConnectionPingPong
    : public PingConnection
    , private SessionConnection::Callback {
 public:
  PingConnectionPingPong(unique_ptr<RawConnection> raw_connection, unique_ptr<AuthData> auth_data)
      : auth_data_(std::move(auth_data)) {
    auth_data_->set_header("");
    auth_data_->clear_seq_no();
    connection_ =
        make_unique<SessionConnection>(SessionConnection::Mode::Tcp, std::move(raw_connection), auth_data_.get());
  }

 private:
  unique_ptr<AuthData> auth_data_;
  unique_ptr<SessionConnection> connection_;
  int pong_cnt_{0};
  double rtt_;
  bool is_closed_{false};
  Status status_;
  void on_connected() override {
  }
  void on_closed(Status status) override {
    is_closed_ = true;
    CHECK(status.is_error());
    status_ = std::move(status);
  }

  void on_auth_key_updated() override {
  }
  void on_tmp_auth_key_updated() override {
  }
  void on_server_salt_updated() override {
  }
  void on_server_time_difference_updated() override {
  }

  void on_session_created(uint64 unique_id, uint64 first_id) override {
  }
  void on_session_failed(Status status) override {
  }

  void on_container_sent(uint64 container_id, vector<uint64> msgs_id) override {
  }
  Status on_pong() override {
    pong_cnt_++;
    if (pong_cnt_ == 1) {
      rtt_ = Time::now();
      connection_->set_online(false, false);
    } else if (pong_cnt_ == 2) {
      rtt_ = Time::now() - rtt_;
    }
    return Status::OK();
  }

  void on_message_ack(uint64 id) override {
  }
  Status on_message_result_ok(uint64 id, BufferSlice packet, size_t original_size) override {
    LOG(ERROR) << "Unexpected message";
    return Status::OK();
  }
  void on_message_result_error(uint64 id, int code, BufferSlice descr) override {
  }
  void on_message_failed(uint64 id, Status status) override {
  }
  void on_message_info(uint64 id, int32 state, uint64 answer_id, int32 answer_size) override {
  }

  Status on_destroy_auth_key() override {
    LOG(ERROR) << "Destroy auth key";
    return Status::OK();
  }
  PollableFdInfo &get_poll_info() override {
    return connection_->get_poll_info();
  }
  unique_ptr<RawConnection> move_as_raw_connection() override {
    return connection_->move_as_raw_connection();
  }
  Status flush() override {
    if (was_pong()) {
      return Status::OK();
    }
    CHECK(!is_closed_);
    connection_->flush(this);
    if (is_closed_) {
      CHECK(status_.is_error());
      return std::move(status_);
    }
    return Status::OK();
  }
  bool was_pong() const override {
    return pong_cnt_ >= 2;
  }
  double rtt() const override {
    return rtt_;
  }
};

}  // namespace detail

unique_ptr<PingConnection> PingConnection::create_req_pq(unique_ptr<RawConnection> raw_connection, size_t ping_count) {
  return make_unique<detail::PingConnectionReqPQ>(std::move(raw_connection), ping_count);
}

unique_ptr<PingConnection> PingConnection::create_ping_pong(unique_ptr<RawConnection> raw_connection,
                                                            unique_ptr<AuthData> auth_data) {
  return make_unique<detail::PingConnectionPingPong>(std::move(raw_connection), std::move(auth_data));
}

}  // namespace mtproto
}  // namespace td
