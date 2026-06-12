// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Shared, side-effect-free helpers for the adaptive runtime profile rotation
// tests. Each helper has internal linkage via inline; the RAII guard resets all
// global rotation/selection state so test cases stay independent.

#pragma once

#include "td/mtproto/BrowserProfile.h"
#include "td/mtproto/stealth/StealthRuntimeParams.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/Slice.h"
#include "td/utils/Span.h"

namespace td {
namespace mtproto {
namespace stealth {
namespace rotation_test {

using BrowserProfile = ::td::mtproto::BrowserProfile;

class RotationTestGuard final {
 public:
  RotationTestGuard() {
    reset();
  }
  ~RotationTestGuard() {
    reset();
  }

 private:
  static void reset() {
    reset_runtime_stealth_params_for_tests();
    reset_runtime_profile_quarantine_state_for_tests();
    reset_runtime_profile_rotation_counters_for_tests();
    reset_per_install_selection_salt_for_tests();
  }
};

inline RuntimePlatformHints ios_platform() {
  return RuntimePlatformHints{DeviceClass::Mobile, MobileOs::IOS, DesktopOs::Unknown};
}
inline RuntimePlatformHints android_platform() {
  return RuntimePlatformHints{DeviceClass::Mobile, MobileOs::Android, DesktopOs::Unknown};
}
inline RuntimePlatformHints linux_platform() {
  return RuntimePlatformHints{DeviceClass::Desktop, MobileOs::None, DesktopOs::Linux};
}
inline RuntimePlatformHints windows_platform() {
  return RuntimePlatformHints{DeviceClass::Desktop, MobileOs::None, DesktopOs::Windows};
}
inline RuntimePlatformHints darwin_platform() {
  return RuntimePlatformHints{DeviceClass::Desktop, MobileOs::None, DesktopOs::Darwin};
}

inline bool platform_allows(const RuntimePlatformHints &platform, BrowserProfile profile) {
  for (auto allowed : allowed_profiles_for_platform(platform)) {
    if (allowed == profile) {
      return true;
    }
  }
  return false;
}

// Struct-default weights keep every platform lane non-zero, so the params stay
// valid across a platform override (validation requires only a positive
// allowed/TlsOnly/release weight for the platform, not a policy sum).
inline StealthRuntimeParams rotation_params(const RuntimePlatformHints &platform, TransportConfidence confidence,
                                            bool release_gating, bool rotation_enabled, uint32 threshold = 2) {
  auto params = default_runtime_stealth_params();
  params.platform_hints = platform;
  params.profile_weights = ProfileWeights{};
  params.transport_confidence = confidence;
  params.release_mode_profile_gating = release_gating;
  params.profile_rotation.enabled = rotation_enabled;
  params.profile_rotation.failure_threshold = threshold;
  return params;
}

// Quarantines one wire variant by recording `threshold` malformed-hello failures.
inline void quarantine_variant(Slice destination, BrowserProfile profile, bool hello_uses_ech, uint32 threshold) {
  for (uint32 i = 0; i < threshold; i++) {
    note_runtime_profile_failure(destination, RuntimeProfileWireVariant{profile, hello_uses_ech},
                                 RuntimeProfileFailureSignal::MalformedHelloResponse);
  }
}

}  // namespace rotation_test
}  // namespace stealth
}  // namespace mtproto
}  // namespace td
