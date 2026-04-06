//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "test/stealth/TlsInitTestHelpers.h"

#include "td/mtproto/AuthKey.h"
#include "td/mtproto/IStreamTransport.h"
#include "td/mtproto/RawConnection.h"

#include "td/utils/buffer.h"
#include "td/utils/BufferedFd.h"
#include "td/utils/port/PollFlags.h"
#include "td/utils/ScopeGuard.h"
#include "td/utils/StorerBase.h"
#include "td/utils/tests.h"

#include <cstring>

namespace {

using td::mtproto::IStreamTransport;
using td::mtproto::RawConnection;
using td::mtproto::set_transport_factory_for_tests;
using td::mtproto::StreamTransportFactoryForTests;
using td::mtproto::test::create_socket_pair;
using td::mtproto::test::read_exact;
using td::mtproto::TransportType;

class FixedStorer final : public td::Storer {
 public:
  explicit FixedStorer(td::Slice data) : data_(data.str()) {
  }

  size_t size() const final {
    return data_.size();
  }

  size_t store(td::uint8 *ptr) const final {
    std::memcpy(ptr, data_.data(), data_.size());
    return data_.size();
  }

 private:
  td::string data_;
};

class NoopStatsCallback final : public RawConnection::StatsCallback {
 public:
  void on_read(td::uint64 bytes) final {
  }

  void on_write(td::uint64 bytes) final {
    writes.push_back(bytes);
  }

  void on_pong() final {
  }

  void on_error() final {
  }

  void on_mtproto_error() final {
  }

  td::vector<td::uint64> writes;
};

class NoopRawConnectionCallback final : public RawConnection::Callback {
 public:
  td::Status on_raw_packet(const td::mtproto::PacketInfo &packet_info, td::BufferSlice packet) final {
    (void)packet_info;
    (void)packet;
    return td::Status::Error("unexpected inbound packet in flush-order seam test");
  }
};

class DeferredFlushTransport final : public IStreamTransport {
 public:
  td::Result<size_t> read_next(td::BufferSlice *message, td::uint32 *quick_ack) final {
    (void)message;
    (void)quick_ack;
    UNREACHABLE();
    return 0;
  }

  bool support_quick_ack() const final {
    return false;
  }

  void write(td::BufferWriter &&message, bool quick_ack) final {
    (void)quick_ack;
    queued_payload_ = message.as_buffer_slice().as_slice().str();
    write_calls++;
  }

  bool can_read() const final {
    return false;
  }

  bool can_write() const final {
    return true;
  }

  void init(td::ChainBufferReader *input, td::ChainBufferWriter *output) final {
    input_ = input;
    output_ = output;
  }

  size_t max_prepend_size() const final {
    return 0;
  }

  size_t max_append_size() const final {
    return 0;
  }

  TransportType get_type() const final {
    return TransportType{TransportType::Tcp, 0, td::mtproto::ProxySecret()};
  }

  bool use_random_padding() const final {
    return false;
  }

  void pre_flush_write(double now) final {
    pre_flush_write_calls++;
    last_pre_flush_now = now;
    CHECK(output_ != nullptr);
    if (!queued_payload_.empty()) {
      flushed_payload_ = queued_payload_;
      output_->append(flushed_payload_);
      queued_payload_.clear();
    }
  }

  td::string queued_payload_;
  td::string flushed_payload_;
  int write_calls{0};
  int pre_flush_write_calls{0};
  double last_pre_flush_now{0.0};

 private:
  td::ChainBufferReader *input_{nullptr};
  td::ChainBufferWriter *output_{nullptr};
};

DeferredFlushTransport *g_deferred_flush_transport = nullptr;

td::unique_ptr<IStreamTransport> make_deferred_flush_transport(TransportType type) {
  (void)type;
  auto transport = td::make_unique<DeferredFlushTransport>();
  g_deferred_flush_transport = transport.get();
  return transport;
}

TEST(RawConnectionFlushOrder, PreFlushWriteRunsBeforeSocketFlushAndMakesQueuedBytesVisible) {
  auto socket_pair = create_socket_pair().move_as_ok();
  auto previous_factory = set_transport_factory_for_tests(&make_deferred_flush_transport);
  SCOPE_EXIT {
    g_deferred_flush_transport = nullptr;
    set_transport_factory_for_tests(previous_factory);
  };

  auto stats_callback = td::make_unique<NoopStatsCallback>();
  auto *stats_ptr = stats_callback.get();
  auto raw_connection = RawConnection::create(
      td::IPAddress(), td::BufferedFd<td::SocketFd>(std::move(socket_pair.client)),
      TransportType{TransportType::Tcp, 0, td::mtproto::ProxySecret()}, std::move(stats_callback));

  ASSERT_TRUE(g_deferred_flush_transport != nullptr);

  FixedStorer storer("raw-connection-flush-order");
  raw_connection->send_no_crypto(storer);

  ASSERT_EQ(1, g_deferred_flush_transport->write_calls);
  ASSERT_FALSE(g_deferred_flush_transport->queued_payload_.empty());
  ASSERT_TRUE(g_deferred_flush_transport->flushed_payload_.empty());

  td::mtproto::AuthKey auth_key;
  NoopRawConnectionCallback callback;
  raw_connection->get_poll_info().add_flags(td::PollFlags::Write());
  ASSERT_TRUE(raw_connection->flush(auth_key, callback).is_ok());

  ASSERT_EQ(1, g_deferred_flush_transport->pre_flush_write_calls);
  ASSERT_FALSE(g_deferred_flush_transport->flushed_payload_.empty());
  ASSERT_TRUE(g_deferred_flush_transport->queued_payload_.empty());
  ASSERT_EQ(1u, stats_ptr->writes.size());
  ASSERT_EQ(static_cast<td::uint64>(g_deferred_flush_transport->flushed_payload_.size()), stats_ptr->writes[0]);

  auto wire = read_exact(socket_pair.peer, g_deferred_flush_transport->flushed_payload_.size());
  ASSERT_TRUE(wire.is_ok());
  ASSERT_EQ(g_deferred_flush_transport->flushed_payload_, wire.ok());
}

}  // namespace
