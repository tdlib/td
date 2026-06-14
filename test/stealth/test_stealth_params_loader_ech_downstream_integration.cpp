// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Integration tests: StealthParamsLoader ECH route-policy downstream propagation.
//
// Existing tests in test_stealth_params_loader_runtime.cpp only verify that a
// reload publishes bulk_threshold_bytes to the runtime snapshot.
// Existing tests in test_stealth_config_runtime_params.cpp verify that
// StealthConfig picks up bulk/IPT/DRS overrides from the snapshot.
// Neither verifies the complete reload -> snapshot -> ECH route-policy ->
// TlsHelloProfileRegistry pipeline.
//
// This file covers:
//  1. After a reload that sets non_ru ECH mode to "disabled", a non-RU hello
//     must NOT contain the ECH extension.
//  2. After a subsequent reload that re-enables non_ru ECH, the extension
//     returns.
//  3. After a reload that raises ech_failure_threshold to 10, the circuit
//     breaker does NOT trip on 3 failures.
//  4. A reload with an invalid config (bulk_threshold too small) must fail
//     closed and leave the prior valid snapshot unchanged.

#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/StealthParamsLoader.h"
#include "td/mtproto/stealth/StealthRuntimeParams.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/port/FileFd.h"
#include "td/utils/port/path.h"
#include "td/utils/port/PollFlags.h"
#include "td/utils/port/SocketFd.h"
#include "td/utils/Status.h"
#include "td/utils/Time.h"

#include "td/mtproto/TlsInit.h"

#include "test/stealth/FingerprintFixtures.h"
#include "test/stealth/TlsHelloParsers.h"
#include "test/stealth/TlsInitTestHelpers.h"
#include "test/stealth/TlsInitTestPeer.h"

#include "td/utils/tests.h"

#include "td/utils/port/config.h"

#if TD_PORT_POSIX

#if !TD_DARWIN

namespace {

using td::FileFd;
using td::mtproto::stealth::default_runtime_platform_hints;
using td::mtproto::stealth::get_runtime_stealth_params_snapshot;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::stealth::note_runtime_ech_failure;
using td::mtproto::stealth::pick_runtime_profile;
using td::mtproto::stealth::profile_spec;
using td::mtproto::stealth::reset_runtime_ech_counters_for_tests;
using td::mtproto::stealth::reset_runtime_ech_failure_state_for_tests;
using td::mtproto::stealth::reset_runtime_stealth_params_for_tests;
using td::mtproto::stealth::StealthParamsLoader;
using td::mtproto::stealth::StealthRuntimeParams;
using td::mtproto::test::create_socket_pair;
using td::mtproto::test::find_extension;
using td::mtproto::test::parse_tls_client_hello;
using td::mtproto::test::TlsInitTestPeer;
using td::mtproto::TlsInit;

constexpr td::Slice kSecret("0123456789secret");
// Full valid config with non_ru ECH enabled.
constexpr td::Slice kConfigEchEnabled =
    "{"
    "\"version\":1,"
    "\"profile_weights\":{"
    "\"chrome133\":50,\"chrome131\":20,\"chrome120\":15,\"firefox148\":15,"
    "\"safari26_3\":20,\"ios14\":70,\"android11_okhttp_advisory\":30},"
    "\"route_policy\":{"
    "\"unknown\":{\"ech_mode\":\"disabled\"},"
    "\"ru\":{\"ech_mode\":\"disabled\"},"
    "\"non_ru\":{\"ech_mode\":\"rfc9180_outer\"}},"
    "\"route_failure\":{"
    "\"ech_failure_threshold\":3,\"ech_disable_ttl_seconds\":300.0,\"persist_across_restart\":true},"
    "\"bulk_threshold_bytes\":12288}";

// Same config but with non_ru ECH mode set to "disabled".
constexpr td::Slice kConfigEchDisabled =
    "{"
    "\"version\":1,"
    "\"profile_weights\":{"
    "\"chrome133\":50,\"chrome131\":20,\"chrome120\":15,\"firefox148\":15,"
    "\"safari26_3\":20,\"ios14\":70,\"android11_okhttp_advisory\":30},"
    "\"route_policy\":{"
    "\"unknown\":{\"ech_mode\":\"disabled\"},"
    "\"ru\":{\"ech_mode\":\"disabled\"},"
    "\"non_ru\":{\"ech_mode\":\"disabled\"}},"
    "\"route_failure\":{"
    "\"ech_failure_threshold\":3,\"ech_disable_ttl_seconds\":300.0,\"persist_across_restart\":true},"
    "\"bulk_threshold_bytes\":12288}";

// Config with high threshold=10.
constexpr td::Slice kConfigHighThreshold =
    "{"
    "\"version\":1,"
    "\"profile_weights\":{"
    "\"chrome133\":50,\"chrome131\":20,\"chrome120\":15,\"firefox148\":15,"
    "\"safari26_3\":20,\"ios14\":70,\"android11_okhttp_advisory\":30},"
    "\"route_policy\":{"
    "\"unknown\":{\"ech_mode\":\"disabled\"},"
    "\"ru\":{\"ech_mode\":\"disabled\"},"
    "\"non_ru\":{\"ech_mode\":\"rfc9180_outer\"}},"
    "\"route_failure\":{"
    "\"ech_failure_threshold\":10,\"ech_disable_ttl_seconds\":300.0,\"persist_across_restart\":true},"
    "\"bulk_threshold_bytes\":12288}";

// Invalid config: bulk_threshold_bytes=128 is below min valid value.
constexpr td::Slice kConfigInvalid =
    "{"
    "\"version\":1,"
    "\"profile_weights\":{"
    "\"chrome133\":50,\"chrome131\":20,\"chrome120\":15,\"firefox148\":15,"
    "\"safari26_3\":20,\"ios14\":70,\"android11_okhttp_advisory\":30},"
    "\"route_policy\":{"
    "\"unknown\":{\"ech_mode\":\"disabled\"},"
    "\"ru\":{\"ech_mode\":\"disabled\"},"
    "\"non_ru\":{\"ech_mode\":\"rfc9180_outer\"}},"
    "\"route_failure\":{"
    "\"ech_failure_threshold\":3,\"ech_disable_ttl_seconds\":300.0,\"persist_across_restart\":true},"
    "\"bulk_threshold_bytes\":128}";

class RuntimeParamsGuard final {
 public:
  RuntimeParamsGuard() {
    reset_runtime_stealth_params_for_tests();
    reset_runtime_ech_failure_state_for_tests();
    reset_runtime_ech_counters_for_tests();
  }
  ~RuntimeParamsGuard() {
    reset_runtime_stealth_params_for_tests();
    reset_runtime_ech_failure_state_for_tests();
    reset_runtime_ech_counters_for_tests();
  }
};

class ScopedTempDir final {
 public:
  ScopedTempDir() {
    dir_ = td::mkdtemp(td::get_temporary_dir(), "stealth-ech-downstream").move_as_ok();
  }
  ~ScopedTempDir() {
    td::rmrf(dir_).ignore();
  }
  td::Slice path() const {
    return dir_;
  }

 private:
  td::string dir_;
};

void write_config(td::Slice path, td::Slice content) {
  auto file = FileFd::open(path.str(), FileFd::Write | FileFd::Create | FileFd::Truncate, 0600).move_as_ok();
  ASSERT_EQ(content.size(), file.write(content).move_as_ok());
  ASSERT_TRUE(file.sync().is_ok());
}

td::string make_path(td::Slice dir, td::Slice name) {
  td::string result = dir.str();
  result += TD_DIR_SLASH;
  result += name.str();
  return result;
}

struct EchCandidate final {
  td::string domain;
  td::int32 unix_time{0};
};

EchCandidate find_ech_enabled_candidate() {
  auto platform = default_runtime_platform_hints();
  for (td::uint32 bucket = 20000; bucket < 20512; bucket++) {
    auto unix_time = static_cast<td::int32>(bucket * 86400 + 1800);
    for (td::uint32 i = 0; i < 256; i++) {
      td::string domain = "ech-dl-" + td::to_string(i) + ".example.com";
      auto profile = pick_runtime_profile(domain, unix_time, platform);
      if (profile_spec(profile).allows_ech) {
        return EchCandidate{std::move(domain), unix_time};
      }
    }
  }
  UNREACHABLE();
  return EchCandidate{};
}

class NoopCallback final : public td::TransparentProxy::Callback {
 public:
  void set_result(td::Result<td::BufferedFd<td::SocketFd>>) final {
  }
  void on_connected() final {
  }
};

TlsInit create_non_ru_tls_init(td::SocketFd fd, td::Slice domain, td::int32 unix_time) {
  NetworkRouteHints route;
  route.is_known = true;
  route.is_ru = false;
  auto diff = static_cast<double>(unix_time) - td::Time::now();
  return TlsInit(std::move(fd), domain.str(), kSecret.str(), td::make_unique<NoopCallback>(), {}, diff, route);
}

td::string flush_hello(TlsInit &tls_init, td::SocketFd &peer_fd) {
  auto bytes_to_read = TlsInitTestPeer::fd(tls_init).ready_for_flush_write();
  CHECK(bytes_to_read > 0);
  TlsInitTestPeer::fd(tls_init).get_poll_info().add_flags(td::PollFlags::Write());
  while (TlsInitTestPeer::fd(tls_init).ready_for_flush_write() > 0) {
    CHECK(TlsInitTestPeer::fd(tls_init).flush_write().is_ok());
  }
  return td::mtproto::test::read_exact(peer_fd, bytes_to_read).move_as_ok();
}

bool wire_has_ech(const td::string &wire) {
  auto parsed = parse_tls_client_hello(wire);
  if (parsed.is_error()) {
    return false;
  }
  return find_extension(parsed.ok(), td::mtproto::test::fixtures::kEchExtensionType) != nullptr;
}

// ----- Tests ----------------------------------------------------------------

// After a loader reload that sets non_ru ECH mode to "disabled", new TlsInit
// hellos (non-RU route) must not include the ECH extension. A subsequent
// reload with ECH re-enabled must restore the extension.
TEST(StealthParamsLoaderEchDownstream, ReloadNonRuEchDisabledSuppressesExtension) {
  SKIP_IF_NO_SOCKET_PAIR();
  ScopedTempDir tmp;
  RuntimeParamsGuard guard;
  auto cand = find_ech_enabled_candidate();
  auto path = make_path(tmp.path(), "stealth.json");

  // Load config with ECH enabled.
  write_config(path, kConfigEchEnabled);
  StealthParamsLoader loader(path);
  ASSERT_TRUE(loader.try_reload());
  ASSERT_EQ(static_cast<size_t>(12288), get_runtime_stealth_params_snapshot().bulk_threshold_bytes);

  // Non-RU hello must have ECH extension.
  {
    auto pair = create_socket_pair().move_as_ok();
    auto tls = create_non_ru_tls_init(std::move(pair.client), cand.domain, cand.unix_time);
    TlsInitTestPeer::send_hello(tls);
    auto wire = flush_hello(tls, pair.peer);
    ASSERT_TRUE(wire_has_ech(wire));
  }

  // Reload with ECH disabled.
  write_config(path, kConfigEchDisabled);
  ASSERT_TRUE(loader.try_reload());

  // Non-RU hello must NOT have ECH extension after reload.
  {
    auto pair = create_socket_pair().move_as_ok();
    auto tls = create_non_ru_tls_init(std::move(pair.client), cand.domain, cand.unix_time);
    TlsInitTestPeer::send_hello(tls);
    auto wire = flush_hello(tls, pair.peer);
    ASSERT_FALSE(wire_has_ech(wire));
  }

  // Reload with ECH re-enabled.
  write_config(path, kConfigEchEnabled);
  ASSERT_TRUE(loader.try_reload());

  // Extension must return.
  {
    auto pair = create_socket_pair().move_as_ok();
    auto tls = create_non_ru_tls_init(std::move(pair.client), cand.domain, cand.unix_time);
    TlsInitTestPeer::send_hello(tls);
    auto wire = flush_hello(tls, pair.peer);
    ASSERT_TRUE(wire_has_ech(wire));
  }
}

// After a loader reload that raises ech_failure_threshold to 10, the circuit
// breaker must NOT trip on 3 failures (the old default threshold).
TEST(StealthParamsLoaderEchDownstream, ReloadHighThresholdDelaysCircuitBreakerTrip) {
  SKIP_IF_NO_SOCKET_PAIR();
  ScopedTempDir tmp;
  RuntimeParamsGuard guard;
  auto cand = find_ech_enabled_candidate();
  auto path = make_path(tmp.path(), "stealth.json");

  // Load config with high threshold=10.
  write_config(path, kConfigHighThreshold);
  StealthParamsLoader loader(path);
  ASSERT_TRUE(loader.try_reload());
  ASSERT_EQ(10u, get_runtime_stealth_params_snapshot().route_failure.ech_failure_threshold);

  // Drive 3 ECH failures (the old default threshold=3) directly.
  for (int i = 0; i < 3; i++) {
    note_runtime_ech_failure(cand.domain, cand.unix_time);
  }

  // With threshold=10, ECH must still be enabled after only 3 failures.
  {
    auto pair = create_socket_pair().move_as_ok();
    auto tls = create_non_ru_tls_init(std::move(pair.client), cand.domain, cand.unix_time);
    TlsInitTestPeer::send_hello(tls);
    auto wire = flush_hello(tls, pair.peer);
    ASSERT_TRUE(wire_has_ech(wire));
  }

  // Drive failures up to exactly threshold=10 and verify breaker trips.
  for (int i = 3; i < 10; i++) {
    note_runtime_ech_failure(cand.domain, cand.unix_time);
  }
  {
    auto pair = create_socket_pair().move_as_ok();
    auto tls = create_non_ru_tls_init(std::move(pair.client), cand.domain, cand.unix_time);
    TlsInitTestPeer::send_hello(tls);
    auto wire = flush_hello(tls, pair.peer);
    ASSERT_FALSE(wire_has_ech(wire));
  }
}

// A reload with an invalid config must fail closed (loader returns false) and
// the prior valid runtime snapshot must remain unchanged.
TEST(StealthParamsLoaderEchDownstream, InvalidConfigReloadFailsClosedPreservingPriorSnapshot) {
  ScopedTempDir tmp;
  RuntimeParamsGuard guard;
  auto path = make_path(tmp.path(), "stealth.json");

  // Load a valid config first: sets bulk_threshold_bytes=12288.
  write_config(path, kConfigEchEnabled);
  StealthParamsLoader loader(path);
  ASSERT_TRUE(loader.try_reload());
  ASSERT_EQ(static_cast<size_t>(12288), get_runtime_stealth_params_snapshot().bulk_threshold_bytes);

  // Overwrite with an invalid config and attempt reload.
  write_config(path, kConfigInvalid);
  auto ok = loader.try_reload();
  // try_reload must return false on validation failure.
  ASSERT_FALSE(ok);

  // The snapshot must still reflect the last valid value.
  ASSERT_EQ(static_cast<size_t>(12288), get_runtime_stealth_params_snapshot().bulk_threshold_bytes);
}

}  // namespace

#endif  // !TD_DARWIN
#endif  // TD_PORT_POSIX
