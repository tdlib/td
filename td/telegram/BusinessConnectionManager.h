//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/BusinessConnectionId.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/files/FileId.h"
#include "td/telegram/MessageInputReplyTo.h"
#include "td/telegram/net/DcId.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/Promise.h"
#include "td/utils/Status.h"
#include "td/utils/WaitFreeHashMap.h"

#include <memory>

namespace td {

struct InputMessageContent;
struct ReplyMarkup;
class Td;

class BusinessConnectionManager final : public Actor {
 public:
  BusinessConnectionManager(Td *td, ActorShared<> parent);
  BusinessConnectionManager(const BusinessConnectionManager &) = delete;
  BusinessConnectionManager &operator=(const BusinessConnectionManager &) = delete;
  BusinessConnectionManager(BusinessConnectionManager &&) = delete;
  BusinessConnectionManager &operator=(BusinessConnectionManager &&) = delete;
  ~BusinessConnectionManager() final;

  Status check_business_connection(const BusinessConnectionId &connection_id, DialogId dialog_id) const;

  DcId get_business_connection_dc_id(const BusinessConnectionId &connection_id) const;

  void on_update_bot_business_connect(telegram_api::object_ptr<telegram_api::botBusinessConnection> &&connection);

  void on_update_bot_new_business_message(const BusinessConnectionId &connection_id,
                                          telegram_api::object_ptr<telegram_api::Message> &&message);

  void on_update_bot_edit_business_message(const BusinessConnectionId &connection_id,
                                           telegram_api::object_ptr<telegram_api::Message> &&message);

  void on_update_bot_delete_business_messages(const BusinessConnectionId &connection_id, DialogId dialog_id,
                                              vector<int32> &&messages);

  void get_business_connection(const BusinessConnectionId &connection_id,
                               Promise<td_api::object_ptr<td_api::businessConnection>> &&promise);

  void send_message(BusinessConnectionId business_connection_id, DialogId dialog_id,
                    td_api::object_ptr<td_api::InputMessageReplyTo> &&reply_to, bool disable_notification,
                    bool protect_content, td_api::object_ptr<td_api::ReplyMarkup> &&reply_markup,
                    td_api::object_ptr<td_api::InputMessageContent> &&input_message_content,
                    Promise<td_api::object_ptr<td_api::message>> &&promise);

 private:
  struct BusinessConnection;
  struct PendingMessage;
  class SendBusinessMessageQuery;
  class SendBusinessMediaQuery;
  class UploadBusinessMediaQuery;
  class UploadMediaCallback;
  class UploadThumbnailCallback;

  struct UploadMediaResult {
    unique_ptr<PendingMessage> message_;
    telegram_api::object_ptr<telegram_api::InputMedia> input_media_;
  };

  struct BeingUploadedMedia {
    unique_ptr<PendingMessage> message_;
    telegram_api::object_ptr<telegram_api::InputFile> input_file_;
    Promise<UploadMediaResult> promise_;
  };

  void tear_down() final;

  void on_get_business_connection(const BusinessConnectionId &connection_id,
                                  Result<telegram_api::object_ptr<telegram_api::Updates>> r_updates);

  MessageInputReplyTo create_business_message_input_reply_to(
      td_api::object_ptr<td_api::InputMessageReplyTo> &&reply_to);

  Result<InputMessageContent> process_input_message_content(
      DialogId dialog_id, td_api::object_ptr<td_api::InputMessageContent> &&input_message_content);

  unique_ptr<PendingMessage> create_business_message_to_send(BusinessConnectionId business_connection_id,
                                                             DialogId dialog_id, MessageInputReplyTo &&input_reply_to,
                                                             bool disable_notification, bool protect_content,
                                                             unique_ptr<ReplyMarkup> &&reply_markup,
                                                             InputMessageContent &&input_content) const;

  void do_send_message(unique_ptr<PendingMessage> &&message, Promise<td_api::object_ptr<td_api::message>> &&promise);

  static FileId get_message_file_id(const unique_ptr<PendingMessage> &message);

  FileId get_message_thumbnail_file_id(const unique_ptr<PendingMessage> &message, FileId file_id) const;

  void upload_media(unique_ptr<PendingMessage> &&message, Promise<UploadMediaResult> &&promise,
                    vector<int> bad_parts = {});

  void complete_send_media(unique_ptr<PendingMessage> &&message,
                           telegram_api::object_ptr<telegram_api::InputMedia> &&input_media,
                           Promise<td_api::object_ptr<td_api::message>> &&promise);

  void on_upload_media(FileId file_id, telegram_api::object_ptr<telegram_api::InputFile> input_file);

  void on_upload_media_error(FileId file_id, Status status);

  void on_upload_thumbnail(FileId thumbnail_file_id,
                           telegram_api::object_ptr<telegram_api::InputFile> thumbnail_input_file);

  void do_upload_media(BeingUploadedMedia &&being_uploaded_media,
                       telegram_api::object_ptr<telegram_api::InputFile> input_thumbnail);

  void complete_upload_media(unique_ptr<PendingMessage> &&message,
                             telegram_api::object_ptr<telegram_api::MessageMedia> &&media,
                             Promise<UploadMediaResult> &&promise);

  WaitFreeHashMap<BusinessConnectionId, unique_ptr<BusinessConnection>, BusinessConnectionIdHash> business_connections_;

  FlatHashMap<BusinessConnectionId, vector<Promise<td_api::object_ptr<td_api::businessConnection>>>,
              BusinessConnectionIdHash>
      get_business_connection_queries_;

  std::shared_ptr<UploadMediaCallback> upload_media_callback_;
  std::shared_ptr<UploadThumbnailCallback> upload_thumbnail_callback_;

  FlatHashMap<FileId, BeingUploadedMedia, FileIdHash> being_uploaded_files_;
  FlatHashMap<FileId, BeingUploadedMedia, FileIdHash> being_uploaded_thumbnails_;

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
