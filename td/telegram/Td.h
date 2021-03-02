//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/files/FileId.h"
#include "td/telegram/net/MtprotoHeader.h"
#include "td/telegram/net/NetQuery.h"
#include "td/telegram/net/NetQueryStats.h"
#include "td/telegram/StateManager.h"
#include "td/telegram/TdCallback.h"
#include "td/telegram/TdParameters.h"
#include "td/telegram/TermsOfService.h"

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"
#include "td/actor/Timeout.h"

#include "td/db/DbKey.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/Container.h"
#include "td/utils/logging.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace td {

class AnimationsManager;
class AudiosManager;
class AuthManager;
class BackgroundManager;
class CallManager;
class CallbackQueriesManager;
class ConfigManager;
class ContactsManager;
class CountryInfoManager;
class DeviceTokenManager;
class DocumentsManager;
class FileManager;
class FileReferenceManager;
class GroupCallManager;
class InlineQueriesManager;
class HashtagHints;
class LanguagePackManager;
class MessagesManager;
class NetStatsManager;
class NotificationManager;
class PasswordManager;
class PhoneNumberManager;
class PollManager;
class PrivacyManager;
class SecureManager;
class SecretChatsManager;
class StickersManager;
class StorageManager;
class TopDialogManager;
class UpdatesManager;
class VideoNotesManager;
class VideosManager;
class VoiceNotesManager;
class WebPagesManager;

}  // namespace td

namespace td {

extern int VERBOSITY_NAME(td_init);
extern int VERBOSITY_NAME(td_requests);

// Td may start closing after explicit "close" or "destroy" query.
// Or it may start closing by itself, because authorization is lost.
// It any case the parent will be notified via updateAuthorizationState.
//
// Td needs a way to know that it will receive no more queries.
// It happens after "hangup".
//
// Parent needs a way to know that it will receive no more updates.
// It happens after destruction of callback or after on_closed.
class Td final : public NetQueryCallback {
 public:
  Td(const Td &) = delete;
  Td(Td &&) = delete;
  Td &operator=(const Td &) = delete;
  Td &operator=(Td &&) = delete;
  ~Td() override;

  struct Options {
    std::shared_ptr<NetQueryStats> net_query_stats;
  };

  Td(unique_ptr<TdCallback> callback, Options options);

  void request(uint64 id, tl_object_ptr<td_api::Function> function);

  void destroy();

  void schedule_get_terms_of_service(int32 expires_in);

  void schedule_get_promo_data(int32 expires_in);

  void on_result(NetQueryPtr query) override;

  void on_update_server_time_difference();

  void on_authorization_lost();

  void on_online_updated(bool force, bool send_update);
  void on_update_status_success(bool is_online);

  void on_channel_unban_timeout(int64 channel_id_long);

  bool is_online() const;

  void set_is_bot_online(bool is_bot_online);

  template <class ActorT, class... ArgsT>
  ActorId<ActorT> create_net_actor(ArgsT &&... args) {
    auto slot_id = request_actors_.create(ActorOwn<>(), RequestActorIdType);
    inc_request_actor_refcnt();
    auto actor = make_unique<ActorT>(std::forward<ArgsT>(args)...);
    actor->set_parent(actor_shared(this, slot_id));

    auto actor_own = register_actor("net_actor", std::move(actor));
    auto actor_id = actor_own.get();
    *request_actors_.get(slot_id) = std::move(actor_own);
    return actor_id;
  }

  unique_ptr<AudiosManager> audios_manager_;
  unique_ptr<CallbackQueriesManager> callback_queries_manager_;
  unique_ptr<DocumentsManager> documents_manager_;
  unique_ptr<VideoNotesManager> video_notes_manager_;
  unique_ptr<VideosManager> videos_manager_;
  unique_ptr<VoiceNotesManager> voice_notes_manager_;

  unique_ptr<AnimationsManager> animations_manager_;
  ActorOwn<AnimationsManager> animations_manager_actor_;
  unique_ptr<AuthManager> auth_manager_;
  ActorOwn<AuthManager> auth_manager_actor_;
  unique_ptr<BackgroundManager> background_manager_;
  ActorOwn<BackgroundManager> background_manager_actor_;
  unique_ptr<ContactsManager> contacts_manager_;
  ActorOwn<ContactsManager> contacts_manager_actor_;
  unique_ptr<CountryInfoManager> country_info_manager_;
  ActorOwn<CountryInfoManager> country_info_manager_actor_;
  unique_ptr<FileManager> file_manager_;
  ActorOwn<FileManager> file_manager_actor_;
  unique_ptr<FileReferenceManager> file_reference_manager_;
  ActorOwn<FileReferenceManager> file_reference_manager_actor_;
  unique_ptr<GroupCallManager> group_call_manager_;
  ActorOwn<GroupCallManager> group_call_manager_actor_;
  unique_ptr<InlineQueriesManager> inline_queries_manager_;
  ActorOwn<InlineQueriesManager> inline_queries_manager_actor_;
  unique_ptr<MessagesManager> messages_manager_;
  ActorOwn<MessagesManager> messages_manager_actor_;
  unique_ptr<NotificationManager> notification_manager_;
  ActorOwn<NotificationManager> notification_manager_actor_;
  unique_ptr<PollManager> poll_manager_;
  ActorOwn<PollManager> poll_manager_actor_;
  unique_ptr<StickersManager> stickers_manager_;
  ActorOwn<StickersManager> stickers_manager_actor_;
  unique_ptr<UpdatesManager> updates_manager_;
  ActorOwn<UpdatesManager> updates_manager_actor_;
  unique_ptr<WebPagesManager> web_pages_manager_;
  ActorOwn<WebPagesManager> web_pages_manager_actor_;

  ActorOwn<CallManager> call_manager_;
  ActorOwn<PhoneNumberManager> change_phone_number_manager_;
  ActorOwn<ConfigManager> config_manager_;
  ActorOwn<PhoneNumberManager> confirm_phone_number_manager_;
  ActorOwn<DeviceTokenManager> device_token_manager_;
  ActorOwn<HashtagHints> hashtag_hints_;
  ActorOwn<LanguagePackManager> language_pack_manager_;
  ActorOwn<NetStatsManager> net_stats_manager_;
  ActorOwn<PasswordManager> password_manager_;
  ActorOwn<PrivacyManager> privacy_manager_;
  ActorOwn<SecureManager> secure_manager_;
  ActorOwn<SecretChatsManager> secret_chats_manager_;
  ActorOwn<StateManager> state_manager_;
  ActorOwn<StorageManager> storage_manager_;
  ActorOwn<TopDialogManager> top_dialog_manager_;
  ActorOwn<PhoneNumberManager> verify_phone_number_manager_;

  class ResultHandler : public std::enable_shared_from_this<ResultHandler> {
   public:
    ResultHandler() = default;
    ResultHandler(const ResultHandler &) = delete;
    ResultHandler &operator=(const ResultHandler &) = delete;
    virtual ~ResultHandler() = default;

    virtual void on_result(NetQueryPtr query);
    virtual void on_result(uint64 id, BufferSlice packet) {
      UNREACHABLE();
    }
    virtual void on_error(uint64 id, Status status) {
      UNREACHABLE();
    }

    friend class Td;

   protected:
    void send_query(NetQueryPtr query);

    Td *td = nullptr;

   private:
    void set_td(Td *new_td);
  };

  template <class HandlerT, class... Args>
  std::shared_ptr<HandlerT> create_handler(Args &&... args) {
    LOG_CHECK(close_flag_ < 2) << close_flag_
#if TD_CLANG || TD_GCC
                               << ' ' << __PRETTY_FUNCTION__
#endif
        ;
    auto ptr = std::make_shared<HandlerT>(std::forward<Args>(args)...);
    ptr->set_td(this);
    return ptr;
  }

  void send_update(tl_object_ptr<td_api::Update> &&object);

  static td_api::object_ptr<td_api::Object> static_request(td_api::object_ptr<td_api::Function> function);

 private:
  static constexpr const char *TDLIB_VERSION = "1.7.2";
  static constexpr int64 ONLINE_ALARM_ID = 0;
  static constexpr int64 PING_SERVER_ALARM_ID = -1;
  static constexpr int32 PING_SERVER_TIMEOUT = 300;
  static constexpr int64 TERMS_OF_SERVICE_ALARM_ID = -2;
  static constexpr int64 PROMO_DATA_ALARM_ID = -3;

  void on_connection_state_changed(StateManager::State new_state);

  void send_result(uint64 id, tl_object_ptr<td_api::Object> object);
  void send_error(uint64 id, Status error);
  void send_error_impl(uint64 id, tl_object_ptr<td_api::error> error);
  void send_error_raw(uint64 id, int32 code, CSlice error);
  void answer_ok_query(uint64 id, Status status);

  ActorShared<Td> create_reference();

  void inc_actor_refcnt();
  void dec_actor_refcnt();

  void inc_request_actor_refcnt();
  void dec_request_actor_refcnt();

  void close();
  void on_closed();

  void dec_stop_cnt();

  unique_ptr<TdCallback> callback_;
  Options td_options_;

  MtprotoHeader::Options options_;

  TdParameters parameters_;

  StateManager::State connection_state_;

  std::unordered_multiset<uint64> request_set_;
  int actor_refcnt_ = 0;
  int request_actor_refcnt_ = 0;
  int stop_cnt_ = 2;
  bool destroy_flag_ = false;
  int close_flag_ = 0;

  enum class State : int32 { WaitParameters, Decrypt, Run, Close } state_ = State::WaitParameters;
  bool is_database_encrypted_ = false;

  vector<std::pair<uint64, std::shared_ptr<ResultHandler>>> result_handlers_;
  enum : int8 { RequestActorIdType = 1, ActorIdType = 2 };
  Container<ActorOwn<Actor>> request_actors_;

  bool is_online_ = false;
  bool is_bot_online_ = false;
  NetQueryRef update_status_query_;

  int64 alarm_id_ = 1;
  std::unordered_map<int64, uint64> pending_alarms_;
  MultiTimeout alarm_timeout_{"AlarmTimeout"};

  TermsOfService pending_terms_of_service_;

  double last_sent_server_time_difference_ = 1e100;

  struct DownloadInfo {
    int32 offset = -1;
    int32 limit = -1;
    vector<uint64> request_ids;
  };
  std::unordered_map<FileId, DownloadInfo, FileIdHash> pending_file_downloads_;

  vector<std::pair<uint64, td_api::object_ptr<td_api::Function>>> pending_preauthentication_requests_;

  template <class T>
  void complete_pending_preauthentication_requests(const T &func);

  td_api::object_ptr<td_api::AuthorizationState> get_fake_authorization_state_object() const;

  static void on_alarm_timeout_callback(void *td_ptr, int64 alarm_id);
  void on_alarm_timeout(int64 alarm_id);

  td_api::object_ptr<td_api::updateTermsOfService> get_update_terms_of_service_object() const;

  void on_get_terms_of_service(Result<std::pair<int32, TermsOfService>> result, bool dummy);

  void on_get_promo_data(Result<telegram_api::object_ptr<telegram_api::help_PromoData>> result, bool dummy);

  template <class T>
  friend class RequestActor;        // uses send_result/send_error
  friend class TestQuery;           // uses send_result/send_error, TODO pass Promise<>
  friend class AuthManager;         // uses send_result/send_error, TODO pass Promise<>
  friend class PhoneNumberManager;  // uses send_result/send_error, TODO pass Promise<>

  void add_handler(uint64 id, std::shared_ptr<ResultHandler> handler);
  std::shared_ptr<ResultHandler> extract_handler(uint64 id);
  void invalidate_handler(ResultHandler *handler);
  void clear_handlers();
  // void destroy_handler(ResultHandler *handler);

  void clear_requests();

  void on_file_download_finished(FileId file_id);

  static bool is_internal_config_option(Slice name);

  void on_config_option_updated(const string &name);

  static tl_object_ptr<td_api::ConnectionState> get_connection_state_object(StateManager::State state);

  void send(NetQueryPtr &&query);

  class OnRequest;

  class DownloadFileCallback;

  std::shared_ptr<DownloadFileCallback> download_file_callback_;

  class UploadFileCallback;

  std::shared_ptr<UploadFileCallback> upload_file_callback_;

  std::shared_ptr<ActorContext> old_context_;

  static int *get_log_verbosity_level(Slice name);

  template <class T>
  Promise<T> create_request_promise(uint64 id) {
    return PromiseCreator::lambda([id = id, actor_id = actor_id(this)](Result<T> r_state) {
      if (r_state.is_error()) {
        send_closure(actor_id, &Td::send_error, id, r_state.move_as_error());
      } else {
        send_closure(actor_id, &Td::send_result, id, r_state.move_as_ok());
      }
    });
  }

  Promise<Unit> create_ok_request_promise(uint64 id);

  static bool is_authentication_request(int32 id);

  static bool is_synchronous_request(int32 id);

  static bool is_preinitialization_request(int32 id);

  static bool is_preauthentication_request(int32 id);

  template <class T>
  void on_request(uint64 id, const T &request) = delete;

  void on_request(uint64 id, const td_api::setTdlibParameters &request);

  void on_request(uint64 id, const td_api::checkDatabaseEncryptionKey &request);

  void on_request(uint64 id, td_api::setDatabaseEncryptionKey &request);

  void on_request(uint64 id, const td_api::getAuthorizationState &request);

  void on_request(uint64 id, td_api::setAuthenticationPhoneNumber &request);

  void on_request(uint64 id, const td_api::resendAuthenticationCode &request);

  void on_request(uint64 id, td_api::checkAuthenticationCode &request);

  void on_request(uint64 id, td_api::registerUser &request);

  void on_request(uint64 id, td_api::requestQrCodeAuthentication &request);

  void on_request(uint64 id, td_api::checkAuthenticationPassword &request);

  void on_request(uint64 id, const td_api::requestAuthenticationPasswordRecovery &request);

  void on_request(uint64 id, td_api::recoverAuthenticationPassword &request);

  void on_request(uint64 id, const td_api::logOut &request);

  void on_request(uint64 id, const td_api::close &request);

  void on_request(uint64 id, const td_api::destroy &request);

  void on_request(uint64 id, td_api::checkAuthenticationBotToken &request);

  void on_request(uint64 id, td_api::confirmQrCodeAuthentication &request);

  void on_request(uint64 id, const td_api::getCurrentState &request);

  void on_request(uint64 id, td_api::getPasswordState &request);

  void on_request(uint64 id, td_api::setPassword &request);

  void on_request(uint64 id, td_api::getRecoveryEmailAddress &request);

  void on_request(uint64 id, td_api::setRecoveryEmailAddress &request);

  void on_request(uint64 id, td_api::checkRecoveryEmailAddressCode &request);

  void on_request(uint64 id, const td_api::resendRecoveryEmailAddressCode &request);

  void on_request(uint64 id, td_api::requestPasswordRecovery &request);

  void on_request(uint64 id, td_api::recoverPassword &request);

  void on_request(uint64 id, td_api::getTemporaryPasswordState &request);

  void on_request(uint64 id, td_api::createTemporaryPassword &request);

  void on_request(uint64 id, td_api::processPushNotification &request);

  void on_request(uint64 id, td_api::registerDevice &request);

  void on_request(uint64 id, td_api::getUserPrivacySettingRules &request);

  void on_request(uint64 id, td_api::setUserPrivacySettingRules &request);

  void on_request(uint64 id, const td_api::getAccountTtl &request);

  void on_request(uint64 id, const td_api::setAccountTtl &request);

  void on_request(uint64 id, td_api::deleteAccount &request);

  void on_request(uint64 id, td_api::changePhoneNumber &request);

  void on_request(uint64 id, td_api::checkChangePhoneNumberCode &request);

  void on_request(uint64 id, td_api::resendChangePhoneNumberCode &request);

  void on_request(uint64 id, const td_api::getActiveSessions &request);

  void on_request(uint64 id, const td_api::terminateSession &request);

  void on_request(uint64 id, const td_api::terminateAllOtherSessions &request);

  void on_request(uint64 id, const td_api::getConnectedWebsites &request);

  void on_request(uint64 id, const td_api::disconnectWebsite &request);

  void on_request(uint64 id, const td_api::disconnectAllWebsites &request);

  void on_request(uint64 id, const td_api::getMe &request);

  void on_request(uint64 id, const td_api::getUser &request);

  void on_request(uint64 id, const td_api::getUserFullInfo &request);

  void on_request(uint64 id, const td_api::getBasicGroup &request);

  void on_request(uint64 id, const td_api::getBasicGroupFullInfo &request);

  void on_request(uint64 id, const td_api::getSupergroup &request);

  void on_request(uint64 id, const td_api::getSupergroupFullInfo &request);

  void on_request(uint64 id, const td_api::getSecretChat &request);

  void on_request(uint64 id, const td_api::getChat &request);

  void on_request(uint64 id, const td_api::getMessage &request);

  void on_request(uint64 id, const td_api::getMessageLocally &request);

  void on_request(uint64 id, const td_api::getRepliedMessage &request);

  void on_request(uint64 id, const td_api::getChatPinnedMessage &request);

  void on_request(uint64 id, const td_api::getCallbackQueryMessage &request);

  void on_request(uint64 id, const td_api::getMessageThread &request);

  void on_request(uint64 id, const td_api::getMessages &request);

  void on_request(uint64 id, const td_api::getMessageLink &request);

  void on_request(uint64 id, const td_api::getMessageEmbeddingCode &request);

  void on_request(uint64 id, td_api::getMessageLinkInfo &request);

  void on_request(uint64 id, const td_api::getFile &request);

  void on_request(uint64 id, td_api::getRemoteFile &request);

  void on_request(uint64 id, td_api::getStorageStatistics &request);

  void on_request(uint64 id, td_api::getStorageStatisticsFast &request);

  void on_request(uint64 id, td_api::getDatabaseStatistics &request);

  void on_request(uint64 id, td_api::optimizeStorage &request);

  void on_request(uint64 id, td_api::getNetworkStatistics &request);

  void on_request(uint64 id, td_api::resetNetworkStatistics &request);

  void on_request(uint64 id, td_api::addNetworkStatistics &request);

  void on_request(uint64 id, const td_api::setNetworkType &request);

  void on_request(uint64 id, const td_api::getAutoDownloadSettingsPresets &request);

  void on_request(uint64 id, const td_api::setAutoDownloadSettings &request);

  void on_request(uint64 id, td_api::getTopChats &request);

  void on_request(uint64 id, const td_api::removeTopChat &request);

  void on_request(uint64 id, const td_api::getChats &request);

  void on_request(uint64 id, td_api::searchPublicChat &request);

  void on_request(uint64 id, td_api::searchPublicChats &request);

  void on_request(uint64 id, td_api::searchChats &request);

  void on_request(uint64 id, td_api::searchChatsOnServer &request);

  void on_request(uint64 id, const td_api::searchChatsNearby &request);

  void on_request(uint64 id, const td_api::addRecentlyFoundChat &request);

  void on_request(uint64 id, const td_api::removeRecentlyFoundChat &request);

  void on_request(uint64 id, const td_api::clearRecentlyFoundChats &request);

  void on_request(uint64 id, const td_api::getGroupsInCommon &request);

  void on_request(uint64 id, td_api::checkChatUsername &request);

  void on_request(uint64 id, const td_api::getCreatedPublicChats &request);

  void on_request(uint64 id, const td_api::checkCreatedPublicChatsLimit &request);

  void on_request(uint64 id, const td_api::getSuitableDiscussionChats &request);

  void on_request(uint64 id, const td_api::getInactiveSupergroupChats &request);

  void on_request(uint64 id, const td_api::openChat &request);

  void on_request(uint64 id, const td_api::closeChat &request);

  void on_request(uint64 id, const td_api::viewMessages &request);

  void on_request(uint64 id, const td_api::openMessageContent &request);

  void on_request(uint64 id, td_api::getExternalLink &request);

  void on_request(uint64 id, const td_api::getChatHistory &request);

  void on_request(uint64 id, const td_api::deleteChatHistory &request);

  void on_request(uint64 id, const td_api::deleteChat &request);

  void on_request(uint64 id, const td_api::getMessageThreadHistory &request);

  void on_request(uint64 id, td_api::searchChatMessages &request);

  void on_request(uint64 id, td_api::searchSecretMessages &request);

  void on_request(uint64 id, td_api::searchMessages &request);

  void on_request(uint64 id, td_api::searchCallMessages &request);

  void on_request(uint64 id, const td_api::deleteAllCallMessages &request);

  void on_request(uint64 id, const td_api::searchChatRecentLocationMessages &request);

  void on_request(uint64 id, const td_api::getActiveLiveLocationMessages &request);

  void on_request(uint64 id, const td_api::getChatMessageByDate &request);

  void on_request(uint64 id, td_api::getChatMessageCount &request);

  void on_request(uint64 id, const td_api::getChatScheduledMessages &request);

  void on_request(uint64 id, td_api::getMessagePublicForwards &request);

  void on_request(uint64 id, const td_api::removeNotification &request);

  void on_request(uint64 id, const td_api::removeNotificationGroup &request);

  void on_request(uint64 id, const td_api::deleteMessages &request);

  void on_request(uint64 id, const td_api::deleteChatMessagesFromUser &request);

  void on_request(uint64 id, const td_api::readAllChatMentions &request);

  void on_request(uint64 id, td_api::sendMessage &request);

  void on_request(uint64 id, td_api::sendMessageAlbum &request);

  void on_request(uint64 id, td_api::sendBotStartMessage &request);

  void on_request(uint64 id, td_api::sendInlineQueryResultMessage &request);

  void on_request(uint64 id, td_api::addLocalMessage &request);

  void on_request(uint64 id, td_api::editMessageText &request);

  void on_request(uint64 id, td_api::editMessageLiveLocation &request);

  void on_request(uint64 id, td_api::editMessageMedia &request);

  void on_request(uint64 id, td_api::editMessageCaption &request);

  void on_request(uint64 id, td_api::editMessageReplyMarkup &request);

  void on_request(uint64 id, td_api::editInlineMessageText &request);

  void on_request(uint64 id, td_api::editInlineMessageLiveLocation &request);

  void on_request(uint64 id, td_api::editInlineMessageMedia &request);

  void on_request(uint64 id, td_api::editInlineMessageCaption &request);

  void on_request(uint64 id, td_api::editInlineMessageReplyMarkup &request);

  void on_request(uint64 id, td_api::editMessageSchedulingState &request);

  void on_request(uint64 id, td_api::setGameScore &request);

  void on_request(uint64 id, td_api::setInlineGameScore &request);

  void on_request(uint64 id, td_api::getGameHighScores &request);

  void on_request(uint64 id, td_api::getInlineGameHighScores &request);

  void on_request(uint64 id, const td_api::deleteChatReplyMarkup &request);

  void on_request(uint64 id, td_api::sendChatAction &request);

  void on_request(uint64 id, td_api::sendChatScreenshotTakenNotification &request);

  void on_request(uint64 id, td_api::forwardMessages &request);

  void on_request(uint64 id, const td_api::resendMessages &request);

  void on_request(uint64 id, td_api::getWebPagePreview &request);

  void on_request(uint64 id, td_api::getWebPageInstantView &request);

  void on_request(uint64 id, const td_api::createPrivateChat &request);

  void on_request(uint64 id, const td_api::createBasicGroupChat &request);

  void on_request(uint64 id, const td_api::createSupergroupChat &request);

  void on_request(uint64 id, td_api::createSecretChat &request);

  void on_request(uint64 id, td_api::createNewBasicGroupChat &request);

  void on_request(uint64 id, td_api::createNewSupergroupChat &request);

  void on_request(uint64 id, td_api::createNewSecretChat &request);

  void on_request(uint64 id, const td_api::createCall &request);

  void on_request(uint64 id, const td_api::acceptCall &request);

  void on_request(uint64 id, td_api::sendCallSignalingData &request);

  void on_request(uint64 id, const td_api::discardCall &request);

  void on_request(uint64 id, td_api::sendCallRating &request);

  void on_request(uint64 id, td_api::sendCallDebugInformation &request);

  void on_request(uint64 id, const td_api::createVoiceChat &request);

  void on_request(uint64 id, const td_api::getGroupCall &request);

  void on_request(uint64 id, td_api::joinGroupCall &request);

  void on_request(uint64 id, const td_api::toggleGroupCallMuteNewParticipants &request);

  void on_request(uint64 id, const td_api::inviteGroupCallParticipants &request);

  void on_request(uint64 id, const td_api::setGroupCallParticipantIsSpeaking &request);

  void on_request(uint64 id, const td_api::toggleGroupCallParticipantIsMuted &request);

  void on_request(uint64 id, const td_api::setGroupCallParticipantVolumeLevel &request);

  void on_request(uint64 id, const td_api::loadGroupCallParticipants &request);

  void on_request(uint64 id, const td_api::leaveGroupCall &request);

  void on_request(uint64 id, const td_api::discardGroupCall &request);

  void on_request(uint64 id, const td_api::upgradeBasicGroupChatToSupergroupChat &request);

  void on_request(uint64 id, const td_api::getChatListsToAddChat &request);

  void on_request(uint64 id, const td_api::addChatToList &request);

  void on_request(uint64 id, const td_api::getChatFilter &request);

  void on_request(uint64 id, const td_api::getRecommendedChatFilters &request);

  void on_request(uint64 id, td_api::createChatFilter &request);

  void on_request(uint64 id, td_api::editChatFilter &request);

  void on_request(uint64 id, const td_api::deleteChatFilter &request);

  void on_request(uint64 id, const td_api::reorderChatFilters &request);

  void on_request(uint64 id, td_api::setChatTitle &request);

  void on_request(uint64 id, const td_api::setChatPhoto &request);

  void on_request(uint64 id, const td_api::setChatMessageTtlSetting &request);

  void on_request(uint64 id, const td_api::setChatPermissions &request);

  void on_request(uint64 id, td_api::setChatDraftMessage &request);

  void on_request(uint64 id, const td_api::toggleChatIsPinned &request);

  void on_request(uint64 id, const td_api::toggleChatIsMarkedAsUnread &request);

  void on_request(uint64 id, const td_api::toggleMessageSenderIsBlocked &request);

  void on_request(uint64 id, const td_api::toggleChatDefaultDisableNotification &request);

  void on_request(uint64 id, const td_api::setPinnedChats &request);

  void on_request(uint64 id, td_api::setChatClientData &request);

  void on_request(uint64 id, td_api::setChatDescription &request);

  void on_request(uint64 id, const td_api::setChatDiscussionGroup &request);

  void on_request(uint64 id, td_api::setChatLocation &request);

  void on_request(uint64 id, const td_api::setChatSlowModeDelay &request);

  void on_request(uint64 id, const td_api::pinChatMessage &request);

  void on_request(uint64 id, const td_api::unpinChatMessage &request);

  void on_request(uint64 id, const td_api::unpinAllChatMessages &request);

  void on_request(uint64 id, const td_api::joinChat &request);

  void on_request(uint64 id, const td_api::leaveChat &request);

  void on_request(uint64 id, const td_api::addChatMember &request);

  void on_request(uint64 id, const td_api::addChatMembers &request);

  void on_request(uint64 id, td_api::setChatMemberStatus &request);

  void on_request(uint64 id, const td_api::banChatMember &request);

  void on_request(uint64 id, const td_api::canTransferOwnership &request);

  void on_request(uint64 id, td_api::transferChatOwnership &request);

  void on_request(uint64 id, const td_api::getChatMember &request);

  void on_request(uint64 id, td_api::searchChatMembers &request);

  void on_request(uint64 id, td_api::getChatAdministrators &request);

  void on_request(uint64 id, const td_api::replacePrimaryChatInviteLink &request);

  void on_request(uint64 id, const td_api::createChatInviteLink &request);

  void on_request(uint64 id, td_api::editChatInviteLink &request);

  void on_request(uint64 id, td_api::getChatInviteLink &request);

  void on_request(uint64 id, const td_api::getChatInviteLinkCounts &request);

  void on_request(uint64 id, td_api::getChatInviteLinks &request);

  void on_request(uint64 id, td_api::getChatInviteLinkMembers &request);

  void on_request(uint64 id, td_api::revokeChatInviteLink &request);

  void on_request(uint64 id, td_api::deleteRevokedChatInviteLink &request);

  void on_request(uint64 id, const td_api::deleteAllRevokedChatInviteLinks &request);

  void on_request(uint64 id, td_api::checkChatInviteLink &request);

  void on_request(uint64 id, td_api::joinChatByInviteLink &request);

  void on_request(uint64 id, td_api::getChatEventLog &request);

  void on_request(uint64 id, const td_api::clearAllDraftMessages &request);

  void on_request(uint64 id, const td_api::downloadFile &request);

  void on_request(uint64 id, const td_api::getFileDownloadedPrefixSize &request);

  void on_request(uint64 id, const td_api::cancelDownloadFile &request);

  void on_request(uint64 id, td_api::uploadFile &request);

  void on_request(uint64 id, const td_api::cancelUploadFile &request);

  void on_request(uint64 id, td_api::writeGeneratedFilePart &request);

  void on_request(uint64 id, const td_api::setFileGenerationProgress &request);

  void on_request(uint64 id, td_api::finishFileGeneration &request);

  void on_request(uint64 id, const td_api::readFilePart &request);

  void on_request(uint64 id, const td_api::deleteFile &request);

  void on_request(uint64 id, td_api::getMessageFileType &request);

  void on_request(uint64 id, const td_api::getMessageImportConfirmationText &request);

  void on_request(uint64 id, const td_api::importMessages &request);

  void on_request(uint64 id, const td_api::blockMessageSenderFromReplies &request);

  void on_request(uint64 id, const td_api::getBlockedMessageSenders &request);

  void on_request(uint64 id, td_api::addContact &request);

  void on_request(uint64 id, td_api::importContacts &request);

  void on_request(uint64 id, const td_api::getContacts &request);

  void on_request(uint64 id, td_api::searchContacts &request);

  void on_request(uint64 id, td_api::removeContacts &request);

  void on_request(uint64 id, const td_api::getImportedContactCount &request);

  void on_request(uint64 id, td_api::changeImportedContacts &request);

  void on_request(uint64 id, const td_api::clearImportedContacts &request);

  void on_request(uint64 id, const td_api::sharePhoneNumber &request);

  void on_request(uint64 id, const td_api::getRecentInlineBots &request);

  void on_request(uint64 id, td_api::setName &request);

  void on_request(uint64 id, td_api::setBio &request);

  void on_request(uint64 id, td_api::setUsername &request);

  void on_request(uint64 id, td_api::setCommands &request);

  void on_request(uint64 id, const td_api::setLocation &request);

  void on_request(uint64 id, td_api::setProfilePhoto &request);

  void on_request(uint64 id, const td_api::deleteProfilePhoto &request);

  void on_request(uint64 id, const td_api::getUserProfilePhotos &request);

  void on_request(uint64 id, td_api::setSupergroupUsername &request);

  void on_request(uint64 id, const td_api::setSupergroupStickerSet &request);

  void on_request(uint64 id, const td_api::toggleSupergroupSignMessages &request);

  void on_request(uint64 id, const td_api::toggleSupergroupIsAllHistoryAvailable &request);

  void on_request(uint64 id, const td_api::toggleSupergroupIsBroadcastGroup &request);

  void on_request(uint64 id, const td_api::reportSupergroupSpam &request);

  void on_request(uint64 id, td_api::getSupergroupMembers &request);

  void on_request(uint64 id, td_api::closeSecretChat &request);

  void on_request(uint64 id, td_api::getStickers &request);

  void on_request(uint64 id, td_api::searchStickers &request);

  void on_request(uint64 id, const td_api::getInstalledStickerSets &request);

  void on_request(uint64 id, const td_api::getArchivedStickerSets &request);

  void on_request(uint64 id, const td_api::getTrendingStickerSets &request);

  void on_request(uint64 id, const td_api::getAttachedStickerSets &request);

  void on_request(uint64 id, const td_api::getStickerSet &request);

  void on_request(uint64 id, td_api::searchStickerSet &request);

  void on_request(uint64 id, td_api::searchInstalledStickerSets &request);

  void on_request(uint64 id, td_api::searchStickerSets &request);

  void on_request(uint64 id, const td_api::changeStickerSet &request);

  void on_request(uint64 id, const td_api::viewTrendingStickerSets &request);

  void on_request(uint64 id, td_api::reorderInstalledStickerSets &request);

  void on_request(uint64 id, td_api::uploadStickerFile &request);

  void on_request(uint64 id, td_api::createNewStickerSet &request);

  void on_request(uint64 id, td_api::addStickerToSet &request);

  void on_request(uint64 id, td_api::setStickerSetThumbnail &request);

  void on_request(uint64 id, td_api::setStickerPositionInSet &request);

  void on_request(uint64 id, td_api::removeStickerFromSet &request);

  void on_request(uint64 id, const td_api::getRecentStickers &request);

  void on_request(uint64 id, td_api::addRecentSticker &request);

  void on_request(uint64 id, td_api::removeRecentSticker &request);

  void on_request(uint64 id, td_api::clearRecentStickers &request);

  void on_request(uint64 id, const td_api::getSavedAnimations &request);

  void on_request(uint64 id, td_api::addSavedAnimation &request);

  void on_request(uint64 id, td_api::removeSavedAnimation &request);

  void on_request(uint64 id, td_api::getStickerEmojis &request);

  void on_request(uint64 id, td_api::searchEmojis &request);

  void on_request(uint64 id, td_api::getEmojiSuggestionsUrl &request);

  void on_request(uint64 id, const td_api::getFavoriteStickers &request);

  void on_request(uint64 id, td_api::addFavoriteSticker &request);

  void on_request(uint64 id, td_api::removeFavoriteSticker &request);

  void on_request(uint64 id, const td_api::getChatNotificationSettingsExceptions &request);

  void on_request(uint64 id, const td_api::getScopeNotificationSettings &request);

  void on_request(uint64 id, td_api::setChatNotificationSettings &request);

  void on_request(uint64 id, td_api::setScopeNotificationSettings &request);

  void on_request(uint64 id, const td_api::resetAllNotificationSettings &request);

  void on_request(uint64 id, const td_api::removeChatActionBar &request);

  void on_request(uint64 id, td_api::reportChat &request);

  void on_request(uint64 id, td_api::reportChatPhoto &request);

  void on_request(uint64 id, td_api::getChatStatisticsUrl &request);

  void on_request(uint64 id, const td_api::getChatStatistics &request);

  void on_request(uint64 id, const td_api::getMessageStatistics &request);

  void on_request(uint64 id, td_api::getStatisticalGraph &request);

  void on_request(uint64 id, const td_api::getMapThumbnailFile &request);

  void on_request(uint64 id, const td_api::getLocalizationTargetInfo &request);

  void on_request(uint64 id, td_api::getLanguagePackInfo &request);

  void on_request(uint64 id, td_api::getLanguagePackStrings &request);

  void on_request(uint64 id, td_api::synchronizeLanguagePack &request);

  void on_request(uint64 id, td_api::addCustomServerLanguagePack &request);

  void on_request(uint64 id, td_api::setCustomLanguagePack &request);

  void on_request(uint64 id, td_api::editCustomLanguagePackInfo &request);

  void on_request(uint64 id, td_api::setCustomLanguagePackString &request);

  void on_request(uint64 id, td_api::deleteLanguagePack &request);

  void on_request(uint64 id, td_api::getOption &request);

  void on_request(uint64 id, td_api::setOption &request);

  void on_request(uint64 id, td_api::setPollAnswer &request);

  void on_request(uint64 id, td_api::getPollVoters &request);

  void on_request(uint64 id, td_api::stopPoll &request);

  void on_request(uint64 id, const td_api::hideSuggestedAction &request);

  void on_request(uint64 id, const td_api::getLoginUrlInfo &request);

  void on_request(uint64 id, const td_api::getLoginUrl &request);

  void on_request(uint64 id, td_api::getInlineQueryResults &request);

  void on_request(uint64 id, td_api::answerInlineQuery &request);

  void on_request(uint64 id, td_api::getCallbackQueryAnswer &request);

  void on_request(uint64 id, td_api::answerCallbackQuery &request);

  void on_request(uint64 id, td_api::answerShippingQuery &request);

  void on_request(uint64 id, td_api::answerPreCheckoutQuery &request);

  void on_request(uint64 id, td_api::getBankCardInfo &request);

  void on_request(uint64 id, const td_api::getPaymentForm &request);

  void on_request(uint64 id, td_api::validateOrderInfo &request);

  void on_request(uint64 id, td_api::sendPaymentForm &request);

  void on_request(uint64 id, const td_api::getPaymentReceipt &request);

  void on_request(uint64 id, const td_api::getSavedOrderInfo &request);

  void on_request(uint64 id, const td_api::deleteSavedOrderInfo &request);

  void on_request(uint64 id, const td_api::deleteSavedCredentials &request);

  void on_request(uint64 id, td_api::getPassportElement &request);

  void on_request(uint64 id, td_api::getAllPassportElements &request);

  void on_request(uint64 id, td_api::setPassportElement &request);

  void on_request(uint64 id, const td_api::deletePassportElement &request);

  void on_request(uint64 id, td_api::setPassportElementErrors &request);

  void on_request(uint64 id, td_api::getPreferredCountryLanguage &request);

  void on_request(uint64 id, td_api::sendPhoneNumberVerificationCode &request);

  void on_request(uint64 id, const td_api::resendPhoneNumberVerificationCode &request);

  void on_request(uint64 id, td_api::checkPhoneNumberVerificationCode &request);

  void on_request(uint64 id, td_api::sendEmailAddressVerificationCode &request);

  void on_request(uint64 id, const td_api::resendEmailAddressVerificationCode &request);

  void on_request(uint64 id, td_api::checkEmailAddressVerificationCode &request);

  void on_request(uint64 id, td_api::getPassportAuthorizationForm &request);

  void on_request(uint64 id, td_api::getPassportAuthorizationFormAvailableElements &request);

  void on_request(uint64 id, td_api::sendPassportAuthorizationForm &request);

  void on_request(uint64 id, td_api::sendPhoneNumberConfirmationCode &request);

  void on_request(uint64 id, const td_api::resendPhoneNumberConfirmationCode &request);

  void on_request(uint64 id, td_api::checkPhoneNumberConfirmationCode &request);

  void on_request(uint64 id, const td_api::getSupportUser &request);

  void on_request(uint64 id, const td_api::getBackgrounds &request);

  void on_request(uint64 id, td_api::getBackgroundUrl &request);

  void on_request(uint64 id, td_api::searchBackground &request);

  void on_request(uint64 id, td_api::setBackground &request);

  void on_request(uint64 id, const td_api::removeBackground &request);

  void on_request(uint64 id, const td_api::resetBackgrounds &request);

  void on_request(uint64 id, td_api::getRecentlyVisitedTMeUrls &request);

  void on_request(uint64 id, td_api::setBotUpdatesStatus &request);

  void on_request(uint64 id, td_api::sendCustomRequest &request);

  void on_request(uint64 id, td_api::answerCustomQuery &request);

  void on_request(uint64 id, const td_api::setAlarm &request);

  void on_request(uint64 id, td_api::searchHashtags &request);

  void on_request(uint64 id, td_api::removeRecentHashtag &request);

  void on_request(uint64 id, td_api::acceptTermsOfService &request);

  void on_request(uint64 id, const td_api::getCountries &request);

  void on_request(uint64 id, const td_api::getCountryCode &request);

  void on_request(uint64 id, const td_api::getPhoneNumberInfo &request);

  void on_request(uint64 id, const td_api::getInviteText &request);

  void on_request(uint64 id, td_api::getDeepLinkInfo &request);

  void on_request(uint64 id, const td_api::getApplicationConfig &request);

  void on_request(uint64 id, td_api::saveApplicationLogEvent &request);

  void on_request(uint64 id, td_api::addProxy &request);

  void on_request(uint64 id, td_api::editProxy &request);

  void on_request(uint64 id, const td_api::enableProxy &request);

  void on_request(uint64 id, const td_api::disableProxy &request);

  void on_request(uint64 id, const td_api::removeProxy &request);

  void on_request(uint64 id, const td_api::getProxies &request);

  void on_request(uint64 id, const td_api::getProxyLink &request);

  void on_request(uint64 id, const td_api::pingProxy &request);

  void on_request(uint64 id, const td_api::getTextEntities &request);

  void on_request(uint64 id, const td_api::parseTextEntities &request);

  void on_request(uint64 id, const td_api::parseMarkdown &request);

  void on_request(uint64 id, const td_api::getMarkdownText &request);

  void on_request(uint64 id, const td_api::getFileMimeType &request);

  void on_request(uint64 id, const td_api::getFileExtension &request);

  void on_request(uint64 id, const td_api::cleanFileName &request);

  void on_request(uint64 id, const td_api::getLanguagePackString &request);

  void on_request(uint64 id, const td_api::getPushReceiverId &request);

  void on_request(uint64 id, const td_api::getChatFilterDefaultIconName &request);

  void on_request(uint64 id, const td_api::getJsonValue &request);

  void on_request(uint64 id, const td_api::getJsonString &request);

  void on_request(uint64 id, const td_api::setLogStream &request);

  void on_request(uint64 id, const td_api::getLogStream &request);

  void on_request(uint64 id, const td_api::setLogVerbosityLevel &request);

  void on_request(uint64 id, const td_api::getLogVerbosityLevel &request);

  void on_request(uint64 id, const td_api::getLogTags &request);

  void on_request(uint64 id, const td_api::setLogTagVerbosityLevel &request);

  void on_request(uint64 id, const td_api::getLogTagVerbosityLevel &request);

  void on_request(uint64 id, const td_api::addLogMessage &request);

  // test
  void on_request(uint64 id, const td_api::testNetwork &request);
  void on_request(uint64 id, td_api::testProxy &request);
  void on_request(uint64 id, const td_api::testGetDifference &request);
  void on_request(uint64 id, const td_api::testUseUpdate &request);
  void on_request(uint64 id, const td_api::testReturnError &request);
  void on_request(uint64 id, const td_api::testCallEmpty &request);
  void on_request(uint64 id, const td_api::testSquareInt &request);
  void on_request(uint64 id, td_api::testCallString &request);
  void on_request(uint64 id, td_api::testCallBytes &request);
  void on_request(uint64 id, td_api::testCallVectorInt &request);
  void on_request(uint64 id, td_api::testCallVectorIntObject &request);
  void on_request(uint64 id, td_api::testCallVectorString &request);
  void on_request(uint64 id, td_api::testCallVectorStringObject &request);

  template <class T>
  static td_api::object_ptr<td_api::Object> do_static_request(const T &request) {
    return td_api::make_object<td_api::error>(400, "The method can't be executed synchronously");
  }
  static td_api::object_ptr<td_api::Object> do_static_request(const td_api::getTextEntities &request);
  static td_api::object_ptr<td_api::Object> do_static_request(td_api::parseTextEntities &request);
  static td_api::object_ptr<td_api::Object> do_static_request(td_api::parseMarkdown &request);
  static td_api::object_ptr<td_api::Object> do_static_request(td_api::getMarkdownText &request);
  static td_api::object_ptr<td_api::Object> do_static_request(const td_api::getFileMimeType &request);
  static td_api::object_ptr<td_api::Object> do_static_request(const td_api::getFileExtension &request);
  static td_api::object_ptr<td_api::Object> do_static_request(const td_api::cleanFileName &request);
  static td_api::object_ptr<td_api::Object> do_static_request(const td_api::getLanguagePackString &request);
  static td_api::object_ptr<td_api::Object> do_static_request(const td_api::getPushReceiverId &request);
  static td_api::object_ptr<td_api::Object> do_static_request(const td_api::getChatFilterDefaultIconName &request);
  static td_api::object_ptr<td_api::Object> do_static_request(td_api::getJsonValue &request);
  static td_api::object_ptr<td_api::Object> do_static_request(const td_api::getJsonString &request);
  static td_api::object_ptr<td_api::Object> do_static_request(td_api::setLogStream &request);
  static td_api::object_ptr<td_api::Object> do_static_request(const td_api::getLogStream &request);
  static td_api::object_ptr<td_api::Object> do_static_request(const td_api::setLogVerbosityLevel &request);
  static td_api::object_ptr<td_api::Object> do_static_request(const td_api::getLogVerbosityLevel &request);
  static td_api::object_ptr<td_api::Object> do_static_request(const td_api::getLogTags &request);
  static td_api::object_ptr<td_api::Object> do_static_request(const td_api::setLogTagVerbosityLevel &request);
  static td_api::object_ptr<td_api::Object> do_static_request(const td_api::getLogTagVerbosityLevel &request);
  static td_api::object_ptr<td_api::Object> do_static_request(const td_api::addLogMessage &request);
  static td_api::object_ptr<td_api::Object> do_static_request(td_api::testReturnError &request);

  static DbKey as_db_key(string key);
  Status init(DbKey key) TD_WARN_UNUSED_RESULT;
  void init_options_and_network();
  void init_connection_creator();
  void init_file_manager();
  void init_managers();
  void clear();
  void close_impl(bool destroy_flag);
  static Status fix_parameters(TdParameters &parameters) TD_WARN_UNUSED_RESULT;
  Status set_parameters(td_api::object_ptr<td_api::tdlibParameters> parameters) TD_WARN_UNUSED_RESULT;

  static td_api::object_ptr<td_api::error> make_error(int32 code, CSlice error) {
    return td_api::make_object<td_api::error>(code, error.str());
  }

  // Actor
  void start_up() override;
  void tear_down() override;
  void hangup_shared() override;
  void hangup() override;
};

}  // namespace td
