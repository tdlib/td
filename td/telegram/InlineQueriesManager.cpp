//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/InlineQueriesManager.h"

#include "td/telegram/td_api.h"
#include "td/telegram/td_api.hpp"
#include "td/telegram/telegram_api.h"
#include "td/telegram/telegram_api.hpp"

#include "td/telegram/AccessRights.h"
#include "td/telegram/AnimationsManager.h"
#include "td/telegram/AudiosManager.h"
#include "td/telegram/AuthManager.h"
#include "td/telegram/Contact.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/Document.h"
#include "td/telegram/DocumentsManager.h"
#include "td/telegram/files/FileManager.h"
#include "td/telegram/files/FileType.h"
#include "td/telegram/Game.h"
#include "td/telegram/Global.h"
#include "td/telegram/InputMessageText.h"
#include "td/telegram/Location.h"
#include "td/telegram/MessageContent.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/misc.h"
#include "td/telegram/Photo.h"
#include "td/telegram/ReplyMarkup.h"
#include "td/telegram/StickersManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/TdDb.h"
#include "td/telegram/Venue.h"
#include "td/telegram/VideosManager.h"
#include "td/telegram/VoiceNotesManager.h"

#include "td/telegram/net/DcId.h"

#include "td/utils/base64.h"
#include "td/utils/buffer.h"
#include "td/utils/HttpUrl.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Slice.h"
#include "td/utils/Time.h"
#include "td/utils/tl_helpers.h"
#include "td/utils/tl_parsers.h"

#include <algorithm>
#include <functional>

namespace td {

class GetInlineBotResultsQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  UserId bot_user_id_;
  uint64 query_hash_;

  static constexpr int32 GET_INLINE_BOT_RESULTS_FLAG_HAS_LOCATION = 1 << 0;

 public:
  explicit GetInlineBotResultsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  NetQueryRef send(UserId bot_user_id, tl_object_ptr<telegram_api::InputUser> bot_input_user, DialogId dialog_id,
                   Location user_location, const string &query, const string &offset, uint64 query_hash) {
    bot_user_id_ = bot_user_id;
    query_hash_ = query_hash;
    int32 flags = 0;
    if (!user_location.empty()) {
      flags |= GET_INLINE_BOT_RESULTS_FLAG_HAS_LOCATION;
    }

    auto input_peer = td->messages_manager_->get_input_peer(dialog_id, AccessRights::Read);
    if (input_peer == nullptr) {
      input_peer = make_tl_object<telegram_api::inputPeerEmpty>();
    }

    auto net_query = G()->net_query_creator().create(telegram_api::messages_getInlineBotResults(
        flags, std::move(bot_input_user), std::move(input_peer),
        user_location.empty() ? nullptr : user_location.get_input_geo_point(), query, offset));
    auto result = net_query.get_weak();
    net_query->need_resend_on_503_ = false;
    send_query(std::move(net_query));
    return result;
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_getInlineBotResults>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    td->inline_queries_manager_->on_get_inline_query_results(bot_user_id_, query_hash_, result_ptr.move_as_ok());
    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    if (status.code() == NetQuery::Cancelled) {
      status = Status::Error(406, "Request cancelled");
    } else if (status.message() == "BOT_RESPONSE_TIMEOUT") {
      status = Status::Error(502, "The bot is not responding");
    }
    LOG(INFO) << "Inline query returned error " << status;

    td->inline_queries_manager_->on_get_inline_query_results(bot_user_id_, query_hash_, nullptr);
    promise_.set_error(std::move(status));
  }
};

class SetInlineBotResultsQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit SetInlineBotResultsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(int64 inline_query_id, bool is_gallery, bool is_personal,
            vector<tl_object_ptr<telegram_api::InputBotInlineResult>> &&results, int32 cache_time,
            const string &next_offset, const string &switch_pm_text, const string &switch_pm_parameter) {
    int32 flags = 0;
    if (is_gallery) {
      flags |= telegram_api::messages_setInlineBotResults::GALLERY_MASK;
    }
    if (is_personal) {
      flags |= telegram_api::messages_setInlineBotResults::PRIVATE_MASK;
    }
    if (!next_offset.empty()) {
      flags |= telegram_api::messages_setInlineBotResults::NEXT_OFFSET_MASK;
    }
    tl_object_ptr<telegram_api::inlineBotSwitchPM> inline_bot_switch_pm;
    if (!switch_pm_text.empty()) {
      flags |= telegram_api::messages_setInlineBotResults::SWITCH_PM_MASK;
      inline_bot_switch_pm = make_tl_object<telegram_api::inlineBotSwitchPM>(switch_pm_text, switch_pm_parameter);
    }
    send_query(G()->net_query_creator().create(telegram_api::messages_setInlineBotResults(
        flags, false /*ignored*/, false /*ignored*/, inline_query_id, std::move(results), cache_time, next_offset,
        std::move(inline_bot_switch_pm))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_setInlineBotResults>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    bool result = result_ptr.ok();
    if (!result) {
      LOG(INFO) << "Sending answer to an inline query has failed";
    }
    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

InlineQueriesManager::InlineQueriesManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
  drop_inline_query_result_timeout_.set_callback(on_drop_inline_query_result_timeout_callback);
  drop_inline_query_result_timeout_.set_callback_data(static_cast<void *>(this));
}

void InlineQueriesManager::tear_down() {
  parent_.reset();
}

void InlineQueriesManager::on_drop_inline_query_result_timeout_callback(void *inline_queries_manager_ptr,
                                                                        int64 query_hash) {
  auto inline_queries_manager = static_cast<InlineQueriesManager *>(inline_queries_manager_ptr);
  auto it = inline_queries_manager->inline_query_results_.find(query_hash);
  CHECK(it != inline_queries_manager->inline_query_results_.end());
  CHECK(it->second.results != nullptr);
  CHECK(it->second.pending_request_count >= 0);
  if (it->second.pending_request_count == 0) {
    inline_queries_manager->inline_query_results_.erase(it);
  }
}

void InlineQueriesManager::after_get_difference() {
  if (td_->auth_manager_->is_bot()) {
    return;
  }
  if (recently_used_bots_loaded_ < 2) {
    Promise<Unit> promise;
    load_recently_used_bots(promise);
  }
}

tl_object_ptr<telegram_api::inputBotInlineMessageID> InlineQueriesManager::get_input_bot_inline_message_id(
    const string &inline_message_id) {
  auto r_binary = base64url_decode(inline_message_id);
  if (r_binary.is_error()) {
    return nullptr;
  }
  BufferSlice buffer_slice(r_binary.ok());
  TlBufferParser parser(&buffer_slice);
  auto result = telegram_api::inputBotInlineMessageID::fetch(parser);
  parser.fetch_end();
  if (parser.get_error()) {
    return nullptr;
  }
  if (!DcId::is_valid(result->dc_id_)) {
    return nullptr;
  }
  LOG(INFO) << "Have inline message id: " << to_string(result);
  return result;
}

string InlineQueriesManager::get_inline_message_id(
    tl_object_ptr<telegram_api::inputBotInlineMessageID> &&input_bot_inline_message_id) {
  if (input_bot_inline_message_id == nullptr) {
    return "";
  }
  LOG(INFO) << "Got inline message id: " << to_string(input_bot_inline_message_id);

  return base64url_encode(serialize(*input_bot_inline_message_id));
}

Result<tl_object_ptr<telegram_api::InputBotInlineMessage>> InlineQueriesManager::get_inline_message(
    tl_object_ptr<td_api::InputMessageContent> &&input_message_content,
    tl_object_ptr<td_api::ReplyMarkup> &&reply_markup_ptr, int32 allowed_media_content_id) const {
  if (input_message_content == nullptr) {
    return Status::Error(400, "Inline message can't be empty");
  }
  TRY_RESULT(reply_markup, get_reply_markup(std::move(reply_markup_ptr), true, true, false, true));
  auto input_reply_markup = get_input_reply_markup(reply_markup);
  int32 flags = 0;
  if (input_reply_markup != nullptr) {
    flags |= telegram_api::inputBotInlineMessageText::REPLY_MARKUP_MASK;
  }

  auto constructor_id = input_message_content->get_id();
  if (constructor_id == td_api::inputMessageText::ID) {
    TRY_RESULT(input_message_text, process_input_message_text(td_->contacts_manager_.get(), DialogId(),
                                                              std::move(input_message_content), true));

    if (input_message_text.disable_web_page_preview) {
      flags |= telegram_api::inputBotInlineMessageText::NO_WEBPAGE_MASK;
    }
    if (!input_message_text.text.entities.empty()) {
      flags |= telegram_api::inputBotInlineMessageText::ENTITIES_MASK;
    }
    return make_tl_object<telegram_api::inputBotInlineMessageText>(
        flags, false /*ignored*/, std::move(input_message_text.text.text),
        get_input_message_entities(td_->contacts_manager_.get(), input_message_text.text.entities,
                                   "get_inline_message"),
        std::move(input_reply_markup));
  }
  if (constructor_id == td_api::inputMessageContact::ID) {
    TRY_RESULT(contact, process_input_message_contact(std::move(input_message_content)));
    return contact.get_input_bot_inline_message_media_contact(flags, std::move(input_reply_markup));
  }
  if (constructor_id == td_api::inputMessageLocation::ID) {
    TRY_RESULT(location, process_input_message_location(std::move(input_message_content)));
    if (location.heading != 0) {
      flags |= telegram_api::inputBotInlineMessageMediaGeo::HEADING_MASK;
    }
    if (location.live_period != 0) {
      flags |= telegram_api::inputBotInlineMessageMediaGeo::PERIOD_MASK;
      flags |= telegram_api::inputBotInlineMessageMediaGeo::PROXIMITY_NOTIFICATION_RADIUS_MASK;
    }
    return make_tl_object<telegram_api::inputBotInlineMessageMediaGeo>(
        flags, location.location.get_input_geo_point(), location.heading, location.live_period,
        location.proximity_alert_radius, std::move(input_reply_markup));
  }
  if (constructor_id == td_api::inputMessageVenue::ID) {
    TRY_RESULT(venue, process_input_message_venue(std::move(input_message_content)));
    return venue.get_input_bot_inline_message_media_venue(flags, std::move(input_reply_markup));
  }
  if (constructor_id == allowed_media_content_id) {
    TRY_RESULT(caption, process_input_caption(td_->contacts_manager_.get(), DialogId(),
                                              extract_input_caption(input_message_content), true));
    auto entities = get_input_message_entities(td_->contacts_manager_.get(), caption.entities, "get_inline_message");
    if (!entities.empty()) {
      flags |= telegram_api::inputBotInlineMessageText::ENTITIES_MASK;
    }

    return make_tl_object<telegram_api::inputBotInlineMessageMediaAuto>(flags, caption.text, std::move(entities),
                                                                        std::move(input_reply_markup));
  }
  return Status::Error(400, "Unallowed inline message content type");
}

bool InlineQueriesManager::register_inline_message_content(
    int64 query_id, const string &result_id, FileId file_id,
    tl_object_ptr<telegram_api::BotInlineMessage> &&inline_message, int32 allowed_media_content_id, Photo *photo,
    Game *game) {
  InlineMessageContent content =
      create_inline_message_content(td_, file_id, std::move(inline_message), allowed_media_content_id, photo, game);
  if (content.message_content != nullptr) {
    inline_message_contents_[query_id].emplace(result_id, std::move(content));
    return true;
  }
  return false;
}

const InlineMessageContent *InlineQueriesManager::get_inline_message_content(int64 query_id, const string &result_id) {
  auto it = inline_message_contents_.find(query_id);
  if (it == inline_message_contents_.end()) {
    return nullptr;
  }

  auto result_it = it->second.find(result_id);
  if (result_it == it->second.end()) {
    return nullptr;
  }

  if (update_bot_usage(get_inline_bot_user_id(query_id))) {
    save_recently_used_bots();
  }
  return &result_it->second;
}

UserId InlineQueriesManager::get_inline_bot_user_id(int64 query_id) const {
  auto it = query_id_to_bot_user_id_.find(query_id);
  if (it == query_id_to_bot_user_id_.end()) {
    return UserId();
  }
  return it->second;
}

void InlineQueriesManager::answer_inline_query(int64 inline_query_id, bool is_personal,
                                               vector<tl_object_ptr<td_api::InputInlineQueryResult>> &&input_results,
                                               int32 cache_time, const string &next_offset,
                                               const string &switch_pm_text, const string &switch_pm_parameter,
                                               Promise<Unit> &&promise) const {
  if (!td_->auth_manager_->is_bot()) {
    return promise.set_error(Status::Error(400, "Method can be used by bots only"));
  }

  if (!switch_pm_text.empty()) {
    if (switch_pm_parameter.empty()) {
      return promise.set_error(Status::Error(400, "Can't use empty switch_pm_parameter"));
    }
    if (switch_pm_parameter.size() > 64) {
      return promise.set_error(Status::Error(400, "Too long switch_pm_parameter specified"));
    }
    if (!is_base64url_characters(switch_pm_parameter)) {
      return promise.set_error(Status::Error(400, "Unallowed characters in switch_pm_parameter are used"));
    }
  }

  vector<tl_object_ptr<telegram_api::InputBotInlineResult>> results;

  bool is_gallery = false;
  bool force_vertical = false;
  for (auto &input_result : input_results) {
    if (input_result == nullptr) {
      return promise.set_error(Status::Error(400, "Inline query result must be non-empty"));
    }

    string id;
    string url;
    string type;
    string title;
    string description;
    string thumbnail_url;
    string thumbnail_type = "image/jpeg";
    string content_url;
    string content_type;
    int32 thumbnail_width = 0;
    int32 thumbnail_height = 0;
    int32 width = 0;
    int32 height = 0;
    int32 duration = 0;

    FileType file_type = FileType::Temp;
    Result<tl_object_ptr<telegram_api::InputBotInlineMessage>> r_inline_message = Status::Error(500, "Uninited");
    switch (input_result->get_id()) {
      case td_api::inputInlineQueryResultAnimation::ID: {
        auto animation = move_tl_object_as<td_api::inputInlineQueryResultAnimation>(input_result);
        type = "gif";
        id = std::move(animation->id_);
        title = std::move(animation->title_);
        thumbnail_url = std::move(animation->thumbnail_url_);
        if (!animation->thumbnail_mime_type_.empty()) {
          thumbnail_type = std::move(animation->thumbnail_mime_type_);
        }
        content_url = std::move(animation->video_url_);
        content_type = std::move(animation->video_mime_type_);
        if (content_type != "image/gif" && content_type != "video/mp4") {
          return promise.set_error(Status::Error(400, "Wrong animation MIME type specified"));
        }
        duration = animation->video_duration_;
        width = animation->video_width_;
        height = animation->video_height_;
        is_gallery = true;

        file_type = FileType::Animation;
        r_inline_message = get_inline_message(std::move(animation->input_message_content_),
                                              std::move(animation->reply_markup_), td_api::inputMessageAnimation::ID);
        break;
      }
      case td_api::inputInlineQueryResultArticle::ID: {
        auto article = move_tl_object_as<td_api::inputInlineQueryResultArticle>(input_result);
        type = "article";
        id = std::move(article->id_);
        if (!article->url_.empty()) {
          content_url = std::move(article->url_);
          content_type = "text/html";
          if (!article->hide_url_) {
            url = content_url;
          }
        }
        title = std::move(article->title_);
        description = std::move(article->description_);
        thumbnail_url = std::move(article->thumbnail_url_);
        if (!thumbnail_url.empty()) {
          thumbnail_width = article->thumbnail_width_;
          thumbnail_height = article->thumbnail_height_;
        }
        force_vertical = true;

        r_inline_message =
            get_inline_message(std::move(article->input_message_content_), std::move(article->reply_markup_), -1);
        break;
      }
      case td_api::inputInlineQueryResultAudio::ID: {
        auto audio = move_tl_object_as<td_api::inputInlineQueryResultAudio>(input_result);
        type = "audio";
        id = std::move(audio->id_);
        title = std::move(audio->title_);
        description = std::move(audio->performer_);
        content_url = std::move(audio->audio_url_);
        content_type = "audio/mpeg";
        duration = audio->audio_duration_;
        force_vertical = true;

        file_type = FileType::Audio;
        r_inline_message = get_inline_message(std::move(audio->input_message_content_), std::move(audio->reply_markup_),
                                              td_api::inputMessageAudio::ID);
        break;
      }
      case td_api::inputInlineQueryResultContact::ID: {
        auto contact = move_tl_object_as<td_api::inputInlineQueryResultContact>(input_result);
        type = "contact";
        id = std::move(contact->id_);
        string phone_number = trim(contact->contact_->phone_number_);
        string first_name = trim(contact->contact_->first_name_);
        string last_name = trim(contact->contact_->last_name_);
        if (phone_number.empty()) {
          return promise.set_error(Status::Error(400, "Field \"phone_number\" must contain a valid phone number"));
        }
        if (first_name.empty()) {
          return promise.set_error(Status::Error(400, "Field \"first_name\" must be non-empty"));
        }
        title = last_name.empty() ? first_name : first_name + " " + last_name;
        description = std::move(phone_number);
        thumbnail_url = std::move(contact->thumbnail_url_);
        if (!thumbnail_url.empty()) {
          thumbnail_width = contact->thumbnail_width_;
          thumbnail_height = contact->thumbnail_height_;
        }
        force_vertical = true;

        r_inline_message =
            get_inline_message(std::move(contact->input_message_content_), std::move(contact->reply_markup_), -1);
        break;
      }
      case td_api::inputInlineQueryResultDocument::ID: {
        auto document = move_tl_object_as<td_api::inputInlineQueryResultDocument>(input_result);
        type = "file";
        id = std::move(document->id_);
        title = std::move(document->title_);
        description = std::move(document->description_);
        thumbnail_url = std::move(document->thumbnail_url_);
        content_url = std::move(document->document_url_);
        content_type = std::move(document->mime_type_);
        thumbnail_width = document->thumbnail_width_;
        thumbnail_height = document->thumbnail_height_;

        if (content_url.find('.') != string::npos) {
          if (begins_with(content_type, "application/pdf")) {
            content_type = "application/pdf";
          } else if (begins_with(content_type, "application/zip")) {
            content_type = "application/zip";
          } else {
            return promise.set_error(Status::Error(400, "Unallowed document MIME type"));
          }
        }

        file_type = FileType::Document;
        r_inline_message = get_inline_message(std::move(document->input_message_content_),
                                              std::move(document->reply_markup_), td_api::inputMessageDocument::ID);
        break;
      }
      case td_api::inputInlineQueryResultGame::ID: {
        auto game = move_tl_object_as<td_api::inputInlineQueryResultGame>(input_result);
        auto r_reply_markup = get_reply_markup(std::move(game->reply_markup_), true, true, false, true);
        if (r_reply_markup.is_error()) {
          return promise.set_error(r_reply_markup.move_as_error());
        }

        auto input_reply_markup = get_input_reply_markup(r_reply_markup.ok());
        int32 flags = 0;
        if (input_reply_markup != nullptr) {
          flags |= telegram_api::inputBotInlineMessageGame::REPLY_MARKUP_MASK;
        }
        auto result = make_tl_object<telegram_api::inputBotInlineResultGame>(
            game->id_, game->game_short_name_,
            make_tl_object<telegram_api::inputBotInlineMessageGame>(flags, std::move(input_reply_markup)));
        results.push_back(std::move(result));
        continue;
      }
      case td_api::inputInlineQueryResultLocation::ID: {
        auto location = move_tl_object_as<td_api::inputInlineQueryResultLocation>(input_result);
        type = "geo";
        id = std::move(location->id_);
        title = std::move(location->title_);
        description = PSTRING() << location->location_->latitude_ << ' ' << location->location_->longitude_;
        thumbnail_url = std::move(location->thumbnail_url_);
        // duration = location->live_period_;
        if (!thumbnail_url.empty()) {
          thumbnail_width = location->thumbnail_width_;
          thumbnail_height = location->thumbnail_height_;
        }

        r_inline_message =
            get_inline_message(std::move(location->input_message_content_), std::move(location->reply_markup_), -1);
        break;
      }
      case td_api::inputInlineQueryResultPhoto::ID: {
        auto photo = move_tl_object_as<td_api::inputInlineQueryResultPhoto>(input_result);
        type = "photo";
        id = std::move(photo->id_);
        title = std::move(photo->title_);
        description = std::move(photo->description_);
        thumbnail_url = std::move(photo->thumbnail_url_);
        content_url = std::move(photo->photo_url_);
        content_type = "image/jpeg";
        width = photo->photo_width_;
        height = photo->photo_height_;
        is_gallery = true;

        file_type = FileType::Photo;
        r_inline_message = get_inline_message(std::move(photo->input_message_content_), std::move(photo->reply_markup_),
                                              td_api::inputMessagePhoto::ID);
        break;
      }
      case td_api::inputInlineQueryResultSticker::ID: {
        auto sticker = move_tl_object_as<td_api::inputInlineQueryResultSticker>(input_result);
        type = "sticker";
        id = std::move(sticker->id_);
        thumbnail_url = std::move(sticker->thumbnail_url_);
        content_url = std::move(sticker->sticker_url_);
        content_type = "image/webp";  // or "application/x-tgsticker"; not used for previously uploaded files
        width = sticker->sticker_width_;
        height = sticker->sticker_height_;
        is_gallery = true;

        if (content_url.find('.') != string::npos) {
          return promise.set_error(Status::Error(400, "Wrong sticker_file_id specified"));
        }

        file_type = FileType::Sticker;
        r_inline_message = get_inline_message(std::move(sticker->input_message_content_),
                                              std::move(sticker->reply_markup_), td_api::inputMessageSticker::ID);
        break;
      }
      case td_api::inputInlineQueryResultVenue::ID: {
        auto venue = move_tl_object_as<td_api::inputInlineQueryResultVenue>(input_result);
        type = "venue";
        id = std::move(venue->id_);
        title = std::move(venue->venue_->title_);
        description = std::move(venue->venue_->address_);
        thumbnail_url = std::move(venue->thumbnail_url_);
        if (!thumbnail_url.empty()) {
          thumbnail_width = venue->thumbnail_width_;
          thumbnail_height = venue->thumbnail_height_;
        }

        r_inline_message =
            get_inline_message(std::move(venue->input_message_content_), std::move(venue->reply_markup_), -1);
        break;
      }
      case td_api::inputInlineQueryResultVideo::ID: {
        auto video = move_tl_object_as<td_api::inputInlineQueryResultVideo>(input_result);
        type = "video";
        id = std::move(video->id_);
        title = std::move(video->title_);
        description = std::move(video->description_);
        thumbnail_url = std::move(video->thumbnail_url_);
        content_url = std::move(video->video_url_);
        content_type = std::move(video->mime_type_);
        width = video->video_width_;
        height = video->video_height_;
        duration = video->video_duration_;

        if (content_url.find('.') != string::npos) {
          if (begins_with(content_type, "video/mp4")) {
            content_type = "video/mp4";
          } else if (begins_with(content_type, "text/html")) {
            content_type = "text/html";
          } else {
            return promise.set_error(Status::Error(400, "Unallowed video MIME type"));
          }
        }

        file_type = FileType::Video;
        r_inline_message = get_inline_message(std::move(video->input_message_content_), std::move(video->reply_markup_),
                                              td_api::inputMessageVideo::ID);
        break;
      }
      case td_api::inputInlineQueryResultVoiceNote::ID: {
        auto voice_note = move_tl_object_as<td_api::inputInlineQueryResultVoiceNote>(input_result);
        type = "voice";
        id = std::move(voice_note->id_);
        title = std::move(voice_note->title_);
        content_url = std::move(voice_note->voice_note_url_);
        content_type = "audio/ogg";
        duration = voice_note->voice_note_duration_;
        force_vertical = true;

        file_type = FileType::VoiceNote;
        r_inline_message = get_inline_message(std::move(voice_note->input_message_content_),
                                              std::move(voice_note->reply_markup_), td_api::inputMessageVoiceNote::ID);
        break;
      }
      default:
        UNREACHABLE();
        break;
    }
    if (r_inline_message.is_error()) {
      return promise.set_error(r_inline_message.move_as_error());
    }
    auto inline_message = r_inline_message.move_as_ok();
    if (inline_message->get_id() == telegram_api::inputBotInlineMessageMediaAuto::ID && file_type == FileType::Temp) {
      return promise.set_error(Status::Error(400, "Sent message content must be explicitly specified"));
    }

    if (duration < 0) {
      duration = 0;
    }

    int32 flags = 0;
    if (!title.empty()) {
      flags |= telegram_api::inputBotInlineResult::TITLE_MASK;
      if (!clean_input_string(title)) {
        return promise.set_error(Status::Error(400, "Strings must be encoded in UTF-8"));
      }
    }
    if (!description.empty()) {
      flags |= telegram_api::inputBotInlineResult::DESCRIPTION_MASK;
      if (!clean_input_string(description)) {
        return promise.set_error(Status::Error(400, "Strings must be encoded in UTF-8"));
      }
    }

    if (file_type != FileType::Temp && content_url.find('.') == string::npos) {
      auto r_file_id = td_->file_manager_->get_input_file_id(
          file_type, make_tl_object<td_api::inputFileRemote>(content_url), DialogId(), false, false);
      if (r_file_id.is_error()) {
        return promise.set_error(Status::Error(400, r_file_id.error().message()));
      }
      auto file_id = r_file_id.ok();
      FileView file_view = td_->file_manager_->get_file_view(file_id);
      CHECK(file_view.has_remote_location());
      if (file_view.is_encrypted()) {
        return promise.set_error(Status::Error(400, "Can't send encrypted file"));
      }
      if (file_view.main_remote_location().is_web()) {
        return promise.set_error(Status::Error(400, "Can't send web file"));
      }

      if (file_type == FileType::Photo) {
        auto result = make_tl_object<telegram_api::inputBotInlineResultPhoto>(
            id, type, file_view.main_remote_location().as_input_photo(), std::move(inline_message));
        results.push_back(std::move(result));
        continue;
      }

      auto result = make_tl_object<telegram_api::inputBotInlineResultDocument>(
          flags, id, type, title, description, file_view.main_remote_location().as_input_document(),
          std::move(inline_message));
      results.push_back(std::move(result));
      continue;
    }

    if (!url.empty()) {
      flags |= telegram_api::inputBotInlineResult::URL_MASK;
      if (!clean_input_string(url)) {
        return promise.set_error(Status::Error(400, "Strings must be encoded in UTF-8"));
      }
    }
    tl_object_ptr<telegram_api::inputWebDocument> thumbnail;
    if (!thumbnail_url.empty()) {
      flags |= telegram_api::inputBotInlineResult::THUMB_MASK;
      if (!clean_input_string(thumbnail_url)) {
        return promise.set_error(Status::Error(400, "Strings must be encoded in UTF-8"));
      }
      vector<tl_object_ptr<telegram_api::DocumentAttribute>> attributes;
      if (thumbnail_width > 0 && thumbnail_height > 0) {
        attributes.push_back(
            make_tl_object<telegram_api::documentAttributeImageSize>(thumbnail_width, thumbnail_height));
      }
      thumbnail =
          make_tl_object<telegram_api::inputWebDocument>(thumbnail_url, 0, thumbnail_type, std::move(attributes));
    }
    tl_object_ptr<telegram_api::inputWebDocument> content;
    if (!content_url.empty() || !content_type.empty()) {
      flags |= telegram_api::inputBotInlineResult::CONTENT_MASK;
      if (!clean_input_string(content_url)) {
        return promise.set_error(Status::Error(400, "Strings must be encoded in UTF-8"));
      }
      if (!clean_input_string(content_type)) {
        return promise.set_error(Status::Error(400, "Strings must be encoded in UTF-8"));
      }

      vector<tl_object_ptr<telegram_api::DocumentAttribute>> attributes;
      if (width > 0 && height > 0) {
        if ((duration > 0 || type == "video" || content_type == "video/mp4") && !begins_with(content_type, "image/")) {
          attributes.push_back(make_tl_object<telegram_api::documentAttributeVideo>(
              0, false /*ignored*/, false /*ignored*/, duration, width, height));
        } else {
          attributes.push_back(make_tl_object<telegram_api::documentAttributeImageSize>(width, height));
        }
      } else if (type == "audio") {
        attributes.push_back(make_tl_object<telegram_api::documentAttributeAudio>(
            telegram_api::documentAttributeAudio::TITLE_MASK | telegram_api::documentAttributeAudio::PERFORMER_MASK,
            false /*ignored*/, duration, title, description, BufferSlice()));
      } else if (type == "voice") {
        attributes.push_back(make_tl_object<telegram_api::documentAttributeAudio>(
            telegram_api::documentAttributeAudio::VOICE_MASK, false /*ignored*/, duration, "", "", BufferSlice()));
      }
      attributes.push_back(make_tl_object<telegram_api::documentAttributeFilename>(get_url_file_name(content_url)));

      content = make_tl_object<telegram_api::inputWebDocument>(content_url, 0, content_type, std::move(attributes));
    }

    auto result = make_tl_object<telegram_api::inputBotInlineResult>(
        flags, id, type, title, description, url, std::move(thumbnail), std::move(content), std::move(inline_message));
    results.push_back(std::move(result));
  }

  td_->create_handler<SetInlineBotResultsQuery>(std::move(promise))
      ->send(inline_query_id, is_gallery && !force_vertical, is_personal, std::move(results), cache_time, next_offset,
             switch_pm_text, switch_pm_parameter);
}

uint64 InlineQueriesManager::send_inline_query(UserId bot_user_id, DialogId dialog_id, Location user_location,
                                               const string &query, const string &offset, Promise<Unit> &&promise) {
  if (td_->auth_manager_->is_bot()) {
    promise.set_error(Status::Error(5, "Bot can't send inline queries to other bot"));
    return 0;
  }

  auto r_bot_data = td_->contacts_manager_->get_bot_data(bot_user_id);
  if (r_bot_data.is_error()) {
    promise.set_error(r_bot_data.move_as_error());
    return 0;
  }
  if (!r_bot_data.ok().is_inline) {
    promise.set_error(Status::Error(5, "Bot doesn't support inline queries"));
    return 0;
  }

  bool is_broadcast_channel =
      dialog_id.get_type() == DialogType::Channel &&
      td_->contacts_manager_->get_channel_type(dialog_id.get_channel_id()) == ChannelType::Broadcast;

  uint64 query_hash = std::hash<std::string>()(trim(query));
  query_hash = query_hash * 2023654985u + bot_user_id.get();
  query_hash = query_hash * 2023654985u + static_cast<uint64>(is_broadcast_channel);
  query_hash = query_hash * 2023654985u + std::hash<std::string>()(offset);
  if (r_bot_data.ok().need_location) {
    query_hash = query_hash * 2023654985u + static_cast<uint64>(user_location.get_latitude() * 1e4);
    query_hash = query_hash * 2023654985u + static_cast<uint64>(user_location.get_longitude() * 1e4);
  }
  query_hash &= 0x7FFFFFFFFFFFFFFF;

  auto it = inline_query_results_.find(query_hash);
  if (it != inline_query_results_.end()) {
    it->second.pending_request_count++;
    if (Time::now() < it->second.cache_expire_time) {
      promise.set_value(Unit());
      return query_hash;
    }
  } else {
    inline_query_results_[query_hash] = {nullptr, -1.0, 1};
  }

  if (pending_inline_query_ != nullptr) {
    LOG(INFO) << "Drop inline query " << pending_inline_query_->query_hash;
    on_get_inline_query_results(pending_inline_query_->bot_user_id, pending_inline_query_->query_hash, nullptr);
    pending_inline_query_->promise.set_error(Status::Error(406, "Request cancelled"));
  }

  pending_inline_query_ = make_unique<PendingInlineQuery>(
      PendingInlineQuery{query_hash, bot_user_id, dialog_id, user_location, query, offset, std::move(promise)});

  loop();

  return query_hash;
}

void InlineQueriesManager::loop() {
  LOG(INFO) << "Inline query loop";
  if (pending_inline_query_ == nullptr) {
    return;
  }

  auto now = Time::now();
  if (now >= next_inline_query_time_) {
    LOG(INFO) << "Send inline query " << pending_inline_query_->query_hash;
    auto bot_input_user = td_->contacts_manager_->get_input_user(pending_inline_query_->bot_user_id);
    if (bot_input_user != nullptr) {
      if (!sent_query_.empty()) {
        LOG(INFO) << "Cancel inline query request";
        cancel_query(sent_query_);
      }
      sent_query_ =
          td_->create_handler<GetInlineBotResultsQuery>(std::move(pending_inline_query_->promise))
              ->send(pending_inline_query_->bot_user_id, std::move(bot_input_user), pending_inline_query_->dialog_id,
                     pending_inline_query_->user_location, pending_inline_query_->query, pending_inline_query_->offset,
                     pending_inline_query_->query_hash);

      next_inline_query_time_ = now + INLINE_QUERY_DELAY_MS * 1e-3;
    }
    pending_inline_query_ = nullptr;
  } else {
    if (!has_timeout()) {
      LOG(INFO) << "Schedule send inline query " << pending_inline_query_->query_hash << " at "
                << G()->to_server_time(next_inline_query_time_);
      set_timeout_at(next_inline_query_time_);
    }
  }
}

template <class T>
static tl_object_ptr<T> copy(const T &obj) {
  // see https://bugs.llvm.org/show_bug.cgi?id=17537
  static_assert(sizeof(T) == 0, "Only specializations of <copy> can be used");
}

template <class T>
static tl_object_ptr<T> copy(const tl_object_ptr<T> &obj) {
  return obj == nullptr ? nullptr : copy(*obj);
}

template <>
td_api::object_ptr<td_api::localFile> copy(const td_api::localFile &obj) {
  return td_api::make_object<td_api::localFile>(
      obj.path_, obj.can_be_downloaded_, obj.can_be_deleted_, obj.is_downloading_active_, obj.is_downloading_completed_,
      obj.download_offset_, obj.downloaded_prefix_size_, obj.downloaded_size_);
}
template <>
td_api::object_ptr<td_api::remoteFile> copy(const td_api::remoteFile &obj) {
  return td_api::make_object<td_api::remoteFile>(obj.id_, obj.unique_id_, obj.is_uploading_active_,
                                                 obj.is_uploading_completed_, obj.uploaded_size_);
}

template <>
td_api::object_ptr<td_api::file> copy(const td_api::file &obj) {
  FileId file_id(obj.id_, 0);  // wrong, but there should be no difference for get_file_object
  if (file_id.is_valid()) {
    return G()->td().get_actor_unsafe()->file_manager_.get()->get_file_object(file_id);
  } else {
    return td_api::make_object<td_api::file>(obj.id_, obj.size_, obj.expected_size_, copy(obj.local_),
                                             copy(obj.remote_));
  }
}

template <>
tl_object_ptr<td_api::minithumbnail> copy(const td_api::minithumbnail &obj) {
  return make_tl_object<td_api::minithumbnail>(obj.width_, obj.height_, obj.data_);
}

template <>
tl_object_ptr<td_api::photoSize> copy(const td_api::photoSize &obj) {
  return make_tl_object<td_api::photoSize>(obj.type_, copy(obj.photo_), obj.width_, obj.height_,
                                           vector<int32>(obj.progressive_sizes_));
}

template <>
tl_object_ptr<td_api::thumbnail> copy(const td_api::thumbnail &obj) {
  auto format = [&]() -> td_api::object_ptr<td_api::ThumbnailFormat> {
    switch (obj.format_->get_id()) {
      case td_api::thumbnailFormatJpeg::ID:
        return td_api::make_object<td_api::thumbnailFormatJpeg>();
      case td_api::thumbnailFormatPng::ID:
        return td_api::make_object<td_api::thumbnailFormatPng>();
      case td_api::thumbnailFormatWebp::ID:
        return td_api::make_object<td_api::thumbnailFormatWebp>();
      case td_api::thumbnailFormatTgs::ID:
        return td_api::make_object<td_api::thumbnailFormatTgs>();
      case td_api::thumbnailFormatMpeg4::ID:
        return td_api::make_object<td_api::thumbnailFormatMpeg4>();
      case td_api::thumbnailFormatGif::ID:
        return td_api::make_object<td_api::thumbnailFormatGif>();
      default:
        UNREACHABLE();
        return nullptr;
    }
  }();

  return make_tl_object<td_api::thumbnail>(std::move(format), obj.width_, obj.height_, copy(obj.file_));
}

static tl_object_ptr<td_api::photoSize> copy_photo_size(const tl_object_ptr<td_api::photoSize> &obj) {
  return copy(obj);
}

template <>
tl_object_ptr<td_api::MaskPoint> copy(const td_api::MaskPoint &obj) {
  switch (obj.get_id()) {
    case td_api::maskPointForehead::ID:
      return make_tl_object<td_api::maskPointForehead>();
    case td_api::maskPointEyes::ID:
      return make_tl_object<td_api::maskPointEyes>();
    case td_api::maskPointMouth::ID:
      return make_tl_object<td_api::maskPointMouth>();
    case td_api::maskPointChin::ID:
      return make_tl_object<td_api::maskPointChin>();
    default:
      UNREACHABLE();
  }
  return nullptr;
}

template <>
tl_object_ptr<td_api::maskPosition> copy(const td_api::maskPosition &obj) {
  return make_tl_object<td_api::maskPosition>(copy(obj.point_), obj.x_shift_, obj.y_shift_, obj.scale_);
}

template <>
tl_object_ptr<td_api::animation> copy(const td_api::animation &obj) {
  return make_tl_object<td_api::animation>(obj.duration_, obj.width_, obj.height_, obj.file_name_, obj.mime_type_,
                                           obj.has_stickers_, copy(obj.minithumbnail_), copy(obj.thumbnail_),
                                           copy(obj.animation_));
}

template <>
tl_object_ptr<td_api::audio> copy(const td_api::audio &obj) {
  return make_tl_object<td_api::audio>(obj.duration_, obj.title_, obj.performer_, obj.file_name_, obj.mime_type_,
                                       copy(obj.album_cover_minithumbnail_), copy(obj.album_cover_thumbnail_),
                                       copy(obj.audio_));
}

template <>
tl_object_ptr<td_api::document> copy(const td_api::document &obj) {
  return make_tl_object<td_api::document>(obj.file_name_, obj.mime_type_, copy(obj.minithumbnail_),
                                          copy(obj.thumbnail_), copy(obj.document_));
}

template <>
tl_object_ptr<td_api::photo> copy(const td_api::photo &obj) {
  return make_tl_object<td_api::photo>(obj.has_stickers_, copy(obj.minithumbnail_),
                                       transform(obj.sizes_, copy_photo_size));
}

template <>
tl_object_ptr<td_api::sticker> copy(const td_api::sticker &obj) {
  return make_tl_object<td_api::sticker>(obj.set_id_, obj.width_, obj.height_, obj.emoji_, obj.is_animated_,
                                         obj.is_mask_, copy(obj.mask_position_), copy(obj.thumbnail_),
                                         copy(obj.sticker_));
}

template <>
tl_object_ptr<td_api::video> copy(const td_api::video &obj) {
  return make_tl_object<td_api::video>(obj.duration_, obj.width_, obj.height_, obj.file_name_, obj.mime_type_,
                                       obj.has_stickers_, obj.supports_streaming_, copy(obj.minithumbnail_),
                                       copy(obj.thumbnail_), copy(obj.video_));
}

template <>
tl_object_ptr<td_api::voiceNote> copy(const td_api::voiceNote &obj) {
  return make_tl_object<td_api::voiceNote>(obj.duration_, obj.waveform_, obj.mime_type_, copy(obj.voice_));
}

template <>
tl_object_ptr<td_api::contact> copy(const td_api::contact &obj) {
  return make_tl_object<td_api::contact>(obj.phone_number_, obj.first_name_, obj.last_name_, obj.vcard_, obj.user_id_);
}

template <>
tl_object_ptr<td_api::location> copy(const td_api::location &obj) {
  return make_tl_object<td_api::location>(obj.latitude_, obj.longitude_, obj.horizontal_accuracy_);
}

template <>
tl_object_ptr<td_api::venue> copy(const td_api::venue &obj) {
  return make_tl_object<td_api::venue>(copy(obj.location_), obj.title_, obj.address_, obj.provider_, obj.id_,
                                       obj.type_);
}

template <>
tl_object_ptr<td_api::formattedText> copy(const td_api::formattedText &obj) {
  // there is no entities in the game text
  return make_tl_object<td_api::formattedText>(obj.text_, vector<tl_object_ptr<td_api::textEntity>>());
}

template <>
tl_object_ptr<td_api::game> copy(const td_api::game &obj) {
  return make_tl_object<td_api::game>(obj.id_, obj.short_name_, obj.title_, copy(obj.text_), obj.description_,
                                      copy(obj.photo_), copy(obj.animation_));
}

template <>
tl_object_ptr<td_api::inlineQueryResultArticle> copy(const td_api::inlineQueryResultArticle &obj) {
  return make_tl_object<td_api::inlineQueryResultArticle>(obj.id_, obj.url_, obj.hide_url_, obj.title_,
                                                          obj.description_, copy(obj.thumbnail_));
}

template <>
tl_object_ptr<td_api::inlineQueryResultContact> copy(const td_api::inlineQueryResultContact &obj) {
  return make_tl_object<td_api::inlineQueryResultContact>(obj.id_, copy(obj.contact_), copy(obj.thumbnail_));
}

template <>
tl_object_ptr<td_api::inlineQueryResultLocation> copy(const td_api::inlineQueryResultLocation &obj) {
  return make_tl_object<td_api::inlineQueryResultLocation>(obj.id_, copy(obj.location_), obj.title_,
                                                           copy(obj.thumbnail_));
}

template <>
tl_object_ptr<td_api::inlineQueryResultVenue> copy(const td_api::inlineQueryResultVenue &obj) {
  return make_tl_object<td_api::inlineQueryResultVenue>(obj.id_, copy(obj.venue_), copy(obj.thumbnail_));
}

template <>
tl_object_ptr<td_api::inlineQueryResultGame> copy(const td_api::inlineQueryResultGame &obj) {
  return make_tl_object<td_api::inlineQueryResultGame>(obj.id_, copy(obj.game_));
}

template <>
tl_object_ptr<td_api::inlineQueryResultAnimation> copy(const td_api::inlineQueryResultAnimation &obj) {
  return make_tl_object<td_api::inlineQueryResultAnimation>(obj.id_, copy(obj.animation_), obj.title_);
}

template <>
tl_object_ptr<td_api::inlineQueryResultAudio> copy(const td_api::inlineQueryResultAudio &obj) {
  return make_tl_object<td_api::inlineQueryResultAudio>(obj.id_, copy(obj.audio_));
}

template <>
tl_object_ptr<td_api::inlineQueryResultDocument> copy(const td_api::inlineQueryResultDocument &obj) {
  return make_tl_object<td_api::inlineQueryResultDocument>(obj.id_, copy(obj.document_), obj.title_, obj.description_);
}

template <>
tl_object_ptr<td_api::inlineQueryResultPhoto> copy(const td_api::inlineQueryResultPhoto &obj) {
  return make_tl_object<td_api::inlineQueryResultPhoto>(obj.id_, copy(obj.photo_), obj.title_, obj.description_);
}

template <>
tl_object_ptr<td_api::inlineQueryResultSticker> copy(const td_api::inlineQueryResultSticker &obj) {
  return make_tl_object<td_api::inlineQueryResultSticker>(obj.id_, copy(obj.sticker_));
}

template <>
tl_object_ptr<td_api::inlineQueryResultVideo> copy(const td_api::inlineQueryResultVideo &obj) {
  return make_tl_object<td_api::inlineQueryResultVideo>(obj.id_, copy(obj.video_), obj.title_, obj.description_);
}

template <>
tl_object_ptr<td_api::inlineQueryResultVoiceNote> copy(const td_api::inlineQueryResultVoiceNote &obj) {
  return make_tl_object<td_api::inlineQueryResultVoiceNote>(obj.id_, copy(obj.voice_note_), obj.title_);
}

static tl_object_ptr<td_api::InlineQueryResult> copy_result(const tl_object_ptr<td_api::InlineQueryResult> &obj_ptr) {
  tl_object_ptr<td_api::InlineQueryResult> result;
  downcast_call(const_cast<td_api::InlineQueryResult &>(*obj_ptr), [&result](const auto &obj) { result = copy(obj); });
  return result;
}

template <>
tl_object_ptr<td_api::inlineQueryResults> copy(const td_api::inlineQueryResults &obj) {
  return make_tl_object<td_api::inlineQueryResults>(obj.inline_query_id_, obj.next_offset_,
                                                    transform(obj.results_, copy_result), obj.switch_pm_text_,
                                                    obj.switch_pm_parameter_);
}

tl_object_ptr<td_api::inlineQueryResults> InlineQueriesManager::decrease_pending_request_count(uint64 query_hash) {
  auto it = inline_query_results_.find(query_hash);
  CHECK(it != inline_query_results_.end());
  CHECK(it->second.pending_request_count > 0);
  it->second.pending_request_count--;
  LOG(INFO) << "Inline query " << query_hash << " is awaited by " << it->second.pending_request_count
            << " pending requests";
  if (it->second.pending_request_count == 0) {
    auto left_time = it->second.cache_expire_time - Time::now();
    if (left_time < 0) {
      LOG(INFO) << "Drop cache for inline query " << query_hash;
      drop_inline_query_result_timeout_.cancel_timeout(static_cast<int64>(query_hash));
      auto result = std::move(it->second.results);
      inline_query_results_.erase(it);
      return result;
    } else {
      drop_inline_query_result_timeout_.set_timeout_at(static_cast<int64>(query_hash), it->second.cache_expire_time);
    }
  }
  return copy(it->second.results);
}

tl_object_ptr<td_api::thumbnail> InlineQueriesManager::register_thumbnail(
    tl_object_ptr<telegram_api::WebDocument> &&web_document_ptr) const {
  PhotoSize thumbnail = get_web_document_photo_size(td_->file_manager_.get(), FileType::Thumbnail, DialogId(),
                                                    std::move(web_document_ptr));
  if (!thumbnail.file_id.is_valid() || thumbnail.type == 'v') {
    return nullptr;
  }

  return get_thumbnail_object(td_->file_manager_.get(), thumbnail,
                              thumbnail.type == 'g' ? PhotoFormat::Gif : PhotoFormat::Jpeg);
}

string InlineQueriesManager::get_web_document_url(const tl_object_ptr<telegram_api::WebDocument> &web_document_ptr) {
  if (web_document_ptr == nullptr) {
    return {};
  }

  Slice url;
  switch (web_document_ptr->get_id()) {
    case telegram_api::webDocument::ID: {
      auto web_document = static_cast<const telegram_api::webDocument *>(web_document_ptr.get());
      url = web_document->url_;
      break;
    }
    case telegram_api::webDocumentNoProxy::ID: {
      auto web_document = static_cast<const telegram_api::webDocumentNoProxy *>(web_document_ptr.get());
      url = web_document->url_;
      break;
    }
    default:
      UNREACHABLE();
  }

  auto r_http_url = parse_url(url);
  if (r_http_url.is_error()) {
    LOG(ERROR) << "Can't parse URL " << url;
    return {};
  }
  return r_http_url.ok().get_url();
}

string InlineQueriesManager::get_web_document_content_type(
    const tl_object_ptr<telegram_api::WebDocument> &web_document_ptr) {
  if (web_document_ptr == nullptr) {
    return {};
  }

  switch (web_document_ptr->get_id()) {
    case telegram_api::webDocument::ID:
      return static_cast<const telegram_api::webDocument *>(web_document_ptr.get())->mime_type_;
    case telegram_api::webDocumentNoProxy::ID:
      return static_cast<const telegram_api::webDocumentNoProxy *>(web_document_ptr.get())->mime_type_;
    default:
      UNREACHABLE();
  }

  return {};
}

void InlineQueriesManager::on_get_inline_query_results(UserId bot_user_id, uint64 query_hash,
                                                       tl_object_ptr<telegram_api::messages_botResults> &&results) {
  LOG(INFO) << "Receive results for inline query " << query_hash;
  if (results == nullptr) {
    decrease_pending_request_count(query_hash);
    return;
  }
  LOG(INFO) << to_string(results);

  td_->contacts_manager_->on_get_users(std::move(results->users_), "on_get_inline_query_results");

  vector<tl_object_ptr<td_api::InlineQueryResult>> output_results;
  for (auto &result_ptr : results->results_) {
    tl_object_ptr<td_api::InlineQueryResult> output_result;
    switch (result_ptr->get_id()) {
      case telegram_api::botInlineMediaResult::ID: {
        auto result = move_tl_object_as<telegram_api::botInlineMediaResult>(result_ptr);
        auto flags = result->flags_;
        bool has_document = (flags & BOT_INLINE_MEDIA_RESULT_FLAG_HAS_DOCUMENT) != 0;
        bool has_photo = (flags & BOT_INLINE_MEDIA_RESULT_FLAG_HAS_PHOTO) != 0;
        bool is_photo = result->type_ == "photo";
        if (result->type_ == "game") {
          if (!has_photo) {
            LOG(ERROR) << "Receive game without photo in the result of inline query: " << to_string(result);
            break;
          }
          auto game = make_tl_object<td_api::inlineQueryResultGame>();
          Game inline_game(td_, std::move(result->title_), std::move(result->description_), std::move(result->photo_),
                           std::move(result->document_), DialogId());

          game->id_ = std::move(result->id_);
          game->game_ = inline_game.get_game_object(td_);

          if (!register_inline_message_content(results->query_id_, game->id_, FileId(),
                                               std::move(result->send_message_), td_api::inputMessageGame::ID, nullptr,
                                               &inline_game)) {
            continue;
          }
          output_result = std::move(game);
        } else if (has_document && !(has_photo && is_photo)) {
          auto document_ptr = std::move(result->document_);
          int32 document_id = document_ptr->get_id();
          if (document_id == telegram_api::documentEmpty::ID) {
            LOG(ERROR) << "Receive empty cached document in the result of inline query";
            break;
          }
          CHECK(document_id == telegram_api::document::ID);

          auto parsed_document = td_->documents_manager_->on_get_document(
              move_tl_object_as<telegram_api::document>(document_ptr), DialogId());
          switch (parsed_document.type) {
            case Document::Type::Animation: {
              LOG_IF(WARNING, result->type_ != "gif") << "Wrong result type " << result->type_;

              auto animation = make_tl_object<td_api::inlineQueryResultAnimation>();
              animation->id_ = std::move(result->id_);
              animation->animation_ =
                  td_->animations_manager_->get_animation_object(parsed_document.file_id, "inlineQueryResultAnimation");
              animation->title_ = std::move(result->title_);

              if (!register_inline_message_content(results->query_id_, animation->id_, parsed_document.file_id,
                                                   std::move(result->send_message_),
                                                   td_api::inputMessageAnimation::ID)) {
                continue;
              }
              output_result = std::move(animation);
              break;
            }
            case Document::Type::Audio: {
              LOG_IF(WARNING, result->type_ != "audio") << "Wrong result type " << result->type_;

              auto audio = make_tl_object<td_api::inlineQueryResultAudio>();
              audio->id_ = std::move(result->id_);
              audio->audio_ = td_->audios_manager_->get_audio_object(parsed_document.file_id);

              if (!register_inline_message_content(results->query_id_, audio->id_, parsed_document.file_id,
                                                   std::move(result->send_message_), td_api::inputMessageAudio::ID)) {
                continue;
              }
              output_result = std::move(audio);
              break;
            }
            case Document::Type::General: {
              LOG_IF(WARNING, result->type_ != "file") << "Wrong result type " << result->type_;

              auto document = make_tl_object<td_api::inlineQueryResultDocument>();
              document->id_ = std::move(result->id_);
              document->document_ =
                  td_->documents_manager_->get_document_object(parsed_document.file_id, PhotoFormat::Jpeg);
              document->title_ = std::move(result->title_);
              document->description_ = std::move(result->description_);

              if (!register_inline_message_content(results->query_id_, document->id_, parsed_document.file_id,
                                                   std::move(result->send_message_),
                                                   td_api::inputMessageDocument::ID)) {
                continue;
              }
              output_result = std::move(document);
              break;
            }
            case Document::Type::Sticker: {
              LOG_IF(WARNING, result->type_ != "sticker") << "Wrong result type " << result->type_;

              auto sticker = make_tl_object<td_api::inlineQueryResultSticker>();
              sticker->id_ = std::move(result->id_);
              sticker->sticker_ = td_->stickers_manager_->get_sticker_object(parsed_document.file_id);

              if (!register_inline_message_content(results->query_id_, sticker->id_, parsed_document.file_id,
                                                   std::move(result->send_message_), td_api::inputMessageSticker::ID)) {
                continue;
              }
              output_result = std::move(sticker);
              break;
            }
            case Document::Type::Video: {
              LOG_IF(WARNING, result->type_ != "video") << "Wrong result type " << result->type_;

              auto video = make_tl_object<td_api::inlineQueryResultVideo>();
              video->id_ = std::move(result->id_);
              video->video_ = td_->videos_manager_->get_video_object(parsed_document.file_id);
              video->title_ = std::move(result->title_);
              video->description_ = std::move(result->description_);

              if (!register_inline_message_content(results->query_id_, video->id_, parsed_document.file_id,
                                                   std::move(result->send_message_), td_api::inputMessageVideo::ID)) {
                continue;
              }
              output_result = std::move(video);
              break;
            }
            case Document::Type::VideoNote:
              // FIXME
              break;
            case Document::Type::VoiceNote: {
              LOG_IF(WARNING, result->type_ != "voice") << "Wrong result type " << result->type_;

              auto voice_note = make_tl_object<td_api::inlineQueryResultVoiceNote>();
              voice_note->id_ = std::move(result->id_);
              voice_note->voice_note_ = td_->voice_notes_manager_->get_voice_note_object(parsed_document.file_id);
              voice_note->title_ = std::move(result->title_);

              if (!register_inline_message_content(results->query_id_, voice_note->id_, parsed_document.file_id,
                                                   std::move(result->send_message_),
                                                   td_api::inputMessageVoiceNote::ID)) {
                continue;
              }
              output_result = std::move(voice_note);
              break;
            }
            case Document::Type::Unknown:
              // invalid document
              break;
            default:
              UNREACHABLE();
              break;
          }
        } else if (has_photo) {
          LOG_IF(ERROR, !is_photo) << "Wrong result type " << result->type_;
          auto photo = make_tl_object<td_api::inlineQueryResultPhoto>();
          photo->id_ = std::move(result->id_);
          Photo p = get_photo(td_->file_manager_.get(), std::move(result->photo_), DialogId());
          if (p.is_empty()) {
            LOG(ERROR) << "Receive empty cached photo in the result of inline query";
            break;
          }
          photo->photo_ = get_photo_object(td_->file_manager_.get(), p);
          photo->title_ = std::move(result->title_);
          photo->description_ = std::move(result->description_);

          if (!register_inline_message_content(results->query_id_, photo->id_, FileId(),
                                               std::move(result->send_message_), td_api::inputMessagePhoto::ID, &p)) {
            continue;
          }
          output_result = std::move(photo);
        } else {
          LOG(ERROR) << "Receive inline query media result without photo and document: " << to_string(result);
        }
        break;
      }
      case telegram_api::botInlineResult::ID: {
        auto result = move_tl_object_as<telegram_api::botInlineResult>(result_ptr);
        auto content_type = get_web_document_content_type(result->content_);
        if (result->type_ == "article") {
          auto article = make_tl_object<td_api::inlineQueryResultArticle>();
          article->id_ = std::move(result->id_);
          article->url_ = get_web_document_url(std::move(result->content_));
          if (result->url_.empty()) {
            article->hide_url_ = true;
          } else {
            LOG_IF(ERROR, result->url_ != article->url_)
                << "Url has changed from " << article->url_ << " to " << result->url_;
            article->hide_url_ = false;
          }
          article->title_ = std::move(result->title_);
          article->description_ = std::move(result->description_);
          article->thumbnail_ = register_thumbnail(std::move(result->thumb_));

          if (!register_inline_message_content(results->query_id_, article->id_, FileId(),
                                               std::move(result->send_message_), -1)) {
            continue;
          }
          output_result = std::move(article);
        } else if (result->type_ == "contact") {
          auto contact = make_tl_object<td_api::inlineQueryResultContact>();
          contact->id_ = std::move(result->id_);
          if (result->send_message_->get_id() == telegram_api::botInlineMessageMediaContact::ID) {
            auto inline_message_contact =
                static_cast<const telegram_api::botInlineMessageMediaContact *>(result->send_message_.get());
            Contact c(inline_message_contact->phone_number_, inline_message_contact->first_name_,
                      inline_message_contact->last_name_, inline_message_contact->vcard_, 0);
            contact->contact_ = c.get_contact_object();
          } else {
            Contact c(std::move(result->description_), std::move(result->title_), string(), string(), 0);
            contact->contact_ = c.get_contact_object();
          }
          contact->thumbnail_ = register_thumbnail(std::move(result->thumb_));

          if (!register_inline_message_content(results->query_id_, contact->id_, FileId(),
                                               std::move(result->send_message_), -1)) {
            continue;
          }
          output_result = std::move(contact);
        } else if (result->type_ == "geo") {
          auto location = make_tl_object<td_api::inlineQueryResultLocation>();
          location->id_ = std::move(result->id_);
          location->title_ = std::move(result->title_);
          if (result->send_message_->get_id() == telegram_api::botInlineMessageMediaGeo::ID) {
            auto inline_message_geo =
                static_cast<const telegram_api::botInlineMessageMediaGeo *>(result->send_message_.get());
            Location l(inline_message_geo->geo_);
            location->location_ = l.get_location_object();
          } else {
            auto latitude_longitude = split(Slice(result->description_));
            Location l(to_double(latitude_longitude.first), to_double(latitude_longitude.second), 0.0, 0);
            location->location_ = l.get_location_object();
          }
          location->thumbnail_ = register_thumbnail(std::move(result->thumb_));

          if (!register_inline_message_content(results->query_id_, location->id_, FileId(),
                                               std::move(result->send_message_), -1)) {
            continue;
          }
          output_result = std::move(location);
        } else if (result->type_ == "venue") {
          auto venue = make_tl_object<td_api::inlineQueryResultVenue>();
          venue->id_ = std::move(result->id_);
          if (result->send_message_->get_id() == telegram_api::botInlineMessageMediaVenue::ID) {
            auto inline_message_venue =
                static_cast<const telegram_api::botInlineMessageMediaVenue *>(result->send_message_.get());
            Venue v(inline_message_venue->geo_, inline_message_venue->title_, inline_message_venue->address_,
                    inline_message_venue->provider_, inline_message_venue->venue_id_,
                    inline_message_venue->venue_type_);
            venue->venue_ = v.get_venue_object();
          } else if (result->send_message_->get_id() == telegram_api::botInlineMessageMediaGeo::ID) {
            auto inline_message_geo =
                static_cast<const telegram_api::botInlineMessageMediaGeo *>(result->send_message_.get());
            Venue v(inline_message_geo->geo_, std::move(result->title_), std::move(result->description_), string(),
                    string(), string());
            venue->venue_ = v.get_venue_object();
          } else {
            Venue v(nullptr, std::move(result->title_), std::move(result->description_), string(), string(), string());
            venue->venue_ = v.get_venue_object();
          }
          venue->thumbnail_ = register_thumbnail(std::move(result->thumb_));

          if (!register_inline_message_content(results->query_id_, venue->id_, FileId(),
                                               std::move(result->send_message_), -1)) {
            continue;
          }
          output_result = std::move(venue);
        } else if (result->type_ == "photo" && content_type == "image/jpeg") {
          auto photo = make_tl_object<td_api::inlineQueryResultPhoto>();
          photo->id_ = std::move(result->id_);

          PhotoSize photo_size = get_web_document_photo_size(td_->file_manager_.get(), FileType::Temp, DialogId(),
                                                             std::move(result->content_));
          if (!photo_size.file_id.is_valid() || photo_size.type == 'v' || photo_size.type == 'g') {
            LOG(ERROR) << "Receive invalid web document photo";
            continue;
          }

          Photo new_photo;
          new_photo.id = 0;
          PhotoSize thumbnail = get_web_document_photo_size(td_->file_manager_.get(), FileType::Thumbnail, DialogId(),
                                                            std::move(result->thumb_));
          if (thumbnail.file_id.is_valid() && thumbnail.type != 'v' && thumbnail.type != 'g') {
            new_photo.photos.push_back(std::move(thumbnail));
          }
          new_photo.photos.push_back(std::move(photo_size));

          photo->photo_ = get_photo_object(td_->file_manager_.get(), new_photo);
          photo->title_ = std::move(result->title_);
          photo->description_ = std::move(result->description_);

          if (!register_inline_message_content(results->query_id_, photo->id_, FileId(),
                                               std::move(result->send_message_), td_api::inputMessagePhoto::ID,
                                               &new_photo)) {
            continue;
          }
          output_result = std::move(photo);
        } else {
          if (result->content_ == nullptr) {
            LOG(ERROR) << "Unsupported inline query result without content " << to_string(result);
            continue;
          }

          vector<tl_object_ptr<telegram_api::DocumentAttribute>> attributes;
          downcast_call(*result->content_,
                        [&attributes](auto &web_document) { attributes = std::move(web_document.attributes_); });

          bool is_animation = result->type_ == "gif" && (content_type == "image/gif" || content_type == "video/mp4");
          if (is_animation) {
            attributes.push_back(make_tl_object<telegram_api::documentAttributeAnimated>());
          }
          auto default_document_type = [type = result->type_, is_animation] {
            if (type == "audio") {
              return Document::Type::Audio;
            }
            if (is_animation) {
              return Document::Type::Animation;
            }
            if (type == "sticker") {
              return Document::Type::Sticker;
            }
            if (type == "video") {
              return Document::Type::Video;
            }
            if (type == "voice") {
              return Document::Type::VoiceNote;
            }
            return Document::Type::General;
          }();

          auto parsed_document = td_->documents_manager_->on_get_document(
              {std::move(result->content_),
               get_web_document_photo_size(td_->file_manager_.get(), FileType::Thumbnail, DialogId(),
                                           std::move(result->thumb_)),
               std::move(attributes)},
              DialogId(), nullptr, default_document_type);
          auto file_id = parsed_document.file_id;
          if (!file_id.is_valid()) {
            continue;
          }
          if (result->type_ == "audio" && parsed_document.type == Document::Type::Audio) {
            auto audio = make_tl_object<td_api::inlineQueryResultAudio>();
            audio->id_ = std::move(result->id_);
            audio->audio_ = td_->audios_manager_->get_audio_object(file_id);
            if (!register_inline_message_content(results->query_id_, audio->id_, file_id,
                                                 std::move(result->send_message_), td_api::inputMessageAudio::ID)) {
              continue;
            }
            output_result = std::move(audio);
          } else if (result->type_ == "file" && parsed_document.type == Document::Type::General) {
            auto document = make_tl_object<td_api::inlineQueryResultDocument>();
            document->id_ = std::move(result->id_);
            document->document_ = td_->documents_manager_->get_document_object(file_id, PhotoFormat::Jpeg);
            document->title_ = std::move(result->title_);
            document->description_ = std::move(result->description_);
            if (!register_inline_message_content(results->query_id_, document->id_, file_id,
                                                 std::move(result->send_message_), td_api::inputMessageDocument::ID)) {
              continue;
            }
            output_result = std::move(document);
          } else if (is_animation && parsed_document.type == Document::Type::Animation) {
            auto animation = make_tl_object<td_api::inlineQueryResultAnimation>();
            animation->id_ = std::move(result->id_);
            animation->animation_ =
                td_->animations_manager_->get_animation_object(file_id, "inlineQueryResultAnimationCached");
            animation->title_ = std::move(result->title_);
            if (!register_inline_message_content(results->query_id_, animation->id_, file_id,
                                                 std::move(result->send_message_), td_api::inputMessageAnimation::ID)) {
              continue;
            }
            output_result = std::move(animation);
          } else if (result->type_ == "sticker" && parsed_document.type == Document::Type::Sticker) {
            auto sticker = make_tl_object<td_api::inlineQueryResultSticker>();
            sticker->id_ = std::move(result->id_);
            sticker->sticker_ = td_->stickers_manager_->get_sticker_object(file_id);
            if (!register_inline_message_content(results->query_id_, sticker->id_, file_id,
                                                 std::move(result->send_message_), td_api::inputMessageSticker::ID)) {
              continue;
            }
            output_result = std::move(sticker);
          } else if (result->type_ == "video" && parsed_document.type == Document::Type::Video) {
            auto video = make_tl_object<td_api::inlineQueryResultVideo>();
            video->id_ = std::move(result->id_);
            video->video_ = td_->videos_manager_->get_video_object(file_id);
            video->title_ = std::move(result->title_);
            video->description_ = std::move(result->description_);
            if (!register_inline_message_content(results->query_id_, video->id_, file_id,
                                                 std::move(result->send_message_), td_api::inputMessageVideo::ID)) {
              continue;
            }
            output_result = std::move(video);
          } else if (result->type_ == "voice" && parsed_document.type == Document::Type::VoiceNote) {
            auto voice_note = make_tl_object<td_api::inlineQueryResultVoiceNote>();
            voice_note->id_ = std::move(result->id_);
            voice_note->voice_note_ = td_->voice_notes_manager_->get_voice_note_object(file_id);
            voice_note->title_ = std::move(result->title_);
            if (!register_inline_message_content(results->query_id_, voice_note->id_, file_id,
                                                 std::move(result->send_message_), td_api::inputMessageVoiceNote::ID)) {
              continue;
            }
            output_result = std::move(voice_note);
          } else {
            LOG(WARNING) << "Unsupported inline query result " << to_string(result);
          }
        }
        break;
      }
      default:
        UNREACHABLE();
    }
    if (output_result != nullptr) {
      output_results.push_back(std::move(output_result));
    }
  }

  auto it = inline_query_results_.find(query_hash);
  CHECK(it != inline_query_results_.end());

  query_id_to_bot_user_id_[results->query_id_] = bot_user_id;

  string switch_pm_text;
  string switch_pm_parameter;
  if (results->switch_pm_ != nullptr) {
    switch_pm_text = std::move(results->switch_pm_->text_);
    switch_pm_parameter = std::move(results->switch_pm_->start_param_);
  }

  it->second.results = make_tl_object<td_api::inlineQueryResults>(
      results->query_id_, results->next_offset_, std::move(output_results), switch_pm_text, switch_pm_parameter);
  it->second.cache_expire_time = Time::now() + results->cache_time_;
}

vector<UserId> InlineQueriesManager::get_recent_inline_bots(Promise<Unit> &&promise) {
  if (!load_recently_used_bots(promise)) {
    return vector<UserId>();
  }

  promise.set_value(Unit());
  return recently_used_bot_user_ids_;
}

void InlineQueriesManager::save_recently_used_bots() {
  if (recently_used_bots_loaded_ < 2) {
    return;
  }

  string value;
  string value_ids;
  for (auto &bot_user_id : recently_used_bot_user_ids_) {
    if (!value.empty()) {
      value += ',';
      value_ids += ',';
    }
    value += td_->contacts_manager_->get_user_username(bot_user_id);
    value_ids += to_string(bot_user_id.get());
  }
  G()->td_db()->get_binlog_pmc()->set("recently_used_inline_bot_usernames", value);
  G()->td_db()->get_binlog_pmc()->set("recently_used_inline_bots", value_ids);
}

bool InlineQueriesManager::load_recently_used_bots(Promise<Unit> &promise) {
  if (recently_used_bots_loaded_ >= 2) {
    return true;
  }

  string saved_bot_ids = G()->td_db()->get_binlog_pmc()->get("recently_used_inline_bots");
  auto bot_ids = full_split(saved_bot_ids, ',');
  string saved_bots = G()->td_db()->get_binlog_pmc()->get("recently_used_inline_bot_usernames");
  auto bot_usernames = full_split(saved_bots, ',');
  if (bot_ids.empty() && bot_usernames.empty()) {
    recently_used_bots_loaded_ = 2;
    if (!recently_used_bot_user_ids_.empty()) {
      save_recently_used_bots();
    }
    return true;
  }

  LOG(INFO) << "Load recently used inline bots " << saved_bots << '/' << saved_bot_ids;
  if (recently_used_bots_loaded_ == 1 && resolve_recent_inline_bots_multipromise_.promise_count() == 0) {
    // queries was sent and have already been finished
    auto newly_used_bots = std::move(recently_used_bot_user_ids_);
    recently_used_bot_user_ids_.clear();

    if (bot_ids.empty()) {
      // legacy, can be removed in the future
      for (auto it = bot_usernames.rbegin(); it != bot_usernames.rend(); ++it) {
        auto dialog_id = td_->messages_manager_->resolve_dialog_username(*it);
        if (dialog_id.get_type() == DialogType::User) {
          update_bot_usage(dialog_id.get_user_id());
        }
      }
    } else {
      for (auto it = bot_ids.rbegin(); it != bot_ids.rend(); ++it) {
        UserId user_id(to_integer<int32>(*it));
        if (td_->contacts_manager_->have_user(user_id)) {
          update_bot_usage(user_id);
        } else {
          LOG(ERROR) << "Can't find " << user_id;
        }
      }
    }
    for (auto it = newly_used_bots.rbegin(); it != newly_used_bots.rend(); ++it) {
      update_bot_usage(*it);
    }
    recently_used_bots_loaded_ = 2;
    if (!newly_used_bots.empty() || (bot_ids.empty() && !bot_usernames.empty())) {
      save_recently_used_bots();
    }
    return true;
  }

  resolve_recent_inline_bots_multipromise_.add_promise(std::move(promise));
  if (recently_used_bots_loaded_ == 0) {
    resolve_recent_inline_bots_multipromise_.set_ignore_errors(true);
    if (bot_ids.empty() || !G()->parameters().use_chat_info_db) {
      for (auto &bot_username : bot_usernames) {
        td_->messages_manager_->search_public_dialog(bot_username, false,
                                                     resolve_recent_inline_bots_multipromise_.get_promise());
      }
    } else {
      for (auto &bot_id : bot_ids) {
        UserId user_id(to_integer<int32>(bot_id));
        td_->contacts_manager_->get_user(user_id, 3, resolve_recent_inline_bots_multipromise_.get_promise());
      }
    }
    recently_used_bots_loaded_ = 1;
  }
  return false;
}

tl_object_ptr<td_api::inlineQueryResults> InlineQueriesManager::get_inline_query_results_object(uint64 query_hash) {
  // TODO filter out games if request is sent in a broadcast channel or in a secret chat
  return decrease_pending_request_count(query_hash);
}

void InlineQueriesManager::on_new_query(int64 query_id, UserId sender_user_id, Location user_location,
                                        const string &query, const string &offset) {
  if (!sender_user_id.is_valid()) {
    LOG(ERROR) << "Receive new inline query from invalid " << sender_user_id;
    return;
  }
  LOG_IF(ERROR, !td_->contacts_manager_->have_user(sender_user_id)) << "Have no info about " << sender_user_id;
  if (!td_->auth_manager_->is_bot()) {
    LOG(ERROR) << "Receive new inline query";
    return;
  }
  send_closure(G()->td(), &Td::send_update,
               make_tl_object<td_api::updateNewInlineQuery>(
                   query_id, td_->contacts_manager_->get_user_id_object(sender_user_id, "updateNewInlineQuery"),
                   user_location.get_location_object(), query, offset));
}

void InlineQueriesManager::on_chosen_result(
    UserId user_id, Location user_location, const string &query, const string &result_id,
    tl_object_ptr<telegram_api::inputBotInlineMessageID> &&input_bot_inline_message_id) {
  if (!user_id.is_valid()) {
    LOG(ERROR) << "Receive chosen inline query result from invalid " << user_id;
    return;
  }
  LOG_IF(ERROR, !td_->contacts_manager_->have_user(user_id)) << "Have no info about " << user_id;
  if (!td_->auth_manager_->is_bot()) {
    LOG(ERROR) << "Receive chosen inline query result";
    return;
  }
  send_closure(G()->td(), &Td::send_update,
               make_tl_object<td_api::updateNewChosenInlineResult>(
                   td_->contacts_manager_->get_user_id_object(user_id, "updateNewChosenInlineResult"),
                   user_location.get_location_object(), query, result_id,
                   get_inline_message_id(std::move(input_bot_inline_message_id))));
}

bool InlineQueriesManager::update_bot_usage(UserId bot_user_id) {
  if (!bot_user_id.is_valid()) {
    return false;
  }
  if (!recently_used_bot_user_ids_.empty() && recently_used_bot_user_ids_[0] == bot_user_id) {
    return false;
  }
  auto r_bot_data = td_->contacts_manager_->get_bot_data(bot_user_id);
  if (r_bot_data.is_error()) {
    return false;
  }
  if (r_bot_data.ok().username.empty() || !r_bot_data.ok().is_inline) {
    return false;
  }

  auto it = std::find(recently_used_bot_user_ids_.begin(), recently_used_bot_user_ids_.end(), bot_user_id);
  if (it == recently_used_bot_user_ids_.end()) {
    if (static_cast<int32>(recently_used_bot_user_ids_.size()) == MAX_RECENT_INLINE_BOTS) {
      CHECK(!recently_used_bot_user_ids_.empty());
      recently_used_bot_user_ids_.back() = bot_user_id;
    } else {
      recently_used_bot_user_ids_.push_back(bot_user_id);
    }
    it = recently_used_bot_user_ids_.end() - 1;
  }
  std::rotate(recently_used_bot_user_ids_.begin(), it, it + 1);
  return true;
}

void InlineQueriesManager::remove_recent_inline_bot(UserId bot_user_id, Promise<Unit> &&promise) {
  if (td::remove(recently_used_bot_user_ids_, bot_user_id)) {
    save_recently_used_bots();
  }
  promise.set_value(Unit());
}

}  // namespace td
