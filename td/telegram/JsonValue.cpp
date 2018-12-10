//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/JsonValue.h"

#include "td/utils/JsonBuilder.h"
#include "td/utils/misc.h"

namespace td {

static td_api::object_ptr<td_api::JsonValue> get_json_value_object(const JsonValue &json_value);

static td_api::object_ptr<td_api::jsonObjectMember> get_json_value_member_object(
    const std::pair<MutableSlice, JsonValue> &json_value_member) {
  return td_api::make_object<td_api::jsonObjectMember>(json_value_member.first.str(),
                                                       get_json_value_object(json_value_member.second));
}

static td_api::object_ptr<td_api::JsonValue> get_json_value_object(const JsonValue &json_value) {
  switch (json_value.type()) {
    case JsonValue::Type::Null:
      return td_api::make_object<td_api::jsonValueNull>();
    case JsonValue::Type::Boolean:
      return td_api::make_object<td_api::jsonValueBoolean>(json_value.get_boolean());
    case JsonValue::Type::Number:
      return td_api::make_object<td_api::jsonValueNumber>(to_double(json_value.get_number()));
    case JsonValue::Type::String:
      return td_api::make_object<td_api::jsonValueString>(json_value.get_string().str());
    case JsonValue::Type::Array:
      return td_api::make_object<td_api::jsonValueArray>(transform(json_value.get_array(), get_json_value_object));
    case JsonValue::Type::Object:
      return td_api::make_object<td_api::jsonValueObject>(
          transform(json_value.get_object(), get_json_value_member_object));
    default:
      UNREACHABLE();
      return nullptr;
  }
}

Result<td_api::object_ptr<td_api::JsonValue>> get_json_value(MutableSlice json) {
  TRY_RESULT(json_value, json_decode(json));
  return get_json_value_object(json_value);
}

namespace {

class JsonableJsonValue : public Jsonable {
 public:
  explicit JsonableJsonValue(const td_api::JsonValue *json_value) : json_value_(json_value) {
  }
  void store(JsonValueScope *scope) const {
    if (json_value_ == nullptr) {
      *scope << JsonNull();
      return;
    }
    switch (json_value_->get_id()) {
      case td_api::jsonValueNull::ID:
        *scope << JsonNull();
        break;
      case td_api::jsonValueBoolean::ID:
        *scope << static_cast<const td_api::jsonValueBoolean *>(json_value_)->value_;
        break;
      case td_api::jsonValueNumber::ID:
        *scope << static_cast<const td_api::jsonValueNumber *>(json_value_)->value_;
        break;
      case td_api::jsonValueString::ID:
        *scope << static_cast<const td_api::jsonValueString *>(json_value_)->value_;
        break;
      case td_api::jsonValueArray::ID: {
        auto array = scope->enter_array();
        for (auto &value : static_cast<const td_api::jsonValueArray *>(json_value_)->values_) {
          array << JsonableJsonValue(value.get());
        }
        break;
      }
      case td_api::jsonValueObject::ID: {
        auto object = scope->enter_object();
        for (auto &member : static_cast<const td_api::jsonValueObject *>(json_value_)->members_) {
          if (member != nullptr) {
            object << ctie(member->key_, JsonableJsonValue(member->value_.get()));
          }
        }
        break;
      }
      default:
        UNREACHABLE();
    }
  }

 private:
  const td_api::JsonValue *json_value_;
};

}  // namespace

string get_json_string(const td_api::JsonValue *json_value) {
  return json_encode<string>(JsonableJsonValue(json_value));
}

}  // namespace td
