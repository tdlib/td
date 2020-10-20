//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/ChannelId.h"
#include "td/telegram/ChatId.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/PtsManager.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UserId.h"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"
#include "td/actor/Timeout.h"

#include "td/utils/common.h"
#include "td/utils/logging.h"

#include <map>
#include <unordered_set>

namespace td {

extern int VERBOSITY_NAME(get_difference);

class Td;

class UpdatesManager : public Actor {
 public:
  UpdatesManager(Td *td, ActorShared<> parent);

  void on_get_updates(tl_object_ptr<telegram_api::Updates> &&updates_ptr);

  void on_get_updates_state(tl_object_ptr<telegram_api::updates_state> &&state, const char *source);

  void on_get_difference(tl_object_ptr<telegram_api::updates_Difference> &&difference_ptr);

  static std::unordered_set<int64> get_sent_messages_random_ids(const telegram_api::Updates *updates_ptr);

  static vector<const tl_object_ptr<telegram_api::Message> *> get_new_messages(
      const telegram_api::Updates *updates_ptr);

  static vector<DialogId> get_update_notify_settings_dialog_ids(const telegram_api::Updates *updates_ptr);

  static vector<DialogId> get_chat_dialog_ids(const telegram_api::Updates *updates_ptr);

  void get_difference(const char *source);

  void schedule_get_difference(const char *source);

  void init_state();

  void ping_server();

  void on_server_pong(tl_object_ptr<telegram_api::updates_state> &&state);

  int32 get_pts() const {
    return pts_manager_.mem_pts();
  }
  int32 get_qts() const {
    return qts_manager_.mem_pts();
  }
  int32 get_date() const {
    return date_;
  }

  Promise<> set_pts(int32 pts, const char *source) TD_WARN_UNUSED_RESULT;

  static const double MAX_UNFILLED_GAP_TIME;

  static void fill_pts_gap(void *td);

  bool running_get_difference() const {
    return running_get_difference_;
  }

 private:
  static constexpr int32 FORCED_GET_DIFFERENCE_PTS_DIFF = 100000;

  friend class OnUpdate;

  class PendingUpdates {
   public:
    int32 seq_begin;
    int32 seq_end;
    int32 date;
    vector<tl_object_ptr<telegram_api::Update>> updates;

    PendingUpdates(int32 seq_begin, int32 seq_end, int32 date, vector<tl_object_ptr<telegram_api::Update>> &&updates)
        : seq_begin(seq_begin), seq_end(seq_end), date(date), updates(std::move(updates)) {
    }
  };

  Td *td_;
  ActorShared<> parent_;

  PtsManager pts_manager_;
  PtsManager qts_manager_;
  int32 date_ = 0;
  int32 seq_ = 0;
  string date_source_ = "nowhere";

  int32 short_update_date_ = 0;

  std::multimap<int32, PendingUpdates> postponed_updates_;    // updates received during getDifference
  std::multimap<int32, PendingUpdates> pending_seq_updates_;  // updates with too big seq

  std::map<int32, tl_object_ptr<telegram_api::Update>> pending_qts_updates_;  // updates with too big qts

  Timeout seq_gap_timeout_;

  Timeout qts_gap_timeout_;

  int32 retry_time_ = 1;
  Timeout retry_timeout_;

  bool running_get_difference_ = false;
  int32 last_get_difference_pts_ = 0;

  void tear_down() override;

  Promise<> add_pts(int32 pts);
  void on_pts_ack(PtsManager::PtsId ack_token);
  void save_pts(int32 pts);

  Promise<> add_qts(int32 qts);
  void on_qts_ack(PtsManager::PtsId ack_token);
  void save_qts(int32 qts);

  void set_date(int32 date, bool from_update, string date_source);

  int32 get_short_update_date() const;

  void process_get_difference_updates(vector<tl_object_ptr<telegram_api::Message>> &&new_messages,
                                      vector<tl_object_ptr<telegram_api::EncryptedMessage>> &&new_encrypted_messages,
                                      vector<tl_object_ptr<telegram_api::Update>> &&other_updates);

  void on_pending_update(tl_object_ptr<telegram_api::Update> update, int32 seq, const char *source);

  void add_pending_qts_update(tl_object_ptr<telegram_api::Update> &&update, int32 qts);

  void on_pending_updates(vector<tl_object_ptr<telegram_api::Update>> &&updates, int32 seq_begin, int32 seq_end,
                          int32 date, const char *source);

  void process_updates(vector<tl_object_ptr<telegram_api::Update>> &&updates, bool force_apply);

  void process_seq_updates(int32 seq_end, int32 date, vector<tl_object_ptr<telegram_api::Update>> &&updates);

  void process_qts_update(tl_object_ptr<telegram_api::Update> &&update_ptr, int32 qts);

  void process_pending_seq_updates();

  void process_pending_qts_updates();

  static void fill_seq_gap(void *td);

  static void fill_qts_gap(void *td);

  static void fill_get_difference_gap(void *td);

  static void fill_gap(void *td, const char *source);

  void set_seq_gap_timeout(double timeout);

  void set_qts_gap_timeout(double timeout);

  void on_failed_get_difference();

  void before_get_difference(bool is_initial);

  void after_get_difference();

  static const vector<tl_object_ptr<telegram_api::Update>> *get_updates(const telegram_api::Updates *updates_ptr);

  bool is_acceptable_user(UserId user_id) const;

  bool is_acceptable_chat(ChatId chat_id) const;

  bool is_acceptable_channel(ChannelId channel_id) const;

  bool is_acceptable_peer(const tl_object_ptr<telegram_api::Peer> &peer) const;

  bool is_acceptable_message_entities(const vector<tl_object_ptr<telegram_api::MessageEntity>> &message_entities) const;

  bool is_acceptable_message_reply_header(
      const telegram_api::object_ptr<telegram_api::messageReplyHeader> &header) const;

  bool is_acceptable_message_forward_header(
      const telegram_api::object_ptr<telegram_api::messageFwdHeader> &header) const;

  bool is_acceptable_message(const telegram_api::Message *message_ptr) const;

  bool is_acceptable_update(const telegram_api::Update *update) const;

  void on_update(tl_object_ptr<telegram_api::updateNewMessage> update, bool force_apply);
  void on_update(tl_object_ptr<telegram_api::updateMessageID> update, bool force_apply);
  void on_update(tl_object_ptr<telegram_api::updateReadMessagesContents> update, bool force_apply);
  void on_update(tl_object_ptr<telegram_api::updateEditMessage> update, bool force_apply);
  void on_update(tl_object_ptr<telegram_api::updateDeleteMessages> update, bool force_apply);
  void on_update(tl_object_ptr<telegram_api::updateReadHistoryInbox> update, bool force_apply);
  void on_update(tl_object_ptr<telegram_api::updateReadHistoryOutbox> update, bool force_apply);
  void on_update(tl_object_ptr<telegram_api::updateNotifySettings> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updatePeerSettings> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updatePeerLocated> update, bool /*force_apply*/);

  void on_update(tl_object_ptr<telegram_api::updateWebPage> update, bool force_apply);
  void on_update(tl_object_ptr<telegram_api::updateChannelWebPage> update, bool force_apply);

  void on_update(tl_object_ptr<telegram_api::updateFolderPeers> update, bool /*force_apply*/);

  void on_update(tl_object_ptr<telegram_api::updateUserTyping> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updateChatUserTyping> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updateChannelUserTyping> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updateEncryptedChatTyping> update, bool /*force_apply*/);

  void on_update(tl_object_ptr<telegram_api::updateUserStatus> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updateUserName> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updateUserPhone> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updateUserPhoto> update, bool /*force_apply*/);

  void on_update(tl_object_ptr<telegram_api::updatePeerBlocked> update, bool /*force_apply*/);

  void on_update(tl_object_ptr<telegram_api::updateChatParticipants> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updateChatParticipantAdd> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updateChatParticipantAdmin> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updateChatParticipantDelete> update, bool /*force_apply*/);

  void on_update(tl_object_ptr<telegram_api::updateChatDefaultBannedRights> update, bool /*force_apply*/);

  void on_update(tl_object_ptr<telegram_api::updateServiceNotification> update, bool force_apply);

  void on_update(tl_object_ptr<telegram_api::updateDcOptions> update, bool /*force_apply*/);

  void on_update(tl_object_ptr<telegram_api::updateNewChannelMessage> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updateReadChannelInbox> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updateReadChannelOutbox> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updateChannelReadMessagesContents> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updateChannelTooLong> update, bool force_apply);
  void on_update(tl_object_ptr<telegram_api::updateChannel> update, bool force_apply);
  void on_update(tl_object_ptr<telegram_api::updateEditChannelMessage> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updateDeleteChannelMessages> update, bool force_apply);
  void on_update(tl_object_ptr<telegram_api::updateChannelMessageViews> update, bool force_apply);
  void on_update(tl_object_ptr<telegram_api::updateChannelMessageForwards> update, bool force_apply);
  void on_update(tl_object_ptr<telegram_api::updateChannelAvailableMessages> update, bool /*force_apply*/);

  void on_update(tl_object_ptr<telegram_api::updateReadChannelDiscussionInbox> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updateReadChannelDiscussionOutbox> update, bool /*force_apply*/);

  void on_update(tl_object_ptr<telegram_api::updatePinnedMessages> update, bool force_apply);
  void on_update(tl_object_ptr<telegram_api::updatePinnedChannelMessages> update, bool force_apply);

  void on_update(tl_object_ptr<telegram_api::updateDraftMessage> update, bool /*force_apply*/);

  void on_update(tl_object_ptr<telegram_api::updateDialogPinned> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updatePinnedDialogs> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updateDialogUnreadMark> update, bool /*force_apply*/);

  void on_update(tl_object_ptr<telegram_api::updateDialogFilter> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updateDialogFilters> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updateDialogFilterOrder> update, bool /*force_apply*/);

  void on_update(tl_object_ptr<telegram_api::updateBotInlineQuery> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updateBotInlineSend> update, bool /*force_apply*/);

  void on_update(tl_object_ptr<telegram_api::updateBotCallbackQuery> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updateInlineBotCallbackQuery> update, bool /*force_apply*/);

  void on_update(tl_object_ptr<telegram_api::updateFavedStickers> update, bool /*force_apply*/);

  void on_update(tl_object_ptr<telegram_api::updateSavedGifs> update, bool /*force_apply*/);

  void on_update(tl_object_ptr<telegram_api::updateConfig> update, bool /*force_apply*/);

  void on_update(tl_object_ptr<telegram_api::updatePtsChanged> update, bool /*force_apply*/);

  void on_update(tl_object_ptr<telegram_api::updatePrivacy> update, bool /*force_apply*/);

  void on_update(tl_object_ptr<telegram_api::updateEncryption> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updateNewEncryptedMessage> update, bool force_apply);
  void on_update(tl_object_ptr<telegram_api::updateEncryptedMessagesRead> update, bool /*force_apply*/);

  void on_update(tl_object_ptr<telegram_api::updateNewStickerSet> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updateStickerSets> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updateStickerSetsOrder> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updateReadFeaturedStickers> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updateRecentStickers> update, bool /*force_apply*/);

  void on_update(tl_object_ptr<telegram_api::updateBotShippingQuery> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updateBotPrecheckoutQuery> update, bool /*force_apply*/);

  void on_update(tl_object_ptr<telegram_api::updateBotWebhookJSON> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updateBotWebhookJSONQuery> update, bool /*force_apply*/);

  void on_update(tl_object_ptr<telegram_api::updatePhoneCall> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updatePhoneCallSignalingData> update, bool /*force_apply*/);

  void on_update(tl_object_ptr<telegram_api::updateContactsReset> update, bool /*force_apply*/);

  void on_update(tl_object_ptr<telegram_api::updateLangPackTooLong> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updateLangPack> update, bool /*force_apply*/);

  void on_update(tl_object_ptr<telegram_api::updateGeoLiveViewed> update, bool /*force_apply*/);

  void on_update(tl_object_ptr<telegram_api::updateMessagePoll> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updateMessagePollVote> update, bool /*force_apply*/);

  void on_update(tl_object_ptr<telegram_api::updateNewScheduledMessage> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updateDeleteScheduledMessages> update, bool /*force_apply*/);

  void on_update(tl_object_ptr<telegram_api::updateLoginToken> update, bool /*force_apply*/);

  void on_update(tl_object_ptr<telegram_api::updateChannelParticipant> update, bool /*force_apply*/);

  // unsupported updates

  void on_update(tl_object_ptr<telegram_api::updateTheme> update, bool /*force_apply*/);
};

}  // namespace td
