//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/LinkManager.h"

#include "td/telegram/AccessRights.h"
#include "td/telegram/BackgroundType.h"
#include "td/telegram/ChannelId.h"
#include "td/telegram/ChannelType.h"
#include "td/telegram/ChatManager.h"
#include "td/telegram/ConfigManager.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/DialogInviteLinkManager.h"
#include "td/telegram/DialogManager.h"
#include "td/telegram/DialogParticipant.h"
#include "td/telegram/Global.h"
#include "td/telegram/LanguagePackManager.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/misc.h"
#include "td/telegram/net/Proxy.h"
#include "td/telegram/OptionManager.h"
#include "td/telegram/ServerMessageId.h"
#include "td/telegram/StarGiftCollectionId.h"
#include "td/telegram/StickersManager.h"
#include "td/telegram/StoryAlbumId.h"
#include "td/telegram/StoryId.h"
#include "td/telegram/Td.h"
#include "td/telegram/TdDb.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UserId.h"
#include "td/telegram/UserManager.h"

#include "td/mtproto/ProxySecret.h"

#include "td/utils/algorithm.h"
#include "td/utils/base64.h"
#include "td/utils/buffer.h"
#include "td/utils/FlatHashSet.h"
#include "td/utils/HttpUrl.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/Time.h"
#include "td/utils/utf8.h"

#include <tuple>

namespace td {

static bool is_valid_start_parameter(Slice start_parameter) {
  return is_base64url_characters(start_parameter);
}

static bool is_valid_video_chat_invite_hash(Slice invite_hash) {
  return is_base64url_characters(invite_hash);
}

static bool is_valid_phone_number(Slice phone_number) {
  if (phone_number.empty() || phone_number.size() > 32) {
    return false;
  }
  for (auto c : phone_number) {
    if (!is_digit(c)) {
      return false;
    }
  }
  return true;
}

static bool is_valid_phone_number_hash(Slice hash) {
  if (hash.empty() || hash.size() > 32) {
    return false;
  }
  for (auto c : hash) {
    if (!is_hex_digit(c)) {
      return false;
    }
  }
  return true;
}

static bool is_valid_game_name(Slice name) {
  return name.size() >= 3 && is_valid_username(name);
}

static bool is_valid_theme_name(CSlice name) {
  return !name.empty() && check_utf8(name);
}

static bool is_valid_web_app_name(Slice name) {
  return name.size() >= 3 && is_valid_username(name);
}

static bool is_valid_sticker_set_name(Slice name) {
  return !name.empty() && is_base64url_characters(name);
}

static bool is_valid_language_pack_id(Slice language_pack_id) {
  return !language_pack_id.empty() && LanguagePackManager::check_language_code_name(language_pack_id);
}

static bool is_valid_upgraded_gift_name(CSlice name) {
  if (name.empty()) {
    return false;
  }
  if (!check_utf8(name)) {
    return false;
  }
  return true;
}

static bool is_valid_user_token(CSlice token) {
  return !token.empty() && check_utf8(token);
}

static bool is_valid_login_code(CSlice code) {
  return !code.empty() && check_utf8(code);
}

static bool is_valid_premium_referrer(CSlice referrer) {
  return check_utf8(referrer);
}

static bool is_valid_proxy_server(CSlice server) {
  return !server.empty() && server.size() <= 255u && check_utf8(server);
}

static bool is_valid_proxy_username(CSlice username) {
  return check_utf8(username);
}

static bool is_valid_proxy_password(CSlice password) {
  return check_utf8(password);
}

static bool is_valid_invoice_name(CSlice invoice_name) {
  return !invoice_name.empty() && check_utf8(invoice_name);
}

static bool is_valid_gift_code(CSlice gift_code) {
  return !gift_code.empty() && check_utf8(gift_code);
}

static bool is_valid_business_link_name(CSlice link_name) {
  return !link_name.empty() && check_utf8(link_name);
}

static bool is_valid_star_top_up_purpose(CSlice purpose) {
  return check_utf8(purpose);
}

static bool is_valid_story_id(Slice story_id) {
  auto r_story_id = to_integer_safe<int32>(story_id);
  return r_story_id.is_ok() && StoryId(r_story_id.ok()).is_server();
}

static bool is_valid_star_gift_collection_id(Slice collection_id) {
  auto r_collection_id = to_integer_safe<int32>(collection_id);
  return r_collection_id.is_ok() && StarGiftCollectionId(r_collection_id.ok()).is_valid();
}

static bool is_valid_story_album_id(Slice story_album_id) {
  auto r_story_album_id = to_integer_safe<int32>(story_album_id);
  return r_story_album_id.is_ok() && StoryAlbumId(r_story_album_id.ok()).is_valid();
}

static const vector<string> &get_appearance_settings_subsections() {
  static const vector<string> subsections{
      "themes", "themes/edit", "themes/create", "wallpapers", "wallpapers/edit", "wallpapers/set",
      "wallpapers/choose-photo", "your-color/profile", "your-color/profile/add-icons", "your-color/profile/use-gift",
      "your-color/profile/reset", "your-color/name", "your-color/name/add-icons", "your-color/name/use-gift",
      "night-mode", "auto-night-mode", "text-size", "text-size/use-system", "message-corners", "animations",
      "stickers-and-emoji", "stickers-and-emoji/edit", "stickers-and-emoji/trending", "stickers-and-emoji/archived",
      "stickers-and-emoji/archived/edit", "stickers-and-emoji/emoji", "stickers-and-emoji/emoji/edit",
      "stickers-and-emoji/emoji/archived", "stickers-and-emoji/emoji/archived/edit", "stickers-and-emoji/emoji/suggest",
      "stickers-and-emoji/emoji/quick-reaction", "stickers-and-emoji/emoji/quick-reaction/choose",
      "stickers-and-emoji/suggest-by-emoji", "stickers-and-emoji/large-emoji", "stickers-and-emoji/dynamic-order",
      "stickers-and-emoji/emoji/show-more", "app-icon",
      // no formatting
      "tap-for-next-media"};
  return subsections;
}

static const vector<string> &get_business_settings_subsections() {
  static const vector<string> subsections{"do-not-hide-ads"};
  return subsections;
}

static const vector<string> &get_data_settings_subsections() {
  static const vector<string> subsections{
      "storage", "storage/edit", "storage/auto-remove", "storage/clear-cache", "storage/max-cache", "usage",
      "usage/mobile", "usage/wifi", "usage/reset", "usage/roaming", "auto-download/mobile",
      "auto-download/mobile/enable", "auto-download/mobile/usage", "auto-download/mobile/photos",
      "auto-download/mobile/stories", "auto-download/mobile/videos", "auto-download/mobile/files", "auto-download/wifi",
      "auto-download/wifi/enable", "auto-download/wifi/usage", "auto-download/wifi/photos",
      "auto-download/wifi/stories", "auto-download/wifi/videos", "auto-download/wifi/files", "auto-download/roaming",
      "auto-download/roaming/enable", "auto-download/roaming/usage", "auto-download/roaming/photos",
      "auto-download/roaming/stories", "auto-download/roaming/videos", "auto-download/roaming/files",
      "auto-download/reset", "save-to-photos/chats", "save-to-photos/chats/max-video-size",
      "save-to-photos/chats/add-exception", "save-to-photos/chats/delete-all", "save-to-photos/groups",
      "save-to-photos/groups/max-video-size", "save-to-photos/groups/add-exception", "save-to-photos/groups/delete-all",
      "save-to-photos/channels", "save-to-photos/channels/max-video-size", "save-to-photos/channels/add-exception",
      "save-to-photos/channels/delete-all", "less-data-calls", "open-links", "share-sheet",
      "share-sheet/suggested-chats", "share-sheet/suggest-by", "share-sheet/reset", "saved-edited-photos",
      "pause-music", "raise-to-listen", "raise-to-speak", "show-18-content", "proxy", "proxy/edit", "proxy/use-proxy",
      "proxy/add-proxy", "proxy/share-list",
      // no formatting
      "proxy/use-for-calls"};
  return subsections;
}

static const vector<string> &get_device_settings_subsections() {
  static const vector<string> subsections{"edit", "link-desktop", "terminate-sessions", "auto-terminate"};
  return subsections;
}

static const vector<string> &get_edit_profile_settings_subsections() {
  static const vector<string> subsections{"set-photo", "first-name",    "last-name", "bio",
                                          "birthday",  "change-number", "username",  "your-color",
                                          "channel",   "add-account",   "log-out"};
  return subsections;
}

static const vector<string> &get_edit_profile_other_settings_subsections() {
  static const vector<string> subsections{"emoji-status", "profile-color/profile", "profile-color/profile/add-icons",
                                          "profile-color/profile/use-gift", "profile-color/name",
                                          "profile-color/name/add-icons", "profile-color/name/use-gift",
                                          // no formatting
                                          "profile-photo/use-emoji"};
  return subsections;
}

static const vector<string> &get_folder_settings_subsections() {
  static const vector<string> subsections{"edit", "create", "add-recommended", "show-tags", "tab-view"};
  return subsections;
}

static const vector<string> &get_in_app_browser_settings_subsections() {
  static const vector<string> subsections{"enable-browser", "clear-cookies", "clear-cache", "history",
                                          "clear-history",  "never-open",    "clear-list",  "search"};
  return subsections;
}

static const vector<string> &get_language_settings_subsections() {
  static const vector<string> subsections{"show-button", "translate-chats", "do-not-translate"};
  return subsections;
}

static const vector<string> &get_my_stars_settings_subsections() {
  static const vector<string> subsections{"top-up", "stats", "gift", "earn"};
  return subsections;
}

static const vector<string> &get_notification_settings_subsections() {
  static const vector<string> subsections{
      "accounts", "private-chats", "private-chats/edit", "private-chats/show", "private-chats/preview",
      "private-chats/sound", "private-chats/add-exception", "private-chats/delete-exceptions",
      "private-chats/light-color", "private-chats/vibrate", "private-chats/priority", "groups", "groups/edit",
      "groups/show", "groups/preview", "groups/sound", "groups/add-exception", "groups/delete-exceptions",
      "groups/light-color", "groups/vibrate", "groups/priority", "channels", "channels/edit", "channels/show",
      "channels/preview", "channels/sound", "channels/add-exception", "channels/delete-exceptions",
      "channels/light-color", "channels/vibrate", "channels/priority", "stories", "stories/new", "stories/important",
      "stories/show-sender", "stories/sound", "stories/add-exception", "stories/delete-exceptions",
      "stories/light-color", "stories/vibrate", "stories/priority", "reactions", "reactions/messages",
      "reactions/stories", "reactions/show-sender", "reactions/sound", "reactions/light-color", "reactions/vibrate",
      "reactions/priority", "in-app-sounds", "in-app-vibrate", "in-app-preview", "in-chat-sounds", "in-app-popup",
      "lock-screen-names", "include-channels", "include-muted-chats", "count-unread-messages", "new-contacts",
      "pinned-messages", "reset",
      // no formatting
      "web"};
  return subsections;
}

static const vector<string> &get_power_saving_settings_subsections() {
  static const vector<string> subsections{"videos", "gifs", "stickers", "emoji", "effects", "preload", "background",
                                          "call-animations", "particles",
                                          // no formatting
                                          "transitions"};
  return subsections;
}

static const vector<string> &get_privacy_settings_subsections() {
  static const vector<string> subsections{
      "blocked", "blocked/edit", "blocked/block-user", "blocked/block-user/chats", "blocked/block-user/contacts",
      "active-websites", "active-websites/edit", "active-websites/disconnect-all", "passcode", "passcode/disable",
      "passcode/change", "passcode/auto-lock", "passcode/face-id", "passcode/fingerprint", "2sv", "2sv/change",
      "2sv/disable", "2sv/change-email", "passkey", "passkey/create", "auto-delete", "auto-delete/set-custom",
      "login-email", "phone-number", "phone-number/never", "phone-number/always", "last-seen", "last-seen/never",
      "last-seen/always", "last-seen/hide-read-time", "profile-photos", "profile-photos/never", "profile-photos/always",
      "profile-photos/set-public", "profile-photos/update-public", "profile-photos/remove-public", "bio", "bio/never",
      "bio/always", "gifts", "gifts/show-icon", "gifts/never", "gifts/always", "gifts/accepted-types", "birthday",
      "birthday/add", "birthday/never", "birthday/always", "saved-music", "saved-music/never", "saved-music/always",
      "forwards", "forwards/never", "forwards/always", "calls", "calls/never", "calls/always", "calls/p2p",
      "calls/p2p/never", "calls/p2p/always", "calls/ios-integration", "voice", "voice/never", "voice/always",
      "messages", "messages/set-price", "messages/exceptions", "invites", "invites/never", "invites/always",
      "self-destruct", "data-settings", "data-settings/sync-contacts", "data-settings/delete-synced",
      "data-settings/suggest-contacts", "data-settings/delete-cloud-drafts", "data-settings/clear-payment-info",
      "data-settings/link-previews", "data-settings/bot-settings", "data-settings/map-provider",
      // no formatting
      "archive-and-mute"};
  return subsections;
}

static const vector<string> &get_qr_code_settings_subsections() {
  static const vector<string> subsections{"share", "scan"};
  return subsections;
}

static const vector<string> &get_send_gift_settings_subsections() {
  static const vector<string> subsections{"self"};
  return subsections;
}

static const vector<string> &get_calls_sections() {
  static const vector<string> sections{"all", "missed", "edit", "show-tab", "start-call"};
  return sections;
}

static const vector<string> &get_contacts_sections() {
  static const vector<string> sections{"search", "sort", "new", "invite", "manage"};
  return sections;
}

static const vector<string> &get_my_profile_sections() {
  static const vector<string> sections{"posts", "posts/all-stories", "posts/add-album", "gifts", "archived-posts"};
  return sections;
}

static string get_url_query_hash(bool is_tg, const HttpUrlQuery &url_query) {
  const auto &path = url_query.path_;
  if (is_tg) {
    if (path.size() == 1 && path[0] == "join") {
      // join?invite=<hash>
      return url_query.get_arg("invite").str();
    }
  } else {
    if (path.size() >= 2 && path[0] == "joinchat") {
      // /joinchat/<hash>
      return path[1];
    }
    if (!path.empty() && path[0].size() >= 2 && (path[0][0] == ' ' || path[0][0] == '+')) {
      // /+<link>
      return path[0].substr(1);
    }
  }
  return string();
}

static string get_url_query_slug(bool is_tg, const HttpUrlQuery &url_query, Slice link_name) {
  const auto &path = url_query.path_;
  if (is_tg) {
    if (path.size() == 1 && path[0] == link_name) {
      // {link_name}?slug=<hash>
      return url_query.get_arg("slug").str();
    }
  } else {
    if (path.size() >= 2 && path[0] == link_name) {
      // /{link_name}/<hash>
      return path[1];
    }
  }
  return string();
}

static string get_url_query_draft_text(const HttpUrlQuery &url_query) {
  auto text_slice = url_query.get_arg("text");
  if (text_slice.empty()) {
    return string();
  }
  auto text = text_slice.str();
  if (!check_utf8(text)) {
    return string();
  }
  text = utf8_truncate(std::move(text), 4096u);
  if (text[0] == '@') {
    return ' ' + text;
  }
  return text;
}

static vector<string> get_referral_program_start_parameter_prefixes() {
  if (Scheduler::context() != nullptr) {
    return full_split(G()->get_option_string("starref_start_param_prefixes", "_tgr_"), ' ');
  }
  return vector<string>{"_tgr_"};
}

static AdministratorRights get_administrator_rights(Slice rights, bool for_channel) {
  bool can_manage_dialog = false;
  bool can_change_info = false;
  bool can_post_messages = false;
  bool can_edit_messages = false;
  bool can_delete_messages = false;
  bool can_invite_users = false;
  bool can_restrict_members = false;
  bool can_pin_messages = false;
  bool can_manage_topics = false;
  bool can_promote_members = false;
  bool can_manage_calls = false;
  bool can_post_stories = false;
  bool can_edit_stories = false;
  bool can_delete_stories = false;
  bool can_manage_direct_messages = false;
  bool is_anonymous = false;
  for (auto right : full_split(rights, ' ')) {
    if (right == "change_info") {
      can_change_info = true;
    } else if (right == "post_messages") {
      can_post_messages = true;
    } else if (right == "edit_messages") {
      can_edit_messages = true;
    } else if (right == "delete_messages") {
      can_delete_messages = true;
    } else if (right == "restrict_members") {
      can_restrict_members = true;
    } else if (right == "invite_users") {
      can_invite_users = true;
    } else if (right == "pin_messages") {
      can_pin_messages = true;
    } else if (right == "manage_topics") {
      can_manage_topics = true;
    } else if (right == "promote_members") {
      can_promote_members = true;
    } else if (right == "manage_video_chats") {
      can_manage_calls = true;
    } else if (right == "post_stories") {
      can_post_stories = true;
    } else if (right == "edit_stories") {
      can_edit_stories = true;
    } else if (right == "delete_stories") {
      can_delete_stories = true;
    } else if (right == "manage_direct_messages") {
      can_manage_direct_messages = true;
    } else if (right == "anonymous") {
      is_anonymous = true;
    } else if (right == "manage_chat") {
      can_manage_dialog = true;
    }
  }
  return AdministratorRights(is_anonymous, can_manage_dialog, can_change_info, can_post_messages, can_edit_messages,
                             can_delete_messages, can_invite_users, can_restrict_members, can_pin_messages,
                             can_manage_topics, can_promote_members, can_manage_calls, can_post_stories,
                             can_edit_stories, can_delete_stories, can_manage_direct_messages,
                             for_channel ? ChannelType::Broadcast : ChannelType::Megagroup);
}

static string get_admin_string(AdministratorRights rights) {
  vector<string> admin_rights;
  if (rights.can_change_info_and_settings()) {
    admin_rights.emplace_back("change_info");
  }
  if (rights.can_post_messages()) {
    admin_rights.emplace_back("post_messages");
  }
  if (rights.can_edit_messages()) {
    admin_rights.emplace_back("edit_messages");
  }
  if (rights.can_delete_messages()) {
    admin_rights.emplace_back("delete_messages");
  }
  if (rights.can_restrict_members()) {
    admin_rights.emplace_back("restrict_members");
  }
  if (rights.can_invite_users()) {
    admin_rights.emplace_back("invite_users");
  }
  if (rights.can_pin_messages()) {
    admin_rights.emplace_back("pin_messages");
  }
  if (rights.can_manage_topics()) {
    admin_rights.emplace_back("manage_topics");
  }
  if (rights.can_promote_members()) {
    admin_rights.emplace_back("promote_members");
  }
  if (rights.can_manage_calls()) {
    admin_rights.emplace_back("manage_video_chats");
  }
  if (rights.can_post_stories()) {
    admin_rights.emplace_back("post_stories");
  }
  if (rights.can_edit_stories()) {
    admin_rights.emplace_back("edit_stories");
  }
  if (rights.can_delete_stories()) {
    admin_rights.emplace_back("delete_stories");
  }
  if (rights.can_manage_direct_messages()) {
    admin_rights.emplace_back("manage_direct_messages");
  }
  if (rights.is_anonymous()) {
    admin_rights.emplace_back("anonymous");
  }
  if (rights.can_manage_dialog()) {
    admin_rights.emplace_back("manage_chat");
  }
  if (admin_rights.empty()) {
    return string();
  }
  return "&admin=" + implode(admin_rights, '+');
}

static td_api::object_ptr<td_api::targetChatTypes> get_target_chat_types(Slice chat_types) {
  bool allow_users = false;
  bool allow_bots = false;
  bool allow_groups = false;
  bool allow_channels = false;
  for (auto chat_type : full_split(chat_types, ' ')) {
    if (chat_type == "users") {
      allow_users = true;
    } else if (chat_type == "bots") {
      allow_bots = true;
    } else if (chat_type == "groups") {
      allow_groups = true;
    } else if (chat_type == "channels") {
      allow_channels = true;
    }
  }
  if (!allow_users && !allow_bots && !allow_groups && !allow_channels) {
    return nullptr;
  }
  return td_api::make_object<td_api::targetChatTypes>(allow_users, allow_bots, allow_groups, allow_channels);
}

static td_api::object_ptr<td_api::WebAppOpenMode> get_web_app_open_mode_object(const string &mode) {
  if (mode == "compact") {
    return td_api::make_object<td_api::webAppOpenModeCompact>();
  }
  if (mode == "fullscreen") {
    return td_api::make_object<td_api::webAppOpenModeFullScreen>();
  }
  return td_api::make_object<td_api::webAppOpenModeFullSize>();
}

class LinkManager::InternalLinkAttachMenuBot final : public InternalLink {
  td_api::object_ptr<td_api::targetChatTypes> allowed_chat_types_;
  unique_ptr<InternalLink> dialog_link_;
  string bot_username_;
  string url_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    td_api::object_ptr<td_api::TargetChat> target_chat;
    if (dialog_link_ != nullptr) {
      target_chat = td_api::make_object<td_api::targetChatInternalLink>(dialog_link_->get_internal_link_type_object());
    } else if (allowed_chat_types_ != nullptr) {
      target_chat = td_api::make_object<td_api::targetChatChosen>(td_api::make_object<td_api::targetChatTypes>(
          allowed_chat_types_->allow_user_chats_, allowed_chat_types_->allow_bot_chats_,
          allowed_chat_types_->allow_group_chats_, allowed_chat_types_->allow_channel_chats_));
    } else {
      target_chat = td_api::make_object<td_api::targetChatCurrent>();
    }
    return td_api::make_object<td_api::internalLinkTypeAttachmentMenuBot>(std::move(target_chat), bot_username_, url_);
  }

 public:
  InternalLinkAttachMenuBot(td_api::object_ptr<td_api::targetChatTypes> allowed_chat_types,
                            unique_ptr<InternalLink> dialog_link, string bot_username, Slice start_parameter)
      : allowed_chat_types_(std::move(allowed_chat_types))
      , dialog_link_(std::move(dialog_link))
      , bot_username_(std::move(bot_username)) {
    if (!start_parameter.empty() && is_valid_start_parameter(start_parameter)) {
      url_ = PSTRING() << "start://" << start_parameter;
    }
  }
};

class LinkManager::InternalLinkAuthenticationCode final : public InternalLink {
  string code_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeAuthenticationCode>(code_);
  }

 public:
  explicit InternalLinkAuthenticationCode(string &&code) : code_(std::move(code)) {
  }
};

class LinkManager::InternalLinkBackground final : public InternalLink {
  string background_name_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeBackground>(background_name_);
  }

 public:
  explicit InternalLinkBackground(string background_name) : background_name_(std::move(background_name)) {
  }
};

class LinkManager::InternalLinkBotAddToChannel final : public InternalLink {
  string bot_username_;
  AdministratorRights administrator_rights_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeBotAddToChannel>(
        bot_username_, administrator_rights_.get_chat_administrator_rights_object());
  }

 public:
  InternalLinkBotAddToChannel(string bot_username, AdministratorRights &&administrator_rights)
      : bot_username_(std::move(bot_username)), administrator_rights_(std::move(administrator_rights)) {
  }
};

class LinkManager::InternalLinkBotStart final : public InternalLink {
  string bot_username_;
  string start_parameter_;
  bool autostart_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    bool autostart = autostart_;
    if (Scheduler::context() != nullptr && !autostart) {
      if (bot_username_ == G()->get_option_string("premium_bot_username")) {
        autostart = true;
      } else {
        const Td *td = G()->td().get_actor_unsafe();
        auto dialog_id = td->dialog_manager_->get_resolved_dialog_by_username(bot_username_);
        if (dialog_id.get_type() == DialogType::User && td->messages_manager_->get_dialog_has_last_message(dialog_id) &&
            !td->messages_manager_->is_dialog_blocked(dialog_id)) {
          autostart = true;
        }
      }
    }
    return td_api::make_object<td_api::internalLinkTypeBotStart>(bot_username_, start_parameter_, autostart);
  }

 public:
  InternalLinkBotStart(string bot_username, string start_parameter, bool autostart)
      : bot_username_(std::move(bot_username)), start_parameter_(std::move(start_parameter)), autostart_(autostart) {
  }
};

class LinkManager::InternalLinkBotStartInGroup final : public InternalLink {
  string bot_username_;
  string start_parameter_;
  AdministratorRights administrator_rights_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeBotStartInGroup>(
        bot_username_, start_parameter_,
        administrator_rights_ == AdministratorRights() ? nullptr
                                                       : administrator_rights_.get_chat_administrator_rights_object());
  }

 public:
  InternalLinkBotStartInGroup(string bot_username, string start_parameter, AdministratorRights &&administrator_rights)
      : bot_username_(std::move(bot_username))
      , start_parameter_(std::move(start_parameter))
      , administrator_rights_(std::move(administrator_rights)) {
  }
};

class LinkManager::InternalLinkBusinessChat final : public InternalLink {
  string link_name_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeBusinessChat>(link_name_);
  }

 public:
  explicit InternalLinkBusinessChat(string link_name) : link_name_(std::move(link_name)) {
  }
};

class LinkManager::InternalLinkBuyStars final : public InternalLink {
  int64 star_count_;
  string purpose_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeStarPurchase>(star_count_, purpose_);
  }

 public:
  InternalLinkBuyStars(int64 star_count, string purpose)
      : star_count_(clamp(star_count, static_cast<int64>(1), static_cast<int64>(1000000000000)))
      , purpose_(std::move(purpose)) {
  }
};

class LinkManager::InternalLinkCalls final : public InternalLink {
  string section_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeCallsPage>(section_);
  }

 public:
  explicit InternalLinkCalls(string section) : section_(std::move(section)) {
  }
};

class LinkManager::InternalLinkConfirmPhone final : public InternalLink {
  string hash_;
  string phone_number_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypePhoneNumberConfirmation>(hash_, phone_number_);
  }

 public:
  InternalLinkConfirmPhone(string hash, string phone_number)
      : hash_(std::move(hash)), phone_number_(std::move(phone_number)) {
  }
};

class LinkManager::InternalLinkContacts final : public InternalLink {
  string section_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeContactsPage>(section_);
  }

 public:
  explicit InternalLinkContacts(string section) : section_(std::move(section)) {
  }
};

class LinkManager::InternalLinkDialogBoost final : public InternalLink {
  string url_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeChatBoost>(url_);
  }

 public:
  explicit InternalLinkDialogBoost(string url) : url_(std::move(url)) {
  }
};

class LinkManager::InternalLinkDialogFolderInvite final : public InternalLink {
  string url_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeChatFolderInvite>(url_);
  }

 public:
  explicit InternalLinkDialogFolderInvite(string url) : url_(std::move(url)) {
  }
};

class LinkManager::InternalLinkDialogInvite final : public InternalLink {
  string url_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeChatInvite>(url_);
  }

 public:
  explicit InternalLinkDialogInvite(string url) : url_(std::move(url)) {
  }
};

class LinkManager::InternalLinkDialogReferralProgram final : public InternalLink {
  string username_;
  string referral_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeChatAffiliateProgram>(username_, referral_);
  }

 public:
  InternalLinkDialogReferralProgram(string username, string referral)
      : username_(std::move(username)), referral_(std::move(referral)) {
  }
};

class LinkManager::InternalLinkDialogSelection final : public InternalLink {
  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeChatSelection>();
  }
};

class LinkManager::InternalLinkGame final : public InternalLink {
  string bot_username_;
  string game_short_name_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeGame>(bot_username_, game_short_name_);
  }

 public:
  InternalLinkGame(string bot_username, string game_short_name)
      : bot_username_(std::move(bot_username)), game_short_name_(std::move(game_short_name)) {
  }
};

class LinkManager::InternalLinkGiftAuction final : public InternalLink {
  string slug_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeGiftAuction>(slug_);
  }

 public:
  explicit InternalLinkGiftAuction(string slug) : slug_(std::move(slug)) {
  }
};

class LinkManager::InternalLinkGroupCall final : public InternalLink {
  string url_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeGroupCall>(url_);
  }

 public:
  explicit InternalLinkGroupCall(string url) : url_(std::move(url)) {
  }
};

class LinkManager::InternalLinkInstantView final : public InternalLink {
  string url_;
  string fallback_url_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeInstantView>(url_, fallback_url_);
  }

 public:
  InternalLinkInstantView(string url, string fallback_url)
      : url_(std::move(url)), fallback_url_(std::move(fallback_url)) {
  }
};

class LinkManager::InternalLinkInvoice final : public InternalLink {
  string invoice_name_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeInvoice>(invoice_name_);
  }

 public:
  explicit InternalLinkInvoice(string invoice_name) : invoice_name_(std::move(invoice_name)) {
  }
};

class LinkManager::InternalLinkLanguage final : public InternalLink {
  string language_pack_id_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeLanguagePack>(language_pack_id_);
  }

 public:
  explicit InternalLinkLanguage(string &&language_pack_id) : language_pack_id_(std::move(language_pack_id)) {
  }
};

class LinkManager::InternalLinkLiveStory final : public InternalLink {
  string dialog_username_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeLiveStory>(dialog_username_);
  }

 public:
  explicit InternalLinkLiveStory(string dialog_username) : dialog_username_(std::move(dialog_username)) {
  }
};

class LinkManager::InternalLinkMainWebApp final : public InternalLink {
  string bot_username_;
  string start_parameter_;
  string mode_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeMainWebApp>(bot_username_, start_parameter_,
                                                                   get_web_app_open_mode_object(mode_));
  }

 public:
  InternalLinkMainWebApp(string bot_username, string start_parameter, string mode)
      : bot_username_(std::move(bot_username)), start_parameter_(std::move(start_parameter)), mode_(std::move(mode)) {
  }
};

class LinkManager::InternalLinkMessage final : public InternalLink {
  string url_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeMessage>(url_);
  }

 public:
  explicit InternalLinkMessage(string url) : url_(std::move(url)) {
  }
};

class LinkManager::InternalLinkMessageDraft final : public InternalLink {
  FormattedText text_;
  bool contains_link_ = false;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeMessageDraft>(
        get_formatted_text_object(nullptr, text_, true, -1), contains_link_);
  }

 public:
  InternalLinkMessageDraft(FormattedText &&text, bool contains_link)
      : text_(std::move(text)), contains_link_(contains_link) {
  }
};

class LinkManager::InternalLinkMonoforum final : public InternalLink {
  string channel_username_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeDirectMessagesChat>(channel_username_);
  }

 public:
  explicit InternalLinkMonoforum(string channel_username) : channel_username_(std::move(channel_username)) {
  }
};

class LinkManager::InternalLinkMyProfile final : public InternalLink {
  string section_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeMyProfilePage>(section_);
  }

 public:
  explicit InternalLinkMyProfile(string section) : section_(std::move(section)) {
  }
};

class LinkManager::InternalLinkNewChannelChat final : public InternalLink {
  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeNewChannelChat>();
  }
};

class LinkManager::InternalLinkNewGroupChat final : public InternalLink {
  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeNewGroupChat>();
  }
};

class LinkManager::InternalLinkNewPrivateChat final : public InternalLink {
  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeNewPrivateChat>();
  }
};

class LinkManager::InternalLinkPassportDataRequest final : public InternalLink {
  UserId bot_user_id_;
  string scope_;
  string public_key_;
  string nonce_;
  string callback_url_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypePassportDataRequest>(bot_user_id_.get(), scope_, public_key_,
                                                                            nonce_, callback_url_);
  }

 public:
  InternalLinkPassportDataRequest(UserId bot_user_id, string scope, string public_key, string nonce,
                                  string callback_url)
      : bot_user_id_(bot_user_id)
      , scope_(std::move(scope))
      , public_key_(std::move(public_key))
      , nonce_(std::move(nonce))
      , callback_url_(std::move(callback_url)) {
  }
};

class LinkManager::InternalLinkPostStory final : public InternalLink {
  string content_type_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    auto content_type = [&]() -> td_api::object_ptr<td_api::StoryContentType> {
      if (content_type_ == "photo") {
        return td_api::make_object<td_api::storyContentTypePhoto>();
      }
      if (content_type_ == "video") {
        return td_api::make_object<td_api::storyContentTypeVideo>();
      }
      if (content_type_ == "live") {
        return td_api::make_object<td_api::storyContentTypeLive>();
      }
      if (!content_type_.empty()) {
        return td_api::make_object<td_api::storyContentTypeUnsupported>();
      }
      return nullptr;
    }();
    return td_api::make_object<td_api::internalLinkTypeNewStory>(std::move(content_type));
  }

 public:
  explicit InternalLinkPostStory(string content_type) : content_type_(std::move(content_type)) {
  }
};

class LinkManager::InternalLinkPremiumFeatures final : public InternalLink {
  string referrer_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypePremiumFeaturesPage>(referrer_);
  }

 public:
  explicit InternalLinkPremiumFeatures(string &&referrer) : referrer_(std::move(referrer)) {
  }
};

class LinkManager::InternalLinkPremiumGift final : public InternalLink {
  string referrer_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypePremiumGiftPurchase>(referrer_);
  }

 public:
  explicit InternalLinkPremiumGift(string &&referrer) : referrer_(std::move(referrer)) {
  }
};

class LinkManager::InternalLinkPremiumGiftCode final : public InternalLink {
  string code_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypePremiumGiftCode>(code_);
  }

 public:
  explicit InternalLinkPremiumGiftCode(string code) : code_(std::move(code)) {
  }
};

class LinkManager::InternalLinkProxy final : public InternalLink {
  string server_;
  int32 port_;
  td_api::object_ptr<td_api::ProxyType> type_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    auto type = type_.get();
    if (type == nullptr) {
      return td_api::make_object<td_api::internalLinkTypeProxy>();
    }
    auto proxy_type = [type]() -> td_api::object_ptr<td_api::ProxyType> {
      switch (type->get_id()) {
        case td_api::proxyTypeSocks5::ID: {
          auto type_socks = static_cast<const td_api::proxyTypeSocks5 *>(type);
          return td_api::make_object<td_api::proxyTypeSocks5>(type_socks->username_, type_socks->password_);
        }
        case td_api::proxyTypeMtproto::ID: {
          auto type_mtproto = static_cast<const td_api::proxyTypeMtproto *>(type);
          return td_api::make_object<td_api::proxyTypeMtproto>(type_mtproto->secret_);
        }
        default:
          UNREACHABLE();
          return nullptr;
      }
    }();
    return td_api::make_object<td_api::internalLinkTypeProxy>(
        td_api::make_object<td_api::proxy>(server_, port_, std::move(proxy_type)));
  }

 public:
  InternalLinkProxy(string server, int32 port, td_api::object_ptr<td_api::ProxyType> type)
      : server_(std::move(server)), port_(port), type_(std::move(type)) {
  }
};

class LinkManager::InternalLinkPublicDialog final : public InternalLink {
  string dialog_username_;
  string draft_text_;
  bool open_profile_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypePublicChat>(dialog_username_, draft_text_, open_profile_);
  }

 public:
  InternalLinkPublicDialog(string dialog_username, string draft_text, bool open_profile)
      : dialog_username_(std::move(dialog_username)), draft_text_(std::move(draft_text)), open_profile_(open_profile) {
  }
};

class LinkManager::InternalLinkQrCodeAuthentication final : public InternalLink {
  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeQrCodeAuthentication>();
  }
};

class LinkManager::InternalLinkRestorePurchases final : public InternalLink {
  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeRestorePurchases>();
  }
};

class LinkManager::InternalLinkSavedMessages final : public InternalLink {
  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeSavedMessages>();
  }
};

class LinkManager::InternalLinkSearch final : public InternalLink {
  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeSearch>();
  }
};

class LinkManager::InternalLinkSettings final : public InternalLink {
  vector<string> path_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    auto section = [&]() -> td_api::object_ptr<td_api::SettingsSection> {
      if (path_.empty()) {
        return nullptr;
      }
      string subsection;
      if (path_.size() >= 2u) {
        subsection = path_[1];
        for (size_t i = 2; i < path_.size(); i++) {
          subsection += '/';
          subsection += path_[i];
        }
      }
      if (path_[0] == "appearance") {
        if (td::contains(get_appearance_settings_subsections(), subsection)) {
          return td_api::make_object<td_api::settingsSectionAppearance>(subsection);
        }
        return td_api::make_object<td_api::settingsSectionAppearance>();
      }
      if (path_[0] == "ask-question") {
        return td_api::make_object<td_api::settingsSectionAskQuestion>();
      }
      if (path_[0] == "auto_delete") {
        return td_api::make_object<td_api::settingsSectionPrivacyAndSecurity>("auto-delete");
      }
      if (path_[0] == "business") {
        if (td::contains(get_business_settings_subsections(), subsection)) {
          return td_api::make_object<td_api::settingsSectionBusiness>(subsection);
        }
        return td_api::make_object<td_api::settingsSectionBusiness>();
      }
      if (path_[0] == "change_number") {
        return td_api::make_object<td_api::settingsSectionEditProfile>("change-number");
      }
      if (path_[0] == "chat" && path_.size() >= 2u && path_[1] == "browser") {
        if (path_.size() == 2u) {
          subsection = string();
        } else {
          subsection = path_[2];
        }
        if (td::contains(get_in_app_browser_settings_subsections(), subsection)) {
          return td_api::make_object<td_api::settingsSectionInAppBrowser>(subsection);
        }
        return td_api::make_object<td_api::settingsSectionInAppBrowser>();
      }
      if (path_[0] == "data") {
        if (td::contains(get_data_settings_subsections(), subsection)) {
          return td_api::make_object<td_api::settingsSectionDataAndStorage>(subsection);
        }
        return td_api::make_object<td_api::settingsSectionDataAndStorage>();
      }
      if (path_[0] == "devices") {
        if (td::contains(get_device_settings_subsections(), subsection)) {
          return td_api::make_object<td_api::settingsSectionDevices>(subsection);
        }
        return td_api::make_object<td_api::settingsSectionDevices>();
      }
      if (path_[0] == "edit") {
        if (td::contains(get_edit_profile_settings_subsections(), subsection)) {
          return td_api::make_object<td_api::settingsSectionEditProfile>(subsection);
        }
        return td_api::make_object<td_api::settingsSectionEditProfile>();
      }
      if (path_[0] == "edit_profile") {
        return td_api::make_object<td_api::settingsSectionEditProfile>();
      }
      if (path_[0] == "emoji-status" || path_[0] == "profile-color" || path_[0] == "profile-photo") {
        if (subsection.empty()) {
          subsection = path_[0];
        } else {
          subsection = PSTRING() << path_[0] << '/' << subsection;
        }
        if (td::contains(get_edit_profile_other_settings_subsections(), subsection)) {
          return td_api::make_object<td_api::settingsSectionEditProfile>(subsection);
        }
        if (path_[0] == "emoji-status") {
          return td_api::make_object<td_api::settingsSectionEditProfile>(path_[0]);
        }
        return td_api::make_object<td_api::settingsSectionEditProfile>();
      }
      if (path_[0] == "faq") {
        return td_api::make_object<td_api::settingsSectionFaq>();
      }
      if (path_[0] == "features") {
        return td_api::make_object<td_api::settingsSectionFeatures>();
      }
      if (path_[0] == "folders") {
        if (td::contains(get_folder_settings_subsections(), subsection)) {
          return td_api::make_object<td_api::settingsSectionChatFolders>(subsection);
        }
        return td_api::make_object<td_api::settingsSectionChatFolders>();
      }
      if (path_[0] == "language") {
        if (td::contains(get_language_settings_subsections(), subsection)) {
          return td_api::make_object<td_api::settingsSectionLanguage>(subsection);
        }
        return td_api::make_object<td_api::settingsSectionLanguage>();
      }
      if (path_[0] == "login_email") {
        return td_api::make_object<td_api::settingsSectionPrivacyAndSecurity>("login-email");
      }
      if (path_[0] == "notifications") {
        if (td::contains(get_notification_settings_subsections(), subsection)) {
          return td_api::make_object<td_api::settingsSectionNotifications>(subsection);
        }
        return td_api::make_object<td_api::settingsSectionNotifications>();
      }
      if (path_[0] == "power-saving") {
        if (td::contains(get_power_saving_settings_subsections(), subsection)) {
          return td_api::make_object<td_api::settingsSectionPowerSaving>(subsection);
        }
        return td_api::make_object<td_api::settingsSectionPowerSaving>();
      }
      if (path_[0] == "password") {
        return td_api::make_object<td_api::settingsSectionPrivacyAndSecurity>("2sv");
      }
      if (path_[0] == "phone_privacy") {
        return td_api::make_object<td_api::settingsSectionPrivacyAndSecurity>("phone-number");
      }
      if (path_[0] == "premium") {
        return td_api::make_object<td_api::settingsSectionPremium>();
      }
      if (path_[0] == "privacy") {
        if (td::contains(get_privacy_settings_subsections(), subsection)) {
          return td_api::make_object<td_api::settingsSectionPrivacyAndSecurity>(subsection);
        }
        return td_api::make_object<td_api::settingsSectionPrivacyAndSecurity>();
      }
      if (path_[0] == "privacy-policy") {
        return td_api::make_object<td_api::settingsSectionPrivacyPolicy>();
      }
      if (path_[0] == "qr-code") {
        if (td::contains(get_qr_code_settings_subsections(), subsection)) {
          return td_api::make_object<td_api::settingsSectionQrCode>(subsection);
        }
        return td_api::make_object<td_api::settingsSectionQrCode>();
      }
      if (path_[0] == "search") {
        return td_api::make_object<td_api::settingsSectionSearch>();
      }
      if (path_[0] == "send-gift") {
        if (td::contains(get_send_gift_settings_subsections(), subsection)) {
          return td_api::make_object<td_api::settingsSectionSendGift>(subsection);
        }
        return td_api::make_object<td_api::settingsSectionSendGift>();
      }
      if (path_[0] == "stars") {
        if (td::contains(get_my_stars_settings_subsections(), subsection)) {
          return td_api::make_object<td_api::settingsSectionMyStars>(subsection);
        }
        return td_api::make_object<td_api::settingsSectionMyStars>();
      }
      if (path_[0] == "themes") {
        return td_api::make_object<td_api::settingsSectionAppearance>();
      }
      if (path_[0] == "ton") {
        return td_api::make_object<td_api::settingsSectionMyToncoins>();
      }
      return nullptr;
    }();
    return td_api::make_object<td_api::internalLinkTypeSettings>(std::move(section));
  }

 public:
  explicit InternalLinkSettings(vector<string> &&path) : path_(std::move(path)) {
  }
};

class LinkManager::InternalLinkStickerSet final : public InternalLink {
  string sticker_set_name_;
  bool expect_custom_emoji_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeStickerSet>(sticker_set_name_, expect_custom_emoji_);
  }

 public:
  InternalLinkStickerSet(string &&sticker_set_name, bool expect_custom_emoji)
      : sticker_set_name_(std::move(sticker_set_name)), expect_custom_emoji_(expect_custom_emoji) {
  }
};

class LinkManager::InternalLinkStarGiftCollection final : public InternalLink {
  string gift_owner_username_;
  StarGiftCollectionId collection_id_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeGiftCollection>(gift_owner_username_, collection_id_.get());
  }

 public:
  InternalLinkStarGiftCollection(string gift_owner_username, StarGiftCollectionId collection_id)
      : gift_owner_username_(std::move(gift_owner_username)), collection_id_(collection_id) {
  }
};

class LinkManager::InternalLinkStory final : public InternalLink {
  string story_poster_username_;
  StoryId story_id_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeStory>(story_poster_username_, story_id_.get());
  }

 public:
  InternalLinkStory(string story_poster_username, StoryId story_id)
      : story_poster_username_(std::move(story_poster_username)), story_id_(story_id) {
  }
};

class LinkManager::InternalLinkStoryAlbum final : public InternalLink {
  string story_album_owner_username_;
  StoryAlbumId story_album_id_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeStoryAlbum>(story_album_owner_username_, story_album_id_.get());
  }

 public:
  InternalLinkStoryAlbum(string story_album_owner_username, StoryAlbumId story_album_id)
      : story_album_owner_username_(std::move(story_album_owner_username)), story_album_id_(story_album_id) {
  }
};

class LinkManager::InternalLinkTheme final : public InternalLink {
  string theme_name_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeTheme>(theme_name_);
  }

 public:
  explicit InternalLinkTheme(string &&theme_name) : theme_name_(std::move(theme_name)) {
  }
};

class LinkManager::InternalLinkUnknownDeepLink final : public InternalLink {
  string link_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeUnknownDeepLink>(link_);
  }

 public:
  explicit InternalLinkUnknownDeepLink(string link) : link_(std::move(link)) {
  }
};

class LinkManager::InternalLinkUserPhoneNumber final : public InternalLink {
  string phone_number_;
  string draft_text_;
  bool open_profile_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeUserPhoneNumber>(phone_number_, draft_text_, open_profile_);
  }

 public:
  InternalLinkUserPhoneNumber(Slice phone_number, string draft_text, bool open_profile)
      : phone_number_(PSTRING() << '+' << phone_number)
      , draft_text_(std::move(draft_text))
      , open_profile_(open_profile) {
  }
};

class LinkManager::InternalLinkUpgradedGift final : public InternalLink {
  string name_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeUpgradedGift>(name_);
  }

 public:
  explicit InternalLinkUpgradedGift(string &&name) : name_(std::move(name)) {
  }
};

class LinkManager::InternalLinkUserToken final : public InternalLink {
  string token_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeUserToken>(token_);
  }

 public:
  explicit InternalLinkUserToken(string &&token) : token_(std::move(token)) {
  }
};

class LinkManager::InternalLinkVideoChat final : public InternalLink {
  string dialog_username_;
  string invite_hash_;
  bool is_live_stream_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeVideoChat>(dialog_username_, invite_hash_, is_live_stream_);
  }

 public:
  InternalLinkVideoChat(string dialog_username, string invite_hash, bool is_live_stream)
      : dialog_username_(std::move(dialog_username))
      , invite_hash_(std::move(invite_hash))
      , is_live_stream_(is_live_stream) {
  }
};

class LinkManager::InternalLinkWebApp final : public InternalLink {
  string bot_username_;
  string web_app_short_name_;
  string start_parameter_;
  string mode_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeWebApp>(bot_username_, web_app_short_name_, start_parameter_,
                                                               get_web_app_open_mode_object(mode_));
  }

 public:
  InternalLinkWebApp(string bot_username, string web_app_short_name, string start_parameter, string mode)
      : bot_username_(std::move(bot_username))
      , web_app_short_name_(std::move(web_app_short_name))
      , start_parameter_(std::move(start_parameter))
      , mode_(std::move(mode)) {
  }
};

class GetRecentMeUrlsQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::tMeUrls>> promise_;

 public:
  explicit GetRecentMeUrlsQuery(Promise<td_api::object_ptr<td_api::tMeUrls>> &&promise) : promise_(std::move(promise)) {
  }

  void send(const string &referrer) {
    send_query(G()->net_query_creator().create(telegram_api::help_getRecentMeUrls(referrer)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::help_getRecentMeUrls>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto urls_full = result_ptr.move_as_ok();
    td_->user_manager_->on_get_users(std::move(urls_full->users_), "GetRecentMeUrlsQuery");
    td_->chat_manager_->on_get_chats(std::move(urls_full->chats_), "GetRecentMeUrlsQuery");

    auto urls = std::move(urls_full->urls_);
    auto results = td_api::make_object<td_api::tMeUrls>();
    results->urls_.reserve(urls.size());
    for (auto &url_ptr : urls) {
      CHECK(url_ptr != nullptr);
      td_api::object_ptr<td_api::tMeUrl> result = td_api::make_object<td_api::tMeUrl>();
      switch (url_ptr->get_id()) {
        case telegram_api::recentMeUrlUser::ID: {
          auto url = telegram_api::move_object_as<telegram_api::recentMeUrlUser>(url_ptr);
          result->url_ = std::move(url->url_);
          UserId user_id(url->user_id_);
          if (!user_id.is_valid()) {
            LOG(ERROR) << "Receive invalid " << user_id;
            result = nullptr;
            break;
          }
          result->type_ = td_api::make_object<td_api::tMeUrlTypeUser>(
              td_->user_manager_->get_user_id_object(user_id, "tMeUrlTypeUser"));
          break;
        }
        case telegram_api::recentMeUrlChat::ID: {
          auto url = telegram_api::move_object_as<telegram_api::recentMeUrlChat>(url_ptr);
          result->url_ = std::move(url->url_);
          ChannelId channel_id(url->chat_id_);
          if (!channel_id.is_valid()) {
            LOG(ERROR) << "Receive invalid " << channel_id;
            result = nullptr;
            break;
          }
          result->type_ = td_api::make_object<td_api::tMeUrlTypeSupergroup>(
              td_->chat_manager_->get_supergroup_id_object(channel_id, "tMeUrlTypeSupergroup"));
          break;
        }
        case telegram_api::recentMeUrlChatInvite::ID: {
          auto url = telegram_api::move_object_as<telegram_api::recentMeUrlChatInvite>(url_ptr);
          result->url_ = std::move(url->url_);
          td_->dialog_invite_link_manager_->on_get_dialog_invite_link_info(result->url_, std::move(url->chat_invite_),
                                                                           Promise<Unit>());
          auto info_object = td_->dialog_invite_link_manager_->get_chat_invite_link_info_object(result->url_);
          if (info_object == nullptr) {
            result = nullptr;
            break;
          }
          result->type_ = td_api::make_object<td_api::tMeUrlTypeChatInvite>(std::move(info_object));
          break;
        }
        case telegram_api::recentMeUrlStickerSet::ID: {
          auto url = telegram_api::move_object_as<telegram_api::recentMeUrlStickerSet>(url_ptr);
          result->url_ = std::move(url->url_);
          auto sticker_set_id =
              td_->stickers_manager_->on_get_sticker_set_covered(std::move(url->set_), false, "recentMeUrlStickerSet");
          if (!sticker_set_id.is_valid()) {
            LOG(ERROR) << "Receive invalid sticker set";
            result = nullptr;
            break;
          }
          result->type_ = td_api::make_object<td_api::tMeUrlTypeStickerSet>(sticker_set_id.get());
          break;
        }
        case telegram_api::recentMeUrlUnknown::ID:
          // skip
          result = nullptr;
          break;
        default:
          UNREACHABLE();
      }
      if (result != nullptr) {
        results->urls_.push_back(std::move(result));
      }
    }
    promise_.set_value(std::move(results));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetDeepLinkInfoQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::deepLinkInfo>> promise_;

 public:
  explicit GetDeepLinkInfoQuery(Promise<td_api::object_ptr<td_api::deepLinkInfo>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(Slice link) {
    send_query(G()->net_query_creator().create_unauth(telegram_api::help_getDeepLinkInfo(link.str())));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::help_getDeepLinkInfo>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    switch (result->get_id()) {
      case telegram_api::help_deepLinkInfoEmpty::ID:
        return promise_.set_value(nullptr);
      case telegram_api::help_deepLinkInfo::ID: {
        auto info = telegram_api::move_object_as<telegram_api::help_deepLinkInfo>(result);
        auto text = get_formatted_text(nullptr, std::move(info->message_), std::move(info->entities_), true, true,
                                       "GetDeepLinkInfoQuery");
        return promise_.set_value(td_api::make_object<td_api::deepLinkInfo>(
            get_formatted_text_object(td_->user_manager_.get(), text, true, -1), info->update_app_));
      }
      default:
        UNREACHABLE();
    }
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class RequestUrlAuthQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::LoginUrlInfo>> promise_;
  string url_;
  DialogId dialog_id_;

 public:
  explicit RequestUrlAuthQuery(Promise<td_api::object_ptr<td_api::LoginUrlInfo>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(string url, MessageFullId message_full_id, int32 button_id) {
    url_ = std::move(url);
    int32 flags = 0;
    tl_object_ptr<telegram_api::InputPeer> input_peer;
    if (message_full_id.get_dialog_id().is_valid()) {
      dialog_id_ = message_full_id.get_dialog_id();
      input_peer = td_->dialog_manager_->get_input_peer(dialog_id_, AccessRights::Read);
      CHECK(input_peer != nullptr);
      flags |= telegram_api::messages_requestUrlAuth::PEER_MASK;
    } else {
      flags |= telegram_api::messages_requestUrlAuth::URL_MASK;
    }
    send_query(G()->net_query_creator().create(telegram_api::messages_requestUrlAuth(
        flags, std::move(input_peer), message_full_id.get_message_id().get_server_message_id().get(), button_id,
        url_)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_requestUrlAuth>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for RequestUrlAuthQuery: " << to_string(result);
    switch (result->get_id()) {
      case telegram_api::urlAuthResultRequest::ID: {
        auto request = telegram_api::move_object_as<telegram_api::urlAuthResultRequest>(result);
        UserId bot_user_id = UserManager::get_user_id(request->bot_);
        if (!bot_user_id.is_valid()) {
          return on_error(Status::Error(500, "Receive invalid bot_user_id"));
        }
        td_->user_manager_->on_get_user(std::move(request->bot_), "RequestUrlAuthQuery");
        promise_.set_value(td_api::make_object<td_api::loginUrlInfoRequestConfirmation>(
            url_, request->domain_, td_->user_manager_->get_user_id_object(bot_user_id, "RequestUrlAuthQuery"),
            request->request_write_access_, request->request_phone_number_, request->browser_, request->platform_,
            request->ip_, request->region_));
        break;
      }
      case telegram_api::urlAuthResultAccepted::ID: {
        auto accepted = telegram_api::move_object_as<telegram_api::urlAuthResultAccepted>(result);
        promise_.set_value(td_api::make_object<td_api::loginUrlInfoOpen>(accepted->url_, true));
        break;
      }
      case telegram_api::urlAuthResultDefault::ID:
        promise_.set_value(td_api::make_object<td_api::loginUrlInfoOpen>(url_, false));
        break;
    }
  }

  void on_error(Status status) final {
    if (!dialog_id_.is_valid() ||
        !td_->dialog_manager_->on_get_dialog_error(dialog_id_, status, "RequestUrlAuthQuery")) {
      LOG(INFO) << "Receive error for RequestUrlAuthQuery: " << status;
    }
    if (status.message() == "URL_EXPIRED" || status.message() == "URL_INVALID") {
      return promise_.set_error(std::move(status));
    }
    promise_.set_value(td_api::make_object<td_api::loginUrlInfoOpen>(url_, false));
  }
};

class AcceptUrlAuthQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::httpUrl>> promise_;
  string url_;
  DialogId dialog_id_;

 public:
  explicit AcceptUrlAuthQuery(Promise<td_api::object_ptr<td_api::httpUrl>> &&promise) : promise_(std::move(promise)) {
  }

  void send(string url, MessageFullId message_full_id, int32 button_id, bool allow_write_access,
            bool allow_phone_number_access) {
    url_ = std::move(url);
    int32 flags = 0;
    telegram_api::object_ptr<telegram_api::InputPeer> input_peer;
    if (message_full_id.get_dialog_id().is_valid()) {
      dialog_id_ = message_full_id.get_dialog_id();
      input_peer = td_->dialog_manager_->get_input_peer(dialog_id_, AccessRights::Read);
      CHECK(input_peer != nullptr);
      flags |= telegram_api::messages_acceptUrlAuth::PEER_MASK;
    } else {
      flags |= telegram_api::messages_acceptUrlAuth::URL_MASK;
    }
    send_query(G()->net_query_creator().create(telegram_api::messages_acceptUrlAuth(
        flags, allow_write_access, allow_phone_number_access, std::move(input_peer),
        message_full_id.get_message_id().get_server_message_id().get(), button_id, url_)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_acceptUrlAuth>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive " << to_string(result);
    switch (result->get_id()) {
      case telegram_api::urlAuthResultRequest::ID:
        LOG(ERROR) << "Receive unexpected " << to_string(result);
        return on_error(Status::Error(500, "Receive unexpected urlAuthResultRequest"));
      case telegram_api::urlAuthResultAccepted::ID: {
        auto accepted = telegram_api::move_object_as<telegram_api::urlAuthResultAccepted>(result);
        promise_.set_value(td_api::make_object<td_api::httpUrl>(accepted->url_));
        break;
      }
      case telegram_api::urlAuthResultDefault::ID:
        promise_.set_value(td_api::make_object<td_api::httpUrl>(url_));
        break;
    }
  }

  void on_error(Status status) final {
    if (!dialog_id_.is_valid() ||
        !td_->dialog_manager_->on_get_dialog_error(dialog_id_, status, "AcceptUrlAuthQuery")) {
      LOG(INFO) << "Receive error for AcceptUrlAuthQuery: " << status;
    }
    promise_.set_error(std::move(status));
  }
};

LinkManager::LinkManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

LinkManager::~LinkManager() = default;

void LinkManager::start_up() {
  autologin_update_time_ = Time::now() - 365 * 86400;

  autologin_domains_ = full_split(G()->td_db()->get_binlog_pmc()->get("autologin_domains"), '\xFF');

  url_auth_domains_ = full_split(G()->td_db()->get_binlog_pmc()->get("url_auth_domains"), '\xFF');

  whitelisted_domains_ = full_split(G()->td_db()->get_binlog_pmc()->get("whitelisted_domains"), '\xFF');
}

void LinkManager::tear_down() {
  parent_.reset();
}

static bool tolower_begins_with(Slice str, Slice prefix) {
  if (prefix.size() > str.size()) {
    return false;
  }
  for (size_t i = 0; i < prefix.size(); i++) {
    if (to_lower(str[i]) != prefix[i]) {
      return false;
    }
  }
  return true;
}

Result<string> LinkManager::check_link(CSlice link, bool http_only, bool https_only) {
  auto result = check_link_impl(link, http_only, https_only);
  if (result.is_ok()) {
    return result;
  }
  auto error = result.move_as_error();
  if (check_utf8(link)) {
    return Status::Error(400, PSLICE() << "URL '" << link << "' is invalid: " << error.message());
  } else {
    return Status::Error(400, PSLICE() << "URL is invalid: " << error.message());
  }
}

string LinkManager::get_checked_link(Slice link, bool http_only, bool https_only) {
  auto result = check_link_impl(link, http_only, https_only);
  if (result.is_ok()) {
    return result.move_as_ok();
  }
  return string();
}

Result<string> LinkManager::check_link_impl(Slice link, bool http_only, bool https_only) {
  bool is_tg = false;
  bool is_ton = false;
  bool is_tonsite = false;
  if (tolower_begins_with(link, "tg:")) {
    link.remove_prefix(3);
    is_tg = true;
  } else if (tolower_begins_with(link, "ton:")) {
    link.remove_prefix(4);
    is_ton = true;
  } else if (tolower_begins_with(link, "tonsite:")) {
    link.remove_prefix(8);
    is_tonsite = true;
  }
  if ((is_tg || is_ton || is_tonsite) && begins_with(link, "//")) {
    link.remove_prefix(2);
  }
  TRY_RESULT(http_url, parse_url(link));
  if (https_only && (http_url.protocol_ != HttpUrl::Protocol::Https || is_tg || is_ton || is_tonsite)) {
    return Status::Error("Only HTTPS links are allowed");
  }
  if (is_tg || is_ton || is_tonsite) {
    if (http_only) {
      return Status::Error("Only HTTP links are allowed");
    }
    if (tolower_begins_with(link, "http://") || http_url.protocol_ == HttpUrl::Protocol::Https ||
        !http_url.userinfo_.empty() || http_url.specified_port_ != 0 || http_url.is_ipv6_) {
      return Status::Error(is_tg ? Slice("Wrong tg URL") : Slice("Wrong ton URL"));
    }

    Slice query(http_url.query_);
    CHECK(query[0] == '/');
    if (query.size() > 1 && query[1] == '?') {
      query.remove_prefix(1);
    }
    for (auto c : http_url.host_) {
      if (!is_alnum(c) && c != '-' && c != '_' && !(is_tonsite && c == '.')) {
        return Status::Error("Unallowed characters in URL host");
      }
    }
    return PSTRING() << (is_tg ? "tg" : (is_tonsite ? "tonsite" : "ton")) << "://" << http_url.host_ << query;
  }

  if (http_url.host_.find('.') == string::npos && !http_url.is_ipv6_) {
    return Status::Error("Wrong HTTP URL");
  }
  return http_url.get_url();
}

LinkManager::LinkInfo LinkManager::get_link_info(Slice link) {
  LinkInfo result;
  if (link.empty()) {
    return result;
  }
  link.truncate(link.find('#'));

  bool is_tg = false;
  if (tolower_begins_with(link, "tg:")) {
    link.remove_prefix(3);
    if (begins_with(link, "//")) {
      link.remove_prefix(2);
    }
    is_tg = true;
  }

  auto r_http_url = parse_url(link);
  if (r_http_url.is_error()) {
    return result;
  }
  auto http_url = r_http_url.move_as_ok();

  if (!http_url.userinfo_.empty() || http_url.is_ipv6_) {
    return result;
  }

  if (is_tg) {
    if (tolower_begins_with(link, "http://") || http_url.protocol_ == HttpUrl::Protocol::Https ||
        http_url.specified_port_ != 0) {
      return result;
    }

    result.type_ = LinkType::Tg;
    result.query_ = link.str();
    return result;
  } else {
    if (http_url.port_ != 80 && http_url.port_ != 443) {
      return result;
    }

    auto host = url_decode(http_url.host_, false);
    to_lower_inplace(host);
    if (ends_with(host, ".t.me") && host.size() >= 9 && host.find('.') == host.size() - 5) {
      Slice subdomain(&host[0], host.size() - 5);
      static const FlatHashSet<Slice, SliceHash> disallowed_subdomains(
          {"addemoji",     "addlist",     "addstickers", "addtheme", "auction",  "auth",  "boost", "call",
           "confirmphone", "contact",     "giftcode",    "invoice",  "joinchat", "login", "m",     "nft",
           "proxy",        "setlanguage", "share",       "socks",    "web",      "a",     "k",     "z"});
      if (is_valid_username(subdomain) && disallowed_subdomains.count(subdomain) == 0) {
        result.type_ = LinkType::TMe;
        result.query_ = PSTRING() << '/' << subdomain << http_url.query_;
        return result;
      }
    }
    if (begins_with(host, "www.")) {
      host = host.substr(4);
    }

    string cur_t_me_url;
    vector<Slice> t_me_urls{Slice("t.me"), Slice("telegram.me"), Slice("telegram.dog")};
#if TD_EMSCRIPTEN
    t_me_urls.push_back(Slice("web.t.me"));
    t_me_urls.push_back(Slice("a.t.me"));
    t_me_urls.push_back(Slice("k.t.me"));
    t_me_urls.push_back(Slice("z.t.me"));
#endif
    if (Scheduler::context() != nullptr) {  // for tests only
      cur_t_me_url = G()->get_option_string("t_me_url");
      if (tolower_begins_with(cur_t_me_url, "http://") || tolower_begins_with(cur_t_me_url, "https://")) {
        Slice t_me_url = cur_t_me_url;
        t_me_url = t_me_url.substr(t_me_url[4] == 's' ? 8 : 7);
        if (!td::contains(t_me_urls, t_me_url)) {
          t_me_urls.push_back(t_me_url);
        }
      }
    }

    for (auto t_me_url : t_me_urls) {
      if (host == t_me_url) {
        result.type_ = LinkType::TMe;

        Slice query = http_url.query_;
        while (true) {
          if (begins_with(query, "/s/")) {
            query.remove_prefix(2);
            continue;
          }
          if (begins_with(query, "/%73/")) {
            query.remove_prefix(4);
            continue;
          }
          break;
        }
        result.query_ = query.str();
        return result;
      }
    }

    if (http_url.query_.size() > 1) {
      for (auto telegraph_url : {Slice("telegra.ph"), Slice("te.legra.ph"), Slice("graph.org")}) {
        if (host == telegraph_url) {
          result.type_ = LinkType::Telegraph;
          result.query_ = std::move(http_url.query_);
          return result;
        }
      }
    }
  }
  return result;
}

bool LinkManager::is_internal_link(Slice link) {
  auto info = get_link_info(link);
  return info.type_ != LinkType::External;
}

unique_ptr<LinkManager::InternalLink> LinkManager::parse_internal_link(Slice link, bool is_trusted) {
  auto info = get_link_info(link);
  switch (info.type_) {
    case LinkType::External:
      return nullptr;
    case LinkType::Tg:
      return parse_tg_link_query(info.query_, is_trusted);
    case LinkType::TMe:
      return parse_t_me_link_query(info.query_, is_trusted);
    case LinkType::Telegraph:
      return td::make_unique<InternalLinkInstantView>(PSTRING() << "https://telegra.ph" << info.query_, link.str());
    default:
      UNREACHABLE();
      return nullptr;
  }
}

namespace {
struct CopyArg {
  Slice name_;
  const HttpUrlQuery *url_query_;
  bool *is_first_;

  CopyArg(Slice name, const HttpUrlQuery *url_query, bool *is_first)
      : name_(name), url_query_(url_query), is_first_(is_first) {
  }
};

StringBuilder &operator<<(StringBuilder &string_builder, const CopyArg &copy_arg) {
  auto arg = copy_arg.url_query_->get_arg(copy_arg.name_);
  if (arg.empty()) {
    for (const auto &query_arg : copy_arg.url_query_->args_) {
      if (query_arg.first == copy_arg.name_) {
        char c = *copy_arg.is_first_ ? '?' : '&';
        *copy_arg.is_first_ = false;
        return string_builder << c << copy_arg.name_;
      }
    }
    return string_builder;
  }
  char c = *copy_arg.is_first_ ? '?' : '&';
  *copy_arg.is_first_ = false;
  return string_builder << c << copy_arg.name_ << '=' << url_encode(arg);
}
}  // namespace

unique_ptr<LinkManager::InternalLink> LinkManager::parse_tg_link_query(Slice query, bool is_trusted) {
  const auto url_query = parse_url_query(query);
  const auto &path = url_query.path_;

  bool is_first_arg = true;
  auto copy_arg = [&](Slice name) {
    return CopyArg(name, &url_query, &is_first_arg);
  };
  auto pass_arg = [&](Slice name) {
    return url_encode(url_query.get_arg(name));
  };
  auto get_arg = [&](Slice name) {
    return url_query.get_arg(name).str();
  };
  auto has_arg = [&](Slice name) {
    return !url_query.get_arg(name).empty();
  };

  if (path.size() == 1 && path[0] == "resolve") {
    auto username = get_arg("domain");
    if (is_valid_username(username)) {
      if (has_arg("post")) {
        // resolve?domain=<username>&post=12345&single&thread=<thread_id>&comment=<message_id>&t=<media_timestamp>
        return td::make_unique<InternalLinkMessage>(
            PSTRING() << "tg://resolve" << copy_arg("domain") << copy_arg("post") << copy_arg("single")
                      << copy_arg("thread") << copy_arg("comment") << copy_arg("t"));
      }
      if (username == "oauth" && has_arg("startapp")) {
        return nullptr;
      }
      for (auto &arg : url_query.args_) {
        if ((arg.first == "voicechat" || arg.first == "videochat" || arg.first == "livestream") &&
            is_valid_video_chat_invite_hash(arg.second)) {
          // resolve?domain=<username>&videochat
          // resolve?domain=<username>&videochat=<invite_hash>
          if (Scheduler::context() != nullptr) {
            send_closure(G()->dialog_manager(), &DialogManager::reload_video_chat_on_search, username);
          }
          return td::make_unique<InternalLinkVideoChat>(std::move(username), arg.second, arg.first == "livestream");
        }
        if (arg.first == "ref" && is_valid_start_parameter(arg.second) && !arg.second.empty()) {
          // resolve?domain=<bot_username>&ref=<referrer>
          return td::make_unique<InternalLinkDialogReferralProgram>(std::move(username), arg.second);
        }
        if (arg.first == "start" && is_valid_start_parameter(arg.second)) {
          auto prefixes = get_referral_program_start_parameter_prefixes();
          for (Slice prefix : prefixes) {
            if (begins_with(arg.second, prefix) && arg.second.size() > prefix.size()) {
              // resolve?domain=<bot_username>&start=_tgr_<referrer>
              return td::make_unique<InternalLinkDialogReferralProgram>(std::move(username),
                                                                        arg.second.substr(prefix.size()));
            }
          }
          // resolve?domain=<bot_username>&start=<parameter>
          return td::make_unique<InternalLinkBotStart>(std::move(username), arg.second, is_trusted);
        }
        if (arg.first == "startgroup" && is_valid_start_parameter(arg.second)) {
          // resolve?domain=<bot_username>&startgroup=<parameter>
          // resolve?domain=<bot_username>&startgroup=>parameter>&admin=change_info+delete_messages+restrict_members
          // resolve?domain=<bot_username>&startgroup&admin=change_info+delete_messages+restrict_members
          auto administrator_rights = get_administrator_rights(url_query.get_arg("admin"), false);
          return td::make_unique<InternalLinkBotStartInGroup>(std::move(username), arg.second,
                                                              std::move(administrator_rights));
        }
        if (arg.first == "startchannel") {
          // resolve?domain=<bot_username>&startchannel&admin=change_info+post_messages+promote_members
          auto administrator_rights = get_administrator_rights(url_query.get_arg("admin"), true);
          if (administrator_rights != AdministratorRights()) {
            return td::make_unique<InternalLinkBotAddToChannel>(std::move(username), std::move(administrator_rights));
          }
        }
        if (arg.first == "game" && is_valid_game_name(arg.second)) {
          // resolve?domain=<bot_username>&game=<short_name>
          return td::make_unique<InternalLinkGame>(std::move(username), arg.second);
        }
        if (arg.first == "appname" && is_valid_web_app_name(arg.second)) {
          // resolve?domain=<bot_username>&appname=<app_name>
          // resolve?domain=<bot_username>&appname=<app_name>&startapp=<start_parameter>&mode=compact
          return td::make_unique<InternalLinkWebApp>(std::move(username), arg.second, get_arg("startapp"),
                                                     get_arg("mode"));
        }
        if (arg.first == "story" && is_valid_story_id(arg.second)) {
          // resolve?domain=<username>&story=<story_id>
          return td::make_unique<InternalLinkStory>(std::move(username), StoryId(to_integer<int32>(arg.second)));
        }
        if (arg.first == "story" && arg.second == "live") {
          // resolve?domain=<username>&story=live
          return td::make_unique<InternalLinkLiveStory>(std::move(username));
        }
        if (arg.first == "startapp" && is_valid_start_parameter(arg.second) && !url_query.has_arg("appname")) {
          // resolve?domain=<bot_username>&startapp=
          // resolve?domain=<bot_username>&startapp=<start_parameter>&mode=compact
          return td::make_unique<InternalLinkMainWebApp>(std::move(username), arg.second, get_arg("mode"));
        }
        if (arg.first == "attach" && is_valid_username(arg.second)) {
          // resolve?domain=<username>&attach=<bot_username>
          // resolve?domain=<username>&attach=<bot_username>&startattach=<start_parameter>
          return td::make_unique<InternalLinkAttachMenuBot>(
              nullptr, td::make_unique<InternalLinkPublicDialog>(std::move(username), string(), false), arg.second,
              url_query.get_arg("startattach"));
        }
        if (arg.first == "startattach" && !has_arg("attach")) {
          // resolve?domain=<bot_username>&startattach&choose=users+bots+groups+channels
          // resolve?domain=<bot_username>&startattach=<start_parameter>&choose=users+bots+groups+channels
          return td::make_unique<InternalLinkAttachMenuBot>(get_target_chat_types(url_query.get_arg("choose")), nullptr,
                                                            std::move(username), arg.second);
        }
        if (arg.first == "direct") {
          // resolve?domain=<username>&direct
          return td::make_unique<InternalLinkMonoforum>(std::move(username));
        }
        if (arg.first == "collection" && is_valid_star_gift_collection_id(arg.second)) {
          // resolve?domain=<username>&collection=<collection_id>
          return td::make_unique<InternalLinkStarGiftCollection>(std::move(username),
                                                                 StarGiftCollectionId(to_integer<int32>(arg.second)));
        }
        if (arg.first == "album" && is_valid_story_album_id(arg.second)) {
          // resolve?domain=<username>&album=<story_album_id>
          return td::make_unique<InternalLinkStoryAlbum>(std::move(username),
                                                         StoryAlbumId(to_integer<int32>(arg.second)));
        }
      }
      if (username == "telegrampassport") {
        // resolve?domain=telegrampassport&bot_id=...&scope=...&public_key=...&nonce=...&callback_url=...
        auto passport_link = get_internal_link_passport(query, url_query.args_, false);
        if (passport_link != nullptr) {
          return passport_link;
        }
      }
      // resolve?domain=<username>
      return td::make_unique<InternalLinkPublicDialog>(std::move(username), get_url_query_draft_text(url_query),
                                                       url_query.has_arg("profile"));
    } else {
      string phone_number_str = get_arg("phone");
      auto phone_number = phone_number_str[0] == ' ' ? Slice(phone_number_str).substr(1) : Slice(phone_number_str);
      if (is_valid_phone_number(phone_number)) {
        auto attach = url_query.get_arg("attach");
        if (is_valid_username(attach)) {
          // resolve?phone=<phone_number>&attach=<bot_username>
          // resolve?phone=<phone_number>&attach=<bot_username>&startattach=<start_parameter>
          return td::make_unique<InternalLinkAttachMenuBot>(
              nullptr, td::make_unique<InternalLinkUserPhoneNumber>(phone_number, string(), false), attach.str(),
              url_query.get_arg("startattach"));
        }
        // resolve?phone=12345
        return td::make_unique<InternalLinkUserPhoneNumber>(phone_number, get_url_query_draft_text(url_query),
                                                            url_query.has_arg("profile"));
      }
    }
  } else if (path.size() == 1 && path[0] == "nft") {
    // nft?slug=<slug>
    auto name = get_arg("slug");
    if (is_valid_upgraded_gift_name(name)) {
      return td::make_unique<InternalLinkUpgradedGift>(std::move(name));
    }
  } else if (path.size() == 1 && path[0] == "contact") {
    // contact?token=<token>
    auto token = get_arg("token");
    if (is_valid_user_token(token)) {
      return td::make_unique<InternalLinkUserToken>(std::move(token));
    }
  } else if (path.size() >= 1u && path[0] == "contacts") {
    // contacts[/section]
    if (path.size() == 2u && td::contains(get_contacts_sections(), path[1])) {
      return td::make_unique<InternalLinkContacts>(path[1]);
    }
    return td::make_unique<InternalLinkContacts>(string());
  } else if (path.size() == 2 && path[0] == "chats" && path[1] == "edit") {
    // chats/edit
    return td::make_unique<InternalLinkDialogSelection>();
  } else if (path.size() == 2 && path[0] == "chats" && path[1] == "emoji-status") {
    // chats/emoji-status
    return td::make_unique<InternalLinkSettings>(vector<string>{"emoji-status"});
  } else if (path.size() == 2 && path[0] == "chats" && path[1] == "search") {
    // chats/search
    return td::make_unique<InternalLinkSearch>();
  } else if (path.size() == 1 && path[0] == "login") {
    // login?code=123456
    auto code = get_arg("code");
    if (is_valid_login_code(code)) {
      return td::make_unique<InternalLinkAuthenticationCode>(std::move(code));
    }
    // login?token=<token>
    if (has_arg("token")) {
      return td::make_unique<InternalLinkQrCodeAuthentication>();
    }
  } else if (path.size() == 1 && path[0] == "new") {
    // new
    return td::make_unique<InternalLinkNewPrivateChat>();
  } else if (path.size() == 2 && path[0] == "new" && path[1] == "channel") {
    // new/channel
    return td::make_unique<InternalLinkNewChannelChat>();
  } else if (path.size() == 2 && path[0] == "new" && path[1] == "group") {
    // new/group
    return td::make_unique<InternalLinkNewGroupChat>();
  } else if (path.size() == 1 && path[0] == "oauth" && has_arg("token")) {
    // oauth?token=...
    return nullptr;
  } else if (path.size() <= 2 && path[0] == "post") {
    // post[/content-type]
    return td::make_unique<InternalLinkPostStory>(path.size() == 2 ? path[1] : string());
  } else if (path.size() == 1 && path[0] == "restore_purchases") {
    // restore_purchases
    return td::make_unique<InternalLinkRestorePurchases>();
  } else if (path.size() == 1 && path[0] == "passport") {
    // passport?bot_id=...&scope=...&public_key=...&nonce=...&callback_url=...
    return get_internal_link_passport(query, url_query.args_, true);
  } else if (path.size() == 1 && path[0] == "premium_offer") {
    // premium_offer?ref=<referrer>
    auto referrer = get_arg("ref");
    if (is_valid_premium_referrer(referrer)) {
      return td::make_unique<InternalLinkPremiumFeatures>(std::move(referrer));
    }
  } else if (path.size() == 1 && path[0] == "premium_multigift") {
    // premium_multigift?ref=<referrer>
    auto referrer = get_arg("ref");
    if (is_valid_premium_referrer(referrer)) {
      return td::make_unique<InternalLinkPremiumGift>(std::move(referrer));
    }
  } else if (path.size() >= 2 && path[0] == "settings" && path[1] == "saved-messages") {
    // settings/saved-messages
    return td::make_unique<InternalLinkSavedMessages>();
  } else if (path.size() >= 2 && path[0] == "settings" && path[1] == "calls") {
    // settings/calls[/section]
    string section;
    if (path.size() >= 3u) {
      section = path[2];
      for (size_t i = 3; i < path.size(); i++) {
        section += '/';
        section += path[i];
      }
    }
    if (!td::contains(get_calls_sections(), section)) {
      section = string();
    }
    return td::make_unique<InternalLinkCalls>(std::move(section));
  } else if (path.size() >= 2 && path[0] == "settings" && path[1] == "my-profile") {
    // settings/my-profile[/section]
    string section;
    if (path.size() >= 3u) {
      section = path[2];
      for (size_t i = 3; i < path.size(); i++) {
        section += '/';
        section += path[i];
      }
    }
    if (!td::contains(get_my_profile_sections(), section)) {
      section = string();
    }
    return td::make_unique<InternalLinkMyProfile>(std::move(section));
  } else if (!path.empty() && path[0] == "settings") {
    // settings[/section[/subsection]]
    return td::make_unique<InternalLinkSettings>(vector<string>{path.begin() + 1, path.end()});
  } else if (!path.empty() && path[0] == "stars") {
    // stars
    return td::make_unique<InternalLinkSettings>(vector<string>{"stars"});
  } else if (!path.empty() && path[0] == "ton") {
    // ton
    return td::make_unique<InternalLinkSettings>(vector<string>{"ton"});
  } else if (path.size() == 1 && path[0] == "addlist") {
    auto slug = get_url_query_slug(true, url_query, "addlist");
    if (!slug.empty() && is_base64url_characters(slug)) {
      // addlist?slug=<slug>
      return td::make_unique<InternalLinkDialogFolderInvite>(get_dialog_filter_invite_link(slug, true));
    }
  } else if (path.size() == 1 && path[0] == "call") {
    auto slug = get_url_query_slug(true, url_query, "call");
    if (!slug.empty() && is_base64url_characters(slug)) {
      // call?slug=<slug>
      return td::make_unique<InternalLinkGroupCall>(get_group_call_invite_link(slug, true));
    }
  } else if (path.size() == 1 && path[0] == "join") {
    auto invite_hash = get_url_query_hash(true, url_query);
    if (!invite_hash.empty() && !is_valid_phone_number(invite_hash) && is_base64url_characters(invite_hash)) {
      // join?invite=<hash>
      return td::make_unique<InternalLinkDialogInvite>(get_dialog_invite_link(invite_hash, true));
    }
  } else if (path.size() == 1 && (path[0] == "addstickers" || path[0] == "addemoji")) {
    // addstickers?set=<name>
    // addemoji?set=<name>
    auto name = get_arg("set");
    if (is_valid_sticker_set_name(name)) {
      return td::make_unique<InternalLinkStickerSet>(std::move(name), path[0] == "addemoji");
    }
  } else if (path.size() == 1 && path[0] == "setlanguage") {
    // setlanguage?lang=<name>
    auto language_pack_id = get_arg("lang");
    if (is_valid_language_pack_id(language_pack_id)) {
      return td::make_unique<InternalLinkLanguage>(std::move(language_pack_id));
    }
  } else if (path.size() == 1 && path[0] == "addtheme") {
    // addtheme?slug=<name>
    auto theme_name = get_arg("slug");
    if (is_valid_theme_name(theme_name)) {
      return td::make_unique<InternalLinkTheme>(std::move(theme_name));
    }
  } else if (path.size() == 1 && path[0] == "confirmphone") {
    auto hash = get_arg("hash");
    auto phone_number = get_arg("phone");
    if (is_valid_phone_number_hash(hash) && is_valid_phone_number(phone_number)) {
      // confirmphone?phone=<phone>&hash=<hash>
      return td::make_unique<InternalLinkConfirmPhone>(std::move(hash), std::move(phone_number));
    }
  } else if (path.size() == 1 && path[0] == "socks") {
    // socks?server=<server>&port=<port>&user=<user>&pass=<pass>
    auto server = get_arg("server");
    auto port = to_integer<int32>(get_arg("port"));
    auto username = get_arg("user");
    auto password = get_arg("pass");
    if (is_valid_proxy_server(server) && 0 < port && port < 65536 && is_valid_proxy_username(username) &&
        is_valid_proxy_password(password)) {
      return td::make_unique<InternalLinkProxy>(
          std::move(server), port,
          td_api::make_object<td_api::proxyTypeSocks5>(std::move(username), std::move(password)));
    } else {
      return td::make_unique<InternalLinkProxy>(string(), 0, nullptr);
    }
  } else if (path.size() == 1 && path[0] == "proxy") {
    // proxy?server=<server>&port=<port>&secret=<secret>
    auto server = get_arg("server");
    auto port = to_integer<int32>(get_arg("port"));
    auto r_secret = mtproto::ProxySecret::from_link(get_arg("secret"));
    if (is_valid_proxy_server(server) && 0 < port && port < 65536 && r_secret.is_ok()) {
      return td::make_unique<InternalLinkProxy>(
          std::move(server), port, td_api::make_object<td_api::proxyTypeMtproto>(r_secret.ok().get_encoded_secret()));
    } else {
      return td::make_unique<InternalLinkProxy>(string(), 0, nullptr);
    }
  } else if (path.size() == 1 && path[0] == "privatepost") {
    // privatepost?channel=123456789&post=12345&single&thread=<thread_id>&comment=<message_id>&t=<media_timestamp>
    if (has_arg("channel") && has_arg("post")) {
      return td::make_unique<InternalLinkMessage>(
          PSTRING() << "tg://privatepost" << copy_arg("channel") << copy_arg("post") << copy_arg("single")
                    << copy_arg("thread") << copy_arg("comment") << copy_arg("t"));
    }
  } else if (path.size() == 1 && path[0] == "boost") {
    // boost?domain=channel_username
    // boost?channel=123456
    if (has_arg("domain")) {
      return td::make_unique<InternalLinkDialogBoost>(PSTRING() << "tg://boost" << copy_arg("domain"));
    }
    if (has_arg("channel")) {
      return td::make_unique<InternalLinkDialogBoost>(PSTRING() << "tg://boost" << copy_arg("channel"));
    }
  } else if (path.size() == 1 && path[0] == "bg") {
    // bg?color=<color>
    // bg?gradient=<hex_color>-<hex_color>&rotation=...
    // bg?gradient=<hex_color>~<hex_color>~<hex_color>~<hex_color>
    // bg?slug=<background_name>&mode=blur+motion
    // bg?slug=<pattern_name>&intensity=...&bg_color=...&mode=blur+motion
    if (has_arg("color")) {
      return td::make_unique<InternalLinkBackground>(pass_arg("color"));
    }
    if (has_arg("gradient")) {
      return td::make_unique<InternalLinkBackground>(PSTRING() << pass_arg("gradient") << copy_arg("rotation"));
    }
    if (has_arg("slug")) {
      return td::make_unique<InternalLinkBackground>(PSTRING()
                                                     << pass_arg("slug") << copy_arg("mode") << copy_arg("intensity")
                                                     << copy_arg("bg_color") << copy_arg("rotation"));
    }
  } else if (path.size() == 1 && path[0] == "invoice") {
    // invoice?slug=<invoice_name>
    auto invoice_name = get_arg("slug");
    if (is_valid_invoice_name(invoice_name)) {
      return td::make_unique<InternalLinkInvoice>(std::move(invoice_name));
    }
  } else if (path.size() == 1 && path[0] == "giftcode") {
    // giftcode?slug=<code>
    auto gift_code = get_arg("slug");
    if (is_valid_gift_code(gift_code)) {
      return td::make_unique<InternalLinkPremiumGiftCode>(std::move(gift_code));
    }
  } else if (path.size() == 1 && path[0] == "message") {
    // message?slug=<name>
    auto link_name = get_arg("slug");
    if (is_valid_business_link_name(link_name)) {
      return td::make_unique<InternalLinkBusinessChat>(std::move(link_name));
    }
  } else if (path.size() == 1 && (path[0] == "share" || path[0] == "msg" || path[0] == "msg_url")) {
    // msg_url?url=<url>
    // msg_url?url=<url>&text=<text>
    return get_internal_link_message_draft(get_arg("url"), get_arg("text"));
  } else if (path.size() == 1 && path[0] == "stars_topup") {
    // stars_topup?balance=<star_count>&purpose=<purpose>
    auto purpose = get_arg("purpose");
    if (has_arg("balance") && is_valid_star_top_up_purpose(purpose)) {
      return td::make_unique<InternalLinkBuyStars>(to_integer<int64>(url_query.get_arg("balance")), std::move(purpose));
    }
  } else if (path.size() == 1 && path[0] == "stargift_auction") {
    auto slug = get_url_query_slug(true, url_query, "stargift_auction");
    if (!slug.empty()) {
      // stargift_auction?slug=<slug>
      return td::make_unique<InternalLinkGiftAuction>(slug);
    }
  }
  if (!path.empty() && !path[0].empty()) {
    return td::make_unique<InternalLinkUnknownDeepLink>(PSTRING() << "tg://" << query);
  }
  return nullptr;
}

unique_ptr<LinkManager::InternalLink> LinkManager::parse_t_me_link_query(Slice query, bool is_trusted) {
  CHECK(query[0] == '/');
  const auto url_query = parse_url_query(query);
  const auto &path = url_query.path_;
  if (path.empty() || path[0].empty()) {
    return nullptr;
  }

  bool is_first_arg = true;
  auto copy_arg = [&](Slice name) {
    return CopyArg(name, &url_query, &is_first_arg);
  };

  auto get_arg = [&](Slice name) {
    return url_query.get_arg(name).str();
  };
  auto has_arg = [&](Slice name) {
    return !url_query.get_arg(name).empty();
  };

  if (path[0] == "c") {
    if (path.size() >= 3 && to_integer<int64>(path[1]) > 0 && to_integer<int64>(path[2]) > 0) {
      // /c/123456789/12345?single&thread=<thread_id>&comment=<message_id>&t=<media_timestamp>
      // /c/123456789/1234/12345?single&comment=<message_id>&t=<media_timestamp>
      is_first_arg = false;
      auto post = to_integer<int64>(path[2]);
      auto thread = PSTRING() << copy_arg("thread");
      if (path.size() >= 4 && to_integer<int64>(path[3]) > 0) {
        thread = PSTRING() << "&thread=" << post;
        post = to_integer<int64>(path[3]);
      }
      return td::make_unique<InternalLinkMessage>(PSTRING() << "tg://privatepost?channel=" << to_integer<int64>(path[1])
                                                            << "&post=" << post << copy_arg("single") << thread
                                                            << copy_arg("comment") << copy_arg("t"));
    } else if (path.size() >= 2 && to_integer<int64>(path[1]) > 0 && url_query.has_arg("boost")) {
      // /c/123456789?boost
      return td::make_unique<InternalLinkDialogBoost>(PSTRING() << "tg://boost?channel=" << to_integer<int64>(path[1]));
    }
  } else if (path[0] == "login") {
    if (path.size() >= 2 && is_valid_login_code(path[1])) {
      // /login/<code>
      auto code = path[1];
      return td::make_unique<InternalLinkAuthenticationCode>(std::move(code));
    }
  } else if (path[0] == "addlist") {
    auto slug = get_url_query_slug(false, url_query, "addlist");
    if (!slug.empty() && is_base64url_characters(slug)) {
      // /addlist/<slug>
      return td::make_unique<InternalLinkDialogFolderInvite>(get_dialog_filter_invite_link(slug, true));
    }
  } else if (path[0] == "call") {
    auto slug = get_url_query_slug(false, url_query, "call");
    if (!slug.empty() && is_base64url_characters(slug)) {
      // /call/<slug>
      return td::make_unique<InternalLinkGroupCall>(get_group_call_invite_link(slug, true));
    }
  } else if (path[0] == "joinchat") {
    auto invite_hash = get_url_query_hash(false, url_query);
    if (!invite_hash.empty() && !is_valid_phone_number(invite_hash) && is_base64url_characters(invite_hash)) {
      // /joinchat/<hash>
      return td::make_unique<InternalLinkDialogInvite>(get_dialog_invite_link(invite_hash, true));
    }
  } else if (path[0] == "auction") {
    auto slug = get_url_query_slug(false, url_query, "auction");
    if (!slug.empty()) {
      // /auction/<slug>
      return td::make_unique<InternalLinkGiftAuction>(slug);
    }
  } else if (path[0][0] == ' ' || path[0][0] == '+') {
    auto invite_hash = get_url_query_hash(false, url_query);
    if (is_valid_phone_number(invite_hash)) {
      auto attach = url_query.get_arg("attach");
      if (is_valid_username(attach)) {
        // /+<phone_number>?attach=<bot_username>
        // /+<phone_number>?attach=<bot_username>&startattach=<start_parameter>
        return td::make_unique<InternalLinkAttachMenuBot>(
            nullptr, td::make_unique<InternalLinkUserPhoneNumber>(invite_hash, string(), false), attach.str(),
            url_query.get_arg("startattach"));
      }
      // /+<phone_number>
      return td::make_unique<InternalLinkUserPhoneNumber>(invite_hash, get_url_query_draft_text(url_query),
                                                          url_query.has_arg("profile"));
    } else if (!invite_hash.empty() && is_base64url_characters(invite_hash)) {
      // /+<link>
      return td::make_unique<InternalLinkDialogInvite>(get_dialog_invite_link(invite_hash, true));
    }
  } else if (path[0] == "nft") {
    if (path.size() >= 2 && !path[1].empty()) {
      // /nft/<slug>
      string name = path[1];
      for (std::size_t i = 2; i < path.size(); i++) {
        name += '/';
        name += path[i];
      }
      if (is_valid_upgraded_gift_name(name)) {
        return td::make_unique<InternalLinkUpgradedGift>(std::move(name));
      }
    }
  } else if (path[0] == "contact") {
    if (path.size() >= 2 && is_valid_user_token(path[1])) {
      // /contact/<token>
      auto token = path[1];
      return td::make_unique<InternalLinkUserToken>(std::move(token));
    }
  } else if (path[0] == "addstickers" || path[0] == "addemoji") {
    if (path.size() >= 2 && is_valid_sticker_set_name(path[1])) {
      // /addstickers/<name>
      // /addemoji/<name>
      auto name = path[1];
      return td::make_unique<InternalLinkStickerSet>(std::move(name), path[0] == "addemoji");
    }
  } else if (path[0] == "setlanguage") {
    if (path.size() >= 2 && is_valid_language_pack_id(path[1])) {
      // /setlanguage/<name>
      auto language_pack_id = path[1];
      return td::make_unique<InternalLinkLanguage>(std::move(language_pack_id));
    }
  } else if (path[0] == "addtheme") {
    if (path.size() >= 2 && is_valid_theme_name(path[1])) {
      // /addtheme/<name>
      auto theme_name = path[1];
      return td::make_unique<InternalLinkTheme>(std::move(theme_name));
    }
  } else if (path[0] == "confirmphone") {
    auto hash = get_arg("hash");
    auto phone_number = get_arg("phone");
    if (is_valid_phone_number_hash(hash) && is_valid_phone_number(phone_number)) {
      // /confirmphone?phone=<phone>&hash=<hash>
      return td::make_unique<InternalLinkConfirmPhone>(std::move(hash), std::move(phone_number));
    }
  } else if (path[0] == "socks") {
    // /socks?server=<server>&port=<port>&user=<user>&pass=<pass>
    auto server = get_arg("server");
    auto port = to_integer<int32>(get_arg("port"));
    auto username = get_arg("user");
    auto password = get_arg("pass");
    if (is_valid_proxy_server(server) && 0 < port && port < 65536 && is_valid_proxy_username(username) &&
        is_valid_proxy_password(password)) {
      return td::make_unique<InternalLinkProxy>(
          std::move(server), port,
          td_api::make_object<td_api::proxyTypeSocks5>(std::move(username), std::move(password)));
    } else {
      return td::make_unique<InternalLinkProxy>(string(), 0, nullptr);
    }
  } else if (path[0] == "proxy") {
    // /proxy?server=<server>&port=<port>&secret=<secret>
    auto server = get_arg("server");
    auto port = to_integer<int32>(get_arg("port"));
    auto r_secret = mtproto::ProxySecret::from_link(get_arg("secret"));
    if (is_valid_proxy_server(server) && 0 < port && port < 65536 && r_secret.is_ok()) {
      return td::make_unique<InternalLinkProxy>(
          std::move(server), port, td_api::make_object<td_api::proxyTypeMtproto>(r_secret.ok().get_encoded_secret()));
    } else {
      return td::make_unique<InternalLinkProxy>(string(), 0, nullptr);
    }
  } else if (path[0] == "bg") {
    if (path.size() >= 2 && !path[1].empty()) {
      // /bg/<hex_color>
      // /bg/<hex_color>-<hex_color>?rotation=...
      // /bg/<hex_color>~<hex_color>~<hex_color>~<hex_color>
      // /bg/<background_name>?mode=blur+motion
      // /bg/<pattern_name>?intensity=...&bg_color=...&mode=blur+motion
      if (BackgroundType::is_background_name_local(path[1])) {
        return td::make_unique<InternalLinkBackground>(PSTRING() << url_encode(path[1]) << copy_arg("rotation"));
      }
      return td::make_unique<InternalLinkBackground>(PSTRING()
                                                     << url_encode(path[1]) << copy_arg("mode") << copy_arg("intensity")
                                                     << copy_arg("bg_color") << copy_arg("rotation"));
    }
  } else if (path[0] == "invoice") {
    if (path.size() >= 2 && is_valid_invoice_name(path[1])) {
      // /invoice/<name>
      return td::make_unique<InternalLinkInvoice>(path[1]);
    }
  } else if (path[0] == "giftcode") {
    if (path.size() >= 2 && is_valid_gift_code(path[1])) {
      // /giftcode/<code>
      return td::make_unique<InternalLinkPremiumGiftCode>(path[1]);
    }
  } else if (path[0] == "m") {
    if (path.size() >= 2 && is_valid_business_link_name(path[1])) {
      // /m/<link_name>
      return td::make_unique<InternalLinkBusinessChat>(path[1]);
    }
  } else if (path[0][0] == '$') {
    auto invoice_name = Slice(path[0]).substr(1).str();
    if (is_valid_invoice_name(invoice_name)) {
      // /$<invoice_name>
      return td::make_unique<InternalLinkInvoice>(std::move(invoice_name));
    }
  } else if (path[0] == "share" || path[0] == "msg") {
    if (path.size() > 1 && path[1] == "url") {
      // /share/url?url=<url>
      // /share/url?url=<url>&text=<text>
      return get_internal_link_message_draft(get_arg("url"), get_arg("text"));
    }
  } else if (path[0] == "iv") {
    if (path.size() == 1 && has_arg("url")) {
      // /iv?url=<url>&rhash=<rhash>
      return td::make_unique<InternalLinkInstantView>(
          PSTRING() << get_t_me_url() << "iv" << copy_arg("url") << copy_arg("rhash"), get_arg("url"));
    }
  } else if (is_valid_username(path[0]) && path[0] != "i") {
    if (path.size() >= 2 && to_integer<int64>(path[1]) > 0) {
      // /<username>/12345?single&thread=<thread_id>&comment=<message_id>&t=<media_timestamp>
      // /<username>/1234/12345?single&comment=<message_id>&t=<media_timestamp>
      is_first_arg = false;
      auto post = to_integer<int64>(path[1]);
      auto thread = PSTRING() << copy_arg("thread");
      if (path.size() >= 3 && to_integer<int64>(path[2]) > 0) {
        thread = PSTRING() << "&thread=" << post;
        post = to_integer<int64>(path[2]);
      }
      return td::make_unique<InternalLinkMessage>(PSTRING() << "tg://resolve?domain=" << url_encode(path[0])
                                                            << "&post=" << post << copy_arg("single") << thread
                                                            << copy_arg("comment") << copy_arg("t"));
    }
    auto username = path[0];
    if (to_lower(username) == "boost") {
      if (path.size() == 2 && is_valid_username(path[1])) {
        // /boost/<username>
        return td::make_unique<InternalLinkDialogBoost>(PSTRING() << "tg://boost?domain=" << url_encode(path[1]));
      }
      auto channel_id = url_query.get_arg("c");
      if (path.size() == 1 && to_integer<int64>(channel_id) > 0) {
        // /boost?c=<channel_id>
        return td::make_unique<InternalLinkDialogBoost>(PSTRING()
                                                        << "tg://boost?channel=" << to_integer<int64>(channel_id));
      }
    }
    if (path.size() == 3 && path[1] == "s" && is_valid_story_id(path[2])) {
      // /<username>/s/<story_id>
      return td::make_unique<InternalLinkStory>(std::move(username), StoryId(to_integer<int32>(path[2])));
    }
    if (path.size() == 3 && path[1] == "s" && path[2] == "live") {
      // /<username>/s/live
      return td::make_unique<InternalLinkLiveStory>(std::move(username));
    }
    if (path.size() == 3 && path[1] == "c" && is_valid_star_gift_collection_id(path[2])) {
      // /<username>/c/<collection_id>
      return td::make_unique<InternalLinkStarGiftCollection>(std::move(username),
                                                             StarGiftCollectionId(to_integer<int32>(path[2])));
    }
    if (path.size() == 3 && path[1] == "a" && is_valid_story_album_id(path[2])) {
      // /<username>/a/<story_album_id>
      return td::make_unique<InternalLinkStoryAlbum>(std::move(username), StoryAlbumId(to_integer<int32>(path[2])));
    }
    if (path.size() == 2 && is_valid_web_app_name(path[1])) {
      // /<username>/<web_app_name>
      // /<username>/<web_app_name>?startapp=<start_parameter>&mode=compact
      return td::make_unique<InternalLinkWebApp>(std::move(username), path[1], get_arg("startapp"), get_arg("mode"));
    }
    for (auto &arg : url_query.args_) {
      if ((arg.first == "voicechat" || arg.first == "videochat" || arg.first == "livestream") &&
          is_valid_video_chat_invite_hash(arg.second)) {
        // /<username>?videochat
        // /<username>?videochat=<invite_hash>
        if (Scheduler::context() != nullptr) {
          send_closure(G()->dialog_manager(), &DialogManager::reload_video_chat_on_search, username);
        }
        return td::make_unique<InternalLinkVideoChat>(std::move(username), arg.second, arg.first == "livestream");
      }
      if (arg.first == "boost") {
        // /<username>?boost
        return td::make_unique<InternalLinkDialogBoost>(PSTRING() << "tg://boost?domain=" << url_encode(username));
      }
      if (arg.first == "ref" && is_valid_start_parameter(arg.second) && !arg.second.empty()) {
        // /<bot_username>?ref=<referrer>
        return td::make_unique<InternalLinkDialogReferralProgram>(std::move(username), arg.second);
      }
      if (arg.first == "start" && is_valid_start_parameter(arg.second)) {
        auto prefixes = get_referral_program_start_parameter_prefixes();
        for (Slice prefix : prefixes) {
          if (begins_with(arg.second, prefix) && arg.second.size() > prefix.size()) {
            // /<bot_username>?start=_tgr_<referrer>
            return td::make_unique<InternalLinkDialogReferralProgram>(std::move(username),
                                                                      arg.second.substr(prefix.size()));
          }
        }
        // /<bot_username>?start=<parameter>
        return td::make_unique<InternalLinkBotStart>(std::move(username), arg.second, is_trusted);
      }
      if (arg.first == "startgroup" && is_valid_start_parameter(arg.second)) {
        // /<bot_username>?startgroup=<parameter>
        // /<bot_username>?startgroup=<parameter>&admin=change_info+delete_messages+restrict_members
        // /<bot_username>?startgroup&admin=change_info+delete_messages+restrict_members
        auto administrator_rights = get_administrator_rights(url_query.get_arg("admin"), false);
        return td::make_unique<InternalLinkBotStartInGroup>(std::move(username), arg.second,
                                                            std::move(administrator_rights));
      }
      if (arg.first == "startchannel") {
        // /<bot_username>?startchannel&admin=change_info+post_messages+promote_members
        auto administrator_rights = get_administrator_rights(url_query.get_arg("admin"), true);
        if (administrator_rights != AdministratorRights()) {
          return td::make_unique<InternalLinkBotAddToChannel>(std::move(username), std::move(administrator_rights));
        }
      }
      if (arg.first == "startapp" && is_valid_start_parameter(arg.second)) {
        // /<bot_username>?startapp
        // /<bot_username>?startapp=<parameter>&mode=compact
        return td::make_unique<InternalLinkMainWebApp>(std::move(username), arg.second, get_arg("mode"));
      }
      if (arg.first == "game" && is_valid_game_name(arg.second)) {
        // /<bot_username>?game=<short_name>
        return td::make_unique<InternalLinkGame>(std::move(username), arg.second);
      }
      if (arg.first == "attach" && is_valid_username(arg.second)) {
        // /<username>?attach=<bot_username>
        // /<username>?attach=<bot_username>&startattach=<start_parameter>
        return td::make_unique<InternalLinkAttachMenuBot>(
            nullptr, td::make_unique<InternalLinkPublicDialog>(std::move(username), string(), false), arg.second,
            url_query.get_arg("startattach"));
      }
      if (arg.first == "startattach" && !has_arg("attach")) {
        // /<bot_username>?startattach&choose=users+bots+groups+channels
        // /<bot_username>?startattach=<start_parameter>&choose=users+bots+groups+channels
        return td::make_unique<InternalLinkAttachMenuBot>(get_target_chat_types(url_query.get_arg("choose")), nullptr,
                                                          std::move(username), arg.second);
      }
      if (arg.first == "direct") {
        // /<username>?direct
        return td::make_unique<InternalLinkMonoforum>(std::move(username));
      }
    }

    // /<username>
    return td::make_unique<InternalLinkPublicDialog>(std::move(username), get_url_query_draft_text(url_query),
                                                     url_query.has_arg("profile"));
  }
  return nullptr;
}

unique_ptr<LinkManager::InternalLink> LinkManager::get_internal_link_message_draft(Slice url, Slice text) {
  if (url.empty() && text.empty()) {
    return nullptr;
  }
  while (!text.empty() && text.back() == '\n') {
    text.remove_suffix(1);
  }
  url = trim(url);
  if (url.empty()) {
    url = text;
    text = Slice();
  }
  FormattedText full_text;
  bool contains_url = false;
  if (!text.empty()) {
    contains_url = true;
    full_text.text = PSTRING() << url << '\n' << text;
  } else {
    full_text.text = url.str();
  }
  if (fix_formatted_text(full_text.text, full_text.entities, false, false, false, true, true).is_error()) {
    return nullptr;
  }
  if (full_text.text[0] == '@') {
    full_text.text = ' ' + full_text.text;
    for (auto &entity : full_text.entities) {
      entity.offset++;
    }
  }
  return td::make_unique<InternalLinkMessageDraft>(std::move(full_text), contains_url);
}

unique_ptr<LinkManager::InternalLink> LinkManager::get_internal_link_passport(
    Slice query, const vector<std::pair<string, string>> &args, bool allow_unknown) {
  auto get_arg = [&args](Slice key) {
    for (auto &arg : args) {
      if (arg.first == key) {
        return CSlice(arg.second);
      }
    }
    return CSlice();
  };

  UserId bot_user_id(to_integer<int64>(get_arg("bot_id")));
  auto scope = get_arg("scope");
  auto public_key = get_arg("public_key");
  auto nonce = get_arg("nonce");
  if (nonce.empty()) {
    nonce = get_arg("payload");
  }
  auto callback_url = get_arg("callback_url");

  if (!bot_user_id.is_valid() || scope.empty() || !check_utf8(scope) || public_key.empty() || !check_utf8(public_key) ||
      nonce.empty() || !check_utf8(nonce)) {
    if (!allow_unknown) {
      return nullptr;
    }
    return td::make_unique<InternalLinkUnknownDeepLink>(PSTRING() << "tg://" << query);
  }
  return td::make_unique<InternalLinkPassportDataRequest>(bot_user_id, scope.str(), public_key.str(), nonce.str(),
                                                          callback_url.str());
}

Result<string> LinkManager::get_internal_link(const td_api::object_ptr<td_api::InternalLinkType> &type,
                                              bool is_internal) {
  if (type == nullptr) {
    return Status::Error(400, "Link type must be non-empty");
  }
  return get_internal_link_impl(type.get(), is_internal);
}

Result<string> LinkManager::get_internal_link_impl(const td_api::InternalLinkType *type_ptr, bool is_internal) {
  switch (type_ptr->get_id()) {
    case td_api::internalLinkTypeAttachmentMenuBot::ID: {
      auto link = static_cast<const td_api::internalLinkTypeAttachmentMenuBot *>(type_ptr);
      if (!is_valid_username(link->bot_username_)) {
        return Status::Error(400, "Invalid bot username specified");
      }
      string start_parameter;
      if (!link->url_.empty()) {
        if (!begins_with(link->url_, "start://")) {
          return Status::Error(400, "Unsupported link URL specified");
        }
        auto start_parameter_slice = Slice(link->url_).substr(8);
        if (start_parameter_slice.empty() || !is_valid_start_parameter(start_parameter_slice)) {
          return Status::Error(400, "Invalid start parameter specified");
        }
        start_parameter = PSTRING() << '=' << start_parameter_slice;
      }
      if (link->target_chat_ == nullptr) {
        return Status::Error(400, "Target chat must be non-empty");
      }
      switch (link->target_chat_->get_id()) {
        case td_api::targetChatChosen::ID: {
          auto dialog_types = static_cast<const td_api::targetChatChosen *>(link->target_chat_.get())->types_.get();
          vector<string> types;
          if (dialog_types != nullptr) {
            if (dialog_types->allow_user_chats_) {
              types.push_back("users");
            }
            if (dialog_types->allow_bot_chats_) {
              types.push_back("bots");
            }
            if (dialog_types->allow_group_chats_) {
              types.push_back("groups");
            }
            if (dialog_types->allow_channel_chats_) {
              types.push_back("channels");
            }
          }
          if (types.empty()) {
            return Status::Error(400, "At least one target chat type must be allowed");
          }
          auto choose = implode(types, '+');
          if (is_internal) {
            return PSTRING() << "tg://resolve?domain=" << link->bot_username_ << "&startattach" << start_parameter
                             << "&choose=" << choose;
          } else {
            return PSTRING() << get_t_me_url() << link->bot_username_ << "?startattach" << start_parameter
                             << "&choose=" << choose;
          }
        }
        case td_api::targetChatCurrent::ID:
          if (is_internal) {
            return PSTRING() << "tg://resolve?domain=" << link->bot_username_ << "&startattach" << start_parameter;
          } else {
            return PSTRING() << get_t_me_url() << link->bot_username_ << "?startattach" << start_parameter;
          }
        case td_api::targetChatInternalLink::ID: {
          auto target = static_cast<const td_api::targetChatInternalLink *>(link->target_chat_.get());
          if (!start_parameter.empty()) {
            start_parameter = "&startattach" + start_parameter;
          }
          if (target->link_ == nullptr) {
            return Status::Error(400, "Target link must be non-empty");
          }
          switch (target->link_->get_id()) {
            case td_api::internalLinkTypeUserPhoneNumber::ID: {
              auto user_phone_number_link =
                  static_cast<const td_api::internalLinkTypeUserPhoneNumber *>(target->link_.get());
              if (user_phone_number_link->open_profile_) {
                return Status::Error(400, "Link must not open chat profile information screen");
              }
              string phone_number;
              if (user_phone_number_link->phone_number_[0] == '+') {
                phone_number = user_phone_number_link->phone_number_.substr(1);
              } else {
                phone_number = user_phone_number_link->phone_number_;
              }
              if (!is_valid_phone_number(phone_number)) {
                return Status::Error(400, "Invalid target phone number specified");
              }
              if (is_internal) {
                return PSTRING() << "tg://resolve?phone=+" << phone_number << "&attach=" << link->bot_username_
                                 << start_parameter;
              } else {
                return PSTRING() << get_t_me_url() << '+' << phone_number << "?attach=" << link->bot_username_
                                 << start_parameter;
              }
            }
            case td_api::internalLinkTypePublicChat::ID: {
              auto public_chat_link = static_cast<const td_api::internalLinkTypePublicChat *>(target->link_.get());
              if (public_chat_link->open_profile_) {
                return Status::Error(400, "Link must not open chat profile information screen");
              }
              if (!is_valid_username(public_chat_link->chat_username_)) {
                return Status::Error(400, "Invalid target public chat username specified");
              }
              if (is_internal) {
                return PSTRING() << "tg://resolve?domain=" << public_chat_link->chat_username_
                                 << "&attach=" << link->bot_username_ << start_parameter;
              } else {
                return PSTRING() << get_t_me_url() << public_chat_link->chat_username_
                                 << "?attach=" << link->bot_username_ << start_parameter;
              }
            }
            default:
              return Status::Error(400, "Unsupported target link specified");
          }
        }
        default:
          UNREACHABLE();
      }
      break;
    }
    case td_api::internalLinkTypeAuthenticationCode::ID: {
      auto link = static_cast<const td_api::internalLinkTypeAuthenticationCode *>(type_ptr);
      if (!is_valid_login_code(link->code_)) {
        return Status::Error(400, "Invalid authentication code specified");
      }
      if (is_internal) {
        return PSTRING() << "tg://login?code=" << url_encode(link->code_);
      } else {
        return PSTRING() << get_t_me_url() << "login/" << url_encode(link->code_);
      }
    }
    case td_api::internalLinkTypeBackground::ID: {
      auto link = static_cast<const td_api::internalLinkTypeBackground *>(type_ptr);
      auto params_pos = link->background_name_.find('?');
      string slug = params_pos >= link->background_name_.size() ? link->background_name_
                                                                : link->background_name_.substr(0, params_pos);
      if (slug.empty()) {
        return Status::Error(400, "Background name must be non-empty");
      }

      if (BackgroundType::is_background_name_local(slug)) {
        TRY_RESULT(background_type, BackgroundType::get_local_background_type(link->background_name_));
        auto background_link = background_type.get_link(!is_internal);
        CHECK(!background_type.has_file());
        if (is_internal) {
          Slice field_name = background_type.has_gradient_fill() ? Slice("gradient") : Slice("color");
          return PSTRING() << "tg://bg?" << field_name << '=' << background_link;
        } else {
          return PSTRING() << get_t_me_url() << "bg/" << background_link;
        }
      }

      auto prefix = is_internal ? string("tg://bg?slug=") : get_t_me_url() + "bg/";
      const auto url_query = parse_url_query(link->background_name_);

      bool is_first_arg = !is_internal;
      auto copy_arg = [&](Slice name) {
        return CopyArg(name, &url_query, &is_first_arg);
      };
      return PSTRING() << prefix << url_encode(slug) << copy_arg("mode") << copy_arg("intensity")
                       << copy_arg("bg_color") << copy_arg("rotation");
    }
    case td_api::internalLinkTypeBotAddToChannel::ID: {
      auto link = static_cast<const td_api::internalLinkTypeBotAddToChannel *>(type_ptr);
      if (!is_valid_username(link->bot_username_)) {
        return Status::Error(400, "Invalid bot username specified");
      }
      auto admin = get_admin_string(AdministratorRights(link->administrator_rights_, ChannelType::Broadcast));
      if (is_internal) {
        return PSTRING() << "tg://resolve?domain=" << link->bot_username_ << "&startchannel" << admin;
      } else {
        return PSTRING() << get_t_me_url() << link->bot_username_ << "?startchannel" << admin;
      }
    }
    case td_api::internalLinkTypeBotStart::ID: {
      auto link = static_cast<const td_api::internalLinkTypeBotStart *>(type_ptr);
      if (link->autostart_) {
        return Status::Error(400, "Can't create an autostart bot link");
      }
      if (!is_valid_username(link->bot_username_)) {
        return Status::Error(400, "Invalid bot username specified");
      }
      if (!is_valid_start_parameter(link->start_parameter_)) {
        return Status::Error(400, "Invalid start parameter specified");
      }
      auto start_parameter = link->start_parameter_.empty() ? string() : "=" + link->start_parameter_;
      if (is_internal) {
        return PSTRING() << "tg://resolve?domain=" << link->bot_username_ << "&start" << start_parameter;
      } else {
        return PSTRING() << get_t_me_url() << link->bot_username_ << "?start" << start_parameter;
      }
    }
    case td_api::internalLinkTypeBotStartInGroup::ID: {
      auto link = static_cast<const td_api::internalLinkTypeBotStartInGroup *>(type_ptr);
      if (!is_valid_username(link->bot_username_)) {
        return Status::Error(400, "Invalid bot username specified");
      }
      if (!is_valid_start_parameter(link->start_parameter_)) {
        return Status::Error(400, "Invalid start parameter specified");
      }
      auto admin = get_admin_string(AdministratorRights(link->administrator_rights_, ChannelType::Megagroup));
      auto start_parameter = link->start_parameter_.empty() ? string() : "=" + link->start_parameter_;
      if (is_internal) {
        return PSTRING() << "tg://resolve?domain=" << link->bot_username_ << "&startgroup" << start_parameter << admin;
      } else {
        return PSTRING() << get_t_me_url() << link->bot_username_ << "?startgroup" << start_parameter << admin;
      }
    }
    case td_api::internalLinkTypeBusinessChat::ID: {
      auto link = static_cast<const td_api::internalLinkTypeBusinessChat *>(type_ptr);
      if (!is_valid_business_link_name(link->link_name_)) {
        return Status::Error("Invalid link name specified");
      }
      if (is_internal) {
        return PSTRING() << "tg://message?slug=" << url_encode(link->link_name_);
      } else {
        return PSTRING() << get_t_me_url() << "m/" << url_encode(link->link_name_);
      }
    }
    case td_api::internalLinkTypeCallsPage::ID: {
      auto link = static_cast<const td_api::internalLinkTypeCallsPage *>(type_ptr);
      if (!is_internal) {
        return Status::Error("HTTP link is unavailable for the link type");
      }
      if (td::contains(get_calls_sections(), link->section_)) {
        return PSTRING() << "tg://settings/calls/" << link->section_;
      }
      return "tg://settings/calls";
    }
    case td_api::internalLinkTypeChatAffiliateProgram::ID: {
      auto link = static_cast<const td_api::internalLinkTypeChatAffiliateProgram *>(type_ptr);
      if (!is_valid_username(link->username_)) {
        return Status::Error(400, "Invalid username specified");
      }
      if (!is_valid_start_parameter(link->referrer_) || link->referrer_.empty()) {
        return Status::Error(400, "Invalid referrer specified");
      }
      auto start_parameter = PSTRING() << "start=" << get_referral_program_start_parameter_prefixes()[0]
                                       << link->referrer_;
      if (is_internal) {
        return PSTRING() << "tg://resolve?domain=" << link->username_ << "&" << start_parameter;
      } else {
        return PSTRING() << get_t_me_url() << link->username_ << "?" << start_parameter;
      }
    }
    case td_api::internalLinkTypeChatBoost::ID: {
      auto link = static_cast<const td_api::internalLinkTypeChatBoost *>(type_ptr);
      auto parsed_link = parse_internal_link(link->url_);
      if (parsed_link == nullptr) {
        return Status::Error(400, "Invalid chat boost URL specified");
      }
      auto parsed_object = parsed_link->get_internal_link_type_object();
      if (parsed_object->get_id() != td_api::internalLinkTypeChatBoost::ID) {
        return Status::Error(400, "Invalid chat boost URL specified");
      }
      if (!is_internal) {
        return Status::Error(400, "Use getChatBoostLink to get an HTTPS link to boost a chat");
      }
      return std::move(static_cast<td_api::internalLinkTypeChatBoost &>(*parsed_object).url_);
    }
    case td_api::internalLinkTypeChatFolderInvite::ID: {
      auto link = static_cast<const td_api::internalLinkTypeChatFolderInvite *>(type_ptr);
      auto slug = get_dialog_filter_invite_link_slug(link->invite_link_);
      if (slug.empty()) {
        return Status::Error(400, "Invalid invite link specified");
      }
      return get_dialog_filter_invite_link(slug, is_internal);
    }
    case td_api::internalLinkTypeChatInvite::ID: {
      auto link = static_cast<const td_api::internalLinkTypeChatInvite *>(type_ptr);
      auto invite_hash = get_dialog_invite_link_hash(link->invite_link_);
      if (invite_hash.empty()) {
        return Status::Error(400, "Invalid invite link specified");
      }
      return get_dialog_invite_link(invite_hash, is_internal);
    }
    case td_api::internalLinkTypeChatSelection::ID:
      if (!is_internal) {
        return Status::Error("HTTP link is unavailable for the link type");
      }
      return "tg://chats/edit";
    case td_api::internalLinkTypeContactsPage::ID: {
      auto link = static_cast<const td_api::internalLinkTypeContactsPage *>(type_ptr);
      if (!is_internal) {
        return Status::Error("HTTP link is unavailable for the link type");
      }
      if (td::contains(get_contacts_sections(), link->section_)) {
        return PSTRING() << "tg://contacts/" << link->section_;
      }
      return "tg://contacts";
    }
    case td_api::internalLinkTypeDirectMessagesChat::ID: {
      auto link = static_cast<const td_api::internalLinkTypeDirectMessagesChat *>(type_ptr);
      if (!is_valid_username(link->channel_username_)) {
        return Status::Error(400, "Invalid channel username specified");
      }
      if (is_internal) {
        return PSTRING() << "tg://resolve?domain=" << url_encode(link->channel_username_) << "&direct";
      } else {
        return PSTRING() << get_t_me_url() << url_encode(link->channel_username_) << "?direct";
      }
    }
    case td_api::internalLinkTypeGame::ID: {
      auto link = static_cast<const td_api::internalLinkTypeGame *>(type_ptr);
      if (!is_valid_username(link->bot_username_)) {
        return Status::Error(400, "Invalid bot username specified");
      }
      if (!is_valid_game_name(link->game_short_name_)) {
        return Status::Error(400, "Invalid game name specified");
      }
      if (is_internal) {
        return PSTRING() << "tg://resolve?domain=" << link->bot_username_ << "&game=" << link->game_short_name_;
      } else {
        return PSTRING() << get_t_me_url() << link->bot_username_ << "?game=" << link->game_short_name_;
      }
    }
    case td_api::internalLinkTypeGiftAuction::ID: {
      auto link = static_cast<const td_api::internalLinkTypeGiftAuction *>(type_ptr);
      if (link->auction_id_.empty()) {
        return Status::Error(400, "Invalid gift auction identifier specified");
      }
      if (is_internal) {
        return PSTRING() << "tg://stargift_auction?slug=" << url_encode(link->auction_id_);
      } else {
        return PSTRING() << get_t_me_url() << "auction/" << url_encode(link->auction_id_);
      }
    }
    case td_api::internalLinkTypeGiftCollection::ID: {
      auto link = static_cast<const td_api::internalLinkTypeGiftCollection *>(type_ptr);
      if (!is_valid_username(link->gift_owner_username_)) {
        return Status::Error(400, "Invalid gift collection owner username specified");
      }
      if (!StarGiftCollectionId(link->collection_id_).is_valid()) {
        return Status::Error(400, "Invalid gift collection identifier specified");
      }
      if (is_internal) {
        return PSTRING() << "tg://resolve?domain=" << link->gift_owner_username_
                         << "&collection=" << link->collection_id_;
      } else {
        return PSTRING() << get_t_me_url() << link->gift_owner_username_ << "/c/" << link->collection_id_;
      }
    }
    case td_api::internalLinkTypeGroupCall::ID: {
      auto link = static_cast<const td_api::internalLinkTypeGroupCall *>(type_ptr);
      auto slug = get_group_call_invite_link_slug(link->invite_link_);
      if (slug.empty()) {
        return Status::Error(400, "Invalid group call link specified");
      }
      return get_group_call_invite_link(slug, is_internal);
    }
    case td_api::internalLinkTypeInstantView::ID: {
      auto link = static_cast<const td_api::internalLinkTypeInstantView *>(type_ptr);
      if (is_internal) {
        return Status::Error("Deep link is unavailable for the link type");
      }
      auto info = get_link_info(link->url_);
      auto fallback_info = get_link_info(link->fallback_url_);
      switch (info.type_) {
        case LinkType::External:
        case LinkType::Tg:
          return Status::Error("Invalid instant view URL provided");
        case LinkType::Telegraph:
          if (fallback_info.type_ != LinkType::Telegraph ||
              link->url_ != (PSLICE() << "https://telegra.ph" << fallback_info.query_)) {
            return Status::Error("Unrelated fallback URL provided");
          }
          return link->fallback_url_;
        case LinkType::TMe:
          // skip URL and fallback_url consistency checks
          return link->url_;
        default:
          UNREACHABLE();
          break;
      }
    }
    case td_api::internalLinkTypeInvoice::ID: {
      auto link = static_cast<const td_api::internalLinkTypeInvoice *>(type_ptr);
      if (!is_valid_invoice_name(link->invoice_name_)) {
        return Status::Error(400, "Invalid invoice name specified");
      }
      if (is_internal) {
        return PSTRING() << "tg://invoice?slug=" << url_encode(link->invoice_name_);
      } else {
        return PSTRING() << get_t_me_url() << '$' << url_encode(link->invoice_name_);
      }
    }
    case td_api::internalLinkTypeLanguagePack::ID: {
      auto link = static_cast<const td_api::internalLinkTypeLanguagePack *>(type_ptr);
      if (!is_valid_language_pack_id(link->language_pack_id_)) {
        return Status::Error(400, "Invalid language pack specified");
      }
      if (is_internal) {
        return PSTRING() << "tg://setlanguage?lang=" << url_encode(link->language_pack_id_);
      } else {
        return PSTRING() << get_t_me_url() << "setlanguage/" << url_encode(link->language_pack_id_);
      }
    }
    case td_api::internalLinkTypeLiveStory::ID: {
      auto link = static_cast<const td_api::internalLinkTypeLiveStory *>(type_ptr);
      if (!is_valid_username(link->story_poster_username_)) {
        return Status::Error(400, "Invalid story poster username specified");
      }
      if (is_internal) {
        return PSTRING() << "tg://resolve?domain=" << link->story_poster_username_ << "&story=live";
      } else {
        return PSTRING() << get_t_me_url() << link->story_poster_username_ << "/s/live";
      }
    }
    case td_api::internalLinkTypeMainWebApp::ID: {
      auto link = static_cast<const td_api::internalLinkTypeMainWebApp *>(type_ptr);
      if (!is_valid_username(link->bot_username_)) {
        return Status::Error(400, "Invalid bot username specified");
      }
      string start_parameter;
      if (!link->start_parameter_.empty()) {
        if (!is_valid_start_parameter(link->start_parameter_)) {
          return Status::Error(400, "Invalid start parameter specified");
        }
        start_parameter = PSTRING() << '=' << link->start_parameter_;
      }
      string mode;
      if (link->mode_ != nullptr) {
        switch (link->mode_->get_id()) {
          case td_api::webAppOpenModeCompact::ID:
            mode = "&mode=compact";
            break;
          case td_api::webAppOpenModeFullSize::ID:
            break;
          case td_api::webAppOpenModeFullScreen::ID:
            mode = "&mode=fullscreen";
            break;
          default:
            UNREACHABLE();
        }
      }
      if (is_internal) {
        return PSTRING() << "tg://resolve?domain=" << link->bot_username_ << "&startapp" << start_parameter << mode;
      } else {
        return PSTRING() << get_t_me_url() << link->bot_username_ << "?startapp" << start_parameter << mode;
      }
    }
    case td_api::internalLinkTypeMessage::ID: {
      auto link = static_cast<const td_api::internalLinkTypeMessage *>(type_ptr);
      auto parsed_link = parse_internal_link(link->url_);
      if (parsed_link == nullptr) {
        return Status::Error(400, "Invalid message URL specified");
      }
      auto parsed_object = parsed_link->get_internal_link_type_object();
      if (parsed_object->get_id() != td_api::internalLinkTypeMessage::ID) {
        return Status::Error(400, "Invalid message URL specified");
      }
      if (!is_internal) {
        return Status::Error(400, "Use getMessageLink to get an HTTPS link to a message");
      }
      return std::move(static_cast<td_api::internalLinkTypeMessage &>(*parsed_object).url_);
    }
    case td_api::internalLinkTypeMessageDraft::ID: {
      auto *link = static_cast<const td_api::internalLinkTypeMessageDraft *>(type_ptr);
      string text;
      if (link->text_ != nullptr) {
        text = link->text_->text_;
      }
      string url;
      if (link->contains_link_) {
        std::tie(url, text) = split(text, '\n');
      } else {
        url = std::move(text);
        text.clear();
      }
      if (!text.empty()) {
        text = "&text=" + url_encode(text);
      }
      if (is_internal) {
        return PSTRING() << "tg://msg_url?url=" << url_encode(url) << text;
      } else {
        return PSTRING() << get_t_me_url() << "share/url?url=" << url_encode(url) << text;
      }
    }
    case td_api::internalLinkTypeMyProfilePage::ID: {
      auto link = static_cast<const td_api::internalLinkTypeMyProfilePage *>(type_ptr);
      if (!is_internal) {
        return Status::Error("HTTP link is unavailable for the link type");
      }
      if (td::contains(get_my_profile_sections(), link->section_)) {
        return PSTRING() << "tg://settings/my-profile/" << link->section_;
      }
      return "tg://settings/my-profile";
    }
    case td_api::internalLinkTypeNewChannelChat::ID:
      if (!is_internal) {
        return Status::Error("HTTP link is unavailable for the link type");
      }
      return "tg://new/channel";
    case td_api::internalLinkTypeNewGroupChat::ID:
      if (!is_internal) {
        return Status::Error("HTTP link is unavailable for the link type");
      }
      return "tg://new/group";
    case td_api::internalLinkTypeNewPrivateChat::ID:
      if (!is_internal) {
        return Status::Error("HTTP link is unavailable for the link type");
      }
      return "tg://new";
    case td_api::internalLinkTypeNewStory::ID: {
      auto link = static_cast<const td_api::internalLinkTypeNewStory *>(type_ptr);
      if (!is_internal) {
        return Status::Error("HTTP link is unavailable for the link type");
      }
      if (link->content_type_ != nullptr) {
        switch (link->content_type_->get_id()) {
          case td_api::storyContentTypePhoto::ID:
            return "tg://post/photo";
          case td_api::storyContentTypeVideo::ID:
            return "tg://post/video";
          case td_api::storyContentTypeLive::ID:
            return "tg://post/live";
          case td_api::storyContentTypeUnsupported::ID:
            return "tg://post/unsupported";
          default:
            UNREACHABLE();
        }
      }
      return "tg://post";
    }
    case td_api::internalLinkTypePassportDataRequest::ID: {
      auto link = static_cast<const td_api::internalLinkTypePassportDataRequest *>(type_ptr);
      if (!is_internal) {
        return Status::Error("HTTP link is unavailable for the link type");
      }
      if (!UserId(link->bot_user_id_).is_valid()) {
        return Status::Error("Invalid bot user identifier specified");
      }
      if (link->scope_.empty() || !check_utf8(link->scope_) || link->public_key_.empty() ||
          !check_utf8(link->public_key_) || link->nonce_.empty() || !check_utf8(link->nonce_)) {
        return Status::Error("Invalid parameters specified");
      }
      return PSTRING() << "tg://resolve?domain=telegrampassport&bot_id=" << link->bot_user_id_
                       << "&scope=" << url_encode(link->scope_) << "&public_key=" << url_encode(link->public_key_)
                       << "&nonce=" << url_encode(link->nonce_) << "&callback_url=" << url_encode(link->callback_url_);
    }
    case td_api::internalLinkTypePhoneNumberConfirmation::ID: {
      auto link = static_cast<const td_api::internalLinkTypePhoneNumberConfirmation *>(type_ptr);
      if (!is_valid_phone_number(link->phone_number_)) {
        return Status::Error("Invalid phone number specified");
      }
      if (!is_valid_phone_number_hash(link->hash_)) {
        return Status::Error("Invalid phone number hash specified");
      }
      if (is_internal) {
        return PSTRING() << "tg://confirmphone?phone=" << url_encode(link->phone_number_)
                         << "&hash=" << url_encode(link->hash_);
      } else {
        return PSTRING() << get_t_me_url() << "confirmphone?phone=" << url_encode(link->phone_number_)
                         << "&hash=" << url_encode(link->hash_);
      }
    }
    case td_api::internalLinkTypePremiumFeaturesPage::ID: {
      auto link = static_cast<const td_api::internalLinkTypePremiumFeaturesPage *>(type_ptr);
      if (!is_internal) {
        return Status::Error("HTTP link is unavailable for the link type");
      }
      if (!is_valid_premium_referrer(link->referrer_)) {
        return Status::Error("Invalid referrer specified");
      }
      return PSTRING() << "tg://premium_offer?ref=" << url_encode(link->referrer_);
    }
    case td_api::internalLinkTypePremiumGiftCode::ID: {
      auto link = static_cast<const td_api::internalLinkTypePremiumGiftCode *>(type_ptr);
      if (!is_valid_gift_code(link->code_)) {
        return Status::Error("Invalid gift code specified");
      }
      if (is_internal) {
        return PSTRING() << "tg://giftcode?slug=" << url_encode(link->code_);
      } else {
        return PSTRING() << get_t_me_url() << "giftcode/" << url_encode(link->code_);
      }
    }
    case td_api::internalLinkTypePremiumGiftPurchase::ID: {
      auto link = static_cast<const td_api::internalLinkTypePremiumGiftPurchase *>(type_ptr);
      if (!is_internal) {
        return Status::Error("HTTP link is unavailable for the link type");
      }
      if (!is_valid_premium_referrer(link->referrer_)) {
        return Status::Error("Invalid referrer specified");
      }
      return PSTRING() << "tg://premium_multigift?ref=" << url_encode(link->referrer_);
    }
    case td_api::internalLinkTypeProxy::ID: {
      auto link = static_cast<const td_api::internalLinkTypeProxy *>(type_ptr);
      if (link->proxy_ == nullptr) {
        if (is_internal) {
          return "tg://proxy?port=-1&server=0.0.0.0";
        } else {
          return PSTRING() << get_t_me_url() << "proxy?port=-1&server=0.0.0.0";
        }
      }
      TRY_RESULT(proxy, Proxy::create_proxy(link->proxy_.get()));
      return get_proxy_link(proxy, is_internal);
    }
    case td_api::internalLinkTypePublicChat::ID: {
      auto link = static_cast<const td_api::internalLinkTypePublicChat *>(type_ptr);
      if (!is_valid_username(link->chat_username_)) {
        return Status::Error(400, "Invalid chat username specified");
      }
      if (!check_utf8(link->draft_text_)) {
        return Status::Error(400, "Draft text must be encoded in UTF-8");
      }
      return get_public_dialog_link(link->chat_username_, link->draft_text_, link->open_profile_, is_internal);
    }
    case td_api::internalLinkTypeQrCodeAuthentication::ID:
      return Status::Error("The link must never be generated client-side");
    case td_api::internalLinkTypeRestorePurchases::ID:
      if (!is_internal) {
        return Status::Error("HTTP link is unavailable for the link type");
      }
      return "tg://restore_purchases";
    case td_api::internalLinkTypeSavedMessages::ID:
      if (!is_internal) {
        return Status::Error("HTTP link is unavailable for the link type");
      }
      return "tg://settings/saved-messages";
    case td_api::internalLinkTypeSearch::ID:
      if (!is_internal) {
        return Status::Error("HTTP link is unavailable for the link type");
      }
      return "tg://chats/search";
    case td_api::internalLinkTypeSettings::ID: {
      auto link = static_cast<const td_api::internalLinkTypeSettings *>(type_ptr);
      if (!is_internal) {
        return Status::Error("HTTP link is unavailable for the link type");
      }
      auto *section_ptr = link->section_.get();
      if (section_ptr == nullptr) {
        return "tg://settings";
      }
      switch (section_ptr->get_id()) {
        case td_api::settingsSectionAppearance::ID: {
          const auto &subsection = static_cast<const td_api::settingsSectionAppearance *>(section_ptr)->subsection_;
          if (td::contains(get_appearance_settings_subsections(), subsection)) {
            return PSTRING() << "tg://settings/appearance/" << subsection;
          }
          return "tg://settings/themes";
        }
        case td_api::settingsSectionBusiness::ID: {
          const auto &subsection = static_cast<const td_api::settingsSectionBusiness *>(section_ptr)->subsection_;
          if (td::contains(get_business_settings_subsections(), subsection)) {
            return PSTRING() << "tg://settings/business/" << subsection;
          }
          return "tg://settings/business";
        }
        case td_api::settingsSectionAskQuestion::ID:
          return "tg://settings/ask-question";
        case td_api::settingsSectionChatFolders::ID: {
          const auto &subsection = static_cast<const td_api::settingsSectionChatFolders *>(section_ptr)->subsection_;
          if (td::contains(get_folder_settings_subsections(), subsection)) {
            return PSTRING() << "tg://settings/folders/" << subsection;
          }
          return "tg://settings/folders";
        }
        case td_api::settingsSectionDataAndStorage::ID: {
          const auto &subsection = static_cast<const td_api::settingsSectionDataAndStorage *>(section_ptr)->subsection_;
          if (td::contains(get_data_settings_subsections(), subsection)) {
            return PSTRING() << "tg://settings/data/" << subsection;
          }
          return "tg://settings/data";
        }
        case td_api::settingsSectionDevices::ID: {
          const auto &subsection = static_cast<const td_api::settingsSectionDevices *>(section_ptr)->subsection_;
          if (td::contains(get_device_settings_subsections(), subsection)) {
            return PSTRING() << "tg://settings/devices/" << subsection;
          }
          return "tg://settings/devices";
        }
        case td_api::settingsSectionEditProfile::ID: {
          const auto &subsection = static_cast<const td_api::settingsSectionEditProfile *>(section_ptr)->subsection_;
          if (td::contains(get_edit_profile_settings_subsections(), subsection)) {
            if (subsection == "change-number") {
              return "tg://settings/change_number";
            }
            return PSTRING() << "tg://settings/edit/" << subsection;
          } else if (td::contains(get_edit_profile_other_settings_subsections(), subsection)) {
            return PSTRING() << "tg://settings/" << subsection;
          }
          return "tg://settings/edit_profile";
        }
        case td_api::settingsSectionFaq::ID:
          return "tg://settings/faq";
        case td_api::settingsSectionFeatures::ID:
          return "tg://settings/features";
        case td_api::settingsSectionInAppBrowser::ID: {
          const auto &subsection = static_cast<const td_api::settingsSectionInAppBrowser *>(section_ptr)->subsection_;
          if (td::contains(get_in_app_browser_settings_subsections(), subsection)) {
            return PSTRING() << "tg://settings/chat/browser/" << subsection;
          }
          return "tg://settings/chat/browser";
        }
        case td_api::settingsSectionLanguage::ID: {
          const auto &subsection = static_cast<const td_api::settingsSectionLanguage *>(section_ptr)->subsection_;
          if (td::contains(get_language_settings_subsections(), subsection)) {
            return PSTRING() << "tg://settings/language/" << subsection;
          }
          return "tg://settings/language";
        }
        case td_api::settingsSectionMyStars::ID: {
          const auto &subsection = static_cast<const td_api::settingsSectionMyStars *>(section_ptr)->subsection_;
          if (td::contains(get_my_stars_settings_subsections(), subsection)) {
            return PSTRING() << "tg://settings/stars/" << subsection;
          }
          return "tg://stars";
        }
        case td_api::settingsSectionMyToncoins::ID:
          return "tg://ton";
        case td_api::settingsSectionNotifications::ID: {
          const auto &subsection = static_cast<const td_api::settingsSectionNotifications *>(section_ptr)->subsection_;
          if (td::contains(get_notification_settings_subsections(), subsection)) {
            return PSTRING() << "tg://settings/notifications/" << subsection;
          }
          return "tg://settings/notifications";
        }
        case td_api::settingsSectionPowerSaving::ID: {
          const auto &subsection = static_cast<const td_api::settingsSectionPowerSaving *>(section_ptr)->subsection_;
          if (td::contains(get_power_saving_settings_subsections(), subsection)) {
            return PSTRING() << "tg://settings/power-saving/" << subsection;
          }
          return "tg://settings/power-saving";
        }
        case td_api::settingsSectionPremium::ID:
          return "tg://settings/premium";
        case td_api::settingsSectionPrivacyAndSecurity::ID: {
          const auto &subsection =
              static_cast<const td_api::settingsSectionPrivacyAndSecurity *>(section_ptr)->subsection_;
          if (td::contains(get_privacy_settings_subsections(), subsection)) {
            if (subsection == "phone-number") {
              return "tg://settings/phone_privacy";
            }
            if (subsection == "auto-delete") {
              return "tg://settings/auto_delete";
            }
            if (subsection == "login-email") {
              return "tg://settings/login_email";
            }
            if (subsection == "2sv") {
              return "tg://settings/password";
            }
            return PSTRING() << "tg://settings/privacy/" << subsection;
          }
          return "tg://settings/privacy";
        }
        case td_api::settingsSectionPrivacyPolicy::ID:
          return "tg://settings/privacy-policy";
        case td_api::settingsSectionQrCode::ID: {
          const auto &subsection = static_cast<const td_api::settingsSectionQrCode *>(section_ptr)->subsection_;
          if (td::contains(get_qr_code_settings_subsections(), subsection)) {
            return PSTRING() << "tg://settings/qr-code/" << subsection;
          }
          return "tg://settings/qr-code";
        }
        case td_api::settingsSectionSearch::ID:
          return "tg://settings/search";
        case td_api::settingsSectionSendGift::ID: {
          const auto &subsection = static_cast<const td_api::settingsSectionSendGift *>(section_ptr)->subsection_;
          if (td::contains(get_send_gift_settings_subsections(), subsection)) {
            return PSTRING() << "tg://settings/send-gift/" << subsection;
          }
          return "tg://settings/send-gift";
        }
        default:
          UNREACHABLE();
          return "";
      }
    }
    case td_api::internalLinkTypeStarPurchase::ID: {
      auto link = static_cast<const td_api::internalLinkTypeStarPurchase *>(type_ptr);
      if (!is_internal) {
        return Status::Error("HTTP link is unavailable for the link type");
      }
      if (link->star_count_ <= 0) {
        return Status::Error(400, "Invalid Telegram Star amount provided");
      }
      if (!is_valid_star_top_up_purpose(link->purpose_)) {
        return Status::Error(400, "Invalid purpose specified");
      }
      return PSTRING() << "tg://stars_topup?balance=" << link->star_count_ << "&purpose=" << url_encode(link->purpose_);
    }
    case td_api::internalLinkTypeStickerSet::ID: {
      auto link = static_cast<const td_api::internalLinkTypeStickerSet *>(type_ptr);
      if (!is_valid_sticker_set_name(link->sticker_set_name_)) {
        return Status::Error(400, "Invalid sticker set name specified");
      }
      if (is_internal) {
        return PSTRING() << "tg://add" << (link->expect_custom_emoji_ ? "emoji" : "stickers")
                         << "?set=" << url_encode(link->sticker_set_name_);
      } else {
        return PSTRING() << get_t_me_url() << "add" << (link->expect_custom_emoji_ ? "emoji" : "stickers") << '/'
                         << url_encode(link->sticker_set_name_);
      }
    }
    case td_api::internalLinkTypeStory::ID: {
      auto link = static_cast<const td_api::internalLinkTypeStory *>(type_ptr);
      if (!is_valid_username(link->story_poster_username_)) {
        return Status::Error(400, "Invalid story poster username specified");
      }
      if (!StoryId(link->story_id_).is_server()) {
        return Status::Error(400, "Invalid story identifier specified");
      }
      if (is_internal) {
        return PSTRING() << "tg://resolve?domain=" << link->story_poster_username_ << "&story=" << link->story_id_;
      } else {
        return PSTRING() << get_t_me_url() << link->story_poster_username_ << "/s/" << link->story_id_;
      }
    }
    case td_api::internalLinkTypeStoryAlbum::ID: {
      auto link = static_cast<const td_api::internalLinkTypeStoryAlbum *>(type_ptr);
      if (!is_valid_username(link->story_album_owner_username_)) {
        return Status::Error(400, "Invalid story album owner username specified");
      }
      if (!StoryAlbumId(link->story_album_id_).is_valid()) {
        return Status::Error(400, "Invalid story album identifier specified");
      }
      if (is_internal) {
        return PSTRING() << "tg://resolve?domain=" << link->story_album_owner_username_
                         << "&album=" << link->story_album_id_;
      } else {
        return PSTRING() << get_t_me_url() << link->story_album_owner_username_ << "/a/" << link->story_album_id_;
      }
    }
    case td_api::internalLinkTypeTheme::ID: {
      auto link = static_cast<const td_api::internalLinkTypeTheme *>(type_ptr);
      if (!is_valid_theme_name(link->theme_name_)) {
        return Status::Error(400, "Invalid theme name specified");
      }
      if (is_internal) {
        return PSTRING() << "tg://addtheme?slug=" << url_encode(link->theme_name_);
      } else {
        return PSTRING() << get_t_me_url() << "addtheme/" << url_encode(link->theme_name_);
      }
    }
    case td_api::internalLinkTypeUnknownDeepLink::ID: {
      auto link = static_cast<const td_api::internalLinkTypeUnknownDeepLink *>(type_ptr);
      if (!is_internal) {
        return Status::Error("HTTP link is unavailable for the link type");
      }
      auto parsed_link = parse_internal_link(link->link_);
      if (parsed_link == nullptr) {
        return Status::Error(400, "Invalid deep link URL specified");
      }
      auto parsed_object = parsed_link->get_internal_link_type_object();
      if (parsed_object->get_id() != td_api::internalLinkTypeUnknownDeepLink::ID) {
        return Status::Error(400, "Invalid deep link URL specified");
      }
      return std::move(static_cast<td_api::internalLinkTypeUnknownDeepLink &>(*parsed_object).link_);
    }
    case td_api::internalLinkTypeUpgradedGift::ID: {
      auto link = static_cast<const td_api::internalLinkTypeUpgradedGift *>(type_ptr);
      if (!is_valid_upgraded_gift_name(link->name_)) {
        return Status::Error(400, "Invalid gift name specified");
      }
      if (is_internal) {
        return PSTRING() << "tg://nft?slug=" << url_encode(link->name_);
      } else {
        return PSTRING() << get_t_me_url() << "nft/" << url_encode(link->name_);
      }
    }
    case td_api::internalLinkTypeUserPhoneNumber::ID: {
      auto link = static_cast<const td_api::internalLinkTypeUserPhoneNumber *>(type_ptr);
      string phone_number;
      if (link->phone_number_[0] == '+') {
        phone_number = link->phone_number_.substr(1);
      } else {
        phone_number = link->phone_number_;
      }
      if (!is_valid_phone_number(phone_number)) {
        return Status::Error(400, "Invalid phone number specified");
      }
      if (!check_utf8(link->draft_text_)) {
        return Status::Error(400, "Draft text must be encoded in UTF-8");
      }
      if (is_internal) {
        return PSTRING() << "tg://resolve?phone=+" << phone_number << (link->draft_text_.empty() ? "" : "&text=")
                         << url_encode(link->draft_text_) << (link->open_profile_ ? "&profile" : "");
      } else {
        bool has_draft = !link->draft_text_.empty();
        return PSTRING() << get_t_me_url() << '+' << phone_number << (has_draft ? "?text=" : "")
                         << url_encode(link->draft_text_)
                         << (link->open_profile_ ? (has_draft ? "&profile" : "?profile") : "");
      }
    }
    case td_api::internalLinkTypeUserToken::ID: {
      auto link = static_cast<const td_api::internalLinkTypeUserToken *>(type_ptr);
      if (!is_valid_user_token(link->token_)) {
        return Status::Error(400, "Invalid user token specified");
      }
      if (is_internal) {
        return PSTRING() << "tg://contact?token=" << url_encode(link->token_);
      } else {
        return PSTRING() << get_t_me_url() << "contact/" << url_encode(link->token_);
      }
    }
    case td_api::internalLinkTypeVideoChat::ID: {
      auto link = static_cast<const td_api::internalLinkTypeVideoChat *>(type_ptr);
      if (!is_valid_username(link->chat_username_)) {
        return Status::Error(400, "Invalid chat username specified");
      }
      if (!is_valid_video_chat_invite_hash(link->invite_hash_)) {
        return Status::Error(400, "Invalid invite hash specified");
      }
      string invite_hash;
      if (!link->invite_hash_.empty()) {
        invite_hash = '=' + url_encode(link->invite_hash_);
      }
      auto name = link->is_live_stream_ ? Slice("livestream") : Slice("videochat");
      if (is_internal) {
        return PSTRING() << "tg://resolve?domain=" << link->chat_username_ << '&' << name << invite_hash;
      } else {
        return PSTRING() << get_t_me_url() << link->chat_username_ << '?' << name << invite_hash;
      }
    }
    case td_api::internalLinkTypeWebApp::ID: {
      auto link = static_cast<const td_api::internalLinkTypeWebApp *>(type_ptr);
      if (!is_valid_username(link->bot_username_)) {
        return Status::Error(400, "Invalid bot username specified");
      }
      if (!is_valid_web_app_name(link->web_app_short_name_)) {
        return Status::Error(400, "Invalid Web App name specified");
      }
      if (!is_valid_start_parameter(link->start_parameter_)) {
        return Status::Error(400, "Invalid start parameter specified");
      }
      string mode;
      if (link->mode_ != nullptr) {
        switch (link->mode_->get_id()) {
          case td_api::webAppOpenModeCompact::ID:
            mode = "&mode=compact";
            break;
          case td_api::webAppOpenModeFullSize::ID:
            break;
          case td_api::webAppOpenModeFullScreen::ID:
            mode = "&mode=fullscreen";
            break;
          default:
            UNREACHABLE();
        }
      }
      string parameters;
      if (!link->start_parameter_.empty()) {
        parameters = PSTRING() << (is_internal ? '&' : '?') << "startapp=" << link->start_parameter_ << mode;
      } else if (!mode.empty()) {
        if (!is_internal) {
          mode[0] = '?';
        }
        parameters = std::move(mode);
      }
      if (is_internal) {
        return PSTRING() << "tg://resolve?domain=" << link->bot_username_ << "&appname=" << link->web_app_short_name_
                         << parameters;
      } else {
        return PSTRING() << get_t_me_url() << link->bot_username_ << '/' << link->web_app_short_name_ << parameters;
      }
    }
    default:
      break;
  }
  UNREACHABLE();
  return Status::Error(500, "Unsupported");
}

void LinkManager::update_autologin_token(string autologin_token) {
  autologin_update_time_ = Time::now();
  autologin_token_ = std::move(autologin_token);
}

void LinkManager::update_autologin_domains(vector<string> autologin_domains, vector<string> url_auth_domains,
                                           vector<string> whitelisted_domains) {
  if (autologin_domains_ != autologin_domains) {
    autologin_domains_ = std::move(autologin_domains);
    G()->td_db()->get_binlog_pmc()->set("autologin_domains", implode(autologin_domains_, '\xFF'));
  }
  if (url_auth_domains_ != url_auth_domains) {
    url_auth_domains_ = std::move(url_auth_domains);
    G()->td_db()->get_binlog_pmc()->set("url_auth_domains", implode(url_auth_domains_, '\xFF'));
  }
  if (whitelisted_domains_ != whitelisted_domains) {
    whitelisted_domains_ = std::move(whitelisted_domains);
    G()->td_db()->get_binlog_pmc()->set("whitelisted_domains", implode(whitelisted_domains_, '\xFF'));
  }
}

void LinkManager::get_recent_me_urls(const string &referrer, Promise<td_api::object_ptr<td_api::tMeUrls>> &&promise) {
  td_->create_handler<GetRecentMeUrlsQuery>(std::move(promise))->send(referrer);
}

void LinkManager::get_deep_link_info(Slice link, Promise<td_api::object_ptr<td_api::deepLinkInfo>> &&promise) {
  Slice link_scheme("tg:");
  if (begins_with(link, link_scheme)) {
    link.remove_prefix(link_scheme.size());
    if (begins_with(link, "//")) {
      link.remove_prefix(2);
    }
  }
  size_t pos = 0;
  while (pos < link.size() && link[pos] != '/' && link[pos] != '?' && link[pos] != '#') {
    pos++;
  }
  link.truncate(pos);
  td_->create_handler<GetDeepLinkInfoQuery>(std::move(promise))->send(link);
}

void LinkManager::get_external_link_info(string &&link, Promise<td_api::object_ptr<td_api::LoginUrlInfo>> &&promise) {
  bool is_ton = false;
  if (tolower_begins_with(link, "tonsite://")) {
    link = link.substr(10);
    is_ton = true;
  }
  auto default_result = td_api::make_object<td_api::loginUrlInfoOpen>(link, false);
  if (G()->close_flag()) {
    return promise.set_value(std::move(default_result));
  }

  auto r_url = parse_url(link);
  if (r_url.is_error()) {
    auto info = get_link_info(link);
    if (info.type_ == LinkType::Tg) {
      const auto url_query = parse_url_query(info.query_);
      const auto &path = url_query.path_;
      if (path.size() == 1 &&
          ((path[0] == "resolve" && url_query.get_arg("domain") == "oauth" && !url_query.get_arg("startapp").empty()) ||
           (path[0] == "oauth" && !url_query.get_arg("token").empty()))) {
        td_->create_handler<RequestUrlAuthQuery>(std::move(promise))->send(link, MessageFullId(), 0);
        return;
      }
    }
    return promise.set_value(std::move(default_result));
  }

  auto url = r_url.move_as_ok();
  if (!url.userinfo_.empty() || url.is_ipv6_) {
    return promise.set_value(std::move(default_result));
  }
  if (is_ton || (url.host_.size() >= 4u && to_lower(url.host_.substr(url.host_.size() - 4)) == ".ton")) {
    auto ton_proxy_address = td_->option_manager_->get_option_string("ton_proxy_address");
    if (ton_proxy_address.empty()) {
      return promise.set_value(std::move(default_result));
    }
    url.protocol_ = HttpUrl::Protocol::Https;
    string new_host;
    for (auto c : url.host_) {
      if (c == '.') {
        new_host += "-d";
      } else if (c == '-') {
        new_host += "-h";
      } else {
        new_host += c;
      }
    }
    url.host_ = PSTRING() << new_host << '.' << ton_proxy_address;
    default_result->url_ = url.get_url();
  }

  bool skip_confirmation = td::contains(whitelisted_domains_, url.host_);
  default_result->skip_confirmation_ = skip_confirmation;

  if (!td::contains(autologin_domains_, url.host_)) {
    if (td::contains(url_auth_domains_, url.host_)) {
      td_->create_handler<RequestUrlAuthQuery>(std::move(promise))->send(link, MessageFullId(), 0);
      return;
    }
    return promise.set_value(std::move(default_result));
  }

  if (autologin_update_time_ < Time::now() - 10000) {
    auto query_promise = PromiseCreator::lambda([link = std::move(link), default_result = std::move(default_result),
                                                 promise = std::move(promise)](Result<Unit> &&result) mutable {
      if (result.is_error()) {
        return promise.set_value(std::move(default_result));
      }
      send_closure(G()->link_manager(), &LinkManager::get_external_link_info, std::move(link), std::move(promise));
    });
    return send_closure(G()->config_manager(), &ConfigManager::reget_config, std::move(query_promise));
  }

  if (autologin_token_.empty()) {
    return promise.set_value(std::move(default_result));
  }

  url.protocol_ = HttpUrl::Protocol::Https;
  Slice path = url.query_;
  path.truncate(url.query_.find_first_of("?#"));
  Slice parameters_hash = Slice(url.query_).substr(path.size());
  Slice parameters = parameters_hash;
  parameters.truncate(parameters.find('#'));
  Slice hash = parameters_hash.substr(parameters.size());

  string added_parameter;
  if (parameters.empty()) {
    added_parameter = '?';
  } else if (parameters.size() == 1) {
    CHECK(parameters == "?");
  } else {
    added_parameter = '&';
  }
  added_parameter += "autologin_token=";
  added_parameter += autologin_token_;

  url.query_ = PSTRING() << path << parameters << added_parameter << hash;

  promise.set_value(td_api::make_object<td_api::loginUrlInfoOpen>(url.get_url(), skip_confirmation));
}

void LinkManager::get_login_url_info(MessageFullId message_full_id, int64 button_id,
                                     Promise<td_api::object_ptr<td_api::LoginUrlInfo>> &&promise) {
  TRY_RESULT_PROMISE(promise, url, td_->messages_manager_->get_login_button_url(message_full_id, button_id));
  td_->create_handler<RequestUrlAuthQuery>(std::move(promise))
      ->send(std::move(url), message_full_id, narrow_cast<int32>(button_id));
}

void LinkManager::get_login_url(MessageFullId message_full_id, int64 button_id, bool allow_write_access,
                                Promise<td_api::object_ptr<td_api::httpUrl>> &&promise) {
  TRY_RESULT_PROMISE(promise, url, td_->messages_manager_->get_login_button_url(message_full_id, button_id));
  td_->create_handler<AcceptUrlAuthQuery>(std::move(promise))
      ->send(std::move(url), message_full_id, narrow_cast<int32>(button_id), allow_write_access, false);
}

void LinkManager::get_link_login_url(const string &url, bool allow_write_access, bool allow_phone_number_access,
                                     Promise<td_api::object_ptr<td_api::httpUrl>> &&promise) {
  td_->create_handler<AcceptUrlAuthQuery>(std::move(promise))
      ->send(url, MessageFullId(), 0, allow_write_access, allow_phone_number_access);
}

Result<string> LinkManager::get_background_url(const string &name,
                                               td_api::object_ptr<td_api::BackgroundType> background_type) {
  if (background_type == nullptr) {
    return Status::Error(400, "Type must be non-empty");
  }
  if (background_type->get_id() == td_api::backgroundTypeChatTheme::ID) {
    return Status::Error(400, "Background has no link");
  }
  TRY_RESULT(type, BackgroundType::get_background_type(background_type.get(), 0));
  auto url = PSTRING() << get_t_me_url() << "bg/";
  auto link = type.get_link();
  if (type.has_file()) {
    url += name;
    if (!link.empty()) {
      url += '?';
      url += link;
    }
  } else {
    url += link;
  }
  return url;
}

td_api::object_ptr<td_api::BackgroundType> LinkManager::get_background_type_object(const string &link,
                                                                                   bool is_pattern) {
  auto parsed_link = parse_internal_link(link);
  if (parsed_link == nullptr) {
    return nullptr;
  }
  auto parsed_object = parsed_link->get_internal_link_type_object();
  if (parsed_object->get_id() != td_api::internalLinkTypeBackground::ID) {
    return nullptr;
  }
  auto background_name =
      std::move(static_cast<td_api::internalLinkTypeBackground *>(parsed_object.get())->background_name_);
  if (!BackgroundType::is_background_name_local(background_name)) {
    BackgroundType type(false, is_pattern, nullptr);
    type.apply_parameters_from_link(background_name);
    return type.get_background_type_object();
  }
  auto r_background_type = BackgroundType::get_local_background_type(background_name);
  if (r_background_type.is_error()) {
    return nullptr;
  }
  return r_background_type.ok().get_background_type_object();
}

bool LinkManager::has_video_chat_invite_hash(Slice link) {
  auto internal_link = parse_internal_link(link);
  if (internal_link == nullptr) {
    return false;
  }
  auto internal_link_type = internal_link->get_internal_link_type_object();
  return internal_link_type->get_id() == td_api::internalLinkTypeVideoChat::ID &&
         !static_cast<const td_api::internalLinkTypeVideoChat *>(internal_link_type.get())->invite_hash_.empty();
}

string LinkManager::get_dialog_filter_invite_link_slug(Slice invite_link) {
  auto link_info = get_link_info(invite_link);
  if (link_info.type_ != LinkType::Tg && link_info.type_ != LinkType::TMe) {
    return string();
  }
  const auto url_query = parse_url_query(link_info.query_);
  auto slug = get_url_query_slug(link_info.type_ == LinkType::Tg, url_query, "addlist");
  if (!is_base64url_characters(slug)) {
    return string();
  }
  return slug;
}

string LinkManager::get_dialog_filter_invite_link(Slice slug, bool is_internal) {
  if (!is_base64url_characters(slug)) {
    return string();
  }
  if (is_internal) {
    return PSTRING() << "tg:addlist?slug=" << slug;
  } else {
    return PSTRING() << get_t_me_url() << "addlist/" << slug;
  }
}

string LinkManager::get_dialog_invite_link_hash(Slice invite_link) {
  auto link_info = get_link_info(invite_link);
  if (link_info.type_ != LinkType::Tg && link_info.type_ != LinkType::TMe) {
    return string();
  }
  const auto url_query = parse_url_query(link_info.query_);
  auto invite_hash = get_url_query_hash(link_info.type_ == LinkType::Tg, url_query);
  if (is_valid_phone_number(invite_hash)) {
    return string();
  }
  if (!is_base64url_characters(invite_hash)) {
    return string();
  }
  return invite_hash;
}

string LinkManager::get_dialog_invite_link(Slice invite_hash, bool is_internal) {
  if (!is_base64url_characters(invite_hash)) {
    return string();
  }
  if (is_internal) {
    return PSTRING() << "tg:join?invite=" << invite_hash;
  } else {
    return PSTRING() << get_t_me_url() << '+' << invite_hash;
  }
}

string LinkManager::get_group_call_invite_link_slug(Slice invite_link) {
  auto link_info = get_link_info(invite_link);
  if (link_info.type_ != LinkType::Tg && link_info.type_ != LinkType::TMe) {
    return string();
  }
  const auto url_query = parse_url_query(link_info.query_);
  auto slug = get_url_query_slug(link_info.type_ == LinkType::Tg, url_query, "call");
  if (!is_base64url_characters(slug)) {
    return string();
  }
  return slug;
}

string LinkManager::get_group_call_invite_link(Slice slug, bool is_internal) {
  if (!is_base64url_characters(slug)) {
    return string();
  }
  if (is_internal) {
    return PSTRING() << "tg:call?slug=" << slug;
  } else {
    return PSTRING() << get_t_me_url() << "call/" << slug;
  }
}

string LinkManager::get_instant_view_link_url(Slice link) {
  auto link_info = get_link_info(link);
  if (link_info.type_ != LinkType::TMe) {
    return string();
  }
  const auto url_query = parse_url_query(link_info.query_);
  const auto &path = url_query.path_;
  if (path.size() == 1 && path[0] == "iv") {
    return url_query.get_arg("url").str();
  }
  return string();
}

string LinkManager::get_instant_view_link_rhash(Slice link) {
  auto link_info = get_link_info(link);
  if (link_info.type_ != LinkType::TMe) {
    return string();
  }
  const auto url_query = parse_url_query(link_info.query_);
  const auto &path = url_query.path_;
  if (path.size() == 1 && path[0] == "iv" && !url_query.get_arg("url").empty()) {
    return url_query.get_arg("rhash").str();
  }
  return string();
}

string LinkManager::get_instant_view_link(Slice url, Slice rhash) {
  return PSTRING() << get_t_me_url() << "iv?url=" << url_encode(url) << "&rhash=" << url_encode(rhash);
}

string LinkManager::get_public_dialog_link(Slice username, Slice draft_text, bool open_profile, bool is_internal) {
  if (is_internal) {
    return PSTRING() << "tg://resolve?domain=" << url_encode(username) << (draft_text.empty() ? "" : "&text=")
                     << url_encode(draft_text) << (open_profile ? "&profile" : "");
  } else {
    return PSTRING() << get_t_me_url() << url_encode(username) << (draft_text.empty() ? "" : "?text=")
                     << url_encode(draft_text) << (open_profile ? (draft_text.empty() ? "?profile" : "&profile") : "");
  }
}

Result<string> LinkManager::get_proxy_link(const Proxy &proxy, bool is_internal) {
  string url = is_internal ? "tg://" : get_t_me_url();
  bool is_socks = false;
  switch (proxy.type()) {
    case Proxy::Type::Socks5:
      url += "socks";
      is_socks = true;
      break;
    case Proxy::Type::HttpTcp:
    case Proxy::Type::HttpCaching:
      return Status::Error(400, "HTTP proxies have no public links");
    case Proxy::Type::Mtproto:
      url += "proxy";
      break;
    default:
      UNREACHABLE();
  }
  url += "?server=";
  url += url_encode(proxy.server());
  url += "&port=";
  url += to_string(proxy.port());
  if (is_socks) {
    if (!proxy.user().empty() || !proxy.password().empty()) {
      url += "&user=";
      url += url_encode(proxy.user());
      url += "&pass=";
      url += url_encode(proxy.password());
    }
  } else {
    url += "&secret=";
    url += proxy.secret().get_encoded_secret();
  }
  return std::move(url);
}

UserId LinkManager::get_link_user_id(Slice url) {
  string lower_cased_url = to_lower(url);
  url = lower_cased_url;

  Slice link_scheme("tg:");
  if (!begins_with(url, link_scheme)) {
    return UserId();
  }
  url.remove_prefix(link_scheme.size());
  if (begins_with(url, "//")) {
    url.remove_prefix(2);
  }

  Slice host("user");
  if (!begins_with(url, host) || (url.size() > host.size() && Slice("/?#").find(url[host.size()]) == Slice::npos)) {
    return UserId();
  }
  url.remove_prefix(host.size());
  if (begins_with(url, "/")) {
    url.remove_prefix(1);
  }
  if (!begins_with(url, "?")) {
    return UserId();
  }
  url.remove_prefix(1);
  url.truncate(url.find('#'));

  for (auto parameter : full_split(url, '&')) {
    Slice key;
    Slice value;
    std::tie(key, value) = split(parameter, '=');
    if (key == Slice("id")) {
      auto r_user_id = to_integer_safe<int64>(value);
      if (r_user_id.is_error()) {
        return UserId();
      }
      return UserId(r_user_id.ok());
    }
  }
  return UserId();
}

string LinkManager::get_t_me_url() {
  if (Scheduler::context() != nullptr) {
    return G()->get_option_string("t_me_url", "https://t.me/");
  } else {
    return "https://t.me/";
  }
}

Result<CustomEmojiId> LinkManager::get_link_custom_emoji_id(Slice url) {
  string lower_cased_url = to_lower(url);
  url = lower_cased_url;

  Slice link_scheme("tg:");
  if (!begins_with(url, link_scheme)) {
    return Status::Error(400, "Custom emoji URL must have scheme tg");
  }
  url.remove_prefix(link_scheme.size());
  if (begins_with(url, "//")) {
    url.remove_prefix(2);
  }

  Slice host("emoji");
  if (!begins_with(url, host) || (url.size() > host.size() && Slice("/?#").find(url[host.size()]) == Slice::npos)) {
    return Status::Error(400, PSLICE() << "Custom emoji URL must have host \"" << host << '"');
  }
  url.remove_prefix(host.size());
  if (begins_with(url, "/")) {
    url.remove_prefix(1);
  }
  if (!begins_with(url, "?")) {
    return Status::Error(400, "Custom emoji URL must have an emoji identifier");
  }
  url.remove_prefix(1);
  url.truncate(url.find('#'));

  for (auto parameter : full_split(url, '&')) {
    Slice key;
    Slice value;
    std::tie(key, value) = split(parameter, '=');
    if (key == Slice("id")) {
      auto r_document_id = to_integer_safe<int64>(value);
      if (r_document_id.is_error() || r_document_id.ok() == 0) {
        return Status::Error(400, "Invalid custom emoji identifier specified");
      }
      return CustomEmojiId(r_document_id.ok());
    }
  }
  return Status::Error(400, "Custom emoji URL must have an emoji identifier");
}

Result<DialogBoostLinkInfo> LinkManager::get_dialog_boost_link_info(Slice url) {
  if (url.empty()) {
    return Status::Error("URL must be non-empty");
  }
  auto link_info = get_link_info(url);
  if (link_info.type_ != LinkType::Tg && link_info.type_ != LinkType::TMe) {
    return Status::Error("Invalid chat boost link URL");
  }
  url = link_info.query_;

  Slice username;
  Slice channel_id_slice;
  if (link_info.type_ == LinkType::Tg) {
    // boost?domain=username
    // boost?channel=123456789

    if (!begins_with(url, "boost")) {
      return Status::Error("Wrong chat boost link URL");
    }
    url = url.substr(5);

    if (begins_with(url, "/")) {
      url = url.substr(1);
    }
    if (!begins_with(url, "?")) {
      return Status::Error("Wrong chat boost link URL");
    }
    url = url.substr(1);

    auto args = full_split(url, '&');
    for (auto arg : args) {
      auto key_value = split(arg, '=');
      if (key_value.first == "domain") {
        username = key_value.second;
      } else if (key_value.first == "channel") {
        channel_id_slice = key_value.second;
      }
    }
  } else {
    // /username?boost
    // /c/123456789?boost

    CHECK(!url.empty() && url[0] == '/');
    url.remove_prefix(1);

    size_t username_end_pos = 0;
    while (username_end_pos < url.size() && url[username_end_pos] != '/' && url[username_end_pos] != '?' &&
           url[username_end_pos] != '#') {
      username_end_pos++;
    }
    username = url.substr(0, username_end_pos);
    url = url.substr(username_end_pos);
    if (!url.empty() && url[0] == '/') {
      url = url.substr(1);
    }
    if (username == "c") {
      username = Slice();
      size_t channel_id_end_pos = 0;
      while (channel_id_end_pos < url.size() && url[channel_id_end_pos] != '/' && url[channel_id_end_pos] != '?' &&
             url[channel_id_end_pos] != '#') {
        channel_id_end_pos++;
      }
      channel_id_slice = url.substr(0, channel_id_end_pos);
      url = url.substr(channel_id_end_pos);
    }

    bool is_boost = false;
    auto query_pos = url.find('?');
    if (query_pos != Slice::npos) {
      auto args = full_split(url.substr(query_pos + 1), '&');
      for (auto arg : args) {
        auto key_value = split(arg, '=');
        if (key_value.first == "boost") {
          is_boost = true;
        }
      }
    }

    if (!is_boost) {
      return Status::Error("Wrong chat boost link URL");
    }
  }

  ChannelId channel_id;
  if (username.empty()) {
    auto r_channel_id = to_integer_safe<int64>(channel_id_slice);
    if (r_channel_id.is_error() || !ChannelId(r_channel_id.ok()).is_valid()) {
      return Status::Error("Wrong channel ID");
    }
    channel_id = ChannelId(r_channel_id.ok());
  }

  DialogBoostLinkInfo info;
  info.username = username.str();
  info.channel_id = channel_id;
  LOG(INFO) << "Have link to boost chat @" << info.username << '/' << channel_id.get();
  return std::move(info);
}

Result<MessageLinkInfo> LinkManager::get_message_link_info(Slice url) {
  if (url.empty()) {
    return Status::Error("URL must be non-empty");
  }
  auto link_info = get_link_info(url);
  if (link_info.type_ != LinkType::Tg && link_info.type_ != LinkType::TMe) {
    return Status::Error("Invalid message link URL");
  }
  url = link_info.query_;

  Slice username;
  Slice channel_id_slice;
  Slice message_id_slice;
  Slice comment_message_id_slice = "0";
  Slice top_thread_message_id_slice;
  Slice media_timestamp_slice;
  bool is_single = false;
  bool for_comment = false;
  if (link_info.type_ == LinkType::Tg) {
    // resolve?domain=username&post=12345&single&t=123&comment=12&thread=21
    // privatepost?channel=123456789&post=12345&single&t=123&comment=12&thread=21

    bool is_resolve = false;
    if (begins_with(url, "resolve")) {
      url = url.substr(7);
      is_resolve = true;
    } else if (begins_with(url, "privatepost")) {
      url = url.substr(11);
    } else {
      return Status::Error("Wrong message link URL");
    }

    if (begins_with(url, "/")) {
      url = url.substr(1);
    }
    if (!begins_with(url, "?")) {
      return Status::Error("Wrong message link URL");
    }
    url = url.substr(1);

    auto args = full_split(url, '&');
    for (auto arg : args) {
      auto key_value = split(arg, '=');
      if (is_resolve) {
        if (key_value.first == "domain") {
          username = key_value.second;
        }
      } else {
        if (key_value.first == "channel") {
          channel_id_slice = key_value.second;
        }
      }
      if (key_value.first == "post") {
        message_id_slice = key_value.second;
      }
      if (key_value.first == "t") {
        media_timestamp_slice = key_value.second;
      }
      if (key_value.first == "single") {
        is_single = true;
      }
      if (key_value.first == "comment") {
        comment_message_id_slice = key_value.second;
      }
      if (key_value.first == "thread") {
        for_comment = true;
        top_thread_message_id_slice = key_value.second;
      }
    }
  } else {
    // /c/123456789/12345
    // /c/123456789/1234/12345
    // /username/12345?single

    CHECK(!url.empty() && url[0] == '/');
    url.remove_prefix(1);

    auto username_end_pos = url.find('/');
    if (username_end_pos == Slice::npos) {
      return Status::Error("Wrong message link URL");
    }
    username = url.substr(0, username_end_pos);
    url = url.substr(username_end_pos + 1);
    if (username == "c") {
      username = Slice();
      auto channel_id_end_pos = url.find('/');
      if (channel_id_end_pos == Slice::npos) {
        return Status::Error("Wrong message link URL");
      }
      channel_id_slice = url.substr(0, channel_id_end_pos);
      url = url.substr(channel_id_end_pos + 1);
    }

    auto query_pos = url.find('?');
    message_id_slice = url.substr(0, query_pos);
    if (query_pos != Slice::npos) {
      auto args = full_split(url.substr(query_pos + 1), '&');
      for (auto arg : args) {
        auto key_value = split(arg, '=');
        if (key_value.first == "t") {
          media_timestamp_slice = key_value.second;
        }
        if (key_value.first == "single") {
          is_single = true;
        }
        if (key_value.first == "comment") {
          comment_message_id_slice = key_value.second;
        }
        if (key_value.first == "thread") {
          for_comment = true;
          top_thread_message_id_slice = key_value.second;
        }
      }
    }
    auto slash_pos = message_id_slice.find('/');
    if (slash_pos != Slice::npos) {
      top_thread_message_id_slice = message_id_slice.substr(0, slash_pos);
      message_id_slice.remove_prefix(slash_pos + 1);
    }
  }

  ChannelId channel_id;
  if (username.empty()) {
    auto r_channel_id = to_integer_safe<int64>(channel_id_slice);
    if (r_channel_id.is_error() || !ChannelId(r_channel_id.ok()).is_valid()) {
      return Status::Error("Wrong channel ID");
    }
    channel_id = ChannelId(r_channel_id.ok());
  }

  auto r_message_id = to_integer_safe<int32>(message_id_slice);
  if (r_message_id.is_error() || !ServerMessageId(r_message_id.ok()).is_valid()) {
    return Status::Error("Wrong message ID");
  }

  int32 top_thread_message_id = 0;
  if (!top_thread_message_id_slice.empty()) {
    auto r_top_thread_message_id = to_integer_safe<int32>(top_thread_message_id_slice);
    if (r_top_thread_message_id.is_error()) {
      return Status::Error("Wrong message thread ID");
    }
    top_thread_message_id = r_top_thread_message_id.ok();
    if (!ServerMessageId(top_thread_message_id).is_valid()) {
      return Status::Error("Invalid message thread ID");
    }
  }

  auto r_comment_message_id = to_integer_safe<int32>(comment_message_id_slice);
  if (r_comment_message_id.is_error() ||
      !(r_comment_message_id.ok() == 0 || ServerMessageId(r_comment_message_id.ok()).is_valid())) {
    return Status::Error("Wrong comment message ID");
  }

  bool is_media_timestamp_invalid = false;
  int32 media_timestamp = 0;
  const int32 MAX_MEDIA_TIMESTAMP = 10000000;
  if (!media_timestamp_slice.empty()) {
    int32 current_value = 0;
    for (size_t i = 0; i <= media_timestamp_slice.size(); i++) {
      auto c = i < media_timestamp_slice.size() ? media_timestamp_slice[i] : 's';
      if ('0' <= c && c <= '9') {
        current_value = current_value * 10 + c - '0';
        if (current_value > MAX_MEDIA_TIMESTAMP) {
          is_media_timestamp_invalid = true;
          break;
        }
      } else {
        auto mul = 0;
        switch (to_lower(c)) {
          case 'h':
            mul = 3600;
            break;
          case 'm':
            mul = 60;
            break;
          case 's':
            mul = 1;
            break;
        }
        if (mul == 0 || current_value > MAX_MEDIA_TIMESTAMP / mul ||
            media_timestamp + current_value * mul > MAX_MEDIA_TIMESTAMP) {
          is_media_timestamp_invalid = true;
          break;
        }
        media_timestamp += current_value * mul;
        current_value = 0;
      }
    }
  }

  MessageLinkInfo info;
  info.username = username.str();
  info.channel_id = channel_id;
  info.message_id = MessageId(ServerMessageId(r_message_id.ok()));
  info.comment_message_id = MessageId(ServerMessageId(r_comment_message_id.ok()));
  info.top_thread_message_id = MessageId(ServerMessageId(top_thread_message_id));
  info.media_timestamp = is_media_timestamp_invalid ? 0 : media_timestamp;
  info.is_single = is_single;
  info.for_comment = for_comment;
  LOG(INFO) << "Have link to " << info.message_id << " in chat @" << info.username << '/' << channel_id.get();
  return std::move(info);
}

}  // namespace td
