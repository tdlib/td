//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/StringBuilder.h"

namespace td {

struct BackgroundFill {
  int32 top_color = 0;
  int32 bottom_color = 0;
  int32 rotation_angle = 0;
  int32 third_color = -1;
  int32 fourth_color = -1;

  BackgroundFill() = default;
  explicit BackgroundFill(int32 solid_color) : top_color(solid_color), bottom_color(solid_color) {
  }
  BackgroundFill(int32 top_color, int32 bottom_color, int32 rotation_angle)
      : top_color(top_color), bottom_color(bottom_color), rotation_angle(rotation_angle) {
  }
  BackgroundFill(int32 first_color, int32 second_color, int32 third_color, int32 fourth_color)
      : top_color(first_color), bottom_color(second_color), third_color(third_color), fourth_color(fourth_color) {
  }

  explicit BackgroundFill(const telegram_api::wallPaperSettings *settings);

  static Result<BackgroundFill> get_background_fill(Slice name);

  enum class Type : int32 { Solid, Gradient, FreeformGradient };
  Type get_type() const {
    if (third_color != -1) {
      return Type::FreeformGradient;
    }
    if (top_color == bottom_color) {
      return Type::Solid;
    }
    return Type::Gradient;
  }

  int64 get_id() const;

  bool is_dark() const;

  static bool is_valid_id(int64 id);
};

bool operator==(const BackgroundFill &lhs, const BackgroundFill &rhs);

class BackgroundType {
  enum class Type : int32 { Wallpaper, Pattern, Fill };
  Type type = Type::Fill;
  bool is_blurred = false;
  bool is_moving = false;
  int32 intensity = 0;
  BackgroundFill fill;

  friend bool operator==(const BackgroundType &lhs, const BackgroundType &rhs);

  friend StringBuilder &operator<<(StringBuilder &string_builder, const BackgroundType &type);

 public:
  BackgroundType() = default;
  BackgroundType(bool is_blurred, bool is_moving)
      : type(Type::Wallpaper), is_blurred(is_blurred), is_moving(is_moving) {
  }
  BackgroundType(bool is_moving, const BackgroundFill &fill, int32 intensity)
      : type(Type::Pattern), is_moving(is_moving), intensity(intensity), fill(fill) {
  }
  explicit BackgroundType(BackgroundFill fill) : type(Type::Fill), fill(fill) {
  }

  BackgroundType(bool is_pattern, telegram_api::object_ptr<telegram_api::wallPaperSettings> settings);

  static Result<BackgroundType> get_background_type(const td_api::BackgroundType *background_type);

  bool has_file() const {
    return type == Type::Wallpaper || type == Type::Pattern;
  }

  BackgroundFill get_background_fill();

  string get_mime_type() const;

  void apply_parameters_from_link(Slice name);

  string get_link() const;

  bool has_equal_type(const BackgroundType &other) const {
    return type == other.type;
  }

  td_api::object_ptr<td_api::BackgroundType> get_background_type_object() const;

  telegram_api::object_ptr<telegram_api::wallPaperSettings> get_input_wallpaper_settings() const;

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const BackgroundType &lhs, const BackgroundType &rhs);

StringBuilder &operator<<(StringBuilder &string_builder, const BackgroundType &type);

}  // namespace td
