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
#include "td/actor/MultiTimeout.h"

#include "td/utils/common.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/FlatHashSet.h"
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

  bool have_dialog_access_by_invite_link(DialogId dialog_id) const;

  void remove_dialog_access_by_invite_link(DialogId dialog_id);

 private:
  void tear_down() final;

  static void on_invite_link_info_expire_timeout_callback(void *dialog_invite_link_manager_ptr, int64 dialog_id_long);

  void on_invite_link_info_expire_timeout(DialogId dialog_id);

  void add_dialog_access_by_invite_link(DialogId dialog_id, const string &invite_link, int32 accessible_before_date);

  int32 get_dialog_accessible_by_invite_link_before_date(DialogId dialog_id) const;

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

  struct DialogAccessByInviteLink {
    FlatHashSet<string> invite_links;
    int32 accessible_before_date = 0;
  };
  FlatHashMap<DialogId, DialogAccessByInviteLink, DialogIdHash> dialog_access_by_invite_link_;

  MultiTimeout invite_link_info_expire_timeout_{"InviteLinkInfoExpireTimeout"};

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
