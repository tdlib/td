//
// Copyright testertesterov (https://t.me/testertesterov) 2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include <sstream>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <limits>
#include <algorithm>
#include "td_json_parser.h"

namespace td {

namespace {
  int hexCharToInt(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return 0;
  }

  bool isWhitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
  }

  bool isControlCharacter(char c) {
    return (c >= 0x00 && c <= 0x1F) || c == 0x7F;
  }

  struct ParseContext {
    const std::string& input;
    size_t pos;
    size_t line;
    size_t column;

    ParseContext(const std::string& str) 
      : input(str), pos(0), line(1), column(1) {}

    char current() const {
      return pos < input.size() ? input[pos] : '\0';
    }

    char next() {
      if (pos >= input.size()) return '\0';
      char c = input[pos++];
      if (c == '\n') {
        line++;
        column = 1;
      } else {
        column++;
      }
      return c;
    }

    void backup() {
      if (pos > 0) {
        pos--;
        if (input[pos] == '\n') {
          line--;
          size_t temp = pos;
          column = 1;
          while (temp > 0 && input[temp - 1] != '\n') {
            temp--;
            column++;
          }
        } else {
          column--;
        }
      }
    }

    void error(const std::string& msg) const {
      throw JsonParseError(msg, line, column);
    }
  };

  std::string decodeUnicodeEscape(ParseContext& ctx) {
    size_t startPos = ctx.pos;
    size_t startLine = ctx.line;
    size_t startColumn = ctx.column;

    if (ctx.current() != '\\') {
      ctx.error("Expected '\\' for Unicode escape");
    }
    ctx.next();

    if (ctx.current() != 'u') {
      ctx.error("Expected 'u' after '\\' for Unicode escape");
    }
    ctx.next();

    unsigned int codePoint = 0;
    for (int i = 0; i < 4; i++) {
      if (ctx.pos >= ctx.input.size()) {
        ctx.pos = startPos;
        ctx.line = startLine;
        ctx.column = startColumn;
        ctx.error("Unexpected end of input in Unicode escape");
      }

      char c = ctx.current();
      if (!std::isxdigit(static_cast<unsigned char>(c))) {
        ctx.pos = startPos;
        ctx.line = startLine;
        ctx.column = startColumn;
        ctx.error("Invalid hex character in Unicode escape");
      }

      codePoint = (codePoint << 4) | hexCharToInt(c);
      ctx.next();
    }

    if (codePoint >= 0xD800 && codePoint <= 0xDBFF) {
      if (ctx.pos + 5 >= ctx.input.size() || 
          ctx.input[ctx.pos] != '\\' || 
          ctx.input[ctx.pos + 1] != 'u') {
        ctx.pos = startPos;
        ctx.line = startLine;
        ctx.column = startColumn;
        ctx.error("Missing low surrogate in Unicode escape pair");
      }

      ctx.next();
      ctx.next();

      unsigned int lowSurrogate = 0;
      for (int i = 0; i < 4; i++) {
        if (ctx.pos >= ctx.input.size()) {
          ctx.pos = startPos;
          ctx.line = startLine;
          ctx.column = startColumn;
          ctx.error("Unexpected end of input in Unicode escape");
        }

        char c = ctx.current();
        if (!std::isxdigit(static_cast<unsigned char>(c))) {
          ctx.pos = startPos;
          ctx.line = startLine;
          ctx.column = startColumn;
          ctx.error("Invalid hex character in Unicode escape");
        }

        lowSurrogate = (lowSurrogate << 4) | hexCharToInt(c);
        ctx.next();
      }

      if (lowSurrogate < 0xDC00 || lowSurrogate > 0xDFFF) {
        ctx.pos = startPos;
        ctx.line = startLine;
        ctx.column = startColumn;
        ctx.error("Invalid low surrogate in Unicode escape pair");
      }

      codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (lowSurrogate - 0xDC00);
    } else if (codePoint >= 0xDC00 && codePoint <= 0xDFFF) {
      ctx.pos = startPos;
      ctx.line = startLine;
      ctx.column = startColumn;
      ctx.error("Unexpected low surrogate without high surrogate");
    }

    std::string utf8;

    if (codePoint <= 0x7F) {
      utf8.push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7FF) {
      utf8.push_back(static_cast<char>(0xC0 | ((codePoint >> 6) & 0x1F)));
      utf8.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else if (codePoint <= 0xFFFF) {
      utf8.push_back(static_cast<char>(0xE0 | ((codePoint >> 12) & 0x0F)));
      utf8.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
      utf8.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else if (codePoint <= 0x10FFFF) {
      utf8.push_back(static_cast<char>(0xF0 | ((codePoint >> 18) & 0x07)));
      utf8.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
      utf8.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
      utf8.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else {
      ctx.pos = startPos;
      ctx.line = startLine;
      ctx.column = startColumn;
      ctx.error("Invalid Unicode code point");
    }

    return utf8;
  }

  void skipWhitespace(ParseContext& ctx) {
    while (ctx.pos < ctx.input.size() && isWhitespace(ctx.input[ctx.pos])) {
      ctx.next();
    }
  }

  std::string parseString(ParseContext& ctx) {
    if (ctx.current() != '"') {
      ctx.error("Expected '\"' at start of string");
    }
    ctx.next();

    std::string result;

    while (ctx.pos < ctx.input.size()) {
      char c = ctx.current();

      if (c == '"') {
        ctx.next();
        return result;
      }

      if (c == '\\') {
        ctx.next();

        if (ctx.pos >= ctx.input.size()) {
          ctx.error("Unexpected end of input after '\\'");
        }

        char escapeChar = ctx.current();
        switch (escapeChar) {
          case '"': result += '"'; break;
          case '\\': result += '\\'; break;
          case '/': result += '/'; break;
          case 'b': result += '\b'; break;
          case 'f': result += '\f'; break;
          case 'n': result += '\n'; break;
          case 'r': result += '\r'; break;
          case 't': result += '\t'; break;
          case 'u':
            ctx.backup();
            result += decodeUnicodeEscape(ctx);
            continue;
          default:
            ctx.error(std::string("Invalid escape sequence: \\") + escapeChar);
        }
        ctx.next();
      } else {
        if (isControlCharacter(c)) {
          ctx.error("Control character in string must be escaped");
        }

        if (static_cast<unsigned char>(c) >= 0x80) {
          unsigned char first = static_cast<unsigned char>(c);
          int expectedLen = 0;

          if ((first & 0xE0) == 0xC0) expectedLen = 2;
          else if ((first & 0xF0) == 0xE0) expectedLen = 3;
          else if ((first & 0xF8) == 0xF0) expectedLen = 4;
          else {
            ctx.error("Invalid UTF-8 sequence start");
          }

          for (int i = 1; i < expectedLen; i++) {
            if (ctx.pos + i >= ctx.input.size()) {
              ctx.error("Unexpected end of UTF-8 sequence");
            }
            unsigned char next = static_cast<unsigned char>(ctx.input[ctx.pos + i]);
            if ((next & 0xC0) != 0x80) {
              ctx.error("Invalid UTF-8 continuation byte");
            }
          }

          for (int i = 0; i < expectedLen; i++) {
            result += ctx.current();
            ctx.next();
          }
          continue;
        } else {
          result += c;
          ctx.next();
        }
      }
    }

    ctx.error("Unexpected end of input in string");
    return "";
  }

  JsonValue parseNumber(ParseContext& ctx) {
    size_t start = ctx.pos;
    size_t startLine = ctx.line;
    size_t startColumn = ctx.column;

    std::string numStr;

    if (ctx.current() == '-') {
      numStr += ctx.current();
      ctx.next();
    }

    if (ctx.current() == '0') {
      numStr += ctx.current();
      ctx.next();

      if (ctx.pos < ctx.input.size() && 
          ctx.current() >= '0' && ctx.current() <= '9') {
        ctx.pos = start;
        ctx.line = startLine;
        ctx.column = startColumn;
        ctx.error("Numbers cannot have leading zeros");
      }
    } else if (ctx.current() >= '1' && ctx.current() <= '9') {
      while (ctx.pos < ctx.input.size() && 
             ctx.current() >= '0' && ctx.current() <= '9') {
        numStr += ctx.current();
        ctx.next();
      }
    } else {
      ctx.pos = start;
      ctx.line = startLine;
      ctx.column = startColumn;
      ctx.error("Expected digit in number");
    }

    if (ctx.pos < ctx.input.size() && ctx.current() == '.') {
      numStr += ctx.current();
      ctx.next();

      if (ctx.pos >= ctx.input.size() || 
          !(ctx.current() >= '0' && ctx.current() <= '9')) {
        ctx.pos = start;
        ctx.line = startLine;
        ctx.column = startColumn;
        ctx.error("Expected digit after decimal point");
      }

      while (ctx.pos < ctx.input.size() && 
             ctx.current() >= '0' && ctx.current() <= '9') {
        numStr += ctx.current();
        ctx.next();
      }
    }

    if (ctx.pos < ctx.input.size() && 
        (ctx.current() == 'e' || ctx.current() == 'E')) {
      numStr += ctx.current();
      ctx.next();

      if (ctx.pos < ctx.input.size() && 
          (ctx.current() == '+' || ctx.current() == '-')) {
        numStr += ctx.current();
        ctx.next();
      }

      if (ctx.pos >= ctx.input.size() || 
          !(ctx.current() >= '0' && ctx.current() <= '9')) {
        ctx.pos = start;
        ctx.line = startLine;
        ctx.column = startColumn;
        ctx.error("Expected digit in exponent");
      }

      while (ctx.pos < ctx.input.size() && 
             ctx.current() >= '0' && ctx.current() <= '9') {
        numStr += ctx.current();
        ctx.next();
      }
    }

    try {
      double value = std::stod(numStr);

      if (!std::isfinite(value)) {
        ctx.pos = start;
        ctx.line = startLine;
        ctx.column = startColumn;
        ctx.error("Number must be finite");
      }

      return JsonValue(value);
    } catch (const std::exception&) {
      ctx.pos = start;
      ctx.line = startLine;
      ctx.column = startColumn;
      ctx.error("Invalid number format");
    }

    return JsonValue();
  }

  JsonValue parseValue(ParseContext& ctx);

  JsonValue parseArray(ParseContext& ctx) {
    if (ctx.current() != '[') {
      ctx.error("Expected '[' at start of array");
    }
    ctx.next();

    skipWhitespace(ctx);

    JsonValue arr;
    arr.type = JsonValue::Array;

    if (ctx.current() == ']') {
      ctx.next();
      return arr;
    }

    while (ctx.pos <= ctx.input.size()) {
      JsonValue element = parseValue(ctx);
      arr.arrayValue.push_back(element);

      skipWhitespace(ctx);

      if (ctx.current() == ',') {
        ctx.next();
        skipWhitespace(ctx);

        if (ctx.current() == ']') {
          ctx.error("Trailing comma in array");
        }
      } else if (ctx.current() == ']') {
        ctx.next();
        break;
      } else if (ctx.pos >= ctx.input.size()) {
        ctx.error("Unexpected end of input in array");
      } else {
        ctx.error("Expected ',' or ']' in array");
      }
    }

    return arr;
  }

  JsonValue parseObject(ParseContext& ctx) {
    if (ctx.current() != '{') {
      ctx.error("Expected '{' at start of object");
    }
    ctx.next();

    skipWhitespace(ctx);

    JsonValue obj;
    obj.type = JsonValue::Object;

    if (ctx.current() == '}') {
      ctx.next();
      return obj;
    }

    while (ctx.pos <= ctx.input.size()) {
      skipWhitespace(ctx);

      if (ctx.current() != '"') {
        ctx.error("Expected '\"' at start of object key");
      }

      std::string key = parseString(ctx);

      if (obj.objectValue.find(key) != obj.objectValue.end()) {
        ctx.error("Duplicate key in object: " + key);
      }

      skipWhitespace(ctx);

      if (ctx.current() != ':') {
        ctx.error("Expected ':' after object key");
      }
      ctx.next();

      skipWhitespace(ctx);

      JsonValue value = parseValue(ctx);
      obj.objectValue[key] = value;

      skipWhitespace(ctx);

      if (ctx.current() == ',') {
        ctx.next();
        skipWhitespace(ctx);

        if (ctx.current() == '}') {
          ctx.error("Trailing comma in object");
        }
      } else if (ctx.current() == '}') {
        ctx.next();
        break;
      } else if (ctx.pos >= ctx.input.size()) {
        ctx.error("Unexpected end of input in object");
      } else {
        ctx.error("Expected ',' or '}' in object");
      }
    }

    return obj;
  }

  JsonValue parseValue(ParseContext& ctx) {
    skipWhitespace(ctx);

    if (ctx.pos >= ctx.input.size()) {
      ctx.error("Unexpected end of input");
    }

    char c = ctx.current();

    if (c == '"') {
      return JsonValue(parseString(ctx));
    } else if (c == '{') {
      return parseObject(ctx);
    } else if (c == '[') {
      return parseArray(ctx);
    } else if (c == 't') {
      if (ctx.input.compare(ctx.pos, 4, "true") == 0) {
        ctx.pos += 4;
        ctx.column += 4;
        return JsonValue(true);
      }
      ctx.error("Expected 'true'");
    } else if (c == 'f') {
      if (ctx.input.compare(ctx.pos, 5, "false") == 0) {
        ctx.pos += 5;
        ctx.column += 5;
        return JsonValue(false);
      }
      ctx.error("Expected 'false'");
    } else if (c == 'n') {
      if (ctx.input.compare(ctx.pos, 4, "null") == 0) {
        ctx.pos += 4;
        ctx.column += 4;
        return JsonValue();
      }
      ctx.error("Expected 'null'");
    } else if (c == '-' || (c >= '0' && c <= '9')) {
      return parseNumber(ctx);
    }

    ctx.error(std::string("Unexpected character: ") + c);
    return JsonValue();
  }
}

JsonValue::JsonValue() : type(Null) {}

JsonValue::JsonValue(const std::string& str) : type(String), stringValue(str) {}

JsonValue::JsonValue(double num) : type(Number), numberValue(num) {}

JsonValue::JsonValue(bool b) : type(Boolean), boolValue(b) {}

JsonValue::JsonValue(const JsonValue& other) {
  copyFrom(other);
}

JsonValue& JsonValue::operator=(const JsonValue& other) {
  if (this != &other) {
    copyFrom(other);
  }
  return *this;
}

void JsonValue::copyFrom(const JsonValue& other) {
  type = other.type;
  stringValue = other.stringValue;
  numberValue = other.numberValue;
  boolValue = other.boolValue;
  objectValue = other.objectValue;
  arrayValue = other.arrayValue;
}

bool JsonValue::isString() const { return type == String; }
bool JsonValue::isNumber() const { return type == Number; }
bool JsonValue::isBoolean() const { return type == Boolean; }
bool JsonValue::isObject() const { return type == Object; }
bool JsonValue::isArray() const { return type == Array; }
bool JsonValue::isNull() const { return type == Null; }

std::string JsonValue::asString() const {
  if (type == String) return stringValue;
  if (type == Number) {
    if (std::isinf(numberValue)) {
      return numberValue > 0 ? "Infinity" : "-Infinity";
    }
    if (std::isnan(numberValue)) return "NaN";

    std::ostringstream oss;
    if (numberValue == std::floor(numberValue) && 
        numberValue <= 9007199254740991.0 && 
        numberValue >= -9007199254740991.0) {
      oss << static_cast<long long>(numberValue);
    } else {
      oss << std::setprecision(17) << numberValue;
    }
    return oss.str();
  }
  if (type == Boolean) return boolValue ? "true" : "false";
  if (type == Null) return "null";
  return "";
}

double JsonValue::asNumber() const {
  if (type == Number) return numberValue;
  if (type == String) {
    try {
      return std::stod(stringValue);
    } catch (...) {
      return 0.0;
    }
  }
  if (type == Boolean) return boolValue ? 1.0 : 0.0;
  return 0.0;
}

bool JsonValue::asBoolean() const {
  if (type == Boolean) return boolValue;
  if (type == Number) return numberValue != 0.0;
  if (type == String) return !stringValue.empty();
  return false;
}

bool JsonValue::has(const std::string& key) const {
  return isObject() && objectValue.find(key) != objectValue.end();
}

const JsonValue& JsonValue::get(const std::string& key) const {
  static const JsonValue nullValue;
  if (!isObject()) return nullValue;
  auto it = objectValue.find(key);
  if (it != objectValue.end()) return it->second;
  return nullValue;
}

JsonValue& JsonValue::operator[](const std::string& key) {
  if (type != Object && type != Null) {
    throw std::runtime_error("Cannot use operator[] on non-object value");
  }
  if (type == Null) {
    type = Object;
  }
  return objectValue[key];
}

const JsonValue& JsonValue::at(size_t index) const {
  static const JsonValue nullValue;
  if (!isArray() || index >= arrayValue.size()) return nullValue;
  return arrayValue[index];
}

JsonValue& JsonValue::at(size_t index) {
  static JsonValue nullValue;
  if (!isArray() || index >= arrayValue.size()) return nullValue;
  return arrayValue[index];
}

size_t JsonValue::size() const {
  if (isArray()) return arrayValue.size();
  if (isObject()) return objectValue.size();
  return 0;
}

void JsonValue::push_back(const JsonValue& val) {
  if (type != Array && type != Null) {
    throw std::runtime_error("Cannot push_back on non-array value");
  }
  if (type == Null) {
    type = Array;
  }
  arrayValue.push_back(val);
}

JsonValue json_decode(const std::string& json_str) {
  ParseContext ctx(json_str);

  if (json_str.size() >= 3 && 
      static_cast<unsigned char>(json_str[0]) == 0xEF &&
      static_cast<unsigned char>(json_str[1]) == 0xBB &&
      static_cast<unsigned char>(json_str[2]) == 0xBF) {
    ctx.pos = 3;
    ctx.column = 4;
  }

  skipWhitespace(ctx);

  if (ctx.pos >= ctx.input.size()) {
    throw JsonParseError("Empty input", ctx.line, ctx.column);
  }

  JsonValue result = parseValue(ctx);

  skipWhitespace(ctx);

  if (ctx.pos < ctx.input.size()) {
    ctx.error("Unexpected trailing characters");
  }

  return result;
}

std::string json_encode(const JsonValue& value, bool pretty) {
  std::ostringstream oss;

  if (value.isNull()) {
    oss << "null";
  } else if (value.isBoolean()) {
    oss << (value.boolValue ? "true" : "false");
  } else if (value.isNumber()) {
    oss << value.asString();
  } else if (value.isString()) {
    oss << '"';
    for (char c : value.stringValue) {
      switch (c) {
        case '"': oss << "\\\""; break;
        case '\\': oss << "\\\\"; break;
        case '/': oss << "\\/"; break;
        case '\b': oss << "\\b"; break;
        case '\f': oss << "\\f"; break;
        case '\n': oss << "\\n"; break;
        case '\r': oss << "\\r"; break;
        case '\t': oss << "\\t"; break;
        default:
          if (isControlCharacter(c)) {
            oss << "\\u" << std::hex << std::setw(4) << std::setfill('0') 
                << static_cast<int>(static_cast<unsigned char>(c));
          } else {
            oss << c;
          }
          break;
      }
    }
    oss << '"';
  } else if (value.isArray()) {
    oss << '[';
    if (pretty && !value.arrayValue.empty()) oss << '\n';

    for (size_t i = 0; i < value.arrayValue.size(); i++) {
      if (pretty) {
        oss << std::string(2, ' ');
      }

      oss << json_encode(value.arrayValue[i], pretty);

      if (i != value.arrayValue.size() - 1) {
        oss << ',';
      }

      if (pretty) oss << '\n';
    }

    if (pretty && !value.arrayValue.empty()) {
      oss << ' ';
    }
    oss << ']';
  } else if (value.isObject()) {
    oss << '{';
    if (pretty && !value.objectValue.empty()) oss << '\n';

    size_t i = 0;
    for (const auto& pair : value.objectValue) {
      if (pretty) {
        oss << std::string(2, ' ');
      }

      oss << '"';
      for (char c : pair.first) {
        switch (c) {
          case '"': oss << "\\\""; break;
          case '\\': oss << "\\\\"; break;
          case '/': oss << "\\/"; break;
          case '\b': oss << "\\b"; break;
          case '\f': oss << "\\f"; break;
          case '\n': oss << "\\n"; break;
          case '\r': oss << "\\r"; break;
          case '\t': oss << "\\t"; break;
          default:
            if (isControlCharacter(c)) {
              oss << "\\u" << std::hex << std::setw(4) << std::setfill('0') 
                  << static_cast<int>(static_cast<unsigned char>(c));
            } else {
              oss << c;
            }
            break;
        }
      }
      oss << '"';

      oss << ':';
      if (pretty) oss << ' ';

      oss << json_encode(pair.second, pretty);

      if (++i != value.objectValue.size()) {
        oss << ',';
      }

      if (pretty) oss << '\n';
    }

    if (pretty && !value.objectValue.empty()) {
      oss << ' ';
    }
    oss << '}';
  }

  return oss.str();
}

std::string json_get_string(const JsonValue& json, const std::string& key, 
                           const std::string& defaultValue) {
  if (json.isObject() && json.has(key)) {
    const JsonValue& val = json.get(key);
    if (val.isString() || val.isNumber() || val.isBoolean() || val.isNull()) {
      return val.asString();
    }
  }
  return defaultValue;
}

double json_get_number(const JsonValue& json, const std::string& key, 
                      double defaultValue) {
  if (json.isObject() && json.has(key)) {
    const JsonValue& val = json.get(key);
    if (val.isNumber() || val.isString() || val.isBoolean()) {
      return val.asNumber();
    }
  }
  return defaultValue;
}

bool json_get_bool(const JsonValue& json, const std::string& key, 
                  bool defaultValue) {
  if (json.isObject() && json.has(key)) {
    const JsonValue& val = json.get(key);
    if (val.isBoolean() || val.isNumber() || val.isString()) {
      return val.asBoolean();
    }
  }
  return defaultValue;
}

JsonValue json_get_object(const JsonValue& json, const std::string& key) {
  if (json.isObject() && json.has(key)) {
    const JsonValue& val = json.get(key);
    if (val.isObject()) {
      return val;
    }
  }
  return JsonValue();
}

JsonValue json_get_array(const JsonValue& json, const std::string& key) {
  if (json.isObject() && json.has(key)) {
    const JsonValue& val = json.get(key);
    if (val.isArray()) {
      return val;
    }
  }
  return JsonValue();
}

bool json_has_key(const JsonValue& json, const std::string& key) {
  return json.isObject() && json.has(key);
}

}
