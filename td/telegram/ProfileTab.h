//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/ChannelType.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/Status.h"
#include "td/utils/StringBuilder.h"

namespace td {

enum class ProfileTab : int32 { Default, Posts, Gifts, Media, Files, Music, Voice, Links, Gifs };

ProfileTab get_profile_tab(telegram_api::object_ptr<telegram_api::ProfileTab> &&profile_tab, ChannelType channel_type);

Result<ProfileTab> get_profile_tab(const td_api::object_ptr<td_api::ProfileTab> &profile_tab, ChannelType channel_type);

telegram_api::object_ptr<telegram_api::ProfileTab> get_input_profile_tab(ProfileTab profile_tab);

td_api::object_ptr<td_api::ProfileTab> get_profile_tab_object(ProfileTab profile_tab);

StringBuilder &operator<<(StringBuilder &string_builder, ProfileTab profile_tab);

}  // namespace td
