//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
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
#include "td/telegram/DialogId.h"
#include "td/telegram/Global.h"
#include "td/telegram/JsonValue.h"
#include "td/telegram/LanguagePackManager.h"
#include "td/telegram/net/MtprotoHeader.h"
#include "td/telegram/net/NetQueryDispatcher.h"
#include "td/telegram/NotificationManager.h"
#include "td/telegram/StateManager.h"
#include "td/telegram/StickersManager.h"
#include "td/telegram/StorageManager.h"
#include "td/telegram/SuggestedAction.h"
#include "td/telegram/Td.h"
#include "td/telegram/TdDb.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/TopDialogManager.h"

#include "td/utils/buffer.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/Status.h"

#include <cmath>
#include <limits>

namespace td {

class SetDefaultReactionQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit SetDefaultReactionQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(const string &reaction) {
    send_query(G()->net_query_creator().create(telegram_api::messages_setDefaultReaction(reaction)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_setDefaultReaction>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    if (result_ptr.ok()) {
      promise_.set_value(Unit());
    } else {
      on_error(Status::Error(400, "Receive false"));
    }
  }

  void on_error(Status status) final {
    LOG(INFO) << "Failed to set default reaction: " << status;
    promise_.set_error(std::move(status));
  }
};

OptionManager::OptionManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
  send_unix_time_update();
}

void OptionManager::tear_down() {
  parent_.reset();
}

OptionManager::~OptionManager() = default;

td_api::object_ptr<td_api::OptionValue> OptionManager::get_unix_time_option_value_object() {
  return td_api::make_object<td_api::optionValueInteger>(G()->unix_time());
}

void OptionManager::send_unix_time_update() {
  last_sent_server_time_difference_ = G()->get_server_time_difference();
  td_->send_update(td_api::make_object<td_api::updateOption>("unix_time", get_unix_time_option_value_object()));
}

void OptionManager::on_update_server_time_difference() {
  if (std::abs(G()->get_server_time_difference() - last_sent_server_time_difference_) < 0.5) {
    return;
  }

  send_unix_time_update();
}

void OptionManager::clear_options() {
  for (const auto &option : G()->shared_config().get_options()) {
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
      return name == "call_receive_timeout_ms" || name == "call_ring_timeout_ms" ||
             name == "channels_read_media_period" || name == "chat_read_mark_expire_period" ||
             name == "chat_read_mark_size_threshold";
    case 'd':
      return name == "dc_txt_domain_name" || name == "default_reaction_needs_sync" || name == "dice_emojis" ||
             name == "dice_success_values";
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
      return name == "online_cloud_timeout_ms" || name == "online_update_period_ms" || name == "otherwise_relogin_days";
    case 'r':
      return name == "rating_e_decay" || name == "reactions_uniq_max" || name == "recent_stickers_limit" ||
             name == "revoke_pm_inbox" || name == "revoke_time_limit" || name == "revoke_pm_time_limit";
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
      if (name == "default_reaction_needs_sync" && G()->shared_config().get_option_boolean(name)) {
        set_default_reaction();
      }
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
  td_->send_update(
      td_api::make_object<td_api::updateOption>(name, get_option_value_object(G()->shared_config().get_option(name))));
}

void OptionManager::get_option(const string &name, Promise<td_api::object_ptr<td_api::OptionValue>> &&promise) {
  bool is_bot = td_->auth_manager_ != nullptr && td_->auth_manager_->is_authorized() && td_->auth_manager_->is_bot();
  auto wrap_promise = [&] {
    return PromiseCreator::lambda([promise = std::move(promise), name](Unit result) mutable {
      // the option is already updated on success, ignore errors
      promise.set_value(get_option_value_object(G()->shared_config().get_option(name)));
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
        return promise.set_value(get_unix_time_option_value_object());
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

void OptionManager::set_option(const string &name, td_api::object_ptr<td_api::OptionValue> &&value,
                               Promise<Unit> &&promise) {
  int32 value_constructor_id = value == nullptr ? td_api::optionValueEmpty::ID : value->get_id();

  auto set_integer_option = [&](Slice option_name, int64 min_value = 0,
                                int64 max_value = std::numeric_limits<int32>::max()) {
    if (name != option_name) {
      return false;
    }
    if (value_constructor_id != td_api::optionValueInteger::ID &&
        value_constructor_id != td_api::optionValueEmpty::ID) {
      promise.set_error(Status::Error(400, PSLICE() << "Option \"" << name << "\" must have integer value"));
      return false;
    }
    if (value_constructor_id == td_api::optionValueEmpty::ID) {
      G()->shared_config().set_option_empty(option_name);
    } else {
      int64 int_value = static_cast<td_api::optionValueInteger *>(value.get())->value_;
      if (int_value < min_value || int_value > max_value) {
        promise.set_error(Status::Error(400, PSLICE() << "Option's \"" << name << "\" value " << int_value
                                                      << " is outside of the valid range [" << min_value << ", "
                                                      << max_value << "]"));
        return false;
      }
      G()->shared_config().set_option_integer(name, int_value);
    }
    promise.set_value(Unit());
    return true;
  };

  auto set_boolean_option = [&](Slice option_name) {
    if (name != option_name) {
      return false;
    }
    if (value_constructor_id != td_api::optionValueBoolean::ID &&
        value_constructor_id != td_api::optionValueEmpty::ID) {
      promise.set_error(Status::Error(400, PSLICE() << "Option \"" << name << "\" must have boolean value"));
      return false;
    }
    if (value_constructor_id == td_api::optionValueEmpty::ID) {
      G()->shared_config().set_option_empty(name);
    } else {
      bool bool_value = static_cast<td_api::optionValueBoolean *>(value.get())->value_;
      G()->shared_config().set_option_boolean(name, bool_value);
    }
    promise.set_value(Unit());
    return true;
  };

  auto set_string_option = [&](Slice option_name, auto check_value) {
    if (name != option_name) {
      return false;
    }
    if (value_constructor_id != td_api::optionValueString::ID && value_constructor_id != td_api::optionValueEmpty::ID) {
      promise.set_error(Status::Error(400, PSLICE() << "Option \"" << name << "\" must have string value"));
      return false;
    }
    if (value_constructor_id == td_api::optionValueEmpty::ID) {
      G()->shared_config().set_option_empty(name);
    } else {
      const string &str_value = static_cast<td_api::optionValueString *>(value.get())->value_;
      if (str_value.empty()) {
        G()->shared_config().set_option_empty(name);
      } else {
        if (check_value(str_value)) {
          G()->shared_config().set_option_string(name, str_value);
        } else {
          promise.set_error(Status::Error(400, PSLICE() << "Option \"" << name << "\" can't have specified value"));
          return false;
        }
      }
    }
    promise.set_value(Unit());
    return true;
  };

  bool is_bot = td_->auth_manager_ != nullptr && td_->auth_manager_->is_authorized() && td_->auth_manager_->is_bot();
  switch (name[0]) {
    case 'a':
      if (set_boolean_option("always_parse_markdown")) {
        return;
      }
      if (!is_bot && name == "archive_and_mute_new_chats_from_unknown_users") {
        if (value_constructor_id != td_api::optionValueBoolean::ID &&
            value_constructor_id != td_api::optionValueEmpty::ID) {
          return promise.set_error(
              Status::Error(400, "Option \"archive_and_mute_new_chats_from_unknown_users\" must have boolean value"));
        }

        auto archive_and_mute = value_constructor_id == td_api::optionValueBoolean::ID &&
                                static_cast<td_api::optionValueBoolean *>(value.get())->value_;
        send_closure_later(td_->config_manager_, &ConfigManager::set_archive_and_mute, archive_and_mute,
                           std::move(promise));
        return;
      }
      break;
    case 'c':
      if (!is_bot && set_string_option("connection_parameters", [](Slice value) {
            string value_copy = value.str();
            auto r_json_value = get_json_value(value_copy);
            if (r_json_value.is_error()) {
              return false;
            }
            return r_json_value.ok()->get_id() == td_api::jsonValueObject::ID;
          })) {
        return;
      }
      break;
    case 'd':
      if (!is_bot && set_string_option("default_reaction", [td = td_](Slice value) {
            return td->stickers_manager_->is_active_reaction(value.str());
          })) {
        G()->shared_config().set_option_boolean("default_reaction_needs_sync", true);
        return;
      }
      if (!is_bot && set_boolean_option("disable_animated_emoji")) {
        return;
      }
      if (!is_bot && set_boolean_option("disable_contact_registered_notifications")) {
        return;
      }
      if (!is_bot && set_boolean_option("disable_sent_scheduled_message_notifications")) {
        return;
      }
      if (!is_bot && set_boolean_option("disable_top_chats")) {
        return;
      }
      if (set_boolean_option("disable_persistent_network_statistics")) {
        return;
      }
      if (set_boolean_option("disable_time_adjustment_protection")) {
        return;
      }
      if (name == "drop_notification_ids") {
        G()->td_db()->get_binlog_pmc()->erase("notification_id_current");
        G()->td_db()->get_binlog_pmc()->erase("notification_group_id_current");
        return promise.set_value(Unit());
      }
      break;
    case 'i':
      if (set_boolean_option("ignore_background_updates")) {
        return;
      }
      if (set_boolean_option("ignore_default_disable_notification")) {
        return;
      }
      if (set_boolean_option("ignore_inline_thumbnails")) {
        return;
      }
      if (set_boolean_option("ignore_platform_restrictions")) {
        return;
      }
      if (set_boolean_option("is_emulator")) {
        return;
      }
      if (!is_bot && name == "ignore_sensitive_content_restrictions") {
        if (!G()->shared_config().get_option_boolean("can_ignore_sensitive_content_restrictions")) {
          return promise.set_error(
              Status::Error(400, "Option \"ignore_sensitive_content_restrictions\" can't be changed by the user"));
        }

        if (value_constructor_id != td_api::optionValueBoolean::ID &&
            value_constructor_id != td_api::optionValueEmpty::ID) {
          return promise.set_error(
              Status::Error(400, "Option \"ignore_sensitive_content_restrictions\" must have boolean value"));
        }

        auto ignore_sensitive_content_restrictions = value_constructor_id == td_api::optionValueBoolean::ID &&
                                                     static_cast<td_api::optionValueBoolean *>(value.get())->value_;
        send_closure_later(td_->config_manager_, &ConfigManager::set_content_settings,
                           ignore_sensitive_content_restrictions, std::move(promise));
        return;
      }
      if (!is_bot && set_boolean_option("is_location_visible")) {
        td_->contacts_manager_->set_location_visibility();
        return;
      }
      break;
    case 'l':
      if (!is_bot && set_string_option("language_pack_database_path", [](Slice value) { return true; })) {
        return;
      }
      if (!is_bot && set_string_option("localization_target", LanguagePackManager::check_language_pack_name)) {
        return;
      }
      if (!is_bot && set_string_option("language_pack_id", LanguagePackManager::check_language_code_name)) {
        return;
      }
      break;
    case 'm':
      if (set_integer_option("message_unload_delay", 60, 86400)) {
        return;
      }
      break;
    case 'n':
      if (!is_bot &&
          set_integer_option("notification_group_count_max", NotificationManager::MIN_NOTIFICATION_GROUP_COUNT_MAX,
                             NotificationManager::MAX_NOTIFICATION_GROUP_COUNT_MAX)) {
        return;
      }
      if (!is_bot &&
          set_integer_option("notification_group_size_max", NotificationManager::MIN_NOTIFICATION_GROUP_SIZE_MAX,
                             NotificationManager::MAX_NOTIFICATION_GROUP_SIZE_MAX)) {
        return;
      }
      break;
    case 'o':
      if (name == "online") {
        if (value_constructor_id != td_api::optionValueBoolean::ID &&
            value_constructor_id != td_api::optionValueEmpty::ID) {
          return promise.set_error(Status::Error(400, "Option \"online\" must have boolean value"));
        }
        bool is_online = value_constructor_id == td_api::optionValueEmpty::ID ||
                         static_cast<const td_api::optionValueBoolean *>(value.get())->value_;
        if (!is_bot) {
          send_closure(td_->state_manager_, &StateManager::on_online, is_online);
        }
        td_->set_is_online(is_online);
        return promise.set_value(Unit());
      }
      break;
    case 'p':
      if (set_boolean_option("prefer_ipv6")) {
        send_closure(td_->state_manager_, &StateManager::on_network_updated);
        return;
      }
      break;
    case 'r':
      // temporary option
      if (set_boolean_option("reuse_uploaded_photos_by_hash")) {
        return;
      }
      break;
    case 's':
      if (set_integer_option("storage_max_files_size")) {
        return;
      }
      if (set_integer_option("storage_max_time_from_last_access")) {
        return;
      }
      if (set_integer_option("storage_max_file_count")) {
        return;
      }
      if (set_integer_option("storage_immunity_delay")) {
        return;
      }
      if (set_boolean_option("store_all_files_in_files_directory")) {
        return;
      }
      break;
    case 't':
      if (set_boolean_option("test_flood_wait")) {
        return;
      }
      break;
    case 'u':
      if (set_boolean_option("use_pfs")) {
        return;
      }
      if (set_boolean_option("use_quick_ack")) {
        return;
      }
      if (set_boolean_option("use_storage_optimizer")) {
        return;
      }
      if (set_integer_option("utc_time_offset", -12 * 60 * 60, 14 * 60 * 60)) {
        return;
      }
      break;
    case 'X':
    case 'x': {
      if (name.size() > 255) {
        return promise.set_error(Status::Error(400, "Option name is too long"));
      }
      switch (value_constructor_id) {
        case td_api::optionValueBoolean::ID:
          G()->shared_config().set_option_boolean(name,
                                                  static_cast<const td_api::optionValueBoolean *>(value.get())->value_);
          break;
        case td_api::optionValueEmpty::ID:
          G()->shared_config().set_option_empty(name);
          break;
        case td_api::optionValueInteger::ID:
          G()->shared_config().set_option_integer(name,
                                                  static_cast<const td_api::optionValueInteger *>(value.get())->value_);
          break;
        case td_api::optionValueString::ID:
          G()->shared_config().set_option_string(name,
                                                 static_cast<const td_api::optionValueString *>(value.get())->value_);
          break;
        default:
          UNREACHABLE();
      }
      return promise.set_value(Unit());
    }
  }

  if (promise) {
    promise.set_error(Status::Error(400, "Option can't be set"));
  }
}

td_api::object_ptr<td_api::OptionValue> OptionManager::get_option_value_object(Slice value) {
  if (value.empty()) {
    return td_api::make_object<td_api::optionValueEmpty>();
  }

  switch (value[0]) {
    case 'B':
      if (value == "Btrue") {
        return td_api::make_object<td_api::optionValueBoolean>(true);
      }
      if (value == "Bfalse") {
        return td_api::make_object<td_api::optionValueBoolean>(false);
      }
      break;
    case 'I':
      return td_api::make_object<td_api::optionValueInteger>(to_integer<int64>(value.substr(1)));
    case 'S':
      return td_api::make_object<td_api::optionValueString>(value.substr(1).str());
  }

  return td_api::make_object<td_api::optionValueString>(value.str());
}

void OptionManager::get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const {
  updates.push_back(td_api::make_object<td_api::updateOption>(
      "version", td_api::make_object<td_api::optionValueString>(Td::TDLIB_VERSION)));

  updates.push_back(td_api::make_object<td_api::updateOption>(
      "online", td_api::make_object<td_api::optionValueBoolean>(td_->is_online())));

  updates.push_back(td_api::make_object<td_api::updateOption>("unix_time", get_unix_time_option_value_object()));

  for (const auto &option : G()->shared_config().get_options()) {
    if (!is_internal_option(option.first)) {
      updates.push_back(
          td_api::make_object<td_api::updateOption>(option.first, get_option_value_object(option.second)));
    }
  }
}

void OptionManager::set_default_reaction() {
  auto promise = PromiseCreator::lambda([actor_id = actor_id(this)](Result<Unit> &&result) {
    send_closure(actor_id, &OptionManager::on_set_default_reaction, result.is_ok());
  });
  td_->create_handler<SetDefaultReactionQuery>(std::move(promise))
      ->send(G()->shared_config().get_option_string("default_reaction"));
}

void OptionManager::on_set_default_reaction(bool success) {
  if (G()->close_flag() && !success) {
    return;
  }

  G()->shared_config().set_option_empty("default_reaction_needs_sync");
  if (!success) {
    send_closure(G()->config_manager(), &ConfigManager::reget_app_config, Promise<Unit>());
  }
}

}  // namespace td
