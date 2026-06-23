//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/DraftMessage.h"

#include "td/telegram/Dependencies.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessageContentDupType.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/MessageSelfDestructType.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/SuggestedPost.h"
#include "td/telegram/Td.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/logging.h"
#include "td/utils/tl_helpers.h"

namespace td {

class DraftMessageContentVideoNote final : public DraftMessageContent {
 public:
  string path_;
  int32 duration_ = 0;
  int32 length_ = 0;
  MessageSelfDestructType ttl_;

  DraftMessageContentVideoNote() = default;

  DraftMessageContentVideoNote(string &&path, int32 duration, int32 length, MessageSelfDestructType ttl)
      : path_(std::move(path)), duration_(duration), length_(length), ttl_(ttl) {
  }

  DraftMessageContentType get_type() const final {
    return DraftMessageContentType::VideoNote;
  }

  td_api::object_ptr<td_api::DraftMessageContent> get_draft_message_content_object() const final {
    return td_api::make_object<td_api::draftMessageContentVideoNote>(path_, duration_, length_,
                                                                     ttl_.get_message_self_destruct_type_object());
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    bool has_path = !path_.empty();
    bool has_duration = duration_ != 0;
    bool has_length = length_ != 0;
    bool has_ttl = ttl_.is_valid();
    BEGIN_STORE_FLAGS();
    STORE_FLAG(has_path);
    STORE_FLAG(has_duration);
    STORE_FLAG(has_length);
    STORE_FLAG(has_ttl);
    END_STORE_FLAGS();
    if (has_path) {
      td::store(path_, storer);
    }
    if (has_duration) {
      td::store(duration_, storer);
    }
    if (has_length) {
      td::store(length_, storer);
    }
    if (has_ttl) {
      td::store(ttl_, storer);
    }
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    bool has_path;
    bool has_duration;
    bool has_length;
    bool has_ttl;
    BEGIN_PARSE_FLAGS();
    PARSE_FLAG(has_path);
    PARSE_FLAG(has_duration);
    PARSE_FLAG(has_length);
    PARSE_FLAG(has_ttl);
    END_PARSE_FLAGS();
    if (has_path) {
      td::parse(path_, parser);
    }
    if (has_duration) {
      td::parse(duration_, parser);
    }
    if (has_length) {
      td::parse(length_, parser);
    }
    if (has_ttl) {
      td::parse(ttl_, parser);
    }
  }
};

class DraftMessageContentVoiceNote final : public DraftMessageContent {
 public:
  string path_;
  int32 duration_ = 0;
  string waveform_;
  MessageSelfDestructType ttl_;

  DraftMessageContentVoiceNote() = default;

  DraftMessageContentVoiceNote(string &&path, int32 duration, string &&waveform, MessageSelfDestructType ttl)
      : path_(std::move(path)), duration_(duration), waveform_(std::move(waveform)), ttl_(ttl) {
  }

  DraftMessageContentType get_type() const final {
    return DraftMessageContentType::VoiceNote;
  }

  td_api::object_ptr<td_api::DraftMessageContent> get_draft_message_content_object() const final {
    return td_api::make_object<td_api::draftMessageContentVoiceNote>(path_, duration_, waveform_,
                                                                     ttl_.get_message_self_destruct_type_object());
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    bool has_path = !path_.empty();
    bool has_duration = duration_ != 0;
    bool has_waveform = !waveform_.empty();
    bool has_ttl = ttl_.is_valid();
    BEGIN_STORE_FLAGS();
    STORE_FLAG(has_path);
    STORE_FLAG(has_duration);
    STORE_FLAG(has_waveform);
    STORE_FLAG(has_ttl);
    END_STORE_FLAGS();
    if (has_path) {
      td::store(path_, storer);
    }
    if (has_duration) {
      td::store(duration_, storer);
    }
    if (has_waveform) {
      td::store(waveform_, storer);
    }
    if (has_ttl) {
      td::store(ttl_, storer);
    }
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    bool has_path;
    bool has_duration;
    bool has_waveform;
    bool has_ttl;
    BEGIN_PARSE_FLAGS();
    PARSE_FLAG(has_path);
    PARSE_FLAG(has_duration);
    PARSE_FLAG(has_waveform);
    PARSE_FLAG(has_ttl);
    END_PARSE_FLAGS();
    if (has_path) {
      td::parse(path_, parser);
    }
    if (has_duration) {
      td::parse(duration_, parser);
    }
    if (has_waveform) {
      td::parse(waveform_, parser);
    }
    if (has_ttl) {
      td::parse(ttl_, parser);
    }
  }
};

template <class StorerT>
static void store(const DraftMessageContent *content, StorerT &storer) {
  CHECK(content != nullptr);

  auto content_type = content->get_type();
  store(content_type, storer);

  switch (content_type) {
    case DraftMessageContentType::VideoNote: {
      const auto *video_note = static_cast<const DraftMessageContentVideoNote *>(content);
      video_note->store(storer);
      break;
    }
    case DraftMessageContentType::VoiceNote: {
      const auto *voice_note = static_cast<const DraftMessageContentVoiceNote *>(content);
      voice_note->store(storer);
      break;
    }
    default:
      UNREACHABLE();
  }
}

void store_draft_message_content(const DraftMessageContent *content, LogEventStorerCalcLength &storer) {
  store(content, storer);
}

void store_draft_message_content(const DraftMessageContent *content, LogEventStorerUnsafe &storer) {
  store(content, storer);
}

void parse_draft_message_content(unique_ptr<DraftMessageContent> &content, LogEventParser &parser) {
  DraftMessageContentType type;
  parse(type, parser);
  switch (type) {
    case DraftMessageContentType::VideoNote: {
      unique_ptr<DraftMessageContentVideoNote> video_note;
      parse(video_note, parser);
      content = std::move(video_note);
      break;
    }
    case DraftMessageContentType::VoiceNote: {
      unique_ptr<DraftMessageContentVoiceNote> voice_note;
      parse(voice_note, parser);
      content = std::move(voice_note);
      break;
    }
    default:
      parser.set_error("Wrong draft content type");
  }
}

DraftMessage::DraftMessage() = default;

DraftMessage::~DraftMessage() = default;

bool DraftMessage::need_clear_local(MessageContentType content_type) const {
  if (!is_local()) {
    return false;
  }
  switch (local_content_->get_type()) {
    case DraftMessageContentType::VideoNote:
      return content_type == MessageContentType::VideoNote;
    case DraftMessageContentType::VoiceNote:
      return content_type == MessageContentType::VoiceNote;
    default:
      UNREACHABLE();
      return false;
  }
}

bool DraftMessage::need_update_to(const DraftMessage &other, bool from_update) const {
  if (is_local()) {
    return !from_update || other.is_local();
  }
  if (message_input_reply_to_ == other.message_input_reply_to_ && input_message_text_ == other.input_message_text_ &&
      rich_message_ == other.rich_message_ && message_effect_id_ == other.message_effect_id_ &&
      suggested_post_ == other.suggested_post_) {
    return date_ < other.date_;
  } else {
    return !from_update || date_ <= other.date_;
  }
}

unique_ptr<DraftMessage> DraftMessage::clone(Td *td, const unique_ptr<DraftMessage> &draft_message,
                                             DialogId dialog_id) {
  if (draft_message == nullptr) {
    return nullptr;
  }
  auto result = make_unique<DraftMessage>();
  result->date_ = draft_message->date_;
  result->message_input_reply_to_ = draft_message->message_input_reply_to_.clone();
  result->input_message_text_ = draft_message->input_message_text_;
  result->rich_message_ = draft_message->rich_message_.clone(td, dialog_id, MessageContentDupType::ServerCopy);
  if (draft_message->local_content_ != nullptr) {
    switch (draft_message->local_content_->get_type()) {
      case DraftMessageContentType::VideoNote: {
        auto *content = static_cast<const DraftMessageContentVideoNote *>(draft_message->local_content_.get());
        result->local_content_ = td::make_unique<DraftMessageContentVideoNote>(
            string(content->path_), content->duration_, content->length_, content->ttl_);
        break;
      }
      case DraftMessageContentType::VoiceNote: {
        auto *content = static_cast<const DraftMessageContentVoiceNote *>(draft_message->local_content_.get());
        result->local_content_ = td::make_unique<DraftMessageContentVoiceNote>(
            string(content->path_), content->duration_, string(content->waveform_), content->ttl_);
        break;
      }
      default:
        UNREACHABLE();
    }
  }
  result->message_effect_id_ = draft_message->message_effect_id_;
  result->suggested_post_ = SuggestedPost::clone(draft_message->suggested_post_);
  return result;
}

void DraftMessage::add_dependencies(Dependencies &dependencies) const {
  message_input_reply_to_.add_dependencies(dependencies);
  input_message_text_.add_dependencies(dependencies);
  rich_message_.add_dependencies(dependencies);
}

td_api::object_ptr<td_api::draftMessage> DraftMessage::get_draft_message_object(Td *td) const {
  td_api::object_ptr<td_api::DraftMessageContent> content;
  if (local_content_ != nullptr) {
    content = local_content_->get_draft_message_content_object();
  } else if (is_rich_) {
    content =
        td_api::make_object<td_api::draftMessageContentRichMessage>(rich_message_.get_rich_message_object(td, true));
  } else {
    content = input_message_text_.get_draft_message_content_object(td->user_manager_.get());
  }
  auto suggested_post = suggested_post_ == nullptr ? nullptr : suggested_post_->get_input_suggested_post_info_object();
  return td_api::make_object<td_api::draftMessage>(message_input_reply_to_.get_input_message_reply_to_object(td), date_,
                                                   std::move(content), message_effect_id_.get(),
                                                   std::move(suggested_post));
}

DraftMessage::DraftMessage(Td *td, telegram_api::object_ptr<telegram_api::draftMessage> &&draft_message) {
  CHECK(draft_message != nullptr);
  date_ = draft_message->date_;
  message_input_reply_to_ = MessageInputReplyTo(td, std::move(draft_message->reply_to_));
  if (draft_message->rich_message_ != nullptr) {
    is_rich_ = true;
    rich_message_ = RichMessage(td, std::move(draft_message->rich_message_), DialogId());
  } else {
    auto draft_text = get_formatted_text(td->user_manager_.get(), std::move(draft_message->message_),
                                         std::move(draft_message->entities_), true, true, "DraftMessage");
    string web_page_url;
    bool force_small_media = false;
    bool force_large_media = false;
    if (draft_message->media_ != nullptr) {
      if (draft_message->media_->get_id() != telegram_api::inputMediaWebPage::ID) {
        LOG(ERROR) << "Receive draft message with " << to_string(draft_message->media_);
      } else {
        auto media = telegram_api::move_object_as<telegram_api::inputMediaWebPage>(draft_message->media_);
        web_page_url = std::move(media->url_);
        if (web_page_url.empty()) {
          LOG(ERROR) << "Have no URL in a draft with manual link preview";
        }
        force_small_media = media->force_small_media_;
        force_large_media = media->force_large_media_;
      }
    }
    input_message_text_ = InputMessageText(std::move(draft_text), std::move(web_page_url), draft_message->no_webpage_,
                                           force_small_media, force_large_media, draft_message->invert_media_, false);
  }
  message_effect_id_ = MessageEffectId(draft_message->effect_);
  suggested_post_ = SuggestedPost::get_suggested_post(std::move(draft_message->suggested_post_));
}

Result<unique_ptr<DraftMessage>> DraftMessage::get_draft_message(
    Td *td, DialogId dialog_id, const MessageTopic &message_topic,
    td_api::object_ptr<td_api::draftMessage> &&draft_message) {
  if (draft_message == nullptr) {
    return nullptr;
  }

  auto result = make_unique<DraftMessage>();
  result->message_input_reply_to_ = td->messages_manager_->create_message_input_reply_to(
      dialog_id, message_topic, std::move(draft_message->reply_to_), true);
  result->message_effect_id_ = MessageEffectId(draft_message->effect_id_);
  TRY_RESULT(suggested_post, SuggestedPost::get_suggested_post(td, std::move(draft_message->suggested_post_info_)));
  result->suggested_post_ = std::move(suggested_post);

  auto content = std::move(draft_message->content_);
  if (content != nullptr) {
    switch (content->get_id()) {
      case td_api::draftMessageContentText::ID: {
        auto text = td_api::move_object_as<td_api::draftMessageContentText>(content);
        TRY_RESULT(input_message_text,
                   process_input_message_text(td, dialog_id,
                                              td_api::make_object<td_api::inputMessageText>(
                                                  std::move(text->text_), std::move(text->link_preview_options_), true),
                                              false, true));
        result->input_message_text_ = std::move(input_message_text);
        break;
      }
      case td_api::draftMessageContentRichMessage::ID: {
        auto message = td_api::move_object_as<td_api::draftMessageContentRichMessage>(content);
        if (message->message_ == nullptr || true) {
          break;
        }
        TRY_RESULT(rich_message, RichMessage::get_rich_message(td, dialog_id, std::move(message->message_), false));
        result->rich_message_ = std::move(rich_message);
        result->is_rich_ = true;
        break;
      }
      case td_api::draftMessageContentVideoNote::ID: {
        auto video_note = td_api::move_object_as<td_api::draftMessageContentVideoNote>(content);
        TRY_RESULT(ttl,
                   MessageSelfDestructType::get_message_self_destruct_type(std::move(video_note->self_destruct_type_)));
        result->local_content_ = td::make_unique<DraftMessageContentVideoNote>(
            std::move(video_note->file_path_), video_note->duration_, video_note->length_, ttl);
        break;
      }
      case td_api::draftMessageContentVoiceNote::ID: {
        auto voice_note = td_api::move_object_as<td_api::draftMessageContentVoiceNote>(content);
        TRY_RESULT(ttl,
                   MessageSelfDestructType::get_message_self_destruct_type(std::move(voice_note->self_destruct_type_)));
        result->local_content_ = td::make_unique<DraftMessageContentVoiceNote>(
            std::move(voice_note->file_path_), voice_note->duration_, std::move(voice_note->waveform_), ttl);
        break;
      }
      default:
        UNREACHABLE();
        return nullptr;
    }
  }

  if (!result->message_input_reply_to_.is_valid() && result->input_message_text_.is_empty() && !result->is_rich_ &&
      result->local_content_ == nullptr) {
    return nullptr;
  }

  result->date_ = G()->unix_time();
  return std::move(result);
}

bool is_local_draft_message(const unique_ptr<DraftMessage> &draft_message) {
  if (draft_message == nullptr) {
    return false;
  }
  return draft_message->is_local();
}

bool need_update_draft_message(const unique_ptr<DraftMessage> &old_draft_message,
                               const unique_ptr<DraftMessage> &new_draft_message, bool from_update) {
  if (new_draft_message == nullptr) {
    return old_draft_message != nullptr;
  }
  if (old_draft_message == nullptr) {
    return true;
  }
  return old_draft_message->need_update_to(*new_draft_message, from_update);
}

void add_draft_message_dependencies(Dependencies &dependencies, const unique_ptr<DraftMessage> &draft_message) {
  if (draft_message == nullptr) {
    return;
  }
  draft_message->add_dependencies(dependencies);
}

td_api::object_ptr<td_api::draftMessage> get_draft_message_object(Td *td,
                                                                  const unique_ptr<DraftMessage> &draft_message) {
  if (draft_message == nullptr) {
    return nullptr;
  }
  return draft_message->get_draft_message_object(td);
}

unique_ptr<DraftMessage> get_draft_message(Td *td,
                                           telegram_api::object_ptr<telegram_api::DraftMessage> &&draft_message_ptr) {
  if (draft_message_ptr == nullptr) {
    return nullptr;
  }
  switch (draft_message_ptr->get_id()) {
    case telegram_api::draftMessageEmpty::ID:
      return nullptr;
    case telegram_api::draftMessage::ID:
      return td::make_unique<DraftMessage>(td,
                                           telegram_api::move_object_as<telegram_api::draftMessage>(draft_message_ptr));
    default:
      UNREACHABLE();
      return nullptr;
  }
}

vector<InputDialogId> get_draft_message_reply_input_dialog_ids(
    const telegram_api::object_ptr<telegram_api::DraftMessage> &draft_message) {
  if (draft_message == nullptr || draft_message->get_id() != telegram_api::draftMessage::ID) {
    return {};
  }
  auto *input_reply_to = static_cast<const telegram_api::draftMessage *>(draft_message.get())->reply_to_.get();
  if (input_reply_to == nullptr) {
    return {};
  }
  switch (input_reply_to->get_id()) {
    case telegram_api::inputReplyToStory::ID: {
      auto reply_to = static_cast<const telegram_api::inputReplyToStory *>(input_reply_to);
      return {InputDialogId(reply_to->peer_)};
    }
    case telegram_api::inputReplyToMessage::ID: {
      auto reply_to = static_cast<const telegram_api::inputReplyToMessage *>(input_reply_to);
      vector<InputDialogId> result;
      if (reply_to->reply_to_peer_id_ != nullptr) {
        result.emplace_back(reply_to->reply_to_peer_id_);
      }
      if (reply_to->monoforum_peer_id_ != nullptr) {
        result.emplace_back(reply_to->monoforum_peer_id_);
      }
      return result;
    }
    case telegram_api::inputReplyToMonoForum::ID: {
      auto reply_to = static_cast<const telegram_api::inputReplyToMonoForum *>(input_reply_to);
      return {InputDialogId(reply_to->monoforum_peer_id_)};
    }
    case telegram_api::inputReplyToEphemeralMessage::ID:
      return {};
    default:
      UNREACHABLE();
  }
  return {};
}

}  // namespace td
