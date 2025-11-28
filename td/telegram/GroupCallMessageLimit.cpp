//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/GroupCallMessageLimit.h"

#include "td/telegram/JsonValue.h"
#include "td/telegram/misc.h"
#include "td/telegram/StarManager.h"

#include "td/utils/algorithm.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Slice.h"

namespace td {

GroupCallMessageLimit::GroupCallMessageLimit(telegram_api::object_ptr<telegram_api::JSONValue> &&limit) {
  CHECK(limit != nullptr);
  if (limit->get_id() != telegram_api::jsonObject::ID) {
    LOG(ERROR) << "Receive " << to_string(limit);
    return;
  }
  auto get_color = [](telegram_api::object_ptr<telegram_api::JSONValue> &&json_value, Slice name) {
    auto str = get_json_value_string(std::move(json_value), name);
    auto r_color = hex_to_integer_safe<uint32>(str);
    if (r_color.is_error() || r_color.ok() > 0xFFFFFF) {
      LOG(ERROR) << "Receive " << str << " as " << name;
      return 0;
    }
    return static_cast<int32>(r_color.ok());
  };

  auto object = telegram_api::move_object_as<telegram_api::jsonObject>(limit);
  for (auto &value : object->value_) {
    if (value->key_ == "stars") {
      star_count_ = StarManager::get_star_count(get_json_value_long(std::move(value->value_), value->key_));
    } else if (value->key_ == "pin_period") {
      pin_duration_ = get_json_value_int(std::move(value->value_), value->key_);
    } else if (value->key_ == "text_length_max") {
      max_text_length_ = get_json_value_int(std::move(value->value_), value->key_);
    } else if (value->key_ == "emoji_max") {
      max_emoji_count_ = get_json_value_int(std::move(value->value_), value->key_);
    } else if (value->key_ == "color1") {
      color1_ = get_color(std::move(value->value_), value->key_);
    } else if (value->key_ == "color2") {
      color2_ = get_color(std::move(value->value_), value->key_);
    } else if (value->key_ == "color_bg") {
      color_bg_ = get_color(std::move(value->value_), value->key_);
    }
  }
}

GroupCallMessageLimit GroupCallMessageLimit::basic() {
  GroupCallMessageLimit result;
  result.max_text_length_ = 30;
  result.color1_ = 9788635;
  result.color2_ = 9788635;
  result.color_bg_ = 4786075;
  return result;
}

bool GroupCallMessageLimit::is_valid() const {
  return star_count_ >= 0 && pin_duration_ >= 0 && max_text_length_ > 0 && max_emoji_count_ >= 0 &&
         is_valid_color(color1_) && is_valid_color(color2_) && is_valid_color(color_bg_);
}

td_api::object_ptr<td_api::groupCallMessageLevel> GroupCallMessageLimit::get_group_call_message_level_object() const {
  return td_api::make_object<td_api::groupCallMessageLevel>(star_count_, pin_duration_, max_text_length_,
                                                            max_emoji_count_, color1_, color2_, color_bg_);
}

bool operator==(const GroupCallMessageLimit &lhs, const GroupCallMessageLimit &rhs) {
  return lhs.star_count_ == rhs.star_count_ && lhs.pin_duration_ == rhs.pin_duration_ &&
         lhs.max_text_length_ == rhs.max_text_length_ && lhs.max_emoji_count_ == rhs.max_emoji_count_ &&
         lhs.color1_ == rhs.color1_ && lhs.color2_ == rhs.color2_ && lhs.color_bg_ == rhs.color_bg_;
}

bool operator<(const GroupCallMessageLimit &lhs, const GroupCallMessageLimit &rhs) {
  return lhs.star_count_ > rhs.star_count_;
}

GroupCallMessageLimits::GroupCallMessageLimits(telegram_api::object_ptr<telegram_api::JSONValue> &&limits) {
  if (limits == nullptr) {
    return;
  }
  if (limits->get_id() != telegram_api::jsonArray::ID) {
    LOG(ERROR) << "Receive " << to_string(limits);
    return;
  }
  auto array = telegram_api::move_object_as<telegram_api::jsonArray>(limits);
  for (auto &value : array->value_) {
    GroupCallMessageLimit limit(std::move(value));
    if (!limit.is_valid()) {
      LOG(ERROR) << "Receive an invalid group call message level";
      continue;
    }
    if (!limits_.empty() && !(limits_.back() < limit)) {
      LOG(ERROR) << "Receive limits in invalid order";
      continue;
    }
    limits_.push_back(std::move(limit));
  }
  if (limits_.empty() || limits_.back() < GroupCallMessageLimit::basic()) {
    LOG(ERROR) << "Receive no basic limit";
    limits_.push_back(GroupCallMessageLimit::basic());
  }
}

GroupCallMessageLimits GroupCallMessageLimits::basic() {
  GroupCallMessageLimits result;
  result.limits_.push_back(GroupCallMessageLimit::basic());
  return result;
}

int32 GroupCallMessageLimits::get_level(int64 star_count) const {
  for (size_t i = 0; i < limits_.size(); i++) {
    if (star_count >= limits_[i].get_star_count()) {
      return static_cast<int32>(limits_.size() - i) - 1;
    }
  }
  UNREACHABLE();
  return 0;
}

td_api::object_ptr<td_api::updateGroupCallMessageLevels>
GroupCallMessageLimits::get_update_group_call_message_levels_object() const {
  return td_api::make_object<td_api::updateGroupCallMessageLevels>(
      transform(limits_, [](const auto &level) { return level.get_group_call_message_level_object(); }));
}

bool operator==(const GroupCallMessageLimits &lhs, const GroupCallMessageLimits &rhs) {
  return lhs.limits_ == rhs.limits_;
}

}  // namespace td
