//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
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
  explicit StealthParamsLoader(string config_path);

  static Result<StealthRuntimeParams> try_load_strict(Slice config_path) noexcept;

  bool try_reload() noexcept;

  StealthRuntimeParams get_snapshot() const noexcept;

 private:
  static constexpr size_t kMaxConfigBytes = 64 * 1024;
  static constexpr size_t kReloadErrorCooldownThreshold = 5;
  static constexpr double kReloadErrorCooldownSeconds = 60.0;

  static Result<string> read_file_secure(const string &path) noexcept;
  static Result<StealthRuntimeParams> parse_and_validate(string content) noexcept;

  string config_path_;
  mutable std::mutex reload_mu_;
  std::shared_ptr<const StealthRuntimeParams> current_;
  Timestamp reload_cooldown_until_;
  size_t consecutive_reload_failures_{0};
};

}  // namespace stealth
}  // namespace mtproto
}  // namespace td