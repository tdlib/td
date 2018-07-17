//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/PtsManager.h"

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"
#include "td/actor/Timeout.h"

#include "td/utils/common.h"

#include <map>
#include <unordered_set>

namespace td {

class Td;

class UpdatesManager : public Actor {
 public:
  UpdatesManager(Td *td, ActorShared<> parent);

  void on_get_updates(tl_object_ptr<telegram_api::Updates> &&updates_ptr);

  void on_get_updates_state(tl_object_ptr<telegram_api::updates_state> &&state, const char *source);

  void on_get_difference(tl_object_ptr<telegram_api::updates_Difference> &&difference_ptr);

  std::unordered_set<int64> get_sent_messages_random_ids(const telegram_api::Updates *updates_ptr);

  vector<const tl_object_ptr<telegram_api::Message> *> get_new_messages(const telegram_api::Updates *updates_ptr);

  vector<DialogId> get_chats(const telegram_api::Updates *updates_ptr);

  void get_difference(const char *source);

  void schedule_get_difference(const char *source);

  void init_state();

  void ping_server();

  void on_server_pong(tl_object_ptr<telegram_api::updates_state> &&state);

  int32 get_pts() const {
    return pts_manager_.mem_pts();
  }
  int32 get_qts() const {
    return qts_;
  }
  int32 get_date() const {
    return date_;
  }

  string get_state() const;  // for debug purposes only

  Promise<> set_pts(int32 pts, const char *source) TD_WARN_UNUSED_RESULT;

  void set_qts(int32 qts);

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
  int32 qts_ = 0;
  int32 date_ = 0;
  int32 seq_ = 0;
  string date_source_ = "nowhere";

  std::multimap<int32, PendingUpdates> postponed_updates_;    // updates received during getDifference
  std::multimap<int32, PendingUpdates> pending_seq_updates_;  // updates with too big seq

  Timeout seq_gap_timeout_;

  int32 retry_time_ = 1;
  Timeout retry_timeout_;

  bool running_get_difference_ = false;
  int32 last_get_difference_pts_ = 0;

  class State {
   public:
    enum class Type : int32 {
      General,
      RunningGetUpdatesState,
      RunningGetDifference,
      ApplyingDifference,
      ApplyingDifferenceSlice,
      ApplyingUpdates,
      ApplyingSeqUpdates
    };
    Type type = Type::General;
    int32 pts = -1;
    int32 qts = -1;
    int32 date = -1;
  };

  State state_;  // for debug purposes only

  void set_state(State::Type type);  // for debug purposes only

  void tear_down() override;

  Promise<> add_pts(int32 pts);
  void on_pts_ack(PtsManager::PtsId ack_token);
  void save_pts(int32 pts);

  void set_date(int32 date, bool from_update, string date_source);

  static tl_object_ptr<td_api::ChatAction> convert_send_message_action(
      tl_object_ptr<telegram_api::SendMessageAction> action);

  void process_get_difference_updates(vector<tl_object_ptr<telegram_api::Message>> &&new_messages,
                                      vector<tl_object_ptr<telegram_api::EncryptedMessage>> &&new_encrypted_messages,
                                      int32 qts, vector<tl_object_ptr<telegram_api::Update>> &&other_updates);

  void on_pending_update(tl_object_ptr<telegram_api::Update> update, int32 seq, const char *source);

  void on_pending_updates(vector<tl_object_ptr<telegram_api::Update>> &&updates, int32 seq_begin, int32 seq_end,
                          int32 date, const char *source);

  void process_updates(vector<tl_object_ptr<telegram_api::Update>> &&updates, bool force_apply);

  void process_seq_updates(int32 seq_end, int32 date, vector<tl_object_ptr<telegram_api::Update>> &&updates);

  void process_pending_seq_updates();

  static void fill_seq_gap(void *td);

  static void fill_get_difference_gap(void *td);

  static void fill_gap(void *td, const char *source);

  void set_seq_gap_timeout(double timeout);

  void on_failed_get_difference();

  void before_get_difference();

  void after_get_difference();

  bool is_acceptable_message_entities(const vector<tl_object_ptr<telegram_api::MessageEntity>> &message_entities) const;

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

  void on_update(tl_object_ptr<telegram_api::updateWebPage> update, bool force_apply);
  void on_update(tl_object_ptr<telegram_api::updateChannelWebPage> update, bool force_apply);

  void on_update(tl_object_ptr<telegram_api::updateUserTyping> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updateChatUserTyping> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updateEncryptedChatTyping> update, bool /*force_apply*/);

  void on_update(tl_object_ptr<telegram_api::updateUserStatus> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updateUserName> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updateUserPhone> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updateUserPhoto> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updateUserBlocked> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updateContactLink> update, bool /*force_apply*/);

  void on_update(tl_object_ptr<telegram_api::updateChatParticipants> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updateChatParticipantAdd> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updateChatParticipantAdmin> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updateChatParticipantDelete> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updateChatAdmins> update, bool /*force_apply*/);

  void on_update(tl_object_ptr<telegram_api::updateServiceNotification> update, bool force_apply);
  void on_update(tl_object_ptr<telegram_api::updateContactRegistered> update, bool /*force_apply*/);

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
  void on_update(tl_object_ptr<telegram_api::updateChannelPinnedMessage> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updateChannelAvailableMessages> update, bool /*force_apply*/);

  void on_update(tl_object_ptr<telegram_api::updateDraftMessage> update, bool /*force_apply*/);

  void on_update(tl_object_ptr<telegram_api::updateDialogPinned> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updatePinnedDialogs> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updateDialogUnreadMark> update, bool /*force_apply*/);

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
  void on_update(tl_object_ptr<telegram_api::updateNewEncryptedMessage> update, bool /*force_apply*/);
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

  void on_update(tl_object_ptr<telegram_api::updateContactsReset> update, bool /*force_apply*/);

  void on_update(tl_object_ptr<telegram_api::updateLangPackTooLong> update, bool /*force_apply*/);
  void on_update(tl_object_ptr<telegram_api::updateLangPack> update, bool /*force_apply*/);

  // unsupported updates
};

}  // namespace td
