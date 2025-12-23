//
// Copyright testertesterov (https://t.me/testertesterov), Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include <iostream>
#include <string>
#include <cstdlib>
#include <map>
#include <vector>
#include <sstream>
#include <cctype>
#include <td/telegram/td_json_client.h>

namespace SimpleJson {
  class Value {
    public:
      enum Type { Null, String, Number, Boolean, Object, Array };

      Type type;
      std::string stringValue;
      double numberValue;
      bool boolValue;
      std::map<std::string, Value> objectValue;
      std::vector<Value> arrayValue;

      Value() : type(Null) {}
      Value(const std::string& str) : type(String), stringValue(str) {}
      Value(double num) : type(Number), numberValue(num) {}
      Value(bool b) : type(Boolean), boolValue(b) {}

      bool isString() const { return type == String; }
      bool isNumber() const { return type == Number; }
      bool isBoolean() const { return type == Boolean; }
      bool isObject() const { return type == Object; }
      bool isArray() const { return type == Array; }
      bool isNull() const { return type == Null; }

      std::string asString() const {
        if (type == String) return stringValue;
        if (type == Number) return std::to_string(numberValue);
        if (type == Boolean) return boolValue ? "true" : "false";
        return "";
      }

      double asNumber() const {
        if (type == Number) return numberValue;
        if (type == String) return std::atof(stringValue.c_str());
        if (type == Boolean) return boolValue ? 1.0 : 0.0;
        return 0.0;
      }

      bool asBoolean() const {
        if (type == Boolean) return boolValue;
        if (type == Number) return numberValue != 0.0;
        if (type == String) return !stringValue.empty();
        return false;
      }

      bool has(const std::string& key) const {
        return isObject() && objectValue.find(key) != objectValue.end();
      }

      const Value& get(const std::string& key) const {
        static Value nullValue;
        if (!isObject()) return nullValue;
        auto it = objectValue.find(key);
        if (it != objectValue.end()) return it->second;
        return nullValue;
      }

      Value& operator[](const std::string& key) {
        type = Object;
        return objectValue[key];
      }

      size_t size() const {
        if (isArray()) return arrayValue.size();
        if (isObject()) return objectValue.size();
        return 0;
      }
  };

  Value parseValue(const std::string& str, size_t& pos);
  std::string parseString(const std::string& str, size_t& pos);
  Value parseNumber(const std::string& str, size_t& pos);
  Value parseArray(const std::string& str, size_t& pos);
  Value parseObject(const std::string& str, size_t& pos);

  void skipWhitespace(const std::string& str, size_t& pos) {
    while (pos < str.size() && std::isspace(str[pos])) pos++;
  }

  std::string parseString(const std::string& str, size_t& pos) {
    if (pos >= str.size() || str[pos] != '"') return "";
    pos++;
    std::string result;
    while (pos < str.size() && str[pos] != '"') {
      if (str[pos] == '\\' && pos + 1 < str.size()) {
        pos++;
        switch (str[pos]) {
          case '"': result += '"'; break;
          case '\\': result += '\\'; break;
          case '/': result += '/'; break;
          case 'b': result += '\b'; break;
          case 'f': result += '\f'; break;
          case 'n': result += '\n'; break;
          case 'r': result += '\r'; break;
          case 't': result += '\t'; break;
          default: result += str[pos]; break;
        }
      } else {
        result += str[pos];
      }
      pos++;
    }
    if (pos < str.size() && str[pos] == '"') pos++;
    return result;
  }

  Value parseNumber(const std::string& str, size_t& pos) {
    size_t start = pos;
    bool isFloat = false;
    while (pos < str.size()) {
      char c = str[pos];
      if (c == '.' || c == 'e' || c == 'E' || c == '-' || c == '+') {
        isFloat = true;
      } else if (!std::isdigit(c)) {
        break;
      }
      pos++;
    }
    std::string numStr = str.substr(start, pos - start);
    return Value(std::atof(numStr.c_str()));
  }

  Value parseArray(const std::string& str, size_t& pos) {
    Value arr;
    arr.type = Value::Array;
    if (pos >= str.size() || str[pos] != '[') return arr;
    pos++;

    skipWhitespace(str, pos);
    if (pos < str.size() && str[pos] == ']') {
      pos++;
      return arr;
    }

    while (pos < str.size()) {
      arr.arrayValue.push_back(parseValue(str, pos));
      skipWhitespace(str, pos);
      if (pos < str.size() && str[pos] == ',') {
        pos++;
        skipWhitespace(str, pos);
      } else if (pos < str.size() && str[pos] == ']') {
        pos++;
        break;
      }
    }
    return arr;
  }

  Value parseObject(const std::string& str, size_t& pos) {
    Value obj;
    obj.type = Value::Object;
    if (pos >= str.size() || str[pos] != '{') return obj;
    pos++;

    skipWhitespace(str, pos);
    if (pos < str.size() && str[pos] == '}') {
      pos++;
      return obj;
    }

    while (pos < str.size()) {
      skipWhitespace(str, pos);
      if (str[pos] != '"') break;
      std::string key = parseString(str, pos);
      skipWhitespace(str, pos);
      if (pos < str.size() && str[pos] == ':') pos++;
      skipWhitespace(str, pos);
      obj.objectValue[key] = parseValue(str, pos);
      skipWhitespace(str, pos);
      if (pos < str.size() && str[pos] == ',') {
        pos++;
        skipWhitespace(str, pos);
      } else if (pos < str.size() && str[pos] == '}') {
        pos++;
        break;
      }
    }
    return obj;
  }

  Value parseValue(const std::string& str, size_t& pos) {
    skipWhitespace(str, pos);
    if (pos >= str.size()) return Value();

    char c = str[pos];
    if (c == '"') {
      return Value(parseString(str, pos));
    } else if (c == '{') {
      return parseObject(str, pos);
    } else if (c == '[') {
      return parseArray(str, pos);
    } else if (c == 't' && str.substr(pos, 4) == "true") {
      pos += 4;
      return Value(true);
    } else if (c == 'f' && str.substr(pos, 5) == "false") {
      pos += 5;
      return Value(false);
    } else if (c == 'n' && str.substr(pos, 4) == "null") {
      pos += 4;
      return Value();
    } else if (c == '-' || (c >= '0' && c <= '9')) {
      return parseNumber(str, pos);
    }
    return Value();
  }

  Value parse(const std::string& jsonStr) {
    size_t pos = 0;
    return parseValue(jsonStr, pos);
  }

  std::string getString(const Value& json, const std::string& key, const std::string& defaultValue = "") {
    if (json.isObject() && json.has(key) && json.get(key).isString()) {
      return json.get(key).asString();
    }
    return defaultValue;
  }

  double getNumber(const Value& json, const std::string& key, double defaultValue = 0.0) {
    if (json.isObject() && json.has(key) && json.get(key).isNumber()) {
      return json.get(key).asNumber();
    }
    return defaultValue;
  }

  bool getBoolean(const Value& json, const std::string& key, bool defaultValue = false) {
    if (json.isObject() && json.has(key) && json.get(key).isBoolean()) {
      return json.get(key).asBoolean();
    }
    return defaultValue;
  }

  Value getObject(const Value& json, const std::string& key) {
    if (json.isObject() && json.has(key) && json.get(key).isObject()) {
      return json.get(key);
    }
    return Value();
  }

  std::string getType(const Value& json, const std::string& key) {
    if (!json.isObject() || !json.has(key)) return "null";

    const Value& val = json.get(key);
    if (val.isString()) return "string";
    if (val.isNumber()) return "number";
    if (val.isBoolean()) return "boolean";
    if (val.isObject()) return "object";
    if (val.isArray()) return "array";
    return "null";
  }

  bool hasKey(const Value& json, const std::string& key) {
    return json.isObject() && json.has(key);
  }
}

class TDLibAuthHelper {
  private:
    int client_id;
    bool is_authorized;
    bool need_quit;
    bool parameters_sent;

    void send_query(const std::string& query) {
      td_send(client_id, query.c_str());
    }

    void process_response(const std::string& response) {
      SimpleJson::Value json = SimpleJson::parse(response);

      if (!json.isObject()) return;

      std::string type = SimpleJson::getString(json, "@type");

      if (type == "updateAuthorizationState") {
        SimpleJson::Value authState = SimpleJson::getObject(json, "authorization_state");
        std::string authType = SimpleJson::getString(authState, "@type");

        if (authType == "authorizationStateWaitTdlibParameters") {
          if (!parameters_sent) {
            send_tdlib_parameters();
            parameters_sent = true;
          }
        } else if (authType == "authorizationStateWaitPhoneNumber") {
          std::cout << "Please enter phone number (international format): ";
          std::string phone_number;
          std::getline(std::cin, phone_number);

          std::string query = R"({
            "@type": "setAuthenticationPhoneNumber",
            "phone_number": ")" + phone_number + R"("
          })";
          send_query(query);
        } else if (authType == "authorizationStateWaitCode") {
          std::cout << "Please enter code: ";
          std::string code;
          std::getline(std::cin, code);

          std::string query = R"({
            "@type": "checkAuthenticationCode",
            "code": ")" + code + R"("
          })";
          send_query(query);
        } else if (authType == "authorizationStateWaitPassword") {
          std::cout << "Please enter password: ";
          std::string password;
          std::getline(std::cin, password);

          std::string query = R"({
            "@type": "checkAuthenticationPassword",
            "password": ")" + password + R"("
          })";
          send_query(query);
        } else if (authType == "authorizationStateReady") {
          std::cout << "Authorization complete! You are now logged in." << std::endl;
          is_authorized = true;
          need_quit = true;
        } else if (authType == "authorizationStateClosed") {
          std::cout << "Authorization state closed." << std::endl;
          need_quit = true;
        } else if (authType == "authorizationStateWaitPremiumPurchase") {
          std::cout << "Telegram Premium subscription is required." << std::endl;
          need_quit = true;
        }
      } else if (type == "error") {
        std::cout << "ERROR: " << response << std::endl;
        std::string message = SimpleJson::getString(json, "message");
        if (message.find("Valid api_id must be provided") != std::string::npos) {
          std::cout << "\n=== API CREDENTIALS ERROR ===" << std::endl;
          std::cout << "Please check your api_id and api_hash." << std::endl;
          std::cout << "Get them from: https://my.telegram.org" << std::endl;
          need_quit = true;
        }
      }
    }

    void send_tdlib_parameters() {
      std::string query = R"({
        "@type": "setTdlibParameters",
        "database_directory": "tdlib_data",
        "use_message_database": true,
        "use_secret_chats": true,
        "api_id": )" + get_api_id() + R"(,
        "api_hash": ")" + get_api_hash() + R"(",
        "system_language_code": "en",
        "device_model": "C++ TDLib Client",
        "application_version": "1.1"
      })";

      std::cout << "Setting TDLib parameters..." << std::endl;
      send_query(query);
    }

    std::string get_api_id() {
      const char* api_id_env = std::getenv("TD_API_ID");
      if (api_id_env) {
        return api_id_env;
      }
      return "94575";
    }

    std::string get_api_hash() {
      const char* api_hash_env = std::getenv("TD_API_HASH");
      if (api_hash_env) {
        return api_hash_env;
      }
      return "a3406de8d171bb422bb6ddf3bbd800e2";
    }

  public:
    TDLibAuthHelper(int id) : client_id(id), is_authorized(false), 
                              need_quit(false), parameters_sent(false) {}

    void run_auth_flow() {
      const double WAIT_TIMEOUT = 1.0;

      std::cout << "Starting Telegram authentication flow..." << std::endl;
      std::cout << "Press Ctrl+C to cancel at any time." << std::endl;

      send_query("{\"@type\":\"getOption\",\"name\":\"version\"}");

      while (!need_quit) {
        const char *result = td_receive(WAIT_TIMEOUT);
        if (result != nullptr && result[0] != '\0') {
          process_response(result);
        }
      }
    }

    bool is_auth_complete() const {
      return is_authorized;
    }
};

int main() {
  td_execute("{\"@type\":\"setLogVerbosityLevel\",\"new_verbosity_level\":1}");

  int client_id = td_create_client_id();

  std::cout << "Testing TDLib execute method..." << std::endl;
  const char* test_result = td_execute(
    "{\"@type\":\"getTextEntities\","
    "\"text\":\"@telegram /test_command https://telegram.org telegram.me\"}"
  );
  if (test_result) {
    std::cout << "Text entities: " << test_result << std::endl;

    SimpleJson::Value json = SimpleJson::parse(test_result);
    if (json.isArray()) {
      std::cout << "Found " << json.size() << " text entities" << std::endl;
    }
  }

  TDLibAuthHelper auth_helper(client_id);

  auth_helper.run_auth_flow();

  if (auth_helper.is_auth_complete()) {
    const double WAIT_TIMEOUT = 1.0;
    while (true) {
      const char *result = td_receive(WAIT_TIMEOUT);
      if (result != nullptr && result[0] != '\0') {
        SimpleJson::Value json = SimpleJson::parse(result);
        std::string type = SimpleJson::getString(json, "@type");
        std::cout << "[" << type << "] ";

        if (type == "updateMessageSendSucceeded") {
          SimpleJson::Value msg = SimpleJson::getObject(json, "message");
          SimpleJson::Value content = SimpleJson::getObject(msg, "content");
          std::string text = SimpleJson::getString(content, "text");
          if (!text.empty()) {
            std::cout << "Message: " << text.substr(0, 50) << (text.length() > 50 ? "..." : "");
          }
        }
        std::cout << std::endl;
      }
    }
  }

  return 0;
}
