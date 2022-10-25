//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/ForumTopicInfo.h"
#include "td/telegram/td_api.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"
#include "td/utils/Promise.h"

namespace td {

class Td;

class ForumTopicManager final : public Actor {
 public:
  ForumTopicManager(Td *td, ActorShared<> parent);
  ForumTopicManager(const ForumTopicManager &) = delete;
  ForumTopicManager &operator=(const ForumTopicManager &) = delete;
  ForumTopicManager(ForumTopicManager &&) = delete;
  ForumTopicManager &operator=(ForumTopicManager &&) = delete;
  ~ForumTopicManager() final;

  void create_forum_topic(DialogId dialog_id, string &&title, td_api::object_ptr<td_api::forumTopicIcon> &&icon,
                          Promise<td_api::object_ptr<td_api::forumTopicInfo>> &&promise);

  void on_forum_topic_created(ForumTopicInfo &&forum_topic_info,
                              Promise<td_api::object_ptr<td_api::forumTopicInfo>> &&promise);

 private:
  static constexpr size_t MAX_FORUM_TOPIC_TITLE_LENGTH = 128;  // server side limit for forum topic title

  void tear_down() final;

  Status is_forum(DialogId dialog_id);

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
