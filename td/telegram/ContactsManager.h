//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/telegram/AccessRights.h"
#include "td/telegram/ChannelId.h"
#include "td/telegram/ChatId.h"
#include "td/telegram/Contact.h"
#include "td/telegram/DialogAdministrator.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/DialogInviteLink.h"
#include "td/telegram/DialogLocation.h"
#include "td/telegram/DialogParticipant.h"
#include "td/telegram/files/FileId.h"
#include "td/telegram/files/FileSourceId.h"
#include "td/telegram/FolderId.h"
#include "td/telegram/FullMessageId.h"
#include "td/telegram/Location.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/net/DcId.h"
#include "td/telegram/Photo.h"
#include "td/telegram/PublicDialogType.h"
#include "td/telegram/QueryCombiner.h"
#include "td/telegram/RestrictionReason.h"
#include "td/telegram/SecretChatId.h"
#include "td/telegram/StickerSetId.h"
#include "td/telegram/SuggestedAction.h"
#include "td/telegram/UserId.h"

#include "td/actor/actor.h"
#include "td/actor/MultiPromise.h"
#include "td/actor/PromiseFuture.h"
#include "td/actor/Timeout.h"

#include "td/utils/common.h"
#include "td/utils/Hints.h"
#include "td/utils/Status.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/Time.h"

#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace td {

struct BinlogEvent;

class DialogInviteLink;
class DialogLocation;

class Td;

struct BotData {
  string username;
  bool can_join_groups;
  bool can_read_all_group_messages;
  bool is_inline;
  bool need_location;
};

enum class ChannelType : uint8 { Broadcast, Megagroup, Unknown };

enum class CheckDialogUsernameResult : uint8 { Ok, Invalid, Occupied, PublicDialogsTooMuch, PublicGroupsUnavailable };

struct CanTransferOwnershipResult {
  enum class Type : uint8 { Ok, PasswordNeeded, PasswordTooFresh, SessionTooFresh };
  Type type = Type::Ok;
  int32 retry_after = 0;
};

class ContactsManager : public Actor {
 public:
  ContactsManager(Td *td, ActorShared<> parent);
  ContactsManager(const ContactsManager &) = delete;
  ContactsManager &operator=(const ContactsManager &) = delete;
  ContactsManager(ContactsManager &&) = delete;
  ContactsManager &operator=(ContactsManager &&) = delete;
  ~ContactsManager() override;

  static UserId load_my_id();

  static UserId get_user_id(const tl_object_ptr<telegram_api::User> &user);
  static ChatId get_chat_id(const tl_object_ptr<telegram_api::Chat> &chat);
  static ChannelId get_channel_id(const tl_object_ptr<telegram_api::Chat> &chat);

  tl_object_ptr<telegram_api::InputUser> get_input_user(UserId user_id) const;
  bool have_input_user(UserId user_id) const;

  // TODO get_input_chat ???

  tl_object_ptr<telegram_api::InputChannel> get_input_channel(ChannelId channel_id) const;

  tl_object_ptr<telegram_api::InputPeer> get_input_peer_user(UserId user_id, AccessRights access_rights) const;
  bool have_input_peer_user(UserId user_id, AccessRights access_rights) const;

  tl_object_ptr<telegram_api::InputPeer> get_input_peer_chat(ChatId chat_id, AccessRights access_rights) const;
  bool have_input_peer_chat(ChatId chat_id, AccessRights access_rights) const;

  tl_object_ptr<telegram_api::InputPeer> get_input_peer_channel(ChannelId channel_id, AccessRights access_rights) const;
  bool have_input_peer_channel(ChannelId channel_id, AccessRights access_rights) const;

  tl_object_ptr<telegram_api::inputEncryptedChat> get_input_encrypted_chat(SecretChatId secret_chat_id,
                                                                           AccessRights access_rights) const;
  bool have_input_encrypted_peer(SecretChatId secret_chat_id, AccessRights access_rights) const;

  const DialogPhoto *get_user_dialog_photo(UserId user_id);
  const DialogPhoto *get_chat_dialog_photo(ChatId chat_id) const;
  const DialogPhoto *get_channel_dialog_photo(ChannelId channel_id) const;
  const DialogPhoto *get_secret_chat_dialog_photo(SecretChatId secret_chat_id);

  string get_user_title(UserId user_id) const;
  string get_chat_title(ChatId chat_id) const;
  string get_channel_title(ChannelId channel_id) const;
  string get_secret_chat_title(SecretChatId secret_chat_id) const;

  RestrictedRights get_user_default_permissions(UserId user_id) const;
  RestrictedRights get_chat_default_permissions(ChatId chat_id) const;
  RestrictedRights get_channel_default_permissions(ChannelId channel_id) const;
  RestrictedRights get_secret_chat_default_permissions(SecretChatId secret_chat_id) const;

  bool is_update_about_username_change_received(UserId user_id) const;

  void for_each_secret_chat_with_user(UserId user_id, std::function<void(SecretChatId)> f);

  string get_user_username(UserId user_id) const;
  string get_channel_username(ChannelId channel_id) const;
  string get_secret_chat_username(SecretChatId secret_chat_id) const;

  int32 get_secret_chat_date(SecretChatId secret_chat_id) const;
  int32 get_secret_chat_ttl(SecretChatId secret_chat_id) const;
  UserId get_secret_chat_user_id(SecretChatId secret_chat_id) const;
  bool get_secret_chat_is_outbound(SecretChatId secret_chat_id) const;
  SecretChatState get_secret_chat_state(SecretChatId secret_chat_id) const;
  int32 get_secret_chat_layer(SecretChatId secret_chat_id) const;
  FolderId get_secret_chat_initial_folder_id(SecretChatId secret_chat_id) const;

  void on_imported_contacts(int64 random_id, vector<UserId> imported_contact_user_ids,
                            vector<int32> unimported_contact_invites);

  void on_deleted_contacts(const vector<UserId> &deleted_contact_user_ids);

  void on_get_contacts(tl_object_ptr<telegram_api::contacts_Contacts> &&new_contacts);

  void on_get_contacts_failed(Status error);

  void on_get_contacts_statuses(vector<tl_object_ptr<telegram_api::contactStatus>> &&statuses);

  void reload_contacts(bool force);

  void on_get_user(tl_object_ptr<telegram_api::User> &&user, const char *source, bool is_me = false,
                   bool expect_support = false);
  void on_get_users(vector<tl_object_ptr<telegram_api::User>> &&users, const char *source);

  void on_binlog_user_event(BinlogEvent &&event);
  void on_binlog_chat_event(BinlogEvent &&event);
  void on_binlog_channel_event(BinlogEvent &&event);
  void on_binlog_secret_chat_event(BinlogEvent &&event);

  void on_get_user_full(tl_object_ptr<telegram_api::userFull> &&user);

  void on_get_user_photos(UserId user_id, int32 offset, int32 limit, int32 total_count,
                          vector<tl_object_ptr<telegram_api::Photo>> photos);

  void on_get_chat(tl_object_ptr<telegram_api::Chat> &&chat, const char *source);
  void on_get_chats(vector<tl_object_ptr<telegram_api::Chat>> &&chats, const char *source);

  void on_get_chat_full(tl_object_ptr<telegram_api::ChatFull> &&chat_full, Promise<Unit> &&promise);

  void on_update_profile_success(int32 flags, const string &first_name, const string &last_name, const string &about);
  void on_set_bot_commands_success(vector<std::pair<string, string>> &&commands);

  void on_update_user_name(UserId user_id, string &&first_name, string &&last_name, string &&username);
  void on_update_user_phone_number(UserId user_id, string &&phone_number);
  void on_update_user_photo(UserId user_id, tl_object_ptr<telegram_api::UserProfilePhoto> &&photo_ptr);
  void on_update_user_online(UserId user_id, tl_object_ptr<telegram_api::UserStatus> &&status);
  void on_update_user_local_was_online(UserId user_id, int32 local_was_online);
  void on_update_user_is_blocked(UserId user_id, bool is_blocked);
  void on_update_user_common_chat_count(UserId user_id, int32 common_chat_count);
  void on_update_user_need_phone_number_privacy_exception(UserId user_id, bool need_phone_number_privacy_exception);

  void on_change_profile_photo(tl_object_ptr<telegram_api::photos_photo> &&photo, int64 old_photo_id);
  void on_delete_profile_photo(int64 profile_photo_id, Promise<Unit> promise);

  void on_ignored_restriction_reasons_changed();

  void on_get_chat_participants(tl_object_ptr<telegram_api::ChatParticipants> &&participants, bool from_update);
  void on_update_chat_add_user(ChatId chat_id, UserId inviter_user_id, UserId user_id, int32 date, int32 version);
  void on_update_chat_description(ChatId chat_id, string &&description);
  void on_update_chat_edit_administrator(ChatId chat_id, UserId user_id, bool is_administrator, int32 version);
  void on_update_chat_delete_user(ChatId chat_id, UserId user_id, int32 version);
  void on_update_chat_default_permissions(ChatId chat_id, RestrictedRights default_permissions, int32 version);
  void on_update_chat_pinned_message(ChatId chat_id, MessageId pinned_message_id, int32 version);

  void on_update_channel_username(ChannelId channel_id, string &&username);
  void on_update_channel_description(ChannelId channel_id, string &&description);
  void on_update_channel_sticker_set(ChannelId channel_id, StickerSetId sticker_set_id);
  void on_update_channel_linked_channel_id(ChannelId channel_id, ChannelId group_channel_id);
  void on_update_channel_location(ChannelId channel_id, const DialogLocation &location);
  void on_update_channel_slow_mode_delay(ChannelId channel_id, int32 slow_mode_delay, Promise<Unit> &&promise);
  void on_update_channel_slow_mode_next_send_date(ChannelId channel_id, int32 slow_mode_next_send_date);
  void on_update_channel_is_all_history_available(ChannelId channel_id, bool is_all_history_available,
                                                  Promise<Unit> &&promise);
  void on_update_channel_default_permissions(ChannelId channel_id, RestrictedRights default_permissions);
  void on_update_channel_administrator_count(ChannelId channel_id, int32 administrator_count);

  void on_update_bot_stopped(UserId user_id, int32 date, bool is_stopped);
  void on_update_chat_participant(ChatId chat_id, UserId user_id, int32 date, DialogInviteLink invite_link,
                                  tl_object_ptr<telegram_api::ChatParticipant> old_participant,
                                  tl_object_ptr<telegram_api::ChatParticipant> new_participant);
  void on_update_channel_participant(ChannelId channel_id, UserId user_id, int32 date, DialogInviteLink invite_link,
                                     tl_object_ptr<telegram_api::ChannelParticipant> old_participant,
                                     tl_object_ptr<telegram_api::ChannelParticipant> new_participant);

  int32 on_update_peer_located(vector<tl_object_ptr<telegram_api::PeerLocated>> &&peers, bool from_update);

  void on_update_dialog_administrators(DialogId dialog_id, vector<DialogAdministrator> &&administrators,
                                       bool have_access, bool from_database);

  void speculative_add_channel_participants(ChannelId channel_id, const vector<UserId> &added_user_ids,
                                            UserId inviter_user_id, int32 date, bool by_me);

  void speculative_delete_channel_participant(ChannelId channel_id, UserId deleted_user_id, bool by_me);

  void invalidate_channel_full(ChannelId channel_id, bool need_drop_invite_link, bool need_drop_slow_mode_delay);

  bool on_get_channel_error(ChannelId channel_id, const Status &status, const string &source);

  void on_get_permanent_dialog_invite_link(DialogId dialog_id, const DialogInviteLink &invite_link);

  void on_get_dialog_invite_link_info(const string &invite_link,
                                      tl_object_ptr<telegram_api::ChatInvite> &&chat_invite_ptr,
                                      Promise<Unit> &&promise);

  void invalidate_invite_link_info(const string &invite_link);

  void on_get_created_public_channels(PublicDialogType type, vector<tl_object_ptr<telegram_api::Chat>> &&chats);

  void on_get_dialogs_for_discussion(vector<tl_object_ptr<telegram_api::Chat>> &&chats);

  void on_get_inactive_channels(vector<tl_object_ptr<telegram_api::Chat>> &&chats);

  void remove_inactive_channel(ChannelId channel_id);

  UserId get_my_id() const;

  void set_my_online_status(bool is_online, bool send_update, bool is_local);

  struct MyOnlineStatusInfo {
    bool is_online_local = false;
    bool is_online_remote = false;
    int32 was_online_local = 0;
    int32 was_online_remote = 0;
  };

  MyOnlineStatusInfo get_my_online_status() const;

  static UserId get_service_notifications_user_id();

  UserId add_service_notifications_user();

  static UserId get_replies_bot_user_id();

  static UserId get_anonymous_bot_user_id();

  UserId add_anonymous_bot_user();

  void on_update_online_status_privacy();

  void on_update_phone_number_privacy();

  void invalidate_user_full(UserId user_id);

  void on_channel_unban_timeout(ChannelId channel_id);

  void check_dialog_username(DialogId dialog_id, const string &username, Promise<CheckDialogUsernameResult> &&promise);

  static td_api::object_ptr<td_api::CheckChatUsernameResult> get_check_chat_username_result_object(
      CheckDialogUsernameResult result);

  void set_account_ttl(int32 account_ttl, Promise<Unit> &&promise) const;
  void get_account_ttl(Promise<int32> &&promise) const;

  static td_api::object_ptr<td_api::session> convert_authorization_object(
      tl_object_ptr<telegram_api::authorization> &&authorization);

  void confirm_qr_code_authentication(string link, Promise<td_api::object_ptr<td_api::session>> &&promise);

  void get_active_sessions(Promise<tl_object_ptr<td_api::sessions>> &&promise) const;
  void terminate_session(int64 session_id, Promise<Unit> &&promise) const;
  void terminate_all_other_sessions(Promise<Unit> &&promise) const;

  void get_connected_websites(Promise<tl_object_ptr<td_api::connectedWebsites>> &&promise) const;
  void disconnect_website(int64 authorizations_id, Promise<Unit> &&promise) const;
  void disconnect_all_websites(Promise<Unit> &&promise) const;

  void add_contact(td_api::object_ptr<td_api::contact> &&contact, bool share_phone_number, Promise<Unit> &&promise);

  std::pair<vector<UserId>, vector<int32>> import_contacts(const vector<tl_object_ptr<td_api::contact>> &contacts,
                                                           int64 &random_id, Promise<Unit> &&promise);

  std::pair<int32, vector<UserId>> search_contacts(const string &query, int32 limit, Promise<Unit> &&promise);

  void remove_contacts(vector<UserId> user_ids, Promise<Unit> &&promise);

  void remove_contacts_by_phone_number(vector<string> user_phone_numbers, vector<UserId> user_ids,
                                       Promise<Unit> &&promise);

  int32 get_imported_contact_count(Promise<Unit> &&promise);

  std::pair<vector<UserId>, vector<int32>> change_imported_contacts(vector<tl_object_ptr<td_api::contact>> &&contacts,
                                                                    int64 &random_id, Promise<Unit> &&promise);

  void clear_imported_contacts(Promise<Unit> &&promise);

  void on_update_contacts_reset();

  void share_phone_number(UserId user_id, Promise<Unit> &&promise);

  void search_dialogs_nearby(const Location &location, Promise<td_api::object_ptr<td_api::chatsNearby>> &&promise);

  void set_location(const Location &location, Promise<Unit> &&promise);

  void set_location_visibility();

  FileId get_profile_photo_file_id(int64 photo_id) const;

  void set_profile_photo(const td_api::object_ptr<td_api::InputChatPhoto> &input_photo, Promise<Unit> &&promise);

  void send_update_profile_photo_query(FileId file_id, int64 old_photo_id, Promise<Unit> &&promise);

  void delete_profile_photo(int64 profile_photo_id, Promise<Unit> &&promise);

  void set_name(const string &first_name, const string &last_name, Promise<Unit> &&promise);

  void set_bio(const string &bio, Promise<Unit> &&promise);

  void set_username(const string &username, Promise<Unit> &&promise);

  void set_commands(vector<td_api::object_ptr<td_api::botCommand>> &&commands, Promise<Unit> &&promise);

  void set_chat_description(ChatId chat_id, const string &description, Promise<Unit> &&promise);

  void set_channel_username(ChannelId channel_id, const string &username, Promise<Unit> &&promise);

  void set_channel_sticker_set(ChannelId channel_id, StickerSetId sticker_set_id, Promise<Unit> &&promise);

  void toggle_channel_sign_messages(ChannelId channel_id, bool sign_messages, Promise<Unit> &&promise);

  void toggle_channel_is_all_history_available(ChannelId channel_id, bool is_all_history_available,
                                               Promise<Unit> &&promise);

  void convert_channel_to_gigagroup(ChannelId channel_id, Promise<Unit> &&promise);

  void set_channel_description(ChannelId channel_id, const string &description, Promise<Unit> &&promise);

  void set_channel_discussion_group(DialogId dialog_id, DialogId discussion_dialog_id, Promise<Unit> &&promise);

  void set_channel_location(DialogId dialog_id, const DialogLocation &location, Promise<Unit> &&promise);

  void set_channel_slow_mode_delay(DialogId dialog_id, int32 slow_mode_delay, Promise<Unit> &&promise);

  void report_channel_spam(ChannelId channel_id, UserId user_id, const vector<MessageId> &message_ids,
                           Promise<Unit> &&promise);

  void delete_dialog(DialogId dialog_id, Promise<Unit> &&promise);

  void get_channel_statistics(DialogId dialog_id, bool is_dark,
                              Promise<td_api::object_ptr<td_api::ChatStatistics>> &&promise);

  bool can_get_channel_message_statistics(DialogId dialog_id) const;

  void get_channel_message_statistics(FullMessageId full_message_id, bool is_dark,
                                      Promise<td_api::object_ptr<td_api::messageStatistics>> &&promise);

  void load_statistics_graph(DialogId dialog_id, const string &token, int64 x,
                             Promise<td_api::object_ptr<td_api::StatisticalGraph>> &&promise);

  void can_transfer_ownership(Promise<CanTransferOwnershipResult> &&promise);

  static td_api::object_ptr<td_api::CanTransferOwnershipResult> get_can_transfer_ownership_result_object(
      CanTransferOwnershipResult result);

  void transfer_dialog_ownership(DialogId dialog_id, UserId user_id, const string &password, Promise<Unit> &&promise);

  void export_dialog_invite_link(DialogId dialog_id, int32 expire_date, int32 usage_limit, bool is_permanent,
                                 Promise<td_api::object_ptr<td_api::chatInviteLink>> &&promise);

  void edit_dialog_invite_link(DialogId dialog_id, const string &link, int32 expire_date, int32 usage_limit,
                               Promise<td_api::object_ptr<td_api::chatInviteLink>> &&promise);

  void get_dialog_invite_link(DialogId dialog_id, const string &invite_link,
                              Promise<td_api::object_ptr<td_api::chatInviteLink>> &&promise);

  void get_dialog_invite_link_counts(DialogId dialog_id,
                                     Promise<td_api::object_ptr<td_api::chatInviteLinkCounts>> &&promise);

  void get_dialog_invite_links(DialogId dialog_id, UserId creator_user_id, bool is_revoked, int32 offset_date,
                               const string &offset_invite_link, int32 limit,
                               Promise<td_api::object_ptr<td_api::chatInviteLinks>> &&promise);

  void get_dialog_invite_link_users(DialogId dialog_id, const string &invite_link,
                                    td_api::object_ptr<td_api::chatInviteLinkMember> offset_member, int32 limit,
                                    Promise<td_api::object_ptr<td_api::chatInviteLinkMembers>> &&promise);

  void revoke_dialog_invite_link(DialogId dialog_id, const string &link,
                                 Promise<td_api::object_ptr<td_api::chatInviteLinks>> &&promise);

  void delete_revoked_dialog_invite_link(DialogId dialog_id, const string &invite_link, Promise<Unit> &&promise);

  void delete_all_revoked_dialog_invite_links(DialogId dialog_id, UserId creator_user_id, Promise<Unit> &&promise);

  void check_dialog_invite_link(const string &invite_link, Promise<Unit> &&promise) const;

  void import_dialog_invite_link(const string &invite_link, Promise<DialogId> &&promise);

  ChannelId migrate_chat_to_megagroup(ChatId chat_id, Promise<Unit> &promise);

  vector<DialogId> get_created_public_dialogs(PublicDialogType type, Promise<Unit> &&promise);

  void check_created_public_dialogs_limit(PublicDialogType type, Promise<Unit> &&promise);

  vector<DialogId> get_dialogs_for_discussion(Promise<Unit> &&promise);

  vector<DialogId> get_inactive_channels(Promise<Unit> &&promise);

  void dismiss_suggested_action(SuggestedAction action, Promise<Unit> &&promise);

  bool is_user_contact(UserId user_id, bool is_mutual = false) const;

  bool is_user_deleted(UserId user_id) const;

  bool is_user_support(UserId user_id) const;

  bool is_user_bot(UserId user_id) const;
  Result<BotData> get_bot_data(UserId user_id) const TD_WARN_UNUSED_RESULT;

  bool is_user_online(UserId user_id, int32 tolerance = 0) const;

  bool is_user_status_exact(UserId user_id) const;

  bool can_report_user(UserId user_id) const;

  bool have_user(UserId user_id) const;
  bool have_min_user(UserId user_id) const;
  bool have_user_force(UserId user_id);

  bool is_dialog_info_received_from_server(DialogId dialog_id) const;

  void reload_dialog_info(DialogId dialog_id, Promise<Unit> &&promise);

  static void send_get_me_query(Td *td, Promise<Unit> &&promise);
  UserId get_me(Promise<Unit> &&promise);
  bool get_user(UserId user_id, int left_tries, Promise<Unit> &&promise);
  void reload_user(UserId user_id, Promise<Unit> &&promise);
  bool load_user_full(UserId user_id, bool force, Promise<Unit> &&promise);
  void reload_user_full(UserId user_id);

  std::pair<int32, vector<const Photo *>> get_user_profile_photos(UserId user_id, int32 offset, int32 limit,
                                                                  Promise<Unit> &&promise);
  void reload_user_profile_photo(UserId user_id, int64 photo_id, Promise<Unit> &&promise);
  FileSourceId get_user_profile_photo_file_source_id(UserId user_id, int64 photo_id);

  bool have_chat(ChatId chat_id) const;
  bool have_chat_force(ChatId chat_id);
  bool get_chat(ChatId chat_id, int left_tries, Promise<Unit> &&promise);
  void reload_chat(ChatId chat_id, Promise<Unit> &&promise);
  bool load_chat_full(ChatId chat_id, bool force, Promise<Unit> &&promise, const char *source);
  FileSourceId get_chat_full_file_source_id(ChatId chat_id);
  void reload_chat_full(ChatId chat_id, Promise<Unit> &&promise);

  bool get_chat_is_active(ChatId chat_id) const;
  ChannelId get_chat_migrated_to_channel_id(ChatId chat_id) const;
  DialogParticipantStatus get_chat_status(ChatId chat_id) const;
  DialogParticipantStatus get_chat_permissions(ChatId chat_id) const;
  bool is_appointed_chat_administrator(ChatId chat_id) const;

  bool have_channel(ChannelId channel_id) const;
  bool have_min_channel(ChannelId channel_id) const;
  bool have_channel_force(ChannelId channel_id);
  bool get_channel(ChannelId channel_id, int left_tries, Promise<Unit> &&promise);
  void reload_channel(ChannelId chnanel_id, Promise<Unit> &&promise);
  bool load_channel_full(ChannelId channel_id, bool force, Promise<Unit> &&promise);
  FileSourceId get_channel_full_file_source_id(ChannelId channel_id);
  void reload_channel_full(ChannelId channel_id, Promise<Unit> &&promise, const char *source);

  bool is_channel_public(ChannelId channel_id) const;

  bool have_secret_chat(SecretChatId secret_chat_id) const;
  bool have_secret_chat_force(SecretChatId secret_chat_id);
  bool get_secret_chat(SecretChatId secret_chat_id, bool force, Promise<Unit> &&promise);
  bool get_secret_chat_full(SecretChatId secret_chat_id, Promise<Unit> &&promise);

  ChannelType get_channel_type(ChannelId channel_id) const;
  int32 get_channel_date(ChannelId channel_id) const;
  DialogParticipantStatus get_channel_status(ChannelId channel_id) const;
  DialogParticipantStatus get_channel_permissions(ChannelId channel_id) const;
  int32 get_channel_participant_count(ChannelId channel_id) const;
  bool get_channel_sign_messages(ChannelId channel_id) const;
  bool get_channel_has_linked_channel(ChannelId channel_id) const;
  ChannelId get_channel_linked_channel_id(ChannelId channel_id);
  int32 get_channel_slow_mode_delay(ChannelId channel_id);

  void add_dialog_participant(DialogId dialog_id, UserId user_id, int32 forward_limit, Promise<Unit> &&promise);

  void add_dialog_participants(DialogId dialog_id, const vector<UserId> &user_ids, Promise<Unit> &&promise);

  void set_dialog_participant_status(DialogId dialog_id, UserId user_id,
                                     const tl_object_ptr<td_api::ChatMemberStatus> &chat_member_status,
                                     Promise<Unit> &&promise);

  void ban_dialog_participant(DialogId dialog_id, UserId user_id, int32 banned_until_date, bool revoke_messages,
                              Promise<Unit> &&promise);

  DialogParticipant get_dialog_participant(DialogId dialog_id, UserId user_id, int64 &random_id, bool force,
                                           Promise<Unit> &&promise);

  void search_dialog_participants(DialogId dialog_id, const string &query, int32 limit, DialogParticipantsFilter filter,
                                  bool without_bot_info, Promise<DialogParticipants> &&promise);

  vector<DialogAdministrator> get_dialog_administrators(DialogId dialog_id, int left_tries, Promise<Unit> &&promise);

  void get_channel_participants(ChannelId channel_id, tl_object_ptr<td_api::SupergroupMembersFilter> &&filter,
                                string additional_query, int32 offset, int32 limit, int32 additional_limit,
                                bool without_bot_info, Promise<DialogParticipants> &&promise);

  int32 get_user_id_object(UserId user_id, const char *source) const;

  tl_object_ptr<td_api::user> get_user_object(UserId user_id) const;

  vector<int32> get_user_ids_object(const vector<UserId> &user_ids, const char *source) const;

  tl_object_ptr<td_api::users> get_users_object(int32 total_count, const vector<UserId> &user_ids) const;

  tl_object_ptr<td_api::userFullInfo> get_user_full_info_object(UserId user_id) const;

  int32 get_basic_group_id_object(ChatId chat_id, const char *source) const;

  tl_object_ptr<td_api::basicGroup> get_basic_group_object(ChatId chat_id);

  tl_object_ptr<td_api::basicGroupFullInfo> get_basic_group_full_info_object(ChatId chat_id) const;

  int32 get_supergroup_id_object(ChannelId channel_id, const char *source) const;

  tl_object_ptr<td_api::supergroup> get_supergroup_object(ChannelId channel_id) const;

  tl_object_ptr<td_api::supergroupFullInfo> get_supergroup_full_info_object(ChannelId channel_id) const;

  int32 get_secret_chat_id_object(SecretChatId secret_chat_id, const char *source) const;

  tl_object_ptr<td_api::secretChat> get_secret_chat_object(SecretChatId secret_chat_id);

  void on_update_secret_chat(SecretChatId secret_chat_id, int64 access_hash, UserId user_id, SecretChatState state,
                             bool is_outbound, int32 ttl, int32 date, string key_hash, int32 layer,
                             FolderId initial_folder_id);

  tl_object_ptr<td_api::chatMember> get_chat_member_object(const DialogParticipant &dialog_participant) const;

  tl_object_ptr<td_api::chatInviteLinkInfo> get_chat_invite_link_info_object(const string &invite_link) const;

  UserId get_support_user(Promise<Unit> &&promise);

  void repair_chat_participants(ChatId chat_id);

  void after_get_difference();

  void get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const;

  static tl_object_ptr<td_api::dateRange> convert_date_range(
      const tl_object_ptr<telegram_api::statsDateRangeDays> &obj);

  static tl_object_ptr<td_api::StatisticalGraph> convert_stats_graph(tl_object_ptr<telegram_api::StatsGraph> obj);

  static double get_percentage_value(double new_value, double old_value);

  static tl_object_ptr<td_api::statisticalValue> convert_stats_absolute_value(
      const tl_object_ptr<telegram_api::statsAbsValueAndPrev> &obj);

  tl_object_ptr<td_api::chatStatisticsSupergroup> convert_megagroup_stats(
      tl_object_ptr<telegram_api::stats_megagroupStats> obj);

  static tl_object_ptr<td_api::chatStatisticsChannel> convert_broadcast_stats(
      tl_object_ptr<telegram_api::stats_broadcastStats> obj);

  static tl_object_ptr<td_api::messageStatistics> convert_message_stats(
      tl_object_ptr<telegram_api::stats_messageStats> obj);

 private:
  struct User {
    string first_name;
    string last_name;
    string username;
    string phone_number;
    int64 access_hash = -1;

    ProfilePhoto photo;

    vector<RestrictionReason> restriction_reasons;
    string inline_query_placeholder;
    int32 bot_info_version = -1;

    int32 was_online = 0;
    int32 local_was_online = 0;

    string language_code;

    std::unordered_set<int64> photo_ids;

    std::unordered_map<DialogId, int32, DialogIdHash> online_member_dialogs;  // id -> time

    static constexpr uint32 CACHE_VERSION = 4;
    uint32 cache_version = 0;

    bool is_min_access_hash = true;
    bool is_received = false;
    bool is_verified = false;
    bool is_support = false;
    bool is_deleted = true;
    bool is_bot = true;
    bool can_join_groups = true;
    bool can_read_all_group_messages = true;
    bool is_inline_bot = false;
    bool need_location_bot = false;
    bool is_scam = false;
    bool is_fake = false;
    bool is_contact = false;
    bool is_mutual_contact = false;
    bool need_apply_min_photo = false;

    bool is_photo_inited = false;

    bool is_repaired = false;  // whether cached value is rechecked

    bool is_name_changed = true;
    bool is_username_changed = true;
    bool is_photo_changed = true;
    bool is_is_contact_changed = true;
    bool is_is_deleted_changed = true;
    bool is_default_permissions_changed = true;
    bool is_changed = true;             // have new changes that need to be sent to the client and database
    bool need_save_to_database = true;  // have new changes that need only to be saved to the database
    bool is_status_changed = true;
    bool is_online_status_changed = true;  // whether online/offline has changed
    bool is_update_user_sent = false;

    bool is_saved = false;         // is current user version being saved/is saved to the database
    bool is_being_saved = false;   // is current user being saved to the database
    bool is_status_saved = false;  // is current user status being saved/is saved to the database

    bool is_received_from_server = false;  // true, if the user was received from the server and not the database

    uint64 log_event_id = 0;

    template <class StorerT>
    void store(StorerT &storer) const;

    template <class ParserT>
    void parse(ParserT &parser);
  };

  // do not forget to update drop_user_full and on_get_user_full
  struct UserFull {
    Photo photo;

    string about;

    int32 common_chat_count = 0;

    bool is_blocked = false;
    bool can_be_called = false;
    bool supports_video_calls = false;
    bool has_private_calls = false;
    bool can_pin_messages = true;
    bool need_phone_number_privacy_exception = false;

    bool is_common_chat_count_changed = true;
    bool is_changed = true;             // have new changes that need to be sent to the client and database
    bool need_send_update = true;       // have new changes that need only to be sent to the client
    bool need_save_to_database = true;  // have new changes that need only to be saved to the database

    double expires_at = 0.0;

    bool is_expired() const {
      return expires_at < Time::now();
    }

    template <class StorerT>
    void store(StorerT &storer) const;

    template <class ParserT>
    void parse(ParserT &parser);
  };

  struct Chat {
    string title;
    DialogPhoto photo;
    int32 participant_count = 0;
    int32 date = 0;
    int32 version = -1;
    int32 default_permissions_version = -1;
    int32 pinned_message_version = -1;
    ChannelId migrated_to_channel_id;

    DialogParticipantStatus status = DialogParticipantStatus::Banned(0);
    RestrictedRights default_permissions{false, false, false, false, false, false, false, false, false, false, false};

    static constexpr uint32 CACHE_VERSION = 3;
    uint32 cache_version = 0;

    bool is_active = false;

    bool is_title_changed = true;
    bool is_photo_changed = true;
    bool is_default_permissions_changed = true;
    bool is_is_active_changed = true;
    bool is_changed = true;             // have new changes that need to be sent to the client and database
    bool need_save_to_database = true;  // have new changes that need only to be saved to the database
    bool is_update_basic_group_sent = false;

    bool is_repaired = false;  // whether cached value is rechecked

    bool is_saved = false;        // is current chat version being saved/is saved to the database
    bool is_being_saved = false;  // is current chat being saved to the database

    bool is_received_from_server = false;  // true, if the chat was received from the server and not the database

    uint64 log_event_id = 0;

    template <class StorerT>
    void store(StorerT &storer) const;

    template <class ParserT>
    void parse(ParserT &parser);
  };

  // do not forget to update drop_chat_full and on_get_chat_full
  struct ChatFull {
    int32 version = -1;
    UserId creator_user_id;
    vector<DialogParticipant> participants;

    Photo photo;
    vector<FileId> registered_photo_file_ids;
    FileSourceId file_source_id;

    string description;

    DialogInviteLink invite_link;

    bool can_set_username = false;

    bool is_changed = true;             // have new changes that need to be sent to the client and database
    bool need_send_update = true;       // have new changes that need only to be sent to the client
    bool need_save_to_database = true;  // have new changes that need only to be saved to the database

    template <class StorerT>
    void store(StorerT &storer) const;

    template <class ParserT>
    void parse(ParserT &parser);
  };

  struct Channel {
    int64 access_hash = 0;
    string title;
    DialogPhoto photo;
    string username;
    vector<RestrictionReason> restriction_reasons;
    DialogParticipantStatus status = DialogParticipantStatus::Banned(0);
    RestrictedRights default_permissions{false, false, false, false, false, false, false, false, false, false, false};
    int32 date = 0;
    int32 participant_count = 0;

    static constexpr uint32 CACHE_VERSION = 7;
    uint32 cache_version = 0;

    bool has_linked_channel = false;
    bool has_location = false;
    bool sign_messages = false;
    bool is_slow_mode_enabled = false;

    bool is_megagroup = false;
    bool is_gigagroup = false;
    bool is_verified = false;
    bool is_scam = false;
    bool is_fake = false;

    bool is_title_changed = true;
    bool is_username_changed = true;
    bool is_photo_changed = true;
    bool is_default_permissions_changed = true;
    bool is_status_changed = true;
    bool had_read_access = true;
    bool was_member = false;
    bool is_changed = true;             // have new changes that need to be sent to the client and database
    bool need_save_to_database = true;  // have new changes that need only to be saved to the database
    bool is_update_supergroup_sent = false;

    bool is_repaired = false;  // whether cached value is rechecked

    bool is_saved = false;        // is current channel version being saved/is saved to the database
    bool is_being_saved = false;  // is current channel being saved to the database

    bool is_received_from_server = false;  // true, if the channel was received from the server and not the database

    uint64 log_event_id = 0;

    template <class StorerT>
    void store(StorerT &storer) const;

    template <class ParserT>
    void parse(ParserT &parser);
  };

  // do not forget to update invalidate_channel_full and on_get_chat_full
  struct ChannelFull {
    Photo photo;
    vector<FileId> registered_photo_file_ids;
    FileSourceId file_source_id;

    string description;
    int32 participant_count = 0;
    int32 administrator_count = 0;
    int32 restricted_count = 0;
    int32 banned_count = 0;

    DialogInviteLink invite_link;

    uint32 speculative_version = 1;
    uint32 repair_request_version = 0;

    StickerSetId sticker_set_id;

    ChannelId linked_channel_id;

    DialogLocation location;

    DcId stats_dc_id;

    int32 slow_mode_delay = 0;
    int32 slow_mode_next_send_date = 0;

    MessageId migrated_from_max_message_id;
    ChatId migrated_from_chat_id;

    vector<UserId> bot_user_ids;

    bool can_get_participants = false;
    bool can_set_username = false;
    bool can_set_sticker_set = false;
    bool can_set_location = false;
    bool can_view_statistics = false;
    bool is_can_view_statistics_inited = false;
    bool is_all_history_available = true;

    bool is_slow_mode_next_send_date_changed = true;
    bool is_changed = true;             // have new changes that need to be sent to the client and database
    bool need_send_update = true;       // have new changes that need only to be sent to the client
    bool need_save_to_database = true;  // have new changes that need only to be saved to the database

    double expires_at = 0.0;

    bool is_expired() const {
      return expires_at < Time::now();
    }

    template <class StorerT>
    void store(StorerT &storer) const;

    template <class ParserT>
    void parse(ParserT &parser);
  };

  struct SecretChat {
    int64 access_hash = 0;
    UserId user_id;
    SecretChatState state;
    string key_hash;
    int32 ttl = 0;
    int32 date = 0;
    int32 layer = 0;
    FolderId initial_folder_id;

    bool is_outbound = false;

    bool is_ttl_changed = true;
    bool is_state_changed = true;
    bool is_changed = true;             // have new changes that need to be sent to the client and database
    bool need_save_to_database = true;  // have new changes that need only to be saved to the database

    bool is_saved = false;        // is current secret chat version being saved/is saved to the database
    bool is_being_saved = false;  // is current secret chat being saved to the database

    uint64 log_event_id = 0;

    template <class StorerT>
    void store(StorerT &storer) const;

    template <class ParserT>
    void parse(ParserT &parser);
  };

  struct BotInfo {
    int32 version = -1;
    string description;
    vector<std::pair<string, string>> commands;
    bool is_changed = true;

    template <class StorerT>
    void store(StorerT &storer) const;

    template <class ParserT>
    void parse(ParserT &parser);
  };

  struct InviteLinkInfo {
    // known dialog
    DialogId dialog_id;

    // unknown dialog
    string title;
    Photo photo;
    int32 participant_count = 0;
    vector<UserId> participant_user_ids;
    bool is_chat = false;
    bool is_channel = false;
    bool is_public = false;
    bool is_megagroup = false;
  };

  struct UserPhotos {
    vector<Photo> photos;
    int32 count = -1;
    int32 offset = -1;
    bool getting_now = false;
  };

  struct DialogNearby {
    DialogId dialog_id;
    int32 distance;

    DialogNearby(DialogId dialog_id, int32 distance) : dialog_id(dialog_id), distance(distance) {
    }

    bool operator<(const DialogNearby &other) const {
      return distance < other.distance || (distance == other.distance && dialog_id.get() < other.dialog_id.get());
    }

    bool operator==(const DialogNearby &other) const {
      return distance == other.distance && dialog_id == other.dialog_id;
    }

    bool operator!=(const DialogNearby &other) const {
      return !(*this == other);
    }
  };

  class UserLogEvent;
  class ChatLogEvent;
  class ChannelLogEvent;
  class SecretChatLogEvent;

  static constexpr int32 MAX_GET_PROFILE_PHOTOS = 100;        // server side limit
  static constexpr size_t MAX_NAME_LENGTH = 64;               // server side limit for first/last name
  static constexpr size_t MAX_DESCRIPTION_LENGTH = 255;       // server side limit for chat/channel description
  static constexpr size_t MAX_BIO_LENGTH = 70;                // server side limit
  static constexpr int32 MAX_GET_CHANNEL_PARTICIPANTS = 200;  // server side limit

  static constexpr int32 USER_FLAG_HAS_ACCESS_HASH = 1 << 0;
  static constexpr int32 USER_FLAG_HAS_FIRST_NAME = 1 << 1;
  static constexpr int32 USER_FLAG_HAS_LAST_NAME = 1 << 2;
  static constexpr int32 USER_FLAG_HAS_USERNAME = 1 << 3;
  static constexpr int32 USER_FLAG_HAS_PHONE_NUMBER = 1 << 4;
  static constexpr int32 USER_FLAG_HAS_PHOTO = 1 << 5;
  static constexpr int32 USER_FLAG_HAS_STATUS = 1 << 6;
  static constexpr int32 USER_FLAG_HAS_BOT_INFO_VERSION = 1 << 14;
  static constexpr int32 USER_FLAG_IS_ME = 1 << 10;
  static constexpr int32 USER_FLAG_IS_CONTACT = 1 << 11;
  static constexpr int32 USER_FLAG_IS_MUTUAL_CONTACT = 1 << 12;
  static constexpr int32 USER_FLAG_IS_DELETED = 1 << 13;
  static constexpr int32 USER_FLAG_IS_BOT = 1 << 14;
  static constexpr int32 USER_FLAG_IS_BOT_WITH_PRIVACY_DISABLED = 1 << 15;
  static constexpr int32 USER_FLAG_IS_PRIVATE_BOT = 1 << 16;
  static constexpr int32 USER_FLAG_IS_VERIFIED = 1 << 17;
  static constexpr int32 USER_FLAG_IS_RESTRICTED = 1 << 18;
  static constexpr int32 USER_FLAG_IS_INLINE_BOT = 1 << 19;
  static constexpr int32 USER_FLAG_IS_INACCESSIBLE = 1 << 20;
  static constexpr int32 USER_FLAG_NEED_LOCATION_BOT = 1 << 21;
  static constexpr int32 USER_FLAG_HAS_LANGUAGE_CODE = 1 << 22;
  static constexpr int32 USER_FLAG_IS_SUPPORT = 1 << 23;
  static constexpr int32 USER_FLAG_IS_SCAM = 1 << 24;
  static constexpr int32 USER_FLAG_NEED_APPLY_MIN_PHOTO = 1 << 25;
  static constexpr int32 USER_FLAG_IS_FAKE = 1 << 26;

  static constexpr int32 USER_FULL_FLAG_IS_BLOCKED = 1 << 0;
  static constexpr int32 USER_FULL_FLAG_HAS_ABOUT = 1 << 1;
  static constexpr int32 USER_FULL_FLAG_HAS_PHOTO = 1 << 2;
  static constexpr int32 USER_FULL_FLAG_HAS_BOT_INFO = 1 << 3;
  static constexpr int32 USER_FULL_FLAG_HAS_PINNED_MESSAGE = 1 << 6;
  static constexpr int32 USER_FULL_FLAG_CAN_PIN_MESSAGE = 1 << 7;
  static constexpr int32 USER_FULL_FLAG_HAS_FOLDER_ID = 1 << 11;
  static constexpr int32 USER_FULL_FLAG_HAS_SCHEDULED_MESSAGES = 1 << 12;
  static constexpr int32 USER_FULL_FLAG_HAS_MESSAGE_TTL = 1 << 14;

  static constexpr int32 CHAT_FLAG_USER_IS_CREATOR = 1 << 0;
  static constexpr int32 CHAT_FLAG_USER_WAS_KICKED = 1 << 1;
  static constexpr int32 CHAT_FLAG_USER_HAS_LEFT = 1 << 2;
  // static constexpr int32 CHAT_FLAG_ADMINISTRATORS_ENABLED = 1 << 3;
  // static constexpr int32 CHAT_FLAG_IS_ADMINISTRATOR = 1 << 4;
  static constexpr int32 CHAT_FLAG_IS_DEACTIVATED = 1 << 5;
  static constexpr int32 CHAT_FLAG_WAS_MIGRATED = 1 << 6;
  static constexpr int32 CHAT_FLAG_HAS_ACTIVE_GROUP_CALL = 1 << 23;
  static constexpr int32 CHAT_FLAG_IS_GROUP_CALL_NON_EMPTY = 1 << 24;

  static constexpr int32 CHAT_FULL_FLAG_HAS_PINNED_MESSAGE = 1 << 6;
  static constexpr int32 CHAT_FULL_FLAG_HAS_SCHEDULED_MESSAGES = 1 << 8;
  static constexpr int32 CHAT_FULL_FLAG_HAS_FOLDER_ID = 1 << 11;
  static constexpr int32 CHAT_FULL_FLAG_HAS_ACTIVE_GROUP_CALL = 1 << 12;
  static constexpr int32 CHAT_FULL_FLAG_HAS_MESSAGE_TTL = 1 << 14;

  static constexpr int32 CHANNEL_FLAG_USER_IS_CREATOR = 1 << 0;
  static constexpr int32 CHANNEL_FLAG_USER_HAS_LEFT = 1 << 2;
  static constexpr int32 CHANNEL_FLAG_IS_BROADCAST = 1 << 5;
  static constexpr int32 CHANNEL_FLAG_IS_PUBLIC = 1 << 6;
  static constexpr int32 CHANNEL_FLAG_IS_VERIFIED = 1 << 7;
  static constexpr int32 CHANNEL_FLAG_IS_MEGAGROUP = 1 << 8;
  static constexpr int32 CHANNEL_FLAG_IS_RESTRICTED = 1 << 9;
  // static constexpr int32 CHANNEL_FLAG_ANYONE_CAN_INVITE = 1 << 10;
  static constexpr int32 CHANNEL_FLAG_SIGN_MESSAGES = 1 << 11;
  static constexpr int32 CHANNEL_FLAG_IS_MIN = 1 << 12;
  static constexpr int32 CHANNEL_FLAG_HAS_ACCESS_HASH = 1 << 13;
  static constexpr int32 CHANNEL_FLAG_HAS_ADMIN_RIGHTS = 1 << 14;
  static constexpr int32 CHANNEL_FLAG_HAS_BANNED_RIGHTS = 1 << 15;
  static constexpr int32 CHANNEL_FLAG_HAS_UNBAN_DATE = 1 << 16;
  static constexpr int32 CHANNEL_FLAG_HAS_PARTICIPANT_COUNT = 1 << 17;
  static constexpr int32 CHANNEL_FLAG_IS_SCAM = 1 << 19;
  static constexpr int32 CHANNEL_FLAG_HAS_LINKED_CHAT = 1 << 20;
  static constexpr int32 CHANNEL_FLAG_HAS_LOCATION = 1 << 21;
  static constexpr int32 CHANNEL_FLAG_IS_SLOW_MODE_ENABLED = 1 << 22;
  static constexpr int32 CHANNEL_FLAG_HAS_ACTIVE_GROUP_CALL = 1 << 23;
  static constexpr int32 CHANNEL_FLAG_IS_GROUP_CALL_NON_EMPTY = 1 << 24;
  static constexpr int32 CHANNEL_FLAG_IS_FAKE = 1 << 25;
  static constexpr int32 CHANNEL_FLAG_IS_GIGAGROUP = 1 << 26;

  static constexpr int32 CHANNEL_FULL_FLAG_HAS_PARTICIPANT_COUNT = 1 << 0;
  static constexpr int32 CHANNEL_FULL_FLAG_HAS_ADMINISTRATOR_COUNT = 1 << 1;
  static constexpr int32 CHANNEL_FULL_FLAG_HAS_BANNED_COUNT = 1 << 2;
  static constexpr int32 CHANNEL_FULL_FLAG_CAN_GET_PARTICIPANTS = 1 << 3;
  static constexpr int32 CHANNEL_FULL_FLAG_MIGRATED_FROM = 1 << 4;
  static constexpr int32 CHANNEL_FULL_FLAG_HAS_PINNED_MESSAGE = 1 << 5;
  static constexpr int32 CHANNEL_FULL_FLAG_CAN_SET_USERNAME = 1 << 6;
  static constexpr int32 CHANNEL_FULL_FLAG_CAN_SET_STICKER_SET = 1 << 7;
  static constexpr int32 CHANNEL_FULL_FLAG_HAS_STICKER_SET = 1 << 8;
  static constexpr int32 CHANNEL_FULL_FLAG_HAS_AVAILABLE_MIN_MESSAGE_ID = 1 << 9;
  static constexpr int32 CHANNEL_FULL_FLAG_IS_ALL_HISTORY_HIDDEN = 1 << 10;
  static constexpr int32 CHANNEL_FULL_FLAG_HAS_FOLDER_ID = 1 << 11;
  static constexpr int32 CHANNEL_FULL_FLAG_HAS_STATISTICS_DC_ID = 1 << 12;
  static constexpr int32 CHANNEL_FULL_FLAG_HAS_ONLINE_MEMBER_COUNT = 1 << 13;
  static constexpr int32 CHANNEL_FULL_FLAG_HAS_LINKED_CHANNEL_ID = 1 << 14;
  static constexpr int32 CHANNEL_FULL_FLAG_HAS_LOCATION = 1 << 15;
  static constexpr int32 CHANNEL_FULL_FLAG_CAN_SET_LOCATION = 1 << 16;
  static constexpr int32 CHANNEL_FULL_FLAG_HAS_SLOW_MODE_DELAY = 1 << 17;
  static constexpr int32 CHANNEL_FULL_FLAG_HAS_SLOW_MODE_NEXT_SEND_DATE = 1 << 18;
  static constexpr int32 CHANNEL_FULL_FLAG_HAS_SCHEDULED_MESSAGES = 1 << 19;
  static constexpr int32 CHANNEL_FULL_FLAG_CAN_VIEW_STATISTICS = 1 << 20;
  static constexpr int32 CHANNEL_FULL_FLAG_HAS_ACTIVE_GROUP_CALL = 1 << 21;
  static constexpr int32 CHANNEL_FULL_FLAG_IS_BLOCKED = 1 << 22;
  static constexpr int32 CHANNEL_FULL_FLAG_HAS_EXPORTED_INVITE = 1 << 23;
  static constexpr int32 CHANNEL_FULL_FLAG_HAS_MESSAGE_TTL = 1 << 24;

  static constexpr int32 CHAT_INVITE_FLAG_IS_CHANNEL = 1 << 0;
  static constexpr int32 CHAT_INVITE_FLAG_IS_BROADCAST = 1 << 1;
  static constexpr int32 CHAT_INVITE_FLAG_IS_PUBLIC = 1 << 2;
  static constexpr int32 CHAT_INVITE_FLAG_IS_MEGAGROUP = 1 << 3;
  static constexpr int32 CHAT_INVITE_FLAG_HAS_USERS = 1 << 4;

  static constexpr int32 USER_FULL_EXPIRE_TIME = 60;
  static constexpr int32 CHANNEL_FULL_EXPIRE_TIME = 60;

  static constexpr int32 ACCOUNT_UPDATE_FIRST_NAME = 1 << 0;
  static constexpr int32 ACCOUNT_UPDATE_LAST_NAME = 1 << 1;
  static constexpr int32 ACCOUNT_UPDATE_ABOUT = 1 << 2;

  static bool have_input_peer_user(const User *u, AccessRights access_rights);
  static bool have_input_peer_chat(const Chat *c, AccessRights access_rights);
  bool have_input_peer_channel(const Channel *c, ChannelId channel_id, AccessRights access_rights,
                               bool from_linked = false) const;
  static bool have_input_encrypted_peer(const SecretChat *secret_chat, AccessRights access_rights);

  const User *get_user(UserId user_id) const;
  User *get_user(UserId user_id);
  User *get_user_force(UserId user_id);
  User *get_user_force_impl(UserId user_id);

  User *add_user(UserId user_id, const char *source);

  const UserFull *get_user_full(UserId user_id) const;
  UserFull *get_user_full(UserId user_id);
  UserFull *get_user_full_force(UserId user_id);

  UserFull *add_user_full(UserId user_id);

  void send_get_user_full_query(UserId user_id, tl_object_ptr<telegram_api::InputUser> &&input_user,
                                Promise<Unit> &&promise, const char *source);

  const BotInfo *get_bot_info(UserId user_id) const;
  BotInfo *get_bot_info(UserId user_id);
  BotInfo *get_bot_info_force(UserId user_id, bool send_update = true);

  BotInfo *add_bot_info(UserId user_id);

  const Chat *get_chat(ChatId chat_id) const;
  Chat *get_chat(ChatId chat_id);
  Chat *get_chat_force(ChatId chat_id);

  Chat *add_chat(ChatId chat_id);

  const ChatFull *get_chat_full(ChatId chat_id) const;
  ChatFull *get_chat_full(ChatId chat_id);
  ChatFull *get_chat_full_force(ChatId chat_id, const char *source);

  ChatFull *add_chat_full(ChatId chat_id);

  void send_get_chat_full_query(ChatId chat_id, Promise<Unit> &&promise, const char *source);

  const Channel *get_channel(ChannelId channel_id) const;
  Channel *get_channel(ChannelId channel_id);
  Channel *get_channel_force(ChannelId channel_id);

  Channel *add_channel(ChannelId channel_id, const char *source);

  const ChannelFull *get_channel_full(ChannelId channel_id) const;
  const ChannelFull *get_channel_full_const(ChannelId channel_id) const;
  ChannelFull *get_channel_full(ChannelId channel_id, const char *source);
  ChannelFull *get_channel_full_force(ChannelId channel_id, const char *source);

  ChannelFull *add_channel_full(ChannelId channel_id);

  void send_get_channel_full_query(ChannelFull *channel_full, ChannelId channel_id, Promise<Unit> &&promise,
                                   const char *source);

  const SecretChat *get_secret_chat(SecretChatId secret_chat_id) const;
  SecretChat *get_secret_chat(SecretChatId secret_chat_id);
  SecretChat *get_secret_chat_force(SecretChatId secret_chat_id);

  SecretChat *add_secret_chat(SecretChatId secret_chat_id);

  static DialogParticipantStatus get_chat_status(const Chat *c);
  DialogParticipantStatus get_chat_permissions(const Chat *c) const;

  static ChannelType get_channel_type(const Channel *c);
  static DialogParticipantStatus get_channel_status(const Channel *c);
  DialogParticipantStatus get_channel_permissions(const Channel *c) const;
  static bool get_channel_sign_messages(const Channel *c);
  static bool get_channel_has_linked_channel(const Channel *c);

  void set_my_id(UserId my_id);

  static bool is_valid_username(const string &username);

  bool on_update_bot_info(tl_object_ptr<telegram_api::botInfo> &&new_bot_info, bool send_update = true);
  bool is_bot_info_expired(UserId user_id, int32 bot_info_version);

  void on_update_user_name(User *u, UserId user_id, string &&first_name, string &&last_name, string &&username);
  void on_update_user_phone_number(User *u, UserId user_id, string &&phone_number);
  void on_update_user_photo(User *u, UserId user_id, tl_object_ptr<telegram_api::UserProfilePhoto> &&photo,
                            const char *source);
  void on_update_user_is_contact(User *u, UserId user_id, bool is_contact, bool is_mutual_contact);
  void on_update_user_online(User *u, UserId user_id, tl_object_ptr<telegram_api::UserStatus> &&status);
  void on_update_user_local_was_online(User *u, UserId user_id, int32 local_was_online);

  void do_update_user_photo(User *u, UserId user_id, tl_object_ptr<telegram_api::UserProfilePhoto> &&photo,
                            const char *source);
  void do_update_user_photo(User *u, UserId user_id, ProfilePhoto new_photo, bool invalidate_photo_cache,
                            const char *source);

  void upload_profile_photo(FileId file_id, bool is_animation, double main_frame_timestamp, Promise<Unit> &&promise,
                            vector<int> bad_parts = {});

  void on_upload_profile_photo(FileId file_id, tl_object_ptr<telegram_api::InputFile> input_file);
  void on_upload_profile_photo_error(FileId file_id, Status status);

  void register_user_photo(User *u, UserId user_id, const Photo &photo);

  void on_update_user_full_is_blocked(UserFull *user_full, UserId user_id, bool is_blocked);
  void on_update_user_full_common_chat_count(UserFull *user_full, UserId user_id, int32 common_chat_count);
  void on_update_user_full_need_phone_number_privacy_exception(UserFull *user_full, UserId user_id,
                                                               bool need_phone_number_privacy_exception);

  void add_profile_photo_to_cache(UserId user_id, Photo &&photo);
  bool delete_profile_photo_from_cache(UserId user_id, int64 profile_photo_id, bool send_updates);
  void drop_user_photos(UserId user_id, bool is_empty, bool drop_user_full_photo, const char *source);
  void drop_user_full(UserId user_id);

  void on_update_chat_status(Chat *c, ChatId chat_id, DialogParticipantStatus status);
  void on_update_chat_default_permissions(Chat *c, ChatId chat_id, RestrictedRights default_permissions, int32 version);
  void on_update_chat_participant_count(Chat *c, ChatId chat_id, int32 participant_count, int32 version,
                                        const string &debug_str);
  void on_update_chat_photo(Chat *c, ChatId chat_id, tl_object_ptr<telegram_api::ChatPhoto> &&chat_photo_ptr);
  void on_update_chat_title(Chat *c, ChatId chat_id, string &&title);
  void on_update_chat_active(Chat *c, ChatId chat_id, bool is_active);
  void on_update_chat_migrated_to_channel_id(Chat *c, ChatId chat_id, ChannelId migrated_to_channel_id);

  void on_update_chat_full_photo(ChatFull *chat_full, ChatId chat_id, Photo photo);
  bool on_update_chat_full_participants_short(ChatFull *chat_full, ChatId chat_id, int32 version);
  void on_update_chat_full_participants(ChatFull *chat_full, ChatId chat_id, vector<DialogParticipant> participants,
                                        int32 version, bool from_update);
  void on_update_chat_full_invite_link(ChatFull *chat_full,
                                       tl_object_ptr<telegram_api::chatInviteExported> &&invite_link);

  void on_update_channel_photo(Channel *c, ChannelId channel_id,
                               tl_object_ptr<telegram_api::ChatPhoto> &&chat_photo_ptr);
  void on_update_channel_title(Channel *c, ChannelId channel_id, string &&title);
  void on_update_channel_username(Channel *c, ChannelId channel_id, string &&username);
  void on_update_channel_status(Channel *c, ChannelId channel_id, DialogParticipantStatus &&status);
  void on_update_channel_default_permissions(Channel *c, ChannelId channel_id, RestrictedRights default_permissions);

  void on_update_channel_bot_user_ids(ChannelId channel_id, vector<UserId> &&bot_user_ids);

  void on_update_channel_full_photo(ChannelFull *channel_full, ChannelId channel_id, Photo photo);
  void on_update_channel_full_invite_link(ChannelFull *channel_full,
                                          tl_object_ptr<telegram_api::chatInviteExported> &&invite_link);
  void on_update_channel_full_linked_channel_id(ChannelFull *channel_full, ChannelId channel_id,
                                                ChannelId linked_channel_id);
  void on_update_channel_full_location(ChannelFull *channel_full, ChannelId channel_id, const DialogLocation &location);
  void on_update_channel_full_slow_mode_delay(ChannelFull *channel_full, ChannelId channel_id, int32 slow_mode_delay,
                                              int32 slow_mode_next_send_date);
  void on_update_channel_full_slow_mode_next_send_date(ChannelFull *channel_full, int32 slow_mode_next_send_date);
  void on_update_channel_full_bot_user_ids(ChannelFull *channel_full, ChannelId channel_id,
                                           vector<UserId> &&bot_user_ids);

  void on_channel_status_changed(Channel *c, ChannelId channel_id, const DialogParticipantStatus &old_status,
                                 const DialogParticipantStatus &new_status);
  void on_channel_username_changed(Channel *c, ChannelId channel_id, const string &old_username,
                                   const string &new_username);

  void remove_linked_channel_id(ChannelId channel_id);
  ChannelId get_linked_channel_id(ChannelId channel_id) const;

  static bool speculative_add_count(int32 &count, int32 delta_count, int32 min_count = 0);

  void speculative_add_channel_participants(ChannelId channel_id, int32 delta_participant_count, bool by_me);

  void speculative_add_channel_user(ChannelId channel_id, UserId user_id, DialogParticipantStatus new_status,
                                    DialogParticipantStatus old_status);

  void drop_chat_photos(ChatId chat_id, bool is_empty, bool drop_chat_full_photo, const char *source);
  void drop_chat_full(ChatId chat_id);

  void drop_channel_photos(ChannelId channel_id, bool is_empty, bool drop_channel_full_photo, const char *source);

  void update_user_online_member_count(User *u);
  void update_chat_online_member_count(const ChatFull *chat_full, ChatId chat_id, bool is_from_server);
  void update_channel_online_member_count(ChannelId channel_id, bool is_from_server);
  void update_dialog_online_member_count(const vector<DialogParticipant> &participants, DialogId dialog_id,
                                         bool is_from_server);

  void on_chat_update(telegram_api::chatEmpty &chat, const char *source);
  void on_chat_update(telegram_api::chat &chat, const char *source);
  void on_chat_update(telegram_api::chatForbidden &chat, const char *source);
  void on_chat_update(telegram_api::channel &channel, const char *source);
  void on_chat_update(telegram_api::channelForbidden &channel, const char *source);

  void save_user(User *u, UserId user_id, bool from_binlog);
  static string get_user_database_key(UserId user_id);
  static string get_user_database_value(const User *u);
  void save_user_to_database(User *u, UserId user_id);
  void save_user_to_database_impl(User *u, UserId user_id, string value);
  void on_save_user_to_database(UserId user_id, bool success);
  void load_user_from_database(User *u, UserId user_id, Promise<Unit> promise);
  void load_user_from_database_impl(UserId user_id, Promise<Unit> promise);
  void on_load_user_from_database(UserId user_id, string value);

  void save_chat(Chat *c, ChatId chat_id, bool from_binlog);
  static string get_chat_database_key(ChatId chat_id);
  static string get_chat_database_value(const Chat *c);
  void save_chat_to_database(Chat *c, ChatId chat_id);
  void save_chat_to_database_impl(Chat *c, ChatId chat_id, string value);
  void on_save_chat_to_database(ChatId chat_id, bool success);
  void load_chat_from_database(Chat *c, ChatId chat_id, Promise<Unit> promise);
  void load_chat_from_database_impl(ChatId chat_id, Promise<Unit> promise);
  void on_load_chat_from_database(ChatId chat_id, string value);

  void save_channel(Channel *c, ChannelId channel_id, bool from_binlog);
  static string get_channel_database_key(ChannelId channel_id);
  static string get_channel_database_value(const Channel *c);
  void save_channel_to_database(Channel *c, ChannelId channel_id);
  void save_channel_to_database_impl(Channel *c, ChannelId channel_id, string value);
  void on_save_channel_to_database(ChannelId channel_id, bool success);
  void load_channel_from_database(Channel *c, ChannelId channel_id, Promise<Unit> promise);
  void load_channel_from_database_impl(ChannelId channel_id, Promise<Unit> promise);
  void on_load_channel_from_database(ChannelId channel_id, string value);

  void save_secret_chat(SecretChat *c, SecretChatId secret_chat_id, bool from_binlog);
  static string get_secret_chat_database_key(SecretChatId secret_chat_id);
  static string get_secret_chat_database_value(const SecretChat *c);
  void save_secret_chat_to_database(SecretChat *c, SecretChatId secret_chat_id);
  void save_secret_chat_to_database_impl(SecretChat *c, SecretChatId secret_chat_id, string value);
  void on_save_secret_chat_to_database(SecretChatId secret_chat_id, bool success);
  void load_secret_chat_from_database(SecretChat *c, SecretChatId secret_chat_id, Promise<Unit> promise);
  void load_secret_chat_from_database_impl(SecretChatId secret_chat_id, Promise<Unit> promise);
  void on_load_secret_chat_from_database(SecretChatId secret_chat_id, string value);

  void save_user_full(const UserFull *user_full, UserId user_id);
  static string get_user_full_database_key(UserId user_id);
  static string get_user_full_database_value(const UserFull *user_full);
  void on_load_user_full_from_database(UserId user_id, string value);

  void save_bot_info(const BotInfo *bot_info, UserId user_id);
  static string get_bot_info_database_key(UserId user_id);
  static string get_bot_info_database_value(const BotInfo *bot_info);
  void on_load_bot_info_from_database(UserId user_id, string value, bool send_update);

  void save_chat_full(const ChatFull *chat_full, ChatId chat_id);
  static string get_chat_full_database_key(ChatId chat_id);
  static string get_chat_full_database_value(const ChatFull *chat_full);
  void on_load_chat_full_from_database(ChatId chat_id, string value);

  void save_channel_full(const ChannelFull *channel_full, ChannelId channel_id);
  static string get_channel_full_database_key(ChannelId channel_id);
  static string get_channel_full_database_value(const ChannelFull *channel_full);
  void on_load_channel_full_from_database(ChannelId channel_id, string value);

  void update_user(User *u, UserId user_id, bool from_binlog = false, bool from_database = false);
  void update_chat(Chat *c, ChatId chat_id, bool from_binlog = false, bool from_database = false);
  void update_channel(Channel *c, ChannelId channel_id, bool from_binlog = false, bool from_database = false);
  void update_secret_chat(SecretChat *c, SecretChatId secret_chat_id, bool from_binlog = false,
                          bool from_database = false);

  void update_user_full(UserFull *user_full, UserId user_id, bool from_database = false);
  void update_chat_full(ChatFull *chat_full, ChatId chat_id, bool from_database = false);
  void update_channel_full(ChannelFull *channel_full, ChannelId channel_id, bool from_database = false);

  void update_bot_info(BotInfo *bot_info, UserId user_id, bool send_update, bool from_database);

  bool is_chat_full_outdated(const ChatFull *chat_full, const Chat *c, ChatId chat_id);

  bool is_user_contact(const User *u, UserId user_id, bool is_mutual) const;

  int32 get_user_was_online(const User *u, UserId user_id) const;

  int32 get_contacts_hash();

  void update_contacts_hints(const User *u, UserId user_id, bool from_database);

  void save_next_contacts_sync_date();

  void save_contacts_to_database();

  void load_contacts(Promise<Unit> &&promise);

  void on_load_contacts_from_database(string value);

  void on_get_contacts_finished(size_t expected_contact_count);

  void load_imported_contacts(Promise<Unit> &&promise);

  void on_load_imported_contacts_from_database(string value);

  void on_load_imported_contacts_finished();

  void on_clear_imported_contacts(vector<Contact> &&contacts, vector<size_t> contacts_unique_id,
                                  std::pair<vector<size_t>, vector<Contact>> &&to_add, Promise<Unit> &&promise);

  void send_update_chat_member(DialogId dialog_id, UserId agent_user_id, int32 date, DialogInviteLink invite_link,
                               const DialogParticipant &old_dialog_participant,
                               const DialogParticipant &new_dialog_participant);

  static vector<td_api::object_ptr<td_api::chatNearby>> get_chats_nearby_object(
      const vector<DialogNearby> &dialogs_nearby);

  void send_update_users_nearby() const;

  void on_get_dialogs_nearby(Result<tl_object_ptr<telegram_api::Updates>> result,
                             Promise<td_api::object_ptr<td_api::chatsNearby>> &&promise);

  void try_send_set_location_visibility_query();

  void on_set_location_visibility_expire_date(int32 set_expire_date, int32 error_code);

  void set_location_visibility_expire_date(int32 expire_date);

  void update_is_location_visible();

  static bool is_channel_public(const Channel *c);

  void export_dialog_invite_link_impl(DialogId dialog_id, int32 expire_date, int32 usage_limit, bool is_permanent,
                                      Promise<td_api::object_ptr<td_api::chatInviteLink>> &&promise);

  void remove_dialog_access_by_invite_link(DialogId dialog_id);

  Status can_manage_dialog_invite_links(DialogId dialog_id, bool creator_only = false);

  bool update_permanent_invite_link(DialogInviteLink &invite_link, DialogInviteLink new_invite_link);

  void add_chat_participant(ChatId chat_id, UserId user_id, int32 forward_limit, Promise<Unit> &&promise);

  void add_channel_participant(ChannelId channel_id, UserId user_id, Promise<Unit> &&promise,
                               DialogParticipantStatus old_status = DialogParticipantStatus::Left());

  void add_channel_participants(ChannelId channel_id, const vector<UserId> &user_ids, Promise<Unit> &&promise);

  const DialogParticipant *get_chat_participant(ChatId chat_id, UserId user_id) const;

  static const DialogParticipant *get_chat_full_participant(const ChatFull *chat_full, UserId user_id);

  std::pair<int32, vector<UserId>> search_among_users(const vector<UserId> &user_ids, const string &query,
                                                      int32 limit) const;

  DialogParticipants search_private_chat_participants(UserId my_user_id, UserId peer_user_id, const string &query,
                                                      int32 limit, DialogParticipantsFilter filter) const;

  DialogParticipant get_chat_participant(ChatId chat_id, UserId user_id, bool force, Promise<Unit> &&promise);

  DialogParticipant get_channel_participant(ChannelId channel_id, UserId user_id, int64 &random_id, bool force,
                                            Promise<Unit> &&promise);

  static string get_dialog_administrators_database_key(DialogId dialog_id);

  void load_dialog_administrators(DialogId dialog_id, Promise<Unit> &&promise);

  void on_load_dialog_administrators_from_database(DialogId dialog_id, string value, Promise<Unit> &&promise);

  void on_load_administrator_users_finished(DialogId dialog_id, vector<DialogAdministrator> administrators,
                                            Result<> result, Promise<Unit> promise);

  void reload_dialog_administrators(DialogId dialog_id, int32 hash, Promise<Unit> &&promise);

  void remove_dialog_suggested_action(SuggestedAction action);

  void on_dismiss_suggested_action(SuggestedAction action, Result<Unit> &&result);

  static td_api::object_ptr<td_api::updateUser> get_update_unknown_user_object(UserId user_id);

  td_api::object_ptr<td_api::UserStatus> get_user_status_object(UserId user_id, const User *u) const;

  td_api::object_ptr<td_api::botInfo> get_bot_info_object(UserId user_id) const;

  tl_object_ptr<td_api::user> get_user_object(UserId user_id, const User *u) const;

  tl_object_ptr<td_api::userFullInfo> get_user_full_info_object(UserId user_id, const UserFull *user_full) const;

  static td_api::object_ptr<td_api::updateBasicGroup> get_update_unknown_basic_group_object(ChatId chat_id);

  tl_object_ptr<td_api::basicGroup> get_basic_group_object(ChatId chat_id, const Chat *c);

  tl_object_ptr<td_api::basicGroup> get_basic_group_object_const(ChatId chat_id, const Chat *c) const;

  tl_object_ptr<td_api::basicGroupFullInfo> get_basic_group_full_info_object(const ChatFull *chat_full) const;

  static td_api::object_ptr<td_api::updateSupergroup> get_update_unknown_supergroup_object(ChannelId channel_id);

  tl_object_ptr<td_api::supergroup> get_supergroup_object(ChannelId channel_id, const Channel *c) const;

  tl_object_ptr<td_api::supergroupFullInfo> get_supergroup_full_info_object(const ChannelFull *channel_full,
                                                                            ChannelId channel_id) const;

  static tl_object_ptr<td_api::SecretChatState> get_secret_chat_state_object(SecretChatState state);

  static td_api::object_ptr<td_api::updateSecretChat> get_update_unknown_secret_chat_object(SecretChatId user_id);

  tl_object_ptr<td_api::secretChat> get_secret_chat_object(SecretChatId secret_chat_id, const SecretChat *secret_chat);

  tl_object_ptr<td_api::secretChat> get_secret_chat_object_const(SecretChatId secret_chat_id,
                                                                 const SecretChat *secret_chat) const;

  vector<ChannelId> get_channel_ids(vector<tl_object_ptr<telegram_api::Chat>> &&chats, const char *source);

  vector<DialogId> get_dialog_ids(vector<tl_object_ptr<telegram_api::Chat>> &&chats, const char *source);

  void update_dialogs_for_discussion(DialogId dialog_id, bool is_suitable);

  void change_chat_participant_status(ChatId chat_id, UserId user_id, DialogParticipantStatus status,
                                      Promise<Unit> &&promise);

  void change_channel_participant_status(ChannelId channel_id, UserId user_id, DialogParticipantStatus status,
                                         Promise<Unit> &&promise);

  void delete_chat_participant(ChatId chat_id, UserId user_id, bool revoke_messages, Promise<Unit> &&promise);

  void search_chat_participants(ChatId chat_id, const string &query, int32 limit, DialogParticipantsFilter filter,
                                Promise<DialogParticipants> &&promise);

  void do_search_chat_participants(ChatId chat_id, const string &query, int32 limit, DialogParticipantsFilter filter,
                                   Promise<DialogParticipants> &&promise);

  void do_get_channel_participants(ChannelId channel_id, ChannelParticipantsFilter &&filter, string additional_query,
                                   int32 offset, int32 limit, int32 additional_limit,
                                   Promise<DialogParticipants> &&promise);

  void on_get_channel_participants(ChannelId channel_id, ChannelParticipantsFilter filter, int32 offset, int32 limit,
                                   string additional_query, int32 additional_limit,
                                   tl_object_ptr<telegram_api::channels_channelParticipants> &&channel_participants,
                                   Promise<DialogParticipants> &&promise);

  void change_channel_participant_status_impl(ChannelId channel_id, UserId user_id, DialogParticipantStatus status,
                                              DialogParticipantStatus old_status, Promise<Unit> &&promise);

  void promote_channel_participant(ChannelId channel_id, UserId user_id, DialogParticipantStatus status,
                                   DialogParticipantStatus old_status, Promise<Unit> &&promise);

  void restrict_channel_participant(ChannelId channel_id, UserId user_id, DialogParticipantStatus status,
                                    DialogParticipantStatus old_status, Promise<Unit> &&promise);

  void transfer_channel_ownership(ChannelId channel_id, UserId user_id,
                                  tl_object_ptr<telegram_api::InputCheckPasswordSRP> input_check_password,
                                  Promise<Unit> &&promise);

  void delete_chat(ChatId chat_id, Promise<Unit> &&promise);

  void delete_channel(ChannelId channel_id, Promise<Unit> &&promise);

  void get_channel_statistics_dc_id(DialogId dialog_id, bool for_full_statistics, Promise<DcId> &&promise);

  void get_channel_statistics_dc_id_impl(ChannelId channel_id, bool for_full_statistics, Promise<DcId> &&promise);

  void send_get_channel_stats_query(DcId dc_id, ChannelId channel_id, bool is_dark,
                                    Promise<td_api::object_ptr<td_api::ChatStatistics>> &&promise);

  void send_get_channel_message_stats_query(DcId dc_id, FullMessageId full_message_id, bool is_dark,
                                            Promise<td_api::object_ptr<td_api::messageStatistics>> &&promise);

  void send_load_async_graph_query(DcId dc_id, string token, int64 x,
                                   Promise<td_api::object_ptr<td_api::StatisticalGraph>> &&promise);

  static void on_user_online_timeout_callback(void *contacts_manager_ptr, int64 user_id_long);

  static void on_channel_unban_timeout_callback(void *contacts_manager_ptr, int64 channel_id_long);

  static void on_user_nearby_timeout_callback(void *contacts_manager_ptr, int64 user_id_long);

  static void on_slow_mode_delay_timeout_callback(void *contacts_manager_ptr, int64 channel_id_long);

  static void on_invite_link_info_expire_timeout_callback(void *contacts_manager_ptr, int64 dialog_id_long);

  void on_user_online_timeout(UserId user_id);

  void on_user_nearby_timeout(UserId user_id);

  void on_slow_mode_delay_timeout(ChannelId channel_id);

  void on_invite_link_info_expire_timeout(DialogId dialog_id);

  void tear_down() override;

  Td *td_;
  ActorShared<> parent_;
  UserId my_id_;
  UserId support_user_id_;
  int32 my_was_online_local_ = 0;

  std::unordered_map<UserId, unique_ptr<User>, UserIdHash> users_;
  std::unordered_map<UserId, unique_ptr<UserFull>, UserIdHash> users_full_;
  std::unordered_map<UserId, unique_ptr<BotInfo>, UserIdHash> bot_infos_;
  std::unordered_map<UserId, UserPhotos, UserIdHash> user_photos_;
  mutable std::unordered_set<UserId, UserIdHash> unknown_users_;
  std::unordered_map<UserId, tl_object_ptr<telegram_api::UserProfilePhoto>, UserIdHash> pending_user_photos_;
  struct UserIdPhotoIdHash {
    std::size_t operator()(const std::pair<UserId, int64> &pair) const {
      return UserIdHash()(pair.first) * 2023654985u + std::hash<int64>()(pair.second);
    }
  };
  std::unordered_map<std::pair<UserId, int64>, FileSourceId, UserIdPhotoIdHash> user_profile_photo_file_source_ids_;
  std::unordered_map<int64, FileId> my_photo_file_id_;

  std::unordered_map<ChatId, unique_ptr<Chat>, ChatIdHash> chats_;
  std::unordered_map<ChatId, unique_ptr<ChatFull>, ChatIdHash> chats_full_;
  mutable std::unordered_set<ChatId, ChatIdHash> unknown_chats_;
  std::unordered_map<ChatId, FileSourceId, ChatIdHash> chat_full_file_source_ids_;

  std::unordered_set<ChannelId, ChannelIdHash> min_channels_;
  std::unordered_map<ChannelId, unique_ptr<Channel>, ChannelIdHash> channels_;
  std::unordered_map<ChannelId, unique_ptr<ChannelFull>, ChannelIdHash> channels_full_;
  mutable std::unordered_set<ChannelId, ChannelIdHash> unknown_channels_;
  std::unordered_map<ChannelId, FileSourceId, ChannelIdHash> channel_full_file_source_ids_;

  std::unordered_map<SecretChatId, unique_ptr<SecretChat>, SecretChatIdHash> secret_chats_;
  mutable std::unordered_set<SecretChatId, SecretChatIdHash> unknown_secret_chats_;

  std::unordered_map<UserId, vector<SecretChatId>, UserIdHash> secret_chats_with_user_;

  struct DialogAccessByInviteLink {
    std::unordered_set<string> invite_links;
    int32 accessible_before = 0;
  };
  std::unordered_map<string, unique_ptr<InviteLinkInfo>> invite_link_infos_;
  std::unordered_map<DialogId, DialogAccessByInviteLink, DialogIdHash> dialog_access_by_invite_link_;

  bool created_public_channels_inited_[2] = {false, false};
  vector<ChannelId> created_public_channels_[2];

  bool dialogs_for_discussion_inited_ = false;
  vector<DialogId> dialogs_for_discussion_;

  bool inactive_channels_inited_ = false;
  vector<ChannelId> inactive_channels_;

  std::unordered_map<UserId, vector<Promise<Unit>>, UserIdHash> load_user_from_database_queries_;
  std::unordered_set<UserId, UserIdHash> loaded_from_database_users_;
  std::unordered_set<UserId, UserIdHash> unavailable_user_fulls_;
  std::unordered_set<UserId, UserIdHash> unavailable_bot_infos_;

  std::unordered_map<ChatId, vector<Promise<Unit>>, ChatIdHash> load_chat_from_database_queries_;
  std::unordered_set<ChatId, ChatIdHash> loaded_from_database_chats_;
  std::unordered_set<ChatId, ChatIdHash> unavailable_chat_fulls_;

  std::unordered_map<ChannelId, vector<Promise<Unit>>, ChannelIdHash> load_channel_from_database_queries_;
  std::unordered_set<ChannelId, ChannelIdHash> loaded_from_database_channels_;
  std::unordered_set<ChannelId, ChannelIdHash> unavailable_channel_fulls_;

  std::unordered_map<SecretChatId, vector<Promise<Unit>>, SecretChatIdHash> load_secret_chat_from_database_queries_;
  std::unordered_set<SecretChatId, SecretChatIdHash> loaded_from_database_secret_chats_;

  QueryCombiner get_user_full_queries_{"GetUserFullCombiner", 2.0};
  QueryCombiner get_chat_full_queries_{"GetChatFullCombiner", 2.0};
  QueryCombiner get_channel_full_queries_{"GetChannelFullCombiner", 2.0};

  std::unordered_map<DialogId, vector<DialogAdministrator>, DialogIdHash> dialog_administrators_;

  std::unordered_map<DialogId, vector<SuggestedAction>, DialogIdHash> dialog_suggested_actions_;
  std::unordered_map<DialogId, vector<Promise<Unit>>, DialogIdHash> dismiss_suggested_action_queries_;

  class UploadProfilePhotoCallback;
  std::shared_ptr<UploadProfilePhotoCallback> upload_profile_photo_callback_;

  struct UploadedProfilePhoto {
    double main_frame_timestamp;
    bool is_animation;
    bool is_reupload;
    Promise<Unit> promise;

    UploadedProfilePhoto(double main_frame_timestamp, bool is_animation, bool is_reupload, Promise<Unit> promise)
        : main_frame_timestamp(main_frame_timestamp)
        , is_animation(is_animation)
        , is_reupload(is_reupload)
        , promise(std::move(promise)) {
    }
  };
  std::unordered_map<FileId, UploadedProfilePhoto, FileIdHash> uploaded_profile_photos_;  // file_id -> promise

  std::unordered_map<int64, std::pair<vector<UserId>, vector<int32>>> imported_contacts_;

  std::unordered_map<int64, DialogParticipant> received_channel_participant_;

  std::unordered_map<ChannelId, vector<DialogParticipant>, ChannelIdHash> cached_channel_participants_;

  bool are_contacts_loaded_ = false;
  int32 next_contacts_sync_date_ = 0;
  Hints contacts_hints_;  // search contacts by first name, last name and username
  vector<Promise<Unit>> load_contacts_queries_;
  MultiPromiseActor load_contact_users_multipromise_{"LoadContactUsersMultiPromiseActor"};
  int32 saved_contact_count_ = -1;

  int32 was_online_local_ = 0;
  int32 was_online_remote_ = 0;

  bool are_imported_contacts_loaded_ = false;
  vector<Promise<Unit>> load_imported_contacts_queries_;
  MultiPromiseActor load_imported_contact_users_multipromise_{"LoadImportedContactUsersMultiPromiseActor"};
  vector<Contact> all_imported_contacts_;
  bool are_imported_contacts_changing_ = false;
  bool need_clear_imported_contacts_ = false;

  vector<DialogNearby> users_nearby_;
  vector<DialogNearby> channels_nearby_;
  std::unordered_set<UserId, UserIdHash> all_users_nearby_;

  int32 location_visibility_expire_date_ = 0;
  int32 pending_location_visibility_expire_date_ = -1;
  bool is_set_location_visibility_request_sent_ = false;
  Location last_user_location_;

  std::unordered_map<ChannelId, ChannelId, ChannelIdHash> linked_channel_ids_;

  std::unordered_set<UserId, UserIdHash> restricted_user_ids_;
  std::unordered_set<ChannelId, ChannelIdHash> restricted_channel_ids_;

  vector<Contact> next_all_imported_contacts_;
  vector<size_t> imported_contacts_unique_id_;
  vector<size_t> imported_contacts_pos_;

  vector<UserId> imported_contact_user_ids_;  // result of change_imported_contacts
  vector<int32> unimported_contact_invites_;  // result of change_imported_contacts

  MultiTimeout user_online_timeout_{"UserOnlineTimeout"};
  MultiTimeout channel_unban_timeout_{"ChannelUnbanTimeout"};
  MultiTimeout user_nearby_timeout_{"UserNearbyTimeout"};
  MultiTimeout slow_mode_delay_timeout_{"SlowModeDelayTimeout"};
  MultiTimeout invite_link_info_expire_timeout_{"InviteLinkInfoExpireTimeout"};
};

}  // namespace td
