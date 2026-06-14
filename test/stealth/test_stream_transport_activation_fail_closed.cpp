// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/IStreamTransport.h"
#include "td/mtproto/stealth/StealthConfig.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"
#include "td/mtproto/stealth/StealthRuntimeParams.h"

#include "td/utils/logging.h"
#include "td/utils/ScopeGuard.h"
#include "td/utils/tests.h"

namespace {

using td::mtproto::create_transport;
using td::mtproto::IStreamTransport;
using td::mtproto::ProxySecret;
using td::mtproto::set_transport_factory_for_tests;
using td::mtproto::stealth::default_runtime_stealth_params;
using td::mtproto::stealth::reset_runtime_stealth_params_for_tests;
using td::mtproto::stealth::IRng;
using td::mtproto::stealth::RuntimeProfileSelectionDecision;
using td::mtproto::stealth::set_runtime_stealth_params_for_tests;
using td::mtproto::stealth::set_stealth_config_factory_for_tests;
using td::mtproto::stealth::StealthConfig;
using td::mtproto::TransportType;

int g_config_factory_calls = 0;
int g_transport_factory_calls = 0;

class CapturingLog final : public td::LogInterface {
 public:
  void do_append(int log_level, td::CSlice slice) final {
    (void)log_level;
    entries.push_back(slice.str());
  }

  bool contains(td::Slice needle) const {
    auto needle_str = needle.str();
    for (const auto &entry : entries) {
      if (entry.find(needle_str) != td::string::npos) {
        return true;
      }
    }
    return false;
  }

  td::vector<td::string> entries;
};

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

td::Result<StealthConfig> invalid_stealth_config_factory_for_logs(const ProxySecret &secret, IRng &rng) {
  (void)secret;
  (void)rng;
  g_config_factory_calls++;
  return td::Status::Error("bulk_threshold_bytes is out of allowed bounds");
}

td::Result<StealthConfig> invalid_stealth_config_factory_with_secret_leak(const ProxySecret &secret, IRng &rng) {
  (void)rng;
  g_config_factory_calls++;
  std::string message = "test_secret_leak_marker=";
  message += secret.get_raw_secret().str();
  return td::Status::Error(message);
}

td::Result<StealthConfig> invalid_stealth_config_factory_with_multiline_message(const ProxySecret &secret, IRng &rng) {
  (void)secret;
  (void)rng;
  g_config_factory_calls++;
  return td::Status::Error("line1\nline2");
}

td::Result<StealthConfig> invalid_stealth_config_factory_with_non_ascii_message(const ProxySecret &secret, IRng &rng) {
  (void)secret;
  (void)rng;
  g_config_factory_calls++;
  td::string message = "prefix-";
  message.push_back(static_cast<char>(0xc3));
  message.push_back(static_cast<char>(0xa9));
  message += "-suffix";
  return td::Status::Error(message);
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

class RuntimeParamsGuard final {
 public:
  RuntimeParamsGuard() {
    reset_runtime_stealth_params_for_tests();
  }

  ~RuntimeParamsGuard() {
    reset_runtime_stealth_params_for_tests();
  }
};

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

#if !TDLIB_STEALTH_SHAPING
  // emulate_tls() transport construction must fail fast when stealth shaping is
  // compiled out. The explicit guard is enforced by analysis contract tests.
  ASSERT_EQ(0, g_transport_factory_calls);
  ASSERT_EQ(0, g_config_factory_calls);
  return;
#endif

  auto transport =
      create_transport(TransportType{TransportType::ObfuscatedTcp, 2, ProxySecret::from_raw(make_tls_secret())});

  ASSERT_EQ(1, g_transport_factory_calls);
  ASSERT_EQ(1, g_config_factory_calls);
  ASSERT_TRUE(transport->supports_tls_record_sizing());
  ASSERT_EQ(TransportType::ObfuscatedTcp, transport->get_type().type);
}

TEST(StreamTransportActivationFailClosed, InvalidRuntimeConfigFailsClosedAndLogsReason) {
#if !TDLIB_STEALTH_SHAPING
  ASSERT_TRUE(true);
  return;
#endif

  g_config_factory_calls = 0;
  auto previous_config_factory = set_stealth_config_factory_for_tests(&invalid_stealth_config_factory_for_logs);
  CapturingLog capture;
  auto *old_sink = td::load_active_log_interface();
  auto old_verbosity = GET_VERBOSITY_LEVEL();
  td::store_active_log_interface(&capture);
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(INFO));
  SCOPE_EXIT {
    SET_VERBOSITY_LEVEL(old_verbosity);
    td::store_active_log_interface(old_sink);
    set_stealth_config_factory_for_tests(previous_config_factory);
  };

  auto transport =
      create_transport(TransportType{TransportType::ObfuscatedTcp, 2, ProxySecret::from_raw(make_tls_secret())});

  ASSERT_EQ(1, g_config_factory_calls);
  // Fail-closed: a transport is still returned (the factory cannot signal an
  // error), but it must NOT be a usable un-shaped channel. It keeps the
  // ObfuscatedTcp type for upstream logging while refusing to operate: it never
  // accepts writes and fails the connection on the first read so the unmasked
  // legacy obfuscated-MTProto fingerprint is never put on the wire.
  ASSERT_EQ(TransportType::ObfuscatedTcp, transport->get_type().type);
  ASSERT_FALSE(transport->can_write());
  td::BufferSlice message;
  td::uint32 quick_ack = 0;
  ASSERT_TRUE(transport->read_next(&message, &quick_ack).is_error());
  ASSERT_TRUE(capture.contains("Stealth shaping unavailable; refusing emulate_tls transport (fail-closed)"));
  ASSERT_TRUE(capture.contains("[reason:config_validation_failed]"));
  ASSERT_TRUE(capture.contains("[dc_id:2]"));
  ASSERT_TRUE(capture.contains("[tls_emulation:true]"));
  ASSERT_TRUE(capture.contains("[status_code:"));
  ASSERT_TRUE(capture.contains("bulk_threshold_bytes is out of allowed bounds"));
}

TEST(StreamTransportActivationFailClosed, InvalidRuntimeConfigLogRedactsProxySecretMaterial) {
#if !TDLIB_STEALTH_SHAPING
  ASSERT_TRUE(true);
  return;
#endif

  g_config_factory_calls = 0;
  auto previous_config_factory = set_stealth_config_factory_for_tests(&invalid_stealth_config_factory_with_secret_leak);
  CapturingLog capture;
  auto *old_sink = td::load_active_log_interface();
  auto old_verbosity = GET_VERBOSITY_LEVEL();
  td::store_active_log_interface(&capture);
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(INFO));
  auto secret = make_tls_secret();
  SCOPE_EXIT {
    SET_VERBOSITY_LEVEL(old_verbosity);
    td::store_active_log_interface(old_sink);
    set_stealth_config_factory_for_tests(previous_config_factory);
  };

  auto transport = create_transport(TransportType{TransportType::ObfuscatedTcp, 2, ProxySecret::from_raw(secret)});

  ASSERT_EQ(1, g_config_factory_calls);
  ASSERT_EQ(TransportType::ObfuscatedTcp, transport->get_type().type);
  ASSERT_TRUE(capture.contains("Stealth shaping unavailable; refusing emulate_tls transport (fail-closed)"));
  ASSERT_TRUE(capture.contains("[reason:config_validation_failed]"));
  ASSERT_TRUE(capture.contains("[status_code:"));
  ASSERT_TRUE(capture.contains("stealth runtime config rejected"));
  ASSERT_FALSE(capture.contains("test_secret_leak_marker"));
  ASSERT_FALSE(capture.contains(secret));
}

TEST(StreamTransportActivationFailClosed, InvalidRuntimeConfigLogRejectsMultilineStatusPayloads) {
#if !TDLIB_STEALTH_SHAPING
  ASSERT_TRUE(true);
  return;
#endif

  g_config_factory_calls = 0;
  auto previous_config_factory =
      set_stealth_config_factory_for_tests(&invalid_stealth_config_factory_with_multiline_message);
  CapturingLog capture;
  auto *old_sink = td::load_active_log_interface();
  auto old_verbosity = GET_VERBOSITY_LEVEL();
  td::store_active_log_interface(&capture);
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(INFO));
  SCOPE_EXIT {
    SET_VERBOSITY_LEVEL(old_verbosity);
    td::store_active_log_interface(old_sink);
    set_stealth_config_factory_for_tests(previous_config_factory);
  };

  auto transport =
      create_transport(TransportType{TransportType::ObfuscatedTcp, 2, ProxySecret::from_raw(make_tls_secret())});

  ASSERT_EQ(1, g_config_factory_calls);
  ASSERT_EQ(TransportType::ObfuscatedTcp, transport->get_type().type);
  ASSERT_TRUE(capture.contains("Stealth shaping unavailable; refusing emulate_tls transport (fail-closed)"));
  ASSERT_TRUE(capture.contains("stealth runtime config rejected; review stealth params and proxy setup"));
  ASSERT_FALSE(capture.contains("line2"));
}

TEST(StreamTransportActivationFailClosed, InvalidRuntimeConfigLogRejectsNonAsciiStatusPayloads) {
#if !TDLIB_STEALTH_SHAPING
  ASSERT_TRUE(true);
  return;
#endif

  g_config_factory_calls = 0;
  auto previous_config_factory =
      set_stealth_config_factory_for_tests(&invalid_stealth_config_factory_with_non_ascii_message);
  CapturingLog capture;
  auto *old_sink = td::load_active_log_interface();
  auto old_verbosity = GET_VERBOSITY_LEVEL();
  td::store_active_log_interface(&capture);
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(INFO));
  SCOPE_EXIT {
    SET_VERBOSITY_LEVEL(old_verbosity);
    td::store_active_log_interface(old_sink);
    set_stealth_config_factory_for_tests(previous_config_factory);
  };

  auto transport =
      create_transport(TransportType{TransportType::ObfuscatedTcp, 2, ProxySecret::from_raw(make_tls_secret())});

  ASSERT_EQ(1, g_config_factory_calls);
  ASSERT_EQ(TransportType::ObfuscatedTcp, transport->get_type().type);
  ASSERT_TRUE(capture.contains("Stealth shaping unavailable; refusing emulate_tls transport (fail-closed)"));
  ASSERT_TRUE(capture.contains("stealth runtime config rejected; review stealth params and proxy setup"));
  ASSERT_FALSE(capture.contains("suffix"));
}
TEST(StreamTransportActivationFailClosed, SuccessfulActivationLogsEnableDecision) {
#if !TDLIB_STEALTH_SHAPING
  ASSERT_TRUE(true);
  return;
#endif

  g_config_factory_calls = 0;
  auto previous_config_factory = set_stealth_config_factory_for_tests(&counting_stealth_config_factory);
  CapturingLog capture;
  auto *old_sink = td::load_active_log_interface();
  auto old_verbosity = GET_VERBOSITY_LEVEL();
  td::store_active_log_interface(&capture);
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(INFO));
  SCOPE_EXIT {
    SET_VERBOSITY_LEVEL(old_verbosity);
    td::store_active_log_interface(old_sink);
    set_stealth_config_factory_for_tests(previous_config_factory);
  };

  auto transport =
      create_transport(TransportType{TransportType::ObfuscatedTcp, 2, ProxySecret::from_raw(make_tls_secret())});

  ASSERT_EQ(1, g_config_factory_calls);
  ASSERT_EQ(TransportType::ObfuscatedTcp, transport->get_type().type);
  ASSERT_TRUE(capture.contains("Stealth shaping enabled for emulate_tls transport"));
  ASSERT_TRUE(capture.contains("[transport:obfuscated_tcp]"));
  ASSERT_TRUE(capture.contains("[dc_id:2]"));
  ASSERT_TRUE(capture.contains("[tls_emulation:true]"));
}

TEST(StreamTransportActivationFailClosed, RotationEnabledWithoutStampedSelectionFailsClosedBeforeConfigBuild) {
#if !TDLIB_STEALTH_SHAPING
  ASSERT_TRUE(true);
  return;
#endif

  RuntimeParamsGuard runtime_guard;
  auto runtime_params = default_runtime_stealth_params();
  runtime_params.profile_rotation.enabled = true;
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(runtime_params).is_ok());

  g_config_factory_calls = 0;
  auto previous_config_factory = set_stealth_config_factory_for_tests(&counting_stealth_config_factory);
  CapturingLog capture;
  auto *old_sink = td::load_active_log_interface();
  auto old_verbosity = GET_VERBOSITY_LEVEL();
  td::store_active_log_interface(&capture);
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(INFO));
  SCOPE_EXIT {
    SET_VERBOSITY_LEVEL(old_verbosity);
    td::store_active_log_interface(old_sink);
    set_stealth_config_factory_for_tests(previous_config_factory);
  };

  auto transport =
      create_transport(TransportType{TransportType::ObfuscatedTcp, 2, ProxySecret::from_raw(make_tls_secret())});

  ASSERT_EQ(0, g_config_factory_calls);
  ASSERT_EQ(TransportType::ObfuscatedTcp, transport->get_type().type);
  ASSERT_FALSE(transport->can_write());
  td::BufferSlice message;
  td::uint32 quick_ack = 0;
  ASSERT_TRUE(transport->read_next(&message, &quick_ack).is_error());
  ASSERT_TRUE(capture.contains("Stealth shaping unavailable; refusing emulate_tls transport (fail-closed)"));
  ASSERT_TRUE(capture.contains("[reason:missing_runtime_profile_selection]"));
  ASSERT_TRUE(capture.contains("[profile_rotation_enabled:true]"));
}

TEST(StreamTransportActivationFailClosed, RotationEnabledWithStampedSelectionStillActivatesStealth) {
#if !TDLIB_STEALTH_SHAPING
  ASSERT_TRUE(true);
  return;
#endif

  RuntimeParamsGuard runtime_guard;
  auto runtime_params = default_runtime_stealth_params();
  runtime_params.profile_rotation.enabled = true;
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(runtime_params).is_ok());

  g_config_factory_calls = 0;
  auto previous_config_factory = set_stealth_config_factory_for_tests(&counting_stealth_config_factory);
  CapturingLog capture;
  auto *old_sink = td::load_active_log_interface();
  auto old_verbosity = GET_VERBOSITY_LEVEL();
  td::store_active_log_interface(&capture);
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(INFO));
  SCOPE_EXIT {
    SET_VERBOSITY_LEVEL(old_verbosity);
    td::store_active_log_interface(old_sink);
    set_stealth_config_factory_for_tests(previous_config_factory);
  };

  TransportType type{TransportType::ObfuscatedTcp, 2, ProxySecret::from_raw(make_tls_secret())};
  type.selected_runtime_profile = RuntimeProfileSelectionDecision{};
  auto transport = create_transport(type);

  ASSERT_EQ(1, g_config_factory_calls);
  ASSERT_EQ(TransportType::ObfuscatedTcp, transport->get_type().type);
  ASSERT_TRUE(transport->supports_tls_record_sizing());
  ASSERT_TRUE(capture.contains("Stealth shaping enabled for emulate_tls transport"));
}

}  // namespace
