//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/ConnectionState.h"
#include "td/telegram/files/FileId.h"
#include "td/telegram/net/MtprotoHeader.h"
#include "td/telegram/net/NetQuery.h"
#include "td/telegram/net/NetQueryStats.h"
#include "td/telegram/td_api.h"
#include "td/telegram/TdCallback.h"
#include "td/telegram/TdDb.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/TermsOfService.h"

#include "td/db/DbKey.h"

#include "td/actor/actor.h"
#include "td/actor/MultiTimeout.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/Container.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/logging.h"
#include "td/utils/Promise.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"

#include <memory>
#include <unordered_map>
#include <utility>

namespace td {

class AccountManager;
class AnimationsManager;
class AttachMenuManager;
class AudiosManager;
class AuthManager;
class AutosaveManager;
class BackgroundManager;
class BoostManager;
class BotInfoManager;
class BusinessConnectionManager;
class BusinessManager;
class CallManager;
class CallbackQueriesManager;
class ChannelRecommendationManager;
class ChatManager;
class CommonDialogManager;
class ConfigManager;
class CountryInfoManager;
class DeviceTokenManager;
class DialogActionManager;
class DialogFilterManager;
class DialogInviteLinkManager;
class DialogManager;
class DialogParticipantManager;
class DocumentsManager;
class DownloadManager;
class FileManager;
class FileReferenceManager;
class ForumTopicManager;
class GameManager;
class GroupCallManager;
class InlineMessageManager;
class InlineQueriesManager;
class HashtagHints;
class LanguagePackManager;
class LinkManager;
class MessageImportManager;
class MessagesManager;
class NetStatsManager;
class NotificationManager;
class NotificationSettingsManager;
class OptionManager;
class PasswordManager;
class PeopleNearbyManager;
class PhoneNumberManager;
class PollManager;
class PrivacyManager;
class QuickReplyManager;
class ReactionManager;
class SavedMessagesManager;
class SecureManager;
class SecretChatsManager;
class SponsoredMessageManager;
class StarManager;
class StateManager;
class StatisticsManager;
class StickersManager;
class StorageManager;
class StoryManager;
class ThemeManager;
class TimeZoneManager;
class TopDialogManager;
class TranscriptionManager;
class TranslationManager;
class UpdatesManager;
class UserManager;
class VideoNotesManager;
class VideosManager;
class VoiceNotesManager;
class WebPagesManager;

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
// It happens after destruction of callback
class Td final : public Actor {
 public:
  Td(const Td &) = delete;
  Td(Td &&) = delete;
  Td &operator=(const Td &) = delete;
  Td &operator=(Td &&) = delete;
  ~Td() final;

  struct Options {
    std::shared_ptr<NetQueryStats> net_query_stats;
  };

  Td(unique_ptr<TdCallback> callback, Options options);

  void request(uint64 id, tl_object_ptr<td_api::Function> function);

  void destroy();

  void schedule_get_terms_of_service(int32 expires_in);

  void reload_promo_data();

  void on_update(telegram_api::object_ptr<telegram_api::Updates> updates, uint64 auth_key_id);

  void on_result(NetQueryPtr query);

  void on_online_updated(bool force, bool send_update);

  void on_update_status_success(bool is_online);

  bool is_online() const;

  void set_is_online(bool is_online);

  void set_is_bot_online(bool is_bot_online);

  bool can_ignore_background_updates() const {
    return can_ignore_background_updates_;
  }

  bool ignore_background_updates() const;

  unique_ptr<AudiosManager> audios_manager_;
  unique_ptr<CallbackQueriesManager> callback_queries_manager_;
  unique_ptr<DocumentsManager> documents_manager_;
  unique_ptr<OptionManager> option_manager_;
  unique_ptr<VideosManager> videos_manager_;

  unique_ptr<AccountManager> account_manager_;
  ActorOwn<AccountManager> account_manager_actor_;
  unique_ptr<AnimationsManager> animations_manager_;
  ActorOwn<AnimationsManager> animations_manager_actor_;
  unique_ptr<AttachMenuManager> attach_menu_manager_;
  ActorOwn<AttachMenuManager> attach_menu_manager_actor_;
  unique_ptr<AuthManager> auth_manager_;
  ActorOwn<AuthManager> auth_manager_actor_;
  unique_ptr<AutosaveManager> autosave_manager_;
  ActorOwn<AutosaveManager> autosave_manager_actor_;
  unique_ptr<BackgroundManager> background_manager_;
  ActorOwn<BackgroundManager> background_manager_actor_;
  unique_ptr<BoostManager> boost_manager_;
  ActorOwn<BoostManager> boost_manager_actor_;
  unique_ptr<BotInfoManager> bot_info_manager_;
  ActorOwn<BotInfoManager> bot_info_manager_actor_;
  unique_ptr<BusinessConnectionManager> business_connection_manager_;
  ActorOwn<BusinessConnectionManager> business_connection_manager_actor_;
  unique_ptr<BusinessManager> business_manager_;
  ActorOwn<BusinessManager> business_manager_actor_;
  unique_ptr<ChannelRecommendationManager> channel_recommendation_manager_;
  ActorOwn<ChannelRecommendationManager> channel_recommendation_manager_actor_;
  unique_ptr<ChatManager> chat_manager_;
  ActorOwn<ChatManager> chat_manager_actor_;
  unique_ptr<CommonDialogManager> common_dialog_manager_;
  ActorOwn<CommonDialogManager> common_dialog_manager_actor_;
  unique_ptr<CountryInfoManager> country_info_manager_;
  ActorOwn<CountryInfoManager> country_info_manager_actor_;
  unique_ptr<DialogActionManager> dialog_action_manager_;
  ActorOwn<DialogActionManager> dialog_action_manager_actor_;
  unique_ptr<DialogFilterManager> dialog_filter_manager_;
  ActorOwn<DialogFilterManager> dialog_filter_manager_actor_;
  unique_ptr<DialogInviteLinkManager> dialog_invite_link_manager_;
  ActorOwn<DialogInviteLinkManager> dialog_invite_link_manager_actor_;
  unique_ptr<DialogManager> dialog_manager_;
  ActorOwn<DialogManager> dialog_manager_actor_;
  unique_ptr<DialogParticipantManager> dialog_participant_manager_;
  ActorOwn<DialogParticipantManager> dialog_participant_manager_actor_;
  unique_ptr<DownloadManager> download_manager_;
  ActorOwn<DownloadManager> download_manager_actor_;
  unique_ptr<FileManager> file_manager_;
  ActorOwn<FileManager> file_manager_actor_;
  unique_ptr<FileReferenceManager> file_reference_manager_;
  ActorOwn<FileReferenceManager> file_reference_manager_actor_;
  unique_ptr<ForumTopicManager> forum_topic_manager_;
  ActorOwn<ForumTopicManager> forum_topic_manager_actor_;
  unique_ptr<GameManager> game_manager_;
  ActorOwn<GameManager> game_manager_actor_;
  unique_ptr<GroupCallManager> group_call_manager_;
  ActorOwn<GroupCallManager> group_call_manager_actor_;
  unique_ptr<InlineMessageManager> inline_message_manager_;
  ActorOwn<InlineMessageManager> inline_message_manager_actor_;
  unique_ptr<InlineQueriesManager> inline_queries_manager_;
  ActorOwn<InlineQueriesManager> inline_queries_manager_actor_;
  unique_ptr<LinkManager> link_manager_;
  ActorOwn<LinkManager> link_manager_actor_;
  unique_ptr<MessageImportManager> message_import_manager_;
  ActorOwn<MessageImportManager> message_import_manager_actor_;
  unique_ptr<MessagesManager> messages_manager_;
  ActorOwn<MessagesManager> messages_manager_actor_;
  unique_ptr<NotificationManager> notification_manager_;
  ActorOwn<NotificationManager> notification_manager_actor_;
  unique_ptr<NotificationSettingsManager> notification_settings_manager_;
  ActorOwn<NotificationSettingsManager> notification_settings_manager_actor_;
  unique_ptr<PollManager> poll_manager_;
  ActorOwn<PollManager> poll_manager_actor_;
  unique_ptr<PrivacyManager> privacy_manager_;
  ActorOwn<PrivacyManager> privacy_manager_actor_;
  unique_ptr<PeopleNearbyManager> people_nearby_manager_;
  ActorOwn<PeopleNearbyManager> people_nearby_manager_actor_;
  unique_ptr<PhoneNumberManager> phone_number_manager_;
  ActorOwn<PhoneNumberManager> phone_number_manager_actor_;
  unique_ptr<QuickReplyManager> quick_reply_manager_;
  ActorOwn<QuickReplyManager> quick_reply_manager_actor_;
  unique_ptr<ReactionManager> reaction_manager_;
  ActorOwn<ReactionManager> reaction_manager_actor_;
  unique_ptr<SavedMessagesManager> saved_messages_manager_;
  ActorOwn<SavedMessagesManager> saved_messages_manager_actor_;
  unique_ptr<SponsoredMessageManager> sponsored_message_manager_;
  ActorOwn<SponsoredMessageManager> sponsored_message_manager_actor_;
  unique_ptr<StarManager> star_manager_;
  ActorOwn<StarManager> star_manager_actor_;
  unique_ptr<StatisticsManager> statistics_manager_;
  ActorOwn<StatisticsManager> statistics_manager_actor_;
  unique_ptr<StickersManager> stickers_manager_;
  ActorOwn<StickersManager> stickers_manager_actor_;
  unique_ptr<StoryManager> story_manager_;
  ActorOwn<StoryManager> story_manager_actor_;
  unique_ptr<ThemeManager> theme_manager_;
  ActorOwn<ThemeManager> theme_manager_actor_;
  unique_ptr<TimeZoneManager> time_zone_manager_;
  ActorOwn<TimeZoneManager> time_zone_manager_actor_;
  unique_ptr<TopDialogManager> top_dialog_manager_;
  ActorOwn<TopDialogManager> top_dialog_manager_actor_;
  unique_ptr<TranscriptionManager> transcription_manager_;
  ActorOwn<TranscriptionManager> transcription_manager_actor_;
  unique_ptr<TranslationManager> translation_manager_;
  ActorOwn<TranslationManager> translation_manager_actor_;
  unique_ptr<UpdatesManager> updates_manager_;
  ActorOwn<UpdatesManager> updates_manager_actor_;
  unique_ptr<UserManager> user_manager_;
  ActorOwn<UserManager> user_manager_actor_;
  unique_ptr<VideoNotesManager> video_notes_manager_;
  ActorOwn<VideoNotesManager> video_notes_manager_actor_;
  unique_ptr<VoiceNotesManager> voice_notes_manager_;
  ActorOwn<VoiceNotesManager> voice_notes_manager_actor_;
  unique_ptr<WebPagesManager> web_pages_manager_;
  ActorOwn<WebPagesManager> web_pages_manager_actor_;

  ActorOwn<CallManager> call_manager_;
  ActorOwn<HashtagHints> cashtag_search_hints_;
  ActorOwn<ConfigManager> config_manager_;
  ActorOwn<DeviceTokenManager> device_token_manager_;
  ActorOwn<HashtagHints> hashtag_hints_;
  ActorOwn<HashtagHints> hashtag_search_hints_;
  ActorOwn<LanguagePackManager> language_pack_manager_;
  ActorOwn<NetStatsManager> net_stats_manager_;
  ActorOwn<PasswordManager> password_manager_;
  ActorOwn<SecretChatsManager> secret_chats_manager_;
  ActorOwn<SecureManager> secure_manager_;
  ActorOwn<StateManager> state_manager_;
  ActorOwn<StorageManager> storage_manager_;

  class ResultHandler : public std::enable_shared_from_this<ResultHandler> {
   public:
    ResultHandler() = default;
    ResultHandler(const ResultHandler &) = delete;
    ResultHandler &operator=(const ResultHandler &) = delete;
    virtual ~ResultHandler() = default;

    virtual void on_result(BufferSlice packet) {
      UNREACHABLE();
    }

    virtual void on_error(Status status) {
      UNREACHABLE();
    }

    friend class Td;

   protected:
    void send_query(NetQueryPtr query);

    Td *td_ = nullptr;
    bool is_query_sent_ = false;

   private:
    void set_td(Td *td);
  };

  template <class HandlerT, class... Args>
  std::shared_ptr<HandlerT> create_handler(Args &&...args) {
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
  static constexpr int64 ONLINE_ALARM_ID = 0;
  static constexpr int64 PING_SERVER_ALARM_ID = -1;
  static constexpr int32 PING_SERVER_TIMEOUT = 300;
  static constexpr int64 TERMS_OF_SERVICE_ALARM_ID = -2;
  static constexpr int64 PROMO_DATA_ALARM_ID = -3;

  void on_connection_state_changed(ConnectionState new_state);

  void run_request(uint64 id, tl_object_ptr<td_api::Function> function);

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

  ConnectionState connection_state_ = ConnectionState::Empty;

  std::unordered_multimap<uint64, int32> request_set_;
  int actor_refcnt_ = 0;
  int request_actor_refcnt_ = 0;
  int stop_cnt_ = 2;
  bool destroy_flag_ = false;
  int close_flag_ = 0;

  enum class State : int32 { WaitParameters, Run, Close } state_ = State::WaitParameters;
  uint64 set_parameters_request_id_ = 0;

  FlatHashMap<uint64, std::shared_ptr<ResultHandler>> result_handlers_;
  enum : int8 { RequestActorIdType = 1, ActorIdType = 2 };
  Container<ActorOwn<Actor>> request_actors_;

  bool can_ignore_background_updates_ = false;

  bool reloading_promo_data_ = false;
  bool need_reload_promo_data_ = false;

  bool is_online_ = false;
  bool is_bot_online_ = false;
  NetQueryRef update_status_query_;

  int64 alarm_id_ = 1;
  FlatHashMap<int64, uint64> pending_alarms_;
  MultiTimeout alarm_timeout_{"AlarmTimeout"};

  TermsOfService pending_terms_of_service_;

  struct DownloadInfo {
    int64 offset = -1;
    int64 limit = -1;
    vector<uint64> request_ids;
  };
  FlatHashMap<FileId, DownloadInfo, FileIdHash> pending_file_downloads_;

  vector<std::pair<uint64, td_api::object_ptr<td_api::Function>>> pending_preauthentication_requests_;

  vector<std::pair<uint64, td_api::object_ptr<td_api::Function>>> pending_set_parameters_requests_;
  vector<std::pair<uint64, td_api::object_ptr<td_api::Function>>> pending_init_requests_;

  template <class T>
  void complete_pending_preauthentication_requests(const T &func);

  td_api::object_ptr<td_api::AuthorizationState> get_fake_authorization_state_object() const;

  vector<td_api::object_ptr<td_api::Update>> get_fake_current_state() const;

  static void on_alarm_timeout_callback(void *td_ptr, int64 alarm_id);
  void on_alarm_timeout(int64 alarm_id);

  td_api::object_ptr<td_api::updateTermsOfService> get_update_terms_of_service_object() const;

  void on_get_terms_of_service(Result<std::pair<int32, TermsOfService>> result, bool dummy);

  void on_get_promo_data(Result<telegram_api::object_ptr<telegram_api::help_PromoData>> r_promo_data, bool dummy);

  template <class T>
  friend class RequestActor;  // uses send_result/send_error
  friend class AuthManager;   // uses send_result/send_error, TODO pass Promise<>

  void add_handler(uint64 id, std::shared_ptr<ResultHandler> handler);
  std::shared_ptr<ResultHandler> extract_handler(uint64 id);

  void clear_requests();

  void on_file_download_finished(FileId file_id);

  class OnRequest;

  class DownloadFileCallback;

  std::shared_ptr<DownloadFileCallback> download_file_callback_;

  class UploadFileCallback;

  std::shared_ptr<UploadFileCallback> upload_file_callback_;

  std::shared_ptr<ActorContext> old_context_;

  void schedule_get_promo_data(int32 expires_in);

  static int *get_log_verbosity_level(Slice name);

  template <class T>
  Promise<T> create_request_promise(uint64 id) {
    return PromiseCreator::lambda([actor_id = actor_id(this), id](Result<T> r_state) {
      if (r_state.is_error()) {
        send_closure(actor_id, &Td::send_error, id, r_state.move_as_error());
      } else {
        send_closure(actor_id, &Td::send_result, id, r_state.move_as_ok());
      }
    });
  }

  Promise<Unit> create_ok_request_promise(uint64 id);

  static bool is_authentication_request(int32 id);

  static bool is_synchronous_request(const td_api::Function *function);

  static bool is_preinitialization_request(int32 id);

  static bool is_preauthentication_request(int32 id);

  template <class T>
  void on_request(uint64 id, const T &) = delete;

  void on_request(uint64 id, const td_api::setTdlibParameters &request);

  void on_request(uint64 id, const td_api::getAuthorizationState &request);

  void on_request(uint64 id, td_api::setAuthenticationPhoneNumber &request);

  void on_request(uint64 id, td_api::sendAuthenticationFirebaseSms &request);

  void on_request(uint64 id, td_api::reportAuthenticationCodeMissing &request);

  void on_request(uint64 id, td_api::setAuthenticationEmailAddress &request);

  void on_request(uint64 id, td_api::resendAuthenticationCode &request);

  void on_request(uint64 id, td_api::checkAuthenticationEmailCode &request);

  void on_request(uint64 id, td_api::checkAuthenticationCode &request);

  void on_request(uint64 id, td_api::registerUser &request);

  void on_request(uint64 id, td_api::requestQrCodeAuthentication &request);

  void on_request(uint64 id, const td_api::resetAuthenticationEmailAddress &request);

  void on_request(uint64 id, td_api::checkAuthenticationPassword &request);

  void on_request(uint64 id, const td_api::requestAuthenticationPasswordRecovery &request);

  void on_request(uint64 id, td_api::checkAuthenticationPasswordRecoveryCode &request);

  void on_request(uint64 id, td_api::recoverAuthenticationPassword &request);

  void on_request(uint64 id, const td_api::logOut &request);

  void on_request(uint64 id, const td_api::close &request);

  void on_request(uint64 id, const td_api::destroy &request);

  void on_request(uint64 id, td_api::checkAuthenticationBotToken &request);

  void on_request(uint64 id, td_api::confirmQrCodeAuthentication &request);

  void on_request(uint64 id, td_api::setDatabaseEncryptionKey &request);

  void on_request(uint64 id, const td_api::getCurrentState &request);

  void on_request(uint64 id, td_api::getPasswordState &request);

  void on_request(uint64 id, td_api::setPassword &request);

  void on_request(uint64 id, td_api::setLoginEmailAddress &request);

  void on_request(uint64 id, const td_api::resendLoginEmailAddressCode &request);

  void on_request(uint64 id, td_api::checkLoginEmailAddressCode &request);

  void on_request(uint64 id, td_api::getRecoveryEmailAddress &request);

  void on_request(uint64 id, td_api::setRecoveryEmailAddress &request);

  void on_request(uint64 id, td_api::checkRecoveryEmailAddressCode &request);

  void on_request(uint64 id, const td_api::resendRecoveryEmailAddressCode &request);

  void on_request(uint64 id, const td_api::cancelRecoveryEmailAddressVerification &request);

  void on_request(uint64 id, td_api::requestPasswordRecovery &request);

  void on_request(uint64 id, td_api::checkPasswordRecoveryCode &request);

  void on_request(uint64 id, td_api::recoverPassword &request);

  void on_request(uint64 id, const td_api::resetPassword &request);

  void on_request(uint64 id, const td_api::cancelPasswordReset &request);

  void on_request(uint64 id, td_api::getTemporaryPasswordState &request);

  void on_request(uint64 id, td_api::createTemporaryPassword &request);

  void on_request(uint64 id, td_api::processPushNotification &request);

  void on_request(uint64 id, td_api::registerDevice &request);

  void on_request(uint64 id, td_api::getUserPrivacySettingRules &request);

  void on_request(uint64 id, td_api::setUserPrivacySettingRules &request);

  void on_request(uint64 id, const td_api::getDefaultMessageAutoDeleteTime &request);

  void on_request(uint64 id, const td_api::setDefaultMessageAutoDeleteTime &request);

  void on_request(uint64 id, const td_api::getAccountTtl &request);

  void on_request(uint64 id, const td_api::setAccountTtl &request);

  void on_request(uint64 id, td_api::deleteAccount &request);

  void on_request(uint64 id, td_api::sendPhoneNumberCode &request);

  void on_request(uint64 id, td_api::sendPhoneNumberFirebaseSms &request);

  void on_request(uint64 id, td_api::reportPhoneNumberCodeMissing &request);

  void on_request(uint64 id, td_api::resendPhoneNumberCode &request);

  void on_request(uint64 id, td_api::checkPhoneNumberCode &request);

  void on_request(uint64 id, const td_api::getUserLink &request);

  void on_request(uint64 id, td_api::searchUserByToken &request);

  void on_request(uint64 id, const td_api::getActiveSessions &request);

  void on_request(uint64 id, const td_api::terminateSession &request);

  void on_request(uint64 id, const td_api::terminateAllOtherSessions &request);

  void on_request(uint64 id, const td_api::confirmSession &request);

  void on_request(uint64 id, const td_api::toggleSessionCanAcceptCalls &request);

  void on_request(uint64 id, const td_api::toggleSessionCanAcceptSecretChats &request);

  void on_request(uint64 id, const td_api::setInactiveSessionTtl &request);

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

  void on_request(uint64 id, const td_api::getMessageReadDate &request);

  void on_request(uint64 id, const td_api::getMessageViewers &request);

  void on_request(uint64 id, const td_api::getMessages &request);

  void on_request(uint64 id, const td_api::getChatSponsoredMessages &request);

  void on_request(uint64 id, const td_api::clickChatSponsoredMessage &request);

  void on_request(uint64 id, const td_api::reportChatSponsoredMessage &request);

  void on_request(uint64 id, const td_api::getMessageLink &request);

  void on_request(uint64 id, const td_api::getMessageEmbeddingCode &request);

  void on_request(uint64 id, td_api::getMessageLinkInfo &request);

  void on_request(uint64 id, td_api::translateText &request);

  void on_request(uint64 id, td_api::translateMessageText &request);

  void on_request(uint64 id, const td_api::recognizeSpeech &request);

  void on_request(uint64 id, const td_api::rateSpeechRecognition &request);

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

  void on_request(uint64 id, const td_api::getAutosaveSettings &request);

  void on_request(uint64 id, td_api::setAutosaveSettings &request);

  void on_request(uint64 id, const td_api::clearAutosaveSettingsExceptions &request);

  void on_request(uint64 id, const td_api::getRecommendedChats &request);

  void on_request(uint64 id, const td_api::getChatSimilarChats &request);

  void on_request(uint64 id, const td_api::getChatSimilarChatCount &request);

  void on_request(uint64 id, const td_api::openChatSimilarChat &request);

  void on_request(uint64 id, const td_api::getTopChats &request);

  void on_request(uint64 id, const td_api::removeTopChat &request);

  void on_request(uint64 id, const td_api::loadChats &request);

  void on_request(uint64 id, const td_api::getChats &request);

  void on_request(uint64 id, const td_api::loadSavedMessagesTopics &request);

  void on_request(uint64 id, const td_api::getSavedMessagesTopicHistory &request);

  void on_request(uint64 id, const td_api::getSavedMessagesTopicMessageByDate &request);

  void on_request(uint64 id, const td_api::deleteSavedMessagesTopicHistory &request);

  void on_request(uint64 id, const td_api::deleteSavedMessagesTopicMessagesByDate &request);

  void on_request(uint64 id, const td_api::toggleSavedMessagesTopicIsPinned &request);

  void on_request(uint64 id, const td_api::setPinnedSavedMessagesTopics &request);

  void on_request(uint64 id, td_api::searchPublicChat &request);

  void on_request(uint64 id, td_api::searchPublicChats &request);

  void on_request(uint64 id, td_api::searchChats &request);

  void on_request(uint64 id, td_api::searchChatsOnServer &request);

  void on_request(uint64 id, const td_api::searchChatsNearby &request);

  void on_request(uint64 id, td_api::searchRecentlyFoundChats &request);

  void on_request(uint64 id, const td_api::addRecentlyFoundChat &request);

  void on_request(uint64 id, const td_api::removeRecentlyFoundChat &request);

  void on_request(uint64 id, const td_api::clearRecentlyFoundChats &request);

  void on_request(uint64 id, const td_api::getRecentlyOpenedChats &request);

  void on_request(uint64 id, const td_api::getGroupsInCommon &request);

  void on_request(uint64 id, td_api::checkChatUsername &request);

  void on_request(uint64 id, const td_api::getCreatedPublicChats &request);

  void on_request(uint64 id, const td_api::checkCreatedPublicChatsLimit &request);

  void on_request(uint64 id, const td_api::getSuitableDiscussionChats &request);

  void on_request(uint64 id, const td_api::getInactiveSupergroupChats &request);

  void on_request(uint64 id, const td_api::getSuitablePersonalChats &request);

  void on_request(uint64 id, const td_api::openChat &request);

  void on_request(uint64 id, const td_api::closeChat &request);

  void on_request(uint64 id, const td_api::viewMessages &request);

  void on_request(uint64 id, const td_api::openMessageContent &request);

  void on_request(uint64 id, const td_api::clickAnimatedEmojiMessage &request);

  void on_request(uint64 id, const td_api::getInternalLink &request);

  void on_request(uint64 id, const td_api::getInternalLinkType &request);

  void on_request(uint64 id, td_api::getExternalLinkInfo &request);

  void on_request(uint64 id, td_api::getExternalLink &request);

  void on_request(uint64 id, const td_api::getChatHistory &request);

  void on_request(uint64 id, const td_api::deleteChatHistory &request);

  void on_request(uint64 id, const td_api::deleteChat &request);

  void on_request(uint64 id, const td_api::getMessageThreadHistory &request);

  void on_request(uint64 id, const td_api::getChatMessageCalendar &request);

  void on_request(uint64 id, td_api::searchChatMessages &request);

  void on_request(uint64 id, td_api::searchSecretMessages &request);

  void on_request(uint64 id, td_api::searchMessages &request);

  void on_request(uint64 id, td_api::searchSavedMessages &request);

  void on_request(uint64 id, const td_api::searchCallMessages &request);

  void on_request(uint64 id, td_api::searchOutgoingDocumentMessages &request);

  void on_request(uint64 id, td_api::searchPublicMessagesByTag &request);

  void on_request(uint64 id, td_api::searchPublicStoriesByTag &request);

  void on_request(uint64 id, td_api::searchPublicStoriesByLocation &request);

  void on_request(uint64 id, td_api::searchPublicStoriesByVenue &request);

  void on_request(uint64 id, td_api::getSearchedForTags &request);

  void on_request(uint64 id, td_api::removeSearchedForTag &request);

  void on_request(uint64 id, td_api::clearSearchedForTags &request);

  void on_request(uint64 id, const td_api::deleteAllCallMessages &request);

  void on_request(uint64 id, const td_api::searchChatRecentLocationMessages &request);

  void on_request(uint64 id, const td_api::getActiveLiveLocationMessages &request);

  void on_request(uint64 id, const td_api::getChatMessageByDate &request);

  void on_request(uint64 id, const td_api::getChatSparseMessagePositions &request);

  void on_request(uint64 id, const td_api::getChatMessageCount &request);

  void on_request(uint64 id, const td_api::getChatMessagePosition &request);

  void on_request(uint64 id, const td_api::getChatScheduledMessages &request);

  void on_request(uint64 id, const td_api::getEmojiReaction &request);

  void on_request(uint64 id, const td_api::getCustomEmojiReactionAnimations &request);

  void on_request(uint64 id, const td_api::getMessageAvailableReactions &request);

  void on_request(uint64 id, const td_api::clearRecentReactions &request);

  void on_request(uint64 id, const td_api::addMessageReaction &request);

  void on_request(uint64 id, const td_api::removeMessageReaction &request);

  void on_request(uint64 id, const td_api::setMessageReactions &request);

  void on_request(uint64 id, td_api::getMessageAddedReactions &request);

  void on_request(uint64 id, const td_api::setDefaultReactionType &request);

  void on_request(uint64 id, const td_api::getSavedMessagesTags &request);

  void on_request(uint64 id, td_api::setSavedMessagesTagLabel &request);

  void on_request(uint64 id, const td_api::getMessageEffect &request);

  void on_request(uint64 id, td_api::getMessagePublicForwards &request);

  void on_request(uint64 id, td_api::getStoryPublicForwards &request);

  void on_request(uint64 id, const td_api::removeNotification &request);

  void on_request(uint64 id, const td_api::removeNotificationGroup &request);

  void on_request(uint64 id, const td_api::deleteMessages &request);

  void on_request(uint64 id, const td_api::deleteChatMessagesBySender &request);

  void on_request(uint64 id, const td_api::deleteChatMessagesByDate &request);

  void on_request(uint64 id, const td_api::readAllChatMentions &request);

  void on_request(uint64 id, const td_api::readAllMessageThreadMentions &request);

  void on_request(uint64 id, const td_api::readAllChatReactions &request);

  void on_request(uint64 id, const td_api::readAllMessageThreadReactions &request);

  void on_request(uint64 id, const td_api::getChatAvailableMessageSenders &request);

  void on_request(uint64 id, const td_api::setChatMessageSender &request);

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

  void on_request(uint64 id, td_api::setMessageFactCheck &request);

  void on_request(uint64 id, td_api::sendBusinessMessage &request);

  void on_request(uint64 id, td_api::sendBusinessMessageAlbum &request);

  void on_request(uint64 id, td_api::editBusinessMessageText &request);

  void on_request(uint64 id, td_api::editBusinessMessageLiveLocation &request);

  void on_request(uint64 id, td_api::editBusinessMessageMedia &request);

  void on_request(uint64 id, td_api::editBusinessMessageCaption &request);

  void on_request(uint64 id, td_api::editBusinessMessageReplyMarkup &request);

  void on_request(uint64 id, td_api::stopBusinessPoll &request);

  void on_request(uint64 id, const td_api::loadQuickReplyShortcuts &request);

  void on_request(uint64 id, const td_api::setQuickReplyShortcutName &request);

  void on_request(uint64 id, const td_api::deleteQuickReplyShortcut &request);

  void on_request(uint64 id, const td_api::reorderQuickReplyShortcuts &request);

  void on_request(uint64 id, const td_api::loadQuickReplyShortcutMessages &request);

  void on_request(uint64 id, const td_api::deleteQuickReplyShortcutMessages &request);

  void on_request(uint64 id, td_api::addQuickReplyShortcutMessage &request);

  void on_request(uint64 id, td_api::addQuickReplyShortcutInlineQueryResultMessage &request);

  void on_request(uint64 id, td_api::addQuickReplyShortcutMessageAlbum &request);

  void on_request(uint64 id, td_api::readdQuickReplyShortcutMessages &request);

  void on_request(uint64 id, td_api::editQuickReplyMessage &request);

  void on_request(uint64 id, const td_api::getStory &request);

  void on_request(uint64 id, const td_api::getChatsToSendStories &request);

  void on_request(uint64 id, const td_api::canSendStory &request);

  void on_request(uint64 id, td_api::sendStory &request);

  void on_request(uint64 id, td_api::editStory &request);

  void on_request(uint64 id, td_api::setStoryPrivacySettings &request);

  void on_request(uint64 id, const td_api::toggleStoryIsPostedToChatPage &request);

  void on_request(uint64 id, const td_api::deleteStory &request);

  void on_request(uint64 id, const td_api::loadActiveStories &request);

  void on_request(uint64 id, const td_api::setChatActiveStoriesList &request);

  void on_request(uint64 id, const td_api::getForumTopicDefaultIcons &request);

  void on_request(uint64 id, td_api::createForumTopic &request);

  void on_request(uint64 id, td_api::editForumTopic &request);

  void on_request(uint64 id, const td_api::getForumTopic &request);

  void on_request(uint64 id, const td_api::getForumTopicLink &request);

  void on_request(uint64 id, td_api::getForumTopics &request);

  void on_request(uint64 id, const td_api::toggleForumTopicIsClosed &request);

  void on_request(uint64 id, const td_api::toggleGeneralForumTopicIsHidden &request);

  void on_request(uint64 id, const td_api::toggleForumTopicIsPinned &request);

  void on_request(uint64 id, const td_api::setPinnedForumTopics &request);

  void on_request(uint64 id, const td_api::deleteForumTopic &request);

  void on_request(uint64 id, td_api::setGameScore &request);

  void on_request(uint64 id, td_api::setInlineGameScore &request);

  void on_request(uint64 id, td_api::getGameHighScores &request);

  void on_request(uint64 id, td_api::getInlineGameHighScores &request);

  void on_request(uint64 id, const td_api::deleteChatReplyMarkup &request);

  void on_request(uint64 id, td_api::sendChatAction &request);

  void on_request(uint64 id, td_api::forwardMessages &request);

  void on_request(uint64 id, const td_api::sendQuickReplyShortcutMessages &request);

  void on_request(uint64 id, td_api::resendMessages &request);

  void on_request(uint64 id, td_api::getWebPagePreview &request);

  void on_request(uint64 id, td_api::getWebPageInstantView &request);

  void on_request(uint64 id, const td_api::createPrivateChat &request);

  void on_request(uint64 id, const td_api::createBasicGroupChat &request);

  void on_request(uint64 id, const td_api::createSupergroupChat &request);

  void on_request(uint64 id, const td_api::createSecretChat &request);

  void on_request(uint64 id, td_api::createNewBasicGroupChat &request);

  void on_request(uint64 id, td_api::createNewSupergroupChat &request);

  void on_request(uint64 id, const td_api::createNewSecretChat &request);

  void on_request(uint64 id, const td_api::createCall &request);

  void on_request(uint64 id, const td_api::acceptCall &request);

  void on_request(uint64 id, td_api::sendCallSignalingData &request);

  void on_request(uint64 id, const td_api::discardCall &request);

  void on_request(uint64 id, td_api::sendCallRating &request);

  void on_request(uint64 id, td_api::sendCallDebugInformation &request);

  void on_request(uint64 id, td_api::sendCallLog &request);

  void on_request(uint64 id, const td_api::getVideoChatAvailableParticipants &request);

  void on_request(uint64 id, const td_api::setVideoChatDefaultParticipant &request);

  void on_request(uint64 id, td_api::createVideoChat &request);

  void on_request(uint64 id, const td_api::getVideoChatRtmpUrl &request);

  void on_request(uint64 id, const td_api::replaceVideoChatRtmpUrl &request);

  void on_request(uint64 id, const td_api::getGroupCall &request);

  void on_request(uint64 id, const td_api::startScheduledGroupCall &request);

  void on_request(uint64 id, const td_api::toggleGroupCallEnabledStartNotification &request);

  void on_request(uint64 id, td_api::joinGroupCall &request);

  void on_request(uint64 id, td_api::startGroupCallScreenSharing &request);

  void on_request(uint64 id, const td_api::endGroupCallScreenSharing &request);

  void on_request(uint64 id, td_api::setGroupCallTitle &request);

  void on_request(uint64 id, const td_api::toggleGroupCallMuteNewParticipants &request);

  void on_request(uint64 id, const td_api::revokeGroupCallInviteLink &request);

  void on_request(uint64 id, const td_api::inviteGroupCallParticipants &request);

  void on_request(uint64 id, const td_api::getGroupCallInviteLink &request);

  void on_request(uint64 id, td_api::startGroupCallRecording &request);

  void on_request(uint64 id, const td_api::toggleGroupCallScreenSharingIsPaused &request);

  void on_request(uint64 id, const td_api::endGroupCallRecording &request);

  void on_request(uint64 id, const td_api::toggleGroupCallIsMyVideoPaused &request);

  void on_request(uint64 id, const td_api::toggleGroupCallIsMyVideoEnabled &request);

  void on_request(uint64 id, const td_api::setGroupCallParticipantIsSpeaking &request);

  void on_request(uint64 id, const td_api::toggleGroupCallParticipantIsMuted &request);

  void on_request(uint64 id, const td_api::setGroupCallParticipantVolumeLevel &request);

  void on_request(uint64 id, const td_api::toggleGroupCallParticipantIsHandRaised &request);

  void on_request(uint64 id, const td_api::loadGroupCallParticipants &request);

  void on_request(uint64 id, const td_api::leaveGroupCall &request);

  void on_request(uint64 id, const td_api::endGroupCall &request);

  void on_request(uint64 id, const td_api::getGroupCallStreams &request);

  void on_request(uint64 id, td_api::getGroupCallStreamSegment &request);

  void on_request(uint64 id, const td_api::upgradeBasicGroupChatToSupergroupChat &request);

  void on_request(uint64 id, const td_api::getChatListsToAddChat &request);

  void on_request(uint64 id, const td_api::addChatToList &request);

  void on_request(uint64 id, const td_api::getChatFolder &request);

  void on_request(uint64 id, const td_api::getRecommendedChatFolders &request);

  void on_request(uint64 id, td_api::createChatFolder &request);

  void on_request(uint64 id, td_api::editChatFolder &request);

  void on_request(uint64 id, const td_api::deleteChatFolder &request);

  void on_request(uint64 id, const td_api::getChatFolderChatsToLeave &request);

  void on_request(uint64 id, td_api::getChatFolderChatCount &request);

  void on_request(uint64 id, const td_api::reorderChatFolders &request);

  void on_request(uint64 id, const td_api::toggleChatFolderTags &request);

  void on_request(uint64 id, const td_api::getChatsForChatFolderInviteLink &request);

  void on_request(uint64 id, td_api::createChatFolderInviteLink &request);

  void on_request(uint64 id, td_api::getChatFolderInviteLinks &request);

  void on_request(uint64 id, td_api::editChatFolderInviteLink &request);

  void on_request(uint64 id, td_api::deleteChatFolderInviteLink &request);

  void on_request(uint64 id, td_api::checkChatFolderInviteLink &request);

  void on_request(uint64 id, td_api::addChatFolderByInviteLink &request);

  void on_request(uint64 id, const td_api::getChatFolderNewChats &request);

  void on_request(uint64 id, const td_api::processChatFolderNewChats &request);

  void on_request(uint64 id, const td_api::getArchiveChatListSettings &request);

  void on_request(uint64 id, td_api::setArchiveChatListSettings &request);

  void on_request(uint64 id, const td_api::getReadDatePrivacySettings &request);

  void on_request(uint64 id, td_api::setReadDatePrivacySettings &request);

  void on_request(uint64 id, const td_api::getNewChatPrivacySettings &request);

  void on_request(uint64 id, td_api::setNewChatPrivacySettings &request);

  void on_request(uint64 id, const td_api::canSendMessageToUser &request);

  void on_request(uint64 id, td_api::setChatTitle &request);

  void on_request(uint64 id, const td_api::setChatPhoto &request);

  void on_request(uint64 id, const td_api::setChatAccentColor &request);

  void on_request(uint64 id, const td_api::setChatProfileAccentColor &request);

  void on_request(uint64 id, const td_api::setChatMessageAutoDeleteTime &request);

  void on_request(uint64 id, const td_api::setChatEmojiStatus &request);

  void on_request(uint64 id, const td_api::setChatPermissions &request);

  void on_request(uint64 id, td_api::setChatBackground &request);

  void on_request(uint64 id, const td_api::deleteChatBackground &request);

  void on_request(uint64 id, td_api::setChatTheme &request);

  void on_request(uint64 id, td_api::setChatDraftMessage &request);

  void on_request(uint64 id, const td_api::toggleChatHasProtectedContent &request);

  void on_request(uint64 id, const td_api::toggleChatIsPinned &request);

  void on_request(uint64 id, const td_api::toggleChatViewAsTopics &request);

  void on_request(uint64 id, const td_api::toggleChatIsTranslatable &request);

  void on_request(uint64 id, const td_api::toggleChatIsMarkedAsUnread &request);

  void on_request(uint64 id, const td_api::setMessageSenderBlockList &request);

  void on_request(uint64 id, const td_api::toggleChatDefaultDisableNotification &request);

  void on_request(uint64 id, const td_api::setPinnedChats &request);

  void on_request(uint64 id, const td_api::readChatList &request);

  void on_request(uint64 id, const td_api::getStoryNotificationSettingsExceptions &request);

  void on_request(uint64 id, const td_api::getChatActiveStories &request);

  void on_request(uint64 id, const td_api::getChatPostedToChatPageStories &request);

  void on_request(uint64 id, const td_api::getChatArchivedStories &request);

  void on_request(uint64 id, const td_api::setChatPinnedStories &request);

  void on_request(uint64 id, const td_api::openStory &request);

  void on_request(uint64 id, const td_api::closeStory &request);

  void on_request(uint64 id, const td_api::getStoryAvailableReactions &request);

  void on_request(uint64 id, const td_api::setStoryReaction &request);

  void on_request(uint64 id, td_api::getStoryInteractions &request);

  void on_request(uint64 id, td_api::getChatStoryInteractions &request);

  void on_request(uint64 id, td_api::reportStory &request);

  void on_request(uint64 id, const td_api::activateStoryStealthMode &request);

  void on_request(uint64 id, const td_api::getChatBoostLevelFeatures &request);

  void on_request(uint64 id, const td_api::getChatBoostFeatures &request);

  void on_request(uint64 id, const td_api::getAvailableChatBoostSlots &request);

  void on_request(uint64 id, const td_api::getChatBoostStatus &request);

  void on_request(uint64 id, const td_api::boostChat &request);

  void on_request(uint64 id, const td_api::getChatBoostLink &request);

  void on_request(uint64 id, td_api::getChatBoostLinkInfo &request);

  void on_request(uint64 id, td_api::getChatBoosts &request);

  void on_request(uint64 id, const td_api::getUserChatBoosts &request);

  void on_request(uint64 id, const td_api::getAttachmentMenuBot &request);

  void on_request(uint64 id, const td_api::toggleBotIsAddedToAttachmentMenu &request);

  void on_request(uint64 id, td_api::setChatAvailableReactions &request);

  void on_request(uint64 id, td_api::setChatClientData &request);

  void on_request(uint64 id, td_api::setChatDescription &request);

  void on_request(uint64 id, const td_api::setChatDiscussionGroup &request);

  void on_request(uint64 id, td_api::setChatLocation &request);

  void on_request(uint64 id, const td_api::setChatSlowModeDelay &request);

  void on_request(uint64 id, const td_api::pinChatMessage &request);

  void on_request(uint64 id, const td_api::unpinChatMessage &request);

  void on_request(uint64 id, const td_api::unpinAllChatMessages &request);

  void on_request(uint64 id, const td_api::unpinAllMessageThreadMessages &request);

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

  void on_request(uint64 id, const td_api::getChatAdministrators &request);

  void on_request(uint64 id, const td_api::replacePrimaryChatInviteLink &request);

  void on_request(uint64 id, td_api::createChatInviteLink &request);

  void on_request(uint64 id, td_api::editChatInviteLink &request);

  void on_request(uint64 id, td_api::getChatInviteLink &request);

  void on_request(uint64 id, const td_api::getChatInviteLinkCounts &request);

  void on_request(uint64 id, td_api::getChatInviteLinks &request);

  void on_request(uint64 id, td_api::getChatInviteLinkMembers &request);

  void on_request(uint64 id, td_api::getChatJoinRequests &request);

  void on_request(uint64 id, const td_api::processChatJoinRequest &request);

  void on_request(uint64 id, td_api::processChatJoinRequests &request);

  void on_request(uint64 id, td_api::revokeChatInviteLink &request);

  void on_request(uint64 id, td_api::deleteRevokedChatInviteLink &request);

  void on_request(uint64 id, const td_api::deleteAllRevokedChatInviteLinks &request);

  void on_request(uint64 id, td_api::checkChatInviteLink &request);

  void on_request(uint64 id, td_api::joinChatByInviteLink &request);

  void on_request(uint64 id, td_api::getChatEventLog &request);

  void on_request(uint64 id, const td_api::getTimeZones &request);

  void on_request(uint64 id, const td_api::clearAllDraftMessages &request);

  void on_request(uint64 id, const td_api::downloadFile &request);

  void on_request(uint64 id, const td_api::getFileDownloadedPrefixSize &request);

  void on_request(uint64 id, const td_api::cancelDownloadFile &request);

  void on_request(uint64 id, const td_api::getSuggestedFileName &request);

  void on_request(uint64 id, td_api::preliminaryUploadFile &request);

  void on_request(uint64 id, const td_api::cancelPreliminaryUploadFile &request);

  void on_request(uint64 id, td_api::writeGeneratedFilePart &request);

  void on_request(uint64 id, const td_api::setFileGenerationProgress &request);

  void on_request(uint64 id, td_api::finishFileGeneration &request);

  void on_request(uint64 id, const td_api::readFilePart &request);

  void on_request(uint64 id, const td_api::deleteFile &request);

  void on_request(uint64 id, const td_api::addFileToDownloads &request);

  void on_request(uint64 id, const td_api::toggleDownloadIsPaused &request);

  void on_request(uint64 id, const td_api::toggleAllDownloadsArePaused &request);

  void on_request(uint64 id, const td_api::removeFileFromDownloads &request);

  void on_request(uint64 id, const td_api::removeAllFilesFromDownloads &request);

  void on_request(uint64 id, td_api::searchFileDownloads &request);

  void on_request(uint64 id, td_api::setApplicationVerificationToken &request);

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

  void on_request(uint64 id, const td_api::getCloseFriends &request);

  void on_request(uint64 id, const td_api::setCloseFriends &request);

  void on_request(uint64 id, td_api::setUserPersonalProfilePhoto &request);

  void on_request(uint64 id, td_api::suggestUserProfilePhoto &request);

  void on_request(uint64 id, td_api::searchUserByPhoneNumber &request);

  void on_request(uint64 id, const td_api::sharePhoneNumber &request);

  void on_request(uint64 id, const td_api::getRecentInlineBots &request);

  void on_request(uint64 id, td_api::setName &request);

  void on_request(uint64 id, td_api::setBio &request);

  void on_request(uint64 id, td_api::setUsername &request);

  void on_request(uint64 id, td_api::toggleUsernameIsActive &request);

  void on_request(uint64 id, td_api::reorderActiveUsernames &request);

  void on_request(uint64 id, td_api::setBirthdate &request);

  void on_request(uint64 id, const td_api::setPersonalChat &request);

  void on_request(uint64 id, const td_api::setEmojiStatus &request);

  void on_request(uint64 id, const td_api::toggleHasSponsoredMessagesEnabled &request);

  void on_request(uint64 id, const td_api::getThemedEmojiStatuses &request);

  void on_request(uint64 id, const td_api::getThemedChatEmojiStatuses &request);

  void on_request(uint64 id, const td_api::getDefaultEmojiStatuses &request);

  void on_request(uint64 id, const td_api::getDefaultChatEmojiStatuses &request);

  void on_request(uint64 id, const td_api::getRecentEmojiStatuses &request);

  void on_request(uint64 id, const td_api::clearRecentEmojiStatuses &request);

  void on_request(uint64 id, td_api::setCommands &request);

  void on_request(uint64 id, td_api::deleteCommands &request);

  void on_request(uint64 id, td_api::getCommands &request);

  void on_request(uint64 id, td_api::setMenuButton &request);

  void on_request(uint64 id, const td_api::getMenuButton &request);

  void on_request(uint64 id, const td_api::setDefaultGroupAdministratorRights &request);

  void on_request(uint64 id, const td_api::setDefaultChannelAdministratorRights &request);

  void on_request(uint64 id, const td_api::canBotSendMessages &request);

  void on_request(uint64 id, const td_api::allowBotToSendMessages &request);

  void on_request(uint64 id, td_api::sendWebAppCustomRequest &request);

  void on_request(uint64 id, td_api::setBotName &request);

  void on_request(uint64 id, const td_api::getBotName &request);

  void on_request(uint64 id, td_api::setBotProfilePhoto &request);

  void on_request(uint64 id, td_api::toggleBotUsernameIsActive &request);

  void on_request(uint64 id, td_api::reorderBotActiveUsernames &request);

  void on_request(uint64 id, td_api::setBotInfoDescription &request);

  void on_request(uint64 id, const td_api::getBotInfoDescription &request);

  void on_request(uint64 id, td_api::setBotInfoShortDescription &request);

  void on_request(uint64 id, const td_api::getBotInfoShortDescription &request);

  void on_request(uint64 id, const td_api::setLocation &request);

  void on_request(uint64 id, td_api::setBusinessLocation &request);

  void on_request(uint64 id, td_api::setBusinessOpeningHours &request);

  void on_request(uint64 id, td_api::setBusinessGreetingMessageSettings &request);

  void on_request(uint64 id, td_api::setBusinessAwayMessageSettings &request);

  void on_request(uint64 id, td_api::setBusinessStartPage &request);

  void on_request(uint64 id, td_api::setProfilePhoto &request);

  void on_request(uint64 id, const td_api::deleteProfilePhoto &request);

  void on_request(uint64 id, const td_api::getUserProfilePhotos &request);

  void on_request(uint64 id, const td_api::setAccentColor &request);

  void on_request(uint64 id, const td_api::setProfileAccentColor &request);

  void on_request(uint64 id, const td_api::getBusinessConnectedBot &request);

  void on_request(uint64 id, td_api::setBusinessConnectedBot &request);

  void on_request(uint64 id, const td_api::deleteBusinessConnectedBot &request);

  void on_request(uint64 id, const td_api::toggleBusinessConnectedBotChatIsPaused &request);

  void on_request(uint64 id, const td_api::removeBusinessConnectedBotFromChat &request);

  void on_request(uint64 id, const td_api::getBusinessChatLinks &request);

  void on_request(uint64 id, td_api::createBusinessChatLink &request);

  void on_request(uint64 id, td_api::editBusinessChatLink &request);

  void on_request(uint64 id, td_api::deleteBusinessChatLink &request);

  void on_request(uint64 id, td_api::getBusinessChatLinkInfo &request);

  void on_request(uint64 id, td_api::setSupergroupUsername &request);

  void on_request(uint64 id, td_api::toggleSupergroupUsernameIsActive &request);

  void on_request(uint64 id, const td_api::disableAllSupergroupUsernames &request);

  void on_request(uint64 id, td_api::reorderSupergroupActiveUsernames &request);

  void on_request(uint64 id, const td_api::setSupergroupStickerSet &request);

  void on_request(uint64 id, const td_api::setSupergroupCustomEmojiStickerSet &request);

  void on_request(uint64 id, const td_api::setSupergroupUnrestrictBoostCount &request);

  void on_request(uint64 id, const td_api::toggleSupergroupSignMessages &request);

  void on_request(uint64 id, const td_api::toggleSupergroupJoinToSendMessages &request);

  void on_request(uint64 id, const td_api::toggleSupergroupJoinByRequest &request);

  void on_request(uint64 id, const td_api::toggleSupergroupIsAllHistoryAvailable &request);

  void on_request(uint64 id, const td_api::toggleSupergroupCanHaveSponsoredMessages &request);

  void on_request(uint64 id, const td_api::toggleSupergroupHasHiddenMembers &request);

  void on_request(uint64 id, const td_api::toggleSupergroupHasAggressiveAntiSpamEnabled &request);

  void on_request(uint64 id, const td_api::toggleSupergroupIsForum &request);

  void on_request(uint64 id, const td_api::toggleSupergroupIsBroadcastGroup &request);

  void on_request(uint64 id, const td_api::reportSupergroupSpam &request);

  void on_request(uint64 id, const td_api::reportSupergroupAntiSpamFalsePositive &request);

  void on_request(uint64 id, td_api::getSupergroupMembers &request);

  void on_request(uint64 id, td_api::closeSecretChat &request);

  void on_request(uint64 id, td_api::getStickers &request);

  void on_request(uint64 id, td_api::getAllStickerEmojis &request);

  void on_request(uint64 id, td_api::searchStickers &request);

  void on_request(uint64 id, const td_api::getGreetingStickers &request);

  void on_request(uint64 id, const td_api::getPremiumStickers &request);

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

  void on_request(uint64 id, td_api::getSuggestedStickerSetName &request);

  void on_request(uint64 id, td_api::checkStickerSetName &request);

  void on_request(uint64 id, td_api::createNewStickerSet &request);

  void on_request(uint64 id, td_api::addStickerToSet &request);

  void on_request(uint64 id, td_api::replaceStickerInSet &request);

  void on_request(uint64 id, td_api::setStickerSetThumbnail &request);

  void on_request(uint64 id, td_api::setCustomEmojiStickerSetThumbnail &request);

  void on_request(uint64 id, td_api::setStickerSetTitle &request);

  void on_request(uint64 id, td_api::deleteStickerSet &request);

  void on_request(uint64 id, td_api::setStickerPositionInSet &request);

  void on_request(uint64 id, const td_api::removeStickerFromSet &request);

  void on_request(uint64 id, td_api::setStickerEmojis &request);

  void on_request(uint64 id, td_api::setStickerKeywords &request);

  void on_request(uint64 id, td_api::setStickerMaskPosition &request);

  void on_request(uint64 id, const td_api::getOwnedStickerSets &request);

  void on_request(uint64 id, const td_api::getRecentStickers &request);

  void on_request(uint64 id, td_api::addRecentSticker &request);

  void on_request(uint64 id, td_api::removeRecentSticker &request);

  void on_request(uint64 id, td_api::clearRecentStickers &request);

  void on_request(uint64 id, const td_api::getSavedAnimations &request);

  void on_request(uint64 id, td_api::addSavedAnimation &request);

  void on_request(uint64 id, td_api::removeSavedAnimation &request);

  void on_request(uint64 id, td_api::getStickerEmojis &request);

  void on_request(uint64 id, td_api::searchEmojis &request);

  void on_request(uint64 id, td_api::getKeywordEmojis &request);

  void on_request(uint64 id, const td_api::getEmojiCategories &request);

  void on_request(uint64 id, td_api::getAnimatedEmoji &request);

  void on_request(uint64 id, td_api::getEmojiSuggestionsUrl &request);

  void on_request(uint64 id, const td_api::getCustomEmojiStickers &request);

  void on_request(uint64 id, const td_api::getDefaultChatPhotoCustomEmojiStickers &request);

  void on_request(uint64 id, const td_api::getDefaultProfilePhotoCustomEmojiStickers &request);

  void on_request(uint64 id, const td_api::getDefaultBackgroundCustomEmojiStickers &request);

  void on_request(uint64 id, const td_api::getDisallowedChatEmojiStatuses &request);

  void on_request(uint64 id, const td_api::getFavoriteStickers &request);

  void on_request(uint64 id, td_api::addFavoriteSticker &request);

  void on_request(uint64 id, td_api::removeFavoriteSticker &request);

  void on_request(uint64 id, const td_api::getSavedNotificationSound &request);

  void on_request(uint64 id, const td_api::getSavedNotificationSounds &request);

  void on_request(uint64 id, td_api::addSavedNotificationSound &request);

  void on_request(uint64 id, const td_api::removeSavedNotificationSound &request);

  void on_request(uint64 id, const td_api::getChatNotificationSettingsExceptions &request);

  void on_request(uint64 id, const td_api::getScopeNotificationSettings &request);

  void on_request(uint64 id, td_api::setChatNotificationSettings &request);

  void on_request(uint64 id, td_api::setForumTopicNotificationSettings &request);

  void on_request(uint64 id, td_api::setScopeNotificationSettings &request);

  void on_request(uint64 id, td_api::setReactionNotificationSettings &request);

  void on_request(uint64 id, const td_api::resetAllNotificationSettings &request);

  void on_request(uint64 id, const td_api::removeChatActionBar &request);

  void on_request(uint64 id, td_api::reportChat &request);

  void on_request(uint64 id, td_api::reportChatPhoto &request);

  void on_request(uint64 id, const td_api::reportMessageReactions &request);

  void on_request(uint64 id, const td_api::getChatStatistics &request);

  void on_request(uint64 id, const td_api::getChatRevenueStatistics &request);

  void on_request(uint64 id, const td_api::getChatRevenueWithdrawalUrl &request);

  void on_request(uint64 id, const td_api::getChatRevenueTransactions &request);

  void on_request(uint64 id, const td_api::getStarRevenueStatistics &request);

  void on_request(uint64 id, const td_api::getStarWithdrawalUrl &request);

  void on_request(uint64 id, const td_api::getStarAdAccountUrl &request);

  void on_request(uint64 id, const td_api::getMessageStatistics &request);

  void on_request(uint64 id, const td_api::getStoryStatistics &request);

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

  void on_request(uint64 id, const td_api::hideContactCloseBirthdays &request);

  void on_request(uint64 id, td_api::getBusinessConnection &request);

  void on_request(uint64 id, const td_api::getLoginUrlInfo &request);

  void on_request(uint64 id, const td_api::getLoginUrl &request);

  void on_request(uint64 id, const td_api::shareUsersWithBot &request);

  void on_request(uint64 id, const td_api::shareChatWithBot &request);

  void on_request(uint64 id, td_api::getInlineQueryResults &request);

  void on_request(uint64 id, td_api::answerInlineQuery &request);

  void on_request(uint64 id, td_api::searchWebApp &request);

  void on_request(uint64 id, td_api::getWebAppLinkUrl &request);

  void on_request(uint64 id, td_api::getWebAppUrl &request);

  void on_request(uint64 id, td_api::sendWebAppData &request);

  void on_request(uint64 id, td_api::openWebApp &request);

  void on_request(uint64 id, const td_api::closeWebApp &request);

  void on_request(uint64 id, td_api::answerWebAppQuery &request);

  void on_request(uint64 id, td_api::getCallbackQueryAnswer &request);

  void on_request(uint64 id, td_api::answerCallbackQuery &request);

  void on_request(uint64 id, td_api::answerShippingQuery &request);

  void on_request(uint64 id, td_api::answerPreCheckoutQuery &request);

  void on_request(uint64 id, td_api::getBankCardInfo &request);

  void on_request(uint64 id, td_api::getPaymentForm &request);

  void on_request(uint64 id, td_api::validateOrderInfo &request);

  void on_request(uint64 id, td_api::sendPaymentForm &request);

  void on_request(uint64 id, const td_api::getPaymentReceipt &request);

  void on_request(uint64 id, const td_api::getSavedOrderInfo &request);

  void on_request(uint64 id, const td_api::deleteSavedOrderInfo &request);

  void on_request(uint64 id, const td_api::deleteSavedCredentials &request);

  void on_request(uint64 id, td_api::createInvoiceLink &request);

  void on_request(uint64 id, td_api::refundStarPayment &request);

  void on_request(uint64 id, td_api::getPassportElement &request);

  void on_request(uint64 id, td_api::getAllPassportElements &request);

  void on_request(uint64 id, td_api::setPassportElement &request);

  void on_request(uint64 id, const td_api::deletePassportElement &request);

  void on_request(uint64 id, td_api::setPassportElementErrors &request);

  void on_request(uint64 id, td_api::getPreferredCountryLanguage &request);

  void on_request(uint64 id, td_api::sendEmailAddressVerificationCode &request);

  void on_request(uint64 id, const td_api::resendEmailAddressVerificationCode &request);

  void on_request(uint64 id, td_api::checkEmailAddressVerificationCode &request);

  void on_request(uint64 id, td_api::getPassportAuthorizationForm &request);

  void on_request(uint64 id, td_api::getPassportAuthorizationFormAvailableElements &request);

  void on_request(uint64 id, td_api::sendPassportAuthorizationForm &request);

  void on_request(uint64 id, const td_api::getSupportUser &request);

  void on_request(uint64 id, const td_api::getInstalledBackgrounds &request);

  void on_request(uint64 id, td_api::getBackgroundUrl &request);

  void on_request(uint64 id, td_api::searchBackground &request);

  void on_request(uint64 id, td_api::setDefaultBackground &request);

  void on_request(uint64 id, const td_api::deleteDefaultBackground &request);

  void on_request(uint64 id, const td_api::removeInstalledBackground &request);

  void on_request(uint64 id, const td_api::resetInstalledBackgrounds &request);

  void on_request(uint64 id, td_api::getRecentlyVisitedTMeUrls &request);

  void on_request(uint64 id, td_api::setBotUpdatesStatus &request);

  void on_request(uint64 id, td_api::sendCustomRequest &request);

  void on_request(uint64 id, td_api::answerCustomQuery &request);

  void on_request(uint64 id, const td_api::setAlarm &request);

  void on_request(uint64 id, td_api::searchHashtags &request);

  void on_request(uint64 id, td_api::removeRecentHashtag &request);

  void on_request(uint64 id, const td_api::getPremiumLimit &request);

  void on_request(uint64 id, const td_api::getPremiumFeatures &request);

  void on_request(uint64 id, const td_api::getPremiumStickerExamples &request);

  void on_request(uint64 id, const td_api::viewPremiumFeature &request);

  void on_request(uint64 id, const td_api::clickPremiumSubscriptionButton &request);

  void on_request(uint64 id, const td_api::getPremiumState &request);

  void on_request(uint64 id, const td_api::getPremiumGiftCodePaymentOptions &request);

  void on_request(uint64 id, td_api::checkPremiumGiftCode &request);

  void on_request(uint64 id, td_api::applyPremiumGiftCode &request);

  void on_request(uint64 id, td_api::launchPrepaidPremiumGiveaway &request);

  void on_request(uint64 id, const td_api::getPremiumGiveawayInfo &request);

  void on_request(uint64 id, const td_api::getStarPaymentOptions &request);

  void on_request(uint64 id, td_api::getStarTransactions &request);

  void on_request(uint64 id, td_api::canPurchaseFromStore &request);

  void on_request(uint64 id, td_api::assignAppStoreTransaction &request);

  void on_request(uint64 id, td_api::assignGooglePlayTransaction &request);

  void on_request(uint64 id, const td_api::getBusinessFeatures &request);

  void on_request(uint64 id, td_api::acceptTermsOfService &request);

  void on_request(uint64 id, const td_api::getCountries &request);

  void on_request(uint64 id, const td_api::getCountryCode &request);

  void on_request(uint64 id, const td_api::getPhoneNumberInfo &request);

  void on_request(uint64 id, td_api::getCollectibleItemInfo &request);

  void on_request(uint64 id, const td_api::getApplicationDownloadLink &request);

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

  void on_request(uint64 id, const td_api::getUserSupportInfo &request);

  void on_request(uint64 id, td_api::setUserSupportInfo &request);

  void on_request(uint64 id, const td_api::getSupportName &request);

  void on_request(uint64 id, const td_api::searchQuote &request);

  void on_request(uint64 id, const td_api::getTextEntities &request);

  void on_request(uint64 id, const td_api::parseTextEntities &request);

  void on_request(uint64 id, const td_api::parseMarkdown &request);

  void on_request(uint64 id, const td_api::getMarkdownText &request);

  void on_request(uint64 id, const td_api::searchStringsByPrefix &request);

  void on_request(uint64 id, const td_api::checkQuickReplyShortcutName &request);

  void on_request(uint64 id, const td_api::getCountryFlagEmoji &request);

  void on_request(uint64 id, const td_api::getFileMimeType &request);

  void on_request(uint64 id, const td_api::getFileExtension &request);

  void on_request(uint64 id, const td_api::cleanFileName &request);

  void on_request(uint64 id, const td_api::getLanguagePackString &request);

  void on_request(uint64 id, const td_api::getPhoneNumberInfoSync &request);

  void on_request(uint64 id, const td_api::getPushReceiverId &request);

  void on_request(uint64 id, const td_api::getChatFolderDefaultIconName &request);

  void on_request(uint64 id, const td_api::getJsonValue &request);

  void on_request(uint64 id, const td_api::getJsonString &request);

  void on_request(uint64 id, const td_api::getThemeParametersJsonString &request);

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
  static td_api::object_ptr<td_api::Object> do_static_request(const td_api::getOption &request);
  static td_api::object_ptr<td_api::Object> do_static_request(td_api::searchQuote &request);
  static td_api::object_ptr<td_api::Object> do_static_request(const td_api::getTextEntities &request);
  static td_api::object_ptr<td_api::Object> do_static_request(td_api::parseTextEntities &request);
  static td_api::object_ptr<td_api::Object> do_static_request(td_api::parseMarkdown &request);
  static td_api::object_ptr<td_api::Object> do_static_request(td_api::getMarkdownText &request);
  static td_api::object_ptr<td_api::Object> do_static_request(td_api::searchStringsByPrefix &request);
  static td_api::object_ptr<td_api::Object> do_static_request(const td_api::checkQuickReplyShortcutName &request);
  static td_api::object_ptr<td_api::Object> do_static_request(const td_api::getCountryFlagEmoji &request);
  static td_api::object_ptr<td_api::Object> do_static_request(const td_api::getFileMimeType &request);
  static td_api::object_ptr<td_api::Object> do_static_request(const td_api::getFileExtension &request);
  static td_api::object_ptr<td_api::Object> do_static_request(const td_api::cleanFileName &request);
  static td_api::object_ptr<td_api::Object> do_static_request(const td_api::getLanguagePackString &request);
  static td_api::object_ptr<td_api::Object> do_static_request(td_api::getPhoneNumberInfoSync &request);
  static td_api::object_ptr<td_api::Object> do_static_request(const td_api::getPushReceiverId &request);
  static td_api::object_ptr<td_api::Object> do_static_request(const td_api::getChatFolderDefaultIconName &request);
  static td_api::object_ptr<td_api::Object> do_static_request(td_api::getJsonValue &request);
  static td_api::object_ptr<td_api::Object> do_static_request(const td_api::getJsonString &request);
  static td_api::object_ptr<td_api::Object> do_static_request(const td_api::getThemeParametersJsonString &request);
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

  struct Parameters {
    int32 api_id_ = 0;
    string api_hash_;
    bool use_secret_chats_ = false;
  };

  void finish_set_parameters();

  void init(Parameters parameters, Result<TdDb::OpenedDatabase> r_opened_database);

  void init_options_and_network();

  void init_file_manager();

  void init_non_actor_managers();

  void init_managers();

  void init_pure_actor_managers();

  void process_binlog_events(TdDb::OpenedDatabase &&events);

  void clear();

  void close_impl(bool destroy_flag);

  Result<std::pair<Parameters, TdDb::Parameters>> get_parameters(
      td_api::object_ptr<td_api::setTdlibParameters> parameters) TD_WARN_UNUSED_RESULT;

  static td_api::object_ptr<td_api::error> make_error(int32 code, CSlice error) {
    return td_api::make_object<td_api::error>(code, error.str());
  }

  // Actor
  void start_up() final;
  void tear_down() final;
  void hangup_shared() final;
  void hangup() final;
};

}  // namespace td
