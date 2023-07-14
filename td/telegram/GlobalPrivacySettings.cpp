//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/GlobalPrivacySettings.h"

#include "td/telegram/Global.h"
#include "td/telegram/net/NetQueryCreator.h"
#include "td/telegram/Td.h"

#include "td/utils/buffer.h"
#include "td/utils/Status.h"

namespace td {

class GetGlobalPrivacySettingsQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::archiveChatListSettings>> promise_;

 public:
  explicit GetGlobalPrivacySettingsQuery(Promise<td_api::object_ptr<td_api::archiveChatListSettings>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create(telegram_api::account_getGlobalPrivacySettings()));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::account_getGlobalPrivacySettings>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto settings = GlobalPrivacySettings(result_ptr.move_as_ok());
    promise_.set_value(settings.get_archive_chat_list_settings_object());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

GlobalPrivacySettings::GlobalPrivacySettings(telegram_api::object_ptr<telegram_api::globalPrivacySettings> &&settings)
    : archive_and_mute_new_noncontact_peers_(settings->archive_and_mute_new_noncontact_peers_)
    , keep_archived_unmuted_(settings->keep_archived_unmuted_)
    , keep_archived_folders_(settings->keep_archived_folders_) {
}

td_api::object_ptr<td_api::archiveChatListSettings> GlobalPrivacySettings::get_archive_chat_list_settings_object()
    const {
  return td_api::make_object<td_api::archiveChatListSettings>(archive_and_mute_new_noncontact_peers_,
                                                              keep_archived_unmuted_, keep_archived_folders_);
}

void GlobalPrivacySettings::get_global_privacy_settings(
    Td *td, Promise<td_api::object_ptr<td_api::archiveChatListSettings>> &&promise) {
  td->create_handler<GetGlobalPrivacySettingsQuery>(std::move(promise))->send();
}

}  // namespace td
