//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/Promise.h"

namespace td {

class Td;

class GlobalPrivacySettings {
  bool archive_and_mute_new_noncontact_peers_ = false;
  bool keep_archived_unmuted_ = false;
  bool keep_archived_folders_ = false;

 public:
  explicit GlobalPrivacySettings(telegram_api::object_ptr<telegram_api::globalPrivacySettings> &&settings);

  explicit GlobalPrivacySettings(td_api::object_ptr<td_api::archiveChatListSettings> &&settings);

  telegram_api::object_ptr<telegram_api::globalPrivacySettings> get_input_global_privacy_settings() const;

  td_api::object_ptr<td_api::archiveChatListSettings> get_archive_chat_list_settings_object() const;

  static void get_global_privacy_settings(Td *td,
                                          Promise<td_api::object_ptr<td_api::archiveChatListSettings>> &&promise);

  static void set_global_privacy_settings(Td *td, GlobalPrivacySettings settings, Promise<Unit> &&promise);
};

}  // namespace td
