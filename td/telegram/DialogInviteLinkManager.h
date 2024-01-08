//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/AccentColorId.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/DialogInviteLink.h"
#include "td/telegram/Photo.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UserId.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/Promise.h"

namespace td {

class Td;

class DialogInviteLinkManager final : public Actor {
 public:
  DialogInviteLinkManager(Td *td, ActorShared<> parent);
  DialogInviteLinkManager(const DialogInviteLinkManager &) = delete;
  DialogInviteLinkManager &operator=(const DialogInviteLinkManager &) = delete;
  DialogInviteLinkManager(DialogInviteLinkManager &&) = delete;
  DialogInviteLinkManager &operator=(DialogInviteLinkManager &&) = delete;
  ~DialogInviteLinkManager() final;

  void check_dialog_invite_link(const string &invite_link, bool force, Promise<Unit> &&promise);

  void import_dialog_invite_link(const string &invite_link, Promise<DialogId> &&promise);

  void on_get_dialog_invite_link_info(const string &invite_link,
                                      telegram_api::object_ptr<telegram_api::ChatInvite> &&chat_invite_ptr,
                                      Promise<Unit> &&promise);

  void invalidate_invite_link_info(const string &invite_link);

  td_api::object_ptr<td_api::chatInviteLinkInfo> get_chat_invite_link_info_object(const string &invite_link);

 private:
  void tear_down() final;

  struct InviteLinkInfo {
    // known dialog
    DialogId dialog_id;

    // unknown dialog
    string title;
    Photo photo;
    AccentColorId accent_color_id;
    int32 participant_count = 0;
    vector<UserId> participant_user_ids;
    string description;
    bool creates_join_request = false;
    bool is_chat = false;
    bool is_channel = false;
    bool is_public = false;
    bool is_megagroup = false;
    bool is_verified = false;
    bool is_scam = false;
    bool is_fake = false;
  };
  FlatHashMap<string, unique_ptr<InviteLinkInfo>> invite_link_infos_;

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
