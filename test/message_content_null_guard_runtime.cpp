// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/MessageContent.h"
#include "td/telegram/ToDoCompletion.h"
#include "td/telegram/ToDoList.h"

#include "td/utils/tests.h"

namespace td {

class MessageToDoList final : public MessageContent {
 public:
  ToDoList list;
  vector<ToDoCompletion> completions;

  MessageToDoList() = default;
  MessageToDoList(ToDoList &&list, vector<ToDoCompletion> &&completions)
      : list(std::move(list)), completions(std::move(completions)) {
  }

  MessageContentType get_type() const final {
    return MessageContentType::ToDoList;
  }
};

}  // namespace td

TEST(MessageContentNullGuardRuntime, PollHelpersFailClosedOnNullContent) {
  ASSERT_FALSE(td::get_message_content_poll_can_view_stats(nullptr, nullptr));
  ASSERT_FALSE(td::get_message_content_poll_has_option(nullptr, nullptr, td::string("opt")));
  ASSERT_FALSE(td::get_message_content_to_do_list_has_item(nullptr, 1));
}

TEST(MessageContentNullGuardRuntime, PollHelpersFailClosedOnWrongContentType) {
  auto text = td::create_text_message_content("hello", td::vector<td::MessageEntity>{}, td::WebPageId(), false, false,
                                              false, td::string());

  ASSERT_TRUE(text != nullptr);
  ASSERT_EQ(td::PollId(), td::get_message_content_poll_id(text.get()));
  ASSERT_FALSE(td::message_content_poll_has_media(text.get(), nullptr));
  ASSERT_FALSE(td::get_message_content_poll_is_anonymous(nullptr, text.get()));
  ASSERT_TRUE(td::get_message_content_poll_is_closed(nullptr, text.get()));
  ASSERT_FALSE(td::get_message_content_poll_can_add_option(nullptr, text.get()));
  ASSERT_FALSE(td::get_message_content_poll_can_view_stats(nullptr, text.get()));
  ASSERT_FALSE(td::get_message_content_poll_has_unread_votes(nullptr, text.get()));
  ASSERT_FALSE(td::get_message_content_poll_has_option(nullptr, text.get(), td::string("opt")));
  ASSERT_FALSE(td::get_message_content_to_do_list_has_item(text.get(), 1));

  td::remove_message_content_poll_has_unread_votes(nullptr, text.get());
}

TEST(MessageContentNullGuardRuntime, ToDoListHelperRecognizesExistingTaskIdentifiers) {
  td::vector<td::telegram_api::object_ptr<td::telegram_api::todoItem>> items;
  items.push_back(td::telegram_api::make_object<td::telegram_api::todoItem>(
      11, td::telegram_api::make_object<td::telegram_api::textWithEntities>(
              "task one", td::vector<td::telegram_api::object_ptr<td::telegram_api::MessageEntity>>{})));
  items.push_back(td::telegram_api::make_object<td::telegram_api::todoItem>(
      22, td::telegram_api::make_object<td::telegram_api::textWithEntities>(
              "task two", td::vector<td::telegram_api::object_ptr<td::telegram_api::MessageEntity>>{})));

  td::ToDoList list(nullptr,
                    td::telegram_api::make_object<td::telegram_api::todoList>(
                        0, false, false,
                        td::telegram_api::make_object<td::telegram_api::textWithEntities>(
                            "checklist", td::vector<td::telegram_api::object_ptr<td::telegram_api::MessageEntity>>{}),
                        std::move(items)));

  td::MessageToDoList content(std::move(list), td::vector<td::ToDoCompletion>{});

  ASSERT_TRUE(td::get_message_content_to_do_list_has_item(&content, 11));
  ASSERT_TRUE(td::get_message_content_to_do_list_has_item(&content, 22));
  ASSERT_FALSE(td::get_message_content_to_do_list_has_item(&content, 33));
  ASSERT_FALSE(td::get_message_content_to_do_list_has_item(&content, 0));
  ASSERT_FALSE(td::get_message_content_to_do_list_has_item(&content, -7));
}
