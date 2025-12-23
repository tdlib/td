//
// Copyright testertesterov (https://t.me/testertesterov), Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include <iostream>
#include <string>
#include <cstdlib>
#include <td/telegram/td_json_client.h>
#include "td_json_parser.h"

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
    try {
      td::JsonValue json = td::json_decode(response);

      if (!json.isObject()) return;

      std::string type = td::json_get_string(json, "@type");

      if (type == "updateAuthorizationState") {
        td::JsonValue authState = td::json_get_object(json, "authorization_state");
        std::string authType = td::json_get_string(authState, "@type");

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
        } else if (authType == "authorizationStateWaitEmailAddress") {
          std::cout << "Please enter your email address: ";
          std::string email;
          std::getline(std::cin, email);

          std::string query = R"({
            "@type": "setAuthenticationEmailAddress",
            "email_address": ")" + email + R"("
          })";
          send_query(query);
        } else if (authType == "authorizationStateWaitEmailCode") {
          std::cout << "Please enter the email authentication code: ";
          std::string code;
          std::getline(std::cin, code);

          std::string query = R"({
            "@type": "checkAuthenticationEmailCode",
            "code": {
              "@type": "emailAddressAuthenticationCode",
              "code": ")" + code + R"("
            }
          })";
          send_query(query);
        } else if (authType == "authorizationStateWaitRegistration") {
          std::cout << "Please enter your first name: ";
          std::string first_name;
          std::getline(std::cin, first_name);

          std::cout << "Please enter your last name: ";
          std::string last_name;
          std::getline(std::cin, last_name);

          std::string query = R"({
            "@type": "registerUser",
            "first_name": ")" + first_name + R"(",
            "last_name": ")" + last_name + R"("
          })";
          send_query(query);
        }
      } else if (type == "error") {
        std::cout << "ERROR: " << response << std::endl;
        std::string message = td::json_get_string(json, "message");
        if (message.find("Valid api_id must be provided") != std::string::npos) {
          std::cout << "\n=== API CREDENTIALS ERROR ===" << std::endl;
          std::cout << "Please check your api_id and api_hash." << std::endl;
          std::cout << "Get them from: https://my.telegram.org" << std::endl;
          need_quit = true;
        }
      }
    } catch (const td::JsonParseError& e) {
      std::cerr << "JSON parse error at line " << e.line() << ", column " << e.column()
                << ": " << e.what() << std::endl;
    } catch (const std::exception& e) {
      std::cerr << "Error processing response: " << e.what() << std::endl;
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
  std::cout << "Created TDLib client with ID: " << client_id << std::endl;

  std::cout << "Testing TDLib execute method..." << std::endl;
  const char* test_result = td_execute(
    "{\"@type\":\"getTextEntities\","
    "\"text\":\"@telegram /test_command https://telegram.org telegram.me\"}"
  );

  if (test_result) {
    try {
      std::cout << "Text entities: " << test_result << std::endl;
      td::JsonValue json = td::json_decode(test_result);

      if (json.isObject()) {
        td::JsonValue entities = td::json_get_array(json, "entities");

        std::cout << "\nParsed " << entities.size() << " text entities:" << std::endl;
        for (size_t i = 0; i < entities.size(); i++) {
          td::JsonValue entity = entities.at(i);
          int offset = static_cast<int>(td::json_get_number(entity, "offset"));
          int length = static_cast<int>(td::json_get_number(entity, "length"));
          td::JsonValue type = td::json_get_object(entity, "type");
          std::string type_name = td::json_get_string(type, "@type");

          std::cout << "  " << i + 1 << ". Offset: " << offset 
                    << ", Length: " << length 
                    << ", Type: " << type_name << std::endl;
        }
      }
    } catch (const td::JsonParseError& e) {
      std::cerr << "Failed to parse test result: " << e.what() 
                << " at line " << e.line() << ", column " << e.column() << std::endl;
    }
  }

  TDLibAuthHelper auth_helper(client_id);
  auth_helper.run_auth_flow();

  if (auth_helper.is_auth_complete()) {
    std::cout << "\n=== AUTHORIZATION SUCCESSFUL ===" << std::endl;
    std::cout << "Starting main event loop. Press Ctrl+C to exit." << std::endl;

    td_send(client_id, "{\"@type\":\"getMe\"}");

    const double WAIT_TIMEOUT = 1.0;
    while (true) {
      const char *result = td_receive(WAIT_TIMEOUT);
      if (result != nullptr && result[0] != '\0') {
        try {
          td::JsonValue json = td::json_decode(result);
          std::string type = td::json_get_string(json, "@type");

          std::cout << "\n[" << type << "] ";

          if (type == "updateMessageSendSucceeded") {
            td::JsonValue msg = td::json_get_object(json, "message");
            td::JsonValue content = td::json_get_object(msg, "content");
            std::string text = td::json_get_string(content, "text");
            if (!text.empty()) {
              std::cout << "Message: " << text.substr(0, 50) 
                        << (text.length() > 50 ? "..." : "");
            }
          } else if (type == "updateUser") {
            td::JsonValue user = td::json_get_object(json, "user");
            std::string first_name = td::json_get_string(user, "first_name");
            std::string last_name = td::json_get_string(user, "last_name");
            std::cout << "User update: " << first_name << " " << last_name;
          } else if (type == "updateNewMessage") {
            td::JsonValue msg = td::json_get_object(json, "message");
            td::JsonValue sender_id = td::json_get_object(msg, "sender_id");
            std::string sender = td::json_get_string(sender_id, "@type");
            std::cout << "New message from: " << sender;
          } else if (type == "updateAuthorizationState") {
            td::JsonValue authState = td::json_get_object(json, "authorization_state");
            std::string authType = td::json_get_string(authState, "@type");
            if (authType == "authorizationStateClosed") {
              std::cout << "Authorization closed. Exiting..." << std::endl;
              break;
            }
          }

          std::cout << std::endl;

        } catch (const td::JsonParseError& e) {
          std::cerr << "Failed to parse update: " << e.what() << std::endl;
        }
      }
    }
  }

  std::cout << "\nExiting..." << std::endl;
  return 0;
}
