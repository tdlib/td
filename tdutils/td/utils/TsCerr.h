// SPDX-FileCopyrightText: Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: BSL-1.0 AND MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
#pragma once

#include <atomic>

namespace td {

class Slice;

class TsCerr {
 public:
  TsCerr();
  TsCerr(const TsCerr &) = delete;
  TsCerr &operator=(const TsCerr &) = delete;
  TsCerr(TsCerr &&) = delete;
  TsCerr &operator=(TsCerr &&) = delete;
  ~TsCerr();

  TsCerr &operator<<(Slice slice);

 private:
  bool lock_is_acquired_{false};

  static std::atomic_flag &lock();
  static bool enterCritical();
  static void exitCritical();
};

}  // namespace td
