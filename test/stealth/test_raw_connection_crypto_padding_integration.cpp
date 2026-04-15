// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/TlsInitTestHelpers.h"

#include "td/mtproto/AuthKey.h"
#include "td/mtproto/IStreamTransport.h"
#include "td/mtproto/PacketInfo.h"
#include "td/mtproto/RawConnection.h"

#include "td/utils/BufferedFd.h"
#include "td/utils/ScopeGuard.h"
#include "td/utils/StorerBase.h"
#include "td/utils/tests.h"

#include <cstring>

namespace {

using td::mtproto::AuthKey;
using td::mtproto::IStreamTransport;
using td::mtproto::PacketInfo;
using td::mtproto::RawConnection;
using td::mtproto::set_transport_factory_for_tests;
using td::mtproto::StreamTransportFactoryForTests;
using td::mtproto::test::create_socket_pair;
using td::mtproto::TransportType;

constexpr size_t kStealthMinimumWireSize = 152;

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

class PacketSizingTransport final : public IStreamTransport {
 public:
  explicit PacketSizingTransport(bool enable_stealth_padding) : enable_stealth_padding_(enable_stealth_padding) {
  }

  td::Result<size_t> read_next(td::BufferSlice *message, td::uint32 *quick_ack) final {
    (void)message;
    (void)quick_ack;
    return 0;
  }

  bool support_quick_ack() const final {
    return false;
  }

  void write(td::BufferWriter &&message, bool quick_ack) final {
    (void)quick_ack;
    last_message_size = message.size();
    write_calls++;
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

  void configure_packet_info(PacketInfo *packet_info) const final {
    CHECK(packet_info != nullptr);
    if (!enable_stealth_padding_) {
      return;
    }
    packet_info->use_random_padding = true;
    packet_info->use_stealth_padding = true;
    packet_info->stealth_padding_min_bytes = 12;
    packet_info->stealth_padding_max_bytes = 12;
  }

  mutable size_t last_message_size{0};
  mutable int write_calls{0};

 private:
  bool enable_stealth_padding_{false};
};

bool g_enable_stealth_padding = false;
PacketSizingTransport *g_packet_sizing_transport = nullptr;

td::unique_ptr<IStreamTransport> make_packet_sizing_transport(TransportType type) {
  (void)type;
  auto transport = td::make_unique<PacketSizingTransport>(g_enable_stealth_padding);
  g_packet_sizing_transport = transport.get();
  return transport;
}

size_t send_and_capture_size(bool enable_stealth_padding) {
  auto socket_pair = create_socket_pair().move_as_ok();
  g_enable_stealth_padding = enable_stealth_padding;
  auto raw_connection = RawConnection::create(
      td::IPAddress(), td::BufferedFd<td::SocketFd>(std::move(socket_pair.client)),
      TransportType{TransportType::Tcp, 0, td::mtproto::ProxySecret()}, td::make_unique<NoopStatsCallback>());
  CHECK(g_packet_sizing_transport != nullptr);

  FixedStorer storer("ping");
  AuthKey auth_key(1, td::string(256, 'a'));
  raw_connection->send_crypto(storer, 1, 1, auth_key, 0, td::mtproto::stealth::TrafficHint::Keepalive);

  CHECK(g_packet_sizing_transport->write_calls == 1);
  return g_packet_sizing_transport->last_message_size;
}

TEST(RawConnectionCryptoPaddingIntegration, SendCryptoUsesConfiguredStealthPaddingInsteadOfLegacyBucketOnly) {
  SKIP_IF_NO_SOCKET_PAIR();
  auto previous_factory = set_transport_factory_for_tests(&make_packet_sizing_transport);
  SCOPE_EXIT {
    g_enable_stealth_padding = false;
    g_packet_sizing_transport = nullptr;
    set_transport_factory_for_tests(previous_factory);
  };

  auto baseline_size = send_and_capture_size(false);
  auto stealth_size = send_and_capture_size(true);

  ASSERT_TRUE(stealth_size > baseline_size);
  ASSERT_TRUE(stealth_size >= kStealthMinimumWireSize);
}

}  // namespace