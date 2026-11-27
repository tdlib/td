//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/Slice.h"

namespace td {

// the exported rich message keeps the blocks and the texts as the server sends them, because td_api objects merge
// some of them, like links and automatically detected links, and drop identifiers and list numbering

struct ExportInlineButtonAction {
  enum class Type : int32 {
    Url,
    Auth,
    WebView,
    Callback,
    CallbackWithPassword,
    Game,
    Buy,
    SwitchInline,
    SwitchInlineSame,
    UserProfile,
    CopyText,
    Disabled
  };
  enum class PeerType : int32 { SameBotPm, Pm, Chat, Megagroup, Broadcast, BotPm };

  Type type = Type::Url;
  string url;
  bool has_forward_text = false;
  string forward_text;
  string callback_data;
  string query;
  bool has_peer_types = false;
  vector<PeerType> peer_types;
  string copy_text;
  int64 user_id = 0;
  int32 button_id = 0;
  bool requires_password = false;
  bool same_peer = false;
};

Slice get_export_inline_button_action_type_string(ExportInlineButtonAction::Type type);

Slice get_export_inline_button_peer_type_string(ExportInlineButtonAction::PeerType type);

enum class ExportRichButtonStyle : int32 { Default, Primary, Success, Danger, Link };

Slice get_export_rich_button_style_string(ExportRichButtonStyle style);

enum class ExportRichButtonAlignment : int32 { Stretch, Left, Center, Right };

Slice get_export_rich_button_alignment_string(ExportRichButtonAlignment alignment);

struct ExportRichButton {
  ExportInlineButtonAction action;
  bool has_style = false;
  ExportRichButtonStyle style = ExportRichButtonStyle::Default;
};

struct ExportRichText {
  enum class Type : int32 {
    Empty,
    Plain,
    Concat,
    Bold,
    Italic,
    Underline,
    Strike,
    Fixed,
    Url,
    Email,
    Phone,
    Subscript,
    Superscript,
    Marked,
    Anchor,
    Math,
    CustomEmoji,
    Spoiler,
    Mention,
    Hashtag,
    BotCommand,
    Cashtag,
    AutoUrl,
    AutoEmail,
    AutoPhone,
    BankCard,
    TonAddress,
    MentionName,
    FormattedDate,
    InlineImage,
    Diff,
    Button
  };

  Type type = Type::Empty;
  // the text of Plain, and the alternative text of CustomEmoji
  string text;
  // the URL of Url, the address of Email and Phone, the name of Anchor and the source of Math
  string data;
  // a single child for the wrapping types, and the parts of Concat
  vector<ExportRichText> children;
  // the old text of Diff
  vector<ExportRichText> old_children;
  // non-null only for Button
  unique_ptr<ExportRichButton> button;
  // the web page of Url, the custom emoji of CustomEmoji, the document of InlineImage and the user of MentionName
  int64 id = 0;
  int32 date = 0;
  int32 width = 0;
  int32 height = 0;
  bool relative = false;
  bool short_time = false;
  bool long_time = false;
  bool short_date = false;
  bool long_date = false;
  bool day_of_week = false;
};

struct ExportRichCaption {
  ExportRichText text;
  ExportRichText credit;
};

struct ExportRichBlock;

enum class ExportRichTaskState : int32 { None, Unchecked, Checked };

enum class ExportRichListKind : int32 { Bullet, Ordered };

enum class ExportRichContent : int32 { Text, Blocks };

struct ExportRichListItem {
  ExportRichContent content = ExportRichContent::Text;
  ExportRichTaskState task_state = ExportRichTaskState::None;
  ExportRichText text;
  vector<ExportRichBlock> blocks;
  bool has_num = false;
  string num;
  bool has_type = false;
  string type;
  bool has_value = false;
  int32 value = 0;
};

enum class ExportRichTableAlignment : int32 { Left, Center, Right };

enum class ExportRichTableVerticalAlignment : int32 { Top, Middle, Bottom };

struct ExportRichTableCell {
  bool has_text = false;
  ExportRichText text;
  bool has_colspan = false;
  int32 colspan = 0;
  bool has_rowspan = false;
  int32 rowspan = 0;
  ExportRichTableAlignment alignment = ExportRichTableAlignment::Left;
  ExportRichTableVerticalAlignment vertical_alignment = ExportRichTableVerticalAlignment::Top;
  bool header = false;
};

struct ExportRichTableRow {
  vector<ExportRichTableCell> cells;
};

struct ExportRichRelatedArticle {
  string url;
  int64 webpage_id = 0;
  bool has_title = false;
  string title;
  bool has_description = false;
  string description;
  bool has_photo_id = false;
  int64 photo_id = 0;
  bool has_author = false;
  string author;
  bool has_published_date = false;
  int32 published_date = 0;
};

struct ExportRichChannel {
  enum class Source : int32 {
    ChatEmpty,
    Chat,
    ChatForbidden,
    Channel,
    ChannelForbidden,
    Community,
    CommunityForbidden
  };

  Source source = Source::ChatEmpty;
  int64 id = 0;
  bool has_title = false;
  string title;
  bool has_username = false;
  string username;
  bool has_access_hash = false;
  int64 access_hash = 0;
  bool broadcast = false;
  bool megagroup = false;
  bool monoforum = false;
};

struct ExportRichMapPoint {
  enum class Source : int32 { GeoPointEmpty, GeoPoint, InputGeoPointEmpty, InputGeoPoint };

  Source source = Source::GeoPointEmpty;
  double latitude = 0.0;
  double longitude = 0.0;
  bool has_access_hash = false;
  int64 access_hash = 0;
  bool has_accuracy_radius = false;
  int32 accuracy_radius = 0;
};

struct ExportRichBlock {
  enum class Kind : int32 {
    Unsupported,
    Heading,
    Paragraph,
    Footer,
    Thinking,
    AuthorDate,
    Code,
    Divider,
    Anchor,
    List,
    Quote,
    Photo,
    Video,
    Cover,
    Embed,
    EmbedPost,
    Collage,
    Slideshow,
    Channel,
    Audio,
    File,
    Math,
    Table,
    Details,
    RelatedArticles,
    Map,
    InputMap,
    ButtonRow
  };

  Kind kind = Kind::Unsupported;
  // the text of the text blocks, the author of AuthorDate, the text of a Quote with text,
  // and the title of Table, Details and RelatedArticles
  ExportRichText text;
  ExportRichText quote_caption;
  ExportRichCaption caption;
  string language;
  string formula;
  string name;
  string url;
  string author;
  bool has_optional_url = false;
  string optional_url;
  bool has_html = false;
  string html;
  // the blocks of a Quote with blocks, Cover, EmbedPost, Collage, Slideshow and Details
  vector<ExportRichBlock> blocks;
  vector<ExportRichListItem> list_items;
  vector<ExportRichTableRow> table_rows;
  vector<ExportRichRelatedArticle> related_articles;
  // the buttons of ButtonRow, which are Button texts
  vector<ExportRichText> buttons;
  ExportRichChannel channel;
  ExportRichMapPoint map_point;
  ExportRichListKind list_kind = ExportRichListKind::Bullet;
  bool has_list_type = false;
  string list_type;
  bool has_start = false;
  int32 start = 0;
  bool reversed = false;
  ExportRichContent quote_content = ExportRichContent::Text;
  ExportRichButtonAlignment button_alignment = ExportRichButtonAlignment::Stretch;
  bool has_optional_webpage_id = false;
  int64 optional_webpage_id = 0;
  bool has_poster_photo_id = false;
  int64 poster_photo_id = 0;
  // the size of Embed; both are present or absent together
  bool has_size = false;
  int32 width = 0;
  int32 height = 0;
  int64 photo_id = 0;
  int64 document_id = 0;
  int64 webpage_id = 0;
  int64 author_photo_id = 0;
  int32 date = 0;
  int32 heading_level = 0;
  int32 zoom = 0;
  int32 map_width = 0;
  int32 map_height = 0;
  bool full_width = false;
  bool allow_scrolling = false;
  bool autoplay = false;
  bool loop = false;
  bool spoiler = false;
  bool open = false;
  bool bordered = false;
  bool striped = false;
  bool compact = false;
  bool pullquote = false;
};

struct ExportFile {
  enum class SkipReason : int32 { None, Unavailable, FileType, FileSize };

  // the path relative to the export directory with '/' as the separator; empty if the file is skipped
  string relative_path;
  SkipReason skip_reason = SkipReason::FileType;
};

struct ExportRichPhoto {
  int64 id = 0;
  td_api::object_ptr<td_api::photo> photo;
  // the largest size of the photo; like the other files of rich messages, it is unavailable unless it is loaded
  ExportFile file{string(), ExportFile::SkipReason::Unavailable};
  // smaller sizes of the photo, which HTML pages show instead of scaling it: the photo in a block,
  // and the photo of a card of an embedded post's author or of a related article
  ExportFile thumbnail{string(), ExportFile::SkipReason::Unavailable};
  ExportFile card_thumbnail{string(), ExportFile::SkipReason::Unavailable};
};

struct ExportRichDocument {
  enum class Type : int32 { File, Animation, Audio, Sticker, Video, VideoNote, VoiceNote };

  int64 id = 0;
  Type type = Type::File;
  td_api::object_ptr<td_api::file> document;
  // nullptr if none
  td_api::object_ptr<td_api::thumbnail> thumbnail;
  string file_name;
  string mime_type;
  string sticker_emoji;
  string performer;
  string title;
  int32 duration = 0;
  int32 width = 0;
  int32 height = 0;
  ExportFile file{string(), ExportFile::SkipReason::Unavailable};
  ExportFile thumbnail_file{string(), ExportFile::SkipReason::Unavailable};
};

struct ExportRichMessage {
  vector<ExportRichBlock> blocks;
  // in the order in which the server sends them
  vector<ExportRichPhoto> photos;
  vector<ExportRichDocument> documents;
  bool rtl = false;
  // true, if the server sent only a part of the message
  bool part = false;

  // nullptr if the message has no such photo or document
  const ExportRichPhoto *get_photo(int64 photo_id) const;
  const ExportRichDocument *get_document(int64 document_id) const;
};

// the blocks of the message; its photos and documents are added by the caller
unique_ptr<ExportRichMessage> get_export_rich_message(const telegram_api::richMessage &rich_message);

}  // namespace td
