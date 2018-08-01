//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"
#include "td/actor/MultiPromise.h"
#include "td/actor/PromiseFuture.h"
#include "td/actor/Timeout.h"

#include "td/db/binlog/BinlogEvent.h"

#include "td/telegram/AccessRights.h"
#include "td/telegram/ChannelId.h"
#include "td/telegram/ChatId.h"
#include "td/telegram/Contact.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/DialogParticipant.h"
#include "td/telegram/files/FileId.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/Photo.h"
#include "td/telegram/SecretChatId.h"
#include "td/telegram/UserId.h"

#include "td/utils/common.h"
#include "td/utils/Hints.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/StringBuilder.h"

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace td {

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

class ContactsManager : public Actor {
 public:
  ContactsManager(Td *td, ActorShared<> parent);

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

  bool is_update_about_username_change_received(UserId user_id) const;

  string get_user_username(UserId user_id) const;
  string get_channel_username(ChannelId channel_id) const;
  string get_secret_chat_username(SecretChatId secret_chat_id) const;

  int32 get_secret_chat_date(SecretChatId secret_chat_id) const;
  int32 get_secret_chat_ttl(SecretChatId secret_chat_id) const;
  UserId get_secret_chat_user_id(SecretChatId secret_chat_id) const;
  SecretChatState get_secret_chat_state(SecretChatId secret_chat_id) const;
  int32 get_secret_chat_layer(SecretChatId secret_chat_id) const;

  bool default_can_report_spam_in_secret_chat(SecretChatId secret_chat_id) const;

  void on_imported_contacts(int64 random_id, vector<UserId> imported_contact_user_ids,
                            vector<int32> unimported_contact_invites);

  void on_deleted_contacts(const vector<UserId> &deleted_contact_user_ids);

  void on_get_contacts(tl_object_ptr<telegram_api::contacts_Contacts> &&new_contacts);

  void on_get_contacts_failed(Status error);

  void on_get_contacts_statuses(vector<tl_object_ptr<telegram_api::contactStatus>> &&statuses);

  void reload_contacts(bool force);

  void on_get_contacts_link(tl_object_ptr<telegram_api::contacts_link> &&link);

  void on_get_user(tl_object_ptr<telegram_api::User> &&user, bool is_me = false, bool is_support = false);
  void on_get_users(vector<tl_object_ptr<telegram_api::User>> &&users);

  void on_binlog_user_event(BinlogEvent &&event);
  void on_binlog_chat_event(BinlogEvent &&event);
  void on_binlog_channel_event(BinlogEvent &&event);
  void on_binlog_secret_chat_event(BinlogEvent &&event);

  void on_get_user_full(tl_object_ptr<telegram_api::userFull> &&user_full);

  void on_get_user_photos(UserId user_id, int32 offset, int32 limit, int32 total_count,
                          vector<tl_object_ptr<telegram_api::Photo>> photos);

  void on_get_chat(tl_object_ptr<telegram_api::Chat> &&chat);
  void on_get_chats(vector<tl_object_ptr<telegram_api::Chat>> &&chats);

  void on_get_chat_full(tl_object_ptr<telegram_api::ChatFull> &&chat_full);

  void on_update_profile_success(int32 flags, const string &first_name, const string &last_name, const string &about);

  void on_update_user_name(UserId user_id, string &&first_name, string &&last_name, string &&username);
  void on_update_user_phone_number(UserId user_id, string &&phone_number);
  void on_update_user_photo(UserId user_id, tl_object_ptr<telegram_api::UserProfilePhoto> &&photo_ptr);
  void on_update_user_online(UserId user_id, tl_object_ptr<telegram_api::UserStatus> &&status);
  void on_update_user_links(UserId user_id, tl_object_ptr<telegram_api::ContactLink> &&outbound,
                            tl_object_ptr<telegram_api::ContactLink> &&inbound);
  void on_update_user_blocked(UserId user_id, bool is_blocked);

  void on_delete_profile_photo(int64 profile_photo_id, Promise<Unit> promise);

  void on_get_chat_participants(tl_object_ptr<telegram_api::ChatParticipants> &&participants);
  void on_update_chat_add_user(ChatId chat_id, UserId inviter_user_id, UserId user_id, int32 date, int32 version);
  void on_update_chat_edit_administrator(ChatId chat_id, UserId user_id, bool is_administrator, int32 version);
  void on_update_chat_delete_user(ChatId chat_id, UserId user_id, int32 version);
  void on_update_chat_everyone_is_administrator(ChatId chat_id, bool everyone_is_administrator, int32 version);

  void on_update_channel_username(ChannelId channel_id, string &&username);
  void on_update_channel_description(ChannelId channel_id, string &&description);
  void on_update_channel_sticker_set(ChannelId channel_id, int64 sticker_set_id);
  void on_update_channel_pinned_message(ChannelId channel_id, MessageId message_id);
  void on_update_channel_is_all_history_available(ChannelId channel_id, bool is_all_history_available);

  void on_update_dialog_administrators(DialogId dialog_id, vector<UserId> administrator_user_ids, bool have_access);

  static bool speculative_add_count(int32 &count, int32 new_count);

  void speculative_add_channel_participants(ChannelId channel_id, int32 new_participant_count, bool by_me);

  void invalidate_channel_full(ChannelId channel_id);

  bool on_get_channel_error(ChannelId channel_id, const Status &status, const string &source);

  void on_get_channel_participants_success(ChannelId channel_id, ChannelParticipantsFilter filter, int32 offset,
                                           int32 limit, int64 random_id, int32 total_count,
                                           vector<tl_object_ptr<telegram_api::ChannelParticipant>> &&participants);

  void on_get_channel_participants_fail(ChannelId channel_id, ChannelParticipantsFilter filter, int32 offset,
                                        int32 limit, int64 random_id);

  static Slice get_dialog_invite_link_hash(const string &invite_link);

  void on_get_chat_invite_link(ChatId chat_id, tl_object_ptr<telegram_api::ExportedChatInvite> &&invite_link_ptr);

  void on_get_channel_invite_link(ChannelId channel_id,
                                  tl_object_ptr<telegram_api::ExportedChatInvite> &&invite_link_ptr);

  void on_get_dialog_invite_link_info(const string &invite_link,
                                      tl_object_ptr<telegram_api::ChatInvite> &&chat_invite_ptr);

  void invalidate_invite_link(const string &invite_link);

  void on_get_created_public_channels(vector<tl_object_ptr<telegram_api::Chat>> &&chats);

  void on_get_user_full_success(UserId user_id);

  void on_get_user_full_fail(UserId user_id, Status &&error);

  void on_get_chat_full_success(ChatId chat_id);

  void on_get_chat_full_fail(ChatId chat_id, Status &&error);

  void on_get_channel_full_success(ChannelId channel_id);

  void on_get_channel_full_fail(ChannelId channel_id, Status &&error);

  UserId get_my_id(const char *source) const;

  void set_my_online_status(bool is_online, bool send_update, bool is_local);

  UserId get_service_notifications_user_id();

  void on_update_online_status_privacy();

  void on_channel_unban_timeout(ChannelId channel_id);

  void check_dialog_username(DialogId dialog_id, const string &username, Promise<CheckDialogUsernameResult> &&promise);

  static td_api::object_ptr<td_api::CheckChatUsernameResult> get_check_chat_username_result_object(
      CheckDialogUsernameResult result);

  void set_account_ttl(int32 account_ttl, Promise<Unit> &&promise) const;
  void get_account_ttl(Promise<int32> &&promise) const;

  void get_active_sessions(Promise<tl_object_ptr<td_api::sessions>> &&promise) const;
  void terminate_session(int64 session_id, Promise<Unit> &&promise) const;
  void terminate_all_other_sessions(Promise<Unit> &&promise) const;

  void get_connected_websites(Promise<tl_object_ptr<td_api::connectedWebsites>> &&promise) const;
  void disconnect_website(int64 authorizations_id, Promise<Unit> &&promise) const;
  void disconnect_all_websites(Promise<Unit> &&promise) const;

  Status block_user(UserId user_id);

  Status unblock_user(UserId user_id);

  int64 get_blocked_users(int32 offset, int32 limit, Promise<Unit> &&promise);

  void on_get_blocked_users_result(int32 offset, int32 limit, int64 random_id, int32 total_count,
                                   vector<tl_object_ptr<telegram_api::contactBlocked>> &&blocked_users);

  void on_failed_get_blocked_users(int64 random_id);

  tl_object_ptr<td_api::users> get_blocked_users_object(int64 random_id);

  std::pair<vector<UserId>, vector<int32>> import_contacts(const vector<tl_object_ptr<td_api::contact>> &contacts,
                                                           int64 &random_id, Promise<Unit> &&promise);

  std::pair<int32, vector<UserId>> search_contacts(const string &query, int32 limit, Promise<Unit> &&promise);

  void remove_contacts(vector<UserId> user_ids, Promise<Unit> &&promise);

  int32 get_imported_contact_count(Promise<Unit> &&promise);

  std::pair<vector<UserId>, vector<int32>> change_imported_contacts(vector<tl_object_ptr<td_api::contact>> &&contacts,
                                                                    int64 &random_id, Promise<Unit> &&promise);

  void clear_imported_contacts(Promise<Unit> &&promise);

  void on_update_contacts_reset();

  void set_profile_photo(const tl_object_ptr<td_api::InputFile> &input_photo, Promise<Unit> &&promise);

  void delete_profile_photo(int64 profile_photo_id, Promise<Unit> &&promise);

  void set_name(const string &first_name, const string &last_name, Promise<Unit> &&promise);

  void set_bio(const string &bio, Promise<Unit> &&promise);

  void set_username(const string &username, Promise<Unit> &&promise);

  void toggle_chat_administrators(ChatId chat_id, bool everyone_is_administrator, Promise<Unit> &&promise);

  void set_channel_username(ChannelId channel_id, const string &username, Promise<Unit> &&promise);

  void set_channel_sticker_set(ChannelId channel_id, int64 sticker_set_id, Promise<Unit> &&promise);

  void toggle_channel_invites(ChannelId channel_id, bool anyone_can_invite, Promise<Unit> &&promise);

  void toggle_channel_sign_messages(ChannelId channel_id, bool sign_messages, Promise<Unit> &&promise);

  void toggle_channel_is_all_history_available(ChannelId channel_id, bool is_all_history_available,
                                               Promise<Unit> &&promise);

  void set_channel_description(ChannelId channel_id, const string &description, Promise<Unit> &&promise);

  void pin_channel_message(ChannelId channel_id, MessageId message_id, bool disable_notification,
                           Promise<Unit> &&promise);

  void unpin_channel_message(ChannelId channel_id, Promise<Unit> &&promise);

  void report_channel_spam(ChannelId channel_id, UserId user_id, const vector<MessageId> &message_ids,
                           Promise<Unit> &&promise);

  void delete_channel(ChannelId channel_id, Promise<Unit> &&promise);

  void add_chat_participant(ChatId chat_id, UserId user_id, int32 forward_limit, Promise<Unit> &&promise);

  void add_channel_participant(ChannelId channel_id, UserId user_id, Promise<Unit> &&promise,
                               DialogParticipantStatus old_status = DialogParticipantStatus::Left());

  void add_channel_participants(ChannelId channel_id, const vector<UserId> &user_ids, Promise<Unit> &&promise);

  void change_chat_participant_status(ChatId chat_id, UserId user_id, DialogParticipantStatus status,
                                      Promise<Unit> &&promise);

  void change_channel_participant_status(ChannelId channel_id, UserId user_id, DialogParticipantStatus status,
                                         Promise<Unit> &&promise);

  void export_chat_invite_link(ChatId chat_id, Promise<Unit> &&promise);

  void export_channel_invite_link(ChannelId channel_id, Promise<Unit> &&promise);

  void check_dialog_invite_link(const string &invite_link, Promise<Unit> &&promise) const;

  void import_dialog_invite_link(const string &invite_link, Promise<DialogId> &&promise);

  string get_chat_invite_link(ChatId chat_id) const;

  string get_channel_invite_link(ChannelId channel_id);

  MessageId get_channel_pinned_message_id(ChannelId channel_id);

  ChannelId migrate_chat_to_megagroup(ChatId chat_id, Promise<Unit> &promise);

  vector<DialogId> get_created_public_dialogs(Promise<Unit> &&promise);

  bool is_user_deleted(UserId user_id) const;

  bool is_user_bot(UserId user_id) const;
  Result<BotData> get_bot_data(UserId user_id) const TD_WARN_UNUSED_RESULT;

  bool have_user(UserId user_id) const;
  bool have_min_user(UserId user_id) const;
  bool have_user_force(UserId user_id);

  static void send_get_me_query(Td *td, Promise<Unit> &&promise);
  UserId get_me(Promise<Unit> &&promise);
  bool get_user(UserId user_id, int left_tries, Promise<Unit> &&promise);
  bool get_user_full(UserId user_id, Promise<Unit> &&promise);

  std::pair<int32, vector<const Photo *>> get_user_profile_photos(UserId user_id, int32 offset, int32 limit,
                                                                  Promise<Unit> &&promise);

  bool have_chat(ChatId chat_id) const;
  bool have_chat_force(ChatId chat_id);
  bool get_chat(ChatId chat_id, int left_tries, Promise<Unit> &&promise);
  bool get_chat_full(ChatId chat_id, Promise<Unit> &&promise);

  bool get_chat_is_active(ChatId chat_id) const;
  DialogParticipantStatus get_chat_status(ChatId chat_id) const;
  bool is_appointed_chat_administrator(ChatId chat_id) const;

  bool have_channel(ChannelId channel_id) const;
  bool have_min_channel(ChannelId channel_id) const;
  bool have_channel_force(ChannelId channel_id);
  bool get_channel(ChannelId channel_id, int left_tries, Promise<Unit> &&promise);
  bool get_channel_full(ChannelId channel_id, Promise<Unit> &&promise);

  bool have_secret_chat(SecretChatId secret_chat_id) const;
  bool have_secret_chat_force(SecretChatId secret_chat_id);
  bool get_secret_chat(SecretChatId secret_chat_id, bool force, Promise<Unit> &&promise);
  bool get_secret_chat_full(SecretChatId secret_chat_id, Promise<Unit> &&promise);

  ChannelType get_channel_type(ChannelId channel_id) const;
  int32 get_channel_date(ChannelId channel_id) const;
  DialogParticipantStatus get_channel_status(ChannelId channel_id) const;
  bool get_channel_sign_messages(ChannelId channel_id) const;

  std::pair<int32, vector<UserId>> search_among_users(const vector<UserId> &user_ids, const string &query, int32 limit);

  DialogParticipant get_chat_participant(ChatId chat_id, UserId user_id, bool force, Promise<Unit> &&promise);

  std::pair<int32, vector<DialogParticipant>> search_chat_participants(ChatId chat_id, const string &query, int32 limit,
                                                                       DialogParticipantsFilter filter, bool force,
                                                                       Promise<Unit> &&promise);

  DialogParticipant get_channel_participant(ChannelId channel_id, UserId user_id, int64 &random_id, bool force,
                                            Promise<Unit> &&promise);

  std::pair<int32, vector<DialogParticipant>> get_channel_participants(
      ChannelId channel_id, const tl_object_ptr<td_api::SupergroupMembersFilter> &filter,
      const string &additional_query, int32 offset, int32 limit, int32 additional_limit, int64 &random_id, bool force,
      Promise<Unit> &&promise);

  DialogParticipant get_dialog_participant(ChannelId channel_id,
                                           tl_object_ptr<telegram_api::ChannelParticipant> &&participant_ptr) const;

  vector<UserId> get_dialog_administrators(DialogId chat_id, int left_tries, Promise<Unit> &&promise);

  int32 get_user_id_object(UserId user_id, const char *source) const;

  tl_object_ptr<td_api::user> get_user_object(UserId user_id) const;

  vector<int32> get_user_ids_object(const vector<UserId> &user_ids) const;

  tl_object_ptr<td_api::users> get_users_object(int32 total_count, const vector<UserId> &user_ids) const;

  tl_object_ptr<td_api::userFullInfo> get_user_full_info_object(UserId user_id) const;

  int32 get_basic_group_id_object(ChatId chat_id, const char *source) const;

  tl_object_ptr<td_api::basicGroup> get_basic_group_object(ChatId chat_id);

  tl_object_ptr<td_api::basicGroupFullInfo> get_basic_group_full_info_object(ChatId chat_id) const;

  int32 get_supergroup_id_object(ChannelId channel_id, const char *source) const;

  tl_object_ptr<td_api::supergroup> get_supergroup_object(ChannelId channel_id) const;

  tl_object_ptr<td_api::supergroupFullInfo> get_channel_full_info_object(ChannelId channel_id) const;

  int32 get_secret_chat_id_object(SecretChatId secret_chat_id, const char *source) const;

  tl_object_ptr<td_api::secretChat> get_secret_chat_object(SecretChatId secret_chat_id);

  void on_update_secret_chat(SecretChatId secret_chat_id, int64 access_hash, UserId user_id, SecretChatState state,
                             bool is_outbound, int32 ttl, int32 date, string key_hash, int32 layer);

  void on_upload_profile_photo(FileId file_id, tl_object_ptr<telegram_api::InputFile> input_file);
  void on_upload_profile_photo_error(FileId file_id, Status status);

  tl_object_ptr<td_api::chatMember> get_chat_member_object(const DialogParticipant &dialog_participant) const;

  tl_object_ptr<td_api::botInfo> get_bot_info_object(UserId user_id) const;

  tl_object_ptr<td_api::chatInviteLinkInfo> get_chat_invite_link_info_object(const string &invite_link) const;

  UserId get_support_user(Promise<Unit> &&promise);

 private:
  enum class LinkState : uint8 { Unknown, None, KnowsPhoneNumber, Contact };

  friend StringBuilder &operator<<(StringBuilder &string_builder, LinkState link_state);

  struct User {
    string first_name;
    string last_name;
    string username;
    string phone_number;
    int64 access_hash = -1;

    ProfilePhoto photo;

    string restriction_reason;
    string inline_query_placeholder;
    int32 bot_info_version = -1;

    int32 was_online = 0;

    string language_code;

    LinkState outbound = LinkState::Unknown;
    LinkState inbound = LinkState::Unknown;

    bool is_received = false;
    bool is_verified = false;
    bool is_deleted = true;
    bool is_bot = true;
    bool can_join_groups = true;
    bool can_read_all_group_messages = true;
    bool is_inline_bot = false;
    bool need_location_bot = false;

    bool is_photo_inited = false;

    bool is_name_changed = true;
    bool is_username_changed = true;
    bool is_photo_changed = true;
    bool is_outbound_link_changed = true;
    bool is_changed = true;        // have new changes not sent to the database except changes visible to the client
    bool need_send_update = true;  // have new changes not sent to the client
    bool is_status_changed = true;

    bool is_saved = false;         // is current user version being saved/is saved to the database
    bool is_being_saved = false;   // is current user being saved to the database
    bool is_status_saved = false;  // is current user status being saved/is saved to the database

    uint64 logevent_id = 0;

    const char *debug_source = nullptr;

    template <class StorerT>
    void store(StorerT &storer) const;

    template <class ParserT>
    void parse(ParserT &parser);
  };

  struct BotInfo {
    int32 version = -1;
    string description;
    vector<std::pair<string, string>> commands;

    BotInfo(int32 version, string description, vector<std::pair<string, string>> &&commands)
        : version(version), description(std::move(description)), commands(std::move(commands)) {
    }
  };

  // do not forget to update invalidate_user_full and on_get_user_full
  struct UserFull {
    vector<Photo> photos;
    int32 photo_count = -1;
    int32 photos_offset = -1;

    std::unique_ptr<BotInfo> bot_info;

    string about;

    int32 common_chat_count = 0;

    bool getting_photos_now = false;

    bool is_inited = false;  // photos and bot_info may be inited regardless this flag
    bool is_blocked = false;
    bool can_be_called = false;
    bool has_private_calls = false;

    bool is_changed = true;

    double expires_at = 0.0;

    bool is_bot_info_expired(int32 bot_info_version) const;
    bool is_expired() const;
  };

  struct Chat {
    string title;
    DialogPhoto photo;
    int32 participant_count = 0;
    int32 date = 0;
    int32 version = -1;
    ChannelId migrated_to_channel_id;

    bool left = false;
    bool kicked = true;

    bool is_creator = false;
    bool is_administrator = false;
    bool everyone_is_administrator = true;
    bool can_edit = true;

    bool is_active = false;

    bool is_title_changed = true;
    bool is_photo_changed = true;
    bool is_changed = true;        // have new changes not sent to the database except changes visible to the client
    bool need_send_update = true;  // have new changes not sent to the client

    bool is_saved = false;        // is current chat version being saved/is saved to the database
    bool is_being_saved = false;  // is current chat being saved to the database
    uint64 logevent_id = 0;

    template <class StorerT>
    void store(StorerT &storer) const;

    template <class ParserT>
    void parse(ParserT &parser);
  };

  struct ChatFull {
    int32 version = -1;
    UserId creator_user_id;
    vector<DialogParticipant> participants;

    string invite_link;

    bool is_changed = true;
  };

  struct Channel {
    int64 access_hash = 0;
    string title;
    DialogPhoto photo;
    string username;
    string restriction_reason;
    DialogParticipantStatus status = DialogParticipantStatus::Banned(0);
    int32 date = 0;
    int32 participant_count = 0;

    bool anyone_can_invite = false;
    bool sign_messages = false;

    bool is_megagroup = false;
    bool is_verified = false;

    bool is_title_changed = true;
    bool is_username_changed = true;
    bool is_photo_changed = true;
    bool is_status_changed = true;
    bool had_read_access = true;
    bool was_member = false;
    bool is_changed = true;        // have new changes not sent to the database except changes visible to the client
    bool need_send_update = true;  // have new changes not sent to the client

    bool is_saved = false;        // is current channel version being saved/is saved to the database
    bool is_being_saved = false;  // is current channel being saved to the database
    uint64 logevent_id = 0;

    const char *debug_source = nullptr;

    template <class StorerT>
    void store(StorerT &storer) const;

    template <class ParserT>
    void parse(ParserT &parser);
  };

  struct ChannelFull {
    string description;
    int32 participant_count = 0;
    int32 administrator_count = 0;
    int32 restricted_count = 0;
    int32 banned_count = 0;
    string invite_link;
    MessageId pinned_message_id;

    int64 sticker_set_id = 0;  // do not forget to store along with access hash

    MessageId migrated_from_max_message_id;
    ChatId migrated_from_chat_id;

    bool can_get_participants = false;
    bool can_set_username = false;
    bool can_set_sticker_set = false;
    bool is_all_history_available = true;

    bool is_changed = true;

    double expires_at = 0.0;
    bool is_expired() const;
  };

  struct SecretChat {
    int64 access_hash = 0;
    UserId user_id;
    SecretChatState state;
    string key_hash;
    int32 ttl = 0;
    int32 date = 0;
    int32 layer = 0;

    bool is_outbound = false;

    bool is_changed = true;        // have new changes not sent to the database except changes visible to the client
    bool need_send_update = true;  // have new changes not sent to the client

    bool is_saved = false;        // is current secret chat version being saved/is saved to the database
    bool is_being_saved = false;  // is current secret chat being saved to the database
    uint64 logevent_id = 0;

    template <class StorerT>
    void store(StorerT &storer) const;

    template <class ParserT>
    void parse(ParserT &parser);
  };

  struct InviteLinkInfo {
    ChatId chat_id;  // TODO DialogId
    ChannelId channel_id;
    string title;
    DialogPhoto photo;
    int32 participant_count = 0;
    vector<UserId> participant_user_ids;

    bool is_chat = false;
    bool is_channel = false;
    bool is_public = false;
    bool is_megagroup = false;
  };

  class UserLogEvent;
  class ChatLogEvent;
  class ChannelLogEvent;
  class SecretChatLogEvent;

  static constexpr int32 MAX_GET_PROFILE_PHOTOS = 100;  // server side limit
  static constexpr size_t MAX_NAME_LENGTH = 255;        // server side limit for first/last name and title
  static constexpr size_t MAX_BIO_LENGTH = 70;          // server side limit

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

  static constexpr int32 USER_FULL_FLAG_IS_BLOCKED = 1 << 0;
  static constexpr int32 USER_FULL_FLAG_HAS_ABOUT = 1 << 1;
  static constexpr int32 USER_FULL_FLAG_HAS_PHOTO = 1 << 2;
  static constexpr int32 USER_FULL_FLAG_HAS_BOT_INFO = 1 << 3;

  static constexpr int32 CHAT_FLAG_USER_IS_CREATOR = 1 << 0;
  static constexpr int32 CHAT_FLAG_USER_WAS_KICKED = 1 << 1;
  static constexpr int32 CHAT_FLAG_USER_HAS_LEFT = 1 << 2;
  static constexpr int32 CHAT_FLAG_ADMINISTRATORS_ENABLED = 1 << 3;
  static constexpr int32 CHAT_FLAG_IS_ADMINISTRATOR = 1 << 4;
  static constexpr int32 CHAT_FLAG_IS_DEACTIVATED = 1 << 5;
  static constexpr int32 CHAT_FLAG_WAS_MIGRATED = 1 << 6;

  static constexpr int32 CHANNEL_FLAG_USER_IS_CREATOR = 1 << 0;
  static constexpr int32 CHANNEL_FLAG_USER_HAS_LEFT = 1 << 2;
  static constexpr int32 CHANNEL_FLAG_IS_BROADCAST = 1 << 5;
  static constexpr int32 CHANNEL_FLAG_IS_PUBLIC = 1 << 6;
  static constexpr int32 CHANNEL_FLAG_IS_VERIFIED = 1 << 7;
  static constexpr int32 CHANNEL_FLAG_IS_MEGAGROUP = 1 << 8;
  static constexpr int32 CHANNEL_FLAG_IS_RESTRICTED = 1 << 9;
  static constexpr int32 CHANNEL_FLAG_ANYONE_CAN_INVITE = 1 << 10;
  static constexpr int32 CHANNEL_FLAG_SIGN_MESSAGES = 1 << 11;
  static constexpr int32 CHANNEL_FLAG_IS_MIN = 1 << 12;
  static constexpr int32 CHANNEL_FLAG_HAS_ACCESS_HASH = 1 << 13;
  static constexpr int32 CHANNEL_FLAG_HAS_ADMIN_RIGHTS = 1 << 14;
  static constexpr int32 CHANNEL_FLAG_HAS_BANNED_RIGHTS = 1 << 15;
  static constexpr int32 CHANNEL_FLAG_HAS_UNBAN_DATE = 1 << 16;
  static constexpr int32 CHANNEL_FLAG_HAS_PARTICIPANT_COUNT = 1 << 17;

  static constexpr int32 CHANNEL_FULL_FLAG_HAS_PARTICIPANT_COUNT = 1 << 0;
  static constexpr int32 CHANNEL_FULL_FLAG_HAS_ADMINISTRATOR_COUNT = 1 << 1;
  static constexpr int32 CHANNEL_FULL_FLAG_HAS_BANNED_COUNT = 1 << 2;
  static constexpr int32 CHANNEL_FULL_FLAG_CAN_GET_PARTICIPANTS = 1 << 3;
  static constexpr int32 CHANNEL_FULL_FLAG_MIGRATED_FROM = 1 << 4;
  static constexpr int32 CHANNEL_FULL_FLAG_HAS_PINNED_MESSAGE = 1 << 5;
  static constexpr int32 CHANNEL_FULL_FLAG_CAN_SET_USERNAME = 1 << 6;
  static constexpr int32 CHANNEL_FULL_FLAG_CAN_SET_STICKERS = 1 << 7;
  static constexpr int32 CHANNEL_FULL_FLAG_HAS_STICKER_SET = 1 << 8;
  static constexpr int32 CHANNEL_FULL_FLAG_HAS_AVAILABLE_MIN_MESSAGE_ID = 1 << 9;
  static constexpr int32 CHANNEL_FULL_FLAG_IS_ALL_HISTORY_HIDDEN = 1 << 10;

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

  static const CSlice INVITE_LINK_URLS[3];

  static bool have_input_peer_user(const User *user, AccessRights access_rights);
  static bool have_input_peer_chat(const Chat *chat, AccessRights access_rights);
  static bool have_input_peer_channel(const Channel *c, AccessRights access_rights);
  static bool have_input_encrypted_peer(const SecretChat *secret_chat, AccessRights access_rights);

  const User *get_user(UserId user_id) const;
  User *get_user(UserId user_id);
  User *get_user_force(UserId user_id);
  User *get_user_force_impl(UserId user_id);

  User *add_user(UserId user_id, const char *source);

  const UserFull *get_user_full(UserId user_id) const;
  UserFull *get_user_full(UserId user_id);

  void send_get_user_full_query(UserId user_id, tl_object_ptr<telegram_api::InputUser> &&input_user,
                                Promise<Unit> &&promise);

  const Chat *get_chat(ChatId chat_id) const;
  Chat *get_chat(ChatId chat_id);
  Chat *get_chat_force(ChatId chat_id);

  Chat *add_chat(ChatId chat_id);

  const ChatFull *get_chat_full(ChatId chat_id) const;
  ChatFull *get_chat_full(ChatId chat_id);

  void send_get_chat_full_query(ChatId chat_id, Promise<Unit> &&promise);

  const Channel *get_channel(ChannelId channel_id) const;
  Channel *get_channel(ChannelId channel_id);
  Channel *get_channel_force(ChannelId channel_id);

  Channel *add_channel(ChannelId channel_id, const char *source);

  const ChannelFull *get_channel_full(ChannelId channel_id) const;
  ChannelFull *get_channel_full(ChannelId channel_id);

  void send_get_channel_full_query(ChannelId channel_id, tl_object_ptr<telegram_api::InputChannel> &&input_channel,
                                   Promise<Unit> &&promise);

  const SecretChat *get_secret_chat(SecretChatId secret_chat_id) const;
  SecretChat *get_secret_chat(SecretChatId secret_chat_id);
  SecretChat *get_secret_chat_force(SecretChatId secret_chat_id);

  SecretChat *add_secret_chat(SecretChatId secret_chat_id);

  static DialogParticipantStatus get_chat_status(const Chat *c);

  static ChannelType get_channel_type(const Channel *c);
  static DialogParticipantStatus get_channel_status(const Channel *c);
  static bool get_channel_sign_messages(const Channel *c);

  void set_my_id(UserId my_id);

  static LinkState get_link_state(tl_object_ptr<telegram_api::ContactLink> &&link);

  void repair_chat_participants(ChatId chat_id);

  static bool is_valid_username(const string &username);

  bool on_update_bot_info(tl_object_ptr<telegram_api::botInfo> &&bot_info);

  void on_update_user_name(User *u, UserId user_id, string &&first_name, string &&last_name, string &&username);
  void on_update_user_phone_number(User *u, UserId user_id, string &&phone_number);
  void on_update_user_photo(User *u, UserId user_id, tl_object_ptr<telegram_api::UserProfilePhoto> &&photo);
  void on_update_user_online(User *u, UserId user_id, tl_object_ptr<telegram_api::UserStatus> &&status);
  void on_update_user_links(User *u, UserId user_id, LinkState outbound, LinkState inbound);

  void do_update_user_photo(User *u, UserId user_id, tl_object_ptr<telegram_api::UserProfilePhoto> &&photo);

  void on_update_user_full_is_blocked(UserFull *user_full, UserId user_id, bool is_blocked);
  bool on_update_user_full_bot_info(UserFull *user_full, UserId user_id, int32 bot_info_version,
                                    tl_object_ptr<telegram_api::botInfo> &&bot_info);
  void invalidate_user_full(UserId user_id);

  void on_update_chat_left(Chat *c, ChatId chat_id, bool left, bool kicked);
  void on_update_chat_participant_count(Chat *c, ChatId chat_id, int32 participant_count, int32 version,
                                        const string &debug_str);
  void on_update_chat_photo(Chat *c, ChatId chat_id, tl_object_ptr<telegram_api::ChatPhoto> &&chat_photo_ptr);
  void on_update_chat_rights(Chat *c, ChatId chat_id, bool is_creator, bool is_administrator,
                             bool everyone_is_administrator);
  void on_update_chat_title(Chat *c, ChatId chat_id, string &&title);
  void on_update_chat_active(Chat *c, ChatId chat_id, bool is_active);
  void on_update_chat_migrated_to_channel_id(Chat *c, ChatId chat_id, ChannelId migrated_to_channel_id);

  bool on_update_chat_full_participants_short(ChatFull *chat_full, ChatId chat_id, int32 version);
  void on_update_chat_full_participants(ChatFull *chat_full, ChatId chat_id, vector<DialogParticipant> participants,
                                        int32 version);
  void on_update_chat_full_invite_link(ChatFull *chat_full,
                                       tl_object_ptr<telegram_api::ExportedChatInvite> &&invite_link_ptr);

  void on_update_channel_photo(Channel *c, ChannelId channel_id,
                               tl_object_ptr<telegram_api::ChatPhoto> &&chat_photo_ptr);
  void on_update_channel_title(Channel *c, ChannelId channel_id, string &&title);
  void on_update_channel_username(Channel *c, ChannelId channel_id, string &&username);
  void on_update_channel_status(Channel *c, ChannelId channel_id, DialogParticipantStatus &&status);

  void on_update_channel_full_invite_link(ChannelFull *channel_full,
                                          tl_object_ptr<telegram_api::ExportedChatInvite> &&invite_link_ptr);
  void on_update_channel_full_pinned_message(ChannelFull *channel_full, MessageId message_id);

  void speculative_add_channel_users(ChannelId channel_id, DialogParticipantStatus status,
                                     DialogParticipantStatus old_status);

  void invalidate_chat_full(ChatId chat_id);

  void on_chat_update(telegram_api::chatEmpty &chat);
  void on_chat_update(telegram_api::chat &chat);
  void on_chat_update(telegram_api::chatForbidden &chat);
  void on_chat_update(telegram_api::channel &channel);
  void on_chat_update(telegram_api::channelForbidden &channel);

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

  void update_user(User *u, UserId user_id, bool from_binlog = false, bool from_database = false);
  void update_chat(Chat *c, ChatId chat_id, bool from_binlog = false, bool from_database = false);
  void update_channel(Channel *c, ChannelId channel_id, bool from_binlog = false, bool from_database = false);
  void update_secret_chat(SecretChat *c, SecretChatId secret_chat_id, bool from_binlog = false,
                          bool from_database = false);

  void update_user_full(UserFull *user_full, UserId user_id);
  void update_chat_full(ChatFull *chat_full, ChatId chat_id);
  void update_channel_full(ChannelFull *channel_full, ChannelId channel_id);

  bool is_chat_full_outdated(ChatFull *chat_full, Chat *c, ChatId chat_id);

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

  static bool is_valid_invite_link(const string &invite_link);

  bool update_invite_link(string &invite_link, tl_object_ptr<telegram_api::ExportedChatInvite> &&invite_link_ptr);

  const DialogParticipant *get_chat_participant(ChatId chat_id, UserId user_id) const;

  const DialogParticipant *get_chat_participant(const ChatFull *chat_full, UserId user_id) const;

  static string get_dialog_administrators_database_key(DialogId dialog_id);

  void load_dialog_administrators(DialogId dialog_id, Promise<Unit> &&promise);

  void on_load_dialog_administrators_from_database(DialogId dialog_id, string value, Promise<Unit> &&promise);

  void on_load_administrator_users_finished(DialogId dialog_id, vector<UserId> user_ids, Result<> result,
                                            Promise<Unit> promise);

  void reload_dialog_administrators(DialogId dialog_id, int32 hash, Promise<Unit> &&promise);

  tl_object_ptr<td_api::UserStatus> get_user_status_object(UserId user_id, const User *u) const;

  static tl_object_ptr<td_api::LinkState> get_link_state_object(LinkState link);

  static tl_object_ptr<td_api::botInfo> get_bot_info_object(const BotInfo *bot_info);

  tl_object_ptr<td_api::user> get_user_object(UserId user_id, const User *u) const;

  tl_object_ptr<td_api::userFullInfo> get_user_full_info_object(UserId user_id, const UserFull *user_full) const;

  tl_object_ptr<td_api::basicGroup> get_basic_group_object(ChatId chat_id, const Chat *chat);

  tl_object_ptr<td_api::basicGroupFullInfo> get_basic_group_full_info_object(const ChatFull *chat_full) const;

  tl_object_ptr<td_api::supergroup> get_supergroup_object(ChannelId channel_id, const Channel *channel) const;

  tl_object_ptr<td_api::supergroupFullInfo> get_channel_full_info_object(const ChannelFull *channel_full) const;

  static tl_object_ptr<td_api::SecretChatState> get_secret_chat_state_object(SecretChatState state);

  tl_object_ptr<td_api::secretChat> get_secret_chat_object(SecretChatId secret_chat_id, const SecretChat *secret_chat);

  void delete_chat_participant(ChatId chat_id, UserId user_id, Promise<Unit> &&promise);

  void change_channel_participant_status_impl(ChannelId channel_id, UserId user_id, DialogParticipantStatus status,
                                              DialogParticipantStatus old_status, Promise<Unit> &&promise);

  void promote_channel_participant(ChannelId channel_id, UserId user_id, DialogParticipantStatus status,
                                   DialogParticipantStatus old_status, Promise<Unit> &&promise);

  void restrict_channel_participant(ChannelId channel_id, UserId user_id, DialogParticipantStatus status,
                                    DialogParticipantStatus old_status, Promise<Unit> &&promise);

  static void on_user_online_timeout_callback(void *contacts_manager_ptr, int64 user_id_long);

  static void on_channel_unban_timeout_callback(void *contacts_manager_ptr, int64 channel_id_long);

  template <class StorerT>
  static void store_link_state(const LinkState &link_state, StorerT &storer);

  template <class ParserT>
  static void parse_link_state(LinkState &link_state, ParserT &parser);

  void tear_down() override;

  Td *td_;
  ActorShared<> parent_;
  UserId my_id_;
  UserId support_user_id_;
  int32 my_was_online_local_ = 0;

  std::unordered_map<UserId, User, UserIdHash> users_;
  std::unordered_map<UserId, UserFull, UserIdHash> users_full_;
  mutable std::unordered_set<UserId, UserIdHash> unknown_users_;
  std::unordered_map<UserId, tl_object_ptr<telegram_api::UserProfilePhoto>, UserIdHash> pending_user_photos_;

  std::unordered_map<ChatId, Chat, ChatIdHash> chats_;
  std::unordered_map<ChatId, ChatFull, ChatIdHash> chats_full_;
  mutable std::unordered_set<ChatId, ChatIdHash> unknown_chats_;

  std::unordered_set<ChannelId, ChannelIdHash> min_channels_;
  std::unordered_map<ChannelId, Channel, ChannelIdHash> channels_;
  std::unordered_map<ChannelId, ChannelFull, ChannelIdHash> channels_full_;
  mutable std::unordered_set<ChannelId, ChannelIdHash> unknown_channels_;

  std::unordered_map<SecretChatId, SecretChat, SecretChatIdHash> secret_chats_;
  mutable std::unordered_set<SecretChatId, SecretChatIdHash> unknown_secret_chats_;

  std::unordered_map<UserId, vector<SecretChatId>, UserIdHash> secret_chats_with_user_;

  std::unordered_map<ChatId, string, ChatIdHash> chat_invite_links_;           // in-memory cache for invite links
  std::unordered_map<ChannelId, string, ChannelIdHash> channel_invite_links_;  // in-memory cache for invite links
  std::unordered_map<string, unique_ptr<InviteLinkInfo>> invite_link_infos_;

  bool created_public_channels_inited_ = false;
  vector<ChannelId> created_public_channels_;

  std::unordered_map<UserId, vector<Promise<Unit>>, UserIdHash> load_user_from_database_queries_;
  std::unordered_set<UserId, UserIdHash> loaded_from_database_users_;

  std::unordered_map<ChatId, vector<Promise<Unit>>, ChatIdHash> load_chat_from_database_queries_;
  std::unordered_set<ChatId, ChatIdHash> loaded_from_database_chats_;

  std::unordered_map<ChannelId, vector<Promise<Unit>>, ChannelIdHash> load_channel_from_database_queries_;
  std::unordered_set<ChannelId, ChannelIdHash> loaded_from_database_channels_;

  std::unordered_map<SecretChatId, vector<Promise<Unit>>, SecretChatIdHash> load_secret_chat_from_database_queries_;
  std::unordered_set<SecretChatId, SecretChatIdHash> loaded_from_database_secret_chats_;

  std::unordered_map<UserId, vector<Promise<Unit>>, UserIdHash> get_user_full_queries_;
  std::unordered_map<ChatId, vector<Promise<Unit>>, ChatIdHash> get_chat_full_queries_;
  std::unordered_map<ChannelId, vector<Promise<Unit>>, ChannelIdHash> get_channel_full_queries_;

  std::unordered_map<DialogId, vector<UserId>, DialogIdHash> dialog_administrators_;

  class UploadProfilePhotoCallback;
  std::shared_ptr<UploadProfilePhotoCallback> upload_profile_photo_callback_;

  std::unordered_map<FileId, Promise<Unit>, FileIdHash> uploaded_profile_photos_;  // file_id -> promise

  std::unordered_map<int64, std::pair<vector<UserId>, vector<int32>>> imported_contacts_;

  std::unordered_map<int64, DialogParticipant> received_channel_participant_;
  std::unordered_map<int64, std::pair<int32, vector<DialogParticipant>>> received_channel_participants_;

  std::unordered_map<int64, std::pair<int32, vector<UserId>>>
      found_blocked_users_;  // random_id -> [total_count, [user_id]...]

  bool are_contacts_loaded_ = false;
  int32 next_contacts_sync_date_ = 0;
  Hints contacts_hints_;  // search contacts by first name, last name and username
  vector<Promise<Unit>> load_contacts_queries_;
  MultiPromiseActor load_contact_users_multipromise_;
  int32 saved_contact_count_ = -1;

  bool are_imported_contacts_loaded_ = false;
  vector<Promise<Unit>> load_imported_contacts_queries_;
  MultiPromiseActor load_imported_contact_users_multipromise_;
  vector<Contact> all_imported_contacts_;
  bool are_imported_contacts_changing_ = false;
  bool need_clear_imported_contacts_ = false;

  vector<Contact> next_all_imported_contacts_;
  vector<size_t> imported_contacts_unique_id_;
  vector<size_t> imported_contacts_pos_;

  vector<UserId> imported_contact_user_ids_;  // result of change_imported_contacts
  vector<int32> unimported_contact_invites_;  // result of change_imported_contacts

  MultiTimeout user_online_timeout_{"UserOnlineTimeout"};
  MultiTimeout channel_unban_timeout_{"ChannelUnbanTimeout"};

  class OnChatUpdate;
};

}  // namespace td
