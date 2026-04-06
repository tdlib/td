//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/mtproto/IStreamTransport.h"
#include "td/mtproto/stealth/StealthConfig.h"

#include "td/utils/ScopeGuard.h"
#include "td/utils/tests.h"

namespace {

using td::mtproto::create_transport;
using td::mtproto::IStreamTransport;
using td::mtproto::ProxySecret;
using td::mtproto::set_transport_factory_for_tests;
using td::mtproto::stealth::IRng;
using td::mtproto::stealth::set_stealth_config_factory_for_tests;
using td::mtproto::stealth::StealthConfig;
using td::mtproto::TransportType;

int g_config_factory_calls = 0;
int g_transport_factory_calls = 0;

td::string make_tls_secret() {
  td::string secret;
  secret.push_back(static_cast<char>(0xee));
  secret += "0123456789secret";
  secret += "www.google.com";
  return secret;
}

td::Result<StealthConfig> counting_stealth_config_factory(const ProxySecret &secret, IRng &rng) {
  g_config_factory_calls++;
  auto config = StealthConfig::from_secret(secret, rng);
  auto status = config.validate();
  if (status.is_error()) {
    return status;
  }
  return config;
}

class MarkerTransport final : public IStreamTransport {
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
    (void)input;
    (void)output;
  }

  size_t max_prepend_size() const final {
    return 0;
  }

  size_t max_append_size() const final {
    return 0;
  }

  TransportType get_type() const final {
    return TransportType{TransportType::ObfuscatedTcp, 9, ProxySecret::from_raw(make_tls_secret())};
  }

  bool use_random_padding() const final {
    return false;
  }

  double get_shaping_wakeup() const final {
    return 123.456;
  }

  bool supports_tls_record_sizing() const final {
    return false;
  }
};

td::unique_ptr<IStreamTransport> marker_transport_factory(TransportType type) {
  g_transport_factory_calls++;
  if (type.type == TransportType::ObfuscatedTcp && type.secret.emulate_tls()) {
    return td::make_unique<MarkerTransport>();
  }
  return nullptr;
}

td::unique_ptr<IStreamTransport> nullptr_transport_factory(TransportType type) {
  (void)type;
  g_transport_factory_calls++;
  return nullptr;
}

TEST(StreamTransportActivationFailClosed, StrictActivationGateSkipsStealthConfigForLegacyKinds) {
  g_config_factory_calls = 0;
  auto previous_factory = set_stealth_config_factory_for_tests(&counting_stealth_config_factory);
  SCOPE_EXIT {
    set_stealth_config_factory_for_tests(previous_factory);
  };

  auto tcp = create_transport(TransportType{TransportType::Tcp, 0, ProxySecret()});
  auto http = create_transport(TransportType{TransportType::Http, 0, ProxySecret::from_raw("example.com")});
  auto obfuscated =
      create_transport(TransportType{TransportType::ObfuscatedTcp, 2, ProxySecret::from_raw("dd1234567890abcde")});

  ASSERT_EQ(TransportType::Tcp, tcp->get_type().type);
  ASSERT_EQ(TransportType::Http, http->get_type().type);
  ASSERT_EQ(TransportType::ObfuscatedTcp, obfuscated->get_type().type);
  ASSERT_EQ(0, g_config_factory_calls);
}

TEST(StreamTransportActivationFailClosed, TestTransportFactoryBypassesStealthActivationForTlsRequests) {
  g_config_factory_calls = 0;
  g_transport_factory_calls = 0;
  auto previous_transport_factory = set_transport_factory_for_tests(&marker_transport_factory);
  auto previous_config_factory = set_stealth_config_factory_for_tests(&counting_stealth_config_factory);
  SCOPE_EXIT {
    set_stealth_config_factory_for_tests(previous_config_factory);
    set_transport_factory_for_tests(previous_transport_factory);
  };

  auto transport =
      create_transport(TransportType{TransportType::ObfuscatedTcp, 2, ProxySecret::from_raw(make_tls_secret())});

  ASSERT_EQ(1, g_transport_factory_calls);
  ASSERT_EQ(0, g_config_factory_calls);
  ASSERT_EQ(TransportType::ObfuscatedTcp, transport->get_type().type);
  ASSERT_FALSE(transport->support_quick_ack());
  ASSERT_FALSE(transport->supports_tls_record_sizing());
  ASSERT_EQ(123.456, transport->get_shaping_wakeup());
}

TEST(StreamTransportActivationFailClosed, NullTestTransportFactoryFallsBackToSingleProductionActivationPass) {
  g_config_factory_calls = 0;
  g_transport_factory_calls = 0;
  auto previous_transport_factory = set_transport_factory_for_tests(&nullptr_transport_factory);
  auto previous_config_factory = set_stealth_config_factory_for_tests(&counting_stealth_config_factory);
  SCOPE_EXIT {
    set_stealth_config_factory_for_tests(previous_config_factory);
    set_transport_factory_for_tests(previous_transport_factory);
  };

  auto transport =
      create_transport(TransportType{TransportType::ObfuscatedTcp, 2, ProxySecret::from_raw(make_tls_secret())});

  ASSERT_EQ(1, g_transport_factory_calls);
#if TDLIB_STEALTH_SHAPING
  ASSERT_EQ(1, g_config_factory_calls);
  ASSERT_TRUE(transport->supports_tls_record_sizing());
#else
  ASSERT_EQ(0, g_config_factory_calls);
#endif
  ASSERT_EQ(TransportType::ObfuscatedTcp, transport->get_type().type);
}

}  // namespace
