//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ReplyMarkup.h"

#include "td/telegram/Dependencies.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/algorithm.h"
#include "td/utils/logging.h"

namespace td {

bool operator==(const ReplyMarkup &lhs, const ReplyMarkup &rhs) {
  if (lhs.type != rhs.type) {
    return false;
  }
  if (lhs.force_reply != rhs.force_reply) {
    return false;
  }
  if (lhs.type == ReplyMarkup::Type::InlineKeyboard) {
    return lhs.inline_keyboard == rhs.inline_keyboard;
  }

  if (lhs.is_personal != rhs.is_personal) {
    return false;
  }
  if (lhs.placeholder != rhs.placeholder) {
    return false;
  }
  if (lhs.type != ReplyMarkup::Type::ShowKeyboard) {
    return true;
  }
  return lhs.is_persistent == rhs.is_persistent && lhs.need_resize_keyboard == rhs.need_resize_keyboard &&
         lhs.is_one_time_keyboard == rhs.is_one_time_keyboard && lhs.keyboard == rhs.keyboard;
}

bool operator!=(const ReplyMarkup &lhs, const ReplyMarkup &rhs) {
  return !(lhs == rhs);
}

StringBuilder &ReplyMarkup::print(StringBuilder &string_builder) const {
  string_builder << "ReplyMarkup[";
  switch (type) {
    case ReplyMarkup::Type::InlineKeyboard:
      string_builder << "InlineKeyboard";
      break;
    case ReplyMarkup::Type::ShowKeyboard:
      string_builder << "ShowKeyboard";
      break;
    case ReplyMarkup::Type::RemoveKeyboard:
      string_builder << "RemoveKeyboard";
      break;
    case ReplyMarkup::Type::ForceReply:
      string_builder << "ForceReply";
      break;
    default:
      UNREACHABLE();
  }
  if (force_reply) {
    string_builder << ", force reply";
  }
  if (is_personal) {
    string_builder << ", personal";
  }
  if (!placeholder.empty()) {
    string_builder << ", placeholder \"" << placeholder << '"';
  }

  if (type == ReplyMarkup::Type::ShowKeyboard) {
    if (is_persistent) {
      string_builder << ", persistent";
    }
    if (need_resize_keyboard) {
      string_builder << ", need resize";
    }
    if (is_one_time_keyboard) {
      string_builder << ", one time";
    }
  }
  if (type == ReplyMarkup::Type::InlineKeyboard) {
    for (auto &row : inline_keyboard) {
      string_builder << ", " << row;
    }
  }
  if (type == ReplyMarkup::Type::ShowKeyboard) {
    for (auto &row : keyboard) {
      string_builder << ", " << row;
    }
  }

  string_builder << "]";
  return string_builder;
}

StringBuilder &operator<<(StringBuilder &string_builder, const ReplyMarkup &reply_markup) {
  return reply_markup.print(string_builder);
}

unique_ptr<ReplyMarkup> get_reply_markup(telegram_api::object_ptr<telegram_api::ReplyMarkup> &&reply_markup_ptr,
                                         bool is_bot, bool only_inline_keyboard, bool message_contains_mention) {
  if (reply_markup_ptr == nullptr) {
    return nullptr;
  }

  auto reply_markup = make_unique<ReplyMarkup>();
  auto constructor_id = reply_markup_ptr->get_id();
  if (only_inline_keyboard && constructor_id != telegram_api::replyInlineMarkup::ID) {
    LOG(ERROR) << "Inline keyboard expected";
    return nullptr;
  }
  switch (constructor_id) {
    case telegram_api::replyInlineMarkup::ID: {
      auto inline_markup = telegram_api::move_object_as<telegram_api::replyInlineMarkup>(reply_markup_ptr);
      reply_markup->type = ReplyMarkup::Type::InlineKeyboard;
      reply_markup->inline_keyboard.reserve(inline_markup->rows_.size());
      for (auto &row : inline_markup->rows_) {
        vector<InlineKeyboardButton> buttons;
        buttons.reserve(row->buttons_.size());
        for (auto &button : row->buttons_) {
          buttons.push_back(get_inline_keyboard_button(std::move(button)));
          if (buttons.back().text.empty() && !buttons.back().style.get_icon_custom_emoji_id().is_valid()) {
            buttons.pop_back();
          }
        }
        if (!buttons.empty()) {
          reply_markup->inline_keyboard.push_back(std::move(buttons));
        }
      }
      if (reply_markup->inline_keyboard.empty()) {
        return nullptr;
      }
      reply_markup->force_reply = inline_markup->force_reply_;
      break;
    }
    case telegram_api::replyKeyboardMarkup::ID: {
      auto keyboard_markup = telegram_api::move_object_as<telegram_api::replyKeyboardMarkup>(reply_markup_ptr);
      reply_markup->type = ReplyMarkup::Type::ShowKeyboard;
      reply_markup->is_persistent = keyboard_markup->persistent_;
      reply_markup->need_resize_keyboard = keyboard_markup->resize_;
      reply_markup->is_one_time_keyboard = keyboard_markup->single_use_;
      reply_markup->is_personal = keyboard_markup->selective_;
      reply_markup->force_reply = keyboard_markup->force_reply_;
      reply_markup->placeholder = std::move(keyboard_markup->placeholder_);
      reply_markup->keyboard.reserve(keyboard_markup->rows_.size());
      for (auto &row : keyboard_markup->rows_) {
        vector<KeyboardButton> buttons;
        buttons.reserve(row->buttons_.size());
        for (auto &button : row->buttons_) {
          buttons.emplace_back(std::move(button));
          if (buttons.back().is_empty()) {
            buttons.pop_back();
          }
        }
        if (!buttons.empty()) {
          reply_markup->keyboard.push_back(std::move(buttons));
        }
      }
      if (reply_markup->keyboard.empty()) {
        return nullptr;
      }
      break;
    }
    case telegram_api::replyKeyboardHide::ID: {
      auto hide_keyboard_markup = telegram_api::move_object_as<telegram_api::replyKeyboardHide>(reply_markup_ptr);
      reply_markup->type = ReplyMarkup::Type::RemoveKeyboard;
      reply_markup->is_personal = hide_keyboard_markup->selective_;
      break;
    }
    case telegram_api::replyKeyboardForceReply::ID: {
      auto force_reply_markup = telegram_api::move_object_as<telegram_api::replyKeyboardForceReply>(reply_markup_ptr);
      reply_markup->type = ReplyMarkup::Type::ForceReply;
      reply_markup->is_personal = force_reply_markup->selective_;
      reply_markup->placeholder = std::move(force_reply_markup->placeholder_);
      break;
    }
    default:
      UNREACHABLE();
      return nullptr;
  }

  if (!is_bot && reply_markup->type != ReplyMarkup::Type::InlineKeyboard) {
    // incoming keyboard
    if (reply_markup->is_personal) {
      if (!message_contains_mention) {
        reply_markup->is_personal = false;
        reply_markup->force_reply = false;
      }
    } else {
      reply_markup->is_personal = true;
    }
  }

  return reply_markup;
}

static Result<unique_ptr<ReplyMarkup>> get_reply_markup(td_api::object_ptr<td_api::ReplyMarkup> &&reply_markup_ptr,
                                                        bool is_bot, bool only_inline_keyboard,
                                                        bool request_buttons_allowed,
                                                        bool switch_inline_buttons_allowed, bool allow_personal) {
  if (only_inline_keyboard) {
    CHECK(!request_buttons_allowed);
  }
  if (reply_markup_ptr == nullptr || !is_bot) {
    return nullptr;
  }

  auto reply_markup = make_unique<ReplyMarkup>();
  auto constructor_id = reply_markup_ptr->get_id();
  if (only_inline_keyboard && constructor_id != td_api::replyMarkupInlineKeyboard::ID) {
    return Status::Error(400, "Inline keyboard expected");
  }

  switch (constructor_id) {
    case td_api::replyMarkupShowKeyboard::ID: {
      auto show_keyboard_markup = td_api::move_object_as<td_api::replyMarkupShowKeyboard>(reply_markup_ptr);
      reply_markup->type = ReplyMarkup::Type::ShowKeyboard;
      reply_markup->is_persistent = show_keyboard_markup->is_persistent_;
      reply_markup->need_resize_keyboard = show_keyboard_markup->resize_keyboard_;
      reply_markup->is_one_time_keyboard = show_keyboard_markup->one_time_;
      reply_markup->is_personal = show_keyboard_markup->is_personal_ && allow_personal;
      reply_markup->force_reply = show_keyboard_markup->force_reply_;
      reply_markup->placeholder = std::move(show_keyboard_markup->input_field_placeholder_);

      reply_markup->keyboard.reserve(show_keyboard_markup->rows_.size());
      int32 total_button_count = 0;
      for (auto &row : show_keyboard_markup->rows_) {
        vector<KeyboardButton> row_buttons;
        row_buttons.reserve(row.size());

        int32 row_button_count = 0;
        for (auto &button : row) {
          if (button->text_.empty() && button->icon_custom_emoji_id_ == 0) {
            continue;
          }

          TRY_RESULT(current_button, KeyboardButton::get_keyboard_button(std::move(button), request_buttons_allowed));

          row_buttons.push_back(std::move(current_button));
          row_button_count++;
          total_button_count++;
          if (row_button_count >= 12 || total_button_count >= 300) {
            break;
          }
        }
        if (!row_buttons.empty()) {
          reply_markup->keyboard.push_back(std::move(row_buttons));
        }
        if (total_button_count >= 300) {
          break;
        }
      }
      if (reply_markup->keyboard.empty()) {
        return nullptr;
      }
      break;
    }
    case td_api::replyMarkupInlineKeyboard::ID: {
      auto inline_keyboard_markup = td_api::move_object_as<td_api::replyMarkupInlineKeyboard>(reply_markup_ptr);
      reply_markup->type = ReplyMarkup::Type::InlineKeyboard;

      reply_markup->inline_keyboard.reserve(inline_keyboard_markup->rows_.size());
      int32 total_button_count = 0;
      for (auto &row : inline_keyboard_markup->rows_) {
        vector<InlineKeyboardButton> row_buttons;
        row_buttons.reserve(row.size());

        int32 row_button_count = 0;
        for (auto &button : row) {
          if (button->text_.empty() && button->icon_custom_emoji_id_ == 0) {
            continue;
          }

          TRY_RESULT(current_button, get_inline_keyboard_button(std::move(button), switch_inline_buttons_allowed));

          row_buttons.push_back(std::move(current_button));
          row_button_count++;
          total_button_count++;
          if (row_button_count >= 12 || total_button_count >= 300) {
            break;
          }
        }
        if (!row_buttons.empty()) {
          reply_markup->inline_keyboard.push_back(std::move(row_buttons));
        }
        if (total_button_count >= 300) {
          break;
        }
      }
      if (reply_markup->inline_keyboard.empty()) {
        return nullptr;
      }
      reply_markup->force_reply = inline_keyboard_markup->force_reply_;
      break;
    }
    case td_api::replyMarkupRemoveKeyboard::ID: {
      auto remove_keyboard_markup = td_api::move_object_as<td_api::replyMarkupRemoveKeyboard>(reply_markup_ptr);
      reply_markup->type = ReplyMarkup::Type::RemoveKeyboard;
      reply_markup->is_personal = remove_keyboard_markup->is_personal_ && allow_personal;
      break;
    }
    case td_api::replyMarkupForceReply::ID: {
      auto force_reply_markup = td_api::move_object_as<td_api::replyMarkupForceReply>(reply_markup_ptr);
      reply_markup->type = ReplyMarkup::Type::ForceReply;
      reply_markup->is_personal = force_reply_markup->is_personal_ && allow_personal;
      reply_markup->placeholder = std::move(force_reply_markup->input_field_placeholder_);
      break;
    }
    default:
      UNREACHABLE();
  }
  return std::move(reply_markup);
}

Result<unique_ptr<ReplyMarkup>> get_inline_reply_markup(td_api::object_ptr<td_api::ReplyMarkup> &&reply_markup_ptr,
                                                        bool is_bot, bool switch_inline_buttons_allowed) {
  return get_reply_markup(std::move(reply_markup_ptr), is_bot, true, false, switch_inline_buttons_allowed, false);
}

Result<unique_ptr<ReplyMarkup>> get_reply_markup(td_api::object_ptr<td_api::ReplyMarkup> &&reply_markup_ptr,
                                                 DialogType dialog_type, bool is_admined_monoforum, bool is_bot,
                                                 bool is_anonymous) {
  bool only_inline_keyboard = is_anonymous && !is_admined_monoforum;
  bool request_buttons_allowed = dialog_type == DialogType::User;
  bool switch_inline_buttons_allowed = !is_anonymous;
  bool allow_personal = dialog_type != DialogType::User;
  return get_reply_markup(std::move(reply_markup_ptr), is_bot, only_inline_keyboard, request_buttons_allowed,
                          switch_inline_buttons_allowed, allow_personal);
}

unique_ptr<ReplyMarkup> dup_reply_markup(const unique_ptr<ReplyMarkup> &reply_markup, DialogId dialog_id,
                                         const MessageContentDupType &dup_type, bool is_via_bot) {
  if (reply_markup == nullptr || dialog_id.get_type() == DialogType::SecretChat ||
      dup_type == MessageContentDupType::Copy || dup_type == MessageContentDupType::ServerCopy) {
    return nullptr;
  }
  bool is_send = dup_type == MessageContentDupType::Send || dup_type == MessageContentDupType::SendViaBot ||
                 dup_type == MessageContentDupType::SendQuickReply;
  if (!is_send && reply_markup->type != ReplyMarkup::Type::InlineKeyboard) {
    return nullptr;
  }
  auto result = make_unique<ReplyMarkup>();
  result->type = reply_markup->type;
  result->is_personal = reply_markup->is_personal;
  if (is_send) {
    result->force_reply = reply_markup->force_reply;
  }
  result->is_persistent = reply_markup->is_persistent;
  result->need_resize_keyboard = reply_markup->need_resize_keyboard;
  result->keyboard = transform(reply_markup->keyboard, [](const vector<KeyboardButton> &row) {
    return transform(row, [](const KeyboardButton &button) { return button.clone(); });
  });
  result->placeholder = reply_markup->placeholder;
  bool need_drop = false;
  result->inline_keyboard = transform(reply_markup->inline_keyboard, [&](const vector<InlineKeyboardButton> &row) {
    return transform(row, [&](const InlineKeyboardButton &button) {
      auto new_button = button.clone(dialog_id, dup_type, is_via_bot, false);
      if (new_button.is_disabled() && !button.is_disabled()) {
        need_drop = true;
      }
      return new_button;
    });
  });
  if (need_drop) {
    return nullptr;
  }
  return result;
}

telegram_api::object_ptr<telegram_api::ReplyMarkup> ReplyMarkup::get_input_reply_markup(
    UserManager *user_manager) const {
  switch (type) {
    case ReplyMarkup::Type::InlineKeyboard: {
      vector<telegram_api::object_ptr<telegram_api::keyboardInlineButtonRow>> rows;
      rows.reserve(inline_keyboard.size());
      for (auto &row : inline_keyboard) {
        vector<telegram_api::object_ptr<telegram_api::keyboardInlineButton>> buttons;
        buttons.reserve(row.size());
        for (auto &button : row) {
          buttons.push_back(get_input_keyboard_inline_button(user_manager, button));
        }
        rows.push_back(telegram_api::make_object<telegram_api::keyboardInlineButtonRow>(std::move(buttons)));
      }
      return telegram_api::make_object<telegram_api::replyInlineMarkup>(0, force_reply, std::move(rows));
    }
    case ReplyMarkup::Type::ShowKeyboard: {
      vector<telegram_api::object_ptr<telegram_api::keyboardButtonRow>> rows;
      rows.reserve(keyboard.size());
      for (auto &row : keyboard) {
        vector<telegram_api::object_ptr<telegram_api::keyboardButton>> buttons;
        buttons.reserve(row.size());
        for (auto &button : row) {
          buttons.push_back(button.get_input_keyboard_button());
        }
        rows.push_back(telegram_api::make_object<telegram_api::keyboardButtonRow>(std::move(buttons)));
      }
      int32 flags = 0;
      if (!placeholder.empty()) {
        flags |= telegram_api::replyKeyboardMarkup::PLACEHOLDER_MASK;
      }
      return telegram_api::make_object<telegram_api::replyKeyboardMarkup>(
          flags, need_resize_keyboard, is_one_time_keyboard, is_personal, is_persistent, force_reply, std::move(rows),
          placeholder);
    }
    case ReplyMarkup::Type::ForceReply: {
      int32 flags = 0;
      if (!placeholder.empty()) {
        flags |= telegram_api::replyKeyboardForceReply::PLACEHOLDER_MASK;
      }
      return telegram_api::make_object<telegram_api::replyKeyboardForceReply>(flags, is_one_time_keyboard, is_personal,
                                                                              placeholder);
    }
    case ReplyMarkup::Type::RemoveKeyboard:
      return telegram_api::make_object<telegram_api::replyKeyboardHide>(0, is_personal);
    default:
      UNREACHABLE();
      return nullptr;
  }
}

td_api::object_ptr<td_api::ReplyMarkup> ReplyMarkup::get_reply_markup_object(UserManager *user_manager) const {
  switch (type) {
    case ReplyMarkup::Type::InlineKeyboard: {
      vector<vector<td_api::object_ptr<td_api::inlineKeyboardButton>>> rows;
      rows.reserve(inline_keyboard.size());
      for (auto &row : inline_keyboard) {
        vector<td_api::object_ptr<td_api::inlineKeyboardButton>> buttons;
        buttons.reserve(row.size());
        for (auto &button : row) {
          buttons.push_back(get_inline_keyboard_button_object(user_manager, button));
        }
        rows.push_back(std::move(buttons));
      }

      return td_api::make_object<td_api::replyMarkupInlineKeyboard>(std::move(rows), force_reply);
    }
    case ReplyMarkup::Type::ShowKeyboard: {
      vector<vector<td_api::object_ptr<td_api::keyboardButton>>> rows;
      rows.reserve(keyboard.size());
      for (auto &row : keyboard) {
        vector<td_api::object_ptr<td_api::keyboardButton>> buttons;
        buttons.reserve(row.size());
        for (auto &button : row) {
          buttons.push_back(button.get_keyboard_button_object());
        }
        rows.push_back(std::move(buttons));
      }

      return td_api::make_object<td_api::replyMarkupShowKeyboard>(std::move(rows), is_persistent, need_resize_keyboard,
                                                                  is_one_time_keyboard, is_personal, force_reply,
                                                                  placeholder);
    }
    case ReplyMarkup::Type::RemoveKeyboard:
      return td_api::make_object<td_api::replyMarkupRemoveKeyboard>(is_personal);
    case ReplyMarkup::Type::ForceReply:
      return td_api::make_object<td_api::replyMarkupForceReply>(is_personal, placeholder);
    default:
      UNREACHABLE();
      return nullptr;
  }
}

const RequestedDialogType *ReplyMarkup::get_requested_dialog_type(int32 button_id) const {
  for (auto &row : keyboard) {
    for (auto &button : row) {
      auto requested_dialog_type = button.get_requested_dialog_type();
      if (requested_dialog_type != nullptr && requested_dialog_type->get_button_id() == button_id) {
        return requested_dialog_type;
      }
    }
  }
  return nullptr;
}

const string *ReplyMarkup::get_login_button_url(int64 button_id) const {
  for (auto &row : inline_keyboard) {
    for (auto &button : row) {
      auto login_url = button.get_login_url(button_id);
      if (login_url != nullptr) {
        return login_url;
      }
    }
  }
  return nullptr;
}

bool ReplyMarkup::has_buy_button() const {
  return !inline_keyboard.empty() && !inline_keyboard[0].empty() && inline_keyboard[0][0].is_buy();
}

telegram_api::object_ptr<telegram_api::ReplyMarkup> get_input_reply_markup(
    UserManager *user_manager, const unique_ptr<ReplyMarkup> &reply_markup) {
  if (reply_markup == nullptr) {
    return nullptr;
  }

  return reply_markup->get_input_reply_markup(user_manager);
}

td_api::object_ptr<td_api::ReplyMarkup> get_reply_markup_object(UserManager *user_manager,
                                                                const unique_ptr<ReplyMarkup> &reply_markup) {
  if (reply_markup == nullptr) {
    return nullptr;
  }

  return reply_markup->get_reply_markup_object(user_manager);
}

void add_reply_markup_dependencies(Dependencies &dependencies, const ReplyMarkup *reply_markup) {
  if (reply_markup == nullptr) {
    return;
  }
  for (auto &row : reply_markup->inline_keyboard) {
    for (auto &button : row) {
      button.add_dependencies(dependencies);
    }
  }
}

}  // namespace td
