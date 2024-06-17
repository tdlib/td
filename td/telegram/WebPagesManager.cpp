//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/WebPagesManager.h"

#include "td/telegram/AnimationsManager.h"
#include "td/telegram/AudiosManager.h"
#include "td/telegram/AuthManager.h"
#include "td/telegram/ChatManager.h"
#include "td/telegram/Dependencies.h"
#include "td/telegram/DialogManager.h"
#include "td/telegram/Dimensions.h"
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
#include "td/telegram/PhotoFormat.h"
#include "td/telegram/QuickReplyManager.h"
#include "td/telegram/StickerFormat.h"
#include "td/telegram/StickersManager.h"
#include "td/telegram/StickersManager.hpp"
#include "td/telegram/StoryFullId.h"
#include "td/telegram/StoryId.h"
#include "td/telegram/StoryManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/TdDb.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UserManager.h"
#include "td/telegram/VideoNotesManager.h"
#include "td/telegram/VideosManager.h"
#include "td/telegram/VoiceNotesManager.h"
#include "td/telegram/WebPageBlock.h"

#include "td/db/binlog/BinlogEvent.h"
#include "td/db/binlog/BinlogHelper.h"
#include "td/db/SqliteKeyValue.h"
#include "td/db/SqliteKeyValueAsync.h"

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/format.h"
#include "td/utils/HttpUrl.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Slice.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/tl_helpers.h"
#include "td/utils/utf8.h"

#include <limits>

namespace td {

class GetWebPagePreviewQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::webPage>> promise_;
  unique_ptr<WebPagesManager::GetWebPagePreviewOptions> options_;

 public:
  explicit GetWebPagePreviewQuery(Promise<td_api::object_ptr<td_api::webPage>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(const string &text, vector<tl_object_ptr<telegram_api::MessageEntity>> &&entities,
            unique_ptr<WebPagesManager::GetWebPagePreviewOptions> &&options) {
    options_ = std::move(options);

    int32 flags = 0;
    if (!entities.empty()) {
      flags |= telegram_api::messages_getWebPagePreview::ENTITIES_MASK;
    }

    send_query(
        G()->net_query_creator().create(telegram_api::messages_getWebPagePreview(flags, text, std::move(entities))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_getWebPagePreview>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetWebPagePreviewQuery: " << to_string(ptr);
    td_->web_pages_manager_->on_get_web_page_preview(std::move(options_), std::move(ptr), std::move(promise_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetWebPageQuery final : public Td::ResultHandler {
  Promise<WebPageId> promise_;
  WebPageId web_page_id_;
  string url_;

 public:
  explicit GetWebPageQuery(Promise<WebPageId> &&promise) : promise_(std::move(promise)) {
  }

  void send(WebPageId web_page_id, const string &url, int32 hash) {
    if (url.empty()) {
      return promise_.set_value(WebPageId());
    }

    web_page_id_ = web_page_id;
    url_ = url;
    send_query(G()->net_query_creator().create(telegram_api::messages_getWebPage(url, hash)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_getWebPage>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetWebPageQuery: " << to_string(ptr);
    td_->user_manager_->on_get_users(std::move(ptr->users_), "GetWebPageQuery");
    td_->chat_manager_->on_get_chats(std::move(ptr->chats_), "GetWebPageQuery");
    auto page = std::move(ptr->webpage_);
    if (page->get_id() == telegram_api::webPageNotModified::ID) {
      if (web_page_id_.is_valid()) {
        auto web_page = move_tl_object_as<telegram_api::webPageNotModified>(page);
        int32 view_count = web_page->cached_page_views_;
        td_->web_pages_manager_->on_get_web_page_instant_view_view_count(web_page_id_, view_count);
        return promise_.set_value(std::move(web_page_id_));
      } else {
        LOG(ERROR) << "Receive webPageNotModified for " << url_;
        return on_error(Status::Error(500, "Receive webPageNotModified"));
      }
    }
    auto web_page_id = td_->web_pages_manager_->on_get_web_page(std::move(page), DialogId());
    td_->web_pages_manager_->on_get_web_page_by_url(url_, web_page_id, false);
    promise_.set_value(std::move(web_page_id));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class WebPagesManager::WebPageInstantView {
 public:
  vector<unique_ptr<WebPageBlock>> page_blocks_;
  string url_;
  int32 view_count_ = 0;
  int32 hash_ = 0;
  bool is_v2_ = false;
  bool is_rtl_ = false;
  bool is_empty_ = true;
  bool is_full_ = false;
  bool is_loaded_ = false;
  bool was_loaded_from_database_ = false;

  template <class StorerT>
  void store(StorerT &storer) const {
    using ::td::store;
    bool has_url = !url_.empty();
    bool has_view_count = view_count_ > 0;
    BEGIN_STORE_FLAGS();
    STORE_FLAG(is_full_);
    STORE_FLAG(is_loaded_);
    STORE_FLAG(is_rtl_);
    STORE_FLAG(is_v2_);
    STORE_FLAG(has_url);
    STORE_FLAG(has_view_count);
    END_STORE_FLAGS();

    store(page_blocks_, storer);
    store(hash_, storer);
    if (has_url) {
      store(url_, storer);
    }
    if (has_view_count) {
      store(view_count_, storer);
    }
    CHECK(!is_empty_);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using ::td::parse;
    bool has_url;
    bool has_view_count;
    BEGIN_PARSE_FLAGS();
    PARSE_FLAG(is_full_);
    PARSE_FLAG(is_loaded_);
    PARSE_FLAG(is_rtl_);
    PARSE_FLAG(is_v2_);
    PARSE_FLAG(has_url);
    PARSE_FLAG(has_view_count);
    END_PARSE_FLAGS();

    parse(page_blocks_, parser);
    parse(hash_, parser);
    if (has_url) {
      parse(url_, parser);
    }
    if (has_view_count) {
      parse(view_count_, parser);
    }
    is_empty_ = false;
  }

  friend StringBuilder &operator<<(StringBuilder &string_builder,
                                   const WebPagesManager::WebPageInstantView &instant_view) {
    return string_builder << "InstantView(URL = " << instant_view.url_
                          << ", size = " << instant_view.page_blocks_.size()
                          << ", view_count = " << instant_view.view_count_ << ", hash = " << instant_view.hash_
                          << ", is_empty = " << instant_view.is_empty_ << ", is_v2 = " << instant_view.is_v2_
                          << ", is_rtl = " << instant_view.is_rtl_ << ", is_full = " << instant_view.is_full_
                          << ", is_loaded = " << instant_view.is_loaded_
                          << ", was_loaded_from_database = " << instant_view.was_loaded_from_database_ << ")";
  }
};

class WebPagesManager::WebPage {
 public:
  string url_;
  string display_url_;
  string type_;
  string site_name_;
  string title_;
  string description_;
  Photo photo_;
  string embed_url_;
  string embed_type_;
  Dimensions embed_dimensions_;
  int32 duration_ = 0;
  string author_;
  bool has_large_media_ = false;
  Document document_;
  vector<Document> documents_;
  vector<StoryFullId> story_full_ids_;
  vector<FileId> sticker_ids_;
  WebPageInstantView instant_view_;

  FileSourceId file_source_id_;

  mutable uint64 log_event_id_ = 0;

  template <class StorerT>
  void store(StorerT &storer) const {
    using ::td::store;
    bool has_type = !type_.empty();
    bool has_site_name = !site_name_.empty();
    bool has_title = !title_.empty();
    bool has_description = !description_.empty();
    bool has_photo = !photo_.is_empty();
    bool has_embed = !embed_url_.empty();
    bool has_embed_dimensions = has_embed && embed_dimensions_ != Dimensions();
    bool has_duration = duration_ > 0;
    bool has_author = !author_.empty();
    bool has_document = !document_.empty();
    bool has_instant_view = !instant_view_.is_empty_;
    bool is_instant_view_v2 = instant_view_.is_v2_;
    bool has_no_hash = true;
    bool has_documents = !documents_.empty();
    bool has_story_full_ids = !story_full_ids_.empty();
    bool has_sticker_ids = !sticker_ids_.empty();
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
    STORE_FLAG(has_story_full_ids);
    STORE_FLAG(has_large_media_);
    STORE_FLAG(has_sticker_ids);
    END_STORE_FLAGS();

    store(url_, storer);
    store(display_url_, storer);
    if (has_type) {
      store(type_, storer);
    }
    if (has_site_name) {
      store(site_name_, storer);
    }
    if (has_title) {
      store(title_, storer);
    }
    if (has_description) {
      store(description_, storer);
    }
    if (has_photo) {
      store(photo_, storer);
    }
    if (has_embed) {
      store(embed_url_, storer);
      store(embed_type_, storer);
    }
    if (has_embed_dimensions) {
      store(embed_dimensions_, storer);
    }
    if (has_duration) {
      store(duration_, storer);
    }
    if (has_author) {
      store(author_, storer);
    }
    if (has_document) {
      store(document_, storer);
    }
    if (has_documents) {
      store(documents_, storer);
    }
    if (has_story_full_ids) {
      store(story_full_ids_, storer);
    }
    if (has_sticker_ids) {
      Td *td = storer.context()->td().get_actor_unsafe();
      store(static_cast<uint32>(sticker_ids_.size()), storer);
      for (auto &sticker_id : sticker_ids_) {
        td->stickers_manager_->store_sticker(sticker_id, false, storer, "WebPage");
      }
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
    bool has_story_full_ids;
    bool has_sticker_ids;
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
    PARSE_FLAG(has_story_full_ids);
    PARSE_FLAG(has_large_media_);
    PARSE_FLAG(has_sticker_ids);
    END_PARSE_FLAGS();

    parse(url_, parser);
    parse(display_url_, parser);
    if (!has_no_hash) {
      int32 hash;
      parse(hash, parser);
    }
    if (has_type) {
      parse(type_, parser);
    }
    if (has_site_name) {
      parse(site_name_, parser);
    }
    if (has_title) {
      parse(title_, parser);
    }
    if (has_description) {
      parse(description_, parser);
    }
    if (has_photo) {
      parse(photo_, parser);
    }
    if (has_embed) {
      parse(embed_url_, parser);
      parse(embed_type_, parser);
    }
    if (has_embed_dimensions) {
      parse(embed_dimensions_, parser);
    }
    if (has_duration) {
      parse(duration_, parser);
    }
    if (has_author) {
      parse(author_, parser);
    }
    if (has_document) {
      parse(document_, parser);
    }
    if (has_documents) {
      parse(documents_, parser);
    }
    if (has_story_full_ids) {
      parse(story_full_ids_, parser);
      td::remove_if(story_full_ids_, [](StoryFullId story_full_id) { return !story_full_id.is_server(); });
    }
    if (has_sticker_ids) {
      Td *td = parser.context()->td().get_actor_unsafe();
      uint32 sticker_count;
      parse(sticker_count, parser);
      for (size_t i = 0; i < sticker_count; i++) {
        auto sticker_id = td->stickers_manager_->parse_sticker(false, parser);
        if (sticker_id.is_valid()) {
          sticker_ids_.push_back(sticker_id);
        }
      }
    }

    if (has_instant_view) {
      instant_view_.is_empty_ = false;
    }
    if (is_instant_view_v2) {
      instant_view_.is_v2_ = true;
    }
  }

  friend bool operator==(const WebPage &lhs, const WebPage &rhs) {
    return lhs.url_ == rhs.url_ && lhs.display_url_ == rhs.display_url_ && lhs.type_ == rhs.type_ &&
           lhs.site_name_ == rhs.site_name_ && lhs.title_ == rhs.title_ && lhs.description_ == rhs.description_ &&
           lhs.photo_ == rhs.photo_ && lhs.type_ == rhs.type_ && lhs.embed_url_ == rhs.embed_url_ &&
           lhs.embed_type_ == rhs.embed_type_ && lhs.embed_dimensions_ == rhs.embed_dimensions_ &&
           lhs.duration_ == rhs.duration_ && lhs.author_ == rhs.author_ &&
           lhs.has_large_media_ == rhs.has_large_media_ && lhs.document_ == rhs.document_ &&
           lhs.documents_ == rhs.documents_ && lhs.story_full_ids_ == rhs.story_full_ids_ &&
           lhs.sticker_ids_ == rhs.sticker_ids_ && lhs.instant_view_.is_empty_ == rhs.instant_view_.is_empty_ &&
           lhs.instant_view_.is_v2_ == rhs.instant_view_.is_v2_;
  }
};

WebPagesManager::WebPagesManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
  pending_web_pages_timeout_.set_callback(on_pending_web_page_timeout_callback);
  pending_web_pages_timeout_.set_callback_data(static_cast<void *>(this));
}

void WebPagesManager::tear_down() {
  parent_.reset();

  LOG(DEBUG) << "Have " << web_pages_.calc_size() << " web pages to free";
}

WebPagesManager::~WebPagesManager() {
  Scheduler::instance()->destroy_on_scheduler(G()->get_gc_scheduler_id(), web_pages_, web_page_messages_,
                                              web_page_quick_reply_messages_, url_to_web_page_id_,
                                              url_to_file_source_id_);
}

string WebPagesManager::get_web_page_url(const tl_object_ptr<telegram_api::WebPage> &web_page_ptr) {
  CHECK(web_page_ptr != nullptr);
  switch (web_page_ptr->get_id()) {
    case telegram_api::webPageEmpty::ID:
      return static_cast<const telegram_api::webPageEmpty *>(web_page_ptr.get())->url_;
    case telegram_api::webPagePending::ID:
      return static_cast<const telegram_api::webPagePending *>(web_page_ptr.get())->url_;
    case telegram_api::webPage::ID:
      return static_cast<const telegram_api::webPage *>(web_page_ptr.get())->url_;
    case telegram_api::webPageNotModified::ID:
      LOG(ERROR) << "Receive webPageNotModified";
      return string();
    default:
      UNREACHABLE();
      return string();
  }
}

WebPageId WebPagesManager::on_get_web_page(tl_object_ptr<telegram_api::WebPage> &&web_page_ptr,
                                           DialogId owner_dialog_id) {
  CHECK(web_page_ptr != nullptr);
  if (td_->auth_manager_->is_bot()) {
    return WebPageId();
  }
  LOG(DEBUG) << "Receive " << to_string(web_page_ptr);
  switch (web_page_ptr->get_id()) {
    case telegram_api::webPageEmpty::ID: {
      auto web_page = move_tl_object_as<telegram_api::webPageEmpty>(web_page_ptr);
      WebPageId web_page_id(web_page->id_);
      if (!web_page_id.is_valid()) {
        LOG_IF(ERROR, web_page_id != WebPageId()) << "Receive invalid " << web_page_id;
        return WebPageId();
      }

      LOG(INFO) << "Receive empty " << web_page_id;
      const WebPage *web_page_to_delete = get_web_page(web_page_id);
      if (web_page_to_delete != nullptr) {
        if (web_page_to_delete->log_event_id_ != 0) {
          LOG(INFO) << "Erase " << web_page_id << " from binlog";
          binlog_erase(G()->td_db()->get_binlog(), web_page_to_delete->log_event_id_);
          web_page_to_delete->log_event_id_ = 0;
        }
        if (web_page_to_delete->file_source_id_.is_valid()) {
          td_->file_manager_->change_files_source(web_page_to_delete->file_source_id_,
                                                  get_web_page_file_ids(web_page_to_delete), vector<FileId>());
        }
        web_pages_.erase(web_page_id);
      }

      on_web_page_changed(web_page_id, false);
      if (G()->use_message_database()) {
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
      LOG(INFO) << "Receive pending " << web_page_id << ", force_get_date = " << web_page_date
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

      LOG(INFO) << "Receive " << web_page_id;
      auto page = make_unique<WebPage>();

      page->url_ = std::move(web_page->url_);
      page->display_url_ = std::move(web_page->display_url_);
      page->type_ = std::move(web_page->type_);
      page->site_name_ = std::move(web_page->site_name_);
      page->title_ = std::move(web_page->title_);
      page->description_ = std::move(web_page->description_);
      page->photo_ = get_photo(td_, std::move(web_page->photo_), owner_dialog_id);
      page->embed_url_ = std::move(web_page->embed_url_);
      page->embed_type_ = std::move(web_page->embed_type_);
      page->embed_dimensions_ = get_dimensions(web_page->embed_width_, web_page->embed_height_, "webPage");
      page->duration_ = web_page->duration_;
      if (page->duration_ < 0) {
        LOG(ERROR) << "Receive wrong web page duration " << page->duration_;
        page->duration_ = 0;
      }
      page->author_ = std::move(web_page->author_);
      page->has_large_media_ = web_page->has_large_media_;
      if (web_page->document_ != nullptr) {
        int32 document_id = web_page->document_->get_id();
        if (document_id == telegram_api::document::ID) {
          auto parsed_document = td_->documents_manager_->on_get_document(
              move_tl_object_as<telegram_api::document>(web_page->document_), owner_dialog_id);
          page->document_ = std::move(parsed_document);
        }
      }
      for (auto &attribute_ptr : web_page->attributes_) {
        CHECK(attribute_ptr != nullptr);
        switch (attribute_ptr->get_id()) {
          case telegram_api::webPageAttributeTheme::ID: {
            auto attribute = telegram_api::move_object_as<telegram_api::webPageAttributeTheme>(attribute_ptr);
            for (auto &document : attribute->documents_) {
              int32 document_id = document->get_id();
              if (document_id == telegram_api::document::ID) {
                auto parsed_document = td_->documents_manager_->on_get_document(
                    move_tl_object_as<telegram_api::document>(document), owner_dialog_id);
                if (!parsed_document.empty()) {
                  page->documents_.push_back(std::move(parsed_document));
                }
              }
            }
            // TODO attribute->settings_
            break;
          }
          case telegram_api::webPageAttributeStory::ID: {
            auto attribute = telegram_api::move_object_as<telegram_api::webPageAttributeStory>(attribute_ptr);
            auto dialog_id = DialogId(attribute->peer_);
            auto story_id = StoryId(attribute->id_);
            auto story_full_id = StoryFullId(dialog_id, story_id);
            if (!story_full_id.is_server()) {
              LOG(ERROR) << "Receive " << to_string(attribute);
              break;
            }
            if (attribute->story_ != nullptr) {
              auto actual_story_id = td_->story_manager_->on_get_story(dialog_id, std::move(attribute->story_));
              if (story_id != actual_story_id) {
                LOG(ERROR) << "Receive " << actual_story_id << " instead of " << story_id;
              }
            }
            td_->dialog_manager_->force_create_dialog(dialog_id, "webPageAttributeStory");
            page->story_full_ids_.push_back(story_full_id);
            break;
          }
          case telegram_api::webPageAttributeStickerSet::ID: {
            auto attribute = telegram_api::move_object_as<telegram_api::webPageAttributeStickerSet>(attribute_ptr);
            if (!page->sticker_ids_.empty()) {
              LOG(ERROR) << "Receive duplicate webPageAttributeStickerSet";
            }
            for (auto &sticker : attribute->stickers_) {
              auto sticker_id = td_->stickers_manager_
                                    ->on_get_sticker_document(std::move(sticker), StickerFormat::Unknown,
                                                              "webPageAttributeStickerSet")
                                    .second;
              if (sticker_id.is_valid() && page->sticker_ids_.size() < 4) {
                page->sticker_ids_.push_back(sticker_id);
              }
            }
            break;
          }
          default:
            UNREACHABLE();
        }
      }
      if (web_page->cached_page_ != nullptr) {
        on_get_web_page_instant_view(page.get(), std::move(web_page->cached_page_), web_page->hash_, owner_dialog_id);
      }

      update_web_page(std::move(page), web_page_id, false, false);
      return web_page_id;
    }
    case telegram_api::webPageNotModified::ID:
      LOG(ERROR) << "Receive webPageNotModified";
      return WebPageId();
    default:
      UNREACHABLE();
      return WebPageId();
  }
}

void WebPagesManager::update_web_page(unique_ptr<WebPage> web_page, WebPageId web_page_id, bool from_binlog,
                                      bool from_database) {
  LOG(INFO) << "Update " << web_page_id << (from_database ? " from database" : (from_binlog ? " from binlog" : ""));
  CHECK(web_page != nullptr);

  if (from_binlog || from_database) {
    if (!web_page->story_full_ids_.empty()) {
      Dependencies dependencies;
      for (auto story_full_id : web_page->story_full_ids_) {
        dependencies.add(story_full_id);
      }
      if (!dependencies.resolve_force(td_, "update_web_page")) {
        web_page->story_full_ids_ = {};
      }
    }
  }

  auto &page = web_pages_[web_page_id];
  auto old_file_ids = get_web_page_file_ids(page.get());
  WebPageInstantView old_instant_view;
  bool is_changed = true;
  if (page != nullptr) {
    if (*page == *web_page) {
      is_changed = false;
    }

    if (page->story_full_ids_ != web_page->story_full_ids_) {
      for (auto story_full_id : page->story_full_ids_) {
        auto it = story_web_pages_.find(story_full_id);
        if (it != story_web_pages_.end()) {
          it->second.erase(web_page_id);
          if (it->second.empty()) {
            story_web_pages_.erase(it);
          }
        }
      }
      for (auto story_full_id : web_page->story_full_ids_) {
        story_web_pages_[story_full_id].insert(web_page_id);
      }
    }

    old_instant_view = std::move(page->instant_view_);
    web_page->log_event_id_ = page->log_event_id_;
  } else {
    auto it = url_to_file_source_id_.find(web_page->url_);
    if (it != url_to_file_source_id_.end()) {
      VLOG(file_references) << "Move " << it->second << " inside of " << web_page_id;
      web_page->file_source_id_ = it->second;
      url_to_file_source_id_.erase(it);
    }
  }
  page = std::move(web_page);

  // must be called before any other action for correct behavior of get_url_file_source_id
  if (!page->url_.empty()) {
    on_get_web_page_by_url(page->url_, web_page_id, from_database);
  }

  update_web_page_instant_view(web_page_id, page->instant_view_, std::move(old_instant_view));

  auto new_file_ids = get_web_page_file_ids(page.get());
  if (old_file_ids != new_file_ids) {
    td_->file_manager_->change_files_source(get_web_page_file_source_id(page.get()), old_file_ids, new_file_ids);
  }

  if (is_changed && !from_database) {
    on_web_page_changed(web_page_id, true);

    save_web_page(page.get(), web_page_id, from_binlog);
  }
}

void WebPagesManager::update_web_page_instant_view(WebPageId web_page_id, WebPageInstantView &new_instant_view,
                                                   WebPageInstantView &&old_instant_view) {
  LOG(INFO) << "Merge new " << new_instant_view << " and old " << old_instant_view;

  bool new_from_database = new_instant_view.was_loaded_from_database_;
  bool old_from_database = old_instant_view.was_loaded_from_database_;

  if (new_instant_view.is_empty_ && !new_from_database) {
    // new_instant_view is from server and is empty, need to delete the instant view
    if (G()->use_message_database() && (!old_instant_view.is_empty_ || !old_from_database)) {
      // we have no instant view and probably want it to be deleted from database
      LOG(INFO) << "Erase instant view of " << web_page_id << " from database";
      new_instant_view.was_loaded_from_database_ = true;
      G()->td_db()->get_sqlite_pmc()->erase(get_web_page_instant_view_database_key(web_page_id), Auto());
    }
    return;
  }

  if (need_use_old_instant_view(new_instant_view, old_instant_view)) {
    new_instant_view = std::move(old_instant_view);
  }

  if (G()->use_message_database() && !new_instant_view.is_empty_ && new_instant_view.is_loaded_) {
    // we have instant view and probably want it to be saved
    if (!new_from_database && !old_from_database) {
      // if it wasn't loaded from the database, load it first
      auto &load_web_page_instant_view_queries = load_web_page_instant_view_queries_[web_page_id];
      auto previous_queries =
          load_web_page_instant_view_queries.partial.size() + load_web_page_instant_view_queries.full.size();
      if (previous_queries == 0) {
        // try to load it only if there are no pending load queries
        load_web_page_instant_view(web_page_id, false, Auto());
        return;
      }
    }

    if (!new_instant_view.was_loaded_from_database_) {
      LOG(INFO) << "Save instant view of " << web_page_id << " to database";
      /*
      if (web_page_id.get() == 0) {
        auto blocks = std::move(new_instant_view.page_blocks_);
        new_instant_view.page_blocks_.clear();
        for (size_t i = 0; i < blocks.size(); i++) {
          LOG(ERROR) << to_string(blocks[i]->get_page_block_object());
          new_instant_view.page_blocks_.push_back(std::move(blocks[i]));
          log_event_store(new_instant_view);
        }
        UNREACHABLE();
      }
      */
      new_instant_view.was_loaded_from_database_ = true;
      G()->td_db()->get_sqlite_pmc()->set(get_web_page_instant_view_database_key(web_page_id),
                                          log_event_store(new_instant_view).as_slice().str(), Auto());
    }
  }
}

bool WebPagesManager::need_use_old_instant_view(const WebPageInstantView &new_instant_view,
                                                const WebPageInstantView &old_instant_view) {
  if (old_instant_view.is_empty_ || !old_instant_view.is_loaded_) {
    return false;
  }
  if (new_instant_view.is_empty_ || !new_instant_view.is_loaded_) {
    return true;
  }
  if (new_instant_view.is_full_ != old_instant_view.is_full_) {
    return old_instant_view.is_full_;
  }

  if (new_instant_view.hash_ == old_instant_view.hash_) {
    // the same instant view
    return !new_instant_view.is_full_ || old_instant_view.is_full_;
  }

  // data in database is always outdated
  return new_instant_view.was_loaded_from_database_;
}

void WebPagesManager::on_get_web_page_instant_view_view_count(WebPageId web_page_id, int32 view_count) {
  if (get_web_page_instant_view(web_page_id) == nullptr) {
    return;
  }

  auto *instant_view = &web_pages_[web_page_id]->instant_view_;
  CHECK(!instant_view->is_empty_);
  if (instant_view->view_count_ >= view_count) {
    return;
  }
  instant_view->view_count_ = view_count;
  if (G()->use_message_database()) {
    LOG(INFO) << "Save instant view of " << web_page_id << " to database after updating view count to " << view_count;
    G()->td_db()->get_sqlite_pmc()->set(get_web_page_instant_view_database_key(web_page_id),
                                        log_event_store(*instant_view).as_slice().str(), Auto());
  }
}

void WebPagesManager::on_get_web_page_by_url(const string &url, WebPageId web_page_id, bool from_database) {
  if (url.empty()) {
    return;
  }
  auto emplace_result = url_to_web_page_id_.emplace(url, std::make_pair(web_page_id, from_database));
  auto &it = emplace_result.first;
  bool is_inserted = emplace_result.second;
  if (from_database && !it->second.second) {
    // database data can't replace non-database data
    CHECK(!is_inserted);
    return;
  }
  auto &cached_web_page_id = it->second.first;
  if (!from_database && G()->use_message_database() && (cached_web_page_id != web_page_id || is_inserted)) {
    if (web_page_id.is_valid()) {
      G()->td_db()->get_sqlite_pmc()->set(get_web_page_url_database_key(url), to_string(web_page_id.get()), Auto());
    } else {
      G()->td_db()->get_sqlite_pmc()->erase(get_web_page_url_database_key(url), Auto());
    }
  }

  if (!is_inserted) {
    if (cached_web_page_id.is_valid() && !it->second.second && web_page_id.is_valid() &&
        web_page_id != cached_web_page_id) {
      LOG(ERROR) << "URL \"" << url << "\" preview is changed from " << cached_web_page_id << " to " << web_page_id;
    }
    cached_web_page_id = web_page_id;
    it->second.second = from_database;
  }
}

void WebPagesManager::register_web_page(WebPageId web_page_id, MessageFullId message_full_id, const char *source) {
  if (!web_page_id.is_valid()) {
    return;
  }

  LOG(INFO) << "Register " << web_page_id << " from " << message_full_id << " from " << source;
  bool is_inserted = web_page_messages_[web_page_id].insert(message_full_id).second;
  LOG_CHECK(is_inserted) << source << " " << web_page_id << " " << message_full_id;

  if (!td_->auth_manager_->is_bot() && !have_web_page_force(web_page_id)) {
    LOG(INFO) << "Waiting for " << web_page_id << " needed in " << message_full_id;
    pending_web_pages_timeout_.add_timeout_in(web_page_id.get(), 1.0);
  }
}

void WebPagesManager::unregister_web_page(WebPageId web_page_id, MessageFullId message_full_id, const char *source) {
  if (!web_page_id.is_valid()) {
    return;
  }

  LOG(INFO) << "Unregister " << web_page_id << " from " << message_full_id << " from " << source;
  auto &message_ids = web_page_messages_[web_page_id];
  auto is_deleted = message_ids.erase(message_full_id) > 0;
  LOG_CHECK(is_deleted) << source << " " << web_page_id << " " << message_full_id;

  if (message_ids.empty()) {
    web_page_messages_.erase(web_page_id);
  }
}

void WebPagesManager::register_quick_reply_web_page(WebPageId web_page_id, QuickReplyMessageFullId message_full_id,
                                                    const char *source) {
  if (!web_page_id.is_valid()) {
    return;
  }

  LOG(INFO) << "Register " << web_page_id << " from " << message_full_id << " from " << source;
  bool is_inserted = web_page_quick_reply_messages_[web_page_id].insert(message_full_id).second;
  LOG_CHECK(is_inserted) << source << " " << web_page_id << " " << message_full_id;

  if (!have_web_page_force(web_page_id)) {
    LOG(INFO) << "Waiting for " << web_page_id << " needed in " << message_full_id;
    pending_web_pages_timeout_.add_timeout_in(web_page_id.get(), 1.0);
  }
}

void WebPagesManager::unregister_quick_reply_web_page(WebPageId web_page_id, QuickReplyMessageFullId message_full_id,
                                                      const char *source) {
  if (!web_page_id.is_valid()) {
    return;
  }

  LOG(INFO) << "Unregister " << web_page_id << " from " << message_full_id << " from " << source;
  auto &message_ids = web_page_quick_reply_messages_[web_page_id];
  auto is_deleted = message_ids.erase(message_full_id) > 0;
  LOG_CHECK(is_deleted) << source << " " << web_page_id << " " << message_full_id;

  if (message_ids.empty()) {
    web_page_quick_reply_messages_.erase(web_page_id);
  }
}

void WebPagesManager::on_get_web_page_preview(unique_ptr<GetWebPagePreviewOptions> &&options,
                                              tl_object_ptr<telegram_api::MessageMedia> &&message_media_ptr,
                                              Promise<td_api::object_ptr<td_api::webPage>> &&promise) {
  CHECK(message_media_ptr != nullptr);
  int32 constructor_id = message_media_ptr->get_id();
  if (constructor_id != telegram_api::messageMediaWebPage::ID) {
    if (constructor_id == telegram_api::messageMediaEmpty::ID) {
      on_get_web_page_preview_success(std::move(options), WebPageId(), std::move(promise));
      return;
    }

    LOG(ERROR) << "Receive " << to_string(message_media_ptr) << " instead of web page";
    return promise.set_error(Status::Error(500, "Receive not web page in GetWebPagePreview"));
  }

  auto message_media_web_page = move_tl_object_as<telegram_api::messageMediaWebPage>(message_media_ptr);
  CHECK(message_media_web_page->webpage_ != nullptr);

  auto web_page_id = on_get_web_page(std::move(message_media_web_page->webpage_), DialogId());
  if (web_page_id.is_valid() && !have_web_page(web_page_id)) {
    pending_get_web_pages_[web_page_id].emplace_back(std::move(options), std::move(promise));
    return;
  }

  on_get_web_page_preview_success(std::move(options), web_page_id, std::move(promise));
}

void WebPagesManager::on_get_web_page_preview_success(unique_ptr<GetWebPagePreviewOptions> &&options,
                                                      WebPageId web_page_id,
                                                      Promise<td_api::object_ptr<td_api::webPage>> &&promise) {
  CHECK(web_page_id == WebPageId() || have_web_page(web_page_id));
  CHECK(options != nullptr);
  CHECK(options->link_preview_options_ != nullptr);

  if (web_page_id.is_valid() && !options->first_url_.empty()) {
    on_get_web_page_by_url(options->first_url_, web_page_id, true);
  }

  promise.set_value(get_web_page_object(web_page_id, options->link_preview_options_->force_small_media_,
                                        options->link_preview_options_->force_large_media_, options->skip_confirmation_,
                                        options->link_preview_options_->show_above_text_));
}

void WebPagesManager::get_web_page_preview(td_api::object_ptr<td_api::formattedText> &&text,
                                           td_api::object_ptr<td_api::linkPreviewOptions> &&link_preview_options,
                                           Promise<td_api::object_ptr<td_api::webPage>> &&promise) {
  TRY_RESULT_PROMISE(
      promise, formatted_text,
      get_formatted_text(td_, DialogId(), std::move(text), td_->auth_manager_->is_bot(), true, true, true));

  if (link_preview_options == nullptr) {
    link_preview_options = td_api::make_object<td_api::linkPreviewOptions>();
  }
  if (link_preview_options->is_disabled_) {
    return promise.set_value(nullptr);
  }
  auto url = link_preview_options->url_.empty() ? get_first_url(formatted_text).str() : link_preview_options->url_;
  if (url.empty()) {
    return promise.set_value(nullptr);
  }

  LOG(INFO) << "Trying to get web page preview for \"" << url << '"';

  auto web_page_id = get_web_page_by_url(url);
  bool skip_confirmation = is_visible_url(formatted_text, url);
  if (web_page_id.is_valid()) {
    return promise.set_value(get_web_page_object(web_page_id, link_preview_options->force_small_media_,
                                                 link_preview_options->force_large_media_, skip_confirmation,
                                                 link_preview_options->show_above_text_));
  }
  if (!link_preview_options->url_.empty()) {
    formatted_text.text = link_preview_options->url_, formatted_text.entities.clear();
  }
  auto options = make_unique<GetWebPagePreviewOptions>();
  options->first_url_ = std::move(url);
  options->skip_confirmation_ = skip_confirmation;
  options->link_preview_options_ = std::move(link_preview_options);
  td_->create_handler<GetWebPagePreviewQuery>(std::move(promise))
      ->send(formatted_text.text,
             get_input_message_entities(td_->user_manager_.get(), formatted_text.entities, "get_web_page_preview"),
             std::move(options));
}

void WebPagesManager::get_web_page_instant_view(const string &url, bool force_full, Promise<WebPageId> &&promise) {
  LOG(INFO) << "Trying to get web page instant view for the URL \"" << url << '"';
  if (url.empty()) {
    return promise.set_value(WebPageId());
  }
  auto it = url_to_web_page_id_.find(url);
  if (it != url_to_web_page_id_.end()) {
    auto web_page_id = it->second.first;
    if (web_page_id == WebPageId()) {
      // ignore negative caching
      return reload_web_page_by_url(url, std::move(promise));
    }
    return get_web_page_instant_view_impl(web_page_id, force_full, std::move(promise));
  }

  auto new_promise = PromiseCreator::lambda(
      [actor_id = actor_id(this), force_full, promise = std::move(promise)](Result<WebPageId> r_web_page_id) mutable {
        if (r_web_page_id.is_error()) {
          promise.set_error(r_web_page_id.move_as_error());
        } else {
          send_closure(actor_id, &WebPagesManager::get_web_page_instant_view_impl, r_web_page_id.ok(), force_full,
                       std::move(promise));
        }
      });
  load_web_page_by_url(url, std::move(new_promise));
}

void WebPagesManager::get_web_page_instant_view_impl(WebPageId web_page_id, bool force_full,
                                                     Promise<WebPageId> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());

  LOG(INFO) << "Trying to get web page instant view for " << web_page_id;

  const WebPageInstantView *web_page_instant_view = get_web_page_instant_view(web_page_id);
  if (web_page_instant_view == nullptr) {
    return promise.set_value(WebPageId());
  }

  if (!web_page_instant_view->is_loaded_ || (force_full && !web_page_instant_view->is_full_)) {
    return load_web_page_instant_view(web_page_id, force_full, std::move(promise));
  }

  if (force_full) {
    reload_web_page_instant_view(web_page_id);
  }

  promise.set_value(std::move(web_page_id));
}

string WebPagesManager::get_web_page_instant_view_database_key(WebPageId web_page_id) {
  return PSTRING() << "wpiv" << web_page_id.get();
}

void WebPagesManager::load_web_page_instant_view(WebPageId web_page_id, bool force_full, Promise<WebPageId> &&promise) {
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

    if (G()->use_message_database() && !web_page_instant_view->was_loaded_from_database_) {
      LOG(INFO) << "Trying to load " << web_page_id << " instant view from database";
      G()->td_db()->get_sqlite_pmc()->get(
          get_web_page_instant_view_database_key(web_page_id),
          PromiseCreator::lambda([actor_id = actor_id(this), web_page_id](string value) {
            send_closure(actor_id, &WebPagesManager::on_load_web_page_instant_view_from_database, web_page_id,
                         std::move(value));
          }));
    } else {
      reload_web_page_instant_view(web_page_id);
    }
  }
}

void WebPagesManager::reload_web_page_instant_view(WebPageId web_page_id) {
  if (G()->close_flag()) {
    return update_web_page_instant_view_load_requests(web_page_id, true, Global::request_aborted_error());
  }

  LOG(INFO) << "Reload " << web_page_id << " instant view";
  const WebPage *web_page = get_web_page(web_page_id);
  CHECK(web_page != nullptr && !web_page->instant_view_.is_empty_);

  auto promise = PromiseCreator::lambda([actor_id = actor_id(this), web_page_id](Result<WebPageId> result) {
    send_closure(actor_id, &WebPagesManager::update_web_page_instant_view_load_requests, web_page_id, true,
                 std::move(result));
  });
  td_->create_handler<GetWebPageQuery>(std::move(promise))
      ->send(web_page_id, web_page->url_, web_page->instant_view_.is_full_ ? web_page->instant_view_.hash_ : 0);
}

void WebPagesManager::on_load_web_page_instant_view_from_database(WebPageId web_page_id, string value) {
  if (G()->close_flag()) {
    return;
  }
  CHECK(G()->use_message_database());
  LOG(INFO) << "Successfully loaded " << web_page_id << " instant view of size " << value.size() << " from database";
  //  G()->td_db()->get_sqlite_pmc()->erase(get_web_page_instant_view_database_key(web_page_id), Auto());
  //  value.clear();

  WebPage *web_page = web_pages_.get_pointer(web_page_id);
  if (web_page == nullptr || web_page->instant_view_.is_empty_) {
    // possible if web page loses preview/instant view
    LOG(WARNING) << "There is no instant view in " << web_page_id;
    if (!value.empty()) {
      G()->td_db()->get_sqlite_pmc()->erase(get_web_page_instant_view_database_key(web_page_id), Auto());
    }
    update_web_page_instant_view_load_requests(web_page_id, true, web_page_id);
    return;
  }
  auto &web_page_instant_view = web_page->instant_view_;
  if (web_page_instant_view.was_loaded_from_database_) {
    return;
  }

  WebPageInstantView instant_view;
  if (!value.empty()) {
    auto status = log_event_parse(instant_view, value);
    if (status.is_error()) {
      instant_view = WebPageInstantView();

      LOG(ERROR) << "Erase instant view in " << web_page_id << " from database because of " << status.message();
      G()->td_db()->get_sqlite_pmc()->erase(get_web_page_instant_view_database_key(web_page_id), Auto());
    }
  }
  instant_view.was_loaded_from_database_ = true;

  auto old_file_ids = get_web_page_file_ids(web_page);

  update_web_page_instant_view(web_page_id, web_page_instant_view, std::move(instant_view));

  auto new_file_ids = get_web_page_file_ids(web_page);
  if (old_file_ids != new_file_ids) {
    td_->file_manager_->change_files_source(get_web_page_file_source_id(web_page), old_file_ids, new_file_ids);
  }

  update_web_page_instant_view_load_requests(web_page_id, false, web_page_id);
}

void WebPagesManager::update_web_page_instant_view_load_requests(WebPageId web_page_id, bool force_update,
                                                                 Result<WebPageId> r_web_page_id) {
  G()->ignore_result_if_closing(r_web_page_id);
  LOG(INFO) << "Update load requests for " << web_page_id;
  auto it = load_web_page_instant_view_queries_.find(web_page_id);
  if (it == load_web_page_instant_view_queries_.end()) {
    return;
  }
  vector<Promise<WebPageId>> promises[2];
  promises[0] = std::move(it->second.partial);
  promises[1] = std::move(it->second.full);
  reset_to_empty(it->second.partial);
  reset_to_empty(it->second.full);
  load_web_page_instant_view_queries_.erase(it);

  if (r_web_page_id.is_error()) {
    LOG(INFO) << "Receive error " << r_web_page_id.error() << " for load " << web_page_id;
    combine(promises[0], std::move(promises[1]));
    fail_promises(promises[0], r_web_page_id.move_as_error());
    return;
  }

  auto new_web_page_id = r_web_page_id.move_as_ok();
  LOG(INFO) << "Successfully loaded web page " << web_page_id << " as " << new_web_page_id;
  const WebPageInstantView *web_page_instant_view = get_web_page_instant_view(new_web_page_id);
  if (web_page_instant_view == nullptr) {
    combine(promises[0], std::move(promises[1]));
    for (auto &promise : promises[0]) {
      promise.set_value(WebPageId());
    }
    return;
  }
  CHECK(new_web_page_id.is_valid());
  if (web_page_instant_view->is_loaded_) {
    if (web_page_instant_view->is_full_) {
      combine(promises[0], std::move(promises[1]));
    }

    for (auto &promise : promises[0]) {
      promise.set_value(WebPageId(new_web_page_id));
    }
    reset_to_empty(promises[0]);
  }
  if (!promises[0].empty() || !promises[1].empty()) {
    if (force_update) {
      // protection from cycles
      LOG(ERROR) << "Expected to receive " << web_page_id << '/' << new_web_page_id
                 << " from the server, but didn't receive it";
      combine(promises[0], std::move(promises[1]));
      for (auto &promise : promises[0]) {
        promise.set_value(WebPageId());
      }
      return;
    }
    auto &load_queries = load_web_page_instant_view_queries_[new_web_page_id];
    auto old_size = load_queries.partial.size() + load_queries.full.size();
    combine(load_queries.partial, std::move(promises[0]));
    combine(load_queries.full, std::move(promises[1]));
    if (old_size == 0) {
      reload_web_page_instant_view(new_web_page_id);
    }
  }
}

string WebPagesManager::get_web_page_url(WebPageId web_page_id) const {
  const WebPage *web_page = get_web_page(web_page_id);
  if (web_page != nullptr) {
    return web_page->url_;
  }
  return string();
}

WebPageId WebPagesManager::get_web_page_by_url(const string &url) const {
  if (url.empty()) {
    return WebPageId();
  }

  auto it = url_to_web_page_id_.find(url);
  if (it != url_to_web_page_id_.end()) {
    LOG(INFO) << "Return " << it->second << " for the URL \"" << url << '"';
    return it->second.first;
  }

  LOG(INFO) << "Can't find web page identifier for the URL \"" << url << '"';
  return WebPageId();
}

void WebPagesManager::get_web_page_by_url(const string &url, Promise<WebPageId> &&promise) {
  LOG(INFO) << "Trying to get web page identifier for the URL \"" << url << '"';
  if (url.empty()) {
    return promise.set_value(WebPageId());
  }

  auto it = url_to_web_page_id_.find(url);
  if (it != url_to_web_page_id_.end()) {
    return promise.set_value(WebPageId(it->second.first));
  }

  load_web_page_by_url(url, std::move(promise));
}

void WebPagesManager::load_web_page_by_url(string url, Promise<WebPageId> &&promise) {
  if (url.empty()) {
    return promise.set_value(WebPageId());
  }
  if (!G()->use_message_database()) {
    return reload_web_page_by_url(url, std::move(promise));
  }

  LOG(INFO) << "Load \"" << url << '"';
  auto key = get_web_page_url_database_key(url);
  G()->td_db()->get_sqlite_pmc()->get(key, PromiseCreator::lambda([actor_id = actor_id(this), url = std::move(url),
                                                                   promise = std::move(promise)](string value) mutable {
                                        send_closure(actor_id,
                                                     &WebPagesManager::on_load_web_page_id_by_url_from_database,
                                                     std::move(url), std::move(value), std::move(promise));
                                      }));
}

void WebPagesManager::on_load_web_page_id_by_url_from_database(string url, string value, Promise<WebPageId> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());

  LOG(INFO) << "Successfully loaded URL \"" << url << "\" of size " << value.size() << " from database";
  //  G()->td_db()->get_sqlite_pmc()->erase(get_web_page_url_database_key(web_page_id), Auto());
  //  value.clear();

  auto it = url_to_web_page_id_.find(url);
  if (it != url_to_web_page_id_.end()) {
    // URL web page has already been loaded
    return promise.set_value(WebPageId(it->second.first));
  }
  if (!value.empty()) {
    auto web_page_id = WebPageId(to_integer<int64>(value));
    if (web_page_id.is_valid()) {
      if (have_web_page(web_page_id)) {
        // URL web page has already been loaded
        on_get_web_page_by_url(url, web_page_id, true);
        promise.set_value(WebPageId(web_page_id));
        return;
      }

      load_web_page_from_database(web_page_id,
                                  PromiseCreator::lambda([actor_id = actor_id(this), web_page_id, url = std::move(url),
                                                          promise = std::move(promise)](Result<Unit> result) mutable {
                                    send_closure(actor_id, &WebPagesManager::on_load_web_page_by_url_from_database,
                                                 web_page_id, std::move(url), std::move(promise), std::move(result));
                                  }));
      return;
    } else {
      LOG(ERROR) << "Receive invalid " << web_page_id;
    }
  }

  reload_web_page_by_url(url, std::move(promise));
}

void WebPagesManager::on_load_web_page_by_url_from_database(WebPageId web_page_id, string url,
                                                            Promise<WebPageId> &&promise, Result<Unit> &&result) {
  if (result.is_error()) {
    CHECK(G()->close_flag());
    return promise.set_error(Global::request_aborted_error());
  }

  const WebPage *web_page = get_web_page(web_page_id);
  if (web_page == nullptr) {
    return reload_web_page_by_url(url, std::move(promise));
  }

  if (web_page->url_ != url) {
    on_get_web_page_by_url(url, web_page_id, true);
  }

  promise.set_value(WebPageId(web_page_id));
}

void WebPagesManager::reload_web_page_by_url(const string &url, Promise<WebPageId> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  td_->create_handler<GetWebPageQuery>(std::move(promise))->send(WebPageId(), url, 0);
}

bool WebPagesManager::have_web_page(WebPageId web_page_id) const {
  if (!web_page_id.is_valid()) {
    return false;
  }
  return get_web_page(web_page_id) != nullptr;
}

tl_object_ptr<td_api::webPage> WebPagesManager::get_web_page_object(WebPageId web_page_id, bool force_small_media,
                                                                    bool force_large_media, bool skip_confirmation,
                                                                    bool invert_media) const {
  if (!web_page_id.is_valid()) {
    return nullptr;
  }
  const WebPage *web_page = get_web_page(web_page_id);
  if (web_page == nullptr) {
    return nullptr;
  }
  int32 instant_view_version = [web_page] {
    if (web_page->instant_view_.is_empty_) {
      return 0;
    }
    if (web_page->instant_view_.is_v2_) {
      return 2;
    }
    return 1;
  }();

  FormattedText description;
  description.text = web_page->description_;
  description.entities = find_entities(web_page->description_, true, false);

  auto r_url = parse_url(web_page->display_url_);
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
        if (entity.type == MessageEntity::Type::Cashtag) {
          return PSTRING() << "https://twitter.com/search?q=" << url_encode(text) << "&src=cashtag_click";
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

  auto duration = get_web_page_media_duration(web_page);
  DialogId story_sender_dialog_id;
  StoryId story_id;
  if (web_page->story_full_ids_.size() == 1) {
    story_sender_dialog_id = web_page->story_full_ids_[0].get_dialog_id();
    story_id = web_page->story_full_ids_[0].get_story_id();
  }
  auto show_large_media = [&] {
    if (web_page->document_.type == Document::Type::Audio || web_page->document_.type == Document::Type::VoiceNote ||
        web_page->document_.type == Document::Type::General) {
      return true;
    }
    if (!web_page->has_large_media_) {
      return false;
    }
    if (force_large_media) {
      return true;
    }
    if (force_small_media) {
      return false;
    }
    if (instant_view_version != 0 || web_page->document_.file_id.is_valid() || web_page->photo_.is_empty()) {
      return true;
    }
    Slice type = web_page->type_;
    if (type != "app" && type != "article" && type != "profile" && type != "telegram_bot" &&
        type != "telegram_channel" && type != "telegram_chat" && type != "telegram_channel_boost" &&
        type != "telegram_livestream" && type != "telegram_megagroup" && type != "telegram_user" &&
        type != "telegram_voicechat") {
      return true;
    }
    if (web_page->site_name_.empty() && web_page->title_.empty() && web_page->description_.empty() &&
        web_page->author_.empty()) {
      return true;
    }
    if (web_page->site_name_ == "Twitter" || web_page->site_name_ == "Facebook" ||
        web_page->site_name_ == "Instagram") {
      return true;
    }
    return false;
  }();
  auto stickers = transform(web_page->sticker_ids_,
                            [&](FileId sticker_id) { return td_->stickers_manager_->get_sticker_object(sticker_id); });
  return td_api::make_object<td_api::webPage>(
      web_page->url_, web_page->display_url_, web_page->type_, web_page->site_name_, web_page->title_,
      get_formatted_text_object(description, true, duration == 0 ? std::numeric_limits<int32>::max() : duration),
      get_photo_object(td_->file_manager_.get(), web_page->photo_), web_page->embed_url_, web_page->embed_type_,
      web_page->embed_dimensions_.width, web_page->embed_dimensions_.height, web_page->duration_, web_page->author_,
      web_page->has_large_media_, show_large_media, skip_confirmation, invert_media,
      web_page->document_.type == Document::Type::Animation
          ? td_->animations_manager_->get_animation_object(web_page->document_.file_id)
          : nullptr,
      web_page->document_.type == Document::Type::Audio
          ? td_->audios_manager_->get_audio_object(web_page->document_.file_id)
          : nullptr,
      web_page->document_.type == Document::Type::General
          ? td_->documents_manager_->get_document_object(web_page->document_.file_id, PhotoFormat::Jpeg)
          : nullptr,
      web_page->document_.type == Document::Type::Sticker
          ? td_->stickers_manager_->get_sticker_object(web_page->document_.file_id)
          : nullptr,
      web_page->document_.type == Document::Type::Video
          ? td_->videos_manager_->get_video_object(web_page->document_.file_id)
          : nullptr,
      web_page->document_.type == Document::Type::VideoNote
          ? td_->video_notes_manager_->get_video_note_object(web_page->document_.file_id)
          : nullptr,
      web_page->document_.type == Document::Type::VoiceNote
          ? td_->voice_notes_manager_->get_voice_note_object(web_page->document_.file_id)
          : nullptr,
      td_->dialog_manager_->get_chat_id_object(story_sender_dialog_id, "webPage"), story_id.get(), std::move(stickers),
      instant_view_version);
}

tl_object_ptr<td_api::webPageInstantView> WebPagesManager::get_web_page_instant_view_object(
    WebPageId web_page_id) const {
  const WebPage *web_page = get_web_page(web_page_id);
  if (web_page == nullptr || web_page->instant_view_.is_empty_) {
    return nullptr;
  }
  return get_web_page_instant_view_object(web_page_id, &web_page->instant_view_, web_page->url_);
}

tl_object_ptr<td_api::webPageInstantView> WebPagesManager::get_web_page_instant_view_object(
    WebPageId web_page_id, const WebPageInstantView *web_page_instant_view, Slice web_page_url) const {
  if (web_page_instant_view == nullptr) {
    return nullptr;
  }
  if (!web_page_instant_view->is_loaded_) {
    LOG(ERROR) << "Trying to get not loaded web page instant view";
    return nullptr;
  }
  auto feedback_link = td_api::make_object<td_api::internalLinkTypeBotStart>(
      "previews", PSTRING() << "webpage" << web_page_id.get(), true);
  return td_api::make_object<td_api::webPageInstantView>(
      get_page_blocks_object(web_page_instant_view->page_blocks_, td_, web_page_instant_view->url_, web_page_url),
      web_page_instant_view->view_count_, web_page_instant_view->is_v2_ ? 2 : 1, web_page_instant_view->is_rtl_,
      web_page_instant_view->is_full_, std::move(feedback_link));
}

void WebPagesManager::on_web_page_changed(WebPageId web_page_id, bool have_web_page) {
  LOG(INFO) << "Updated " << web_page_id;
  {
    auto it = web_page_messages_.find(web_page_id);
    if (it != web_page_messages_.end()) {
      vector<MessageFullId> message_full_ids;
      for (const auto &message_full_id : it->second) {
        message_full_ids.push_back(message_full_id);
      }
      CHECK(!message_full_ids.empty());
      for (const auto &message_full_id : message_full_ids) {
        if (!have_web_page) {
          td_->messages_manager_->delete_pending_message_web_page(message_full_id);
        } else {
          td_->messages_manager_->on_external_update_message_content(message_full_id, "on_web_page_changed");
        }
      }

      // don't check that on_external_update_message_content doesn't load new messages
      if (!have_web_page && web_page_messages_.count(web_page_id) != 0) {
        vector<MessageFullId> new_message_full_ids;
        for (const auto &message_full_id : web_page_messages_[web_page_id]) {
          new_message_full_ids.push_back(message_full_id);
        }
        LOG(FATAL) << message_full_ids << ' ' << new_message_full_ids;
      }
    }
  }
  {
    auto it = web_page_quick_reply_messages_.find(web_page_id);
    if (it != web_page_quick_reply_messages_.end()) {
      vector<QuickReplyMessageFullId> message_full_ids;
      for (const auto &message_full_id : it->second) {
        message_full_ids.push_back(message_full_id);
      }
      CHECK(!message_full_ids.empty());
      for (const auto &message_full_id : message_full_ids) {
        if (!have_web_page) {
          td_->quick_reply_manager_->delete_pending_message_web_page(message_full_id);
        } else {
          td_->quick_reply_manager_->on_external_update_message_content(message_full_id, "on_web_page_changed");
        }
      }

      // don't check that on_external_update_message_content doesn't load new messages
      if (!have_web_page && web_page_quick_reply_messages_.count(web_page_id) != 0) {
        vector<QuickReplyMessageFullId> new_message_full_ids;
        for (const auto &message_full_id : web_page_quick_reply_messages_[web_page_id]) {
          new_message_full_ids.push_back(message_full_id);
        }
        LOG(FATAL) << message_full_ids << ' ' << new_message_full_ids;
      }
    }
  }
  {
    auto it = pending_get_web_pages_.find(web_page_id);
    if (it != pending_get_web_pages_.end()) {
      auto requests = std::move(it->second);
      pending_get_web_pages_.erase(it);
      for (auto &request : requests) {
        on_get_web_page_preview_success(std::move(request.first), have_web_page ? web_page_id : WebPageId(),
                                        std::move(request.second));
      }
    }
  }
  pending_web_pages_timeout_.cancel_timeout(web_page_id.get());
}

void WebPagesManager::on_story_changed(StoryFullId story_full_id) {
  auto story_it = story_web_pages_.find(story_full_id);
  if (story_it == story_web_pages_.end()) {
    return;
  }
  vector<WebPageId> web_page_ids;
  for (auto web_page_id : story_it->second) {
    web_page_ids.push_back(web_page_id);
  }
  for (auto web_page_id : web_page_ids) {
    on_web_page_changed(web_page_id, true);
  }
}

const WebPagesManager::WebPage *WebPagesManager::get_web_page(WebPageId web_page_id) const {
  return web_pages_.get_pointer(web_page_id);
}

const WebPagesManager::WebPageInstantView *WebPagesManager::get_web_page_instant_view(WebPageId web_page_id) const {
  const WebPage *web_page = get_web_page(web_page_id);
  if (web_page == nullptr || web_page->instant_view_.is_empty_) {
    return nullptr;
  }
  return &web_page->instant_view_;
}

void WebPagesManager::on_pending_web_page_timeout_callback(void *web_pages_manager_ptr, int64 web_page_id_int) {
  if (G()->close_flag()) {
    return;
  }

  auto web_pages_manager = static_cast<WebPagesManager *>(web_pages_manager_ptr);
  send_closure_later(web_pages_manager->actor_id(web_pages_manager), &WebPagesManager::on_pending_web_page_timeout,
                     WebPageId(web_page_id_int));
}

void WebPagesManager::on_pending_web_page_timeout(WebPageId web_page_id) {
  if (G()->close_flag() || have_web_page(web_page_id)) {
    return;
  }

  LOG(INFO) << "Process timeout for " << web_page_id;
  int32 count = 0;
  {
    auto it = web_page_messages_.find(web_page_id);
    if (it != web_page_messages_.end()) {
      vector<MessageFullId> message_full_ids;
      for (const auto &message_full_id : it->second) {
        if (message_full_id.get_dialog_id().get_type() != DialogType::SecretChat) {
          message_full_ids.push_back(message_full_id);
        }
        count++;
      }
      if (!message_full_ids.empty()) {
        send_closure_later(G()->messages_manager(), &MessagesManager::get_messages_from_server,
                           std::move(message_full_ids), Promise<Unit>(), "on_pending_web_page_timeout", nullptr);
      }
    }
  }
  {
    auto it = web_page_quick_reply_messages_.find(web_page_id);
    if (it != web_page_quick_reply_messages_.end()) {
      for (const auto &message_full_id : it->second) {
        send_closure_later(G()->quick_reply_manager(), &QuickReplyManager::reload_quick_reply_message,
                           message_full_id.get_quick_reply_shortcut_id(), message_full_id.get_message_id(),
                           Promise<Unit>());
        count++;
      }
    }
  }
  {
    auto it = pending_get_web_pages_.find(web_page_id);
    if (it != pending_get_web_pages_.end()) {
      auto requests = std::move(it->second);
      pending_get_web_pages_.erase(it);
      for (auto &request : requests) {
        request.second.set_error(Status::Error(500, "Request timeout exceeded"));
        count++;
      }
    }
  }
  if (count == 0) {
    LOG(INFO) << "Have no messages and requests waiting for " << web_page_id;
  }
}

void WebPagesManager::on_get_web_page_instant_view(WebPage *web_page, tl_object_ptr<telegram_api::page> &&page,
                                                   int32 hash, DialogId owner_dialog_id) {
  CHECK(page != nullptr);
  FlatHashMap<int64, unique_ptr<Photo>> photos;
  for (auto &photo_ptr : page->photos_) {
    Photo photo = get_photo(td_, std::move(photo_ptr), owner_dialog_id);
    if (photo.is_empty() || photo.id.get() == 0) {
      LOG(ERROR) << "Receive empty photo in web page instant view for " << web_page->url_;
    } else {
      auto photo_id = photo.id.get();
      photos.emplace(photo_id, make_unique<Photo>(std::move(photo)));
    }
  }
  if (!web_page->photo_.is_empty() && web_page->photo_.id.get() != 0) {
    photos.emplace(web_page->photo_.id.get(), make_unique<Photo>(web_page->photo_));
  }

  FlatHashMap<int64, FileId> animations;
  FlatHashMap<int64, FileId> audios;
  FlatHashMap<int64, FileId> documents;
  FlatHashMap<int64, FileId> videos;
  FlatHashMap<int64, FileId> voice_notes;
  FlatHashMap<int64, FileId> others;
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
      if (!parsed_document.empty() && document_id != 0) {
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
      auto document_id = file_view.remote_location().get_id();
      if (document_id != 0) {
        get_map(document.type)->emplace(document_id, document.file_id);
      } else {
        LOG(ERROR) << document.type << " has zero identifier";
      }
    } else {
      LOG(ERROR) << document.type << " has no remote location";
    }
  };
  if (!web_page->document_.empty()) {
    add_document(web_page->document_);
  }
  for (const auto &document : web_page->documents_) {
    add_document(document);
  }
  for (auto sticker_id : web_page->sticker_ids_) {
    add_document({Document::Type::Sticker, sticker_id});
  }

  LOG(INFO) << "Receive a web page instant view with " << page->blocks_.size() << " blocks, " << animations.size()
            << " animations, " << audios.size() << " audios, " << documents.size() << " documents, " << photos.size()
            << " photos, " << videos.size() << " videos and " << voice_notes.size() << " voice notes";
  web_page->instant_view_.page_blocks_ =
      get_web_page_blocks(td_, std::move(page->blocks_), animations, audios, documents, photos, videos, voice_notes);
  web_page->instant_view_.view_count_ = page->views_;
  web_page->instant_view_.is_v2_ = page->v2_;
  web_page->instant_view_.is_rtl_ = page->rtl_;
  web_page->instant_view_.hash_ = hash;
  web_page->instant_view_.url_ = std::move(page->url_);
  web_page->instant_view_.is_empty_ = false;
  web_page->instant_view_.is_full_ = !page->part_;
  web_page->instant_view_.is_loaded_ = true;

  LOG(DEBUG) << "Receive web page instant view: "
             << to_string(get_web_page_instant_view_object(WebPageId(), &web_page->instant_view_, web_page->url_));
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
  if (!G()->use_message_database()) {
    return;
  }

  CHECK(web_page != nullptr);
  if (!from_binlog) {
    WebPageLogEvent log_event(web_page_id, web_page);
    auto storer = get_log_event_storer(log_event);
    if (web_page->log_event_id_ == 0) {
      web_page->log_event_id_ = binlog_add(G()->td_db()->get_binlog(), LogEvent::HandlerType::WebPages, storer);
    } else {
      binlog_rewrite(G()->td_db()->get_binlog(), web_page->log_event_id_, LogEvent::HandlerType::WebPages, storer);
    }
  }

  LOG(INFO) << "Save " << web_page_id << " to database";
  G()->td_db()->get_sqlite_pmc()->set(
      get_web_page_database_key(web_page_id), log_event_store(*web_page).as_slice().str(),
      PromiseCreator::lambda([actor_id = actor_id(this), web_page_id](Result<> result) {
        send_closure(actor_id, &WebPagesManager::on_save_web_page_to_database, web_page_id, result.is_ok());
      }));
}

string WebPagesManager::get_web_page_url_database_key(const string &url) {
  return "wpurl" + url;
}

void WebPagesManager::on_binlog_web_page_event(BinlogEvent &&event) {
  if (!G()->use_message_database()) {
    binlog_erase(G()->td_db()->get_binlog(), event.id_);
    return;
  }

  WebPageLogEvent log_event;
  log_event_parse(log_event, event.get_data()).ensure();

  auto web_page_id = log_event.web_page_id;
  if (!web_page_id.is_valid()) {
    binlog_erase(G()->td_db()->get_binlog(), event.id_);
    return;
  }
  LOG(INFO) << "Add " << web_page_id << " from binlog";
  auto web_page = std::move(log_event.web_page_out);
  CHECK(web_page != nullptr);

  web_page->log_event_id_ = event.id_;

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
    save_web_page(web_page, web_page_id, web_page->log_event_id_ != 0);
  } else {
    LOG(INFO) << "Successfully saved " << web_page_id << " to database";
    if (web_page->log_event_id_ != 0) {
      LOG(INFO) << "Erase " << web_page_id << " from binlog";
      binlog_erase(G()->td_db()->get_binlog(), web_page->log_event_id_);
      web_page->log_event_id_ = 0;
    }
  }
}

void WebPagesManager::load_web_page_from_database(WebPageId web_page_id, Promise<Unit> promise) {
  if (!G()->use_message_database() || loaded_from_database_web_pages_.count(web_page_id) || !web_page_id.is_valid()) {
    promise.set_value(Unit());
    return;
  }

  LOG(INFO) << "Load " << web_page_id << " from database";
  auto &load_web_page_queries = load_web_page_from_database_queries_[web_page_id];
  load_web_page_queries.push_back(std::move(promise));
  if (load_web_page_queries.size() == 1u) {
    G()->td_db()->get_sqlite_pmc()->get(get_web_page_database_key(web_page_id),
                                        PromiseCreator::lambda([actor_id = actor_id(this), web_page_id](string value) {
                                          send_closure(actor_id, &WebPagesManager::on_load_web_page_from_database,
                                                       web_page_id, std::move(value));
                                        }));
  }
}

void WebPagesManager::on_load_web_page_from_database(WebPageId web_page_id, string value) {
  if (G()->close_flag()) {
    return;
  }
  CHECK(web_page_id.is_valid());
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
  //  value.clear();

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

  set_promises(promises);
}

bool WebPagesManager::have_web_page_force(WebPageId web_page_id) {
  return get_web_page_force(web_page_id) != nullptr;
}

const WebPagesManager::WebPage *WebPagesManager::get_web_page_force(WebPageId web_page_id) {
  const WebPage *web_page = get_web_page(web_page_id);
  if (web_page != nullptr) {
    return web_page;
  }
  if (!G()->use_message_database()) {
    return nullptr;
  }
  if (!web_page_id.is_valid() || loaded_from_database_web_pages_.count(web_page_id)) {
    return nullptr;
  }

  LOG(INFO) << "Trying to load " << web_page_id << " from database";
  on_load_web_page_from_database(web_page_id,
                                 G()->td_db()->get_sqlite_sync_pmc()->get(get_web_page_database_key(web_page_id)));
  return get_web_page(web_page_id);
}

FileSourceId WebPagesManager::get_web_page_file_source_id(WebPage *web_page) {
  if (!web_page->file_source_id_.is_valid()) {
    web_page->file_source_id_ = td_->file_reference_manager_->create_web_page_file_source(web_page->url_);
    VLOG(file_references) << "Create " << web_page->file_source_id_ << " for URL " << web_page->url_;
  } else {
    VLOG(file_references) << "Return " << web_page->file_source_id_ << " for URL " << web_page->url_;
  }
  return web_page->file_source_id_;
}

FileSourceId WebPagesManager::get_url_file_source_id(const string &url) {
  if (url.empty()) {
    return FileSourceId();
  }

  auto web_page_id = get_web_page_by_url(url);
  if (web_page_id.is_valid()) {
    const WebPage *web_page = get_web_page(web_page_id);
    if (web_page != nullptr) {
      if (!web_page->file_source_id_.is_valid()) {
        web_pages_[web_page_id]->file_source_id_ =
            td_->file_reference_manager_->create_web_page_file_source(web_page->url_);
        VLOG(file_references) << "Create " << web_page->file_source_id_ << " for " << web_page_id << " with URL "
                              << url;
      } else {
        VLOG(file_references) << "Return " << web_page->file_source_id_ << " for " << web_page_id << " with URL "
                              << url;
      }
      return web_page->file_source_id_;
    }
  }
  auto &source_id = url_to_file_source_id_[url];
  if (!source_id.is_valid()) {
    source_id = td_->file_reference_manager_->create_web_page_file_source(url);
    VLOG(file_references) << "Create " << source_id << " for URL " << url;
  } else {
    VLOG(file_references) << "Return " << source_id << " for URL " << url;
  }
  return source_id;
}

string WebPagesManager::get_web_page_search_text(WebPageId web_page_id) const {
  const WebPage *web_page = get_web_page(web_page_id);
  if (web_page == nullptr) {
    return "";
  }
  return PSTRING() << web_page->title_ + ' ' + web_page->description_;
}

int32 WebPagesManager::get_web_page_media_duration(WebPageId web_page_id) const {
  const WebPage *web_page = get_web_page(web_page_id);
  if (web_page == nullptr) {
    return -1;
  }
  return get_web_page_media_duration(web_page);
}

int32 WebPagesManager::get_web_page_media_duration(const WebPage *web_page) const {
  if (web_page->document_.type == Document::Type::Audio || web_page->document_.type == Document::Type::Video ||
      web_page->document_.type == Document::Type::VideoNote || web_page->document_.type == Document::Type::VoiceNote ||
      web_page->embed_type_ == "iframe") {
    return web_page->duration_;
  }
  if (!web_page->story_full_ids_.empty()) {
    auto story_duration = td_->story_manager_->get_story_duration(web_page->story_full_ids_[0]);
    return story_duration >= 0 ? story_duration : web_page->duration_;
  }

  return -1;
}

StoryFullId WebPagesManager::get_web_page_story_full_id(WebPageId web_page_id) const {
  const WebPage *web_page = get_web_page(web_page_id);
  if (web_page == nullptr || web_page->story_full_ids_.empty()) {
    return StoryFullId();
  }
  return web_page->story_full_ids_[0];
}

vector<UserId> WebPagesManager::get_web_page_user_ids(WebPageId web_page_id) const {
  const WebPage *web_page = get_web_page(web_page_id);
  vector<UserId> user_ids;
  if (web_page != nullptr && !web_page->story_full_ids_.empty()) {
    for (auto story_full_id : web_page->story_full_ids_) {
      auto dialog_id = story_full_id.get_dialog_id();
      if (dialog_id.get_type() == DialogType::User) {
        user_ids.push_back(dialog_id.get_user_id());
      }
    }
  }
  return user_ids;
}

vector<ChannelId> WebPagesManager::get_web_page_channel_ids(WebPageId web_page_id) const {
  const WebPage *web_page = get_web_page(web_page_id);
  vector<ChannelId> channel_ids;
  if (web_page != nullptr && !web_page->story_full_ids_.empty()) {
    for (auto story_full_id : web_page->story_full_ids_) {
      auto dialog_id = story_full_id.get_dialog_id();
      if (dialog_id.get_type() == DialogType::Channel) {
        channel_ids.push_back(dialog_id.get_channel_id());
      }
    }
  }
  return channel_ids;
}

vector<FileId> WebPagesManager::get_web_page_file_ids(const WebPage *web_page) const {
  if (web_page == nullptr) {
    return vector<FileId>();
  }

  vector<FileId> result = photo_get_file_ids(web_page->photo_);
  if (!web_page->document_.empty()) {
    web_page->document_.append_file_ids(td_, result);
  }
  for (auto &document : web_page->documents_) {
    document.append_file_ids(td_, result);
  }
  append(result, web_page->sticker_ids_);
  if (!web_page->instant_view_.is_empty_) {
    for (auto &page_block : web_page->instant_view_.page_blocks_) {
      page_block->append_file_ids(td_, result);
    }
  }
  return result;
}

}  // namespace td
