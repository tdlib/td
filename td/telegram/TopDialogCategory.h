//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"

#include "td/utils/common.h"

namespace td {

enum class TopDialogCategory : int32 {
  Correspondent,
  BotPM,
  BotInline,
  Group,
  Channel,
  Call,
  ForwardUsers,
  ForwardChats,
  Size
};

inline TopDialogCategory get_top_dialog_category(const td_api::TopChatCategory &category) {
  switch (category.get_id()) {
    case td_api::topChatCategoryUsers::ID:
      return TopDialogCategory::Correspondent;
    case td_api::topChatCategoryBots::ID:
      return TopDialogCategory::BotPM;
    case td_api::topChatCategoryInlineBots::ID:
      return TopDialogCategory::BotInline;
    case td_api::topChatCategoryGroups::ID:
      return TopDialogCategory::Group;
    case td_api::topChatCategoryChannels::ID:
      return TopDialogCategory::Channel;
    case td_api::topChatCategoryCalls::ID:
      return TopDialogCategory::Call;
    case td_api::topChatCategoryForwardChats::ID:
      return TopDialogCategory::ForwardUsers;
    default:
      UNREACHABLE();
  }
}

}  // namespace td
