//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/Status.h"
#include "td/utils/StringBuilder.h"

namespace td {

struct BackgroundFill {
  int32 top_color = 0;
  int32 bottom_color = 0;
  int32 rotation_angle = 0;

  BackgroundFill() = default;
  explicit BackgroundFill(int32 solid_color) : top_color(solid_color), bottom_color(solid_color) {
  }
  BackgroundFill(int32 top_color, int32 bottom_color, int32 rotation_angle)
      : top_color(top_color), bottom_color(bottom_color), rotation_angle(rotation_angle) {
  }

  bool is_solid() const {
    return top_color == bottom_color;
  }

  int64 get_id() const;

  static bool is_valid_id(int64 id);

  static bool is_valid_rotation_angle(int32 rotation_angle) {
    return 0 <= rotation_angle && rotation_angle < 360 && rotation_angle % 45 == 0;
  }
};

bool operator==(const BackgroundFill &lhs, const BackgroundFill &rhs);

struct BackgroundType {
  enum class Type : int32 { Wallpaper, Pattern, Fill };
  Type type = Type::Fill;
  bool is_blurred = false;
  bool is_moving = false;
  int32 intensity = 0;
  BackgroundFill fill;

  BackgroundType() = default;
  BackgroundType(bool is_blurred, bool is_moving)
      : type(Type::Wallpaper), is_blurred(is_blurred), is_moving(is_moving) {
  }
  BackgroundType(bool is_moving, const BackgroundFill &fill, int32 intensity)
      : type(Type::Pattern), is_moving(is_moving), intensity(intensity), fill(fill) {
  }
  explicit BackgroundType(BackgroundFill fill) : type(Type::Fill), fill(fill) {
  }

  bool is_server() const {
    return type == Type::Wallpaper || type == Type::Pattern;
  }

  string get_link() const;
};

bool operator==(const BackgroundType &lhs, const BackgroundType &rhs);

StringBuilder &operator<<(StringBuilder &string_builder, const BackgroundType &type);

Result<BackgroundType> get_background_type(const td_api::BackgroundType *type);

BackgroundType get_background_type(bool is_pattern, telegram_api::object_ptr<telegram_api::wallPaperSettings> settings);

td_api::object_ptr<td_api::BackgroundType> get_background_type_object(const BackgroundType &type);

telegram_api::object_ptr<telegram_api::wallPaperSettings> get_input_wallpaper_settings(const BackgroundType &type);

}  // namespace td
