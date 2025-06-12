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

namespace detail {
template <class... Fs>
struct overload;

template <class F>
struct overload<F> : public F {
  explicit overload(F f) : F(f) {
  }
};
template <class F, class... Fs>
struct overload<F, Fs...>
    : public overload<F>
    , public overload<Fs...> {
  overload(F f, Fs... fs) : overload<F>(f), overload<Fs...>(fs...) {
  }
  using overload<F>::operator();
  using overload<Fs...>::operator();
};
}

template <class... F>
auto overloaded(F... f) {
  return detail::overload<F...>(f...);
}

namespace td_api = td::td_api;

//создание объекта для взаимодействия с клиентом
class TdBasic {
public:
    TdBasic(){
        td::ClientManager::execute(td_api::make_object<td_api::setLogVerbosityLevel>(1));
        client_manager = std::make_unique<td::ClientManager>();
        client_id = client_manager_->create_client_id();
        send_query(td_api::make_object<td_api::getOption>("version"), {});
    }

  //взаимодейтсвие с интерфейсом
  void loop(){
    while(true){
        if(need_restart_) {
            restart();
        }else if(!are_authorized_) {
            std::cout << "Enter action [q] quit [u] check for updates and request results [c] show chats [m <chat_id> "
                     "<text>] send message [me] show self [l] logout: "
                     <<std::endl;
            std::string line;
            std::getline(std::cin, line);
            std::isstringstream ss(line);
            std::string action;
            if(!(ss >> action)) {
                continue;
            }
            if (action == "q"){
                return;
            }
            if (action == "u"){
                std::cout <<"Checking for updates..." << std::endl;
                while(true){
                    auto response = client_manager_->receive(0);
                    if(response.object){
                        process_response(std::move(response))
                    }else{
                        break;
                    }
                }
            } else if (action == "close") {
                std::cout << "Closing..." << std::endl;
                send_query(td::td_api::make_object<td_api::close>(), {}); 
            } else if (action == "me") {
                send_query(td::td_api::make_object<td_api::getMe>(),
                            [this](Object object) {std::cout << to_string(object) << std::endl; });
            } else if (action == "l") {
                std::cout << "Logging out..." << std::endl;
                send_query(td_api::make_object<td_api::logOut>(), {});
            } else if (action == "m") {
                std::int64_t chat_id;
                ss >> chat_id;
                ss.get();
                std::string text;
                std::getline(ss, text);
                std::cout << "Sending message to chat " << chat_id << "...." <<std::endl;
                auto send_message = td_api::make_object<td_api::sendMessage>();
                send_message->chat_id_ = chat_id;
                auto message_content = td_api::make_object<td_api::inputMessageText>();
                message_content->text_ = td_api::make_object<td::td_api::formattedText>();
                message_content->text_->text_=std::move(text);
                send_message->input_message_content_ = std::move(message_content);

                send_query(std::move(send_message),{});
            }else if (action == "c") {
                std::cout << "Loading chat list..." << std::endl;
                send_query(td_api::make_object<td_api::getChats>(nullptr, 20), [this](Object object){
                    if (object->get_id() == td_api::error::ID){
                        return;
                    }
                    auto chats = td::move_tl_object_as<td::api::chats>(object);
                    for(auto chat_id : chats->chats_ids) {
                        std::cout << "[chat_id" << chat_id << "] [title:" <<chat_title[chat_id] << "]" <<std::endl;
                    }
                });
            }
         }
    }
  }

  private:
    using Object = td_api::Object_ptr<td_api::Object>;
    std::unique_ptr<td::ClientManager> client_manager_;
    std::int32_t client_id_{0};

    td_api::object_ptr<td::api::AuthorizationState> autorization_state_;
    bool are_authorized_{false};
    bool need_restart_{false};
    std::uint64_t current_query_id_{0};
    std::uint64_t 
}