//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/base64.h"
#include "td/utils/format.h"
#include "td/utils/JsonBuilder.h"
#include "td/utils/misc.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/tl_storers.h"

#include "td/telegram/td_api.h"
#include "td/telegram/td_api.hpp"

#include <type_traits>

namespace td {

struct JsonInt64 {
  int64 value;
};

inline void to_json(JsonValueScope &jv, const JsonInt64 json_int64) {
  jv << JsonString(PSLICE() << json_int64.value);
}
struct JsonVectorInt64 {
  const std::vector<int64> &value;
};

inline void to_json(JsonValueScope &jv, const JsonVectorInt64 &vec) {
  auto ja = jv.enter_array();
  for (auto &value : vec.value) {
    ja.enter_value() << ToJson(JsonInt64{value});
  }
}

inline void to_json(JsonValueScope &jv, const td_api::Object &object) {
  td_api::downcast_call(const_cast<td_api::Object &>(object), [&jv](const auto &object) { to_json(jv, object); });
}
inline void to_json(JsonValueScope &jv, const td_api::Function &object) {
  td_api::downcast_call(const_cast<td_api::Function &>(object), [&jv](const auto &object) { to_json(jv, object); });
}

template <class T>
void to_json(JsonValueScope &jv, const tl_object_ptr<T> &value) {
  if (value) {
    to_json(jv, *value);
  } else {
    jv << JsonNull();
  }
}

template <class T>
void to_json(JsonValueScope &jv, const std::vector<T> &v) {
  auto ja = jv.enter_array();
  for (auto &value : v) {
    ja.enter_value() << ToJson(value);
  }
}

inline Status from_json(int32 &to, JsonValue &from) {
  if (from.type() != JsonValue::Type::Number && from.type() != JsonValue::Type::String) {
    return Status::Error(PSLICE() << "Expected Number, got " << from.type());
  }
  Slice number = from.type() == JsonValue::Type::String ? from.get_string() : from.get_number();
  TRY_RESULT(res, to_integer_safe<int32>(number));
  to = res;
  return Status::OK();
}

inline Status from_json(bool &to, JsonValue &from) {
  if (from.type() != JsonValue::Type::Boolean) {
    int32 x;
    auto status = from_json(x, from);
    if (status.is_ok()) {
      to = x != 0;
      return Status::OK();
    }
    return Status::Error(PSLICE() << "Expected Boolean, got " << from.type());
  }
  to = from.get_boolean();
  return Status::OK();
}

inline Status from_json(int64 &to, JsonValue &from) {
  if (from.type() != JsonValue::Type::Number && from.type() != JsonValue::Type::String) {
    return Status::Error(PSLICE() << "Expected String or Number, got " << from.type());
  }
  Slice number = from.type() == JsonValue::Type::String ? from.get_string() : from.get_number();
  TRY_RESULT(res, to_integer_safe<int64>(number));
  to = res;
  return Status::OK();
}

inline Status from_json(double &to, JsonValue &from) {
  if (from.type() != JsonValue::Type::Number) {
    return Status::Error(PSLICE() << "Expected Number, got " << from.type());
  }
  to = to_double(from.get_number());
  return Status::OK();
}

inline Status from_json(string &to, JsonValue &from) {
  if (from.type() != JsonValue::Type::String) {
    return Status::Error(PSLICE() << "Expected String, got " << from.type());
  }
  to = from.get_string().str();
  return Status::OK();
}

inline Status from_json_bytes(string &to, JsonValue &from) {
  if (from.type() != JsonValue::Type::String) {
    return Status::Error(PSLICE() << "Expected String, got " << from.type());
  }
  TRY_RESULT(decoded, base64_decode(from.get_string()));
  to = std::move(decoded);
  return Status::OK();
}

template <class T>
Status from_json(std::vector<T> &to, JsonValue &from) {
  if (from.type() != JsonValue::Type::Array) {
    return Status::Error(PSLICE() << "Expected Array, got " << from.type());
  }
  to = std::vector<T>(from.get_array().size());
  size_t i = 0;
  for (auto &value : from.get_array()) {
    TRY_STATUS(from_json(to[i], value));
    i++;
  }
  return Status::OK();
}

template <class T>
class DowncastHelper : public T {
 public:
  explicit DowncastHelper(int32 constructor) : constructor_(constructor) {
  }
  int32 get_id() const override {
    return constructor_;
  }
  void store(TlStorerToString &s, const char *field_name) const override {
  }

 private:
  int32 constructor_{0};
};

template <class T>
std::enable_if_t<!std::is_constructible<T>::value, Status> from_json(tl_object_ptr<T> &to, JsonValue &from) {
  if (from.type() != JsonValue::Type::Object) {
    if (from.type() == JsonValue::Type::Null) {
      to = nullptr;
      return Status::OK();
    }
    return Status::Error(PSLICE() << "Expected Object, got " << from.type());
  }

  auto &object = from.get_object();
  TRY_RESULT(constructor_value, get_json_object_field(object, "@type", JsonValue::Type::Null, false));
  int32 constructor = 0;
  if (constructor_value.type() == JsonValue::Type::Number) {
    constructor = to_integer<int32>(constructor_value.get_number());
  } else if (constructor_value.type() == JsonValue::Type::String) {
    TRY_RESULT(t_constructor, tl_constructor_from_string(to.get(), constructor_value.get_string().str()));
    constructor = t_constructor;
  } else {
    return Status::Error(PSLICE() << "Expected String or Integer, got " << constructor_value.type());
  }

  DowncastHelper<T> helper(constructor);
  Status status;
  bool ok = downcast_call(static_cast<T &>(helper), [&](auto &dummy) {
    auto result = make_tl_object<std::decay_t<decltype(dummy)>>();
    status = from_json(*result, object);
    to = std::move(result);
  });
  TRY_STATUS(std::move(status));
  if (!ok) {
    return Status::Error(PSLICE() << "Unknown constructor " << format::as_hex(constructor));
  }

  return Status::OK();
}

template <class T>
std::enable_if_t<std::is_constructible<T>::value, Status> from_json(tl_object_ptr<T> &to, JsonValue &from) {
  if (from.type() != JsonValue::Type::Object) {
    if (from.type() == JsonValue::Type::Null) {
      to = nullptr;
      return Status::OK();
    }
    return Status::Error(PSLICE() << "Expected Object, got " << from.type());
  }
  to = make_tl_object<T>();
  return from_json(*to, from.get_object());
}

}  // namespace td
