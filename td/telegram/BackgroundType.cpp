//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/BackgroundType.h"

#include "td/utils/logging.h"
#include "td/utils/Slice.h"

namespace td {

static string get_color_hex_string(int32 color) {
  string result;
  for (int i = 20; i >= 0; i -= 4) {
    result += "0123456789abcdef"[(color >> i) & 0xf];
  }
  return result;
}

static BackgroundFill get_background_fill(const td_api::BackgroundFill *fill) {
  CHECK(fill != nullptr);
  switch (fill->get_id()) {
    case td_api::backgroundFillSolid::ID: {
      auto solid = static_cast<const td_api::backgroundFillSolid *>(fill);
      return BackgroundFill(solid->color_);
    }
    case td_api::backgroundFillGradient::ID: {
      auto gradient = static_cast<const td_api::backgroundFillGradient *>(fill);
      return BackgroundFill(gradient->top_color_, gradient->bottom_color_, gradient->rotation_angle_);
    }
    default:
      UNREACHABLE();
      return {};
  }
}

static string get_background_fill_color_hex_string(const BackgroundFill &fill, bool is_first) {
  if (fill.is_solid()) {
    return get_color_hex_string(fill.top_color);
  } else {
    string colors = PSTRING() << get_color_hex_string(fill.top_color) << '-' << get_color_hex_string(fill.bottom_color);
    if (fill.rotation_angle != 0) {
      colors += (PSTRING() << (is_first ? '?' : '&') << "rotation=" << fill.rotation_angle);
    }
    return colors;
  }
}

static bool is_valid_color(int32 color) {
  return 0 <= color && color <= 0xFFFFFF;
}

static bool is_valid_intensity(int32 intensity) {
  return 0 <= intensity && intensity <= 100;
}

int64 BackgroundFill::get_id() const {
  CHECK(is_valid_color(top_color));
  CHECK(is_valid_color(bottom_color));
  CHECK(is_valid_rotation_angle(rotation_angle));
  if (is_solid()) {
    return static_cast<int64>(top_color) + 1;
  }
  return (rotation_angle / 45) * 0x1000001000001 + (static_cast<int64>(top_color) << 24) + bottom_color + (1 << 24) + 1;
}

bool BackgroundFill::is_valid_id(int64 id) {
  return 0 < id && id < 0x8000008000008;
}

bool operator==(const BackgroundFill &lhs, const BackgroundFill &rhs) {
  return lhs.top_color == rhs.top_color && lhs.bottom_color == rhs.bottom_color &&
         lhs.rotation_angle == rhs.rotation_angle;
}

string BackgroundType::get_link() const {
  string mode;
  if (is_blurred) {
    mode = "blur";
  }
  if (is_moving) {
    if (!mode.empty()) {
      mode += '+';
    }
    mode += "motion";
  }

  switch (type) {
    case BackgroundType::Type::Wallpaper: {
      if (!mode.empty()) {
        return PSTRING() << "mode=" << mode;
      }
      return string();
    }
    case BackgroundType::Type::Pattern: {
      string link = PSTRING() << "intensity=" << intensity
                              << "&bg_color=" << get_background_fill_color_hex_string(fill, false);
      if (!mode.empty()) {
        link += "&mode=";
        link += mode;
      }
      return link;
    }
    case BackgroundType::Type::Fill:
      return get_background_fill_color_hex_string(fill, true);
    default:
      UNREACHABLE();
      return string();
  }
}

bool operator==(const BackgroundType &lhs, const BackgroundType &rhs) {
  return lhs.type == rhs.type && lhs.is_blurred == rhs.is_blurred && lhs.is_moving == rhs.is_moving &&
         lhs.intensity == rhs.intensity && lhs.fill == rhs.fill;
}

static StringBuilder &operator<<(StringBuilder &string_builder, const BackgroundType::Type &type) {
  switch (type) {
    case BackgroundType::Type::Wallpaper:
      return string_builder << "Wallpaper";
    case BackgroundType::Type::Pattern:
      return string_builder << "Pattern";
    case BackgroundType::Type::Fill:
      return string_builder << "Fill";
    default:
      UNREACHABLE();
      return string_builder;
  }
}

StringBuilder &operator<<(StringBuilder &string_builder, const BackgroundType &type) {
  return string_builder << "type " << type.type << '[' << type.get_link() << ']';
}

Result<BackgroundType> get_background_type(const td_api::BackgroundType *type) {
  if (type == nullptr) {
    return Status::Error(400, "Type must be non-empty");
  }

  BackgroundType result;
  switch (type->get_id()) {
    case td_api::backgroundTypeWallpaper::ID: {
      auto wallpaper = static_cast<const td_api::backgroundTypeWallpaper *>(type);
      result = BackgroundType(wallpaper->is_blurred_, wallpaper->is_moving_);
      break;
    }
    case td_api::backgroundTypePattern::ID: {
      auto pattern = static_cast<const td_api::backgroundTypePattern *>(type);
      if (pattern->fill_ == nullptr) {
        return Status::Error(400, "Fill info must be non-empty");
      }
      result = BackgroundType(pattern->is_moving_, get_background_fill(pattern->fill_.get()), pattern->intensity_);
      break;
    }
    case td_api::backgroundTypeFill::ID: {
      auto fill = static_cast<const td_api::backgroundTypeFill *>(type);
      if (fill->fill_ == nullptr) {
        return Status::Error(400, "Fill info must be non-empty");
      }
      result = BackgroundType(get_background_fill(fill->fill_.get()));
      break;
    }
    default:
      UNREACHABLE();
  }
  if (!is_valid_intensity(result.intensity)) {
    return Status::Error(400, "Wrong intensity value");
  }
  if (!is_valid_color(result.fill.top_color)) {
    return Status::Error(400, result.fill.is_solid() ? Slice("Wrong color value") : ("Wrong top color value"));
  }
  if (!is_valid_color(result.fill.bottom_color)) {
    return Status::Error(400, "Wrong bottom color value");
  }
  if (!BackgroundFill::is_valid_rotation_angle(result.fill.rotation_angle)) {
    return Status::Error(400, "Wrong rotation angle value");
  }
  return result;
}

BackgroundType get_background_type(bool is_pattern,
                                   telegram_api::object_ptr<telegram_api::wallPaperSettings> settings) {
  bool is_blurred = false;
  bool is_moving = false;
  BackgroundFill fill;
  int32 intensity = 0;
  if (settings) {
    auto flags = settings->flags_;
    is_blurred = (flags & telegram_api::wallPaperSettings::BLUR_MASK) != 0;
    is_moving = (flags & telegram_api::wallPaperSettings::MOTION_MASK) != 0;

    int32 color = 0;
    if ((flags & telegram_api::wallPaperSettings::BACKGROUND_COLOR_MASK) != 0) {
      color = settings->background_color_;
      if (!is_valid_color(color)) {
        LOG(ERROR) << "Receive " << to_string(settings);
        color = 0;
      }
    }
    if ((flags & telegram_api::wallPaperSettings::SECOND_BACKGROUND_COLOR_MASK) != 0) {
      int32 second_color = settings->second_background_color_;
      if (!is_valid_color(second_color)) {
        LOG(ERROR) << "Receive " << to_string(settings);
        second_color = 0;
      }

      int32 rotation_angle = settings->rotation_;
      if (!BackgroundFill::is_valid_rotation_angle(rotation_angle)) {
        LOG(ERROR) << "Receive " << to_string(settings);
        rotation_angle = 0;
      }

      fill = BackgroundFill(color, second_color, rotation_angle);
    } else {
      fill = BackgroundFill(color);
    }

    if ((flags & telegram_api::wallPaperSettings::INTENSITY_MASK) != 0) {
      intensity = settings->intensity_;
      if (!is_valid_intensity(intensity)) {
        LOG(ERROR) << "Receive " << to_string(settings);
        intensity = 0;
      }
    }
  }
  if (is_pattern) {
    return BackgroundType(is_moving, fill, intensity);
  } else {
    return BackgroundType(is_blurred, is_moving);
  }
}

static td_api::object_ptr<td_api::BackgroundFill> get_background_fill_object(const BackgroundFill &fill) {
  if (fill.is_solid()) {
    return td_api::make_object<td_api::backgroundFillSolid>(fill.top_color);
  }
  return td_api::make_object<td_api::backgroundFillGradient>(fill.top_color, fill.bottom_color, fill.rotation_angle);
}

td_api::object_ptr<td_api::BackgroundType> get_background_type_object(const BackgroundType &type) {
  switch (type.type) {
    case BackgroundType::Type::Wallpaper:
      return td_api::make_object<td_api::backgroundTypeWallpaper>(type.is_blurred, type.is_moving);
    case BackgroundType::Type::Pattern:
      return td_api::make_object<td_api::backgroundTypePattern>(get_background_fill_object(type.fill), type.intensity,
                                                                type.is_moving);
    case BackgroundType::Type::Fill:
      return td_api::make_object<td_api::backgroundTypeFill>(get_background_fill_object(type.fill));
    default:
      UNREACHABLE();
      return nullptr;
  }
}

telegram_api::object_ptr<telegram_api::wallPaperSettings> get_input_wallpaper_settings(const BackgroundType &type) {
  int32 flags = 0;
  if (type.is_blurred) {
    flags |= telegram_api::wallPaperSettings::BLUR_MASK;
  }
  if (type.is_moving) {
    flags |= telegram_api::wallPaperSettings::MOTION_MASK;
  }
  if (type.fill.top_color != 0 || type.fill.bottom_color != 0) {
    flags |= telegram_api::wallPaperSettings::BACKGROUND_COLOR_MASK;
  }
  if (!type.fill.is_solid()) {
    flags |= telegram_api::wallPaperSettings::SECOND_BACKGROUND_COLOR_MASK;
  }
  if (type.intensity) {
    flags |= telegram_api::wallPaperSettings::INTENSITY_MASK;
  }
  if (type.is_server()) {
    return telegram_api::make_object<telegram_api::wallPaperSettings>(flags, false /*ignored*/, false /*ignored*/,
                                                                      type.fill.top_color, type.fill.bottom_color,
                                                                      type.intensity, type.fill.rotation_angle);
  }

  UNREACHABLE();
  return nullptr;
}

}  // namespace td
