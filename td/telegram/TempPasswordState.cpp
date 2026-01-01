//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/TempPasswordState.h"

#include "td/telegram/Global.h"

namespace td {

td_api::object_ptr<td_api::temporaryPasswordState> TempPasswordState::get_temporary_password_state_object() const {
  auto unix_time = G()->unix_time();
  if (!has_temp_password || valid_until <= unix_time) {
    return make_tl_object<td_api::temporaryPasswordState>(false, 0);
  }
  return make_tl_object<td_api::temporaryPasswordState>(true, valid_until - unix_time);
}

}  // namespace td
