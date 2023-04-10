//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/BackgroundInfo.h"

#include "td/telegram/BackgroundManager.h"
#include "td/telegram/Td.h"

namespace td {

BackgroundInfo::BackgroundInfo(Td *td, telegram_api::object_ptr<telegram_api::WallPaper> &&wallpaper_ptr) {
  auto background =
      td->background_manager_->on_get_background(BackgroundId(), string(), std::move(wallpaper_ptr), false);
  background_id_ = background.first;
  background_type_ = std::move(background.second);
}

td_api::object_ptr<td_api::background> BackgroundInfo::get_background_object(const Td *td) const {
  return td->background_manager_->get_background_object(background_id_, false, &background_type_);
}

}  // namespace td
