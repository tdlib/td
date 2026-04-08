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
#include "td/mtproto/stealth/Interfaces.h"

#include "td/utils/BufferedFd.h"
#include "td/utils/ScopeGuard.h"
#include "td/utils/StorerBase.h"
#include "td/utils/tests.h"

#include <cstring>

namespace {

using td::mtproto::AuthKey;
using td::mtproto::IStreamTransport;
using td::mtproto::RawConnection;
using td::mtproto::set_transport_factory_for_tests;
using td::mtproto::stealth::TrafficHint;
using td::mtproto::StreamTransportFactoryForTests;
using td::mtproto::test::create_socket_pair;
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
  }
  void on_pong() final {
  }
  void on_error() final {
  }
  void on_mtproto_error() final {
  }
};

class HintCapturingTransport final : public IStreamTransport {
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
    written_hints.push_back(last_hint_);
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

  void set_traffic_hint(TrafficHint hint) final {
    last_hint_ = hint;
  }

  td::vector<TrafficHint> written_hints;

 private:
  TrafficHint last_hint_{TrafficHint::Unknown};
};

HintCapturingTransport *g_hint_transport = nullptr;

td::unique_ptr<IStreamTransport> make_hint_capturing_transport(TransportType type) {
  (void)type;
  auto transport = td::make_unique<HintCapturingTransport>();
  g_hint_transport = transport.get();
  return transport;
}

TEST(RawConnectionHints, SendNoCryptoDefaultsToAuthHandshake) {
  auto socket_pair = create_socket_pair().move_as_ok();
  auto previous_factory = set_transport_factory_for_tests(&make_hint_capturing_transport);
  SCOPE_EXIT {
    g_hint_transport = nullptr;
    set_transport_factory_for_tests(previous_factory);
  };

  auto raw_connection = RawConnection::create(
      td::IPAddress(), td::BufferedFd<td::SocketFd>(std::move(socket_pair.client)),
      TransportType{TransportType::Tcp, 0, td::mtproto::ProxySecret()}, td::make_unique<NoopStatsCallback>());

  ASSERT_TRUE(g_hint_transport != nullptr);

  FixedStorer storer("handshake");
  raw_connection->send_no_crypto(storer);

  ASSERT_EQ(1u, g_hint_transport->written_hints.size());
  ASSERT_EQ(TrafficHint::AuthHandshake, g_hint_transport->written_hints[0]);
}

TEST(RawConnectionHints, SendCryptoPropagatesExplicitHint) {
  auto socket_pair = create_socket_pair().move_as_ok();
  auto previous_factory = set_transport_factory_for_tests(&make_hint_capturing_transport);
  SCOPE_EXIT {
    g_hint_transport = nullptr;
    set_transport_factory_for_tests(previous_factory);
  };

  auto raw_connection = RawConnection::create(
      td::IPAddress(), td::BufferedFd<td::SocketFd>(std::move(socket_pair.client)),
      TransportType{TransportType::Tcp, 0, td::mtproto::ProxySecret()}, td::make_unique<NoopStatsCallback>());

  ASSERT_TRUE(g_hint_transport != nullptr);

  FixedStorer storer("bulk");
  AuthKey auth_key(1, td::string(256, 'a'));
  raw_connection->send_crypto(storer, 1, 1, auth_key, 0, TrafficHint::BulkData);

  ASSERT_EQ(1u, g_hint_transport->written_hints.size());
  ASSERT_EQ(TrafficHint::BulkData, g_hint_transport->written_hints[0]);
}

}  // namespace