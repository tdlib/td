// SPDX-FileCopyrightText: Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: BSL-1.0 AND MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
#pragma once

#include "td/mtproto/stealth/StealthRuntimeParams.h"

#include "td/utils/Time.h"

#include <memory>
#include <mutex>

namespace td {
namespace mtproto {
namespace stealth {

class StealthParamsLoader final {
 public:
  enum class ConfigPresenceRequirement : uint8 {
    Optional = 0,
    Required = 1,
  };

  struct LoadPolicy final {
    ConfigPresenceRequirement config_presence_requirement{ConfigPresenceRequirement::Optional};
  };

  explicit StealthParamsLoader(string config_path);

  static Result<StealthRuntimeParams> try_load_strict(Slice config_path) noexcept;
  static Result<StealthRuntimeParams> try_load_strict(Slice config_path, LoadPolicy policy) noexcept;

  bool try_reload() noexcept;

  StealthRuntimeParams get_snapshot() const noexcept;

 private:
  static constexpr size_t kMaxConfigBytes = 64 * 1024;
  static constexpr size_t kReloadErrorCooldownThreshold = 5;
  static constexpr double kReloadErrorCooldownSeconds = 60.0;

  static Result<string> read_file_secure(const string &path) noexcept;
  static Result<StealthRuntimeParams> parse_and_validate(string content) noexcept;

  string config_path_;
  mutable std::mutex current_mu_;
  std::shared_ptr<const StealthRuntimeParams> current_;
  mutable std::mutex reload_mu_;
  Timestamp reload_cooldown_until_;
  size_t consecutive_reload_failures_{0};
  bool has_successful_reload_{false};
};

}  // namespace stealth
}  // namespace mtproto
}  // namespace td
