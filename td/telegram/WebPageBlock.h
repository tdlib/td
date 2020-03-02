//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/telegram/files/FileId.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/Photo.h"

#include "td/utils/common.h"
#include "td/utils/Slice.h"

#include <unordered_map>

namespace td {

struct GetWebPageBlockObjectContext;

class Td;

class WebPageBlock {
 protected:
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
    Kicker,
    Table,
    Details,
    RelatedArticles,
    Map,
    VoiceNote,
    Size
  };

  virtual Type get_type() const = 0;

  template <class F>
  static void call_impl(Type type, const WebPageBlock *ptr, F &&f);

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  static unique_ptr<WebPageBlock> parse(ParserT &parser);

  template <class StorerT>
  friend void store_web_page_block(const unique_ptr<WebPageBlock> &block, StorerT &storer);

  template <class ParserT>
  friend void parse_web_page_block(unique_ptr<WebPageBlock> &block, ParserT &parser);

  using Context = GetWebPageBlockObjectContext;

 public:
  WebPageBlock() = default;
  WebPageBlock(const WebPageBlock &) = delete;
  WebPageBlock &operator=(const WebPageBlock &) = delete;
  WebPageBlock(WebPageBlock &&) = delete;
  WebPageBlock &operator=(WebPageBlock &&) = delete;
  virtual ~WebPageBlock() = default;

  virtual void append_file_ids(const Td *td, vector<FileId> &file_ids) const = 0;

  virtual td_api::object_ptr<td_api::PageBlock> get_page_block_object(Context *context) const = 0;
};

void store(const unique_ptr<WebPageBlock> &block, LogEventStorerCalcLength &storer);

void store(const unique_ptr<WebPageBlock> &block, LogEventStorerUnsafe &storer);

void parse(unique_ptr<WebPageBlock> &block, LogEventParser &parser);

vector<unique_ptr<WebPageBlock>> get_web_page_blocks(
    Td *td, vector<tl_object_ptr<telegram_api::PageBlock>> page_block_ptrs,
    const std::unordered_map<int64, FileId> &animations, const std::unordered_map<int64, FileId> &audios,
    const std::unordered_map<int64, FileId> &documents, const std::unordered_map<int64, Photo> &photos,
    const std::unordered_map<int64, FileId> &videos, const std::unordered_map<int64, FileId> &voice_notes);

vector<td_api::object_ptr<td_api::PageBlock>> get_page_block_objects(
    const vector<unique_ptr<WebPageBlock>> &page_blocks, Td *td, Slice base_url);

}  // namespace td
