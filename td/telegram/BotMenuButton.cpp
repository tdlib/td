//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/BotMenuButton.h"

#include "td/telegram/ContactsManager.h"
#include "td/telegram/Td.h"

#include "td/utils/buffer.h"

namespace td {

class GetBotMenuButtonQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::botMenuButton>> promise_;

 public:
  explicit GetBotMenuButtonQuery(Promise<td_api::object_ptr<td_api::botMenuButton>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(UserId user_id) {
    auto input_user = user_id.is_valid() ? td_->contacts_manager_->get_input_user(user_id).move_as_ok()
                                         : tl_object_ptr<telegram_api::inputUserEmpty>();
    send_query(G()->net_query_creator().create(telegram_api::bots_getBotMenuButton(std::move(input_user))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::bots_getBotMenuButton>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto bot_menu_button = get_bot_menu_button(result_ptr.move_as_ok());
    promise_.set_value(bot_menu_button == nullptr ? td_api::make_object<td_api::botMenuButton>()
                                                  : bot_menu_button->get_bot_menu_button_object());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

unique_ptr<BotMenuButton> get_bot_menu_button(telegram_api::object_ptr<telegram_api::BotMenuButton> &&bot_menu_button) {
  CHECK(bot_menu_button != nullptr);
  switch (bot_menu_button->get_id()) {
    case telegram_api::botMenuButtonCommands::ID:
      return nullptr;
    case telegram_api::botMenuButtonDefault::ID:
      return td::make_unique<BotMenuButton>(string(), "default");
    case telegram_api::botMenuButton::ID: {
      auto button = telegram_api::move_object_as<telegram_api::botMenuButton>(bot_menu_button);
      if (button->text_.empty()) {
        LOG(ERROR) << "Receive bot menu button with empty text: " << to_string(button);
        return nullptr;
      }
      return td::make_unique<BotMenuButton>(std::move(button->text_), std::move(button->url_));
    }
    default:
      UNREACHABLE();
      return nullptr;
  }
}

td_api::object_ptr<td_api::botMenuButton> BotMenuButton::get_bot_menu_button_object() const {
  return td_api::make_object<td_api::botMenuButton>(text_, url_);
}

bool operator==(const BotMenuButton &lhs, const BotMenuButton &rhs) {
  return lhs.text_ == rhs.text_ && lhs.url_ == rhs.url_;
}

td_api::object_ptr<td_api::botMenuButton> get_bot_menu_button_object(const BotMenuButton *bot_menu_button) {
  if (bot_menu_button == nullptr) {
    return nullptr;
  }
  return bot_menu_button->get_bot_menu_button_object();
}

void get_menu_button(Td *td, UserId user_id, Promise<td_api::object_ptr<td_api::botMenuButton>> &&promise) {
  if (!user_id.is_valid() && user_id != UserId()) {
    return promise.set_error(Status::Error(400, "User not found"));
  }

  td->create_handler<GetBotMenuButtonQuery>(std::move(promise))->send(user_id);
}

}  // namespace td
