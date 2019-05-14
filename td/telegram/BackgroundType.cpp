//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/BackgroundType.h"

#include "td/utils/logging.h"

namespace td {

string BackgroundType::get_color_hex_string() const {
  string result;
  for (int i = 20; i >= 0; i -= 4) {
    result += "0123456789abcdef"[(color >> i) & 0xf];
  }
  return result;
}

bool operator==(const BackgroundType &lhs, const BackgroundType &rhs) {
  return lhs.type == rhs.type && lhs.is_blurred == rhs.is_blurred && lhs.is_moving == rhs.is_moving &&
         lhs.color == rhs.color && lhs.intensity == rhs.intensity;
}

StringBuilder &operator<<(StringBuilder &string_builder, const BackgroundType &type) {
  switch (type.type) {
    case BackgroundType::Type::Wallpaper:
      return string_builder << "type Wallpaper[" << (type.is_blurred ? "blurred" : "") << ' '
                            << (type.is_moving ? "moving" : "") << ']';
    case BackgroundType::Type::Pattern:
      return string_builder << "type Pattern[" << (type.is_moving ? "moving" : "") << ' ' << type.get_color_hex_string()
                            << ' ' << type.intensity << ']';
    case BackgroundType::Type::Solid:
      return string_builder << "type Solid[" << type.get_color_hex_string() << ']';
    default:
      UNREACHABLE();
      return string_builder;
  }
}

Result<BackgroundType> get_background_type(const td_api::BackgroundType *type) {
  if (type == nullptr) {
    return Status::Error(400, "Type must not be empty");
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
      result = BackgroundType(pattern->is_moving_, pattern->color_, pattern->intensity_);
      break;
    }
    case td_api::backgroundTypeSolid::ID: {
      auto solid = static_cast<const td_api::backgroundTypeSolid *>(type);
      result = BackgroundType(solid->color_);
      break;
    }
    default:
      UNREACHABLE();
  }
  if (result.intensity < 0 || result.intensity > 100) {
    return Status::Error(400, "Wrong intensity value");
  }
  if (result.color < 0 || result.color > 0xFFFFFF) {
    return Status::Error(400, "Wrong color value");
  }
  return result;
}

BackgroundType get_background_type(bool is_pattern,
                                   telegram_api::object_ptr<telegram_api::wallPaperSettings> settings) {
  bool is_blurred = false;
  bool is_moving = false;
  int32 color = 0;
  int32 intensity = 0;
  if (settings) {
    auto flags = settings->flags_;
    is_blurred = (flags & telegram_api::wallPaperSettings::BLUR_MASK) != 0;
    is_moving = (flags & telegram_api::wallPaperSettings::MOTION_MASK) != 0;
    if ((flags & telegram_api::wallPaperSettings::BACKGROUND_COLOR_MASK) != 0) {
      color = settings->background_color_;
      if (color < 0 || color > 0xFFFFFF) {
        LOG(ERROR) << "Receive " << to_string(settings);
        color = 0;
      }
    }
    if ((flags & telegram_api::wallPaperSettings::INTENSITY_MASK) != 0) {
      intensity = settings->intensity_;
      if (intensity < 0 || intensity > 100) {
        LOG(ERROR) << "Receive " << to_string(settings);
        intensity = 0;
      }
    }
  }
  if (is_pattern) {
    return BackgroundType(is_moving, color, intensity);
  } else {
    return BackgroundType(is_blurred, is_moving);
  }
}

td_api::object_ptr<td_api::BackgroundType> get_background_type_object(const BackgroundType &type) {
  switch (type.type) {
    case BackgroundType::Type::Wallpaper:
      return td_api::make_object<td_api::backgroundTypeWallpaper>(type.is_blurred, type.is_moving);
    case BackgroundType::Type::Pattern:
      return td_api::make_object<td_api::backgroundTypePattern>(type.is_moving, type.color, type.intensity);
    case BackgroundType::Type::Solid:
      return td_api::make_object<td_api::backgroundTypeSolid>(type.color);
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
  if (type.color != 0) {
    flags |= telegram_api::wallPaperSettings::BACKGROUND_COLOR_MASK;
  }
  if (type.intensity) {
    flags |= telegram_api::wallPaperSettings::INTENSITY_MASK;
  }
  switch (type.type) {
    case BackgroundType::Type::Wallpaper:
    case BackgroundType::Type::Pattern:
      return telegram_api::make_object<telegram_api::wallPaperSettings>(flags, false /*ignored*/, false /*ignored*/,
                                                                        type.color, type.intensity);
    case BackgroundType::Type::Solid:
    default:
      UNREACHABLE();
      return nullptr;
  }
}

}  // namespace td
