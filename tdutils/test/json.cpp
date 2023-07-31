//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/common.h"
#include "td/utils/JsonBuilder.h"
#include "td/utils/logging.h"
#include "td/utils/Slice.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/tests.h"

#include <utility>

static void decode_encode(const td::string &str, td::string result = td::string()) {
  auto str_copy = str;
  auto r_value = td::json_decode(str_copy);
  ASSERT_TRUE(r_value.is_ok());
  if (r_value.is_error()) {
    LOG(INFO) << r_value.error();
    return;
  }
  auto new_str = td::json_encode<td::string>(r_value.ok());
  if (result.empty()) {
    result = str;
  }
  ASSERT_EQ(result, new_str);
}

TEST(JSON, array) {
  char tmp[1000];
  td::StringBuilder sb(td::MutableSlice{tmp, sizeof(tmp)});
  td::JsonBuilder jb(std::move(sb));
  jb.enter_value().enter_array() << "Hello" << -123;
  ASSERT_EQ(jb.string_builder().is_error(), false);
  auto encoded = jb.string_builder().as_cslice().str();
  ASSERT_EQ("[\"Hello\",-123]", encoded);
  decode_encode(encoded);
}

TEST(JSON, object) {
  char tmp[1000];
  td::StringBuilder sb(td::MutableSlice{tmp, sizeof(tmp)});
  td::JsonBuilder jb(std::move(sb));
  auto c = jb.enter_object();
  c("key", "value");
  c("1", 2);
  c.leave();
  ASSERT_EQ(jb.string_builder().is_error(), false);
  auto encoded = jb.string_builder().as_cslice().str();
  ASSERT_EQ("{\"key\":\"value\",\"1\":2}", encoded);
  decode_encode(encoded);
}

TEST(JSON, nested) {
  char tmp[1000];
  td::StringBuilder sb(td::MutableSlice{tmp, sizeof(tmp)});
  td::JsonBuilder jb(std::move(sb));
  {
    auto a = jb.enter_array();
    a << 1;
    { a.enter_value().enter_array() << 2; }
    a << 3;
  }
  ASSERT_EQ(jb.string_builder().is_error(), false);
  auto encoded = jb.string_builder().as_cslice().str();
  ASSERT_EQ("[1,[2],3]", encoded);
  decode_encode(encoded);
}

TEST(JSON, kphp) {
  decode_encode("[]");
  decode_encode("[[]]");
  decode_encode("{}");
  decode_encode("{}");
  decode_encode("\"\\n\"");
  decode_encode(
      "\""
      "some long string \\t \\r \\\\ \\n \\f \\\" "
      "\\u1234"
      "\"");
  decode_encode(
      "{\"keyboard\":[[\"\\u2022 abcdefg\"],[\"\\u2022 hijklmnop\"],[\"\\u2022 "
      "qrstuvwxyz\"]],\"one_time_keyboard\":true}");
  decode_encode(
      "  \n   {  \"keyboard\"  : \n  [[  \"\\u2022 abcdefg\"  ]  , \n [  \"\\u2022 hijklmnop\" \n ],[  \n \"\\u2022 "
      "qrstuvwxyz\"]], \n  \"one_time_keyboard\"\n:\ntrue\n}\n   \n",
      "{\"keyboard\":[[\"\\u2022 abcdefg\"],[\"\\u2022 hijklmnop\"],[\"\\u2022 "
      "qrstuvwxyz\"]],\"one_time_keyboard\":true}");
}

TEST(JSON, get_json_object_field) {
  const td::string encoded_object =
      "{\"null\":null,\"bool\":true,\"int\":\"1\",\"int2\":2,\"long\":\"123456789012\",\"long2\":2123456789012,"
      "\"double\":12345678901.1,\"string\":\"string\",\"string2\":12345e+1,\"array\":[],\"object\":{}}";
  {
    td::string encoded_object_copy = encoded_object;
    auto value = td::json_decode(encoded_object_copy).move_as_ok();
    auto &object = value.get_object();
    ASSERT_EQ(td::json_encode<td::string>(td::get_json_object_field_force(object, "null")), "null");
    ASSERT_EQ(td::json_encode<td::string>(td::get_json_object_field_force(object, "bool")), "true");
    ASSERT_EQ(td::json_encode<td::string>(td::get_json_object_field_force(object, "bool")), "null");
    ASSERT_EQ(td::json_encode<td::string>(td::get_json_object_field_force(object, "int")), "\"1\"");
    ASSERT_EQ(td::json_encode<td::string>(td::get_json_object_field_force(object, "int2")), "2");
    ASSERT_EQ(td::json_encode<td::string>(td::get_json_object_field_force(object, "int3")), "null");
    ASSERT_EQ(td::json_encode<td::string>(td::get_json_object_field_force(object, "long")), "\"123456789012\"");
    ASSERT_EQ(td::json_encode<td::string>(td::get_json_object_field_force(object, "long2")), "2123456789012");
    ASSERT_EQ(td::json_encode<td::string>(td::get_json_object_field_force(object, "double")), "12345678901.1");
    ASSERT_EQ(td::json_encode<td::string>(td::get_json_object_field_force(object, "string")), "\"string\"");
    ASSERT_EQ(td::json_encode<td::string>(td::get_json_object_field_force(object, "string2")), "12345e+1");
    ASSERT_EQ(td::json_encode<td::string>(td::get_json_object_field_force(object, "array")), "[]");
    ASSERT_EQ(td::json_encode<td::string>(td::get_json_object_field_force(object, "object")), "{}");
    ASSERT_EQ(td::json_encode<td::string>(td::get_json_object_field_force(object, "")), "null");
  }

  {
    td::string encoded_object_copy = encoded_object;
    auto value = td::json_decode(encoded_object_copy).move_as_ok();
    auto &object = value.get_object();
    ASSERT_TRUE(td::get_json_object_field(object, "int", td::JsonValue::Type::Number).is_error());
    ASSERT_TRUE(td::get_json_object_field(object, "int", td::JsonValue::Type::Number).is_error());
    ASSERT_TRUE(td::get_json_object_field(object, "int2", td::JsonValue::Type::Number).is_ok());
    ASSERT_TRUE(td::get_json_object_field(object, "int2", td::JsonValue::Type::Number).is_error());
    ASSERT_TRUE(td::get_json_object_field(object, "int3", td::JsonValue::Type::Number).is_ok());
    ASSERT_TRUE(td::get_json_object_field(object, "int3", td::JsonValue::Type::Null).is_ok());
    ASSERT_EQ(td::get_json_object_field(object, "int", td::JsonValue::Type::String).ok().get_string(), "1");
    ASSERT_TRUE(td::get_json_object_field(object, "int", td::JsonValue::Type::Number).is_error());
    ASSERT_EQ(td::get_json_object_field(object, "int", td::JsonValue::Type::Null).ok().type(),
              td::JsonValue::Type::Null);

    ASSERT_TRUE(td::get_json_object_field(object, "long", td::JsonValue::Type::Number, false).is_error());
    ASSERT_TRUE(td::get_json_object_field(object, "long", td::JsonValue::Type::Number, false).is_error());
    ASSERT_TRUE(td::get_json_object_field(object, "long2", td::JsonValue::Type::Number, false).is_ok());
    ASSERT_TRUE(td::get_json_object_field(object, "long2", td::JsonValue::Type::Number, false).is_error());
    ASSERT_TRUE(td::get_json_object_field(object, "long3", td::JsonValue::Type::Number, false).is_error());
    ASSERT_TRUE(td::get_json_object_field(object, "long3", td::JsonValue::Type::Null, false).is_error());
    ASSERT_EQ(td::get_json_object_field(object, "long", td::JsonValue::Type::String, false).ok().get_string(),
              "123456789012");
    ASSERT_TRUE(td::get_json_object_field(object, "long", td::JsonValue::Type::Number, false).is_error());
    ASSERT_EQ(td::get_json_object_field(object, "long", td::JsonValue::Type::Null, false).ok().type(),
              td::JsonValue::Type::Null);
  }

  td::string encoded_object_copy = encoded_object;
  auto value = td::json_decode(encoded_object_copy).move_as_ok();
  auto &object = value.get_object();
  ASSERT_TRUE(td::has_json_object_field(object, "null"));
  ASSERT_TRUE(td::has_json_object_field(object, "object"));
  ASSERT_TRUE(!td::has_json_object_field(object, ""));
  ASSERT_TRUE(!td::has_json_object_field(object, "objec"));
  ASSERT_TRUE(!td::has_json_object_field(object, "object2"));

  ASSERT_TRUE(td::get_json_object_bool_field(object, "int").is_error());
  ASSERT_EQ(td::get_json_object_bool_field(object, "bool").ok(), true);
  ASSERT_EQ(td::get_json_object_bool_field(object, "bool").ok(), true);
  ASSERT_EQ(td::get_json_object_bool_field(object, "bool", true).ok(), true);
  ASSERT_EQ(td::get_json_object_bool_field(object, "bool3").ok(), false);
  ASSERT_EQ(td::get_json_object_bool_field(object, "bool4", true, true).ok(), true);
  ASSERT_TRUE(td::get_json_object_bool_field(object, "bool5", false, true).is_error());

  ASSERT_TRUE(td::get_json_object_int_field(object, "null").is_error());
  ASSERT_EQ(td::get_json_object_int_field(object, "int").ok(), 1);
  ASSERT_EQ(td::get_json_object_int_field(object, "int").ok(), 1);
  ASSERT_EQ(td::get_json_object_int_field(object, "int", true).ok(), 1);
  ASSERT_EQ(td::get_json_object_int_field(object, "int2").ok(), 2);
  ASSERT_EQ(td::get_json_object_int_field(object, "int2").ok(), 2);
  ASSERT_EQ(td::get_json_object_int_field(object, "int2", true).ok(), 2);
  ASSERT_EQ(td::get_json_object_int_field(object, "int3").ok(), 0);
  ASSERT_EQ(td::get_json_object_int_field(object, "int4", true, 5).ok(), 5);
  ASSERT_TRUE(td::get_json_object_int_field(object, "int5", false, 5).is_error());
  ASSERT_EQ(td::get_json_object_int_field(object, "long").is_error(), true);
  ASSERT_EQ(td::get_json_object_int_field(object, "long2").is_error(), true);

  ASSERT_TRUE(td::get_json_object_long_field(object, "null").is_error());
  ASSERT_EQ(td::get_json_object_long_field(object, "long").ok(), 123456789012ll);
  ASSERT_EQ(td::get_json_object_long_field(object, "long").ok(), 123456789012ll);
  ASSERT_EQ(td::get_json_object_long_field(object, "long", true).ok(), 123456789012ll);
  ASSERT_EQ(td::get_json_object_long_field(object, "long2").ok(), 2123456789012ll);
  ASSERT_EQ(td::get_json_object_long_field(object, "long2").ok(), 2123456789012ll);
  ASSERT_EQ(td::get_json_object_long_field(object, "long2", true).ok(), 2123456789012ll);
  ASSERT_EQ(td::get_json_object_long_field(object, "long3").ok(), 0);
  ASSERT_EQ(td::get_json_object_long_field(object, "long4", true, 5).ok(), 5);
  ASSERT_TRUE(td::get_json_object_long_field(object, "long5", false, 5).is_error());
  ASSERT_EQ(td::get_json_object_long_field(object, "int").ok(), 1);
  ASSERT_EQ(td::get_json_object_long_field(object, "int2").ok(), 2);

  auto are_equal_double = [](double lhs, double rhs) {
    return rhs - 1e-3 < lhs && lhs < rhs + 1e-3;
  };

  ASSERT_TRUE(td::get_json_object_double_field(object, "null").is_error());
  ASSERT_TRUE(are_equal_double(td::get_json_object_double_field(object, "double").ok(), 12345678901.1));
  ASSERT_TRUE(are_equal_double(td::get_json_object_double_field(object, "double").ok(), 12345678901.1));
  ASSERT_TRUE(are_equal_double(td::get_json_object_double_field(object, "double", true).ok(), 12345678901.1));
  ASSERT_TRUE(are_equal_double(td::get_json_object_double_field(object, "long2").ok(), 2123456789012.0));
  ASSERT_TRUE(are_equal_double(td::get_json_object_double_field(object, "long2").ok(), 2123456789012.0));
  ASSERT_TRUE(are_equal_double(td::get_json_object_double_field(object, "long2", true).ok(), 2123456789012.0));
  ASSERT_TRUE(are_equal_double(td::get_json_object_double_field(object, "double3").ok(), 0.0));
  ASSERT_TRUE(are_equal_double(td::get_json_object_double_field(object, "double4", true, -5.23).ok(), -5.23));
  ASSERT_TRUE(td::get_json_object_double_field(object, "double5", false, 5).is_error());
  ASSERT_TRUE(td::get_json_object_double_field(object, "int").is_error());
  ASSERT_TRUE(are_equal_double(td::get_json_object_double_field(object, "int2").ok(), 2));

  ASSERT_TRUE(td::get_json_object_string_field(object, "null").is_error());
  ASSERT_EQ(td::get_json_object_string_field(object, "string").ok(), "string");
  ASSERT_EQ(td::get_json_object_string_field(object, "string").ok(), "string");
  ASSERT_EQ(td::get_json_object_string_field(object, "string", true).ok(), "string");
  ASSERT_EQ(td::get_json_object_string_field(object, "string2").ok(), "12345e+1");
  ASSERT_EQ(td::get_json_object_string_field(object, "string2").ok(), "12345e+1");
  ASSERT_EQ(td::get_json_object_string_field(object, "string2", true).ok(), "12345e+1");
  ASSERT_EQ(td::get_json_object_string_field(object, "string3").ok(), td::string());
  ASSERT_EQ(td::get_json_object_string_field(object, "string4", true, "abacaba").ok(), "abacaba");
  ASSERT_TRUE(td::get_json_object_string_field(object, "string5", false, "test").is_error());
  ASSERT_EQ(td::get_json_object_string_field(object, "int").ok(), "1");
  ASSERT_EQ(td::get_json_object_string_field(object, "int2").ok(), "2");
}
