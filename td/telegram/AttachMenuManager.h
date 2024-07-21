//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/files/FileId.h"
#include "td/telegram/files/FileSourceId.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/MessageInputReplyTo.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UserId.h"

#include "td/actor/actor.h"
#include "td/actor/Timeout.h"

#include "td/utils/common.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/Promise.h"
#include "td/utils/Status.h"

namespace td {

class Td;

class AttachMenuManager final : public Actor {
 public:
  AttachMenuManager(Td *td, ActorShared<> parent);

  void init();

  void get_popular_app_bots(const string &offset, int32 limit,
                            Promise<td_api::object_ptr<td_api::foundUsers>> &&promise);

  void get_web_app(UserId bot_user_id, const string &web_app_short_name,
                   Promise<td_api::object_ptr<td_api::foundWebApp>> &&promise);

  void reload_web_app(UserId bot_user_id, const string &web_app_short_name, Promise<Unit> &&promise);

  void request_app_web_view(DialogId dialog_id, UserId bot_user_id, string &&web_app_short_name,
                            string &&start_parameter, const td_api::object_ptr<td_api::themeParameters> &theme,
                            string &&platform, bool allow_write_access, Promise<string> &&promise);

  void request_main_web_view(DialogId dialog_id, UserId bot_user_id, string &&start_parameter,
                             const td_api::object_ptr<td_api::themeParameters> &theme, string &&platform,
                             Promise<td_api::object_ptr<td_api::mainWebApp>> &&promise);

  void request_web_view(DialogId dialog_id, UserId bot_user_id, MessageId top_thread_message_id,
                        td_api::object_ptr<td_api::InputMessageReplyTo> &&reply_to, string &&url,
                        td_api::object_ptr<td_api::themeParameters> &&theme, string &&platform,
                        Promise<td_api::object_ptr<td_api::webAppInfo>> &&promise);

  void open_web_view(int64 query_id, DialogId dialog_id, UserId bot_user_id, MessageId top_thread_message_id,
                     MessageInputReplyTo &&input_reply_to, DialogId as_dialog_id);

  void close_web_view(int64 query_id, Promise<Unit> &&promise);

  void invoke_web_view_custom_method(UserId bot_user_id, const string &method, const string &parameters,
                                     Promise<td_api::object_ptr<td_api::customRequestResult>> &&promise);

  void reload_attach_menu_bots(Promise<Unit> &&promise);

  void get_attach_menu_bot(UserId user_id, Promise<td_api::object_ptr<td_api::attachmentMenuBot>> &&promise);

  void reload_attach_menu_bot(UserId user_id, Promise<Unit> &&promise);

  FileSourceId get_attach_menu_bot_file_source_id(UserId user_id);

  FileSourceId get_web_app_file_source_id(UserId user_id, const string &short_name);

  void toggle_bot_is_added_to_attach_menu(UserId user_id, bool is_added, bool allow_write_access,
                                          Promise<Unit> &&promise);

  void get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const;

  static string get_attach_menu_bots_database_key();

 private:
  static const int32 PING_WEB_VIEW_TIMEOUT = 60;

  void start_up() final;

  void timeout_expired() final;

  void tear_down() final;

  struct AttachMenuBotColor {
    int32 light_color_ = -1;
    int32 dark_color_ = -1;

    template <class StorerT>
    void store(StorerT &storer) const;

    template <class ParserT>
    void parse(ParserT &parser);
  };

  friend bool operator==(const AttachMenuBotColor &lhs, const AttachMenuBotColor &rhs);

  friend bool operator!=(const AttachMenuBotColor &lhs, const AttachMenuBotColor &rhs);

  struct AttachMenuBot {
    bool is_added_ = false;
    UserId user_id_;
    bool supports_self_dialog_ = false;
    bool supports_user_dialogs_ = false;
    bool supports_bot_dialogs_ = false;
    bool supports_group_dialogs_ = false;
    bool supports_broadcast_dialogs_ = false;
    bool request_write_access_ = false;
    bool show_in_attach_menu_ = false;
    bool show_in_side_menu_ = false;
    bool side_menu_disclaimer_needed_ = false;
    string name_;
    AttachMenuBotColor name_color_;
    FileId default_icon_file_id_;
    FileId ios_static_icon_file_id_;
    FileId ios_animated_icon_file_id_;
    FileId android_icon_file_id_;
    FileId macos_icon_file_id_;
    FileId android_side_menu_icon_file_id_;
    FileId ios_side_menu_icon_file_id_;
    FileId macos_side_menu_icon_file_id_;
    AttachMenuBotColor icon_color_;
    FileId placeholder_file_id_;

    static constexpr uint32 CACHE_VERSION = 3;
    uint32 cache_version_ = 0;

    template <class StorerT>
    void store(StorerT &storer) const;

    template <class ParserT>
    void parse(ParserT &parser);
  };

  class AttachMenuBotsLogEvent;

  friend bool operator==(const AttachMenuBot &lhs, const AttachMenuBot &rhs);

  friend bool operator!=(const AttachMenuBot &lhs, const AttachMenuBot &rhs);

  bool is_active() const;

  void on_online(bool is_online);

  static void ping_web_view_static(void *td_void);

  void ping_web_view();

  void schedule_ping_web_view();

  void on_get_web_app(UserId bot_user_id, string web_app_short_name,
                      Result<telegram_api::object_ptr<telegram_api::messages_botApp>> result,
                      Promise<td_api::object_ptr<td_api::foundWebApp>> promise);

  Result<AttachMenuBot> get_attach_menu_bot(tl_object_ptr<telegram_api::attachMenuBot> &&bot);

  td_api::object_ptr<td_api::attachmentMenuBot> get_attachment_menu_bot_object(const AttachMenuBot &bot) const;

  td_api::object_ptr<td_api::updateAttachmentMenuBots> get_update_attachment_menu_bots_object() const;

  void remove_bot_from_attach_menu(UserId user_id);

  void send_update_attach_menu_bots() const;

  void save_attach_menu_bots();

  void on_reload_attach_menu_bots(Result<telegram_api::object_ptr<telegram_api::AttachMenuBots>> &&result);

  void on_get_attach_menu_bot(UserId user_id,
                              Result<telegram_api::object_ptr<telegram_api::attachMenuBotsBot>> &&result,
                              Promise<td_api::object_ptr<td_api::attachmentMenuBot>> &&promise);

  Td *td_;
  ActorShared<> parent_;

  bool is_inited_ = false;
  int64 hash_ = 0;
  vector<AttachMenuBot> attach_menu_bots_;
  FlatHashMap<UserId, FileSourceId, UserIdHash> attach_menu_bot_file_source_ids_;
  vector<Promise<Unit>> reload_attach_menu_bots_queries_;

  FlatHashMap<UserId, FlatHashMap<string, FileSourceId>, UserIdHash> web_app_file_source_ids_;

  struct OpenedWebView {
    DialogId dialog_id_;
    UserId bot_user_id_;
    MessageId top_thread_message_id_;
    MessageInputReplyTo input_reply_to_;
    DialogId as_dialog_id_;
  };
  FlatHashMap<int64, OpenedWebView> opened_web_views_;
  Timeout ping_web_view_timeout_;
};

}  // namespace td
