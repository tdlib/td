//
// Copyright testertesterov (https://t.me/testertesterov) 2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#ifndef TD_JSON_PARSER_H
#define TD_JSON_PARSER_H

#include <string>
#include <map>
#include <vector>

namespace td {

class JsonParseError : public std::exception {
private:
  std::string message_;
  size_t line_;
  size_t column_;

public:
  JsonParseError(const std::string& msg, size_t line = 0, size_t column = 0)
    : message_(msg), line_(line), column_(column) {}

  const char* what() const noexcept override {
    return message_.c_str();
  }

  size_t line() const { return line_; }
  size_t column() const { return column_; }
};

class JsonValue {
public:
  enum Type { Null, String, Number, Boolean, Object, Array };

  JsonValue();
  explicit JsonValue(const std::string& str);
  explicit JsonValue(double num);
  explicit JsonValue(bool b);
  JsonValue(const JsonValue& other);
  JsonValue& operator=(const JsonValue& other);

  bool isString() const;
  bool isNumber() const;
  bool isBoolean() const;
  bool isObject() const;
  bool isArray() const;
  bool isNull() const;

  std::string asString() const;
  double asNumber() const;
  bool asBoolean() const;

  bool has(const std::string& key) const;
  const JsonValue& get(const std::string& key) const;
  JsonValue& operator[](const std::string& key);

  const JsonValue& at(size_t index) const;
  JsonValue& at(size_t index);
  size_t size() const;
  void push_back(const JsonValue& val);

  Type type;
  std::string stringValue;
  double numberValue;
  bool boolValue;
  std::map<std::string, JsonValue> objectValue;
  std::vector<JsonValue> arrayValue;

private:
  void copyFrom(const JsonValue& other);
};

JsonValue json_decode(const std::string& json_str);
std::string json_encode(const JsonValue& value, bool pretty = false);

std::string json_get_string(const JsonValue& json, const std::string& key, 
                           const std::string& defaultValue = "");
double json_get_number(const JsonValue& json, const std::string& key, 
                      double defaultValue = 0.0);
bool json_get_bool(const JsonValue& json, const std::string& key, 
                  bool defaultValue = false);
JsonValue json_get_object(const JsonValue& json, const std::string& key);
JsonValue json_get_array(const JsonValue& json, const std::string& key);
bool json_has_key(const JsonValue& json, const std::string& key);

inline JsonValue json_parse(const std::string& json_str) {
  return json_decode(json_str);
}

inline std::string json_stringify(const JsonValue& value, bool pretty = false) {
  return json_encode(value, pretty);
}

}
#endif
