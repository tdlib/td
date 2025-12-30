//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include <td/telegram/Client.h>
#include <td/telegram/td_api.h>
#include <td/telegram/td_api.hpp>

#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>
#include <chrono>

namespace detail {
  template <class... Fs>
  struct overload;

  template <class F>
  struct overload<F> : public F {
    explicit overload(F f) : F(std::move(f)) {
    }
  };
  template <class F, class... Fs>
  struct overload<F, Fs...>
      : public overload<F>
      , public overload<Fs...> {
    overload(F f, Fs... fs) : overload<F>(std::move(f)), overload<Fs...>(std::move(fs)...) {
    }
    using overload<F>::operator();
    using overload<Fs...>::operator();
  };
}

template <class... F>
auto overloaded(F... f) {
  return detail::overload<F...>(std::move(f)...);
}

namespace td_api = td::td_api;

class TdExample {
 public:
  TdExample() {
    td::ClientManager::execute(td_api::make_object<td_api::setLogVerbosityLevel>(1));
    client_manager_ = std::make_unique<td::ClientManager>();
    client_id_ = client_manager_->create_client_id();
    send_query(td_api::make_object<td_api::getOption>("version"), {});

    input_thread_ = std::thread(&TdExample::input_loop, this);
  }

  ~TdExample() {
    stop_input_thread_ = true;
    if (input_thread_.joinable()) {
      input_thread_.join();
    }
  }

  void loop() {
    while (true) {
      if (need_restart_) {
        restart();
      } else if (!are_authorized_) {
        process_responses();
        process_auth_commands();
      } else {
        process_responses();
        process_user_commands();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    }
  }

 private:
  using Object = td_api::object_ptr<td_api::Object>;
  std::unique_ptr<td::ClientManager> client_manager_;
  std::int32_t client_id_{0};

  td_api::object_ptr<td_api::AuthorizationState> authorization_state_;
  bool are_authorized_{false};
  bool need_restart_{false};
  std::uint64_t current_query_id_{0};
  std::uint64_t authentication_query_id_{0};

  std::map<std::uint64_t, std::function<void(Object)>> handlers_;

  std::map<std::int64_t, td_api::object_ptr<td_api::user>> users_;
  std::map<std::int64_t, std::string> chat_title_;

  std::queue<std::string> auth_command_queue_;
  std::mutex auth_queue_mutex_;

  std::queue<std::string> user_command_queue_;
  std::mutex user_queue_mutex_;

  std::thread input_thread_;
  std::atomic<bool> stop_input_thread_{false};

  enum class AuthStage {
    None,
    PhoneNumber,
    EmailAddress,
    EmailCode,
    Code,
    Password,
    FirstName,
    LastName
  };
  AuthStage current_auth_stage_{AuthStage::None};
  std::string first_name_for_registration_;

  std::mutex auth_state_mutex_;
  bool auth_state_updated_{false};

  void input_loop() {
    while (!stop_input_thread_.load(std::memory_order_acquire)) {
      std::string line;
      if (std::getline(std::cin, line)) {
        if (are_authorized_) {
          std::lock_guard<std::mutex> lock(user_queue_mutex_);
          user_command_queue_.push(std::move(line));
        } else {
          std::lock_guard<std::mutex> lock(auth_queue_mutex_);
          auth_command_queue_.push(std::move(line));
        }
      }
    }
  }

  void process_responses() {
    while (true) {
      auto response = client_manager_->receive(0);
      if (response.object) {
        process_response(std::move(response));
      } else {
        break;
      }
    }
  }

  void process_auth_commands() {
    std::vector<std::string> commands;
    {
      std::lock_guard<std::mutex> lock(auth_queue_mutex_);
      while (!auth_command_queue_.empty()) {
        commands.push_back(std::move(auth_command_queue_.front()));
        auth_command_queue_.pop();
      }
    }

    for (const auto& input : commands) {
      if (input.empty()) continue;

      switch (current_auth_stage_) {
        case AuthStage::PhoneNumber:
          send_query(td_api::make_object<td_api::setAuthenticationPhoneNumber>(input, nullptr),
                     [this](Object object) { check_authentication_error(std::move(object)); });
          break;
        case AuthStage::EmailAddress:
          send_query(td_api::make_object<td_api::setAuthenticationEmailAddress>(input),
                     [this](Object object) { check_authentication_error(std::move(object)); });
          break;
        case AuthStage::EmailCode:
          send_query(td_api::make_object<td_api::checkAuthenticationEmailCode>(
                       td_api::make_object<td_api::emailAddressAuthenticationCode>(input)),
                     [this](Object object) { check_authentication_error(std::move(object)); });
          break;
        case AuthStage::Code:
          send_query(td_api::make_object<td_api::checkAuthenticationCode>(input),
                     [this](Object object) { check_authentication_error(std::move(object)); });
          break;
        case AuthStage::Password:
          send_query(td_api::make_object<td_api::checkAuthenticationPassword>(input),
                     [this](Object object) { check_authentication_error(std::move(object)); });
          break;
        case AuthStage::FirstName:
          first_name_for_registration_ = input;
          current_auth_stage_ = AuthStage::LastName;
          std::cout << "Enter your last name: " << std::flush;
          break;
        case AuthStage::LastName:
          send_query(td_api::make_object<td_api::registerUser>(
                       first_name_for_registration_, input, false),
                     [this](Object object) { check_authentication_error(std::move(object)); });
          current_auth_stage_ = AuthStage::None;
          break;
        case AuthStage::None:
          break;
      }
    }
  }

  void process_user_commands() {
    std::vector<std::string> commands;
    {
      std::lock_guard<std::mutex> lock(user_queue_mutex_);
      while (!user_command_queue_.empty()) {
        commands.push_back(std::move(user_command_queue_.front()));
        user_command_queue_.pop();
      }
    }

    for (const auto& line : commands) {
      if (line.empty()) continue;

      std::istringstream ss(line);
      std::string action;
      if (!(ss >> action)) {
        continue;
      }

      if (action == "q") {
        std::exit(0);
      }
      if (action == "u") {
        std::cout << "Checking for updates..." << std::endl;
      } else if (action == "close") {
        std::cout << "Closing..." << std::endl;
        send_query(td_api::make_object<td_api::close>(), {});
      } else if (action == "me") {
        send_query(td_api::make_object<td_api::getMe>(),
                   [this](Object object) { std::cout << to_string(object) << std::endl; });
      } else if (action == "l") {
        std::cout << "Logging out..." << std::endl;
        send_query(td_api::make_object<td_api::logOut>(), {});
      } else if (action == "m") {
        std::int64_t chat_id;
        if (!(ss >> chat_id)) {
          std::cout << "Invalid chat ID" << std::endl;
          continue;
        }

        ss.get();
        std::string text;
        std::getline(ss, text);

        if (text.empty()) {
          std::cout << "Message text cannot be empty" << std::endl;
          continue;
        }

        std::cout << "Sending message to chat " << chat_id << "..." << std::endl;
        auto send_message = td_api::make_object<td_api::sendMessage>();
        send_message->chat_id_ = chat_id;
        auto message_content = td_api::make_object<td_api::inputMessageText>();
        message_content->text_ = td_api::make_object<td_api::formattedText>();
        message_content->text_->text_ = std::move(text);
        send_message->input_message_content_ = std::move(message_content);

        send_query(std::move(send_message), {});
      } else if (action == "c") {
        std::cout << "Loading chat list..." << std::endl;
        send_query(td_api::make_object<td_api::getChats>(nullptr, 20), [this](Object object) {
          if (object->get_id() == td_api::error::ID) {
            auto error = td::move_tl_object_as<td_api::error>(object);
            std::cout << "Error loading chats: " << to_string(error) << std::endl;
            return;
          }
          auto chats = td::move_tl_object_as<td_api::chats>(object);
          for (auto chat_id : chats->chat_ids_) {
            auto it = chat_title_.find(chat_id);
            if (it != chat_title_.end()) {
              std::cout << "[chat_id:" << chat_id << "] [title:" << it->second << "]" << std::endl;
            } else {
              std::cout << "[chat_id:" << chat_id << "] [title:unknown]" << std::endl;
            }
          }
        });
      } else {
        std::cout << "Unknown command. Available commands:\n"
                  << "  q - quit\n"
                  << "  u - check for updates\n"
                  << "  c - show chats\n"
                  << "  m <chat_id> <text> - send message\n"
                  << "  me - show self\n"
                  << "  l - logout\n"
                  << "  close - close connection\n";
      }
    }
  }

  void restart() {
    client_manager_.reset();
    client_manager_ = std::make_unique<td::ClientManager>();
    client_id_ = client_manager_->create_client_id();

    authorization_state_.reset();
    are_authorized_ = false;
    need_restart_ = false;
    current_query_id_ = 0;
    authentication_query_id_ = 0;
    current_auth_stage_ = AuthStage::None;
    first_name_for_registration_.clear();

    handlers_.clear();
    users_.clear();
    chat_title_.clear();

    {
      std::lock_guard<std::mutex> lock(auth_queue_mutex_);
      std::queue<std::string> empty;
      std::swap(auth_command_queue_, empty);
    }

    {
      std::lock_guard<std::mutex> lock(user_queue_mutex_);
      std::queue<std::string> empty;
      std::swap(user_command_queue_, empty);
    }

    send_query(td_api::make_object<td_api::getOption>("version"), {});
  }

  void send_query(td_api::object_ptr<td_api::Function> f, std::function<void(Object)> handler) {
    auto query_id = next_query_id();
    if (handler) {
      handlers_.emplace(query_id, std::move(handler));
    }
    client_manager_->send(client_id_, query_id, std::move(f));
  }

  void process_response(td::ClientManager::Response response) {
    if (!response.object) {
      return;
    }
    if (response.request_id == 0) {
      return process_update(std::move(response.object));
    }
    auto it = handlers_.find(response.request_id);
    if (it != handlers_.end()) {
      it->second(std::move(response.object));
      handlers_.erase(it);
    }
  }

  std::string get_user_name(std::int64_t user_id) const {
    auto it = users_.find(user_id);
    if (it == users_.end()) {
      return "unknown user";
    }
    std::string result = it->second->first_name_;
    if (!it->second->last_name_.empty()) {
      result += " " + it->second->last_name_;
    }
    return result;
  }

  std::string get_chat_title(std::int64_t chat_id) const {
    auto it = chat_title_.find(chat_id);
    if (it == chat_title_.end()) {
      return "unknown chat";
    }
    return it->second;
  }

  void process_update(td_api::object_ptr<td_api::Object> update) {
    td_api::downcast_call(
        *update, overloaded(
                     [this](td_api::updateAuthorizationState &update_authorization_state) {
                       {
                         std::lock_guard<std::mutex> lock(auth_state_mutex_);
                         authorization_state_ = std::move(update_authorization_state.authorization_state_);
                         auth_state_updated_ = true;
                       }
                       on_authorization_state_update();
                     },
                     [this](td_api::updateNewChat &update_new_chat) {
                       chat_title_[update_new_chat.chat_->id_] = update_new_chat.chat_->title_;
                     },
                     [this](td_api::updateChatTitle &update_chat_title) {
                       chat_title_[update_chat_title.chat_id_] = update_chat_title.title_;
                     },
                     [this](td_api::updateUser &update_user) {
                       auto user_id = update_user.user_->id_;
                       users_[user_id] = std::move(update_user.user_);
                     },
                     [this](td_api::updateNewMessage &update_new_message) {
                       auto chat_id = update_new_message.message_->chat_id_;
                       std::string sender_name;
                       td_api::downcast_call(*update_new_message.message_->sender_id_,
                                             overloaded(
                                                 [this, &sender_name](td_api::messageSenderUser &user) {
                                                   sender_name = get_user_name(user.user_id_);
                                                 },
                                                 [this, &sender_name](td_api::messageSenderChat &chat) {
                                                   sender_name = get_chat_title(chat.chat_id_);
                                                 }));
                       std::string text;
                       if (update_new_message.message_->content_->get_id() == td_api::messageText::ID) {
                         text = static_cast<td_api::messageText &>(*update_new_message.message_->content_).text_->text_;
                       }
                       std::cout << "Received message: [chat_id:" << chat_id
                                 << "] [from:" << sender_name << "] [" << text << "]" << std::endl;
                     },
                     [](auto&) {}));
  }

  void on_authorization_state_update() {
    std::lock_guard<std::mutex> lock(auth_state_mutex_);
    if (!auth_state_updated_) {
      return;
    }
    auth_state_updated_ = false;

    if (!authorization_state_) {
      return;
    }

    static AuthStage last_auth_stage = AuthStage::None;

    td_api::downcast_call(*authorization_state_,
                          overloaded(
                              [this](td_api::authorizationStateReady &) {
                                are_authorized_ = true;
                                current_auth_stage_ = AuthStage::None;
                                last_auth_stage = AuthStage::None;
                                std::cout << "\nAuthorization is completed" << std::endl;
                                print_user_help();
                              },
                              [this](td_api::authorizationStateLoggingOut &) {
                                are_authorized_ = false;
                                current_auth_stage_ = AuthStage::None;
                                last_auth_stage = AuthStage::None;
                                std::cout << "Logging out" << std::endl;
                              },
                              [this](td_api::authorizationStateClosing &) {
                                std::cout << "Closing" << std::endl;
                              },
                              [this](td_api::authorizationStateClosed &) {
                                are_authorized_ = false;
                                need_restart_ = true;
                                current_auth_stage_ = AuthStage::None;
                                last_auth_stage = AuthStage::None;
                                std::cout << "Terminated" << std::endl;
                              },
                              [this](td_api::authorizationStateWaitPhoneNumber &) {
                                if (current_auth_stage_ != AuthStage::PhoneNumber) {
                                  current_auth_stage_ = AuthStage::PhoneNumber;
                                  last_auth_stage = AuthStage::PhoneNumber;
                                  std::cout << "\nEnter phone number: " << std::flush;
                                }
                              },
                              [this](td_api::authorizationStateWaitPremiumPurchase &) {
                                std::cout << "\nTelegram Premium subscription is required" << std::endl;
                              },
                              [this](td_api::authorizationStateWaitEmailAddress &) {
                                if (current_auth_stage_ != AuthStage::EmailAddress) {
                                  current_auth_stage_ = AuthStage::EmailAddress;
                                  last_auth_stage = AuthStage::EmailAddress;
                                  std::cout << "\nEnter email address: " << std::flush;
                                }
                              },
                              [this](td_api::authorizationStateWaitEmailCode &) {
                                if (current_auth_stage_ != AuthStage::EmailCode) {
                                  current_auth_stage_ = AuthStage::EmailCode;
                                  last_auth_stage = AuthStage::EmailCode;
                                  std::cout << "\nEnter email authentication code: " << std::flush;
                                }
                              },
                              [this](td_api::authorizationStateWaitCode &) {
                                if (current_auth_stage_ != AuthStage::Code) {
                                  current_auth_stage_ = AuthStage::Code;
                                  last_auth_stage = AuthStage::Code;
                                  std::cout << "\nEnter authentication code: " << std::flush;
                                }
                              },
                              [this](td_api::authorizationStateWaitRegistration &) {
                                if (current_auth_stage_ != AuthStage::FirstName) {
                                  current_auth_stage_ = AuthStage::FirstName;
                                  last_auth_stage = AuthStage::FirstName;
                                  std::cout << "\nEnter your first name: " << std::flush;
                                }
                              },
                              [this](td_api::authorizationStateWaitPassword &) {
                                if (current_auth_stage_ != AuthStage::Password) {
                                  current_auth_stage_ = AuthStage::Password;
                                  last_auth_stage = AuthStage::Password;
                                  std::cout << "\nEnter authentication password: " << std::flush;
                                }
                              },
                              [this](td_api::authorizationStateWaitOtherDeviceConfirmation &state) {
                                current_auth_stage_ = AuthStage::None;
                                last_auth_stage = AuthStage::None;
                                std::cout << "\nConfirm this login link on another device: " << state.link_ << std::endl;
                              },
                              [this](td_api::authorizationStateWaitTdlibParameters &) {
                                current_auth_stage_ = AuthStage::None;
                                last_auth_stage = AuthStage::None;
                                auto request = td_api::make_object<td_api::setTdlibParameters>();
                                request->database_directory_ = "tdlib";
                                request->use_message_database_ = true;
                                request->use_secret_chats_ = true;
                                request->api_id_ = 94575;
                                request->api_hash_ = "a3406de8d171bb422bb6ddf3bbd800e2";
                                request->system_language_code_ = "en";
                                request->device_model_ = "Desktop";
                                request->application_version_ = "1.0";
                                request->system_version_ = "Unknown";
                                send_query(std::move(request),
                                          [this](Object object) { check_authentication_error(std::move(object)); });
                              }));
  }

  void print_user_help() const {
    std::cout << "\nAvailable commands:\n"
              << "  q - quit\n"
              << "  u - check for updates\n"
              << "  c - show chats\n"
              << "  m <chat_id> <text> - send message\n"
              << "  me - show self\n"
              << "  l - logout\n"
              << "  close - close connection\n"
              << "Enter command: " << std::flush;
  }

  void check_authentication_error(Object object) {
    if (object->get_id() == td_api::error::ID) {
      auto error = td::move_tl_object_as<td_api::error>(object);
      std::cout << "Error: " << to_string(error) << std::endl;
      current_auth_stage_ = AuthStage::None;
      on_authorization_state_update();
    }
  }

  std::uint64_t next_query_id() {
    return ++current_query_id_;
  }
};

int main() {
  TdExample example;
  example.loop();
  return 0;
}
