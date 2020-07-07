//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/Dependencies.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/files/FileId.h"
#include "td/telegram/FullMessageId.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/MessageContentType.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/Photo.h"
#include "td/telegram/ReplyMarkup.h"
#include "td/telegram/secret_api.h"
#include "td/telegram/SecretInputMedia.h"
#include "td/telegram/StickerSetId.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/TopDialogCategory.h"
#include "td/telegram/UserId.h"
#include "td/telegram/WebPageId.h"

#include "td/actor/PromiseFuture.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/Status.h"

#include <utility>

namespace td {

class Game;
struct Photo;
class Td;

class MultiPromiseActor;

// Do not forget to update merge_message_contents when one of the inheritors of this class changes
class MessageContent {
 public:
  MessageContent() = default;
  MessageContent(const MessageContent &) = default;
  MessageContent &operator=(const MessageContent &) = default;
  MessageContent(MessageContent &&) = default;
  MessageContent &operator=(MessageContent &&) = default;

  virtual MessageContentType get_type() const = 0;
  virtual ~MessageContent() = default;
};

struct InputMessageContent {
  unique_ptr<MessageContent> content;
  bool disable_web_page_preview = false;
  bool clear_draft = false;
  int32 ttl = 0;
  UserId via_bot_user_id;

  InputMessageContent(unique_ptr<MessageContent> &&content, bool disable_web_page_preview, bool clear_draft, int32 ttl,
                      UserId via_bot_user_id)
      : content(std::move(content))
      , disable_web_page_preview(disable_web_page_preview)
      , clear_draft(clear_draft)
      , ttl(ttl)
      , via_bot_user_id(via_bot_user_id) {
  }
};

struct InlineMessageContent {
  unique_ptr<MessageContent> message_content;
  unique_ptr<ReplyMarkup> message_reply_markup;
  bool disable_web_page_preview;
};

void store_message_content(const MessageContent *content, LogEventStorerCalcLength &storer);

void store_message_content(const MessageContent *content, LogEventStorerUnsafe &storer);

void parse_message_content(unique_ptr<MessageContent> &content, LogEventParser &parser);

InlineMessageContent create_inline_message_content(Td *td, FileId file_id,
                                                   tl_object_ptr<telegram_api::BotInlineMessage> &&inline_message,
                                                   int32 allowed_media_content_id, Photo *photo, Game *game);

unique_ptr<MessageContent> create_text_message_content(string text, vector<MessageEntity> entities,
                                                       WebPageId web_page_id);

unique_ptr<MessageContent> create_contact_registered_message_content();

unique_ptr<MessageContent> create_screenshot_taken_message_content();

unique_ptr<MessageContent> create_chat_set_ttl_message_content(int32 ttl);

Result<InputMessageContent> get_input_message_content(
    DialogId dialog_id, tl_object_ptr<td_api::InputMessageContent> &&input_message_content, Td *td);

bool can_have_input_media(const Td *td, const MessageContent *content);

SecretInputMedia get_secret_input_media(const MessageContent *content, Td *td,
                                        tl_object_ptr<telegram_api::InputEncryptedFile> input_file,
                                        BufferSlice thumbnail, int32 layer);

tl_object_ptr<telegram_api::InputMedia> get_input_media(const MessageContent *content, Td *td,
                                                        tl_object_ptr<telegram_api::InputFile> input_file,
                                                        tl_object_ptr<telegram_api::InputFile> input_thumbnail,
                                                        FileId file_id, FileId thumbnail_file_id, int32 ttl,
                                                        bool force);

tl_object_ptr<telegram_api::InputMedia> get_input_media(const MessageContent *content, Td *td, int32 ttl, bool force);

void delete_message_content_thumbnail(MessageContent *content, Td *td);

bool can_forward_message_content(const MessageContent *content);

bool update_opened_message_content(MessageContent *content);

int32 get_message_content_index_mask(const MessageContent *content, const Td *td, bool is_secret, bool is_outgoing);

MessageId get_message_content_pinned_message_id(const MessageContent *content);

MessageId get_message_content_replied_message_id(const MessageContent *content);

vector<UserId> get_message_content_added_user_ids(const MessageContent *content);

UserId get_message_content_deleted_user_id(const MessageContent *content);

int32 get_message_content_live_location_period(const MessageContent *content);

bool get_message_content_poll_is_closed(const Td *td, const MessageContent *content);

bool get_message_content_poll_is_anonymous(const Td *td, const MessageContent *content);

bool has_message_content_web_page(const MessageContent *content);

void remove_message_content_web_page(MessageContent *content);

void set_message_content_poll_answer(Td *td, const MessageContent *content, FullMessageId full_message_id,
                                     vector<int32> &&option_ids, Promise<Unit> &&promise);

void get_message_content_poll_voters(Td *td, const MessageContent *content, FullMessageId full_message_id,
                                     int32 option_id, int32 offset, int32 limit,
                                     Promise<std::pair<int32, vector<UserId>>> &&promise);

void stop_message_content_poll(Td *td, const MessageContent *content, FullMessageId full_message_id,
                               unique_ptr<ReplyMarkup> &&reply_markup, Promise<Unit> &&promise);

void merge_message_contents(Td *td, const MessageContent *old_content, MessageContent *new_content,
                            bool need_message_changed_warning, DialogId dialog_id, bool need_merge_files,
                            bool &is_content_changed, bool &need_update);

bool merge_message_content_file_id(Td *td, MessageContent *message_content, FileId new_file_id);

void register_message_content(Td *td, const MessageContent *content, FullMessageId full_message_id, const char *source);

void reregister_message_content(Td *td, const MessageContent *old_content, const MessageContent *new_content,
                                FullMessageId full_message_id, const char *source);

void unregister_message_content(Td *td, const MessageContent *content, FullMessageId full_message_id,
                                const char *source);

unique_ptr<MessageContent> get_secret_message_content(
    Td *td, string message_text, tl_object_ptr<telegram_api::encryptedFile> file,
    tl_object_ptr<secret_api::DecryptedMessageMedia> &&media,
    vector<tl_object_ptr<secret_api::MessageEntity>> &&secret_entities, DialogId owner_dialog_id,
    MultiPromiseActor &load_data_multipromise);

unique_ptr<MessageContent> get_message_content(Td *td, FormattedText message_text,
                                               tl_object_ptr<telegram_api::MessageMedia> &&media,
                                               DialogId owner_dialog_id, bool is_content_read, UserId via_bot_user_id,
                                               int32 *ttl);

enum class MessageContentDupType : int32 { Send, SendViaBot, Forward, Copy, CopyWithoutCaption };

unique_ptr<MessageContent> dup_message_content(Td *td, DialogId dialog_id, const MessageContent *content,
                                               MessageContentDupType type);

unique_ptr<MessageContent> get_action_message_content(Td *td, tl_object_ptr<telegram_api::MessageAction> &&action,
                                                      DialogId owner_dialog_id, MessageId reply_to_message_id);

tl_object_ptr<td_api::MessageContent> get_message_content_object(const MessageContent *content, Td *td,
                                                                 int32 message_date, bool is_content_secret);

const FormattedText *get_message_content_text(const MessageContent *content);

const FormattedText *get_message_content_caption(const MessageContent *content);

int32 get_message_content_duration(const MessageContent *content, const Td *td);

FileId get_message_content_upload_file_id(const MessageContent *content);

FileId get_message_content_any_file_id(const MessageContent *content);

void update_message_content_file_id_remote(MessageContent *content, FileId file_id);

FileId get_message_content_thumbnail_file_id(const MessageContent *content, const Td *td);

vector<FileId> get_message_content_file_ids(const MessageContent *content, const Td *td);

string get_message_content_search_text(const Td *td, const MessageContent *content);

bool need_reget_message_content(const MessageContent *content);

bool need_delay_message_content_notification(const MessageContent *content, UserId my_user_id);

void update_expired_message_content(unique_ptr<MessageContent> &content);

void update_failed_to_send_message_content(Td *td, unique_ptr<MessageContent> &content);

void add_message_content_dependencies(Dependencies &dependencies, const MessageContent *message_content);

void on_sent_message_content(Td *td, const MessageContent *content);

StickerSetId add_sticker_set(Td *td, tl_object_ptr<telegram_api::InputStickerSet> &&input_sticker_set);

void on_dialog_used(TopDialogCategory category, DialogId dialog_id, int32 date);

void update_used_hashtags(Td *td, const MessageContent *content);

}  // namespace td
