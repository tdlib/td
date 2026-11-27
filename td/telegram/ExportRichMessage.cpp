//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ExportRichMessage.h"

#include "td/utils/logging.h"

namespace td {

Slice get_export_inline_button_action_type_string(ExportInlineButtonAction::Type type) {
  using Type = ExportInlineButtonAction::Type;
  switch (type) {
    case Type::Url:
      return Slice("url");
    case Type::Auth:
      return Slice("auth");
    case Type::WebView:
      return Slice("web_view");
    case Type::Callback:
      return Slice("callback");
    case Type::CallbackWithPassword:
      return Slice("callback_with_password");
    case Type::Game:
      return Slice("game");
    case Type::Buy:
      return Slice("buy");
    case Type::SwitchInline:
      return Slice("switch_inline");
    case Type::SwitchInlineSame:
      return Slice("switch_inline_same");
    case Type::UserProfile:
      return Slice("user_profile");
    case Type::CopyText:
      return Slice("copy_text");
    case Type::Disabled:
      return Slice("disabled");
    default:
      UNREACHABLE();
      return Slice();
  }
}

Slice get_export_inline_button_peer_type_string(ExportInlineButtonAction::PeerType type) {
  using PeerType = ExportInlineButtonAction::PeerType;
  switch (type) {
    case PeerType::SameBotPm:
      return Slice("same_bot_pm");
    case PeerType::Pm:
      return Slice("pm");
    case PeerType::Chat:
      return Slice("chat");
    case PeerType::Megagroup:
      return Slice("megagroup");
    case PeerType::Broadcast:
      return Slice("broadcast");
    case PeerType::BotPm:
      return Slice("bot_pm");
    default:
      UNREACHABLE();
      return Slice();
  }
}

Slice get_export_rich_button_style_string(ExportRichButtonStyle style) {
  switch (style) {
    case ExportRichButtonStyle::Default:
      return Slice("default");
    case ExportRichButtonStyle::Primary:
      return Slice("primary");
    case ExportRichButtonStyle::Success:
      return Slice("success");
    case ExportRichButtonStyle::Danger:
      return Slice("danger");
    case ExportRichButtonStyle::Link:
      return Slice("link");
    default:
      UNREACHABLE();
      return Slice();
  }
}

Slice get_export_rich_button_alignment_string(ExportRichButtonAlignment alignment) {
  switch (alignment) {
    case ExportRichButtonAlignment::Stretch:
      return Slice("stretch");
    case ExportRichButtonAlignment::Left:
      return Slice("left");
    case ExportRichButtonAlignment::Center:
      return Slice("center");
    case ExportRichButtonAlignment::Right:
      return Slice("right");
    default:
      UNREACHABLE();
      return Slice();
  }
}

const ExportRichPhoto *ExportRichMessage::get_photo(int64 photo_id) const {
  for (const auto &photo : photos) {
    if (photo.id == photo_id) {
      return &photo;
    }
  }
  return nullptr;
}

const ExportRichDocument *ExportRichMessage::get_document(int64 document_id) const {
  for (const auto &document : documents) {
    if (document.id == document_id) {
      return &document;
    }
  }
  return nullptr;
}

namespace {

ExportInlineButtonAction::PeerType get_peer_type(const telegram_api::InlineQueryPeerType *type) {
  using PeerType = ExportInlineButtonAction::PeerType;
  switch (type->get_id()) {
    case telegram_api::inlineQueryPeerTypeSameBotPM::ID:
      return PeerType::SameBotPm;
    case telegram_api::inlineQueryPeerTypePM::ID:
      return PeerType::Pm;
    case telegram_api::inlineQueryPeerTypeChat::ID:
      return PeerType::Chat;
    case telegram_api::inlineQueryPeerTypeMegagroup::ID:
      return PeerType::Megagroup;
    case telegram_api::inlineQueryPeerTypeBroadcast::ID:
      return PeerType::Broadcast;
    case telegram_api::inlineQueryPeerTypeBotPM::ID:
      return PeerType::BotPm;
    default:
      UNREACHABLE();
      return PeerType::Pm;
  }
}

// returns false for the button types, which only the application sends
bool get_inline_button_action(const telegram_api::InlineButtonType *type, ExportInlineButtonAction &action) {
  using Type = ExportInlineButtonAction::Type;
  switch (type->get_id()) {
    case telegram_api::inlineButtonTypeUrl::ID:
      action.type = Type::Url;
      action.url = static_cast<const telegram_api::inlineButtonTypeUrl *>(type)->url_;
      return true;
    case telegram_api::inlineButtonTypeUrlAuth::ID: {
      const auto *button = static_cast<const telegram_api::inlineButtonTypeUrlAuth *>(type);
      action.type = Type::Auth;
      action.url = button->url_;
      action.button_id = button->button_id_;
      action.has_forward_text = (button->flags_ & 1) != 0;
      action.forward_text = button->fwd_text_;
      return true;
    }
    case telegram_api::inputInlineButtonTypeUrlAuth::ID:
    case telegram_api::inputInlineButtonTypeUserProfile::ID:
      return false;
    case telegram_api::inlineButtonTypeWebView::ID:
      action.type = Type::WebView;
      action.url = static_cast<const telegram_api::inlineButtonTypeWebView *>(type)->url_;
      return true;
    case telegram_api::inlineButtonTypeCallback::ID: {
      const auto *button = static_cast<const telegram_api::inlineButtonTypeCallback *>(type);
      action.requires_password = button->requires_password_;
      action.type = action.requires_password ? Type::CallbackWithPassword : Type::Callback;
      action.callback_data = button->data_.as_slice().str();
      return true;
    }
    case telegram_api::inlineButtonTypeGame::ID:
      action.type = Type::Game;
      return true;
    case telegram_api::inlineButtonTypeBuy::ID:
      action.type = Type::Buy;
      return true;
    case telegram_api::inlineButtonTypeSwitchInline::ID: {
      const auto *button = static_cast<const telegram_api::inlineButtonTypeSwitchInline *>(type);
      action.same_peer = button->same_peer_;
      action.type = action.same_peer ? Type::SwitchInlineSame : Type::SwitchInline;
      action.query = button->query_;
      action.has_peer_types = (button->flags_ & 2) != 0;
      for (const auto &peer_type : button->peer_types_) {
        action.peer_types.push_back(get_peer_type(peer_type.get()));
      }
      return true;
    }
    case telegram_api::inlineButtonTypeUserProfile::ID:
      action.type = Type::UserProfile;
      action.user_id = static_cast<const telegram_api::inlineButtonTypeUserProfile *>(type)->user_id_;
      return true;
    case telegram_api::inlineButtonTypeCopy::ID:
      action.type = Type::CopyText;
      action.copy_text = static_cast<const telegram_api::inlineButtonTypeCopy *>(type)->copy_text_;
      return true;
    case telegram_api::inlineButtonTypeDisabled::ID:
      action.type = Type::Disabled;
      return true;
    default:
      UNREACHABLE();
      return false;
  }
}

ExportRichText get_rich_text(const telegram_api::RichText *text);

bool get_rich_button(const telegram_api::RichText *text, const telegram_api::InlineButtonType *type,
                     const telegram_api::richButtonStyle *style, bool is_inline, ExportRichText &result) {
  using Type = ExportInlineButtonAction::Type;
  auto button = make_unique<ExportRichButton>();
  if (!get_inline_button_action(type, button->action)) {
    return false;
  }
  if (style != nullptr) {
    button->has_style = true;
    auto action_type = button->action.type;
    // only buttons inside a text, which don't open a link, can look like a link
    if (is_inline && style->link_ &&
        (action_type == Type::Callback || action_type == Type::CallbackWithPassword || action_type == Type::Disabled)) {
      button->style = ExportRichButtonStyle::Link;
    } else if (style->bg_danger_) {
      button->style = ExportRichButtonStyle::Danger;
    } else if (style->bg_primary_) {
      button->style = ExportRichButtonStyle::Primary;
    } else if (style->bg_success_) {
      button->style = ExportRichButtonStyle::Success;
    } else {
      button->style = ExportRichButtonStyle::Default;
    }
  }
  result.type = ExportRichText::Type::Button;
  result.children.push_back(get_rich_text(text));
  result.button = std::move(button);
  return true;
}

template <class T>
ExportRichText get_wrapping_rich_text(ExportRichText::Type type, const T *text) {
  ExportRichText result;
  result.type = type;
  result.children.push_back(get_rich_text(text->text_.get()));
  return result;
}

ExportRichText get_rich_text(const telegram_api::RichText *text) {
  using Type = ExportRichText::Type;
  ExportRichText result;
  switch (text->get_id()) {
    case telegram_api::textEmpty::ID:
      break;
    case telegram_api::textPlain::ID:
      result.type = Type::Plain;
      result.text = static_cast<const telegram_api::textPlain *>(text)->text_;
      break;
    case telegram_api::textBold::ID:
      return get_wrapping_rich_text(Type::Bold, static_cast<const telegram_api::textBold *>(text));
    case telegram_api::textItalic::ID:
      return get_wrapping_rich_text(Type::Italic, static_cast<const telegram_api::textItalic *>(text));
    case telegram_api::textUnderline::ID:
      return get_wrapping_rich_text(Type::Underline, static_cast<const telegram_api::textUnderline *>(text));
    case telegram_api::textStrike::ID:
      return get_wrapping_rich_text(Type::Strike, static_cast<const telegram_api::textStrike *>(text));
    case telegram_api::textFixed::ID:
      return get_wrapping_rich_text(Type::Fixed, static_cast<const telegram_api::textFixed *>(text));
    case telegram_api::textUrl::ID: {
      const auto *url = static_cast<const telegram_api::textUrl *>(text);
      result = get_wrapping_rich_text(Type::Url, url);
      result.data = url->url_;
      result.id = url->webpage_id_;
      break;
    }
    case telegram_api::textEmail::ID: {
      const auto *email = static_cast<const telegram_api::textEmail *>(text);
      result = get_wrapping_rich_text(Type::Email, email);
      result.data = email->email_;
      break;
    }
    case telegram_api::textConcat::ID:
      result.type = Type::Concat;
      for (const auto &child : static_cast<const telegram_api::textConcat *>(text)->texts_) {
        result.children.push_back(get_rich_text(child.get()));
      }
      break;
    case telegram_api::textSubscript::ID:
      return get_wrapping_rich_text(Type::Subscript, static_cast<const telegram_api::textSubscript *>(text));
    case telegram_api::textSuperscript::ID:
      return get_wrapping_rich_text(Type::Superscript, static_cast<const telegram_api::textSuperscript *>(text));
    case telegram_api::textMarked::ID:
      return get_wrapping_rich_text(Type::Marked, static_cast<const telegram_api::textMarked *>(text));
    case telegram_api::textPhone::ID: {
      const auto *phone = static_cast<const telegram_api::textPhone *>(text);
      result = get_wrapping_rich_text(Type::Phone, phone);
      result.data = phone->phone_;
      break;
    }
    case telegram_api::textImage::ID: {
      const auto *image = static_cast<const telegram_api::textImage *>(text);
      result.type = Type::InlineImage;
      result.id = image->document_id_;
      result.width = image->w_;
      result.height = image->h_;
      break;
    }
    case telegram_api::textAnchor::ID: {
      const auto *anchor = static_cast<const telegram_api::textAnchor *>(text);
      result = get_wrapping_rich_text(Type::Anchor, anchor);
      result.data = anchor->name_;
      break;
    }
    case telegram_api::textMath::ID:
      result.type = Type::Math;
      result.data = static_cast<const telegram_api::textMath *>(text)->source_;
      break;
    case telegram_api::textCustomEmoji::ID: {
      const auto *custom_emoji = static_cast<const telegram_api::textCustomEmoji *>(text);
      result.type = Type::CustomEmoji;
      result.text = custom_emoji->alt_;
      result.id = custom_emoji->document_id_;
      break;
    }
    case telegram_api::textSpoiler::ID:
      return get_wrapping_rich_text(Type::Spoiler, static_cast<const telegram_api::textSpoiler *>(text));
    case telegram_api::textMention::ID:
      return get_wrapping_rich_text(Type::Mention, static_cast<const telegram_api::textMention *>(text));
    case telegram_api::textHashtag::ID:
      return get_wrapping_rich_text(Type::Hashtag, static_cast<const telegram_api::textHashtag *>(text));
    case telegram_api::textBotCommand::ID:
      return get_wrapping_rich_text(Type::BotCommand, static_cast<const telegram_api::textBotCommand *>(text));
    case telegram_api::textCashtag::ID:
      return get_wrapping_rich_text(Type::Cashtag, static_cast<const telegram_api::textCashtag *>(text));
    case telegram_api::textAutoUrl::ID:
      return get_wrapping_rich_text(Type::AutoUrl, static_cast<const telegram_api::textAutoUrl *>(text));
    case telegram_api::textAutoEmail::ID:
      return get_wrapping_rich_text(Type::AutoEmail, static_cast<const telegram_api::textAutoEmail *>(text));
    case telegram_api::textAutoPhone::ID:
      return get_wrapping_rich_text(Type::AutoPhone, static_cast<const telegram_api::textAutoPhone *>(text));
    case telegram_api::textBankCard::ID:
      return get_wrapping_rich_text(Type::BankCard, static_cast<const telegram_api::textBankCard *>(text));
    case telegram_api::textTonAddress::ID:
      return get_wrapping_rich_text(Type::TonAddress, static_cast<const telegram_api::textTonAddress *>(text));
    case telegram_api::textMentionName::ID: {
      const auto *mention = static_cast<const telegram_api::textMentionName *>(text);
      result = get_wrapping_rich_text(Type::MentionName, mention);
      result.id = mention->user_id_;
      break;
    }
    case telegram_api::textDate::ID: {
      const auto *date = static_cast<const telegram_api::textDate *>(text);
      result = get_wrapping_rich_text(Type::FormattedDate, date);
      result.date = date->date_;
      result.relative = date->relative_;
      result.short_time = date->short_time_;
      result.long_time = date->long_time_;
      result.short_date = date->short_date_;
      result.long_date = date->long_date_;
      result.day_of_week = date->day_of_week_;
      break;
    }
    case telegram_api::textDiff::ID: {
      const auto *diff = static_cast<const telegram_api::textDiff *>(text);
      result.type = Type::Diff;
      result.children.push_back(get_rich_text(diff->text_.get()));
      result.old_children.push_back(get_rich_text(diff->old_text_.get()));
      break;
    }
    case telegram_api::textButton::ID: {
      const auto *button = static_cast<const telegram_api::textButton *>(text);
      if (!get_rich_button(button->text_.get(), button->type_.get(), button->style_.get(), true, result)) {
        return get_rich_text(button->text_.get());
      }
      break;
    }
    default:
      UNREACHABLE();
  }
  return result;
}

ExportRichCaption get_rich_caption(const telegram_api::pageCaption *caption) {
  ExportRichCaption result;
  result.text = get_rich_text(caption->text_.get());
  result.credit = get_rich_text(caption->credit_.get());
  return result;
}

ExportRichTaskState get_task_state(bool has_checkbox, bool is_checked) {
  if (!has_checkbox) {
    return ExportRichTaskState::None;
  }
  return is_checked ? ExportRichTaskState::Checked : ExportRichTaskState::Unchecked;
}

vector<ExportRichBlock> get_rich_blocks(const vector<telegram_api::object_ptr<telegram_api::PageBlock>> &blocks);

ExportRichListItem get_rich_list_item(const telegram_api::PageListItem *item) {
  ExportRichListItem result;
  switch (item->get_id()) {
    case telegram_api::pageListItemText::ID: {
      const auto *text_item = static_cast<const telegram_api::pageListItemText *>(item);
      result.content = ExportRichContent::Text;
      result.task_state = get_task_state(text_item->checkbox_, text_item->checked_);
      result.text = get_rich_text(text_item->text_.get());
      break;
    }
    case telegram_api::pageListItemBlocks::ID: {
      const auto *blocks_item = static_cast<const telegram_api::pageListItemBlocks *>(item);
      result.content = ExportRichContent::Blocks;
      result.task_state = get_task_state(blocks_item->checkbox_, blocks_item->checked_);
      result.blocks = get_rich_blocks(blocks_item->blocks_);
      break;
    }
    default:
      UNREACHABLE();
  }
  return result;
}

// the server doesn't send empty optional strings, so an empty string is taken for an absent one
template <class T>
void fill_ordered_list_item(ExportRichListItem &result, const T *item) {
  result.task_state = get_task_state(item->checkbox_, item->checked_);
  result.has_num = !item->num_.empty();
  result.num = item->num_;
  result.has_value = (item->flags_ & T::VALUE_MASK) != 0;
  result.value = item->value_;
  result.has_type = !item->type_.empty();
  result.type = item->type_;
}

ExportRichListItem get_rich_ordered_list_item(const telegram_api::PageListOrderedItem *item) {
  ExportRichListItem result;
  switch (item->get_id()) {
    case telegram_api::pageListOrderedItemText::ID: {
      const auto *text_item = static_cast<const telegram_api::pageListOrderedItemText *>(item);
      result.content = ExportRichContent::Text;
      fill_ordered_list_item(result, text_item);
      result.text = get_rich_text(text_item->text_.get());
      break;
    }
    case telegram_api::pageListOrderedItemBlocks::ID: {
      const auto *blocks_item = static_cast<const telegram_api::pageListOrderedItemBlocks *>(item);
      result.content = ExportRichContent::Blocks;
      fill_ordered_list_item(result, blocks_item);
      result.blocks = get_rich_blocks(blocks_item->blocks_);
      break;
    }
    default:
      UNREACHABLE();
  }
  return result;
}

ExportRichTableCell get_rich_table_cell(const telegram_api::pageTableCell *cell) {
  ExportRichTableCell result;
  result.header = cell->header_;
  if (cell->align_right_) {
    result.alignment = ExportRichTableAlignment::Right;
  } else if (cell->align_center_) {
    result.alignment = ExportRichTableAlignment::Center;
  }
  if (cell->valign_bottom_) {
    result.vertical_alignment = ExportRichTableVerticalAlignment::Bottom;
  } else if (cell->valign_middle_) {
    result.vertical_alignment = ExportRichTableVerticalAlignment::Middle;
  }
  if (cell->text_ != nullptr) {
    result.has_text = true;
    result.text = get_rich_text(cell->text_.get());
  }
  result.has_colspan = (cell->flags_ & telegram_api::pageTableCell::COLSPAN_MASK) != 0;
  result.colspan = cell->colspan_;
  result.has_rowspan = (cell->flags_ & telegram_api::pageTableCell::ROWSPAN_MASK) != 0;
  result.rowspan = cell->rowspan_;
  return result;
}

ExportRichRelatedArticle get_rich_related_article(const telegram_api::pageRelatedArticle *article) {
  ExportRichRelatedArticle result;
  result.url = article->url_;
  result.webpage_id = article->webpage_id_;
  result.has_title = !article->title_.empty();
  result.title = article->title_;
  result.has_description = !article->description_.empty();
  result.description = article->description_;
  result.has_photo_id = (article->flags_ & telegram_api::pageRelatedArticle::PHOTO_ID_MASK) != 0;
  result.photo_id = article->photo_id_;
  result.has_author = !article->author_.empty();
  result.author = article->author_;
  result.has_published_date = (article->flags_ & telegram_api::pageRelatedArticle::PUBLISHED_DATE_MASK) != 0;
  result.published_date = article->published_date_;
  return result;
}

ExportRichChannel get_rich_channel(const telegram_api::Chat *chat) {
  using Source = ExportRichChannel::Source;
  ExportRichChannel result;
  switch (chat->get_id()) {
    case telegram_api::chatEmpty::ID:
      result.source = Source::ChatEmpty;
      result.id = static_cast<const telegram_api::chatEmpty *>(chat)->id_;
      break;
    case telegram_api::chat::ID: {
      const auto *basic_group = static_cast<const telegram_api::chat *>(chat);
      result.source = Source::Chat;
      result.id = basic_group->id_;
      result.has_title = true;
      result.title = basic_group->title_;
      break;
    }
    case telegram_api::chatForbidden::ID: {
      const auto *basic_group = static_cast<const telegram_api::chatForbidden *>(chat);
      result.source = Source::ChatForbidden;
      result.id = basic_group->id_;
      result.has_title = true;
      result.title = basic_group->title_;
      break;
    }
    case telegram_api::channel::ID: {
      const auto *channel = static_cast<const telegram_api::channel *>(chat);
      result.source = Source::Channel;
      result.id = channel->id_;
      result.has_title = true;
      result.title = channel->title_;
      result.broadcast = channel->broadcast_;
      result.megagroup = channel->megagroup_;
      result.monoforum = channel->monoforum_;
      result.has_access_hash = (channel->flags_ & telegram_api::channel::ACCESS_HASH_MASK) != 0;
      result.access_hash = channel->access_hash_;
      result.has_username = !channel->username_.empty();
      result.username = channel->username_;
      break;
    }
    case telegram_api::channelForbidden::ID: {
      const auto *channel = static_cast<const telegram_api::channelForbidden *>(chat);
      result.source = Source::ChannelForbidden;
      result.id = channel->id_;
      result.has_title = true;
      result.title = channel->title_;
      result.has_access_hash = true;
      result.access_hash = channel->access_hash_;
      result.broadcast = channel->broadcast_;
      result.megagroup = channel->megagroup_;
      result.monoforum = channel->monoforum_;
      break;
    }
    case telegram_api::community::ID: {
      const auto *community = static_cast<const telegram_api::community *>(chat);
      result.source = Source::Community;
      result.id = community->id_;
      result.has_title = true;
      result.title = community->title_;
      result.has_access_hash = (community->flags_ & telegram_api::community::ACCESS_HASH_MASK) != 0;
      result.access_hash = community->access_hash_;
      break;
    }
    case telegram_api::communityForbidden::ID: {
      const auto *community = static_cast<const telegram_api::communityForbidden *>(chat);
      result.source = Source::CommunityForbidden;
      result.id = community->id_;
      result.has_title = true;
      result.title = community->title_;
      result.has_access_hash = (community->flags_ & telegram_api::communityForbidden::ACCESS_HASH_MASK) != 0;
      result.access_hash = community->access_hash_;
      break;
    }
    default:
      UNREACHABLE();
  }
  return result;
}

ExportRichMapPoint get_rich_map_point(const telegram_api::GeoPoint *point) {
  ExportRichMapPoint result;
  if (point->get_id() == telegram_api::geoPointEmpty::ID) {
    result.source = ExportRichMapPoint::Source::GeoPointEmpty;
    return result;
  }
  CHECK(point->get_id() == telegram_api::geoPoint::ID);
  const auto *geo_point = static_cast<const telegram_api::geoPoint *>(point);
  result.source = ExportRichMapPoint::Source::GeoPoint;
  result.latitude = geo_point->lat_;
  result.longitude = geo_point->long_;
  result.has_access_hash = true;
  result.access_hash = geo_point->access_hash_;
  result.has_accuracy_radius = (geo_point->flags_ & telegram_api::geoPoint::ACCURACY_RADIUS_MASK) != 0;
  result.accuracy_radius = geo_point->accuracy_radius_;
  return result;
}

ExportRichMapPoint get_rich_map_point(const telegram_api::InputGeoPoint *point) {
  ExportRichMapPoint result;
  if (point->get_id() == telegram_api::inputGeoPointEmpty::ID) {
    result.source = ExportRichMapPoint::Source::InputGeoPointEmpty;
    return result;
  }
  CHECK(point->get_id() == telegram_api::inputGeoPoint::ID);
  const auto *geo_point = static_cast<const telegram_api::inputGeoPoint *>(point);
  result.source = ExportRichMapPoint::Source::InputGeoPoint;
  result.latitude = geo_point->lat_;
  result.longitude = geo_point->long_;
  result.has_accuracy_radius = (geo_point->flags_ & telegram_api::inputGeoPoint::ACCURACY_RADIUS_MASK) != 0;
  result.accuracy_radius = geo_point->accuracy_radius_;
  return result;
}

ExportRichBlock get_kind_block(ExportRichBlock::Kind kind) {
  ExportRichBlock result;
  result.kind = kind;
  return result;
}

ExportRichBlock get_text_block(ExportRichBlock::Kind kind, const telegram_api::RichText *text) {
  auto result = get_kind_block(kind);
  result.text = get_rich_text(text);
  return result;
}

ExportRichBlock get_heading_block(int32 level, const telegram_api::RichText *text) {
  auto result = get_text_block(ExportRichBlock::Kind::Heading, text);
  result.heading_level = level;
  return result;
}

ExportRichBlock get_map_block(ExportRichBlock::Kind kind, ExportRichMapPoint &&map_point, int32 zoom, int32 width,
                              int32 height, const telegram_api::pageCaption *caption) {
  auto result = get_kind_block(kind);
  result.map_point = std::move(map_point);
  result.zoom = zoom;
  result.map_width = width;
  result.map_height = height;
  result.caption = get_rich_caption(caption);
  return result;
}

ExportRichBlock get_rich_block(const telegram_api::PageBlock *block) {
  using Kind = ExportRichBlock::Kind;
  switch (block->get_id()) {
    case telegram_api::pageBlockUnsupported::ID:
      return get_kind_block(Kind::Unsupported);
    case telegram_api::pageBlockTitle::ID:
      return get_heading_block(1, static_cast<const telegram_api::pageBlockTitle *>(block)->text_.get());
    case telegram_api::pageBlockSubtitle::ID:
      return get_heading_block(2, static_cast<const telegram_api::pageBlockSubtitle *>(block)->text_.get());
    case telegram_api::pageBlockAuthorDate::ID: {
      const auto *author_date = static_cast<const telegram_api::pageBlockAuthorDate *>(block);
      auto result = get_text_block(Kind::AuthorDate, author_date->author_.get());
      result.date = author_date->published_date_;
      return result;
    }
    case telegram_api::pageBlockHeader::ID:
      return get_heading_block(3, static_cast<const telegram_api::pageBlockHeader *>(block)->text_.get());
    case telegram_api::pageBlockSubheader::ID:
      return get_heading_block(4, static_cast<const telegram_api::pageBlockSubheader *>(block)->text_.get());
    case telegram_api::pageBlockKicker::ID:
      return get_heading_block(5, static_cast<const telegram_api::pageBlockKicker *>(block)->text_.get());
    case telegram_api::pageBlockHeading1::ID:
      return get_heading_block(1, static_cast<const telegram_api::pageBlockHeading1 *>(block)->text_.get());
    case telegram_api::pageBlockHeading2::ID:
      return get_heading_block(2, static_cast<const telegram_api::pageBlockHeading2 *>(block)->text_.get());
    case telegram_api::pageBlockHeading3::ID:
      return get_heading_block(3, static_cast<const telegram_api::pageBlockHeading3 *>(block)->text_.get());
    case telegram_api::pageBlockHeading4::ID:
      return get_heading_block(4, static_cast<const telegram_api::pageBlockHeading4 *>(block)->text_.get());
    case telegram_api::pageBlockHeading5::ID:
      return get_heading_block(5, static_cast<const telegram_api::pageBlockHeading5 *>(block)->text_.get());
    case telegram_api::pageBlockHeading6::ID:
      return get_heading_block(6, static_cast<const telegram_api::pageBlockHeading6 *>(block)->text_.get());
    case telegram_api::pageBlockParagraph::ID:
      return get_text_block(Kind::Paragraph, static_cast<const telegram_api::pageBlockParagraph *>(block)->text_.get());
    case telegram_api::pageBlockPreformatted::ID: {
      const auto *preformatted = static_cast<const telegram_api::pageBlockPreformatted *>(block);
      auto result = get_text_block(Kind::Code, preformatted->text_.get());
      result.language = preformatted->language_;
      return result;
    }
    case telegram_api::pageBlockFooter::ID:
      return get_text_block(Kind::Footer, static_cast<const telegram_api::pageBlockFooter *>(block)->text_.get());
    case telegram_api::pageBlockThinking::ID:
      return get_text_block(Kind::Thinking, static_cast<const telegram_api::pageBlockThinking *>(block)->text_.get());
    case telegram_api::pageBlockDivider::ID:
      return get_kind_block(Kind::Divider);
    case telegram_api::pageBlockAnchor::ID: {
      auto result = get_kind_block(Kind::Anchor);
      result.name = static_cast<const telegram_api::pageBlockAnchor *>(block)->name_;
      return result;
    }
    case telegram_api::pageBlockMath::ID: {
      auto result = get_kind_block(Kind::Math);
      result.formula = static_cast<const telegram_api::pageBlockMath *>(block)->source_;
      return result;
    }
    case telegram_api::pageBlockList::ID: {
      auto result = get_kind_block(Kind::List);
      result.list_kind = ExportRichListKind::Bullet;
      for (const auto &item : static_cast<const telegram_api::pageBlockList *>(block)->items_) {
        result.list_items.push_back(get_rich_list_item(item.get()));
      }
      return result;
    }
    case telegram_api::pageBlockOrderedList::ID: {
      const auto *list = static_cast<const telegram_api::pageBlockOrderedList *>(block);
      auto result = get_kind_block(Kind::List);
      result.list_kind = ExportRichListKind::Ordered;
      result.reversed = list->reversed_;
      result.has_start = (list->flags_ & telegram_api::pageBlockOrderedList::START_MASK) != 0;
      result.start = list->start_;
      result.has_list_type = !list->type_.empty();
      result.list_type = list->type_;
      for (const auto &item : list->items_) {
        result.list_items.push_back(get_rich_ordered_list_item(item.get()));
      }
      return result;
    }
    case telegram_api::pageBlockBlockquote::ID: {
      const auto *quote = static_cast<const telegram_api::pageBlockBlockquote *>(block);
      auto result = get_text_block(Kind::Quote, quote->text_.get());
      result.quote_content = ExportRichContent::Text;
      result.quote_caption = get_rich_text(quote->caption_.get());
      return result;
    }
    case telegram_api::pageBlockBlockquoteBlocks::ID: {
      const auto *quote = static_cast<const telegram_api::pageBlockBlockquoteBlocks *>(block);
      auto result = get_kind_block(Kind::Quote);
      result.quote_content = ExportRichContent::Blocks;
      result.blocks = get_rich_blocks(quote->blocks_);
      result.quote_caption = get_rich_text(quote->caption_.get());
      return result;
    }
    case telegram_api::pageBlockPullquote::ID: {
      const auto *quote = static_cast<const telegram_api::pageBlockPullquote *>(block);
      auto result = get_text_block(Kind::Quote, quote->text_.get());
      result.quote_content = ExportRichContent::Text;
      result.quote_caption = get_rich_text(quote->caption_.get());
      result.pullquote = true;
      return result;
    }
    case telegram_api::pageBlockPhoto::ID: {
      const auto *photo = static_cast<const telegram_api::pageBlockPhoto *>(block);
      auto result = get_kind_block(Kind::Photo);
      result.photo_id = photo->photo_id_;
      result.caption = get_rich_caption(photo->caption_.get());
      result.spoiler = photo->spoiler_;
      result.has_optional_url = (photo->flags_ & telegram_api::pageBlockPhoto::WEBPAGE_ID_MASK) != 0;
      result.optional_url = photo->url_;
      result.has_optional_webpage_id = result.has_optional_url;
      result.optional_webpage_id = photo->webpage_id_;
      return result;
    }
    case telegram_api::pageBlockVideo::ID: {
      const auto *video = static_cast<const telegram_api::pageBlockVideo *>(block);
      auto result = get_kind_block(Kind::Video);
      result.document_id = video->video_id_;
      result.caption = get_rich_caption(video->caption_.get());
      result.autoplay = video->autoplay_;
      result.loop = video->loop_;
      result.spoiler = video->spoiler_;
      return result;
    }
    case telegram_api::pageBlockAudio::ID: {
      const auto *audio = static_cast<const telegram_api::pageBlockAudio *>(block);
      auto result = get_kind_block(Kind::Audio);
      result.document_id = audio->audio_id_;
      result.caption = get_rich_caption(audio->caption_.get());
      return result;
    }
    case telegram_api::pageBlockDocument::ID: {
      const auto *document = static_cast<const telegram_api::pageBlockDocument *>(block);
      auto result = get_kind_block(Kind::File);
      result.document_id = document->document_id_;
      result.caption = get_rich_caption(document->caption_.get());
      return result;
    }
    case telegram_api::pageBlockCover::ID: {
      auto result = get_kind_block(Kind::Cover);
      result.blocks.push_back(get_rich_block(static_cast<const telegram_api::pageBlockCover *>(block)->cover_.get()));
      return result;
    }
    case telegram_api::pageBlockEmbed::ID: {
      const auto *embed = static_cast<const telegram_api::pageBlockEmbed *>(block);
      auto result = get_kind_block(Kind::Embed);
      result.full_width = embed->full_width_;
      result.allow_scrolling = embed->allow_scrolling_;
      result.caption = get_rich_caption(embed->caption_.get());
      result.has_optional_url = !embed->url_.empty();
      result.optional_url = embed->url_;
      result.has_html = !embed->html_.empty();
      result.html = embed->html_;
      result.has_poster_photo_id = (embed->flags_ & telegram_api::pageBlockEmbed::POSTER_PHOTO_ID_MASK) != 0;
      result.poster_photo_id = embed->poster_photo_id_;
      result.has_size = (embed->flags_ & telegram_api::pageBlockEmbed::W_MASK) != 0;
      result.width = embed->w_;
      result.height = embed->h_;
      return result;
    }
    case telegram_api::pageBlockEmbedPost::ID: {
      const auto *post = static_cast<const telegram_api::pageBlockEmbedPost *>(block);
      auto result = get_kind_block(Kind::EmbedPost);
      result.url = post->url_;
      result.webpage_id = post->webpage_id_;
      result.author_photo_id = post->author_photo_id_;
      result.author = post->author_;
      result.date = post->date_;
      result.blocks = get_rich_blocks(post->blocks_);
      result.caption = get_rich_caption(post->caption_.get());
      return result;
    }
    case telegram_api::pageBlockCollage::ID: {
      const auto *collage = static_cast<const telegram_api::pageBlockCollage *>(block);
      auto result = get_kind_block(Kind::Collage);
      result.blocks = get_rich_blocks(collage->items_);
      result.caption = get_rich_caption(collage->caption_.get());
      return result;
    }
    case telegram_api::pageBlockSlideshow::ID: {
      const auto *slideshow = static_cast<const telegram_api::pageBlockSlideshow *>(block);
      auto result = get_kind_block(Kind::Slideshow);
      result.blocks = get_rich_blocks(slideshow->items_);
      result.caption = get_rich_caption(slideshow->caption_.get());
      return result;
    }
    case telegram_api::pageBlockChannel::ID: {
      auto result = get_kind_block(Kind::Channel);
      result.channel = get_rich_channel(static_cast<const telegram_api::pageBlockChannel *>(block)->channel_.get());
      return result;
    }
    case telegram_api::pageBlockTable::ID: {
      const auto *table = static_cast<const telegram_api::pageBlockTable *>(block);
      auto result = get_text_block(Kind::Table, table->title_.get());
      result.bordered = table->bordered_;
      result.striped = table->striped_;
      result.compact = table->compact_;
      for (const auto &row : table->rows_) {
        ExportRichTableRow table_row;
        for (const auto &cell : row->cells_) {
          table_row.cells.push_back(get_rich_table_cell(cell.get()));
        }
        result.table_rows.push_back(std::move(table_row));
      }
      return result;
    }
    case telegram_api::pageBlockDetails::ID: {
      const auto *details = static_cast<const telegram_api::pageBlockDetails *>(block);
      auto result = get_text_block(Kind::Details, details->title_.get());
      result.open = details->open_;
      result.blocks = get_rich_blocks(details->blocks_);
      return result;
    }
    case telegram_api::pageBlockRelatedArticles::ID: {
      const auto *related_articles = static_cast<const telegram_api::pageBlockRelatedArticles *>(block);
      auto result = get_text_block(Kind::RelatedArticles, related_articles->title_.get());
      for (const auto &article : related_articles->articles_) {
        result.related_articles.push_back(get_rich_related_article(article.get()));
      }
      return result;
    }
    case telegram_api::pageBlockMap::ID: {
      const auto *map = static_cast<const telegram_api::pageBlockMap *>(block);
      return get_map_block(Kind::Map, get_rich_map_point(map->geo_.get()), map->zoom_, map->w_, map->h_,
                           map->caption_.get());
    }
    case telegram_api::inputPageBlockMap::ID: {
      const auto *map = static_cast<const telegram_api::inputPageBlockMap *>(block);
      return get_map_block(Kind::InputMap, get_rich_map_point(map->geo_.get()), map->zoom_, map->w_, map->h_,
                           map->caption_.get());
    }
    case telegram_api::pageBlockButtonRow::ID: {
      const auto *button_row = static_cast<const telegram_api::pageBlockButtonRow *>(block);
      auto result = get_kind_block(Kind::ButtonRow);
      if (button_row->align_left_) {
        result.button_alignment = ExportRichButtonAlignment::Left;
      } else if (button_row->align_center_) {
        result.button_alignment = ExportRichButtonAlignment::Center;
      } else if (button_row->align_right_) {
        result.button_alignment = ExportRichButtonAlignment::Right;
      }
      for (const auto &button : button_row->buttons_) {
        ExportRichText text;
        if (get_rich_button(button->text_.get(), button->type_.get(), button->style_.get(), false, text)) {
          result.buttons.push_back(std::move(text));
        }
      }
      return result;
    }
    default:
      UNREACHABLE();
      return get_kind_block(Kind::Unsupported);
  }
}

vector<ExportRichBlock> get_rich_blocks(const vector<telegram_api::object_ptr<telegram_api::PageBlock>> &blocks) {
  vector<ExportRichBlock> result;
  result.reserve(blocks.size());
  for (const auto &block : blocks) {
    result.push_back(get_rich_block(block.get()));
  }
  return result;
}

}  // namespace

unique_ptr<ExportRichMessage> get_export_rich_message(const telegram_api::richMessage &rich_message) {
  auto result = make_unique<ExportRichMessage>();
  result->blocks = get_rich_blocks(rich_message.blocks_);
  result->rtl = rich_message.rtl_;
  result->part = rich_message.part_;
  return result;
}

}  // namespace td
