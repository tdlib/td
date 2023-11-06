//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/OptionManager.h"

#include "td/telegram/AccountManager.h"
#include "td/telegram/AnimationsManager.h"
#include "td/telegram/AttachMenuManager.h"
#include "td/telegram/AuthManager.h"
#include "td/telegram/ConfigManager.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/CountryInfoManager.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/GitCommitHash.h"
#include "td/telegram/Global.h"
#include "td/telegram/JsonValue.h"
#include "td/telegram/LanguagePackManager.h"
#include "td/telegram/net/MtprotoHeader.h"
#include "td/telegram/net/NetQueryDispatcher.h"
#include "td/telegram/NotificationManager.h"
#include "td/telegram/ReactionType.h"
#include "td/telegram/StateManager.h"
#include "td/telegram/StickersManager.h"
#include "td/telegram/StorageManager.h"
#include "td/telegram/StoryManager.h"
#include "td/telegram/SuggestedAction.h"
#include "td/telegram/Td.h"
#include "td/telegram/TdDb.h"
#include "td/telegram/TopDialogManager.h"
#include "td/telegram/UpdatesManager.h"

#include "td/db/KeyValueSyncInterface.h"
#include "td/db/TsSeqKeyValue.h"

#include "td/actor/actor.h"

#include "td/utils/algorithm.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/port/Clocks.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/Status.h"

#include <cmath>
#include <functional>
#include <limits>

namespace td {

OptionManager::OptionManager(Td *td)
    : td_(td)
    , current_scheduler_id_(Scheduler::instance()->sched_id())
    , options_(td::make_unique<TsSeqKeyValue>())
    , option_pmc_(G()->td_db()->get_config_pmc_shared()) {
  send_unix_time_update();

  auto all_options = option_pmc_->get_all();
  all_options["utc_time_offset"] = PSTRING() << 'I' << Clocks::tz_offset();
  for (const auto &name_value : all_options) {
    const string &name = name_value.first;
    CHECK(!name.empty());
    options_->set(name, name_value.second);
    if (!is_internal_option(name)) {
      send_closure(G()->td(), &Td::send_update,
                   td_api::make_object<td_api::updateOption>(name, get_option_value_object(name_value.second)));
    } else {
      auto update = get_internal_option_update(name);
      if (update != nullptr) {
        send_closure(G()->td(), &Td::send_update, std::move(update));
      }
    }
  }

  if (!have_option("message_text_length_max")) {
    set_option_integer("message_text_length_max", 4096);
  }
  if (!have_option("message_caption_length_max")) {
    set_option_integer("message_caption_length_max", 1024);
  }
  if (!have_option("message_reply_quote_length_max")) {
    set_option_integer("message_reply_quote_length_max", 1024);
  }
  if (!have_option("story_caption_length_max")) {
    set_option_integer("story_caption_length_max", 200);
  }
  if (!have_option("bio_length_max")) {
    set_option_integer("bio_length_max", 70);
  }
  if (!have_option("suggested_video_note_length")) {
    set_option_integer("suggested_video_note_length", 384);
  }
  if (!have_option("suggested_video_note_video_bitrate")) {
    set_option_integer("suggested_video_note_video_bitrate", 1000);
  }
  if (!have_option("suggested_video_note_audio_bitrate")) {
    set_option_integer("suggested_video_note_audio_bitrate", 64);
  }
  if (!have_option("notification_sound_duration_max")) {
    set_option_integer("notification_sound_duration_max", 5);
  }
  if (!have_option("notification_sound_size_max")) {
    set_option_integer("notification_sound_size_max", 307200);
  }
  if (!have_option("notification_sound_count_max")) {
    set_option_integer("notification_sound_count_max", G()->is_test_dc() ? 5 : 100);
  }
  if (!have_option("chat_folder_count_max")) {
    set_option_integer("chat_folder_count_max", G()->is_test_dc() ? 3 : 10);
  }
  if (!have_option("chat_folder_chosen_chat_count_max")) {
    set_option_integer("chat_folder_chosen_chat_count_max", G()->is_test_dc() ? 5 : 100);
  }
  if (!have_option("aggressive_anti_spam_supergroup_member_count_min")) {
    set_option_integer("aggressive_anti_spam_supergroup_member_count_min", G()->is_test_dc() ? 1 : 100);
  }
  if (!have_option("pinned_forum_topic_count_max")) {
    set_option_integer("pinned_forum_topic_count_max", G()->is_test_dc() ? 3 : 5);
  }
  if (!have_option("archive_all_stories")) {
    // set_option_boolean("archive_all_stories", false);
  }
  if (!have_option("story_stealth_mode_past_period")) {
    set_option_integer("story_stealth_mode_past_period", 300);
  }
  if (!have_option("story_stealth_mode_future_period")) {
    set_option_integer("story_stealth_mode_future_period", 1500);
  }
  if (!have_option("story_stealth_mode_cooldown_period")) {
    set_option_integer("story_stealth_mode_cooldown_period", 3600);
  }
  if (!have_option("giveaway_additional_chat_count_max")) {
    set_option_integer("giveaway_additional_chat_count_max", G()->is_test_dc() ? 3 : 10);
  }
  if (!have_option("giveaway_country_count_max")) {
    set_option_integer("giveaway_country_count_max", G()->is_test_dc() ? 3 : 10);
  }
  if (!have_option("giveaway_boost_count_per_premium")) {
    set_option_integer("giveaway_boost_count_per_premium", 4);
  }
  if (!have_option("giveaway_duration_max")) {
    set_option_integer("giveaway_duration_max", 7 * 86400);
  }
  if (!have_option("channel_custom_accent_color_boost_level_min")) {
    set_option_integer("channel_custom_accent_color_boost_level_min", 5);
  }
  if (!have_option("premium_gift_boost_count")) {
    set_option_integer("premium_gift_boost_count", 3);
  }

  set_option_empty("archive_and_mute_new_chats_from_unknown_users");
  set_option_empty("chat_filter_count_max");
  set_option_empty("chat_filter_chosen_chat_count_max");
  set_option_empty("forum_member_count_min");
  set_option_empty("themed_emoji_statuses_sticker_set_id");
  set_option_empty("themed_premium_statuses_sticker_set_id");
}

OptionManager::~OptionManager() = default;

void OptionManager::on_td_inited() {
  is_td_inited_ = true;

  for (auto &request : pending_get_options_) {
    get_option(request.first, std::move(request.second));
  }
  reset_to_empty(pending_get_options_);
}

void OptionManager::set_option_boolean(Slice name, bool value) {
  set_option(name, value ? Slice("Btrue") : Slice("Bfalse"));
}

void OptionManager::set_option_empty(Slice name) {
  set_option(name, Slice());
}

void OptionManager::set_option_integer(Slice name, int64 value) {
  set_option(name, PSLICE() << 'I' << value);
}

void OptionManager::set_option_string(Slice name, Slice value) {
  set_option(name, PSLICE() << 'S' << value);
}

bool OptionManager::have_option(Slice name) const {
  return options_->isset(name.str());
}

bool OptionManager::get_option_boolean(Slice name, bool default_value) const {
  auto value = get_option(name);
  if (value.empty()) {
    return default_value;
  }
  if (value == "Btrue") {
    return true;
  }
  if (value == "Bfalse") {
    return false;
  }
  LOG(ERROR) << "Found \"" << value << "\" instead of boolean option " << name;
  return default_value;
}

int64 OptionManager::get_option_integer(Slice name, int64 default_value) const {
  auto value = get_option(name);
  if (value.empty()) {
    return default_value;
  }
  if (value[0] != 'I') {
    LOG(ERROR) << "Found \"" << value << "\" instead of integer option " << name;
    return default_value;
  }
  return to_integer<int64>(value.substr(1));
}

string OptionManager::get_option_string(Slice name, string default_value) const {
  auto value = get_option(name);
  if (value.empty()) {
    return default_value;
  }
  if (value[0] != 'S') {
    LOG(ERROR) << "Found \"" << value << "\" instead of string option " << name;
    return default_value;
  }
  return value.substr(1);
}

void OptionManager::set_option(Slice name, Slice value) {
  CHECK(!name.empty());
  CHECK(Scheduler::instance()->sched_id() == current_scheduler_id_);
  if (value.empty()) {
    if (options_->erase(name.str()) == 0) {
      return;
    }
    option_pmc_->erase(name.str());
  } else {
    if (options_->set(name, value) == 0) {
      return;
    }
    option_pmc_->set(name.str(), value.str());
  }

  if (!G()->close_flag() && is_td_inited_) {
    on_option_updated(name);
  }

  if (!is_internal_option(name)) {
    send_closure(G()->td(), &Td::send_update,
                 td_api::make_object<td_api::updateOption>(name.str(), get_option_value_object(get_option(name))));
  } else {
    auto update = get_internal_option_update(name);
    if (update != nullptr) {
      send_closure(G()->td(), &Td::send_update, std::move(update));
    }
  }
}

string OptionManager::get_option(Slice name) const {
  return options_->get(name.str());
}

td_api::object_ptr<td_api::OptionValue> OptionManager::get_unix_time_option_value_object() {
  return td_api::make_object<td_api::optionValueInteger>(G()->unix_time());
}

void OptionManager::send_unix_time_update() {
  last_sent_server_time_difference_ = G()->get_server_time_difference();
  td_->send_update(td_api::make_object<td_api::updateOption>("unix_time", get_unix_time_option_value_object()));
}

void OptionManager::on_update_server_time_difference() {
  // can be called from any thread
  if (std::abs(G()->get_server_time_difference() - last_sent_server_time_difference_) < 0.5) {
    return;
  }

  send_unix_time_update();
}

bool OptionManager::is_internal_option(Slice name) {
  switch (name[0]) {
    case 'a':
      return name == "about_length_limit_default" || name == "about_length_limit_premium" ||
             name == "aggressive_anti_spam_supergroup_member_count_min" || name == "animated_emoji_zoom" ||
             name == "animation_search_emojis" || name == "animation_search_provider" ||
             name == "authorization_autoconfirm_period";
    case 'b':
      return name == "base_language_pack_version";
    case 'c':
      return name == "call_receive_timeout_ms" || name == "call_ring_timeout_ms" ||
             name == "caption_length_limit_default" || name == "caption_length_limit_premium" ||
             name == "channels_limit_default" || name == "channels_limit_premium" ||
             name == "channels_public_limit_default" || name == "channels_public_limit_premium" ||
             name == "channels_read_media_period" || name == "chat_read_mark_expire_period" ||
             name == "chat_read_mark_size_threshold" || name == "chatlist_invites_limit_default" ||
             name == "chatlist_invites_limit_premium" || name == "chatlists_joined_limit_default" ||
             name == "chatlists_joined_limit_premium";
    case 'd':
      return name == "dc_txt_domain_name" || name == "default_reaction" || name == "default_reaction_needs_sync" ||
             name == "dialog_filters_chats_limit_default" || name == "dialog_filters_chats_limit_premium" ||
             name == "dialog_filters_limit_default" || name == "dialog_filters_limit_premium" ||
             name == "dialogs_folder_pinned_limit_default" || name == "dialogs_folder_pinned_limit_premium" ||
             name == "dialogs_pinned_limit_default" || name == "dialogs_pinned_limit_premium" ||
             name == "dice_emojis" || name == "dice_success_values";
    case 'e':
      return name == "edit_time_limit" || name == "emoji_sounds";
    case 'f':
      return name == "fragment_prefixes";
    case 'h':
      return name == "hidden_members_group_size_min";
    case 'i':
      return name == "ignored_restriction_reasons";
    case 'l':
      return name == "language_pack_version";
    case 'm':
      return name == "my_phone_number";
    case 'n':
      return name == "need_premium_for_story_caption_entities" || name == "need_synchronize_archive_all_stories" ||
             name == "notification_cloud_delay_ms" || name == "notification_default_delay_ms";
    case 'o':
      return name == "online_cloud_timeout_ms" || name == "online_update_period_ms" || name == "otherwise_relogin_days";
    case 'p':
      return name == "premium_bot_username" || name == "premium_features" || name == "premium_invoice_slug";
    case 'r':
      return name == "rating_e_decay" || name == "reactions_uniq_max" || name == "reactions_user_max_default" ||
             name == "reactions_user_max_premium" || name == "recent_stickers_limit" ||
             name == "restriction_add_platforms" || name == "revoke_pm_inbox" || name == "revoke_time_limit" ||
             name == "revoke_pm_time_limit";
    case 's':
      return name == "saved_animations_limit" || name == "saved_gifs_limit_default" ||
             name == "saved_gifs_limit_premium" || name == "session_count" || name == "since_last_open" ||
             name == "stickers_faved_limit_default" || name == "stickers_faved_limit_premium" ||
             name == "stickers_normal_by_emoji_per_premium_num" || name == "stickers_premium_by_emoji_num" ||
             name == "stories_changelog_user_id" || name == "stories_sent_monthly_limit_default" ||
             name == "stories_sent_monthly_limit_premium" || name == "stories_sent_weekly_limit_default" ||
             name == "stories_sent_weekly_limit_premium" || name == "stories_suggested_reactions_limit_default" ||
             name == "stories_suggested_reactions_limit_premium" || name == "story_caption_length_limit_default" ||
             name == "story_caption_length_limit_premium" || name == "story_expiring_limit_default" ||
             name == "story_expiring_limit_premium";
    case 'v':
      return name == "video_note_size_max";
    case 'w':
      return name == "webfile_dc_id";
    default:
      return false;
  }
}

td_api::object_ptr<td_api::Update> OptionManager::get_internal_option_update(Slice name) const {
  if (name == "default_reaction") {
    return ReactionType(get_option_string(name)).get_update_default_reaction_type();
  }
  if (name == "otherwise_relogin_days") {
    auto days = narrow_cast<int32>(get_option_integer(name));
    if (days > 0) {
      vector<SuggestedAction> added_actions{SuggestedAction{SuggestedAction::Type::SetPassword, DialogId(), days}};
      return get_update_suggested_actions_object(added_actions, {}, "get_internal_option_update");
    }
  }
  return nullptr;
}

const vector<Slice> &OptionManager::get_synchronous_options() {
  static const vector<Slice> options{"version", "commit_hash"};
  return options;
}

bool OptionManager::is_synchronous_option(Slice name) {
  return td::contains(get_synchronous_options(), name);
}

void OptionManager::on_option_updated(Slice name) {
  switch (name[0]) {
    case 'a':
      if (name == "animated_emoji_zoom") {
        // nothing to do: animated emoji zoom is updated only at launch
      }
      if (name == "animation_search_emojis") {
        td_->animations_manager_->on_update_animation_search_emojis();
      }
      if (name == "animation_search_provider") {
        td_->animations_manager_->on_update_animation_search_provider();
      }
      if (name == "authorization_autoconfirm_period") {
        td_->account_manager_->update_unconfirmed_authorization_timeout(true);
      }
      break;
    case 'b':
      if (name == "base_language_pack_version") {
        send_closure(td_->language_pack_manager_, &LanguagePackManager::on_language_pack_version_changed, true, -1);
      }
      break;
    case 'c':
      if (name == "connection_parameters") {
        if (G()->mtproto_header().set_parameters(get_option_string(name))) {
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
        send_closure(td_->top_dialog_manager_actor_, &TopDialogManager::update_is_enabled, !get_option_boolean(name));
      }
      break;
    case 'e':
      if (name == "emoji_sounds") {
        send_closure(td_->stickers_manager_actor_, &StickersManager::on_update_emoji_sounds);
      }
      break;
    case 'f':
      if (name == "favorite_stickers_limit") {
        td_->stickers_manager_->on_update_favorite_stickers_limit();
      }
      if (name == "fragment_prefixes") {
        send_closure(td_->country_info_manager_actor_, &CountryInfoManager::on_update_fragment_prefixes);
      }
      break;
    case 'i':
      if (name == "ignored_restriction_reasons") {
        send_closure(td_->contacts_manager_actor_, &ContactsManager::on_ignored_restriction_reasons_changed);
      }
      if (name == "is_emulator") {
        if (G()->mtproto_header().set_is_emulator(get_option_boolean(name))) {
          G()->net_query_dispatcher().update_mtproto_header();
        }
      }
      if (name == "is_premium") {
        set_option_boolean(
            "can_use_text_entities_in_story_caption",
            !get_option_boolean("need_premium_for_story_caption_entities") || get_option_boolean("is_premium"));
      }
      break;
    case 'l':
      if (name == "language_pack_id") {
        send_closure(td_->language_pack_manager_, &LanguagePackManager::on_language_code_changed);
        if (G()->mtproto_header().set_language_code(get_option_string(name))) {
          G()->net_query_dispatcher().update_mtproto_header();
        }
        send_closure(td_->attach_menu_manager_actor_, &AttachMenuManager::reload_attach_menu_bots, Promise<Unit>());
      }
      if (name == "language_pack_version") {
        send_closure(td_->language_pack_manager_, &LanguagePackManager::on_language_pack_version_changed, false, -1);
      }
      if (name == "localization_target") {
        send_closure(td_->language_pack_manager_, &LanguagePackManager::on_language_pack_changed);
        if (G()->mtproto_header().set_language_pack(get_option_string(name))) {
          G()->net_query_dispatcher().update_mtproto_header();
        }
      }
      break;
    case 'n':
      if (name == "need_premium_for_story_caption_entities") {
        set_option_boolean(
            "can_use_text_entities_in_story_caption",
            !get_option_boolean("need_premium_for_story_caption_entities") || get_option_boolean("is_premium"));
      }
      if (name == "need_synchronize_archive_all_stories") {
        send_closure(td_->story_manager_actor_, &StoryManager::try_synchronize_archive_all_stories);
      }
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
      break;
    case 'r':
      if (name == "rating_e_decay") {
        send_closure(td_->top_dialog_manager_actor_, &TopDialogManager::update_rating_e_decay);
      }
      if (name == "recent_stickers_limit") {
        td_->stickers_manager_->on_update_recent_stickers_limit();
      }
      break;
    case 's':
      if (name == "saved_animations_limit") {
        td_->animations_manager_->on_update_saved_animations_limit();
      }
      if (name == "session_count") {
        G()->net_query_dispatcher().update_session_count();
        td_->updates_manager_->init_sessions(false);
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
        if (G()->mtproto_header().set_tz_offset(static_cast<int32>(get_option_integer(name)))) {
          G()->net_query_dispatcher().update_mtproto_header();
        }
      }
      break;
    default:
      break;
  }
}

void OptionManager::get_option(const string &name, Promise<td_api::object_ptr<td_api::OptionValue>> &&promise) {
  bool is_bot = td_->auth_manager_ != nullptr && td_->auth_manager_->is_authorized() && td_->auth_manager_->is_bot();
  auto wrap_promise = [this, &promise, &name] {
    return PromiseCreator::lambda([this, promise = std::move(promise), name](Unit result) mutable {
      // the option is already updated on success, ignore errors
      promise.set_value(get_option_value_object(get_option(name)));
    });
  };
  switch (name[0]) {
    // all these options should be added to getCurrentState
    case 'c':
      if (!is_bot && name == "can_ignore_sensitive_content_restrictions") {
        return send_closure_later(td_->config_manager_, &ConfigManager::get_content_settings, wrap_promise());
      }
      break;
    case 'd':
      if (!is_bot && name == "disable_contact_registered_notifications") {
        if (is_td_inited_) {
          send_closure_later(td_->notification_manager_actor_,
                             &NotificationManager::get_disable_contact_registered_notifications, wrap_promise());
        } else {
          pending_get_options_.emplace_back(name, std::move(promise));
        }
        return;
      }
      break;
    case 'i':
      if (!is_bot && name == "ignore_sensitive_content_restrictions") {
        return send_closure_later(td_->config_manager_, &ConfigManager::get_content_settings, wrap_promise());
      }
      if (!is_bot && name == "is_location_visible") {
        if (is_td_inited_) {
          send_closure_later(td_->contacts_manager_actor_, &ContactsManager::get_is_location_visible, wrap_promise());
        } else {
          pending_get_options_.emplace_back(name, std::move(promise));
        }
        return;
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
  }
  wrap_promise().set_value(Unit());
}

td_api::object_ptr<td_api::OptionValue> OptionManager::get_option_synchronously(Slice name) {
  CHECK(!name.empty());
  switch (name[0]) {
    case 'c':
      if (name == "commit_hash") {
        return td_api::make_object<td_api::optionValueString>(get_git_commit_hash());
      }
      break;
    case 'v':
      if (name == "version") {
        return td_api::make_object<td_api::optionValueString>("1.8.21");
      }
      break;
  }
  UNREACHABLE();
}

void OptionManager::set_option(const string &name, td_api::object_ptr<td_api::OptionValue> &&value,
                               Promise<Unit> &&promise) {
  int32 value_constructor_id = value == nullptr ? td_api::optionValueEmpty::ID : value->get_id();

  auto set_integer_option = [&](Slice option_name, int64 min_value = 0,
                                int64 max_value = std::numeric_limits<int32>::max()) {
    if (name != option_name) {
      return false;
    }
    if (value_constructor_id == td_api::optionValueEmpty::ID) {
      set_option_empty(option_name);
    } else {
      if (value_constructor_id != td_api::optionValueInteger::ID) {
        promise.set_error(Status::Error(400, PSLICE() << "Option \"" << name << "\" must have integer value"));
        return false;
      }

      int64 int_value = static_cast<td_api::optionValueInteger *>(value.get())->value_;
      if (int_value < min_value || int_value > max_value) {
        promise.set_error(Status::Error(400, PSLICE() << "Option's \"" << name << "\" value " << int_value
                                                      << " is outside of the valid range [" << min_value << ", "
                                                      << max_value << "]"));
        return false;
      }
      set_option_integer(name, int_value);
    }
    promise.set_value(Unit());
    return true;
  };

  auto set_boolean_option = [&](Slice option_name) {
    if (name != option_name) {
      return false;
    }
    if (value_constructor_id == td_api::optionValueEmpty::ID) {
      set_option_empty(name);
    } else {
      if (value_constructor_id != td_api::optionValueBoolean::ID) {
        promise.set_error(Status::Error(400, PSLICE() << "Option \"" << name << "\" must have boolean value"));
        return false;
      }

      bool bool_value = static_cast<td_api::optionValueBoolean *>(value.get())->value_;
      set_option_boolean(name, bool_value);
    }
    promise.set_value(Unit());
    return true;
  };

  auto set_string_option = [&](Slice option_name, std::function<bool(Slice)> check_value) {
    if (name != option_name) {
      return false;
    }
    if (value_constructor_id == td_api::optionValueEmpty::ID) {
      set_option_empty(name);
    } else {
      if (value_constructor_id != td_api::optionValueString::ID) {
        promise.set_error(Status::Error(400, PSLICE() << "Option \"" << name << "\" must have string value"));
        return false;
      }

      const string &str_value = static_cast<td_api::optionValueString *>(value.get())->value_;
      if (str_value.empty()) {
        set_option_empty(name);
      } else {
        if (check_value(str_value)) {
          set_option_string(name, str_value);
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
      /*
      if (!is_bot && set_boolean_option("archive_all_stories")) {
        set_option_boolean("need_synchronize_archive_all_stories", true);
        return;
      }
      */
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
      if (set_boolean_option("disable_network_statistics")) {
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
      if (set_boolean_option("ignore_file_names")) {
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
        if (!get_option_boolean("can_ignore_sensitive_content_restrictions")) {
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
        ContactsManager::set_location_visibility(td_);
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
        td_->set_is_online(is_online);
        if (!is_bot) {
          send_closure(td_->state_manager_, &StateManager::on_online, is_online);
        }
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
          set_option_boolean(name, static_cast<const td_api::optionValueBoolean *>(value.get())->value_);
          break;
        case td_api::optionValueEmpty::ID:
          set_option_empty(name);
          break;
        case td_api::optionValueInteger::ID:
          set_option_integer(name, static_cast<const td_api::optionValueInteger *>(value.get())->value_);
          break;
        case td_api::optionValueString::ID:
          set_option_string(name, static_cast<const td_api::optionValueString *>(value.get())->value_);
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

void OptionManager::get_common_state(vector<td_api::object_ptr<td_api::Update>> &updates) {
  for (auto option_name : get_synchronous_options()) {
    updates.push_back(
        td_api::make_object<td_api::updateOption>(option_name.str(), get_option_synchronously(option_name)));
  }
}

void OptionManager::get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const {
  get_common_state(updates);

  updates.push_back(td_api::make_object<td_api::updateOption>(
      "online", td_api::make_object<td_api::optionValueBoolean>(td_->is_online())));

  updates.push_back(td_api::make_object<td_api::updateOption>("unix_time", get_unix_time_option_value_object()));

  for (const auto &option : options_->get_all()) {
    if (!is_internal_option(option.first)) {
      updates.push_back(
          td_api::make_object<td_api::updateOption>(option.first, get_option_value_object(option.second)));
    } else {
      auto update = get_internal_option_update(option.first);
      if (update != nullptr) {
        updates.push_back(std::move(update));
      }
    }
  }
}

}  // namespace td
