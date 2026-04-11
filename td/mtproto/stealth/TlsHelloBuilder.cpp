// SPDX-FileCopyrightText: Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: BSL-1.0 AND MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/stealth/TlsHelloBuilder.h"

#include "td/mtproto/BrowserProfile.h"
#include "td/mtproto/ClientHelloExecutor.h"
#include "td/mtproto/ClientHelloOpMapper.h"
#include "td/mtproto/ProxySecret.h"

#include "td/utils/common.h"
#include "td/utils/Random.h"

namespace td {
namespace mtproto {
namespace stealth {
namespace {

class SecureRng final : public IRng {
 public:
  void fill_secure_bytes(MutableSlice dest) final {
    Random::secure_bytes(dest);
  }

  uint32 secure_uint32() final {
    return Random::secure_uint32();
  }

  uint32 bounded(uint32 n) final {
    CHECK(n > 0);

    auto threshold = static_cast<uint32>(-n) % n;
    while (true) {
      auto value = secure_uint32();
      if (value >= threshold) {
        return value % n;
      }
    }
  }
};

bool should_enable_ech(const NetworkRouteHints &route_hints) {
  return route_hints.is_known && !route_hints.is_ru;
}

mtproto::ExecutorConfig make_config(const ProfileSpec &spec, bool enable_ech, IRng &rng) {
  mtproto::ExecutorConfig config;
  config.grease_value_count = 7;
  config.has_ech = enable_ech;
  config.ech_payload_length = 144 + static_cast<int>(rng.bounded(4u) * 32u);
  config.ech_enc_key_length = 32;
  config.alps_type = spec.alps_type;
  // Per-build padding target entropy: 0..255 bytes added to the static
  // `padding_to_target` op so that ECH-disabled wires still vary in
  // length across builds, instead of collapsing into a single fixed
  // fingerprint that DPI can hash.
  config.padding_target_entropy = static_cast<int>(rng.bounded(256u));
  return config;
}

string build_tls_hello_impl(string domain, Slice secret, int32 unix_time, BrowserProfile profile_id,
                           EchMode ech_mode, IRng &rng, bool proxy_mode) {
  CHECK(!domain.empty());
  CHECK(secret.size() == 16);

  auto &spec = profile_spec(profile_id);
  auto enable_ech = ech_mode == EchMode::Rfc9180Outer && spec.allows_ech;
  auto config = make_config(spec, enable_ech, rng);
  // Proxy paths suppress `h2` from ALPN and advertise `http/1.1` only.
  // The post-handshake transport carries raw MTProto bytes and cannot
  // honour HTTP/2 framing semantics, so an `h2` ALPN advertisement
  // would create a detectable L7 mismatch.
  config.force_http11_only_alpn = proxy_mode;
  auto &profile = mtproto::get_profile_spec(profile_id);
  auto ops = mtproto::ClientHelloOpMapper::map(profile, config);
  auto result = mtproto::ClientHelloExecutor::execute(ops, domain, secret, unix_time, config, rng);
  CHECK(result.is_ok());
  return result.move_as_ok();
}

string build_default_hello_impl(string domain, Slice secret, int32 unix_time,
                               const NetworkRouteHints &route_hints, IRng &rng) {
  CHECK(!domain.empty());
  CHECK(secret.size() == 16);

  auto enable_ech = should_enable_ech(route_hints);
  auto &profile = mtproto::get_profile_spec(BrowserProfile::Chrome133);
  mtproto::ExecutorConfig config;
  config.grease_value_count = 7;
  config.has_ech = enable_ech;
  config.ech_payload_length = 144 + static_cast<int>(rng.bounded(4u) * 32u);
  config.ech_enc_key_length = 32;
  config.alps_type = 0x44CD;
  config.padding_target_entropy = static_cast<int>(rng.bounded(256u));

  auto ops = mtproto::ClientHelloOpMapper::map(profile, config);
  auto result = mtproto::ClientHelloExecutor::execute(ops, domain, secret, unix_time, config, rng);
  CHECK(result.is_ok());
  return result.move_as_ok();
}

}  // namespace

namespace detail {

string build_default_tls_client_hello_with_options(string domain, Slice secret, int32 unix_time,
                                                 const NetworkRouteHints &route_hints, IRng &rng,
                                                 const TlsHelloBuildOptions &options) {
  CHECK(!domain.empty());
  CHECK(secret.size() == 16);

  auto enable_ech = should_enable_ech(route_hints);
  auto &profile = mtproto::get_profile_spec(BrowserProfile::Chrome133);
  mtproto::ExecutorConfig config;
  config.grease_value_count = 7;
  config.has_ech = enable_ech;
  config.ech_payload_length = options.ech_payload_length;
  config.ech_enc_key_length = options.ech_enc_key_length;
  config.alps_type = options.alps_extension_type;
  // Plumb the optional PQ group override through to the executor so the
  // hybrid key_share entry and the supported_groups list both reflect the
  // requested codepoint. Without this assignment the field is silently
  // dropped and the executor always emits the canonical 0x11EC.
  config.pq_group_id_override = options.pq_group_id;
  // Plumb the optional explicit padding-extension body length the same way.
  // When non-zero, the executor emits a fixed `0x0015` extension whose body
  // is exactly this many zero bytes, ignoring the normal target+entropy
  // logic — used by `TlsContextEntropy::ExplicitPaddingExtensionLengthIsHonored`
  // and similar test scenarios.
  config.padding_extension_payload_length_override = options.padding_extension_payload_length;

  auto ops = mtproto::ClientHelloOpMapper::map(profile, config);
  auto result = mtproto::ClientHelloExecutor::execute(ops, domain, secret, unix_time, config, rng);
  CHECK(result.is_ok());
  return result.move_as_ok();
}

}  // namespace detail

string build_tls_client_hello_for_profile(string domain, Slice secret, int32 unix_time, BrowserProfile profile,
                                         EchMode ech_mode, IRng &rng) {
  return build_tls_hello_impl(std::move(domain), secret, unix_time, profile, ech_mode, rng, /*proxy_mode=*/false);
}

string build_tls_client_hello_for_profile(string domain, Slice secret, int32 unix_time, BrowserProfile profile,
                                         EchMode ech_mode) {
  SecureRng rng;
  return build_tls_hello_impl(std::move(domain), secret, unix_time, profile, ech_mode, rng, /*proxy_mode=*/false);
}

string build_proxy_tls_client_hello_for_profile(string domain, Slice secret, int32 unix_time, BrowserProfile profile,
                                               EchMode ech_mode, IRng &rng) {
  return build_tls_hello_impl(std::move(domain), secret, unix_time, profile, ech_mode, rng, /*proxy_mode=*/true);
}

string build_proxy_tls_client_hello_for_profile(string domain, Slice secret, int32 unix_time, BrowserProfile profile,
                                               EchMode ech_mode) {
  SecureRng rng;
  return build_tls_hello_impl(std::move(domain), secret, unix_time, profile, ech_mode, rng, /*proxy_mode=*/true);
}

string build_default_tls_client_hello(string domain, Slice secret, int32 unix_time,
                                       const NetworkRouteHints &route_hints, IRng &rng) {
  return build_default_hello_impl(std::move(domain), secret, unix_time, route_hints, rng);
}

string build_proxy_tls_client_hello(string domain, Slice secret, int32 unix_time, const NetworkRouteHints &route_hints,
                                     IRng &rng) {
  return build_default_hello_impl(std::move(domain), secret, unix_time, route_hints, rng);
}

string build_runtime_tls_client_hello(string domain, Slice secret, int32 unix_time,
                                       const NetworkRouteHints &route_hints, IRng &rng) {
  CHECK(!domain.empty());
  CHECK(secret.size() == 16);

#if TD_DARWIN
  return build_default_tls_client_hello(std::move(domain), secret, unix_time, route_hints, rng);
#else
  auto platform = default_runtime_platform_hints();
  auto profile = pick_runtime_profile(domain, unix_time, platform);
  auto ech_mode = get_runtime_ech_decision(domain, unix_time, route_hints).ech_mode;
  // Runtime path is the production proxy hot path; force ALPN to
  // `http/1.1` only to match the proxy contract.
  return build_tls_hello_impl(std::move(domain), secret, unix_time, profile, ech_mode, rng, /*proxy_mode=*/true);
#endif
}

string build_runtime_tls_client_hello(string domain, Slice secret, int32 unix_time,
                                       const NetworkRouteHints &route_hints) {
  SecureRng rng;
  return build_runtime_tls_client_hello(std::move(domain), secret, unix_time, route_hints, rng);
}

string build_default_tls_client_hello(string domain, Slice secret, int32 unix_time,
                                       const NetworkRouteHints &route_hints) {
  SecureRng rng;
  return build_default_tls_client_hello(std::move(domain), secret, unix_time, route_hints, rng);
}

string build_proxy_tls_client_hello(string domain, Slice secret, int32 unix_time,
                                     const NetworkRouteHints &route_hints) {
  SecureRng rng;
  return build_proxy_tls_client_hello(std::move(domain), secret, unix_time, route_hints, rng);
}

string build_default_tls_client_hello(string domain, Slice secret, int32 unix_time) {
  NetworkRouteHints default_route_hints;
  default_route_hints.is_known = false;
  default_route_hints.is_ru = false;
  return build_default_tls_client_hello(std::move(domain), secret, unix_time, default_route_hints);
}

}  // namespace stealth
}  // namespace mtproto
}  // namespace td
