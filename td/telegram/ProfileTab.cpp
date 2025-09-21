//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ProfileTab.h"

#include "td/utils/logging.h"

namespace td {

static bool is_allowed_profile_tab(ProfileTab profile_tab, ChannelType channel_type) {
  switch (channel_type) {
    case ChannelType::Broadcast:
      return true;
    case ChannelType::Megagroup:
      return profile_tab != ProfileTab::Gifts;
    case ChannelType::Unknown:
      return profile_tab == ProfileTab::Posts || profile_tab == ProfileTab::Gifts;
    default:
      UNREACHABLE();
      return false;
  }
}

ProfileTab get_profile_tab(telegram_api::object_ptr<telegram_api::ProfileTab> &&profile_tab, ChannelType channel_type) {
  if (profile_tab == nullptr) {
    return ProfileTab::Default;
  }
  ProfileTab result = [&] {
    switch (profile_tab->get_id()) {
      case telegram_api::profileTabPosts::ID:
        return ProfileTab::Posts;
      case telegram_api::profileTabGifts::ID:
        return ProfileTab::Gifts;
      case telegram_api::profileTabMedia::ID:
        return ProfileTab::Media;
      case telegram_api::profileTabFiles::ID:
        return ProfileTab::Files;
      case telegram_api::profileTabMusic::ID:
        return ProfileTab::Music;
      case telegram_api::profileTabVoice::ID:
        return ProfileTab::Voice;
      case telegram_api::profileTabLinks::ID:
        return ProfileTab::Links;
      case telegram_api::profileTabGifs::ID:
        return ProfileTab::Gifs;
      default:
        UNREACHABLE();
        return ProfileTab::Default;
    }
  }();
  if (!is_allowed_profile_tab(result, channel_type)) {
    LOG(ERROR) << "Receive " << result << " for " << channel_type;
    result = ProfileTab::Default;
  }
  return result;
}

Result<ProfileTab> get_profile_tab(const td_api::object_ptr<td_api::ProfileTab> &profile_tab,
                                   ChannelType channel_type) {
  if (profile_tab == nullptr) {
    return Status::Error(400, "Profile tab must be non-empty");
  }
  ProfileTab result = [&] {
    switch (profile_tab->get_id()) {
      case td_api::profileTabPosts::ID:
        return ProfileTab::Posts;
      case td_api::profileTabGifts::ID:
        return ProfileTab::Gifts;
      case td_api::profileTabMedia::ID:
        return ProfileTab::Media;
      case td_api::profileTabFiles::ID:
        return ProfileTab::Files;
      case td_api::profileTabMusic::ID:
        return ProfileTab::Music;
      case td_api::profileTabVoice::ID:
        return ProfileTab::Voice;
      case td_api::profileTabLinks::ID:
        return ProfileTab::Links;
      case td_api::profileTabGifs::ID:
        return ProfileTab::Gifs;
      default:
        UNREACHABLE();
        return ProfileTab::Default;
    }
  }();
  if (!is_allowed_profile_tab(result, channel_type)) {
    return Status::Error(400, "Invalid profile tab specified for the chat");
  }
  return result;
}

telegram_api::object_ptr<telegram_api::ProfileTab> get_input_profile_tab(ProfileTab profile_tab) {
  switch (profile_tab) {
    case ProfileTab::Default:
      UNREACHABLE();
      return nullptr;
    case ProfileTab::Posts:
      return telegram_api::make_object<telegram_api::profileTabPosts>();
    case ProfileTab::Gifts:
      return telegram_api::make_object<telegram_api::profileTabGifts>();
    case ProfileTab::Media:
      return telegram_api::make_object<telegram_api::profileTabMedia>();
    case ProfileTab::Files:
      return telegram_api::make_object<telegram_api::profileTabFiles>();
    case ProfileTab::Music:
      return telegram_api::make_object<telegram_api::profileTabMusic>();
    case ProfileTab::Voice:
      return telegram_api::make_object<telegram_api::profileTabVoice>();
    case ProfileTab::Links:
      return telegram_api::make_object<telegram_api::profileTabLinks>();
    case ProfileTab::Gifs:
      return telegram_api::make_object<telegram_api::profileTabGifs>();
    default:
      UNREACHABLE();
      return nullptr;
  }
}

td_api::object_ptr<td_api::ProfileTab> get_profile_tab_object(ProfileTab profile_tab) {
  switch (profile_tab) {
    case ProfileTab::Default:
      return nullptr;
    case ProfileTab::Posts:
      return td_api::make_object<td_api::profileTabPosts>();
    case ProfileTab::Gifts:
      return td_api::make_object<td_api::profileTabGifts>();
    case ProfileTab::Media:
      return td_api::make_object<td_api::profileTabMedia>();
    case ProfileTab::Files:
      return td_api::make_object<td_api::profileTabFiles>();
    case ProfileTab::Music:
      return td_api::make_object<td_api::profileTabMusic>();
    case ProfileTab::Voice:
      return td_api::make_object<td_api::profileTabVoice>();
    case ProfileTab::Links:
      return td_api::make_object<td_api::profileTabLinks>();
    case ProfileTab::Gifs:
      return td_api::make_object<td_api::profileTabGifs>();
    default:
      UNREACHABLE();
      return nullptr;
  }
}

StringBuilder &operator<<(StringBuilder &string_builder, ProfileTab profile_tab) {
  string_builder << "profile tab ";
  switch (profile_tab) {
    case ProfileTab::Default:
      return string_builder << "Default";
    case ProfileTab::Posts:
      return string_builder << "Posts";
    case ProfileTab::Gifts:
      return string_builder << "Gifts";
    case ProfileTab::Media:
      return string_builder << "Media";
    case ProfileTab::Files:
      return string_builder << "Files";
    case ProfileTab::Music:
      return string_builder << "Music";
    case ProfileTab::Voice:
      return string_builder << "Voice";
    case ProfileTab::Links:
      return string_builder << "Links";
    case ProfileTab::Gifs:
      return string_builder << "Gifs";
    default:
      UNREACHABLE();
      return string_builder;
  }
}

}  // namespace td
