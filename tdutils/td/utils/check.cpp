// SPDX-FileCopyrightText: Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: BSL-1.0 AND MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
#include "td/utils/check.h"

#include "td/utils/logging.h"
#include "td/utils/Slice.h"
#include "td/utils/SliceBuilder.h"

namespace td {
namespace detail {

void process_check_error(const char *message, const char *file, int line) {
  ::td::Logger(*load_active_log_interface(), log_options, VERBOSITY_NAME(FATAL), Slice(file), line, Slice())
      << "Check `" << message << "` failed";
  ::td::process_fatal_error(PSLICE() << "Check `" << message << "` failed in " << file << " at " << line << '\n');
}

}  // namespace detail
}  // namespace td
