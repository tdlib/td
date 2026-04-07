//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "test/stealth/TlsInitTestHelpers.h"

#include "td/mtproto/IStreamTransport.h"
#include "td/mtproto/RawConnection.h"

#include "td/utils/ScopeGuard.h"
#include "td/utils/tests.h"

namespace {

using td::mtproto::IStreamTransport;
using td::mtproto::RawConnection;
using td::mtproto::set_transport_factory_for_tests;
using td::mtproto::test::create_socket_pair;
using td::mtproto::TransportType;

class NoopStatsCallback final : public RawConnection::StatsCallback {
 public:
  void on_read(td::uint64 bytes) final {
  }
  void on_write(td::uint64 bytes) final {
  }
  void on_pong() final {
  }
  void on_error() final {
  }
  void on_mtproto_error() final {
  }
};

class ShapingWakeupTransport final : public IStreamTransport {
 public:
  td::Result<size_t> read_next(td::BufferSlice *message, td::uint32 *quick_ack) final {
    (void)message;
    (void)quick_ack;
    return 0;
  }

  bool support_quick_ack() const final {
    return false;
  }

  void write(td::BufferWriter &&message, bool quick_ack) final {
    (void)message;
    (void)quick_ack;
  }

  bool can_read() const final {
    return false;
  }

  bool can_write() const final {
    return true;
  }

  void init(td::ChainBufferReader *input, td::ChainBufferWriter *output) final {
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

  double get_shaping_wakeup() const final {
    return shaping_wakeup_;
  }

  double shaping_wakeup_{0.0};
};

ShapingWakeupTransport *g_shaping_wakeup_transport = nullptr;

td::unique_ptr<IStreamTransport> make_shaping_wakeup_transport(TransportType type) {
  (void)type;
  auto transport = td::make_unique<ShapingWakeupTransport>();
  g_shaping_wakeup_transport = transport.get();
  return transport;
}

TEST(RawConnectionShapingWakeupSeam, ShapingWakeupReflectsTransportWakeupIncludingOverdueDeadline) {
  auto socket_pair = create_socket_pair().move_as_ok();
  auto previous_factory = set_transport_factory_for_tests(&make_shaping_wakeup_transport);
  SCOPE_EXIT {
    g_shaping_wakeup_transport = nullptr;
    set_transport_factory_for_tests(previous_factory);
  };

  auto stats_callback = td::make_unique<NoopStatsCallback>();
  auto raw_connection = RawConnection::create(
      td::IPAddress(), td::BufferedFd<td::SocketFd>(std::move(socket_pair.client)),
      TransportType{TransportType::Tcp, 0, td::mtproto::ProxySecret()}, std::move(stats_callback));

  ASSERT_TRUE(g_shaping_wakeup_transport != nullptr);

  auto now = td::Time::now_cached();
  g_shaping_wakeup_transport->shaping_wakeup_ = now + 0.125;
  ASSERT_EQ(g_shaping_wakeup_transport->shaping_wakeup_, raw_connection->shaping_wakeup_at());

  g_shaping_wakeup_transport->shaping_wakeup_ = now - 0.050;
  ASSERT_EQ(g_shaping_wakeup_transport->shaping_wakeup_, raw_connection->shaping_wakeup_at());
  ASSERT_TRUE(raw_connection->shaping_wakeup_at() < td::Time::now_cached());
}

}  // namespace