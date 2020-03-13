//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/telegram/DialogId.h"
#include "td/telegram/files/FileId.h"
#include "td/telegram/files/FileSourceId.h"
#include "td/telegram/FullMessageId.h"
#include "td/telegram/SecretInputMedia.h"
#include "td/telegram/WebPageId.h"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"
#include "td/actor/Timeout.h"

#include "td/utils/common.h"
#include "td/utils/Status.h"

#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace td {

struct BinlogEvent;

class Td;

class WebPagesManager : public Actor {
 public:
  WebPagesManager(Td *td, ActorShared<> parent);

  WebPagesManager(const WebPagesManager &) = delete;
  WebPagesManager &operator=(const WebPagesManager &) = delete;
  WebPagesManager(WebPagesManager &&) = delete;
  WebPagesManager &operator=(WebPagesManager &&) = delete;
  ~WebPagesManager() override;

  WebPageId on_get_web_page(tl_object_ptr<telegram_api::WebPage> &&web_page_ptr, DialogId owner_dialog_id);

  void on_get_web_page_by_url(const string &url, WebPageId web_page_id, bool from_database);

  void on_get_web_page_instant_view_view_count(WebPageId web_page_id, int32 view_count);

  void register_web_page(WebPageId web_page_id, FullMessageId full_message_id, const char *source);

  void unregister_web_page(WebPageId web_page_id, FullMessageId full_message_id, const char *source);

  bool have_web_page(WebPageId web_page_id) const;

  bool have_web_page_force(WebPageId web_page_id);

  tl_object_ptr<td_api::webPage> get_web_page_object(WebPageId web_page_id) const;

  tl_object_ptr<td_api::webPageInstantView> get_web_page_instant_view_object(WebPageId web_page_id) const;

  int64 get_web_page_preview(td_api::object_ptr<td_api::formattedText> &&text, Promise<Unit> &&promise);

  tl_object_ptr<td_api::webPage> get_web_page_preview_result(int64 request_id);

  WebPageId get_web_page_instant_view(const string &url, bool force_full, bool force, Promise<Unit> &&promise);

  WebPageId get_web_page_by_url(const string &url) const;

  WebPageId get_web_page_by_url(const string &url, Promise<Unit> &&promise);

  void reload_web_page_by_url(const string &url, Promise<Unit> &&promise);

  void on_get_web_page_preview_success(int64 request_id, const string &url,
                                       tl_object_ptr<telegram_api::MessageMedia> &&message_media_ptr,
                                       Promise<Unit> &&promise);

  void on_get_web_page_preview_fail(int64 request_id, const string &url, Status error, Promise<Unit> &&promise);

  SecretInputMedia get_secret_input_media(WebPageId web_page_id) const;

  void on_binlog_web_page_event(BinlogEvent &&event);

  FileSourceId get_url_file_source_id(const string &url);

  string get_web_page_search_text(WebPageId web_page_id) const;

 private:
  static constexpr int32 WEBPAGE_FLAG_HAS_TYPE = 1 << 0;
  static constexpr int32 WEBPAGE_FLAG_HAS_SITE_NAME = 1 << 1;
  static constexpr int32 WEBPAGE_FLAG_HAS_TITLE = 1 << 2;
  static constexpr int32 WEBPAGE_FLAG_HAS_DESCRIPTION = 1 << 3;
  static constexpr int32 WEBPAGE_FLAG_HAS_PHOTO = 1 << 4;
  static constexpr int32 WEBPAGE_FLAG_HAS_EMBEDDED_PREVIEW = 1 << 5;
  static constexpr int32 WEBPAGE_FLAG_HAS_EMBEDDED_PREVIEW_SIZE = 1 << 6;
  static constexpr int32 WEBPAGE_FLAG_HAS_DURATION = 1 << 7;
  static constexpr int32 WEBPAGE_FLAG_HAS_AUTHOR = 1 << 8;
  static constexpr int32 WEBPAGE_FLAG_HAS_DOCUMENT = 1 << 9;
  static constexpr int32 WEBPAGE_FLAG_HAS_INSTANT_VIEW = 1 << 10;
  static constexpr int32 WEBPAGE_FLAG_HAS_DOCUMENTS = 1 << 11;

  class WebPage;

  class WebPageInstantView;

  class WebPageLogEvent;

  void update_web_page(unique_ptr<WebPage> web_page, WebPageId web_page_id, bool from_binlog, bool from_database);

  void update_web_page_instant_view(WebPageId web_page_id, WebPageInstantView &new_instant_view,
                                    WebPageInstantView &&old_instant_view);

  static bool need_use_old_instant_view(const WebPageInstantView &new_instant_view,
                                        const WebPageInstantView &old_instant_view);

  void on_web_page_changed(WebPageId web_page_id, bool have_web_page);

  const WebPage *get_web_page(WebPageId web_page_id) const;

  const WebPageInstantView *get_web_page_instant_view(WebPageId web_page_id) const;

  WebPageId get_web_page_instant_view(WebPageId web_page_id, bool force_full, Promise<Unit> &&promise);

  tl_object_ptr<td_api::webPageInstantView> get_web_page_instant_view_object(
      const WebPageInstantView *web_page_instant_view) const;

  static void on_pending_web_page_timeout_callback(void *web_pages_manager_ptr, int64 web_page_id);
  void on_pending_web_page_timeout(WebPageId web_page_id);

  void on_get_web_page_preview_success(int64 request_id, const string &url, WebPageId web_page_id,
                                       Promise<Unit> &&promise);

  void on_get_web_page_instant_view(WebPage *web_page, tl_object_ptr<telegram_api::page> &&page, int32 hash,
                                    DialogId owner_dialog_id);

  void save_web_page(const WebPage *web_page, WebPageId web_page_id, bool from_binlog);

  static string get_web_page_database_key(WebPageId web_page_id);

  void on_save_web_page_to_database(WebPageId web_page_id, bool success);

  void load_web_page_from_database(WebPageId web_page_id, Promise<Unit> promise);

  void on_load_web_page_from_database(WebPageId web_page_id, string value);

  const WebPage *get_web_page_force(WebPageId web_page_id);

  static string get_web_page_instant_view_database_key(WebPageId web_page_id);

  void load_web_page_instant_view(WebPageId web_page_id, bool force_full, Promise<Unit> &&promise);

  void on_load_web_page_instant_view_from_database(WebPageId web_page_id, string value);

  void reload_web_page_instant_view(WebPageId web_page_id);

  void update_web_page_instant_view_load_requests(WebPageId web_page_id, bool force_update, Result<> result);

  static string get_web_page_url_database_key(const string &url);

  void load_web_page_by_url(const string &url, Promise<Unit> &&promise);

  void on_load_web_page_id_by_url_from_database(const string &url, string value, Promise<Unit> &&promise);

  void on_load_web_page_by_url_from_database(WebPageId web_page_id, const string &url, Promise<Unit> &&promise,
                                             Result<> result);

  void tear_down() override;

  FileSourceId get_web_page_file_source_id(WebPage *web_page);

  vector<FileId> get_web_page_file_ids(const WebPage *web_page) const;

  Td *td_;
  ActorShared<> parent_;
  std::unordered_map<WebPageId, unique_ptr<WebPage>, WebPageIdHash> web_pages_;

  std::unordered_map<WebPageId, vector<Promise<Unit>>, WebPageIdHash> load_web_page_from_database_queries_;
  std::unordered_set<WebPageId, WebPageIdHash> loaded_from_database_web_pages_;

  struct PendingWebPageInstantViewQueries {
    vector<Promise<Unit>> partial;
    vector<Promise<Unit>> full;
  };
  std::unordered_map<WebPageId, PendingWebPageInstantViewQueries, WebPageIdHash> load_web_page_instant_view_queries_;

  std::unordered_map<WebPageId, std::unordered_set<FullMessageId, FullMessageIdHash>, WebPageIdHash> web_page_messages_;

  std::unordered_map<WebPageId, std::unordered_map<int64, std::pair<string, Promise<Unit>>>, WebPageIdHash>
      pending_get_web_pages_;

  int64 get_web_page_preview_request_id_ = 1;
  std::unordered_map<int64, WebPageId> got_web_page_previews_;

  std::unordered_map<string, WebPageId> url_to_web_page_id_;

  std::unordered_map<string, FileSourceId> url_to_file_source_id_;

  MultiTimeout pending_web_pages_timeout_{"PendingWebPagesTimeout"};
};

}  // namespace td
