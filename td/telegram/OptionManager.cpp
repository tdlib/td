//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/OptionManager.h"

#include "td/telegram/AnimationsManager.h"
#include "td/telegram/AuthManager.h"
#include "td/telegram/ConfigManager.h"
#include "td/telegram/ConfigShared.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/LanguagePackManager.h"
#include "td/telegram/net/MtprotoHeader.h"
#include "td/telegram/net/NetQueryDispatcher.h"
#include "td/telegram/NotificationManager.h"
#include "td/telegram/StickersManager.h"
#include "td/telegram/StorageManager.h"
#include "td/telegram/SuggestedAction.h"
#include "td/telegram/Td.h"
#include "td/telegram/TopDialogManager.h"

#include "td/utils/misc.h"

namespace td {

OptionManager::OptionManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

void OptionManager::tear_down() {
  parent_.reset();
}

OptionManager::~OptionManager() = default;

void OptionManager::clear_options() {
  for (auto &option : G()->shared_config().get_options()) {
    if (!is_internal_option(option.first)) {
      send_closure(
          G()->td(), &Td::send_update,
          td_api::make_object<td_api::updateOption>(option.first, td_api::make_object<td_api::optionValueEmpty>()));
    }
  }
}

bool OptionManager::is_internal_option(Slice name) {
  switch (name[0]) {
    case 'a':
      return name == "animated_emoji_zoom" || name == "animation_search_emojis" ||
             name == "animation_search_provider" || name == "auth";
    case 'b':
      return name == "base_language_pack_version";
    case 'c':
      return name == "call_ring_timeout_ms" || name == "call_receive_timeout_ms" ||
             name == "channels_read_media_period" || name == "chat_read_mark_expire_period" ||
             name == "chat_read_mark_size_threshold";
    case 'd':
      return name == "dc_txt_domain_name" || name == "dice_emojis" || name == "dice_success_values";
    case 'e':
      return name == "edit_time_limit" || name == "emoji_sounds";
    case 'i':
      return name == "ignored_restriction_reasons";
    case 'l':
      return name == "language_pack_version";
    case 'm':
      return name == "my_phone_number";
    case 'n':
      return name == "notification_cloud_delay_ms" || name == "notification_default_delay_ms";
    case 'o':
      return name == "online_update_period_ms" || name == "online_cloud_timeout_ms" || name == "otherwise_relogin_days";
    case 'r':
      return name == "revoke_pm_inbox" || name == "revoke_time_limit" || name == "revoke_pm_time_limit" ||
             name == "rating_e_decay" || name == "recent_stickers_limit";
    case 's':
      return name == "saved_animations_limit" || name == "session_count";
    case 'v':
      return name == "video_note_size_max";
    case 'w':
      return name == "webfile_dc_id";
    default:
      return false;
  }
}

void OptionManager::on_option_updated(const string &name) {
  if (G()->close_flag()) {
    return;
  }
  switch (name[0]) {
    case 'a':
      if (name == "animated_emoji_zoom") {
        // nothing to do: animated emoji zoom is updated only at launch
      }
      if (name == "animation_search_emojis") {
        td_->animations_manager_->on_update_animation_search_emojis(G()->shared_config().get_option_string(name));
      }
      if (name == "animation_search_provider") {
        td_->animations_manager_->on_update_animation_search_provider(G()->shared_config().get_option_string(name));
      }
      if (name == "auth") {
        send_closure(td_->auth_manager_actor_, &AuthManager::on_authorization_lost,
                     G()->shared_config().get_option_string(name));
      }
      break;
    case 'b':
      if (name == "base_language_pack_version") {
        send_closure(td_->language_pack_manager_, &LanguagePackManager::on_language_pack_version_changed, true, -1);
      }
      break;
    case 'c':
      if (name == "connection_parameters") {
        if (G()->mtproto_header().set_parameters(G()->shared_config().get_option_string(name))) {
          G()->net_query_dispatcher().update_mtproto_header();
        }
      }
      break;
    case 'd':
      if (name == "dice_emojis") {
        send_closure(td_->stickers_manager_actor_, &StickersManager::on_update_dice_emojis);
      }
      if (name == "dice_success_values") {
        send_closure(td_->stickers_manager_actor_, &StickersManager::on_update_dice_success_values);
      }
      if (name == "disable_animated_emoji") {
        td_->stickers_manager_->on_update_disable_animated_emojis();
      }
      if (name == "disable_contact_registered_notifications") {
        send_closure(td_->notification_manager_actor_,
                     &NotificationManager::on_disable_contact_registered_notifications_changed);
      }
      if (name == "disable_top_chats") {
        send_closure(td_->top_dialog_manager_actor_, &TopDialogManager::update_is_enabled,
                     !G()->shared_config().get_option_boolean(name));
      }
      break;
    case 'e':
      if (name == "emoji_sounds") {
        send_closure(td_->stickers_manager_actor_, &StickersManager::on_update_emoji_sounds);
      }
      break;
    case 'f':
      if (name == "favorite_stickers_limit") {
        td_->stickers_manager_->on_update_favorite_stickers_limit(
            narrow_cast<int32>(G()->shared_config().get_option_integer(name)));
      }
      break;
    case 'i':
      if (name == "ignored_restriction_reasons") {
        send_closure(td_->contacts_manager_actor_, &ContactsManager::on_ignored_restriction_reasons_changed);
      }
      if (name == "is_emulator") {
        if (G()->mtproto_header().set_is_emulator(G()->shared_config().get_option_boolean(name))) {
          G()->net_query_dispatcher().update_mtproto_header();
        }
      }
      break;
    case 'l':
      if (name == "language_pack_id") {
        send_closure(td_->language_pack_manager_, &LanguagePackManager::on_language_code_changed);
        if (G()->mtproto_header().set_language_code(G()->shared_config().get_option_string(name))) {
          G()->net_query_dispatcher().update_mtproto_header();
        }
      }
      if (name == "language_pack_version") {
        send_closure(td_->language_pack_manager_, &LanguagePackManager::on_language_pack_version_changed, false, -1);
      }
      if (name == "localization_target") {
        send_closure(td_->language_pack_manager_, &LanguagePackManager::on_language_pack_changed);
        if (G()->mtproto_header().set_language_pack(G()->shared_config().get_option_string(name))) {
          G()->net_query_dispatcher().update_mtproto_header();
        }
      }
      break;
    case 'm':
      if (name == "my_id") {
        G()->set_my_id(G()->shared_config().get_option_integer(name));
      }
      break;
    case 'n':
      if (name == "notification_cloud_delay_ms") {
        send_closure(td_->notification_manager_actor_, &NotificationManager::on_notification_cloud_delay_changed);
      }
      if (name == "notification_default_delay_ms") {
        send_closure(td_->notification_manager_actor_, &NotificationManager::on_notification_default_delay_changed);
      }
      if (name == "notification_group_count_max") {
        send_closure(td_->notification_manager_actor_, &NotificationManager::on_notification_group_count_max_changed,
                     true);
      }
      if (name == "notification_group_size_max") {
        send_closure(td_->notification_manager_actor_, &NotificationManager::on_notification_group_size_max_changed);
      }
      break;
    case 'o':
      if (name == "online_cloud_timeout_ms") {
        send_closure(td_->notification_manager_actor_, &NotificationManager::on_online_cloud_timeout_changed);
      }
      if (name == "otherwise_relogin_days") {
        auto days = narrow_cast<int32>(G()->shared_config().get_option_integer(name));
        if (days > 0) {
          vector<SuggestedAction> added_actions{SuggestedAction{SuggestedAction::Type::SetPassword, DialogId(), days}};
          send_closure(G()->td(), &Td::send_update, get_update_suggested_actions_object(added_actions, {}));
        }
      }
      break;
    case 'r':
      if (name == "rating_e_decay") {
        send_closure(td_->top_dialog_manager_actor_, &TopDialogManager::update_rating_e_decay);
      }
      if (name == "recent_stickers_limit") {
        td_->stickers_manager_->on_update_recent_stickers_limit(
            narrow_cast<int32>(G()->shared_config().get_option_integer(name)));
      }
      break;
    case 's':
      if (name == "saved_animations_limit") {
        td_->animations_manager_->on_update_saved_animations_limit(
            narrow_cast<int32>(G()->shared_config().get_option_integer(name)));
      }
      if (name == "session_count") {
        G()->net_query_dispatcher().update_session_count();
      }
      break;
    case 'u':
      if (name == "use_pfs") {
        G()->net_query_dispatcher().update_use_pfs();
      }
      if (name == "use_storage_optimizer") {
        send_closure(td_->storage_manager_, &StorageManager::update_use_storage_optimizer);
      }
      if (name == "utc_time_offset") {
        if (G()->mtproto_header().set_tz_offset(static_cast<int32>(G()->shared_config().get_option_integer(name)))) {
          G()->net_query_dispatcher().update_mtproto_header();
        }
      }
      break;
    default:
      break;
  }

  if (is_internal_option(name)) {
    return;
  }

  // send_closure was already used in the callback
  td_->send_update(td_api::make_object<td_api::updateOption>(name, G()->shared_config().get_option_value(name)));
}

void OptionManager::get_option(const string &name, Promise<td_api::object_ptr<td_api::OptionValue>> &&promise) {
  bool is_bot = td_->auth_manager_ != nullptr && td_->auth_manager_->is_authorized() && td_->auth_manager_->is_bot();
  auto wrap_promise = [&] {
    return PromiseCreator::lambda([promise = std::move(promise), name](Unit result) mutable {
      // the option is already updated on success, ignore errors
      promise.set_value(G()->shared_config().get_option_value(name));
    });
  };
  switch (name[0]) {
    // all these options should be added to getCurrentState
    case 'a':
      if (!is_bot && name == "archive_and_mute_new_chats_from_unknown_users") {
        return send_closure_later(td_->config_manager_, &ConfigManager::get_global_privacy_settings, wrap_promise());
      }
      break;
    case 'c':
      if (!is_bot && name == "can_ignore_sensitive_content_restrictions") {
        return send_closure_later(td_->config_manager_, &ConfigManager::get_content_settings, wrap_promise());
      }
      break;
    case 'd':
      if (!is_bot && name == "disable_contact_registered_notifications") {
        return send_closure_later(td_->notification_manager_actor_,
                                  &NotificationManager::get_disable_contact_registered_notifications, wrap_promise());
      }
      break;
    case 'i':
      if (!is_bot && name == "ignore_sensitive_content_restrictions") {
        return send_closure_later(td_->config_manager_, &ConfigManager::get_content_settings, wrap_promise());
      }
      if (!is_bot && name == "is_location_visible") {
        return send_closure_later(td_->contacts_manager_actor_, &ContactsManager::get_is_location_visible,
                                  wrap_promise());
      }
      break;
    case 'o':
      if (name == "online") {
        return promise.set_value(td_api::make_object<td_api::optionValueBoolean>(td_->is_online()));
      }
      break;
    case 'u':
      if (name == "unix_time") {
        return promise.set_value(td_api::make_object<td_api::optionValueInteger>(G()->unix_time()));
      }
      break;
    case 'v':
      if (name == "version") {
        return promise.set_value(td_api::make_object<td_api::optionValueString>(Td::TDLIB_VERSION));
      }
      break;
  }
  wrap_promise().set_value(Unit());
}

void OptionManager::get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) {
  for (const auto &option : G()->shared_config().get_options()) {
    if (!is_internal_option(option.first)) {
      updates.push_back(td_api::make_object<td_api::updateOption>(
          option.first, ConfigShared::get_option_value_object(option.second)));
    }
  }
}

}  // namespace td
