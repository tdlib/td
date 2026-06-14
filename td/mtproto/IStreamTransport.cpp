// SPDX-FileCopyrightText: Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: BSL-1.0 AND MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/IStreamTransport.h"

#include "td/mtproto/HttpTransport.h"
#include "td/mtproto/stealth/StealthConfig.h"
#include "td/mtproto/stealth/StealthRuntimeParams.h"
#include "td/mtproto/stealth/StealthTransportDecorator.h"
#include "td/mtproto/TcpTransport.h"

#include "td/utils/logging.h"

#include <utility>

namespace td {
namespace mtproto {

namespace {

StreamTransportFactoryForTests stream_transport_factory_for_tests = nullptr;

// Returned in place of a working transport when an emulate_tls() connection was
// requested but stealth shaping could not be activated (runtime config rejected
// or decorator init failed). Fail-closed: unlike the previous downgrade path it
// never falls back to a plain tcp::ObfuscatedTransport, so the unmasked legacy
// obfuscated-MTProto fingerprint that emulate_tls was meant to hide is never put
// on the wire. write() drops outbound data and can_write() is false so the
// engine never hands it un-shaped bytes; read_next() fails the connection so the
// MTProto reconnect path tears it down (and keeps refusing) instead of silently
// using an un-shaped channel.
class FailClosedStealthTransport final : public IStreamTransport {
 public:
  explicit FailClosedStealthTransport(TransportType type) : type_(std::move(type)) {
  }

  Result<size_t> read_next(BufferSlice *message, uint32 *quick_ack) final {
    (void)message;
    (void)quick_ack;
    return Status::Error(
        "stealth shaping unavailable for emulate_tls transport; refusing to send unmasked traffic");
  }

  bool support_quick_ack() const final {
    return false;
  }

  void write(BufferWriter &&message, bool quick_ack) final {
    // Drop outbound data: emitting it here would write the very unmasked
    // obfuscated-MTProto bytes that emulate_tls was supposed to camouflage.
    (void)message;
    (void)quick_ack;
  }

  bool can_read() const final {
    // Stay readable so the connection's read loop calls read_next() and fails
    // fast instead of idling on an un-shaped channel.
    return true;
  }

  bool can_write() const final {
    return false;
  }

  void init(ChainBufferReader *input, ChainBufferWriter *output) final {
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
    return type_;
  }

  bool use_random_padding() const final {
    return false;
  }

 private:
  TransportType type_;
};

string sanitize_stealth_activation_status_message(const Status &status, const ProxySecret &secret,
                                                  Slice fallback_message) {
  auto message = status.public_message();
  if (message.empty()) {
    return fallback_message.str();
  }

  auto raw_secret = secret.get_raw_secret().str();
  if (!raw_secret.empty() && message.find(raw_secret) != string::npos) {
    return fallback_message.str();
  }

  for (auto c : message) {
    auto byte = static_cast<unsigned char>(c);
    if (byte < 0x20 || byte == 0x7f || byte > 0x7e) {
      return fallback_message.str();
    }
  }

  constexpr size_t kMaxLoggedStatusMessageBytes = 256;
  if (message.size() > kMaxLoggedStatusMessageBytes) {
    return fallback_message.str();
  }

  return message;
}

}  // namespace

unique_ptr<IStreamTransport> create_transport(TransportType type) {
  if (stream_transport_factory_for_tests != nullptr) {
    auto test_transport = stream_transport_factory_for_tests(type);
    if (test_transport != nullptr) {
      return test_transport;
    }
  }

  switch (type.type) {
    case TransportType::ObfuscatedTcp: {
      auto secret_copy = type.secret;
      auto inner = td::make_unique<tcp::ObfuscatedTransport>(type.dc_id, std::move(type.secret));
#if TDLIB_STEALTH_SHAPING
      if (secret_copy.emulate_tls()) {
        auto runtime_params = stealth::get_runtime_stealth_params_snapshot();
        if (runtime_params.profile_rotation.enabled && !type.selected_runtime_profile) {
          LOG(WARNING) << "Stealth shaping unavailable; refusing emulate_tls transport (fail-closed)"
                       << tag("reason", "missing_runtime_profile_selection") << tag("transport", "obfuscated_tcp")
                       << tag("dc_id", type.dc_id) << tag("tls_emulation", true)
                       << tag("profile_rotation_enabled", true);
          inner.reset();
          return td::make_unique<FailClosedStealthTransport>(
              TransportType{TransportType::ObfuscatedTcp, type.dc_id, std::move(secret_copy)});
        }
        auto rng = stealth::make_connection_rng();
        // Single-selection handoff: when the connection path already chose one
        // profile for this attempt, shape the transport with that exact profile so
        // it matches the emitted ClientHello (no split profile state). Otherwise
        // fall back to independent selection (legacy; coherent only while rotation
        // is disabled).
        auto config = type.selected_runtime_profile
                          ? stealth::make_transport_stealth_config(secret_copy, *rng,
                                                                   type.selected_runtime_profile.value().profile)
                          : stealth::make_transport_stealth_config(secret_copy, *rng);
        if (config.is_error()) {
          auto error = config.move_as_error();
          auto safe_status_message = sanitize_stealth_activation_status_message(
              error, secret_copy, "stealth runtime config rejected; review stealth params and proxy setup");
          LOG(WARNING) << "Stealth shaping unavailable; refusing emulate_tls transport (fail-closed)"
                       << tag("reason", "config_validation_failed") << tag("transport", "obfuscated_tcp")
                       << tag("dc_id", type.dc_id) << tag("tls_emulation", true) << tag("status_code", error.code())
                       << tag("status_message", safe_status_message);
          // Fail-closed: drop the unmasked legacy transport instead of returning it.
          inner.reset();
          return td::make_unique<FailClosedStealthTransport>(
              TransportType{TransportType::ObfuscatedTcp, type.dc_id, std::move(secret_copy)});
        }
        auto decorator = stealth::StealthTransportDecorator::create(std::move(inner), config.move_as_ok(),
                                                                    std::move(rng), stealth::make_clock());
        if (decorator.is_error()) {
          auto error = decorator.move_as_error();
          auto safe_status_message = sanitize_stealth_activation_status_message(
              error, secret_copy, "stealth decorator initialization failed; check transport capabilities");
          LOG(WARNING) << "Stealth shaping unavailable; refusing emulate_tls transport (fail-closed)"
                       << tag("reason", "decorator_init_failed") << tag("transport", "obfuscated_tcp")
                       << tag("dc_id", type.dc_id) << tag("tls_emulation", true) << tag("status_code", error.code())
                       << tag("status_message", safe_status_message);
          // Fail-closed: the decorator consumed `inner`; do not rebuild a plain
          // ObfuscatedTransport that would leak the unmasked fingerprint.
          return td::make_unique<FailClosedStealthTransport>(
              TransportType{TransportType::ObfuscatedTcp, type.dc_id, std::move(secret_copy)});
        }
        LOG(INFO) << "Stealth shaping enabled for emulate_tls transport" << tag("transport", "obfuscated_tcp")
                  << tag("dc_id", type.dc_id) << tag("tls_emulation", true);
        return decorator.move_as_ok();
      }
#else
      if (secret_copy.emulate_tls()) {
        LOG(FATAL) << "MTProto TLS-emulation proxy secret requires TDLIB_STEALTH_SHAPING=ON. "
                      "Rebuild TDLib with stealth shaping enabled to avoid legacy fallback fingerprinting.";
      }
#endif
      return std::move(inner);
    }
    case TransportType::Tcp:
      return td::make_unique<tcp::OldTransport>();
    case TransportType::Http:
      return td::make_unique<http::Transport>(type.secret.get_raw_secret().str());
  }
  UNREACHABLE();
}

StreamTransportFactoryForTests set_transport_factory_for_tests(StreamTransportFactoryForTests factory) {
  auto previous = stream_transport_factory_for_tests;
  stream_transport_factory_for_tests = factory;
  return previous;
}

}  // namespace mtproto
}  // namespace td
