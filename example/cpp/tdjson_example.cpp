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
      //std::cout << "Receive: " << response << std::endl;

      if (response.find("\"@type\":\"updateAuthorizationState\"") != std::string::npos) {
        if (response.find("authorizationStateWaitTdlibParameters") != std::string::npos) {
          if (!parameters_sent) {
            send_tdlib_parameters();
            parameters_sent = true;
          }
        } else if (response.find("authorizationStateWaitPhoneNumber") != std::string::npos) {
          std::cout << "Please enter phone number (international format): ";
          std::string phone_number;
          std::getline(std::cin, phone_number);

          std::string query = R"({
              "@type": "setAuthenticationPhoneNumber",
              "phone_number": ")" + phone_number + R"("
          })";
          send_query(query);
        } else if (response.find("authorizationStateWaitCode") != std::string::npos) {
          std::cout << "Please enter code: ";
          std::string code;
          std::getline(std::cin, code);

          std::string query = R"({
              "@type": "checkAuthenticationCode",
              "code": ")" + code + R"("
          })";
          send_query(query);
        } else if (response.find("authorizationStateWaitPassword") != std::string::npos) {
          std::cout << "Please enter password: ";
          std::string password;
          std::getline(std::cin, password);

          std::string query = R"({
              "@type": "checkAuthenticationPassword",
              "password": ")" + password + R"("
          })";
          send_query(query);
        } else if (response.find("authorizationStateReady") != std::string::npos) {
          std::cout << "Authorization complete! You are now logged in." << std::endl;
          is_authorized = true;
          need_quit = true;
        } else if (response.find("authorizationStateClosed") != std::string::npos) {
          std::cout << "Authorization state closed." << std::endl;
          need_quit = true;
        } else if (response.find("authorizationStateWaitPremiumPurchase") != std::string::npos) {
          std::cout << "Telegram Premium subscription is required." << std::endl;
          need_quit = true;
        }
      } else if (response.find("\"@type\":\"error\"") != std::string::npos) {
        std::cout << "ERROR: " << response << std::endl;
        if (response.find("Valid api_id must be provided") != std::string::npos) {
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
  }

  TDLibAuthHelper auth_helper(client_id);

  auth_helper.run_auth_flow();

  if (auth_helper.is_auth_complete()) {
    const double WAIT_TIMEOUT = 1.0;
    while (true) {
      const char *result = td_receive(WAIT_TIMEOUT);
      if (result != nullptr && result[0] != '\0') {
        std::cout << result << std::endl;
      }
    }
  }

  std::cout << "Exiting..." << std::endl;
  return 0;
}
