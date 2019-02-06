//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/WebPagesManager.h"

#include "td/telegram/secret_api.h"
#include "td/telegram/telegram_api.hpp"

#include "td/telegram/AnimationsManager.h"
#include "td/telegram/AnimationsManager.hpp"
#include "td/telegram/AudiosManager.h"
#include "td/telegram/AudiosManager.hpp"
#include "td/telegram/ChannelId.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/DocumentsManager.h"
#include "td/telegram/DocumentsManager.hpp"
#include "td/telegram/FileReferenceManager.h"
#include "td/telegram/files/FileId.h"
#include "td/telegram/files/FileManager.h"
#include "td/telegram/files/FileManager.hpp"
#include "td/telegram/files/FileSourceId.h"
#include "td/telegram/Global.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/Photo.h"
#include "td/telegram/Photo.hpp"
#include "td/telegram/StickersManager.h"
#include "td/telegram/StickersManager.hpp"
#include "td/telegram/Td.h"
#include "td/telegram/TdDb.h"
#include "td/telegram/Version.h"
#include "td/telegram/VideoNotesManager.h"
#include "td/telegram/VideoNotesManager.hpp"
#include "td/telegram/VideosManager.h"
#include "td/telegram/VideosManager.hpp"
#include "td/telegram/VoiceNotesManager.h"
#include "td/telegram/VoiceNotesManager.hpp"

#include "td/actor/PromiseFuture.h"

#include "td/db/binlog/BinlogEvent.h"
#include "td/db/binlog/BinlogHelper.h"
#include "td/db/SqliteKeyValueAsync.h"

#include "td/utils/buffer.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Slice.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/tl_helpers.h"

#include <type_traits>

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

    send_query(G()->net_query_creator().create(
        create_storer(telegram_api::messages_getWebPagePreview(flags, text, std::move(entities)))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_getWebPagePreview>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetWebPagePreviewQuery " << to_string(ptr);
    td->web_pages_manager_->on_get_web_page_preview_success(request_id_, url_, std::move(ptr), std::move(promise_));
  }

  void on_error(uint64 id, Status status) override {
    td->web_pages_manager_->on_get_web_page_preview_fail(request_id_, url_, std::move(status), std::move(promise_));
  }
};

class GetWebPageQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  string url_;

 public:
  explicit GetWebPageQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(const string &url, int32 hash) {
    url_ = url;
    send_query(G()->net_query_creator().create(create_storer(telegram_api::messages_getWebPage(url, hash))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_getWebPage>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetWebPageQuery " << to_string(ptr);
    if (ptr->get_id() != telegram_api::webPageNotModified::ID) {
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
  vector<unique_ptr<PageBlock>> page_blocks;
  int32 hash = 0;
  bool is_rtl = false;
  bool is_empty = true;
  bool is_full = false;
  bool is_loaded = false;
  bool was_loaded_from_database = false;

  template <class T>
  void store(T &storer) const {
    using ::td::store;
    BEGIN_STORE_FLAGS();
    STORE_FLAG(is_full);
    STORE_FLAG(is_loaded);
    STORE_FLAG(is_rtl);
    END_STORE_FLAGS();

    store(page_blocks, storer);
    store(hash, storer);
    CHECK(!is_empty);
  }

  template <class T>
  void parse(T &parser) {
    using ::td::parse;
    BEGIN_PARSE_FLAGS();
    PARSE_FLAG(is_full);
    PARSE_FLAG(is_loaded);
    PARSE_FLAG(is_rtl);
    END_PARSE_FLAGS();

    parse(page_blocks, parser);
    parse(hash, parser);
    is_empty = false;
  }

  friend StringBuilder &operator<<(StringBuilder &string_builder,
                                   const WebPagesManager::WebPageInstantView &instant_view) {
    return string_builder << "InstantView(size = " << instant_view.page_blocks.size()
                          << ", hash = " << instant_view.hash << ", is_empty = " << instant_view.is_empty
                          << ", is_full = " << instant_view.is_full << ", is_loaded = " << instant_view.is_loaded
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
  DocumentsManager::DocumentType document_type = DocumentsManager::DocumentType::Unknown;
  FileId document_file_id;
  WebPageInstantView instant_view;

  FileSourceId file_source_id;

  mutable uint64 logevent_id = 0;

  template <class T>
  void store(T &storer) const {
    using ::td::store;
    bool has_type = !type.empty();
    bool has_site_name = !site_name.empty();
    bool has_title = !title.empty();
    bool has_description = !description.empty();
    bool has_photo = photo.id != -2;
    bool has_embed = !embed_url.empty();
    bool has_embed_dimensions = has_embed && embed_dimensions != Dimensions();
    bool has_duration = duration > 0;
    bool has_author = !author.empty();
    bool has_document = document_type != DocumentsManager::DocumentType::Unknown;
    bool has_instant_view = !instant_view.is_empty;
    bool has_no_hash = true;
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
      Td *td = storer.context()->td().get_actor_unsafe();
      CHECK(td != nullptr);

      store(document_type, storer);
      switch (document_type) {
        case DocumentsManager::DocumentType::Animation:
          td->animations_manager_->store_animation(document_file_id, storer);
          break;
        case DocumentsManager::DocumentType::Audio:
          td->audios_manager_->store_audio(document_file_id, storer);
          break;
        case DocumentsManager::DocumentType::General:
          td->documents_manager_->store_document(document_file_id, storer);
          break;
        case DocumentsManager::DocumentType::Sticker:
          td->stickers_manager_->store_sticker(document_file_id, false, storer);
          break;
        case DocumentsManager::DocumentType::Video:
          td->videos_manager_->store_video(document_file_id, storer);
          break;
        case DocumentsManager::DocumentType::VideoNote:
          td->video_notes_manager_->store_video_note(document_file_id, storer);
          break;
        case DocumentsManager::DocumentType::VoiceNote:
          td->voice_notes_manager_->store_voice_note(document_file_id, storer);
          break;
        case DocumentsManager::DocumentType::Unknown:
        default:
          UNREACHABLE();
      }
    }
  }

  template <class T>
  void parse(T &parser) {
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
    bool has_no_hash;
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
    } else {
      photo.id = -2;
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
      Td *td = parser.context()->td().get_actor_unsafe();
      CHECK(td != nullptr);

      parse(document_type, parser);
      switch (document_type) {
        case DocumentsManager::DocumentType::Animation:
          document_file_id = td->animations_manager_->parse_animation(parser);
          break;
        case DocumentsManager::DocumentType::Audio:
          document_file_id = td->audios_manager_->parse_audio(parser);
          break;
        case DocumentsManager::DocumentType::General:
          document_file_id = td->documents_manager_->parse_document(parser);
          break;
        case DocumentsManager::DocumentType::Sticker:
          document_file_id = td->stickers_manager_->parse_sticker(false, parser);
          break;
        case DocumentsManager::DocumentType::Video:
          document_file_id = td->videos_manager_->parse_video(parser);
          break;
        case DocumentsManager::DocumentType::VideoNote:
          document_file_id = td->video_notes_manager_->parse_video_note(parser);
          break;
        case DocumentsManager::DocumentType::VoiceNote:
          document_file_id = td->voice_notes_manager_->parse_voice_note(parser);
          break;
        case DocumentsManager::DocumentType::Unknown:
        default:
          UNREACHABLE();
      }
      if (!document_file_id.is_valid()) {
        LOG(ERROR) << "Parse invalid document_file_id";
        document_type = DocumentsManager::DocumentType::Unknown;
      }
    }

    if (has_instant_view) {
      instant_view.is_empty = false;
    }
  }
};

class WebPagesManager::RichText {
 public:
  enum class Type : int32 {
    Plain,
    Bold,
    Italic,
    Underline,
    Strikethrough,
    Fixed,
    Url,
    EmailAddress,
    Concatenation,
    Subscript,
    Superscript,
    Marked,
    PhoneNumber,
    InlineImage
  };
  Type type = Type::Plain;
  string content;
  vector<RichText> texts;
  FileId document_file_id;
  WebPageId web_page_id;

  template <class T>
  void store(T &storer) const {
    using ::td::store;
    store(type, storer);
    store(content, storer);
    store(texts, storer);
    if (type == Type::InlineImage) {
      storer.context()->td().get_actor_unsafe()->documents_manager_->store_document(document_file_id, storer);
    }
    if (type == Type::Url) {
      store(web_page_id, storer);
    }
  }

  template <class T>
  void parse(T &parser) {
    using ::td::parse;
    parse(type, parser);
    parse(content, parser);
    parse(texts, parser);
    if (type == Type::InlineImage) {
      document_file_id = parser.context()->td().get_actor_unsafe()->documents_manager_->parse_document(parser);
      if (!document_file_id.is_valid()) {
        LOG(ERROR) << "Failed to load document from database";
        *this = RichText();
      }
    } else {
      document_file_id = FileId();
    }
    if (type == Type::Url && parser.version() >= static_cast<int32>(Version::SupportInstantView2_0)) {
      parse(web_page_id, parser);
    } else {
      web_page_id = WebPageId();
    }
  }
};

class WebPagesManager::PageBlockCaption {
 public:
  RichText text;
  RichText credit;

  template <class T>
  void store(T &storer) const {
    using ::td::store;
    store(text, storer);
    store(credit, storer);
  }

  template <class T>
  void parse(T &parser) {
    using ::td::parse;
    parse(text, parser);
    if (parser.version() >= static_cast<int32>(Version::SupportInstantView2_0)) {
      parse(credit, parser);
    } else {
      credit = RichText();
    }
  }
};

class WebPagesManager::PageBlock {
 public:
  enum class Type : int32 {
    Title,
    Subtitle,
    AuthorDate,
    Header,
    Subheader,
    Paragraph,
    Preformatted,
    Footer,
    Divider,
    Anchor,
    List,
    BlockQuote,
    PullQuote,
    Animation,
    Photo,
    Video,
    Cover,
    Embedded,
    EmbeddedPost,
    Collage,
    Slideshow,
    ChatLink,
    Audio,
    Kicker
  };

  virtual Type get_type() const = 0;

  virtual void append_file_ids(vector<FileId> &file_ids) const = 0;

  virtual tl_object_ptr<td_api::PageBlock> get_page_block_object() const = 0;

  PageBlock() = default;
  PageBlock(const PageBlock &) = delete;
  PageBlock &operator=(const PageBlock &) = delete;
  PageBlock(PageBlock &&) = delete;
  PageBlock &operator=(PageBlock &&) = delete;
  virtual ~PageBlock() = default;

  template <class T>
  void store(T &storer) const {
    using ::td::store;
    Type type = get_type();
    store(type, storer);
    call_impl(type, this, [&](const auto *object) { store(*object, storer); });
  }
  template <class T>
  static unique_ptr<PageBlock> parse(T &parser) {
    using ::td::parse;
    Type type;
    parse(type, parser);
    unique_ptr<PageBlock> res;
    call_impl(type, nullptr, [&](const auto *ptr) {
      using ObjT = std::decay_t<decltype(*ptr)>;
      auto object = make_unique<ObjT>();
      parse(*object, parser);
      res = std::move(object);
    });
    return res;
  }

 private:
  template <class F>
  static void call_impl(Type type, const PageBlock *ptr, F &&f);
};

template <class T>
void store(const unique_ptr<WebPagesManager::PageBlock> &block, T &storer) {
  block->store(storer);
}

template <class T>
void parse(unique_ptr<WebPagesManager::PageBlock> &block, T &parser) {
  block = WebPagesManager::PageBlock::parse(parser);
}

class WebPagesManager::PageBlockTitle : public PageBlock {
  RichText title;

 public:
  PageBlockTitle() = default;

  explicit PageBlockTitle(RichText &&title) : title(std::move(title)) {
  }

  Type get_type() const override {
    return Type::Title;
  }

  void append_file_ids(vector<FileId> &file_ids) const override {
    append_rich_text_file_ids(title, file_ids);
  }

  tl_object_ptr<td_api::PageBlock> get_page_block_object() const override {
    return make_tl_object<td_api::pageBlockTitle>(get_rich_text_object(title));
  }

  template <class T>
  void store(T &storer) const {
    using ::td::store;
    store(title, storer);
  }
  template <class T>
  void parse(T &parser) {
    using ::td::parse;
    parse(title, parser);
  }
};

class WebPagesManager::PageBlockSubtitle : public PageBlock {
  RichText subtitle;

 public:
  PageBlockSubtitle() = default;
  explicit PageBlockSubtitle(RichText &&subtitle) : subtitle(std::move(subtitle)) {
  }

  Type get_type() const override {
    return Type::Subtitle;
  }

  void append_file_ids(vector<FileId> &file_ids) const override {
    append_rich_text_file_ids(subtitle, file_ids);
  }

  tl_object_ptr<td_api::PageBlock> get_page_block_object() const override {
    return make_tl_object<td_api::pageBlockSubtitle>(get_rich_text_object(subtitle));
  }

  template <class T>
  void store(T &storer) const {
    using ::td::store;
    store(subtitle, storer);
  }
  template <class T>
  void parse(T &parser) {
    using ::td::parse;
    parse(subtitle, parser);
  }
};

class WebPagesManager::PageBlockAuthorDate : public PageBlock {
  RichText author;
  int32 date = 0;

 public:
  PageBlockAuthorDate() = default;
  PageBlockAuthorDate(RichText &&author, int32 date) : author(std::move(author)), date(max(date, 0)) {
  }

  Type get_type() const override {
    return Type::AuthorDate;
  }

  void append_file_ids(vector<FileId> &file_ids) const override {
    append_rich_text_file_ids(author, file_ids);
  }

  tl_object_ptr<td_api::PageBlock> get_page_block_object() const override {
    return make_tl_object<td_api::pageBlockAuthorDate>(get_rich_text_object(author), date);
  }

  template <class T>
  void store(T &storer) const {
    using ::td::store;
    store(author, storer);
    store(date, storer);
  }
  template <class T>
  void parse(T &parser) {
    using ::td::parse;
    parse(author, parser);
    parse(date, parser);
  }
};

class WebPagesManager::PageBlockHeader : public PageBlock {
  RichText header;

 public:
  PageBlockHeader() = default;
  explicit PageBlockHeader(RichText &&header) : header(std::move(header)) {
  }

  Type get_type() const override {
    return Type::Header;
  }

  void append_file_ids(vector<FileId> &file_ids) const override {
    append_rich_text_file_ids(header, file_ids);
  }

  tl_object_ptr<td_api::PageBlock> get_page_block_object() const override {
    return make_tl_object<td_api::pageBlockHeader>(get_rich_text_object(header));
  }

  template <class T>
  void store(T &storer) const {
    using ::td::store;
    store(header, storer);
  }
  template <class T>
  void parse(T &parser) {
    using ::td::parse;
    parse(header, parser);
  }
};

class WebPagesManager::PageBlockSubheader : public PageBlock {
  RichText subheader;

 public:
  PageBlockSubheader() = default;
  explicit PageBlockSubheader(RichText &&subheader) : subheader(std::move(subheader)) {
  }

  Type get_type() const override {
    return Type::Subheader;
  }

  void append_file_ids(vector<FileId> &file_ids) const override {
    append_rich_text_file_ids(subheader, file_ids);
  }

  tl_object_ptr<td_api::PageBlock> get_page_block_object() const override {
    return make_tl_object<td_api::pageBlockSubheader>(get_rich_text_object(subheader));
  }

  template <class T>
  void store(T &storer) const {
    using ::td::store;
    store(subheader, storer);
  }
  template <class T>
  void parse(T &parser) {
    using ::td::parse;
    parse(subheader, parser);
  }
};

class WebPagesManager::PageBlockKicker : public PageBlock {
  RichText kicker;

 public:
  PageBlockKicker() = default;
  explicit PageBlockKicker(RichText &&kicker) : kicker(std::move(kicker)) {
  }

  Type get_type() const override {
    return Type::Kicker;
  }

  void append_file_ids(vector<FileId> &file_ids) const override {
    append_rich_text_file_ids(kicker, file_ids);
  }

  tl_object_ptr<td_api::PageBlock> get_page_block_object() const override {
    return make_tl_object<td_api::pageBlockKicker>(get_rich_text_object(kicker));
  }

  template <class T>
  void store(T &storer) const {
    using ::td::store;
    store(kicker, storer);
  }
  template <class T>
  void parse(T &parser) {
    using ::td::parse;
    parse(kicker, parser);
  }
};

class WebPagesManager::PageBlockParagraph : public PageBlock {
  RichText text;

 public:
  PageBlockParagraph() = default;
  explicit PageBlockParagraph(RichText &&text) : text(std::move(text)) {
  }

  Type get_type() const override {
    return Type::Paragraph;
  }

  void append_file_ids(vector<FileId> &file_ids) const override {
    append_rich_text_file_ids(text, file_ids);
  }

  tl_object_ptr<td_api::PageBlock> get_page_block_object() const override {
    return make_tl_object<td_api::pageBlockParagraph>(get_rich_text_object(text));
  }

  template <class T>
  void store(T &storer) const {
    using ::td::store;
    store(text, storer);
  }
  template <class T>
  void parse(T &parser) {
    using ::td::parse;
    parse(text, parser);
  }
};

class WebPagesManager::PageBlockPreformatted : public PageBlock {
  RichText text;
  string language;

 public:
  PageBlockPreformatted() = default;
  PageBlockPreformatted(RichText &&text, string language) : text(std::move(text)), language(std::move(language)) {
  }

  Type get_type() const override {
    return Type::Preformatted;
  }

  void append_file_ids(vector<FileId> &file_ids) const override {
    append_rich_text_file_ids(text, file_ids);
  }

  tl_object_ptr<td_api::PageBlock> get_page_block_object() const override {
    return make_tl_object<td_api::pageBlockPreformatted>(get_rich_text_object(text), language);
  }

  template <class T>
  void store(T &storer) const {
    using ::td::store;
    store(text, storer);
    store(language, storer);
  }
  template <class T>
  void parse(T &parser) {
    using ::td::parse;
    parse(text, parser);
    parse(language, parser);
  }
};

class WebPagesManager::PageBlockFooter : public PageBlock {
  RichText footer;

 public:
  PageBlockFooter() = default;
  explicit PageBlockFooter(RichText &&footer) : footer(std::move(footer)) {
  }

  Type get_type() const override {
    return Type::Footer;
  }

  void append_file_ids(vector<FileId> &file_ids) const override {
    append_rich_text_file_ids(footer, file_ids);
  }

  tl_object_ptr<td_api::PageBlock> get_page_block_object() const override {
    return make_tl_object<td_api::pageBlockFooter>(get_rich_text_object(footer));
  }

  template <class T>
  void store(T &storer) const {
    using ::td::store;
    store(footer, storer);
  }
  template <class T>
  void parse(T &parser) {
    using ::td::parse;
    parse(footer, parser);
  }
};

class WebPagesManager::PageBlockDivider : public PageBlock {
 public:
  Type get_type() const override {
    return Type::Divider;
  }

  void append_file_ids(vector<FileId> &file_ids) const override {
  }

  tl_object_ptr<td_api::PageBlock> get_page_block_object() const override {
    return make_tl_object<td_api::pageBlockDivider>();
  }
  template <class T>
  void store(T &storer) const {
  }
  template <class T>
  void parse(T &parser) {
  }
};

class WebPagesManager::PageBlockAnchor : public PageBlock {
  string name;

 public:
  PageBlockAnchor() = default;
  explicit PageBlockAnchor(string name) : name(std::move(name)) {
  }

  Type get_type() const override {
    return Type::Anchor;
  }

  void append_file_ids(vector<FileId> &file_ids) const override {
  }

  tl_object_ptr<td_api::PageBlock> get_page_block_object() const override {
    return make_tl_object<td_api::pageBlockAnchor>(name);
  }
  template <class T>
  void store(T &storer) const {
    using ::td::store;
    store(name, storer);
  }
  template <class T>
  void parse(T &parser) {
    using ::td::parse;
    parse(name, parser);
  }
};

class WebPagesManager::PageBlockList : public PageBlock {
  vector<RichText> items;
  bool is_ordered = false;

 public:
  PageBlockList() = default;
  PageBlockList(vector<RichText> &&items, bool is_ordered) : items(std::move(items)), is_ordered(is_ordered) {
  }

  Type get_type() const override {
    return Type::List;
  }

  void append_file_ids(vector<FileId> &file_ids) const override {
    for (auto &item : items) {
      append_rich_text_file_ids(item, file_ids);
    }
  }

  tl_object_ptr<td_api::PageBlock> get_page_block_object() const override {
    return make_tl_object<td_api::pageBlockList>(get_rich_text_objects(items), is_ordered);
  }

  template <class T>
  void store(T &storer) const {
    using ::td::store;

    BEGIN_STORE_FLAGS();
    STORE_FLAG(is_ordered);
    END_STORE_FLAGS();

    store(items, storer);
  }
  template <class T>
  void parse(T &parser) {
    using ::td::parse;

    BEGIN_PARSE_FLAGS();
    PARSE_FLAG(is_ordered);
    END_PARSE_FLAGS();

    parse(items, parser);
  }
};

class WebPagesManager::PageBlockBlockQuote : public PageBlock {
  RichText text;
  RichText credit;

 public:
  PageBlockBlockQuote() = default;
  PageBlockBlockQuote(RichText &&text, RichText &&credit) : text(std::move(text)), credit(std::move(credit)) {
  }

  Type get_type() const override {
    return Type::BlockQuote;
  }

  void append_file_ids(vector<FileId> &file_ids) const override {
    append_rich_text_file_ids(text, file_ids);
    append_rich_text_file_ids(credit, file_ids);
  }

  tl_object_ptr<td_api::PageBlock> get_page_block_object() const override {
    return make_tl_object<td_api::pageBlockBlockQuote>(get_rich_text_object(text), get_rich_text_object(credit));
  }

  template <class T>
  void store(T &storer) const {
    using ::td::store;
    store(text, storer);
    store(credit, storer);
  }

  template <class T>
  void parse(T &parser) {
    using ::td::parse;
    parse(text, parser);
    parse(credit, parser);
  }
};

class WebPagesManager::PageBlockPullQuote : public PageBlock {
  RichText text;
  RichText credit;

 public:
  PageBlockPullQuote() = default;
  PageBlockPullQuote(RichText &&text, RichText &&credit) : text(std::move(text)), credit(std::move(credit)) {
  }

  Type get_type() const override {
    return Type::PullQuote;
  }

  void append_file_ids(vector<FileId> &file_ids) const override {
    append_rich_text_file_ids(text, file_ids);
    append_rich_text_file_ids(credit, file_ids);
  }

  tl_object_ptr<td_api::PageBlock> get_page_block_object() const override {
    return make_tl_object<td_api::pageBlockPullQuote>(get_rich_text_object(text), get_rich_text_object(credit));
  }

  template <class T>
  void store(T &storer) const {
    using ::td::store;
    store(text, storer);
    store(credit, storer);
  }

  template <class T>
  void parse(T &parser) {
    using ::td::parse;
    parse(text, parser);
    parse(credit, parser);
  }
};

class WebPagesManager::PageBlockAnimation : public PageBlock {
  FileId animation_file_id;
  PageBlockCaption caption;
  bool need_autoplay = false;

 public:
  PageBlockAnimation() = default;
  PageBlockAnimation(FileId animation_file_id, PageBlockCaption &&caption, bool need_autoplay)
      : animation_file_id(animation_file_id), caption(std::move(caption)), need_autoplay(need_autoplay) {
  }

  Type get_type() const override {
    return Type::Animation;
  }

  void append_file_ids(vector<FileId> &file_ids) const override {
    append_page_block_caption_file_ids(caption, file_ids);
    if (animation_file_id.is_valid()) {
      file_ids.push_back(animation_file_id);
      auto thumbnail_file_id =
          G()->td().get_actor_unsafe()->animations_manager_->get_animation_thumbnail_file_id(animation_file_id);
      if (thumbnail_file_id.is_valid()) {
        file_ids.push_back(thumbnail_file_id);
      }
    }
  }

  tl_object_ptr<td_api::PageBlock> get_page_block_object() const override {
    return make_tl_object<td_api::pageBlockAnimation>(
        G()->td().get_actor_unsafe()->animations_manager_->get_animation_object(animation_file_id,
                                                                                "get_page_block_object"),
        get_page_block_caption_object(caption), need_autoplay);
  }

  template <class T>
  void store(T &storer) const {
    using ::td::store;

    bool has_empty_animation = !animation_file_id.is_valid();
    BEGIN_STORE_FLAGS();
    STORE_FLAG(need_autoplay);
    STORE_FLAG(has_empty_animation);
    END_STORE_FLAGS();

    if (!has_empty_animation) {
      storer.context()->td().get_actor_unsafe()->animations_manager_->store_animation(animation_file_id, storer);
    }
    store(caption, storer);
  }

  template <class T>
  void parse(T &parser) {
    using ::td::parse;

    bool has_empty_animation;
    BEGIN_PARSE_FLAGS();
    PARSE_FLAG(need_autoplay);
    PARSE_FLAG(has_empty_animation);
    END_PARSE_FLAGS();

    if (parser.version() >= static_cast<int32>(Version::FixWebPageInstantViewDatabase)) {
      if (!has_empty_animation) {
        animation_file_id = parser.context()->td().get_actor_unsafe()->animations_manager_->parse_animation(parser);
      } else {
        animation_file_id = FileId();
      }
    } else {
      animation_file_id = FileId();
      parser.set_error("Wrong stored object");
    }
    parse(caption, parser);
  }
};

class WebPagesManager::PageBlockPhoto : public PageBlock {
  Photo photo;
  PageBlockCaption caption;
  string url;
  WebPageId web_page_id;

 public:
  PageBlockPhoto() = default;
  PageBlockPhoto(Photo photo, PageBlockCaption &&caption, string &&url, WebPageId web_page_id)
      : photo(std::move(photo)), caption(std::move(caption)), url(std::move(url)), web_page_id(web_page_id) {
  }

  Type get_type() const override {
    return Type::Photo;
  }

  void append_file_ids(vector<FileId> &file_ids) const override {
    append_page_block_caption_file_ids(caption, file_ids);
  }

  tl_object_ptr<td_api::PageBlock> get_page_block_object() const override {
    return make_tl_object<td_api::pageBlockPhoto>(
        get_photo_object(G()->td().get_actor_unsafe()->file_manager_.get(), &photo),
        get_page_block_caption_object(caption), url);
  }

  template <class T>
  void store(T &storer) const {
    using ::td::store;
    store(photo, storer);
    store(caption, storer);
    store(url, storer);
    store(web_page_id, storer);
  }

  template <class T>
  void parse(T &parser) {
    using ::td::parse;
    parse(photo, parser);
    parse(caption, parser);
    if (parser.version() >= static_cast<int32>(Version::SupportInstantView2_0)) {
      parse(url, parser);
      parse(web_page_id, parser);
    } else {
      url.clear();
      web_page_id = WebPageId();
    }
  }
};

class WebPagesManager::PageBlockVideo : public PageBlock {
  FileId video_file_id;
  PageBlockCaption caption;
  bool need_autoplay = false;
  bool is_looped = false;

 public:
  PageBlockVideo() = default;
  PageBlockVideo(FileId video_file_id, PageBlockCaption &&caption, bool need_autoplay, bool is_looped)
      : video_file_id(video_file_id), caption(std::move(caption)), need_autoplay(need_autoplay), is_looped(is_looped) {
  }

  Type get_type() const override {
    return Type::Video;
  }

  void append_file_ids(vector<FileId> &file_ids) const override {
    append_page_block_caption_file_ids(caption, file_ids);
    if (video_file_id.is_valid()) {
      file_ids.push_back(video_file_id);
      auto thumbnail_file_id =
          G()->td().get_actor_unsafe()->videos_manager_->get_video_thumbnail_file_id(video_file_id);
      if (thumbnail_file_id.is_valid()) {
        file_ids.push_back(thumbnail_file_id);
      }
    }
  }

  tl_object_ptr<td_api::PageBlock> get_page_block_object() const override {
    return make_tl_object<td_api::pageBlockVideo>(
        G()->td().get_actor_unsafe()->videos_manager_->get_video_object(video_file_id),
        get_page_block_caption_object(caption), need_autoplay, is_looped);
  }

  template <class T>
  void store(T &storer) const {
    using ::td::store;

    bool has_empty_video = !video_file_id.is_valid();
    BEGIN_STORE_FLAGS();
    STORE_FLAG(need_autoplay);
    STORE_FLAG(is_looped);
    STORE_FLAG(has_empty_video);
    END_STORE_FLAGS();

    if (!has_empty_video) {
      storer.context()->td().get_actor_unsafe()->videos_manager_->store_video(video_file_id, storer);
    }
    store(caption, storer);
  }

  template <class T>
  void parse(T &parser) {
    using ::td::parse;

    bool has_empty_video;
    BEGIN_PARSE_FLAGS();
    PARSE_FLAG(need_autoplay);
    PARSE_FLAG(is_looped);
    PARSE_FLAG(has_empty_video);
    END_PARSE_FLAGS();

    if (parser.version() >= static_cast<int32>(Version::FixWebPageInstantViewDatabase)) {
      if (!has_empty_video) {
        video_file_id = parser.context()->td().get_actor_unsafe()->videos_manager_->parse_video(parser);
      } else {
        video_file_id = FileId();
      }
    } else {
      video_file_id = FileId();
      parser.set_error("Wrong stored object");
    }
    parse(caption, parser);
  }
};

class WebPagesManager::PageBlockCover : public PageBlock {
  unique_ptr<PageBlock> cover;

 public:
  PageBlockCover() = default;
  explicit PageBlockCover(unique_ptr<PageBlock> &&cover) : cover(std::move(cover)) {
  }

  Type get_type() const override {
    return Type::Cover;
  }

  void append_file_ids(vector<FileId> &file_ids) const override {
    cover->append_file_ids(file_ids);
  }

  tl_object_ptr<td_api::PageBlock> get_page_block_object() const override {
    return make_tl_object<td_api::pageBlockCover>(cover->get_page_block_object());
  }

  template <class T>
  void store(T &storer) const {
    using ::td::store;
    store(cover, storer);
  }

  template <class T>
  void parse(T &parser) {
    using ::td::parse;
    parse(cover, parser);
  }
};

class WebPagesManager::PageBlockEmbedded : public PageBlock {
  string url;
  string html;
  Photo poster_photo;
  Dimensions dimensions;
  PageBlockCaption caption;
  bool is_full_width;
  bool allow_scrolling;

 public:
  PageBlockEmbedded() = default;
  PageBlockEmbedded(string url, string html, Photo poster_photo, Dimensions dimensions, PageBlockCaption &&caption,
                    bool is_full_width, bool allow_scrolling)
      : url(std::move(url))
      , html(std::move(html))
      , poster_photo(std::move(poster_photo))
      , dimensions(dimensions)
      , caption(std::move(caption))
      , is_full_width(is_full_width)
      , allow_scrolling(allow_scrolling) {
  }

  Type get_type() const override {
    return Type::Embedded;
  }

  void append_file_ids(vector<FileId> &file_ids) const override {
    append(file_ids, photo_get_file_ids(poster_photo));
    append_page_block_caption_file_ids(caption, file_ids);
  }

  tl_object_ptr<td_api::PageBlock> get_page_block_object() const override {
    return make_tl_object<td_api::pageBlockEmbedded>(
        url, html, get_photo_object(G()->td().get_actor_unsafe()->file_manager_.get(), &poster_photo), dimensions.width,
        dimensions.height, get_page_block_caption_object(caption), is_full_width, allow_scrolling);
  }

  template <class T>
  void store(T &storer) const {
    using ::td::store;
    BEGIN_STORE_FLAGS();
    STORE_FLAG(is_full_width);
    STORE_FLAG(allow_scrolling);
    END_STORE_FLAGS();

    store(url, storer);
    store(html, storer);
    store(poster_photo, storer);
    store(dimensions, storer);
    store(caption, storer);
  }

  template <class T>
  void parse(T &parser) {
    using ::td::parse;
    BEGIN_PARSE_FLAGS();
    PARSE_FLAG(is_full_width);
    PARSE_FLAG(allow_scrolling);
    END_PARSE_FLAGS();

    parse(url, parser);
    parse(html, parser);
    parse(poster_photo, parser);
    parse(dimensions, parser);
    parse(caption, parser);
  }
};

class WebPagesManager::PageBlockEmbeddedPost : public PageBlock {
  string url;
  string author;
  Photo author_photo;
  int32 date;
  vector<unique_ptr<PageBlock>> page_blocks;
  PageBlockCaption caption;

 public:
  PageBlockEmbeddedPost() = default;
  PageBlockEmbeddedPost(string url, string author, Photo author_photo, int32 date,
                        vector<unique_ptr<PageBlock>> &&page_blocks, PageBlockCaption &&caption)
      : url(std::move(url))
      , author(std::move(author))
      , author_photo(std::move(author_photo))
      , date(max(date, 0))
      , page_blocks(std::move(page_blocks))
      , caption(std::move(caption)) {
  }

  Type get_type() const override {
    return Type::EmbeddedPost;
  }

  void append_file_ids(vector<FileId> &file_ids) const override {
    append(file_ids, photo_get_file_ids(author_photo));
    for (auto &page_block : page_blocks) {
      page_block->append_file_ids(file_ids);
    }
    append_page_block_caption_file_ids(caption, file_ids);
  }

  tl_object_ptr<td_api::PageBlock> get_page_block_object() const override {
    return make_tl_object<td_api::pageBlockEmbeddedPost>(
        url, author, get_photo_object(G()->td().get_actor_unsafe()->file_manager_.get(), &author_photo), date,
        get_page_block_objects(page_blocks), get_page_block_caption_object(caption));
  }
  template <class T>
  void store(T &storer) const {
    using ::td::store;
    store(url, storer);
    store(author, storer);
    store(author_photo, storer);
    store(date, storer);
    store(page_blocks, storer);
    store(caption, storer);
  }

  template <class T>
  void parse(T &parser) {
    using ::td::parse;
    parse(url, parser);
    parse(author, parser);
    parse(author_photo, parser);
    parse(date, parser);
    parse(page_blocks, parser);
    parse(caption, parser);
  }
};

class WebPagesManager::PageBlockCollage : public PageBlock {
  vector<unique_ptr<PageBlock>> page_blocks;
  PageBlockCaption caption;

 public:
  PageBlockCollage() = default;
  PageBlockCollage(vector<unique_ptr<PageBlock>> &&page_blocks, PageBlockCaption &&caption)
      : page_blocks(std::move(page_blocks)), caption(std::move(caption)) {
  }

  Type get_type() const override {
    return Type::Collage;
  }

  void append_file_ids(vector<FileId> &file_ids) const override {
    for (auto &page_block : page_blocks) {
      page_block->append_file_ids(file_ids);
    }
    append_page_block_caption_file_ids(caption, file_ids);
  }

  tl_object_ptr<td_api::PageBlock> get_page_block_object() const override {
    return make_tl_object<td_api::pageBlockCollage>(get_page_block_objects(page_blocks),
                                                    get_page_block_caption_object(caption));
  }

  template <class T>
  void store(T &storer) const {
    using ::td::store;
    store(page_blocks, storer);
    store(caption, storer);
  }

  template <class T>
  void parse(T &parser) {
    using ::td::parse;
    parse(page_blocks, parser);
    parse(caption, parser);
  }
};

class WebPagesManager::PageBlockSlideshow : public PageBlock {
  vector<unique_ptr<PageBlock>> page_blocks;
  PageBlockCaption caption;

 public:
  PageBlockSlideshow() = default;
  PageBlockSlideshow(vector<unique_ptr<PageBlock>> &&page_blocks, PageBlockCaption &&caption)
      : page_blocks(std::move(page_blocks)), caption(std::move(caption)) {
  }

  Type get_type() const override {
    return Type::Slideshow;
  }

  void append_file_ids(vector<FileId> &file_ids) const override {
    for (auto &page_block : page_blocks) {
      page_block->append_file_ids(file_ids);
    }
    append_page_block_caption_file_ids(caption, file_ids);
  }

  tl_object_ptr<td_api::PageBlock> get_page_block_object() const override {
    return make_tl_object<td_api::pageBlockSlideshow>(get_page_block_objects(page_blocks),
                                                      get_page_block_caption_object(caption));
  }

  template <class T>
  void store(T &storer) const {
    using ::td::store;
    store(page_blocks, storer);
    store(caption, storer);
  }

  template <class T>
  void parse(T &parser) {
    using ::td::parse;
    parse(page_blocks, parser);
    parse(caption, parser);
  }
};

class WebPagesManager::PageBlockChatLink : public PageBlock {
  string title;
  DialogPhoto photo;
  string username;

 public:
  PageBlockChatLink() = default;
  PageBlockChatLink(string title, DialogPhoto photo, string username)
      : title(std::move(title)), photo(std::move(photo)), username(std::move(username)) {
  }

  Type get_type() const override {
    return Type::ChatLink;
  }

  void append_file_ids(vector<FileId> &file_ids) const override {
    append(file_ids, dialog_photo_get_file_ids(photo));
  }

  tl_object_ptr<td_api::PageBlock> get_page_block_object() const override {
    return make_tl_object<td_api::pageBlockChatLink>(
        title, get_chat_photo_object(G()->td().get_actor_unsafe()->file_manager_.get(), &photo), username);
  }

  template <class T>
  void store(T &storer) const {
    using ::td::store;
    store(title, storer);
    store(photo, storer);
    store(username, storer);
  }

  template <class T>
  void parse(T &parser) {
    using ::td::parse;
    parse(title, parser);
    parse(photo, parser);
    parse(username, parser);
  }
};

class WebPagesManager::PageBlockAudio : public PageBlock {
  FileId audio_file_id;
  PageBlockCaption caption;

 public:
  PageBlockAudio() = default;
  PageBlockAudio(FileId audio_file_id, PageBlockCaption &&caption)
      : audio_file_id(audio_file_id), caption(std::move(caption)) {
  }

  Type get_type() const override {
    return Type::Audio;
  }

  void append_file_ids(vector<FileId> &file_ids) const override {
    if (audio_file_id.is_valid()) {
      file_ids.push_back(audio_file_id);
      auto thumbnail_file_id =
          G()->td().get_actor_unsafe()->audios_manager_->get_audio_thumbnail_file_id(audio_file_id);
      if (thumbnail_file_id.is_valid()) {
        file_ids.push_back(thumbnail_file_id);
      }
    }
    append_page_block_caption_file_ids(caption, file_ids);
  }

  tl_object_ptr<td_api::PageBlock> get_page_block_object() const override {
    return make_tl_object<td_api::pageBlockAudio>(
        G()->td().get_actor_unsafe()->audios_manager_->get_audio_object(audio_file_id),
        get_page_block_caption_object(caption));
  }

  template <class T>
  void store(T &storer) const {
    using ::td::store;

    bool has_empty_audio = !audio_file_id.is_valid();
    BEGIN_STORE_FLAGS();
    STORE_FLAG(has_empty_audio);
    END_STORE_FLAGS();

    if (!has_empty_audio) {
      storer.context()->td().get_actor_unsafe()->audios_manager_->store_audio(audio_file_id, storer);
    }
    store(caption, storer);
  }

  template <class T>
  void parse(T &parser) {
    using ::td::parse;

    bool has_empty_audio;
    if (parser.version() >= static_cast<int32>(Version::FixPageBlockAudioEmptyFile)) {
      BEGIN_PARSE_FLAGS();
      PARSE_FLAG(has_empty_audio);
      END_PARSE_FLAGS();
    } else {
      has_empty_audio = false;
    }

    if (!has_empty_audio) {
      audio_file_id = parser.context()->td().get_actor_unsafe()->audios_manager_->parse_audio(parser);
    } else {
      audio_file_id = FileId();
    }
    parse(caption, parser);
  }
};

template <class F>
void WebPagesManager::PageBlock::call_impl(Type type, const PageBlock *ptr, F &&f) {
  switch (type) {
    case Type::Title:
      return f(static_cast<const WebPagesManager::PageBlockTitle *>(ptr));
    case Type::Subtitle:
      return f(static_cast<const WebPagesManager::PageBlockSubtitle *>(ptr));
    case Type::AuthorDate:
      return f(static_cast<const WebPagesManager::PageBlockAuthorDate *>(ptr));
    case Type::Header:
      return f(static_cast<const WebPagesManager::PageBlockHeader *>(ptr));
    case Type::Subheader:
      return f(static_cast<const WebPagesManager::PageBlockSubheader *>(ptr));
    case Type::Kicker:
      return f(static_cast<const WebPagesManager::PageBlockKicker *>(ptr));
    case Type::Paragraph:
      return f(static_cast<const WebPagesManager::PageBlockParagraph *>(ptr));
    case Type::Preformatted:
      return f(static_cast<const WebPagesManager::PageBlockPreformatted *>(ptr));
    case Type::Footer:
      return f(static_cast<const WebPagesManager::PageBlockFooter *>(ptr));
    case Type::Divider:
      return f(static_cast<const WebPagesManager::PageBlockDivider *>(ptr));
    case Type::Anchor:
      return f(static_cast<const WebPagesManager::PageBlockAnchor *>(ptr));
    case Type::List:
      return f(static_cast<const WebPagesManager::PageBlockList *>(ptr));
    case Type::BlockQuote:
      return f(static_cast<const WebPagesManager::PageBlockBlockQuote *>(ptr));
    case Type::PullQuote:
      return f(static_cast<const WebPagesManager::PageBlockPullQuote *>(ptr));
    case Type::Animation:
      return f(static_cast<const WebPagesManager::PageBlockAnimation *>(ptr));
    case Type::Photo:
      return f(static_cast<const WebPagesManager::PageBlockPhoto *>(ptr));
    case Type::Video:
      return f(static_cast<const WebPagesManager::PageBlockVideo *>(ptr));
    case Type::Cover:
      return f(static_cast<const WebPagesManager::PageBlockCover *>(ptr));
    case Type::Embedded:
      return f(static_cast<const WebPagesManager::PageBlockEmbedded *>(ptr));
    case Type::EmbeddedPost:
      return f(static_cast<const WebPagesManager::PageBlockEmbeddedPost *>(ptr));
    case Type::Collage:
      return f(static_cast<const WebPagesManager::PageBlockCollage *>(ptr));
    case Type::Slideshow:
      return f(static_cast<const WebPagesManager::PageBlockSlideshow *>(ptr));
    case Type::ChatLink:
      return f(static_cast<const WebPagesManager::PageBlockChatLink *>(ptr));
    case Type::Audio:
      return f(static_cast<const WebPagesManager::PageBlockAudio *>(ptr));
  }
  UNREACHABLE();
}

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
      auto web_page_to_delete = get_web_page(web_page_id);
      if (web_page_to_delete != nullptr) {
        if (web_page_to_delete->logevent_id != 0) {
          LOG(INFO) << "Erase " << web_page_id << " from binlog";
          binlog_erase(G()->td_db()->get_binlog(), web_page_to_delete->logevent_id);
          web_page_to_delete->logevent_id = 0;
        }
        if (web_page_to_delete->file_source_id.is_valid()) {
          td_->file_manager_->change_files_source(web_page_to_delete->file_source_id,
                                                  get_web_page_file_ids(web_page_to_delete), vector<FileId>());
        }
        web_pages_.erase(web_page_id);
      }

      update_messages_content(web_page_id, false);
      if (!G()->parameters().use_message_db) {
        //        update_messages_content(web_page_id, false);
      } else {
        LOG(INFO) << "Delete " << web_page_id << " from database";
        G()->td_db()->get_sqlite_pmc()->erase(get_web_page_database_key(web_page_id), Auto()
                                              /*
              PromiseCreator::lambda([web_page_id](Result<> result) {
                if (result.is_ok()) {
                  send_closure(G()->web_pages_manager(), &WebPagesManager::update_messages_content, web_page_id, false);
                }
              })
            */
        );
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
      LOG(INFO) << "Got pending " << web_page_id << ", date = " << web_page_date << ", now = " << G()->server_time();

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
      if ((web_page->flags_ & WEBPAGE_FLAG_HAS_PHOTO) && web_page->photo_->get_id() == telegram_api::photo::ID) {
        page->photo = get_photo(td_->file_manager_.get(), move_tl_object_as<telegram_api::photo>(web_page->photo_),
                                owner_dialog_id);
      } else {
        page->photo.id = -2;
      }
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
          page->document_type = parsed_document.first;
          page->document_file_id = parsed_document.second;
        }
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
  if (page != nullptr) {
    old_instant_view = std::move(page->instant_view);
    web_page->logevent_id = page->logevent_id;
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

  update_messages_content(web_page_id, true);

  if (!from_database) {
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

void WebPagesManager::on_get_web_page_by_url(const string &url, WebPageId web_page_id, bool from_database) {
  if (!from_database && G()->parameters().use_message_db) {
    if (web_page_id.is_valid()) {
      G()->td_db()->get_sqlite_pmc()->set(get_web_page_url_database_key(url), to_string(web_page_id.get()), Auto());
    } else {
      G()->td_db()->get_sqlite_pmc()->erase(get_web_page_url_database_key(url), Auto());
    }
  }

  auto &cached_web_page_id = url_to_web_page_id_[url];
  if (cached_web_page_id.is_valid() && web_page_id.is_valid() && web_page_id != cached_web_page_id) {
    LOG(ERROR) << "Url \"" << url << "\" preview is changed from " << cached_web_page_id << " to " << web_page_id;
  }

  cached_web_page_id = web_page_id;
}

void WebPagesManager::wait_for_pending_web_page(DialogId dialog_id, MessageId message_id, WebPageId web_page_id) {
  LOG(INFO) << "Waiting for " << web_page_id << " needed in " << message_id << " in " << dialog_id;
  pending_web_pages_[web_page_id].emplace(dialog_id, message_id);
  pending_web_pages_timeout_.add_timeout_in(web_page_id.get(), 1.0);
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
    pending_web_pages_timeout_.add_timeout_in(web_page_id.get(), 1.0);
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
  if (text->text_.empty()) {
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

  auto web_page_instant_view = get_web_page_instant_view(web_page_id);
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
    auto web_page_instant_view = get_web_page_instant_view(web_page_id);
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
  auto web_page = get_web_page(web_page_id);
  CHECK(web_page != nullptr && !web_page->instant_view.is_empty);

  auto promise = PromiseCreator::lambda([web_page_id](Result<> result) {
    send_closure(G()->web_pages_manager(), &WebPagesManager::update_web_page_instant_view_load_requests, web_page_id,
                 true, std::move(result));
  });

  td_->create_handler<GetWebPageQuery>(std::move(promise))
      ->send(web_page->url, web_page->instant_view.is_full ? web_page->instant_view.hash : 0);
}

void WebPagesManager::on_load_web_page_instant_view_from_database(WebPageId web_page_id, string value) {
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
    if (log_event_parse(result, value).is_error()) {
      result = WebPageInstantView();

      LOG(ERROR) << "Erase instant view in " << web_page_id << " from database";
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

  auto web_page_instant_view = get_web_page_instant_view(web_page_id);
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

  auto web_page = get_web_page(web_page_id);
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
  LOG(INFO) << "Reload url \"" << url << '"';
  td_->create_handler<GetWebPageQuery>(std::move(promise))->send(url, 0);
}

SecretInputMedia WebPagesManager::get_secret_input_media(WebPageId web_page_id) const {
  if (!web_page_id.is_valid()) {
    return SecretInputMedia{};
  }

  auto web_page = get_web_page(web_page_id);
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
  auto web_page = get_web_page(web_page_id);
  if (web_page == nullptr) {
    return nullptr;
  }
  return make_tl_object<td_api::webPage>(
      web_page->url, web_page->display_url, web_page->type, web_page->site_name, web_page->title, web_page->description,
      get_photo_object(td_->file_manager_.get(), &web_page->photo), web_page->embed_url, web_page->embed_type,
      web_page->embed_dimensions.width, web_page->embed_dimensions.height, web_page->duration, web_page->author,
      web_page->document_type == DocumentsManager::DocumentType::Animation
          ? td_->animations_manager_->get_animation_object(web_page->document_file_id, "get_web_page_object")
          : nullptr,
      web_page->document_type == DocumentsManager::DocumentType::Audio
          ? td_->audios_manager_->get_audio_object(web_page->document_file_id)
          : nullptr,
      web_page->document_type == DocumentsManager::DocumentType::General
          ? td_->documents_manager_->get_document_object(web_page->document_file_id)
          : nullptr,
      web_page->document_type == DocumentsManager::DocumentType::Sticker
          ? td_->stickers_manager_->get_sticker_object(web_page->document_file_id)
          : nullptr,
      web_page->document_type == DocumentsManager::DocumentType::Video
          ? td_->videos_manager_->get_video_object(web_page->document_file_id)
          : nullptr,
      web_page->document_type == DocumentsManager::DocumentType::VideoNote
          ? td_->video_notes_manager_->get_video_note_object(web_page->document_file_id)
          : nullptr,
      web_page->document_type == DocumentsManager::DocumentType::VoiceNote
          ? td_->voice_notes_manager_->get_voice_note_object(web_page->document_file_id)
          : nullptr,
      !web_page->instant_view.is_empty);
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
  return make_tl_object<td_api::webPageInstantView>(get_page_block_objects(web_page_instant_view->page_blocks),
                                                    web_page_instant_view->is_rtl, web_page_instant_view->is_full);
}

void WebPagesManager::update_messages_content(WebPageId web_page_id, bool have_web_page) {
  LOG(INFO) << "Update messages awaiting " << web_page_id;
  auto it = pending_web_pages_.find(web_page_id);
  if (it != pending_web_pages_.end()) {
    auto full_message_ids = std::move(it->second);
    pending_web_pages_.erase(it);
    for (auto full_message_id : full_message_ids) {
      send_closure_later(G()->messages_manager(), &MessagesManager::on_update_message_web_page, full_message_id,
                         have_web_page);
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
  auto web_page = get_web_page(web_page_id);
  if (web_page == nullptr || web_page->instant_view.is_empty) {
    return nullptr;
  }
  return &web_page->instant_view;
}

void WebPagesManager::on_pending_web_page_timeout_callback(void *web_pages_manager_ptr, int64 web_page_id) {
  static_cast<WebPagesManager *>(web_pages_manager_ptr)->on_pending_web_page_timeout(WebPageId(web_page_id));
}

void WebPagesManager::on_pending_web_page_timeout(WebPageId web_page_id) {
  int32 count = 0;
  auto it = pending_web_pages_.find(web_page_id);
  if (it != pending_web_pages_.end()) {
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
    LOG(WARNING) << "Have no messages waiting for " << web_page_id;
  }
}

WebPagesManager::RichText WebPagesManager::get_rich_text(tl_object_ptr<telegram_api::RichText> &&rich_text_ptr,
                                                         const std::unordered_map<int64, FileId> &documents) {
  CHECK(rich_text_ptr != nullptr);

  RichText result;
  switch (rich_text_ptr->get_id()) {
    case telegram_api::textEmpty::ID:
      break;
    case telegram_api::textPlain::ID: {
      auto rich_text = move_tl_object_as<telegram_api::textPlain>(rich_text_ptr);
      result.content = std::move(rich_text->text_);
      break;
    }
    case telegram_api::textBold::ID: {
      auto rich_text = move_tl_object_as<telegram_api::textBold>(rich_text_ptr);
      result.type = RichText::Type::Bold;
      result.texts.push_back(get_rich_text(std::move(rich_text->text_), documents));
      break;
    }
    case telegram_api::textItalic::ID: {
      auto rich_text = move_tl_object_as<telegram_api::textItalic>(rich_text_ptr);
      result.type = RichText::Type::Italic;
      result.texts.push_back(get_rich_text(std::move(rich_text->text_), documents));
      break;
    }
    case telegram_api::textUnderline::ID: {
      auto rich_text = move_tl_object_as<telegram_api::textUnderline>(rich_text_ptr);
      result.type = RichText::Type::Underline;
      result.texts.push_back(get_rich_text(std::move(rich_text->text_), documents));
      break;
    }
    case telegram_api::textStrike::ID: {
      auto rich_text = move_tl_object_as<telegram_api::textStrike>(rich_text_ptr);
      result.type = RichText::Type::Strikethrough;
      result.texts.push_back(get_rich_text(std::move(rich_text->text_), documents));
      break;
    }
    case telegram_api::textFixed::ID: {
      auto rich_text = move_tl_object_as<telegram_api::textFixed>(rich_text_ptr);
      result.type = RichText::Type::Fixed;
      result.texts.push_back(get_rich_text(std::move(rich_text->text_), documents));
      break;
    }
    case telegram_api::textUrl::ID: {
      auto rich_text = move_tl_object_as<telegram_api::textUrl>(rich_text_ptr);
      result.type = RichText::Type::Url;
      result.content = std::move(rich_text->url_);
      result.texts.push_back(get_rich_text(std::move(rich_text->text_), documents));
      result.web_page_id = WebPageId(rich_text->webpage_id_);
      break;
    }
    case telegram_api::textEmail::ID: {
      auto rich_text = move_tl_object_as<telegram_api::textEmail>(rich_text_ptr);
      result.type = RichText::Type::EmailAddress;
      result.content = std::move(rich_text->email_);
      result.texts.push_back(get_rich_text(std::move(rich_text->text_), documents));
      break;
    }
    case telegram_api::textConcat::ID: {
      auto rich_text = move_tl_object_as<telegram_api::textConcat>(rich_text_ptr);
      result.type = RichText::Type::Concatenation;
      result.texts = get_rich_texts(std::move(rich_text->texts_), documents);
      break;
    }
    case telegram_api::textSubscript::ID: {
      auto rich_text = move_tl_object_as<telegram_api::textSubscript>(rich_text_ptr);
      result.type = RichText::Type::Subscript;
      result.texts.push_back(get_rich_text(std::move(rich_text->text_), documents));
      break;
    }
    case telegram_api::textSuperscript::ID: {
      auto rich_text = move_tl_object_as<telegram_api::textSuperscript>(rich_text_ptr);
      result.type = RichText::Type::Superscript;
      result.texts.push_back(get_rich_text(std::move(rich_text->text_), documents));
      break;
    }
    case telegram_api::textMarked::ID: {
      auto rich_text = move_tl_object_as<telegram_api::textMarked>(rich_text_ptr);
      result.type = RichText::Type::Marked;
      result.texts.push_back(get_rich_text(std::move(rich_text->text_), documents));
      break;
    }
    case telegram_api::textPhone::ID: {
      auto rich_text = move_tl_object_as<telegram_api::textPhone>(rich_text_ptr);
      result.type = RichText::Type::PhoneNumber;
      result.content = std::move(rich_text->phone_);
      result.texts.push_back(get_rich_text(std::move(rich_text->text_), documents));
      break;
    }
    case telegram_api::textImage::ID: {
      auto rich_text = move_tl_object_as<telegram_api::textImage>(rich_text_ptr);
      auto it = documents.find(rich_text->document_id_);
      if (it != documents.end()) {
        result.type = RichText::Type::InlineImage;
        result.document_file_id = it->second;
        Dimensions dimensions = get_dimensions(rich_text->w_, rich_text->h_);
        result.content = PSTRING() << (dimensions.width * static_cast<uint32>(65536) + dimensions.height);
      } else {
        LOG(ERROR) << "Can't find document " << rich_text->document_id_;
      }
      break;
    }
    default:
      UNREACHABLE();
  }
  return result;
}

vector<WebPagesManager::RichText> WebPagesManager::get_rich_texts(
    vector<tl_object_ptr<telegram_api::RichText>> &&rich_text_ptrs,
    const std::unordered_map<int64, FileId> &documents) {
  return transform(std::move(rich_text_ptrs), [&documents](tl_object_ptr<telegram_api::RichText> &&rich_text) {
    return get_rich_text(std::move(rich_text), documents);
  });
}

tl_object_ptr<td_api::RichText> WebPagesManager::get_rich_text_object(const RichText &rich_text) {
  switch (rich_text.type) {
    case RichText::Type::Plain:
      return make_tl_object<td_api::richTextPlain>(rich_text.content);
    case RichText::Type::Bold:
      return make_tl_object<td_api::richTextBold>(get_rich_text_object(rich_text.texts[0]));
    case RichText::Type::Italic:
      return make_tl_object<td_api::richTextItalic>(get_rich_text_object(rich_text.texts[0]));
    case RichText::Type::Underline:
      return make_tl_object<td_api::richTextUnderline>(get_rich_text_object(rich_text.texts[0]));
    case RichText::Type::Strikethrough:
      return make_tl_object<td_api::richTextStrikethrough>(get_rich_text_object(rich_text.texts[0]));
    case RichText::Type::Fixed:
      return make_tl_object<td_api::richTextFixed>(get_rich_text_object(rich_text.texts[0]));
    case RichText::Type::Url:
      return make_tl_object<td_api::richTextUrl>(get_rich_text_object(rich_text.texts[0]), rich_text.content);
    case RichText::Type::EmailAddress:
      return make_tl_object<td_api::richTextEmailAddress>(get_rich_text_object(rich_text.texts[0]), rich_text.content);
    case RichText::Type::Concatenation:
      return make_tl_object<td_api::richTexts>(get_rich_text_objects(rich_text.texts));
    case RichText::Type::Subscript:
      return make_tl_object<td_api::richTextSubscript>(get_rich_text_object(rich_text.texts[0]));
    case RichText::Type::Superscript:
      return make_tl_object<td_api::richTextSuperscript>(get_rich_text_object(rich_text.texts[0]));
    case RichText::Type::Marked:
      return make_tl_object<td_api::richTextMarked>(get_rich_text_object(rich_text.texts[0]));
    case RichText::Type::PhoneNumber:
      return make_tl_object<td_api::richTextPhoneNumber>(get_rich_text_object(rich_text.texts[0]), rich_text.content);
    case RichText::Type::InlineImage: {
      auto dimensions = to_integer<uint32>(rich_text.content);
      auto width = static_cast<int32>(dimensions / 65536);
      auto height = static_cast<int32>(dimensions % 65536);
      return make_tl_object<td_api::richTextInlineImage>(
          G()->td().get_actor_unsafe()->documents_manager_->get_document_object(rich_text.document_file_id), width,
          height);
    }
  }
  UNREACHABLE();
  return nullptr;
}

vector<tl_object_ptr<td_api::RichText>> WebPagesManager::get_rich_text_objects(const vector<RichText> &rich_texts) {
  return transform(rich_texts, [](const RichText &rich_text) { return get_rich_text_object(rich_text); });
}

WebPagesManager::PageBlockCaption WebPagesManager::get_page_block_caption(
    tl_object_ptr<telegram_api::pageCaption> &&page_caption, const std::unordered_map<int64, FileId> &documents) {
  CHECK(page_caption != nullptr);
  PageBlockCaption result;
  result.text = get_rich_text(std::move(page_caption->text_), documents);
  result.credit = get_rich_text(std::move(page_caption->credit_), documents);
  return result;
}

td_api::object_ptr<td_api::pageBlockCaption> WebPagesManager::get_page_block_caption_object(
    const PageBlockCaption &caption) {
  return td_api::make_object<td_api::pageBlockCaption>(get_rich_text_object(caption.text),
                                                       get_rich_text_object(caption.credit));
}

vector<tl_object_ptr<td_api::PageBlock>> WebPagesManager::get_page_block_objects(
    const vector<unique_ptr<PageBlock>> &page_blocks) {
  return transform(page_blocks,
                   [](const unique_ptr<PageBlock> &page_block) { return page_block->get_page_block_object(); });
}

unique_ptr<WebPagesManager::PageBlock> WebPagesManager::get_page_block(
    tl_object_ptr<telegram_api::PageBlock> page_block_ptr, const std::unordered_map<int64, FileId> &animations,
    const std::unordered_map<int64, FileId> &audios, const std::unordered_map<int64, FileId> &documents,
    const std::unordered_map<int64, Photo> &photos, const std::unordered_map<int64, FileId> &videos) const {
  CHECK(page_block_ptr != nullptr);
  switch (page_block_ptr->get_id()) {
    case telegram_api::pageBlockUnsupported::ID:
      return nullptr;
    case telegram_api::pageBlockTitle::ID: {
      auto page_block = move_tl_object_as<telegram_api::pageBlockTitle>(page_block_ptr);
      return make_unique<PageBlockTitle>(get_rich_text(std::move(page_block->text_), documents));
    }
    case telegram_api::pageBlockSubtitle::ID: {
      auto page_block = move_tl_object_as<telegram_api::pageBlockSubtitle>(page_block_ptr);
      return make_unique<PageBlockSubtitle>(get_rich_text(std::move(page_block->text_), documents));
    }
    case telegram_api::pageBlockAuthorDate::ID: {
      auto page_block = move_tl_object_as<telegram_api::pageBlockAuthorDate>(page_block_ptr);
      return make_unique<PageBlockAuthorDate>(get_rich_text(std::move(page_block->author_), documents),
                                              page_block->published_date_);
    }
    case telegram_api::pageBlockHeader::ID: {
      auto page_block = move_tl_object_as<telegram_api::pageBlockHeader>(page_block_ptr);
      return make_unique<PageBlockHeader>(get_rich_text(std::move(page_block->text_), documents));
    }
    case telegram_api::pageBlockSubheader::ID: {
      auto page_block = move_tl_object_as<telegram_api::pageBlockSubheader>(page_block_ptr);
      return make_unique<PageBlockSubheader>(get_rich_text(std::move(page_block->text_), documents));
    }
    case telegram_api::pageBlockKicker::ID: {
      auto page_block = move_tl_object_as<telegram_api::pageBlockKicker>(page_block_ptr);
      return make_unique<PageBlockKicker>(get_rich_text(std::move(page_block->text_), documents));
    }
    case telegram_api::pageBlockParagraph::ID: {
      auto page_block = move_tl_object_as<telegram_api::pageBlockParagraph>(page_block_ptr);
      return make_unique<PageBlockParagraph>(get_rich_text(std::move(page_block->text_), documents));
    }
    case telegram_api::pageBlockPreformatted::ID: {
      auto page_block = move_tl_object_as<telegram_api::pageBlockPreformatted>(page_block_ptr);
      return td::make_unique<PageBlockPreformatted>(get_rich_text(std::move(page_block->text_), documents),
                                                    std::move(page_block->language_));
    }
    case telegram_api::pageBlockFooter::ID: {
      auto page_block = move_tl_object_as<telegram_api::pageBlockFooter>(page_block_ptr);
      return make_unique<PageBlockFooter>(get_rich_text(std::move(page_block->text_), documents));
    }
    case telegram_api::pageBlockDivider::ID:
      return make_unique<PageBlockDivider>();
    case telegram_api::pageBlockAnchor::ID: {
      auto page_block = move_tl_object_as<telegram_api::pageBlockAnchor>(page_block_ptr);
      return td::make_unique<PageBlockAnchor>(std::move(page_block->name_));
    }
    case telegram_api::pageBlockList::ID: {
      auto page_block = move_tl_object_as<telegram_api::pageBlockList>(page_block_ptr);
      return nullptr;
      // return td::make_unique<PageBlockList>(get_rich_texts(std::move(page_block->items_), documents), page_block->ordered_);
    }
    case telegram_api::pageBlockBlockquote::ID: {
      auto page_block = move_tl_object_as<telegram_api::pageBlockBlockquote>(page_block_ptr);
      return make_unique<PageBlockBlockQuote>(get_rich_text(std::move(page_block->text_), documents),
                                              get_rich_text(std::move(page_block->caption_), documents));
    }
    case telegram_api::pageBlockPullquote::ID: {
      auto page_block = move_tl_object_as<telegram_api::pageBlockPullquote>(page_block_ptr);
      return make_unique<PageBlockPullQuote>(get_rich_text(std::move(page_block->text_), documents),
                                             get_rich_text(std::move(page_block->caption_), documents));
    }
    case telegram_api::pageBlockPhoto::ID: {
      auto page_block = move_tl_object_as<telegram_api::pageBlockPhoto>(page_block_ptr);
      auto it = photos.find(page_block->photo_id_);
      Photo photo;
      if (it == photos.end()) {
        photo.id = -2;
      } else {
        photo = it->second;
      }
      string url;
      WebPageId web_page_id;
      if ((page_block->flags_ & telegram_api::pageBlockPhoto::URL_MASK) != 0) {
        url = std::move(page_block->url_);
        web_page_id = WebPageId(page_block->webpage_id_);
      }
      return td::make_unique<PageBlockPhoto>(std::move(photo),
                                             get_page_block_caption(std::move(page_block->caption_), documents),
                                             std::move(url), web_page_id);
    }
    case telegram_api::pageBlockVideo::ID: {
      auto page_block = move_tl_object_as<telegram_api::pageBlockVideo>(page_block_ptr);
      bool need_autoplay = (page_block->flags_ & telegram_api::pageBlockVideo::AUTOPLAY_MASK) != 0;
      bool is_looped = (page_block->flags_ & telegram_api::pageBlockVideo::LOOP_MASK) != 0;
      auto animations_it = animations.find(page_block->video_id_);
      if (animations_it != animations.end()) {
        LOG_IF(ERROR, !is_looped) << "Receive non-looped animation";
        return make_unique<PageBlockAnimation>(
            animations_it->second, get_page_block_caption(std::move(page_block->caption_), documents), need_autoplay);
      }

      auto it = videos.find(page_block->video_id_);
      FileId video_file_id;
      if (it != videos.end()) {
        video_file_id = it->second;
      }
      return make_unique<PageBlockVideo>(
          video_file_id, get_page_block_caption(std::move(page_block->caption_), documents), need_autoplay, is_looped);
    }
    case telegram_api::pageBlockCover::ID: {
      auto page_block = move_tl_object_as<telegram_api::pageBlockCover>(page_block_ptr);
      auto cover = get_page_block(std::move(page_block->cover_), animations, audios, documents, photos, videos);
      if (cover == nullptr) {
        return nullptr;
      }
      return make_unique<PageBlockCover>(std::move(cover));
    }
    case telegram_api::pageBlockEmbed::ID: {
      auto page_block = move_tl_object_as<telegram_api::pageBlockEmbed>(page_block_ptr);
      bool is_full_width = (page_block->flags_ & telegram_api::pageBlockEmbed::FULL_WIDTH_MASK) != 0;
      bool allow_scrolling = (page_block->flags_ & telegram_api::pageBlockEmbed::ALLOW_SCROLLING_MASK) != 0;
      bool has_dimensions = (page_block->flags_ & telegram_api::pageBlockEmbed::W_MASK) != 0;
      auto it = (page_block->flags_ & telegram_api::pageBlockEmbed::POSTER_PHOTO_ID_MASK) != 0
                    ? photos.find(page_block->poster_photo_id_)
                    : photos.end();
      Photo poster_photo;
      if (it == photos.end()) {
        poster_photo.id = -2;
      } else {
        poster_photo = it->second;
      }
      Dimensions dimensions;
      if (has_dimensions) {
        dimensions = get_dimensions(page_block->w_, page_block->h_);
      }
      return td::make_unique<PageBlockEmbedded>(
          std::move(page_block->url_), std::move(page_block->html_), std::move(poster_photo), dimensions,
          get_page_block_caption(std::move(page_block->caption_), documents), is_full_width, allow_scrolling);
    }
    case telegram_api::pageBlockEmbedPost::ID: {
      auto page_block = move_tl_object_as<telegram_api::pageBlockEmbedPost>(page_block_ptr);
      auto it = photos.find(page_block->author_photo_id_);
      Photo author_photo;
      if (it == photos.end()) {
        author_photo.id = -2;
      } else {
        author_photo = it->second;
      }
      return td::make_unique<PageBlockEmbeddedPost>(
          std::move(page_block->url_), std::move(page_block->author_), std::move(author_photo), page_block->date_,
          get_page_blocks(std::move(page_block->blocks_), animations, audios, documents, photos, videos),
          get_page_block_caption(std::move(page_block->caption_), documents));
    }
    case telegram_api::pageBlockCollage::ID: {
      auto page_block = move_tl_object_as<telegram_api::pageBlockCollage>(page_block_ptr);
      return td::make_unique<PageBlockCollage>(
          get_page_blocks(std::move(page_block->items_), animations, audios, documents, photos, videos),
          get_page_block_caption(std::move(page_block->caption_), documents));
    }
    case telegram_api::pageBlockSlideshow::ID: {
      auto page_block = move_tl_object_as<telegram_api::pageBlockSlideshow>(page_block_ptr);
      return td::make_unique<PageBlockSlideshow>(
          get_page_blocks(std::move(page_block->items_), animations, audios, documents, photos, videos),
          get_page_block_caption(std::move(page_block->caption_), documents));
    }
    case telegram_api::pageBlockChannel::ID: {
      auto page_block = move_tl_object_as<telegram_api::pageBlockChannel>(page_block_ptr);
      CHECK(page_block->channel_ != nullptr);
      if (page_block->channel_->get_id() == telegram_api::channel::ID) {
        auto channel = static_cast<telegram_api::channel *>(page_block->channel_.get());
        ChannelId channel_id(channel->id_);
        if (!channel_id.is_valid()) {
          LOG(ERROR) << "Receive invalid " << channel_id;
          return nullptr;
        }

        if (td_->contacts_manager_->have_channel_force(channel_id)) {
          td_->contacts_manager_->on_get_chat(std::move(page_block->channel_), "pageBlockChannel");
          LOG(INFO) << "Receive known min " << channel_id;
          return td::make_unique<PageBlockChatLink>(td_->contacts_manager_->get_channel_title(channel_id),
                                                    *td_->contacts_manager_->get_channel_dialog_photo(channel_id),
                                                    td_->contacts_manager_->get_channel_username(channel_id));
        } else {
          return td::make_unique<PageBlockChatLink>(
              std::move(channel->title_), get_dialog_photo(td_->file_manager_.get(), std::move(channel->photo_)),
              std::move(channel->username_));
        }
      } else {
        LOG(ERROR) << "Receive wrong channel " << to_string(page_block->channel_);
        return nullptr;
      }
    }
    case telegram_api::pageBlockAudio::ID: {
      auto page_block = move_tl_object_as<telegram_api::pageBlockAudio>(page_block_ptr);
      auto it = audios.find(page_block->audio_id_);
      FileId audio_file_id;
      if (it != audios.end()) {
        audio_file_id = it->second;
      }
      return make_unique<PageBlockAudio>(audio_file_id,
                                         get_page_block_caption(std::move(page_block->caption_), documents));
    }
    default:
      UNREACHABLE();
  }
  return nullptr;
}

vector<unique_ptr<WebPagesManager::PageBlock>> WebPagesManager::get_page_blocks(
    vector<tl_object_ptr<telegram_api::PageBlock>> page_block_ptrs, const std::unordered_map<int64, FileId> &animations,
    const std::unordered_map<int64, FileId> &audios, const std::unordered_map<int64, FileId> &documents,
    const std::unordered_map<int64, Photo> &photos, const std::unordered_map<int64, FileId> &videos) const {
  vector<unique_ptr<PageBlock>> result;
  result.reserve(page_block_ptrs.size());
  for (auto &page_block_ptr : page_block_ptrs) {
    auto page_block = get_page_block(std::move(page_block_ptr), animations, audios, documents, photos, videos);
    if (page_block != nullptr) {
      result.push_back(std::move(page_block));
    }
  }
  return result;
}

void WebPagesManager::on_get_web_page_instant_view(WebPage *web_page, tl_object_ptr<telegram_api::page> &&page,
                                                   int32 hash, DialogId owner_dialog_id) {
  CHECK(page != nullptr);
  if ((page->flags_ & telegram_api::page::V2_MASK) == 0) {
    return;
  }

  std::unordered_map<int64, Photo> photos;
  for (auto &photo_ptr : page->photos_) {
    if (photo_ptr->get_id() == telegram_api::photo::ID) {
      Photo photo =
          get_photo(td_->file_manager_.get(), move_tl_object_as<telegram_api::photo>(photo_ptr), owner_dialog_id);
      int64 photo_id = photo.id;
      photos.emplace(photo_id, std::move(photo));
    }
  }
  if (web_page->photo.id != -2 && web_page->photo.id != 0) {
    photos.emplace(web_page->photo.id, web_page->photo);
  }

  std::unordered_map<int64, FileId> animations;
  std::unordered_map<int64, FileId> audios;
  std::unordered_map<int64, FileId> documents;
  std::unordered_map<int64, FileId> videos;
  for (auto &document_ptr : page->documents_) {
    if (document_ptr->get_id() == telegram_api::document::ID) {
      auto document = move_tl_object_as<telegram_api::document>(document_ptr);
      auto document_id = document->id_;
      auto parsed_document = td_->documents_manager_->on_get_document(std::move(document), owner_dialog_id);
      if (parsed_document.first == DocumentsManager::DocumentType::Animation) {
        animations.emplace(document_id, parsed_document.second);
      } else if (parsed_document.first == DocumentsManager::DocumentType::Audio) {
        audios.emplace(document_id, parsed_document.second);
      } else if (parsed_document.first == DocumentsManager::DocumentType::General) {
        documents.emplace(document_id, parsed_document.second);
      } else if (parsed_document.first == DocumentsManager::DocumentType::Video) {
        videos.emplace(document_id, parsed_document.second);
      } else {
        LOG(ERROR) << "Receive document of the wrong type " << static_cast<int32>(parsed_document.first);
      }
    }
  }
  if (web_page->document_type == DocumentsManager::DocumentType::Animation) {
    auto file_view = td_->file_manager_->get_file_view(web_page->document_file_id);
    if (file_view.has_remote_location()) {
      animations.emplace(file_view.remote_location().get_id(), web_page->document_file_id);
    } else {
      LOG(ERROR) << "Animation has no remote location";
    }
  }
  if (web_page->document_type == DocumentsManager::DocumentType::Audio) {
    auto file_view = td_->file_manager_->get_file_view(web_page->document_file_id);
    if (file_view.has_remote_location()) {
      audios.emplace(file_view.remote_location().get_id(), web_page->document_file_id);
    } else {
      LOG(ERROR) << "Audio has no remote location";
    }
  }
  if (web_page->document_type == DocumentsManager::DocumentType::General) {
    auto file_view = td_->file_manager_->get_file_view(web_page->document_file_id);
    if (file_view.has_remote_location()) {
      documents.emplace(file_view.remote_location().get_id(), web_page->document_file_id);
    } else {
      LOG(ERROR) << "Document has no remote location";
    }
  }
  if (web_page->document_type == DocumentsManager::DocumentType::Video) {
    auto file_view = td_->file_manager_->get_file_view(web_page->document_file_id);
    if (file_view.has_remote_location()) {
      videos.emplace(file_view.remote_location().get_id(), web_page->document_file_id);
    } else {
      LOG(ERROR) << "Video has no remote location";
    }
  }

  LOG(INFO) << "Receive a web page instant view with " << page->blocks_.size() << " blocks, " << animations.size()
            << " animations, " << audios.size() << " audios, " << documents.size() << " documents, " << photos.size()
            << " photos and " << videos.size() << " videos";
  web_page->instant_view.page_blocks =
      get_page_blocks(std::move(page->blocks_), animations, audios, documents, photos, videos);
  web_page->instant_view.is_rtl = (page->flags_ & telegram_api::page::RTL_MASK) != 0;
  web_page->instant_view.hash = hash;
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
    CHECK(web_page_out == nullptr);
    web_page_out = make_unique<WebPage>();
    td::parse(*web_page_out, parser);
  }
};

void WebPagesManager::save_web_page(const WebPage *web_page, WebPageId web_page_id, bool from_binlog) {
  if (!G()->parameters().use_message_db) {
    return;
  }

  CHECK(web_page != nullptr);
  if (!from_binlog) {
    WebPageLogEvent logevent(web_page_id, web_page);
    LogEventStorerImpl<WebPageLogEvent> storer(logevent);
    if (web_page->logevent_id == 0) {
      web_page->logevent_id = binlog_add(G()->td_db()->get_binlog(), LogEvent::HandlerType::WebPages, storer);
    } else {
      binlog_rewrite(G()->td_db()->get_binlog(), web_page->logevent_id, LogEvent::HandlerType::WebPages, storer);
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

  web_page->logevent_id = event.id_;

  update_web_page(std::move(web_page), web_page_id, true, false);
}

string WebPagesManager::get_web_page_database_key(WebPageId web_page_id) {
  return PSTRING() << "wp" << web_page_id.get();
}

void WebPagesManager::on_save_web_page_to_database(WebPageId web_page_id, bool success) {
  auto web_page = get_web_page(web_page_id);
  if (web_page == nullptr) {
    LOG(ERROR) << "Can't find " << (success ? "saved " : "failed to save ") << web_page_id;
    return;
  }

  if (!success) {
    LOG(ERROR) << "Failed to save " << web_page_id << " to database";
    save_web_page(web_page, web_page_id, web_page->logevent_id != 0);
  } else {
    LOG(INFO) << "Successfully saved " << web_page_id << " to database";
    if (web_page->logevent_id != 0) {
      LOG(INFO) << "Erase " << web_page_id << " from binlog";
      binlog_erase(G()->td_db()->get_binlog(), web_page->logevent_id);
      web_page->logevent_id = 0;
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
        LOG(FATAL) << status << ": " << format::as_hex_dump<4>(Slice(value));
      }
      update_web_page(std::move(result), web_page_id, true, true);
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
  auto web_page = get_web_page(web_page_id);
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
    auto web_page = get_web_page(web_page_id);
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
  auto web_page = get_web_page(web_page_id);
  if (web_page == nullptr) {
    return "";
  }
  return PSTRING() << web_page->title + " " + web_page->description;
}

void WebPagesManager::append_rich_text_file_ids(const RichText &rich_text, vector<FileId> &file_ids) {
  if (rich_text.type == RichText::Type::InlineImage) {
    CHECK(rich_text.document_file_id.is_valid());
    file_ids.push_back(rich_text.document_file_id);
  } else {
    for (auto &text : rich_text.texts) {
      append_rich_text_file_ids(text, file_ids);
    }
  }
}

void WebPagesManager::append_page_block_caption_file_ids(const PageBlockCaption &caption, vector<FileId> &file_ids) {
  append_rich_text_file_ids(caption.text, file_ids);
  append_rich_text_file_ids(caption.credit, file_ids);
}

vector<FileId> WebPagesManager::get_web_page_file_ids(const WebPage *web_page) {
  if (web_page == nullptr) {
    return vector<FileId>();
  }

  vector<FileId> result = photo_get_file_ids(web_page->photo);
  if (web_page->document_file_id.is_valid()) {
    result.push_back(web_page->document_file_id);
  }
  if (!web_page->instant_view.is_empty) {
    for (auto &page_block : web_page->instant_view.page_blocks) {
      page_block->append_file_ids(result);
    }
  }
  return result;
}

}  // namespace td
