//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/files/FileId.h"
#include "td/telegram/Location.h"
#include "td/telegram/MessageContent.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/net/NetQuery.h"
#include "td/telegram/Photo.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UserId.h"

#include "td/actor/actor.h"
#include "td/actor/MultiPromise.h"
#include "td/actor/MultiTimeout.h"

#include "td/utils/common.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/Promise.h"
#include "td/utils/Status.h"

#include <utility>

namespace td {

class Td;

class Game;

class InlineQueriesManager final : public Actor {
 public:
  InlineQueriesManager(Td *td, ActorShared<> parent);

  void after_get_difference();

  void answer_inline_query(int64 inline_query_id, bool is_personal,
                           td_api::object_ptr<td_api::inlineQueryResultsButton> &&button,
                           vector<td_api::object_ptr<td_api::InputInlineQueryResult>> &&input_results, int32 cache_time,
                           const string &next_offset, Promise<Unit> &&promise) const;

  void get_simple_web_view_url(UserId bot_user_id, string &&url,
                               const td_api::object_ptr<td_api::themeParameters> &theme, string &&platform,
                               Promise<string> &&promise);

  void send_web_view_data(UserId bot_user_id, string &&button_text, string &&data, Promise<Unit> &&promise) const;

  void answer_web_view_query(const string &web_view_query_id,
                             td_api::object_ptr<td_api::InputInlineQueryResult> &&input_result,
                             Promise<td_api::object_ptr<td_api::sentWebAppMessage>> &&promise) const;

  void get_weather(Location location, Promise<td_api::object_ptr<td_api::currentWeather>> &&promise);

  void send_inline_query(UserId bot_user_id, DialogId dialog_id, Location user_location, const string &query,
                         const string &offset, Promise<td_api::object_ptr<td_api::inlineQueryResults>> &&promise);

  vector<UserId> get_recent_inline_bots(Promise<Unit> &&promise);

  void remove_recent_inline_bot(UserId bot_user_id, Promise<Unit> &&promise);

  const InlineMessageContent *get_inline_message_content(int64 query_id, const string &result_id);

  UserId get_inline_bot_user_id(int64 query_id) const;

  void on_get_inline_query_results(DialogId dialog_id, UserId bot_user_id, uint64 query_hash,
                                   tl_object_ptr<telegram_api::messages_botResults> &&results,
                                   Promise<td_api::object_ptr<td_api::inlineQueryResults>> promise);

  void on_new_query(int64 query_id, UserId sender_user_id, Location user_location,
                    tl_object_ptr<telegram_api::InlineQueryPeerType> peer_type, const string &query,
                    const string &offset);

  void on_chosen_result(UserId user_id, Location user_location, const string &query, const string &result_id,
                        tl_object_ptr<telegram_api::InputBotInlineMessageID> &&input_bot_inline_message_id);

  static string get_inline_message_id(
      tl_object_ptr<telegram_api::InputBotInlineMessageID> &&input_bot_inline_message_id);

 private:
  static constexpr size_t MAX_RECENT_INLINE_BOTS = 20;  // some reasonable value
  static constexpr int32 INLINE_QUERY_DELAY_MS = 400;   // server side limit

  static constexpr int32 BOT_INLINE_MEDIA_RESULT_FLAG_HAS_PHOTO = 1 << 0;
  static constexpr int32 BOT_INLINE_MEDIA_RESULT_FLAG_HAS_DOCUMENT = 1 << 1;
  static constexpr int32 BOT_INLINE_MEDIA_RESULT_FLAG_HAS_TITLE = 1 << 2;
  static constexpr int32 BOT_INLINE_MEDIA_RESULT_FLAG_HAS_DESCRIPTION = 1 << 3;

  Result<tl_object_ptr<telegram_api::InputBotInlineResult>> get_input_bot_inline_result(
      td_api::object_ptr<td_api::InputInlineQueryResult> &&result, bool *is_gallery, bool *force_vertical) const;

  Result<tl_object_ptr<telegram_api::InputBotInlineMessage>> get_inline_message(
      tl_object_ptr<td_api::InputMessageContent> &&input_message_content,
      tl_object_ptr<td_api::ReplyMarkup> &&reply_markup_ptr,
      int32 allowed_media_content_id) const TD_WARN_UNUSED_RESULT;

  bool register_inline_message_content(int64 query_id, const string &result_id, FileId file_id,
                                       tl_object_ptr<telegram_api::BotInlineMessage> &&inline_message,
                                       int32 allowed_media_content_id, bool allow_invoice, Photo *photo = nullptr,
                                       Game *game = nullptr);

  tl_object_ptr<td_api::thumbnail> register_thumbnail(
      tl_object_ptr<telegram_api::WebDocument> &&web_document_ptr) const;

  static string get_web_document_url(const tl_object_ptr<telegram_api::WebDocument> &web_document_ptr);

  static string get_web_document_content_type(const tl_object_ptr<telegram_api::WebDocument> &web_document_ptr);

  bool update_bot_usage(UserId bot_user_id);

  void save_recently_used_bots();

  bool load_recently_used_bots(Promise<Unit> &promise);

  void do_get_weather(DialogId dialog_id, Location location,
                      Promise<td_api::object_ptr<td_api::currentWeather>> &&promise);

  void on_get_weather(td_api::object_ptr<td_api::inlineQueryResults> results,
                      Promise<td_api::object_ptr<td_api::currentWeather>> &&promise);

  td_api::object_ptr<td_api::inlineQueryResults> get_inline_query_results_object(uint64 query_hash);

  static void on_drop_inline_query_result_timeout_callback(void *inline_queries_manager_ptr, int64 query_hash);

  void loop() final;

  void tear_down() final;

  int32 recently_used_bots_loaded_ = 0;  // 0 - not loaded, 1 - load request was sent, 2 - loaded
  MultiPromiseActor resolve_recent_inline_bots_multipromise_{"ResolveRecentInlineBotsMultiPromiseActor"};

  vector<UserId> recently_used_bot_user_ids_;

  struct PendingInlineQuery {
    uint64 query_hash;
    UserId bot_user_id;
    DialogId dialog_id;
    tl_object_ptr<telegram_api::InputPeer> input_peer;
    Location user_location;
    string query;
    string offset;
    Promise<td_api::object_ptr<td_api::inlineQueryResults>> promise;
  };

  double next_inline_query_time_ = 0.0;
  unique_ptr<PendingInlineQuery> pending_inline_query_;
  NetQueryRef sent_query_;

  struct InlineQueryResult {
    td_api::object_ptr<td_api::inlineQueryResults> results;
    double cache_expire_time;
    int32 pending_request_count;
  };

  MultiTimeout drop_inline_query_result_timeout_{"DropInlineQueryResultTimeout"};
  FlatHashMap<uint64, InlineQueryResult> inline_query_results_;  // query_hash -> result

  FlatHashMap<int64, FlatHashMap<string, InlineMessageContent>>
      inline_message_contents_;  // query_id -> [result_id -> inline_message_content]

  FlatHashMap<int64, UserId> query_id_to_bot_user_id_;

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
