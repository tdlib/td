//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/WebPagesManager.h"

#include "td/telegram/secret_api.h"

#include "td/telegram/AnimationsManager.h"
#include "td/telegram/AudiosManager.h"
#include "td/telegram/AuthManager.h"
#include "td/telegram/Document.h"
#include "td/telegram/Document.hpp"
#include "td/telegram/DocumentsManager.h"
#include "td/telegram/FileReferenceManager.h"
#include "td/telegram/files/FileId.h"
#include "td/telegram/files/FileManager.h"
#include "td/telegram/files/FileSourceId.h"
#include "td/telegram/Global.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/Photo.h"
#include "td/telegram/StickersManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/TdDb.h"
#include "td/telegram/VideoNotesManager.h"
#include "td/telegram/VideosManager.h"
#include "td/telegram/VoiceNotesManager.h"
#include "td/telegram/WebPageBlock.h"

#include "td/actor/PromiseFuture.h"

#include "td/db/binlog/BinlogEvent.h"
#include "td/db/binlog/BinlogHelper.h"
#include "td/db/SqliteKeyValue.h"
#include "td/db/SqliteKeyValueAsync.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/format.h"
#include "td/utils/HttpUrl.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Slice.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/tl_helpers.h"
#include "td/utils/utf8.h"

namespace td {

class GetWebPagePreviewQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  int64 request_id_;
  string url_;

 public:
  explicit GetWebPagePreviewQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(const string &text, vector<tl_object_ptr<telegram_api::MessageEntity>> &&entities, int64 request_id,
            string url) {
    request_id_ = request_id;
    url_ = std::move(url);

    int32 flags = 0;
    if (!entities.empty()) {
      flags |= telegram_api::messages_getWebPagePreview::ENTITIES_MASK;
    }

    send_query(
        G()->net_query_creator().create(telegram_api::messages_getWebPagePreview(flags, text, std::move(entities))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_getWebPagePreview>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetWebPagePreviewQuery: " << to_string(ptr);
    td->web_pages_manager_->on_get_web_page_preview_success(request_id_, url_, std::move(ptr), std::move(promise_));
  }

  void on_error(uint64 id, Status status) override {
    td->web_pages_manager_->on_get_web_page_preview_fail(request_id_, url_, std::move(status), std::move(promise_));
  }
};

class GetWebPageQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  WebPageId web_page_id_;
  string url_;

 public:
  explicit GetWebPageQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(WebPageId web_page_id, const string &url, int32 hash) {
    web_page_id_ = web_page_id;
    url_ = url;
    send_query(G()->net_query_creator().create(telegram_api::messages_getWebPage(url, hash)));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_getWebPage>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetWebPageQuery: " << to_string(ptr);
    if (ptr->get_id() == telegram_api::webPageNotModified::ID) {
      if (web_page_id_.is_valid()) {
        auto web_page = move_tl_object_as<telegram_api::webPageNotModified>(ptr);
        int32 view_count = (web_page->flags_ & telegram_api::webPageNotModified::CACHED_PAGE_VIEWS_MASK) != 0
                               ? web_page->cached_page_views_
                               : 0;
        td->web_pages_manager_->on_get_web_page_instant_view_view_count(web_page_id_, view_count);
      } else {
        LOG(ERROR) << "Receive webPageNotModified for " << url_;
      }
    } else {
      auto web_page_id = td->web_pages_manager_->on_get_web_page(std::move(ptr), DialogId());
      td->web_pages_manager_->on_get_web_page_by_url(url_, web_page_id, false);
    }

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class WebPagesManager::WebPageInstantView {
 public:
  vector<unique_ptr<WebPageBlock>> page_blocks;
  string url;
  int32 view_count = 0;
  int32 hash = 0;
  bool is_v2 = false;
  bool is_rtl = false;
  bool is_empty = true;
  bool is_full = false;
  bool is_loaded = false;
  bool was_loaded_from_database = false;

  template <class StorerT>
  void store(StorerT &storer) const {
    using ::td::store;
    bool has_url = !url.empty();
    bool has_view_count = view_count > 0;
    BEGIN_STORE_FLAGS();
    STORE_FLAG(is_full);
    STORE_FLAG(is_loaded);
    STORE_FLAG(is_rtl);
    STORE_FLAG(is_v2);
    STORE_FLAG(has_url);
    STORE_FLAG(has_view_count);
    END_STORE_FLAGS();

    store(page_blocks, storer);
    store(hash, storer);
    if (has_url) {
      store(url, storer);
    }
    if (has_view_count) {
      store(view_count, storer);
    }
    CHECK(!is_empty);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using ::td::parse;
    bool has_url;
    bool has_view_count;
    BEGIN_PARSE_FLAGS();
    PARSE_FLAG(is_full);
    PARSE_FLAG(is_loaded);
    PARSE_FLAG(is_rtl);
    PARSE_FLAG(is_v2);
    PARSE_FLAG(has_url);
    PARSE_FLAG(has_view_count);
    END_PARSE_FLAGS();

    parse(page_blocks, parser);
    parse(hash, parser);
    if (has_url) {
      parse(url, parser);
    }
    if (has_view_count) {
      parse(view_count, parser);
    }
    is_empty = false;
  }

  friend StringBuilder &operator<<(StringBuilder &string_builder,
                                   const WebPagesManager::WebPageInstantView &instant_view) {
    return string_builder << "InstantView(url = " << instant_view.url << ", size = " << instant_view.page_blocks.size()
                          << ", view_count = " << instant_view.view_count << ", hash = " << instant_view.hash
                          << ", is_empty = " << instant_view.is_empty << ", is_v2 = " << instant_view.is_v2
                          << ", is_rtl = " << instant_view.is_rtl << ", is_full = " << instant_view.is_full
                          << ", is_loaded = " << instant_view.is_loaded
                          << ", was_loaded_from_database = " << instant_view.was_loaded_from_database << ")";
  }
};

class WebPagesManager::WebPage {
 public:
  string url;
  string display_url;
  string type;
  string site_name;
  string title;
  string description;
  Photo photo;
  string embed_url;
  string embed_type;
  Dimensions embed_dimensions;
  int32 duration = 0;
  string author;
  Document document;
  vector<Document> documents;
  WebPageInstantView instant_view;

  FileSourceId file_source_id;

  mutable uint64 log_event_id = 0;

  template <class StorerT>
  void store(StorerT &storer) const {
    using ::td::store;
    bool has_type = !type.empty();
    bool has_site_name = !site_name.empty();
    bool has_title = !title.empty();
    bool has_description = !description.empty();
    bool has_photo = !photo.is_empty();
    bool has_embed = !embed_url.empty();
    bool has_embed_dimensions = has_embed && embed_dimensions != Dimensions();
    bool has_duration = duration > 0;
    bool has_author = !author.empty();
    bool has_document = !document.empty();
    bool has_instant_view = !instant_view.is_empty;
    bool is_instant_view_v2 = instant_view.is_v2;
    bool has_no_hash = true;
    bool has_documents = !documents.empty();
    BEGIN_STORE_FLAGS();
    STORE_FLAG(has_type);
    STORE_FLAG(has_site_name);
    STORE_FLAG(has_title);
    STORE_FLAG(has_description);
    STORE_FLAG(has_photo);
    STORE_FLAG(has_embed);
    STORE_FLAG(has_embed_dimensions);
    STORE_FLAG(has_duration);
    STORE_FLAG(has_author);
    STORE_FLAG(has_document);
    STORE_FLAG(has_instant_view);
    STORE_FLAG(has_no_hash);
    STORE_FLAG(is_instant_view_v2);
    STORE_FLAG(has_documents);
    END_STORE_FLAGS();

    store(url, storer);
    store(display_url, storer);
    if (has_type) {
      store(type, storer);
    }
    if (has_site_name) {
      store(site_name, storer);
    }
    if (has_title) {
      store(title, storer);
    }
    if (has_description) {
      store(description, storer);
    }
    if (has_photo) {
      store(photo, storer);
    }
    if (has_embed) {
      store(embed_url, storer);
      store(embed_type, storer);
    }
    if (has_embed_dimensions) {
      store(embed_dimensions, storer);
    }
    if (has_duration) {
      store(duration, storer);
    }
    if (has_author) {
      store(author, storer);
    }
    if (has_document) {
      store(document, storer);
    }
    if (has_documents) {
      store(documents, storer);
    }
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using ::td::parse;
    bool has_type;
    bool has_site_name;
    bool has_title;
    bool has_description;
    bool has_photo;
    bool has_embed;
    bool has_embed_dimensions;
    bool has_duration;
    bool has_author;
    bool has_document;
    bool has_instant_view;
    bool is_instant_view_v2;
    bool has_no_hash;
    bool has_documents;
    BEGIN_PARSE_FLAGS();
    PARSE_FLAG(has_type);
    PARSE_FLAG(has_site_name);
    PARSE_FLAG(has_title);
    PARSE_FLAG(has_description);
    PARSE_FLAG(has_photo);
    PARSE_FLAG(has_embed);
    PARSE_FLAG(has_embed_dimensions);
    PARSE_FLAG(has_duration);
    PARSE_FLAG(has_author);
    PARSE_FLAG(has_document);
    PARSE_FLAG(has_instant_view);
    PARSE_FLAG(has_no_hash);
    PARSE_FLAG(is_instant_view_v2);
    PARSE_FLAG(has_documents);
    END_PARSE_FLAGS();

    parse(url, parser);
    parse(display_url, parser);
    if (!has_no_hash) {
      int32 hash;
      parse(hash, parser);
    }
    if (has_type) {
      parse(type, parser);
    }
    if (has_site_name) {
      parse(site_name, parser);
    }
    if (has_title) {
      parse(title, parser);
    }
    if (has_description) {
      parse(description, parser);
    }
    if (has_photo) {
      parse(photo, parser);
    }
    if (has_embed) {
      parse(embed_url, parser);
      parse(embed_type, parser);
    }
    if (has_embed_dimensions) {
      parse(embed_dimensions, parser);
    }
    if (has_duration) {
      parse(duration, parser);
    }
    if (has_author) {
      parse(author, parser);
    }
    if (has_document) {
      parse(document, parser);
    }
    if (has_documents) {
      parse(documents, parser);
    }

    if (has_instant_view) {
      instant_view.is_empty = false;
    }
    if (is_instant_view_v2) {
      instant_view.is_v2 = true;
    }
  }

  friend bool operator==(const WebPage &lhs, const WebPage &rhs) {
    return lhs.url == rhs.url && lhs.display_url == rhs.display_url && lhs.type == rhs.type &&
           lhs.site_name == rhs.site_name && lhs.title == rhs.title && lhs.description == rhs.description &&
           lhs.photo == rhs.photo && lhs.type == rhs.type && lhs.embed_url == rhs.embed_url &&
           lhs.embed_type == rhs.embed_type && lhs.embed_dimensions == rhs.embed_dimensions &&
           lhs.duration == rhs.duration && lhs.author == rhs.author && lhs.document == rhs.document &&
           lhs.documents == rhs.documents && lhs.instant_view.is_empty == rhs.instant_view.is_empty &&
           lhs.instant_view.is_v2 == rhs.instant_view.is_v2;
  }
};

WebPagesManager::WebPagesManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
  pending_web_pages_timeout_.set_callback(on_pending_web_page_timeout_callback);
  pending_web_pages_timeout_.set_callback_data(static_cast<void *>(this));
}

void WebPagesManager::tear_down() {
  parent_.reset();
}

WebPagesManager::~WebPagesManager() = default;

WebPageId WebPagesManager::on_get_web_page(tl_object_ptr<telegram_api::WebPage> &&web_page_ptr,
                                           DialogId owner_dialog_id) {
  CHECK(web_page_ptr != nullptr);
  LOG(DEBUG) << "Got " << to_string(web_page_ptr);
  switch (web_page_ptr->get_id()) {
    case telegram_api::webPageEmpty::ID: {
      auto web_page = move_tl_object_as<telegram_api::webPageEmpty>(web_page_ptr);
      WebPageId web_page_id(web_page->id_);
      if (!web_page_id.is_valid()) {
        LOG_IF(ERROR, web_page_id != WebPageId()) << "Receive invalid " << web_page_id;
        return WebPageId();
      }

      LOG(INFO) << "Got empty " << web_page_id;
      const WebPage *web_page_to_delete = get_web_page(web_page_id);
      if (web_page_to_delete != nullptr) {
        if (web_page_to_delete->log_event_id != 0) {
          LOG(INFO) << "Erase " << web_page_id << " from binlog";
          binlog_erase(G()->td_db()->get_binlog(), web_page_to_delete->log_event_id);
          web_page_to_delete->log_event_id = 0;
        }
        if (web_page_to_delete->file_source_id.is_valid()) {
          td_->file_manager_->change_files_source(web_page_to_delete->file_source_id,
                                                  get_web_page_file_ids(web_page_to_delete), vector<FileId>());
        }
        web_pages_.erase(web_page_id);
      }

      on_web_page_changed(web_page_id, false);
      if (G()->parameters().use_message_db) {
        LOG(INFO) << "Delete " << web_page_id << " from database";
        G()->td_db()->get_sqlite_pmc()->erase(get_web_page_database_key(web_page_id), Auto());
        G()->td_db()->get_sqlite_pmc()->erase(get_web_page_instant_view_database_key(web_page_id), Auto());
      }

      return WebPageId();
    }
    case telegram_api::webPagePending::ID: {
      auto web_page = move_tl_object_as<telegram_api::webPagePending>(web_page_ptr);
      WebPageId web_page_id(web_page->id_);
      if (!web_page_id.is_valid()) {
        LOG(ERROR) << "Receive invalid " << web_page_id;
        return WebPageId();
      }

      auto web_page_date = web_page->date_;
      LOG(INFO) << "Got pending " << web_page_id << ", force_get_date = " << web_page_date
                << ", now = " << G()->server_time();

      pending_web_pages_timeout_.add_timeout_in(web_page_id.get(), max(web_page_date - G()->server_time(), 1.0));
      return web_page_id;
    }
    case telegram_api::webPage::ID: {
      auto web_page = move_tl_object_as<telegram_api::webPage>(web_page_ptr);
      WebPageId web_page_id(web_page->id_);
      if (!web_page_id.is_valid()) {
        LOG(ERROR) << "Receive invalid " << web_page_id;
        return WebPageId();
      }

      LOG(INFO) << "Got " << web_page_id;
      auto page = make_unique<WebPage>();

      page->url = std::move(web_page->url_);
      page->display_url = std::move(web_page->display_url_);
      page->type = std::move(web_page->type_);
      page->site_name = std::move(web_page->site_name_);
      page->title = std::move(web_page->title_);
      page->description = std::move(web_page->description_);
      page->photo = get_photo(td_->file_manager_.get(), std::move(web_page->photo_), owner_dialog_id);
      if (web_page->flags_ & WEBPAGE_FLAG_HAS_EMBEDDED_PREVIEW) {
        page->embed_url = std::move(web_page->embed_url_);
        page->embed_type = std::move(web_page->embed_type_);
      }
      if (web_page->flags_ & WEBPAGE_FLAG_HAS_EMBEDDED_PREVIEW_SIZE) {
        page->embed_dimensions = get_dimensions(web_page->embed_width_, web_page->embed_height_);
      }
      if (web_page->flags_ & WEBPAGE_FLAG_HAS_DURATION) {
        page->duration = web_page->duration_;
        if (page->duration < 0) {
          LOG(ERROR) << "Receive wrong web page duration " << page->duration;
          page->duration = 0;
        }
      }
      if (web_page->flags_ & WEBPAGE_FLAG_HAS_AUTHOR) {
        page->author = std::move(web_page->author_);
      }
      if (web_page->flags_ & WEBPAGE_FLAG_HAS_DOCUMENT) {
        int32 document_id = web_page->document_->get_id();
        if (document_id == telegram_api::document::ID) {
          auto parsed_document = td_->documents_manager_->on_get_document(
              move_tl_object_as<telegram_api::document>(web_page->document_), owner_dialog_id);
          page->document = std::move(parsed_document);
        }
      }
      for (auto &attribute : web_page->attributes_) {
        CHECK(attribute != nullptr);
        page->documents.clear();
        for (auto &document : attribute->documents_) {
          int32 document_id = document->get_id();
          if (document_id == telegram_api::document::ID) {
            auto parsed_document = td_->documents_manager_->on_get_document(
                move_tl_object_as<telegram_api::document>(document), owner_dialog_id);
            if (!parsed_document.empty()) {
              page->documents.push_back(std::move(parsed_document));
            }
          }
        }
        // TODO attribute->settings_
      }
      if (web_page->flags_ & WEBPAGE_FLAG_HAS_INSTANT_VIEW) {
        on_get_web_page_instant_view(page.get(), std::move(web_page->cached_page_), web_page->hash_, owner_dialog_id);
      }

      update_web_page(std::move(page), web_page_id, false, false);
      return web_page_id;
    }
    case telegram_api::webPageNotModified::ID: {
      LOG(ERROR) << "Receive webPageNotModified";
      return WebPageId();
    }
    default:
      UNREACHABLE();
      return WebPageId();
  }
}

void WebPagesManager::update_web_page(unique_ptr<WebPage> web_page, WebPageId web_page_id, bool from_binlog,
                                      bool from_database) {
  LOG(INFO) << "Update " << web_page_id;
  CHECK(web_page != nullptr);

  auto &page = web_pages_[web_page_id];
  auto old_file_ids = get_web_page_file_ids(page.get());
  WebPageInstantView old_instant_view;
  bool is_changed = true;
  if (page != nullptr) {
    if (*page == *web_page) {
      is_changed = false;
    }

    old_instant_view = std::move(page->instant_view);
    web_page->log_event_id = page->log_event_id;
  } else {
    auto it = url_to_file_source_id_.find(web_page->url);
    if (it != url_to_file_source_id_.end()) {
      VLOG(file_references) << "Move " << it->second << " inside of " << web_page_id;
      web_page->file_source_id = it->second;
      url_to_file_source_id_.erase(it);
    }
  }
  page = std::move(web_page);

  update_web_page_instant_view(web_page_id, page->instant_view, std::move(old_instant_view));

  auto new_file_ids = get_web_page_file_ids(page.get());
  if (old_file_ids != new_file_ids) {
    td_->file_manager_->change_files_source(get_web_page_file_source_id(page.get()), old_file_ids, new_file_ids);
  }

  on_get_web_page_by_url(page->url, web_page_id, from_database);

  if (is_changed && !from_database) {
    on_web_page_changed(web_page_id, true);

    save_web_page(page.get(), web_page_id, from_binlog);
  }
}

void WebPagesManager::update_web_page_instant_view(WebPageId web_page_id, WebPageInstantView &new_instant_view,
                                                   WebPageInstantView &&old_instant_view) {
  LOG(INFO) << "Merge new " << new_instant_view << " and old " << old_instant_view;

  bool new_from_database = new_instant_view.was_loaded_from_database;
  bool old_from_database = old_instant_view.was_loaded_from_database;

  if (new_instant_view.is_empty && !new_from_database) {
    // new_instant_view is from server and is empty, need to delete the instant view
    if (G()->parameters().use_message_db && (!old_instant_view.is_empty || !old_from_database)) {
      // we have no instant view and probably want it to be deleted from database
      LOG(INFO) << "Erase instant view of " << web_page_id << " from database";
      new_instant_view.was_loaded_from_database = true;
      G()->td_db()->get_sqlite_pmc()->erase(get_web_page_instant_view_database_key(web_page_id), Auto());
    }
    return;
  }

  if (need_use_old_instant_view(new_instant_view, old_instant_view)) {
    new_instant_view = std::move(old_instant_view);
  }

  if (G()->parameters().use_message_db && !new_instant_view.is_empty && new_instant_view.is_loaded) {
    // we have instant view and probably want it to be saved
    if (!new_from_database && !old_from_database) {
      // if it wasn't loaded from the database, load it first
      auto &load_web_page_instant_view_queries = load_web_page_instant_view_queries_[web_page_id];
      auto previous_queries =
          load_web_page_instant_view_queries.partial.size() + load_web_page_instant_view_queries.full.size();
      if (previous_queries == 0) {
        // try to load it only if there is no pending load queries
        load_web_page_instant_view(web_page_id, false, Auto());
        return;
      }
    }

    if (!new_instant_view.was_loaded_from_database) {
      LOG(INFO) << "Save instant view of " << web_page_id << " to database";
      /*
      if (web_page_id.get() == 0) {
        auto blocks = std::move(new_instant_view.page_blocks);
        new_instant_view.page_blocks.clear();
        for (size_t i = 0; i < blocks.size(); i++) {
          LOG(ERROR) << to_string(blocks[i]->get_page_block_object());
          new_instant_view.page_blocks.push_back(std::move(blocks[i]));
          log_event_store(new_instant_view);
        }
        UNREACHABLE();
      }
      */
      new_instant_view.was_loaded_from_database = true;
      G()->td_db()->get_sqlite_pmc()->set(get_web_page_instant_view_database_key(web_page_id),
                                          log_event_store(new_instant_view).as_slice().str(), Auto());
    }
  }
}

bool WebPagesManager::need_use_old_instant_view(const WebPageInstantView &new_instant_view,
                                                const WebPageInstantView &old_instant_view) {
  if (old_instant_view.is_empty || !old_instant_view.is_loaded) {
    return false;
  }
  if (new_instant_view.is_empty || !new_instant_view.is_loaded) {
    return true;
  }
  if (new_instant_view.is_full != old_instant_view.is_full) {
    return old_instant_view.is_full;
  }

  if (new_instant_view.hash == old_instant_view.hash) {
    // the same instant view
    return !new_instant_view.is_full || old_instant_view.is_full;
  }

  // data in database is always outdated
  return new_instant_view.was_loaded_from_database;
}

void WebPagesManager::on_get_web_page_instant_view_view_count(WebPageId web_page_id, int32 view_count) {
  if (get_web_page_instant_view(web_page_id) == nullptr) {
    return;
  }

  auto *instant_view = &web_pages_[web_page_id]->instant_view;
  CHECK(!instant_view->is_empty);
  if (instant_view->view_count >= view_count) {
    return;
  }
  instant_view->view_count = view_count;
  if (G()->parameters().use_message_db) {
    LOG(INFO) << "Save instant view of " << web_page_id << " to database after updating view count to " << view_count;
    G()->td_db()->get_sqlite_pmc()->set(get_web_page_instant_view_database_key(web_page_id),
                                        log_event_store(*instant_view).as_slice().str(), Auto());
  }
}

void WebPagesManager::on_get_web_page_by_url(const string &url, WebPageId web_page_id, bool from_database) {
  auto &cached_web_page_id = url_to_web_page_id_[url];
  if (!from_database && G()->parameters().use_message_db) {
    if (web_page_id.is_valid()) {
      if (cached_web_page_id != web_page_id) {  // not already saved
        G()->td_db()->get_sqlite_pmc()->set(get_web_page_url_database_key(url), to_string(web_page_id.get()), Auto());
      }
    } else {
      G()->td_db()->get_sqlite_pmc()->erase(get_web_page_url_database_key(url), Auto());
    }
  }

  if (cached_web_page_id.is_valid() && web_page_id.is_valid() && web_page_id != cached_web_page_id) {
    LOG(ERROR) << "Url \"" << url << "\" preview is changed from " << cached_web_page_id << " to " << web_page_id;
  }

  cached_web_page_id = web_page_id;
}

void WebPagesManager::register_web_page(WebPageId web_page_id, FullMessageId full_message_id, const char *source) {
  if (!web_page_id.is_valid()) {
    return;
  }

  LOG(INFO) << "Register " << web_page_id << " from " << full_message_id << " from " << source;
  bool is_inserted = web_page_messages_[web_page_id].insert(full_message_id).second;
  LOG_CHECK(is_inserted) << source << " " << web_page_id << " " << full_message_id;

  if (!td_->auth_manager_->is_bot() && !have_web_page_force(web_page_id)) {
    LOG(INFO) << "Waiting for " << web_page_id << " needed in " << full_message_id;
    pending_web_pages_timeout_.add_timeout_in(web_page_id.get(), 1.0);
  }
}

void WebPagesManager::unregister_web_page(WebPageId web_page_id, FullMessageId full_message_id, const char *source) {
  if (!web_page_id.is_valid()) {
    return;
  }

  LOG(INFO) << "Unregister " << web_page_id << " from " << full_message_id << " from " << source;
  auto &message_ids = web_page_messages_[web_page_id];
  auto is_deleted = message_ids.erase(full_message_id) > 0;
  LOG_CHECK(is_deleted) << source << " " << web_page_id << " " << full_message_id;

  if (message_ids.empty()) {
    web_page_messages_.erase(web_page_id);
    if (pending_get_web_pages_.count(web_page_id) == 0) {
      pending_web_pages_timeout_.cancel_timeout(web_page_id.get());
    }
  }
}

void WebPagesManager::on_get_web_page_preview_success(int64 request_id, const string &url,
                                                      tl_object_ptr<telegram_api::MessageMedia> &&message_media_ptr,
                                                      Promise<Unit> &&promise) {
  CHECK(message_media_ptr != nullptr);
  int32 constructor_id = message_media_ptr->get_id();
  if (constructor_id != telegram_api::messageMediaWebPage::ID) {
    if (constructor_id == telegram_api::messageMediaEmpty::ID) {
      on_get_web_page_preview_success(request_id, url, WebPageId(), std::move(promise));
      return;
    }

    LOG(ERROR) << "Receive " << to_string(message_media_ptr) << " instead of web page";
    on_get_web_page_preview_fail(request_id, url, Status::Error(500, "Receive not web page in GetWebPagePreview"),
                                 std::move(promise));
    return;
  }

  auto message_media_web_page = move_tl_object_as<telegram_api::messageMediaWebPage>(message_media_ptr);
  CHECK(message_media_web_page->webpage_ != nullptr);

  auto web_page_id = on_get_web_page(std::move(message_media_web_page->webpage_), DialogId());
  if (web_page_id.is_valid() && !have_web_page(web_page_id)) {
    pending_get_web_pages_[web_page_id].emplace(request_id,
                                                std::make_pair(url, std::move(promise)));  // TODO MultiPromise ?
    return;
  }

  on_get_web_page_preview_success(request_id, url, web_page_id, std::move(promise));
}

void WebPagesManager::on_get_web_page_preview_success(int64 request_id, const string &url, WebPageId web_page_id,
                                                      Promise<Unit> &&promise) {
  CHECK(web_page_id == WebPageId() || have_web_page(web_page_id));

  CHECK(got_web_page_previews_.find(request_id) == got_web_page_previews_.end());
  got_web_page_previews_[request_id] = web_page_id;

  if (web_page_id.is_valid() && !url.empty()) {
    on_get_web_page_by_url(url, web_page_id, true);
  }

  promise.set_value(Unit());
}

void WebPagesManager::on_get_web_page_preview_fail(int64 request_id, const string &url, Status error,
                                                   Promise<Unit> &&promise) {
  LOG(INFO) << "Clean up getting of web page preview with url \"" << url << '"';
  CHECK(error.is_error());
  promise.set_error(std::move(error));
}

int64 WebPagesManager::get_web_page_preview(td_api::object_ptr<td_api::formattedText> &&text, Promise<Unit> &&promise) {
  if (text == nullptr) {
    promise.set_value(Unit());
    return 0;
  }

  auto r_entities = get_message_entities(td_->contacts_manager_.get(), std::move(text->entities_));
  if (r_entities.is_error()) {
    promise.set_error(r_entities.move_as_error());
    return 0;
  }
  auto entities = r_entities.move_as_ok();

  auto result = fix_formatted_text(text->text_, entities, true, false, true, false);
  if (result.is_error() || text->text_.empty()) {
    promise.set_value(Unit());
    return 0;
  }

  auto url = get_first_url(text->text_, entities);
  if (url.empty()) {
    promise.set_value(Unit());
    return 0;
  }

  LOG(INFO) << "Trying to get web page preview for message \"" << text->text_ << '"';
  int64 request_id = get_web_page_preview_request_id_++;

  auto web_page_id = get_web_page_by_url(url);
  if (web_page_id.is_valid()) {
    got_web_page_previews_[request_id] = web_page_id;
    promise.set_value(Unit());
  } else {
    td_->create_handler<GetWebPagePreviewQuery>(std::move(promise))
        ->send(text->text_, get_input_message_entities(td_->contacts_manager_.get(), entities, "get_web_page_preview"),
               request_id, std::move(url));
  }
  return request_id;
}

tl_object_ptr<td_api::webPage> WebPagesManager::get_web_page_preview_result(int64 request_id) {
  if (request_id == 0) {
    return nullptr;
  }

  auto it = got_web_page_previews_.find(request_id);
  CHECK(it != got_web_page_previews_.end());
  auto web_page_id = it->second;
  got_web_page_previews_.erase(it);
  return get_web_page_object(web_page_id);
}

WebPageId WebPagesManager::get_web_page_instant_view(const string &url, bool force_full, bool force,
                                                     Promise<Unit> &&promise) {
  LOG(INFO) << "Trying to get web page instant view for the url \"" << url << '"';
  auto it = url_to_web_page_id_.find(url);
  if (it != url_to_web_page_id_.end()) {
    if (it->second == WebPageId() && !force) {
      // ignore negative caching
      reload_web_page_by_url(url, std::move(promise));
      return WebPageId();
    }
    return get_web_page_instant_view(it->second, force_full, std::move(promise));
  }

  load_web_page_by_url(url, std::move(promise));
  return WebPageId();
}

WebPageId WebPagesManager::get_web_page_instant_view(WebPageId web_page_id, bool force_full, Promise<Unit> &&promise) {
  LOG(INFO) << "Trying to get web page instant view for " << web_page_id;

  const WebPageInstantView *web_page_instant_view = get_web_page_instant_view(web_page_id);
  if (web_page_instant_view == nullptr) {
    promise.set_value(Unit());
    return WebPageId();
  }

  if (!web_page_instant_view->is_loaded || (force_full && !web_page_instant_view->is_full)) {
    load_web_page_instant_view(web_page_id, force_full, std::move(promise));
    return WebPageId();
  }

  if (force_full) {
    reload_web_page_instant_view(web_page_id);
  }

  promise.set_value(Unit());
  return web_page_id;
}

string WebPagesManager::get_web_page_instant_view_database_key(WebPageId web_page_id) {
  return PSTRING() << "wpiv" << web_page_id.get();
}

void WebPagesManager::load_web_page_instant_view(WebPageId web_page_id, bool force_full, Promise<Unit> &&promise) {
  auto &load_web_page_instant_view_queries = load_web_page_instant_view_queries_[web_page_id];
  auto previous_queries =
      load_web_page_instant_view_queries.partial.size() + load_web_page_instant_view_queries.full.size();
  if (force_full) {
    load_web_page_instant_view_queries.full.push_back(std::move(promise));
  } else {
    load_web_page_instant_view_queries.partial.push_back(std::move(promise));
  }
  LOG(INFO) << "Load " << web_page_id << " instant view, have " << previous_queries << " previous queries";
  if (previous_queries == 0) {
    const WebPageInstantView *web_page_instant_view = get_web_page_instant_view(web_page_id);
    CHECK(web_page_instant_view != nullptr);

    if (G()->parameters().use_message_db && !web_page_instant_view->was_loaded_from_database) {
      LOG(INFO) << "Trying to load " << web_page_id << " instant view from database";
      G()->td_db()->get_sqlite_pmc()->get(
          get_web_page_instant_view_database_key(web_page_id), PromiseCreator::lambda([web_page_id](string value) {
            send_closure(G()->web_pages_manager(), &WebPagesManager::on_load_web_page_instant_view_from_database,
                         web_page_id, std::move(value));
          }));
    } else {
      reload_web_page_instant_view(web_page_id);
    }
  }
}

void WebPagesManager::reload_web_page_instant_view(WebPageId web_page_id) {
  LOG(INFO) << "Reload " << web_page_id << " instant view";
  const WebPage *web_page = get_web_page(web_page_id);
  CHECK(web_page != nullptr && !web_page->instant_view.is_empty);

  auto promise = PromiseCreator::lambda([web_page_id](Result<> result) {
    send_closure(G()->web_pages_manager(), &WebPagesManager::update_web_page_instant_view_load_requests, web_page_id,
                 true, std::move(result));
  });

  if (G()->close_flag()) {
    return promise.set_error(Status::Error(500, "Request aborted"));
  }

  td_->create_handler<GetWebPageQuery>(std::move(promise))
      ->send(web_page_id, web_page->url, web_page->instant_view.is_full ? web_page->instant_view.hash : 0);
}

void WebPagesManager::on_load_web_page_instant_view_from_database(WebPageId web_page_id, string value) {
  if (G()->close_flag()) {
    return;
  }
  CHECK(G()->parameters().use_message_db);
  LOG(INFO) << "Successfully loaded " << web_page_id << " instant view of size " << value.size() << " from database";
  //  G()->td_db()->get_sqlite_pmc()->erase(get_web_page_instant_view_database_key(web_page_id), Auto());
  //  return;

  auto web_page_it = web_pages_.find(web_page_id);
  if (web_page_it == web_pages_.end() || web_page_it->second->instant_view.is_empty) {
    // possible if web page loses preview/instant view
    LOG(WARNING) << "There is no instant view in " << web_page_id;
    if (!value.empty()) {
      G()->td_db()->get_sqlite_pmc()->erase(get_web_page_instant_view_database_key(web_page_id), Auto());
    }
    update_web_page_instant_view_load_requests(web_page_id, true, Unit());
    return;
  }
  WebPage *web_page = web_page_it->second.get();
  auto &web_page_instant_view = web_page->instant_view;
  if (web_page_instant_view.was_loaded_from_database) {
    return;
  }

  WebPageInstantView result;
  if (!value.empty()) {
    auto status = log_event_parse(result, value);
    if (status.is_error()) {
      result = WebPageInstantView();

      LOG(ERROR) << "Erase instant view in " << web_page_id << " from database because of " << status.message();
      G()->td_db()->get_sqlite_pmc()->erase(get_web_page_instant_view_database_key(web_page_id), Auto());
    }
  }
  result.was_loaded_from_database = true;

  auto old_file_ids = get_web_page_file_ids(web_page);

  update_web_page_instant_view(web_page_id, web_page_instant_view, std::move(result));

  auto new_file_ids = get_web_page_file_ids(web_page);
  if (old_file_ids != new_file_ids) {
    td_->file_manager_->change_files_source(get_web_page_file_source_id(web_page), old_file_ids, new_file_ids);
  }

  update_web_page_instant_view_load_requests(web_page_id, false, Unit());
}

void WebPagesManager::update_web_page_instant_view_load_requests(WebPageId web_page_id, bool force_update,
                                                                 Result<> result) {
  // TODO [Error : 0 : Lost promise] on closing
  LOG(INFO) << "Update load requests for " << web_page_id;
  auto it = load_web_page_instant_view_queries_.find(web_page_id);
  if (it == load_web_page_instant_view_queries_.end()) {
    return;
  }
  vector<Promise<Unit>> promises[2];
  promises[0] = std::move(it->second.partial);
  promises[1] = std::move(it->second.full);
  reset_to_empty(it->second.partial);
  reset_to_empty(it->second.full);
  load_web_page_instant_view_queries_.erase(it);

  if (result.is_error()) {
    LOG(INFO) << "Receive error " << result.error() << " for load " << web_page_id;
    combine(promises[0], std::move(promises[1]));
    for (auto &promise : promises[0]) {
      promise.set_error(result.error().clone());
    }
    return;
  }
  LOG(INFO) << "Successfully loaded web page " << web_page_id;

  const WebPageInstantView *web_page_instant_view = get_web_page_instant_view(web_page_id);
  if (web_page_instant_view == nullptr) {
    combine(promises[0], std::move(promises[1]));
    for (auto &promise : promises[0]) {
      promise.set_value(Unit());
    }
    return;
  }
  if (web_page_instant_view->is_loaded) {
    if (web_page_instant_view->is_full) {
      combine(promises[0], std::move(promises[1]));
    }

    for (auto &promise : promises[0]) {
      promise.set_value(Unit());
    }
    reset_to_empty(promises[0]);
  }
  if (!promises[0].empty() || !promises[1].empty()) {
    if (force_update) {
      // protection from cycles
      LOG(ERROR) << "Expected to receive " << web_page_id << " from the server, but didn't receive it";
      combine(promises[0], std::move(promises[1]));
      for (auto &promise : promises[0]) {
        promise.set_value(Unit());
      }
      return;
    }
    auto &load_queries = load_web_page_instant_view_queries_[web_page_id];
    auto old_size = load_queries.partial.size() + load_queries.full.size();
    combine(load_queries.partial, std::move(promises[0]));
    combine(load_queries.full, std::move(promises[1]));
    if (old_size == 0) {
      reload_web_page_instant_view(web_page_id);
    }
  }
}

WebPageId WebPagesManager::get_web_page_by_url(const string &url) const {
  if (url.empty()) {
    return WebPageId();
  }

  LOG(INFO) << "Get web page id for the url \"" << url << '"';

  auto it = url_to_web_page_id_.find(url);
  if (it != url_to_web_page_id_.end()) {
    return it->second;
  }

  return WebPageId();
}

WebPageId WebPagesManager::get_web_page_by_url(const string &url, Promise<Unit> &&promise) {
  LOG(INFO) << "Trying to get web page id for the url \"" << url << '"';

  auto it = url_to_web_page_id_.find(url);
  if (it != url_to_web_page_id_.end()) {
    promise.set_value(Unit());
    return it->second;
  }

  load_web_page_by_url(url, std::move(promise));
  return WebPageId();
}

void WebPagesManager::load_web_page_by_url(const string &url, Promise<Unit> &&promise) {
  if (!G()->parameters().use_message_db) {
    reload_web_page_by_url(url, std::move(promise));
    return;
  }

  LOG(INFO) << "Load \"" << url << '"';
  G()->td_db()->get_sqlite_pmc()->get(get_web_page_url_database_key(url),
                                      PromiseCreator::lambda([url, promise = std::move(promise)](string value) mutable {
                                        send_closure(G()->web_pages_manager(),
                                                     &WebPagesManager::on_load_web_page_id_by_url_from_database, url,
                                                     value, std::move(promise));
                                      }));
}

void WebPagesManager::on_load_web_page_id_by_url_from_database(const string &url, string value,
                                                               Promise<Unit> &&promise) {
  if (G()->close_flag()) {
    return;
  }
  LOG(INFO) << "Successfully loaded url \"" << url << "\" of size " << value.size() << " from database";
  //  G()->td_db()->get_sqlite_pmc()->erase(get_web_page_url_database_key(web_page_id), Auto());
  //  return;

  auto it = url_to_web_page_id_.find(url);
  if (it != url_to_web_page_id_.end()) {
    // URL web page has already been loaded
    promise.set_value(Unit());
    return;
  }
  if (!value.empty()) {
    auto web_page_id = WebPageId(to_integer<int64>(value));
    if (web_page_id.is_valid()) {
      if (have_web_page(web_page_id)) {
        // URL web page has already been loaded
        on_get_web_page_by_url(url, web_page_id, true);
        promise.set_value(Unit());
        return;
      }

      load_web_page_from_database(
          web_page_id,
          PromiseCreator::lambda([web_page_id, url, promise = std::move(promise)](Result<> result) mutable {
            send_closure(G()->web_pages_manager(), &WebPagesManager::on_load_web_page_by_url_from_database, web_page_id,
                         url, std::move(promise), std::move(result));
          }));
      return;
    } else {
      LOG(ERROR) << "Receive invalid " << web_page_id;
    }
  }

  reload_web_page_by_url(url, std::move(promise));
}

void WebPagesManager::on_load_web_page_by_url_from_database(WebPageId web_page_id, const string &url,
                                                            Promise<Unit> &&promise, Result<> result) {
  if (result.is_error()) {
    CHECK(G()->close_flag());
    promise.set_error(Status::Error(500, "Request aborted"));
    return;
  }

  const WebPage *web_page = get_web_page(web_page_id);
  if (web_page == nullptr) {
    reload_web_page_by_url(url, std::move(promise));
    return;
  }

  if (web_page->url != url) {
    on_get_web_page_by_url(url, web_page_id, true);
  }

  promise.set_value(Unit());
}

void WebPagesManager::reload_web_page_by_url(const string &url, Promise<Unit> &&promise) {
  if (G()->close_flag()) {
    return promise.set_error(Status::Error(500, "Request aborted"));
  }

  LOG(INFO) << "Reload url \"" << url << '"';
  td_->create_handler<GetWebPageQuery>(std::move(promise))->send(WebPageId(), url, 0);
}

SecretInputMedia WebPagesManager::get_secret_input_media(WebPageId web_page_id) const {
  if (!web_page_id.is_valid()) {
    return SecretInputMedia{};
  }

  const WebPage *web_page = get_web_page(web_page_id);
  if (web_page == nullptr) {
    return SecretInputMedia{};
  }
  return SecretInputMedia{nullptr, make_tl_object<secret_api::decryptedMessageMediaWebPage>(web_page->url)};
}

bool WebPagesManager::have_web_page(WebPageId web_page_id) const {
  if (!web_page_id.is_valid()) {
    return false;
  }
  return get_web_page(web_page_id) != nullptr;
}

tl_object_ptr<td_api::webPage> WebPagesManager::get_web_page_object(WebPageId web_page_id) const {
  if (!web_page_id.is_valid()) {
    return nullptr;
  }
  const WebPage *web_page = get_web_page(web_page_id);
  if (web_page == nullptr) {
    return nullptr;
  }
  int32 instant_view_version = [web_page] {
    if (web_page->instant_view.is_empty) {
      return 0;
    }
    if (web_page->instant_view.is_v2) {
      return 2;
    }
    return 1;
  }();

  FormattedText description;
  description.text = web_page->description;
  description.entities = find_entities(web_page->description, true);

  auto r_url = parse_url(web_page->display_url);
  if (r_url.is_ok()) {
    Slice host = r_url.ok().host_;
    if (!host.empty() && host.back() == '.') {
      host.truncate(host.size() - 1);
    }

    auto replace_entities = [](Slice text, vector<MessageEntity> &entities, auto replace_url) {
      int32 current_offset = 0;
      for (auto &entity : entities) {
        CHECK(entity.offset >= current_offset);
        text = utf8_utf16_substr(text, static_cast<size_t>(entity.offset - current_offset));
        auto entity_text = utf8_utf16_substr(text, 0, static_cast<size_t>(entity.length));
        text = text.substr(entity_text.size());
        current_offset = entity.offset + entity.length;

        auto replaced_url = replace_url(entity, entity_text);
        if (!replaced_url.empty()) {
          entity = MessageEntity(MessageEntity::Type::TextUrl, entity.offset, entity.length, std::move(replaced_url));
        }
      }
    };

    if (host == "instagram.com" || ends_with(host, ".instagram.com")) {
      replace_entities(description.text, description.entities, [](const MessageEntity &entity, Slice text) {
        if (entity.type == MessageEntity::Type::Mention) {
          return PSTRING() << "https://www.instagram.com/" << text.substr(1) << '/';
        }
        if (entity.type == MessageEntity::Type::Hashtag) {
          return PSTRING() << "https://www.instagram.com/explore/tags/" << url_encode(text.substr(1)) << '/';
        }
        return string();
      });
    } else if (host == "twitter.com" || ends_with(host, ".twitter.com")) {
      replace_entities(description.text, description.entities, [](const MessageEntity &entity, Slice text) {
        if (entity.type == MessageEntity::Type::Mention) {
          return PSTRING() << "https://twitter.com/" << text.substr(1);
        }
        if (entity.type == MessageEntity::Type::Hashtag) {
          return PSTRING() << "https://twitter.com/hashtag/" << url_encode(text.substr(1));
        }
        return string();
      });
    } else if (host == "t.me" || host == "telegram.me" || host == "telegram.dog" || host == "telesco.pe") {
      // leave everything as is
    } else {
      td::remove_if(description.entities,
                    [](const MessageEntity &entity) { return entity.type == MessageEntity::Type::Mention; });

      if (host == "youtube.com" || host == "www.youtube.com") {
        replace_entities(description.text, description.entities, [](const MessageEntity &entity, Slice text) {
          if (entity.type == MessageEntity::Type::Hashtag) {
            return PSTRING() << "https://www.youtube.com/results?search_query=" << url_encode(text);
          }
          return string();
        });
      } else if (host == "music.youtube.com") {
        replace_entities(description.text, description.entities, [](const MessageEntity &entity, Slice text) {
          if (entity.type == MessageEntity::Type::Hashtag) {
            return PSTRING() << "https://music.youtube.com/search?q=" << url_encode(text);
          }
          return string();
        });
      }
    }
  }

  return make_tl_object<td_api::webPage>(
      web_page->url, web_page->display_url, web_page->type, web_page->site_name, web_page->title,
      get_formatted_text_object(description), get_photo_object(td_->file_manager_.get(), web_page->photo),
      web_page->embed_url, web_page->embed_type, web_page->embed_dimensions.width, web_page->embed_dimensions.height,
      web_page->duration, web_page->author,
      web_page->document.type == Document::Type::Animation
          ? td_->animations_manager_->get_animation_object(web_page->document.file_id, "get_web_page_object")
          : nullptr,
      web_page->document.type == Document::Type::Audio
          ? td_->audios_manager_->get_audio_object(web_page->document.file_id)
          : nullptr,
      web_page->document.type == Document::Type::General
          ? td_->documents_manager_->get_document_object(web_page->document.file_id, PhotoFormat::Jpeg)
          : nullptr,
      web_page->document.type == Document::Type::Sticker
          ? td_->stickers_manager_->get_sticker_object(web_page->document.file_id)
          : nullptr,
      web_page->document.type == Document::Type::Video
          ? td_->videos_manager_->get_video_object(web_page->document.file_id)
          : nullptr,
      web_page->document.type == Document::Type::VideoNote
          ? td_->video_notes_manager_->get_video_note_object(web_page->document.file_id)
          : nullptr,
      web_page->document.type == Document::Type::VoiceNote
          ? td_->voice_notes_manager_->get_voice_note_object(web_page->document.file_id)
          : nullptr,
      instant_view_version);
}

tl_object_ptr<td_api::webPageInstantView> WebPagesManager::get_web_page_instant_view_object(
    WebPageId web_page_id) const {
  return get_web_page_instant_view_object(get_web_page_instant_view(web_page_id));
}

tl_object_ptr<td_api::webPageInstantView> WebPagesManager::get_web_page_instant_view_object(
    const WebPageInstantView *web_page_instant_view) const {
  if (web_page_instant_view == nullptr) {
    return nullptr;
  }
  if (!web_page_instant_view->is_loaded) {
    LOG(ERROR) << "Trying to get not loaded web page instant view";
    return nullptr;
  }
  return td_api::make_object<td_api::webPageInstantView>(
      get_page_block_objects(web_page_instant_view->page_blocks, td_, web_page_instant_view->url),
      web_page_instant_view->view_count, web_page_instant_view->is_v2 ? 2 : 1, web_page_instant_view->is_rtl,
      web_page_instant_view->is_full);
}

void WebPagesManager::on_web_page_changed(WebPageId web_page_id, bool have_web_page) {
  LOG(INFO) << "Updated " << web_page_id;
  auto it = web_page_messages_.find(web_page_id);
  if (it != web_page_messages_.end()) {
    vector<FullMessageId> full_message_ids;
    for (auto full_message_id : it->second) {
      full_message_ids.push_back(full_message_id);
    }
    CHECK(!full_message_ids.empty());
    for (auto full_message_id : full_message_ids) {
      if (!have_web_page) {
        td_->messages_manager_->delete_pending_message_web_page(full_message_id);
      } else {
        td_->messages_manager_->on_external_update_message_content(full_message_id);
      }
    }
    if (have_web_page) {
      CHECK(web_page_messages_[web_page_id].size() == full_message_ids.size());
    } else {
      CHECK(web_page_messages_.count(web_page_id) == 0);
    }
  }
  auto get_it = pending_get_web_pages_.find(web_page_id);
  if (get_it != pending_get_web_pages_.end()) {
    auto requests = std::move(get_it->second);
    pending_get_web_pages_.erase(get_it);
    for (auto &request : requests) {
      on_get_web_page_preview_success(request.first, request.second.first, have_web_page ? web_page_id : WebPageId(),
                                      std::move(request.second.second));
    }
  }
  pending_web_pages_timeout_.cancel_timeout(web_page_id.get());
}

const WebPagesManager::WebPage *WebPagesManager::get_web_page(WebPageId web_page_id) const {
  auto p = web_pages_.find(web_page_id);
  if (p == web_pages_.end()) {
    return nullptr;
  } else {
    return p->second.get();
  }
}

const WebPagesManager::WebPageInstantView *WebPagesManager::get_web_page_instant_view(WebPageId web_page_id) const {
  const WebPage *web_page = get_web_page(web_page_id);
  if (web_page == nullptr || web_page->instant_view.is_empty) {
    return nullptr;
  }
  return &web_page->instant_view;
}

void WebPagesManager::on_pending_web_page_timeout_callback(void *web_pages_manager_ptr, int64 web_page_id) {
  static_cast<WebPagesManager *>(web_pages_manager_ptr)->on_pending_web_page_timeout(WebPageId(web_page_id));
}

void WebPagesManager::on_pending_web_page_timeout(WebPageId web_page_id) {
  if (have_web_page(web_page_id)) {
    return;
  }

  int32 count = 0;
  auto it = web_page_messages_.find(web_page_id);
  if (it != web_page_messages_.end()) {
    vector<FullMessageId> full_message_ids;
    for (auto full_message_id : it->second) {
      full_message_ids.push_back(full_message_id);
      count++;
    }
    send_closure_later(G()->messages_manager(), &MessagesManager::get_messages_from_server, std::move(full_message_ids),
                       Promise<Unit>(), nullptr);
  }
  auto get_it = pending_get_web_pages_.find(web_page_id);
  if (get_it != pending_get_web_pages_.end()) {
    auto requests = std::move(get_it->second);
    pending_get_web_pages_.erase(get_it);
    for (auto &request : requests) {
      on_get_web_page_preview_fail(request.first, request.second.first, Status::Error(500, "Request timeout exceeded"),
                                   std::move(request.second.second));
      count++;
    }
  }
  if (count == 0) {
    LOG(WARNING) << "Have no messages and requests waiting for " << web_page_id;
  }
}

void WebPagesManager::on_get_web_page_instant_view(WebPage *web_page, tl_object_ptr<telegram_api::page> &&page,
                                                   int32 hash, DialogId owner_dialog_id) {
  CHECK(page != nullptr);
  std::unordered_map<int64, Photo> photos;
  for (auto &photo_ptr : page->photos_) {
    Photo photo = get_photo(td_->file_manager_.get(), std::move(photo_ptr), owner_dialog_id);
    if (photo.is_empty() || photo.id.get() == 0) {
      LOG(ERROR) << "Receive empty photo in web page instant view for " << web_page->url;
    } else {
      auto photo_id = photo.id.get();
      photos.emplace(photo_id, std::move(photo));
    }
  }
  if (!web_page->photo.is_empty() && web_page->photo.id.get() != 0) {
    photos.emplace(web_page->photo.id.get(), web_page->photo);
  }

  std::unordered_map<int64, FileId> animations;
  std::unordered_map<int64, FileId> audios;
  std::unordered_map<int64, FileId> documents;
  std::unordered_map<int64, FileId> videos;
  std::unordered_map<int64, FileId> voice_notes;
  std::unordered_map<int64, FileId> others;
  auto get_map = [&](Document::Type document_type) {
    switch (document_type) {
      case Document::Type::Animation:
        return &animations;
      case Document::Type::Audio:
        return &audios;
      case Document::Type::General:
        return &documents;
      case Document::Type::Video:
        return &videos;
      case Document::Type::VoiceNote:
        return &voice_notes;
      default:
        return &others;
    }
  };

  for (auto &document_ptr : page->documents_) {
    if (document_ptr->get_id() == telegram_api::document::ID) {
      auto document = move_tl_object_as<telegram_api::document>(document_ptr);
      auto document_id = document->id_;
      auto parsed_document = td_->documents_manager_->on_get_document(std::move(document), owner_dialog_id);
      if (!parsed_document.empty()) {
        get_map(parsed_document.type)->emplace(document_id, parsed_document.file_id);
      }
    }
  }
  if (!others.empty()) {
    auto file_view = td_->file_manager_->get_file_view(others.begin()->second);
    LOG(ERROR) << "Receive document of an unexpected type " << file_view.get_type();
  }

  auto add_document = [&](const Document &document) {
    auto file_view = td_->file_manager_->get_file_view(document.file_id);
    if (file_view.has_remote_location()) {
      get_map(document.type)->emplace(file_view.remote_location().get_id(), document.file_id);
    } else {
      LOG(ERROR) << document.type << " has no remote location";
    }
  };
  if (!web_page->document.empty()) {
    add_document(web_page->document);
  }
  for (auto &document : web_page->documents) {
    add_document(document);
  }

  LOG(INFO) << "Receive a web page instant view with " << page->blocks_.size() << " blocks, " << animations.size()
            << " animations, " << audios.size() << " audios, " << documents.size() << " documents, " << photos.size()
            << " photos, " << videos.size() << " videos and " << voice_notes.size() << " voice notes";
  web_page->instant_view.page_blocks =
      get_web_page_blocks(td_, std::move(page->blocks_), animations, audios, documents, photos, videos, voice_notes);
  web_page->instant_view.view_count = (page->flags_ & telegram_api::page::VIEWS_MASK) != 0 ? page->views_ : 0;
  web_page->instant_view.is_v2 = (page->flags_ & telegram_api::page::V2_MASK) != 0;
  web_page->instant_view.is_rtl = (page->flags_ & telegram_api::page::RTL_MASK) != 0;
  web_page->instant_view.hash = hash;
  web_page->instant_view.url = std::move(page->url_);
  web_page->instant_view.is_empty = false;
  web_page->instant_view.is_full = (page->flags_ & telegram_api::page::PART_MASK) == 0;
  web_page->instant_view.is_loaded = true;

  LOG(DEBUG) << "Receive web page instant view: "
             << to_string(get_web_page_instant_view_object(&web_page->instant_view));
}

class WebPagesManager::WebPageLogEvent {
 public:
  WebPageId web_page_id;
  const WebPage *web_page_in;
  unique_ptr<WebPage> web_page_out;

  WebPageLogEvent() = default;

  WebPageLogEvent(WebPageId web_page_id, const WebPage *web_page) : web_page_id(web_page_id), web_page_in(web_page) {
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    td::store(web_page_id, storer);
    td::store(*web_page_in, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    td::parse(web_page_id, parser);
    td::parse(web_page_out, parser);
  }
};

void WebPagesManager::save_web_page(const WebPage *web_page, WebPageId web_page_id, bool from_binlog) {
  if (!G()->parameters().use_message_db) {
    return;
  }

  CHECK(web_page != nullptr);
  if (!from_binlog) {
    WebPageLogEvent log_event(web_page_id, web_page);
    auto storer = get_log_event_storer(log_event);
    if (web_page->log_event_id == 0) {
      web_page->log_event_id = binlog_add(G()->td_db()->get_binlog(), LogEvent::HandlerType::WebPages, storer);
    } else {
      binlog_rewrite(G()->td_db()->get_binlog(), web_page->log_event_id, LogEvent::HandlerType::WebPages, storer);
    }
  }

  LOG(INFO) << "Save " << web_page_id << " to database";
  G()->td_db()->get_sqlite_pmc()->set(
      get_web_page_database_key(web_page_id), log_event_store(*web_page).as_slice().str(),
      PromiseCreator::lambda([web_page_id](Result<> result) {
        send_closure(G()->web_pages_manager(), &WebPagesManager::on_save_web_page_to_database, web_page_id,
                     result.is_ok());
      }));
}

string WebPagesManager::get_web_page_url_database_key(const string &url) {
  return "wpurl" + url;
}

void WebPagesManager::on_binlog_web_page_event(BinlogEvent &&event) {
  if (!G()->parameters().use_message_db) {
    binlog_erase(G()->td_db()->get_binlog(), event.id_);
    return;
  }

  WebPageLogEvent log_event;
  log_event_parse(log_event, event.data_).ensure();

  auto web_page_id = log_event.web_page_id;
  LOG(INFO) << "Add " << web_page_id << " from binlog";
  auto web_page = std::move(log_event.web_page_out);
  CHECK(web_page != nullptr);

  web_page->log_event_id = event.id_;

  update_web_page(std::move(web_page), web_page_id, true, false);
}

string WebPagesManager::get_web_page_database_key(WebPageId web_page_id) {
  return PSTRING() << "wp" << web_page_id.get();
}

void WebPagesManager::on_save_web_page_to_database(WebPageId web_page_id, bool success) {
  if (G()->close_flag()) {
    return;
  }
  const WebPage *web_page = get_web_page(web_page_id);
  if (web_page == nullptr) {
    LOG(ERROR) << "Can't find " << (success ? "saved " : "failed to save ") << web_page_id;
    return;
  }

  if (!success) {
    LOG(ERROR) << "Failed to save " << web_page_id << " to database";
    save_web_page(web_page, web_page_id, web_page->log_event_id != 0);
  } else {
    LOG(INFO) << "Successfully saved " << web_page_id << " to database";
    if (web_page->log_event_id != 0) {
      LOG(INFO) << "Erase " << web_page_id << " from binlog";
      binlog_erase(G()->td_db()->get_binlog(), web_page->log_event_id);
      web_page->log_event_id = 0;
    }
  }
}

void WebPagesManager::load_web_page_from_database(WebPageId web_page_id, Promise<Unit> promise) {
  if (!G()->parameters().use_message_db || loaded_from_database_web_pages_.count(web_page_id)) {
    promise.set_value(Unit());
    return;
  }

  LOG(INFO) << "Load " << web_page_id << " from database";
  auto &load_web_page_queries = load_web_page_from_database_queries_[web_page_id];
  load_web_page_queries.push_back(std::move(promise));
  if (load_web_page_queries.size() == 1u) {
    G()->td_db()->get_sqlite_pmc()->get(
        get_web_page_database_key(web_page_id), PromiseCreator::lambda([web_page_id](string value) {
          send_closure(G()->web_pages_manager(), &WebPagesManager::on_load_web_page_from_database, web_page_id,
                       std::move(value));
        }));
  }
}

void WebPagesManager::on_load_web_page_from_database(WebPageId web_page_id, string value) {
  if (G()->close_flag()) {
    return;
  }
  if (!loaded_from_database_web_pages_.insert(web_page_id).second) {
    return;
  }

  auto it = load_web_page_from_database_queries_.find(web_page_id);
  vector<Promise<Unit>> promises;
  if (it != load_web_page_from_database_queries_.end()) {
    promises = std::move(it->second);
    CHECK(!promises.empty());
    load_web_page_from_database_queries_.erase(it);
  }

  LOG(INFO) << "Successfully loaded " << web_page_id << " of size " << value.size() << " from database";
  //  G()->td_db()->get_sqlite_pmc()->erase(get_web_page_database_key(web_page_id), Auto());
  //  return;

  if (!have_web_page(web_page_id)) {
    if (!value.empty()) {
      auto result = make_unique<WebPage>();
      auto status = log_event_parse(*result, value);
      if (status.is_error()) {
        LOG(ERROR) << "Failed to parse web page loaded from database: " << status
                   << ", value = " << format::as_hex_dump<4>(Slice(value));
      } else {
        update_web_page(std::move(result), web_page_id, true, true);
      }
    }
  } else {
    // web page has already been loaded from the server
  }

  for (auto &promise : promises) {
    promise.set_value(Unit());
  }
}

bool WebPagesManager::have_web_page_force(WebPageId web_page_id) {
  return get_web_page_force(web_page_id) != nullptr;
}

const WebPagesManager::WebPage *WebPagesManager::get_web_page_force(WebPageId web_page_id) {
  const WebPage *web_page = get_web_page(web_page_id);
  if (web_page != nullptr) {
    return web_page;
  }
  if (!G()->parameters().use_message_db) {
    return nullptr;
  }
  if (loaded_from_database_web_pages_.count(web_page_id)) {
    return nullptr;
  }

  LOG(INFO) << "Trying to load " << web_page_id << " from database";
  on_load_web_page_from_database(web_page_id,
                                 G()->td_db()->get_sqlite_sync_pmc()->get(get_web_page_database_key(web_page_id)));
  return get_web_page(web_page_id);
}

FileSourceId WebPagesManager::get_web_page_file_source_id(WebPage *web_page) {
  if (!web_page->file_source_id.is_valid()) {
    web_page->file_source_id = td_->file_reference_manager_->create_web_page_file_source(web_page->url);
  }
  return web_page->file_source_id;
}

FileSourceId WebPagesManager::get_url_file_source_id(const string &url) {
  auto web_page_id = get_web_page_by_url(url);
  if (web_page_id.is_valid()) {
    const WebPage *web_page = get_web_page(web_page_id);
    if (web_page != nullptr) {
      if (!web_page->file_source_id.is_valid()) {
        web_pages_[web_page_id]->file_source_id =
            td_->file_reference_manager_->create_web_page_file_source(web_page->url);
      }
      return web_page->file_source_id;
    }
  }
  return url_to_file_source_id_[url] = td_->file_reference_manager_->create_web_page_file_source(url);
}

string WebPagesManager::get_web_page_search_text(WebPageId web_page_id) const {
  const WebPage *web_page = get_web_page(web_page_id);
  if (web_page == nullptr) {
    return "";
  }
  return PSTRING() << web_page->title + " " + web_page->description;
}

vector<FileId> WebPagesManager::get_web_page_file_ids(const WebPage *web_page) const {
  if (web_page == nullptr) {
    return vector<FileId>();
  }

  vector<FileId> result = photo_get_file_ids(web_page->photo);
  if (!web_page->document.empty()) {
    web_page->document.append_file_ids(td_, result);
  }
  for (auto &document : web_page->documents) {
    document.append_file_ids(td_, result);
  }
  if (!web_page->instant_view.is_empty) {
    for (auto &page_block : web_page->instant_view.page_blocks) {
      page_block->append_file_ids(td_, result);
    }
  }
  return result;
}

}  // namespace td
