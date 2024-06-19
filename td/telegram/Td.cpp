//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/Td.h"

#include "td/telegram/AccentColorId.h"
#include "td/telegram/AccountManager.h"
#include "td/telegram/AnimationsManager.h"
#include "td/telegram/Application.h"
#include "td/telegram/AttachMenuManager.h"
#include "td/telegram/AudiosManager.h"
#include "td/telegram/AuthManager.h"
#include "td/telegram/AutoDownloadSettings.h"
#include "td/telegram/AutosaveManager.h"
#include "td/telegram/BackgroundId.h"
#include "td/telegram/BackgroundManager.h"
#include "td/telegram/BackgroundType.h"
#include "td/telegram/Birthdate.h"
#include "td/telegram/BoostManager.h"
#include "td/telegram/BotCommand.h"
#include "td/telegram/BotInfoManager.h"
#include "td/telegram/BotMenuButton.h"
#include "td/telegram/BusinessAwayMessage.h"
#include "td/telegram/BusinessConnectionId.h"
#include "td/telegram/BusinessConnectionManager.h"
#include "td/telegram/BusinessGreetingMessage.h"
#include "td/telegram/BusinessIntro.h"
#include "td/telegram/BusinessManager.h"
#include "td/telegram/BusinessWorkHours.h"
#include "td/telegram/CallbackQueriesManager.h"
#include "td/telegram/CallId.h"
#include "td/telegram/CallManager.h"
#include "td/telegram/ChannelId.h"
#include "td/telegram/ChannelRecommendationManager.h"
#include "td/telegram/ChannelType.h"
#include "td/telegram/ChatId.h"
#include "td/telegram/ChatManager.h"
#include "td/telegram/CommonDialogManager.h"
#include "td/telegram/ConfigManager.h"
#include "td/telegram/CountryInfoManager.h"
#include "td/telegram/CustomEmojiId.h"
#include "td/telegram/DeviceTokenManager.h"
#include "td/telegram/DialogAction.h"
#include "td/telegram/DialogActionManager.h"
#include "td/telegram/DialogBoostLinkInfo.h"
#include "td/telegram/DialogEventLog.h"
#include "td/telegram/DialogFilter.h"
#include "td/telegram/DialogFilterId.h"
#include "td/telegram/DialogFilterManager.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/DialogInviteLinkManager.h"
#include "td/telegram/DialogListId.h"
#include "td/telegram/DialogLocation.h"
#include "td/telegram/DialogManager.h"
#include "td/telegram/DialogParticipant.h"
#include "td/telegram/DialogParticipantFilter.h"
#include "td/telegram/DialogParticipantManager.h"
#include "td/telegram/DialogSource.h"
#include "td/telegram/DocumentsManager.h"
#include "td/telegram/DownloadManager.h"
#include "td/telegram/DownloadManagerCallback.h"
#include "td/telegram/EmailVerification.h"
#include "td/telegram/EmojiGroupType.h"
#include "td/telegram/EmojiStatus.h"
#include "td/telegram/FileReferenceManager.h"
#include "td/telegram/files/FileGcParameters.h"
#include "td/telegram/files/FileId.h"
#include "td/telegram/files/FileManager.h"
#include "td/telegram/files/FileSourceId.h"
#include "td/telegram/files/FileStats.h"
#include "td/telegram/files/FileType.h"
#include "td/telegram/ForumTopicManager.h"
#include "td/telegram/GameManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/GlobalPrivacySettings.h"
#include "td/telegram/GroupCallId.h"
#include "td/telegram/GroupCallManager.h"
#include "td/telegram/HashtagHints.h"
#include "td/telegram/InlineMessageManager.h"
#include "td/telegram/InlineQueriesManager.h"
#include "td/telegram/JsonValue.h"
#include "td/telegram/LanguagePackManager.h"
#include "td/telegram/LinkManager.h"
#include "td/telegram/Location.h"
#include "td/telegram/Logging.h"
#include "td/telegram/MessageCopyOptions.h"
#include "td/telegram/MessageEffectId.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/MessageFullId.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/MessageImportManager.h"
#include "td/telegram/MessageLinkInfo.h"
#include "td/telegram/MessageQuote.h"
#include "td/telegram/MessageReaction.h"
#include "td/telegram/MessageSearchFilter.h"
#include "td/telegram/MessageSender.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/MessageSource.h"
#include "td/telegram/MessageThreadInfo.h"
#include "td/telegram/MessageTtl.h"
#include "td/telegram/misc.h"
#include "td/telegram/net/ConnectionCreator.h"
#include "td/telegram/net/MtprotoHeader.h"
#include "td/telegram/net/NetQuery.h"
#include "td/telegram/net/NetQueryDelayer.h"
#include "td/telegram/net/NetQueryDispatcher.h"
#include "td/telegram/net/NetStatsManager.h"
#include "td/telegram/net/NetType.h"
#include "td/telegram/net/Proxy.h"
#include "td/telegram/net/PublicRsaKeySharedMain.h"
#include "td/telegram/net/TempAuthKeyWatchdog.h"
#include "td/telegram/NotificationGroupId.h"
#include "td/telegram/NotificationId.h"
#include "td/telegram/NotificationManager.h"
#include "td/telegram/NotificationObjectId.h"
#include "td/telegram/NotificationSettingsManager.h"
#include "td/telegram/NotificationSettingsScope.h"
#include "td/telegram/OptionManager.h"
#include "td/telegram/PasswordManager.h"
#include "td/telegram/Payments.h"
#include "td/telegram/PeopleNearbyManager.h"
#include "td/telegram/PhoneNumberManager.h"
#include "td/telegram/PhotoSizeSource.h"
#include "td/telegram/PollManager.h"
#include "td/telegram/Premium.h"
#include "td/telegram/PrivacyManager.h"
#include "td/telegram/PublicDialogType.h"
#include "td/telegram/QuickReplyManager.h"
#include "td/telegram/ReactionManager.h"
#include "td/telegram/ReactionNotificationSettings.h"
#include "td/telegram/ReactionType.h"
#include "td/telegram/ReportReason.h"
#include "td/telegram/RequestActor.h"
#include "td/telegram/SavedMessagesManager.h"
#include "td/telegram/SavedMessagesTopicId.h"
#include "td/telegram/ScopeNotificationSettings.h"
#include "td/telegram/SecretChatId.h"
#include "td/telegram/SecretChatsManager.h"
#include "td/telegram/SecureManager.h"
#include "td/telegram/SecureValue.h"
#include "td/telegram/SentEmailCode.h"
#include "td/telegram/SponsoredMessageManager.h"
#include "td/telegram/StarManager.h"
#include "td/telegram/StateManager.h"
#include "td/telegram/StatisticsManager.h"
#include "td/telegram/StickerFormat.h"
#include "td/telegram/StickerListType.h"
#include "td/telegram/StickerSetId.h"
#include "td/telegram/StickersManager.h"
#include "td/telegram/StickerType.h"
#include "td/telegram/StorageManager.h"
#include "td/telegram/StoryId.h"
#include "td/telegram/StoryListId.h"
#include "td/telegram/StoryManager.h"
#include "td/telegram/SuggestedAction.h"
#include "td/telegram/Support.h"
#include "td/telegram/td_api.hpp"
#include "td/telegram/TdDb.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/ThemeManager.h"
#include "td/telegram/TimeZoneManager.h"
#include "td/telegram/TopDialogCategory.h"
#include "td/telegram/TopDialogManager.h"
#include "td/telegram/TranscriptionManager.h"
#include "td/telegram/TranslationManager.h"
#include "td/telegram/UpdatesManager.h"
#include "td/telegram/UserId.h"
#include "td/telegram/UserManager.h"
#include "td/telegram/Version.h"
#include "td/telegram/VideoNotesManager.h"
#include "td/telegram/VideosManager.h"
#include "td/telegram/VoiceNotesManager.h"
#include "td/telegram/WebPageId.h"
#include "td/telegram/WebPagesManager.h"

#include "td/db/binlog/BinlogEvent.h"

#include "td/mtproto/DhCallback.h"
#include "td/mtproto/Handshake.h"
#include "td/mtproto/HandshakeActor.h"
#include "td/mtproto/RawConnection.h"
#include "td/mtproto/RSA.h"
#include "td/mtproto/TransportType.h"

#include "td/actor/actor.h"

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"
#include "td/utils/filesystem.h"
#include "td/utils/format.h"
#include "td/utils/MimeType.h"
#include "td/utils/misc.h"
#include "td/utils/PathView.h"
#include "td/utils/port/IPAddress.h"
#include "td/utils/port/SocketFd.h"
#include "td/utils/port/uname.h"
#include "td/utils/Random.h"
#include "td/utils/Slice.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/Status.h"
#include "td/utils/Timer.h"
#include "td/utils/utf8.h"

#include <limits>
#include <tuple>
#include <type_traits>

namespace td {

int VERBOSITY_NAME(td_init) = VERBOSITY_NAME(DEBUG) + 3;
int VERBOSITY_NAME(td_requests) = VERBOSITY_NAME(INFO);

void Td::ResultHandler::set_td(Td *td) {
  CHECK(td_ == nullptr);
  td_ = td;
}

void Td::ResultHandler::send_query(NetQueryPtr query) {
  CHECK(!is_query_sent_);
  is_query_sent_ = true;
  td_->add_handler(query->id(), shared_from_this());
  query->debug("Send to NetQueryDispatcher");
  G()->net_query_dispatcher().dispatch(std::move(query));
}

class GetPromoDataQuery final : public Td::ResultHandler {
  Promise<telegram_api::object_ptr<telegram_api::help_PromoData>> promise_;

 public:
  explicit GetPromoDataQuery(Promise<telegram_api::object_ptr<telegram_api::help_PromoData>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send() {
    // we don't poll promo data before authorization
    send_query(G()->net_query_creator().create(telegram_api::help_getPromoData()));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::help_getPromoData>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    promise_.set_value(result_ptr.move_as_ok());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetRecentMeUrlsQuery final : public Td::ResultHandler {
  Promise<tl_object_ptr<td_api::tMeUrls>> promise_;

 public:
  explicit GetRecentMeUrlsQuery(Promise<tl_object_ptr<td_api::tMeUrls>> &&promise) : promise_(std::move(promise)) {
  }

  void send(const string &referrer) {
    send_query(G()->net_query_creator().create(telegram_api::help_getRecentMeUrls(referrer)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::help_getRecentMeUrls>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto urls_full = result_ptr.move_as_ok();
    td_->user_manager_->on_get_users(std::move(urls_full->users_), "GetRecentMeUrlsQuery");
    td_->chat_manager_->on_get_chats(std::move(urls_full->chats_), "GetRecentMeUrlsQuery");

    auto urls = std::move(urls_full->urls_);
    auto results = make_tl_object<td_api::tMeUrls>();
    results->urls_.reserve(urls.size());
    for (auto &url_ptr : urls) {
      CHECK(url_ptr != nullptr);
      tl_object_ptr<td_api::tMeUrl> result = make_tl_object<td_api::tMeUrl>();
      switch (url_ptr->get_id()) {
        case telegram_api::recentMeUrlUser::ID: {
          auto url = move_tl_object_as<telegram_api::recentMeUrlUser>(url_ptr);
          result->url_ = std::move(url->url_);
          UserId user_id(url->user_id_);
          if (!user_id.is_valid()) {
            LOG(ERROR) << "Receive invalid " << user_id;
            result = nullptr;
            break;
          }
          result->type_ =
              make_tl_object<td_api::tMeUrlTypeUser>(td_->user_manager_->get_user_id_object(user_id, "tMeUrlTypeUser"));
          break;
        }
        case telegram_api::recentMeUrlChat::ID: {
          auto url = move_tl_object_as<telegram_api::recentMeUrlChat>(url_ptr);
          result->url_ = std::move(url->url_);
          ChannelId channel_id(url->chat_id_);
          if (!channel_id.is_valid()) {
            LOG(ERROR) << "Receive invalid " << channel_id;
            result = nullptr;
            break;
          }
          result->type_ = make_tl_object<td_api::tMeUrlTypeSupergroup>(
              td_->chat_manager_->get_supergroup_id_object(channel_id, "tMeUrlTypeSupergroup"));
          break;
        }
        case telegram_api::recentMeUrlChatInvite::ID: {
          auto url = move_tl_object_as<telegram_api::recentMeUrlChatInvite>(url_ptr);
          result->url_ = std::move(url->url_);
          td_->dialog_invite_link_manager_->on_get_dialog_invite_link_info(result->url_, std::move(url->chat_invite_),
                                                                           Promise<Unit>());
          auto info_object = td_->dialog_invite_link_manager_->get_chat_invite_link_info_object(result->url_);
          if (info_object == nullptr) {
            result = nullptr;
            break;
          }
          result->type_ = make_tl_object<td_api::tMeUrlTypeChatInvite>(std::move(info_object));
          break;
        }
        case telegram_api::recentMeUrlStickerSet::ID: {
          auto url = move_tl_object_as<telegram_api::recentMeUrlStickerSet>(url_ptr);
          result->url_ = std::move(url->url_);
          auto sticker_set_id =
              td_->stickers_manager_->on_get_sticker_set_covered(std::move(url->set_), false, "recentMeUrlStickerSet");
          if (!sticker_set_id.is_valid()) {
            LOG(ERROR) << "Receive invalid sticker set";
            result = nullptr;
            break;
          }
          result->type_ = make_tl_object<td_api::tMeUrlTypeStickerSet>(sticker_set_id.get());
          break;
        }
        case telegram_api::recentMeUrlUnknown::ID:
          // skip
          result = nullptr;
          break;
        default:
          UNREACHABLE();
      }
      if (result != nullptr) {
        results->urls_.push_back(std::move(result));
      }
    }
    promise_.set_value(std::move(results));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class SendCustomRequestQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::customRequestResult>> promise_;

 public:
  explicit SendCustomRequestQuery(Promise<td_api::object_ptr<td_api::customRequestResult>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(const string &method, const string &parameters) {
    send_query(G()->net_query_creator().create(
        telegram_api::bots_sendCustomRequest(method, make_tl_object<telegram_api::dataJSON>(parameters))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::bots_sendCustomRequest>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    promise_.set_value(td_api::make_object<td_api::customRequestResult>(result->data_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class AnswerCustomQueryQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit AnswerCustomQueryQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(int64 custom_query_id, const string &data) {
    send_query(G()->net_query_creator().create(
        telegram_api::bots_answerWebhookJSONQuery(custom_query_id, make_tl_object<telegram_api::dataJSON>(data))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::bots_answerWebhookJSONQuery>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    bool result = result_ptr.ok();
    if (!result) {
      LOG(INFO) << "Sending answer to a custom query has failed";
    }
    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class SetBotUpdatesStatusQuery final : public Td::ResultHandler {
 public:
  void send(int32 pending_update_count, const string &error_message) {
    send_query(
        G()->net_query_creator().create(telegram_api::help_setBotUpdatesStatus(pending_update_count, error_message)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::help_setBotUpdatesStatus>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    bool result = result_ptr.ok();
    LOG_IF(WARNING, !result) << "Set bot updates status has failed";
  }

  void on_error(Status status) final {
    if (!G()->is_expected_error(status)) {
      LOG(WARNING) << "Receive error for SetBotUpdatesStatusQuery: " << status;
    }
    status.ignore();
  }
};

class UpdateStatusQuery final : public Td::ResultHandler {
  bool is_offline_;

 public:
  NetQueryRef send(bool is_offline) {
    is_offline_ = is_offline;
    auto net_query = G()->net_query_creator().create(telegram_api::account_updateStatus(is_offline));
    auto result = net_query.get_weak();
    send_query(std::move(net_query));
    return result;
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::account_updateStatus>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    bool result = result_ptr.ok();
    LOG(INFO) << "Receive result for UpdateStatusQuery: " << result;
    td_->on_update_status_success(!is_offline_);
  }

  void on_error(Status status) final {
    if (status.code() != NetQuery::Canceled && !G()->is_expected_error(status)) {
      LOG(ERROR) << "Receive error for UpdateStatusQuery: " << status;
    }
    status.ignore();
  }
};

class TestNetworkQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit TestNetworkQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create_unauth(telegram_api::help_getConfig()));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::help_getConfig>(packet);
    if (result_ptr.is_error()) {
      return on_error(Status::Error(500, "Fetch failed"));
    }

    LOG(DEBUG) << "TestNetwork OK: " << to_string(result_ptr.ok());
    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    LOG(ERROR) << "Test query failed: " << status;
    promise_.set_error(std::move(status));
  }
};

class TestProxyRequest final : public RequestOnceActor {
  Proxy proxy_;
  int16 dc_id_;
  double timeout_;
  ActorOwn<> child_;
  Promise<> promise_;

  auto get_transport() {
    return mtproto::TransportType{mtproto::TransportType::ObfuscatedTcp, dc_id_, proxy_.secret()};
  }

  void do_run(Promise<Unit> &&promise) final {
    set_timeout_in(timeout_);

    promise_ = std::move(promise);
    IPAddress ip_address;
    auto status = ip_address.init_host_port(proxy_.server(), proxy_.port());
    if (status.is_error()) {
      return promise_.set_error(Status::Error(400, status.public_message()));
    }
    auto r_socket_fd = SocketFd::open(ip_address);
    if (r_socket_fd.is_error()) {
      return promise_.set_error(Status::Error(400, r_socket_fd.error().public_message()));
    }

    auto dc_options = ConnectionCreator::get_default_dc_options(false);
    IPAddress mtproto_ip_address;
    for (auto &dc_option : dc_options.dc_options) {
      if (dc_option.get_dc_id().get_raw_id() == dc_id_) {
        mtproto_ip_address = dc_option.get_ip_address();
        break;
      }
    }

    auto connection_promise =
        PromiseCreator::lambda([actor_id = actor_id(this)](Result<ConnectionCreator::ConnectionData> r_data) mutable {
          send_closure(actor_id, &TestProxyRequest::on_connection_data, std::move(r_data));
        });

    child_ = ConnectionCreator::prepare_connection(ip_address, r_socket_fd.move_as_ok(), proxy_, mtproto_ip_address,
                                                   get_transport(), "Test", "TestPingDC2", nullptr, {}, false,
                                                   std::move(connection_promise));
  }

  void on_connection_data(Result<ConnectionCreator::ConnectionData> r_data) {
    if (r_data.is_error()) {
      return promise_.set_error(r_data.move_as_error());
    }
    class HandshakeContext final : public mtproto::AuthKeyHandshakeContext {
     public:
      mtproto::DhCallback *get_dh_callback() final {
        return nullptr;
      }
      mtproto::PublicRsaKeyInterface *get_public_rsa_key_interface() final {
        return public_rsa_key_.get();
      }

     private:
      std::shared_ptr<mtproto::PublicRsaKeyInterface> public_rsa_key_ = PublicRsaKeySharedMain::create(false);
    };
    auto handshake = make_unique<mtproto::AuthKeyHandshake>(dc_id_, 3600);
    auto data = r_data.move_as_ok();
    auto raw_connection =
        mtproto::RawConnection::create(data.ip_address, std::move(data.buffered_socket_fd), get_transport(), nullptr);
    child_ = create_actor<mtproto::HandshakeActor>(
        "HandshakeActor", std::move(handshake), std::move(raw_connection), make_unique<HandshakeContext>(), 10.0,
        PromiseCreator::lambda([actor_id = actor_id(this)](Result<unique_ptr<mtproto::RawConnection>> raw_connection) {
          send_closure(actor_id, &TestProxyRequest::on_handshake_connection, std::move(raw_connection));
        }),
        PromiseCreator::lambda(
            [actor_id = actor_id(this)](Result<unique_ptr<mtproto::AuthKeyHandshake>> handshake) mutable {
              send_closure(actor_id, &TestProxyRequest::on_handshake, std::move(handshake));
            }));
  }
  void on_handshake_connection(Result<unique_ptr<mtproto::RawConnection>> r_raw_connection) {
    if (r_raw_connection.is_error()) {
      return promise_.set_error(Status::Error(400, r_raw_connection.move_as_error().public_message()));
    }
  }
  void on_handshake(Result<unique_ptr<mtproto::AuthKeyHandshake>> r_handshake) {
    if (!promise_) {
      return;
    }
    if (r_handshake.is_error()) {
      return promise_.set_error(Status::Error(400, r_handshake.move_as_error().public_message()));
    }

    auto handshake = r_handshake.move_as_ok();
    if (!handshake->is_ready_for_finish()) {
      promise_.set_error(Status::Error(400, "Handshake is not ready"));
    }
    promise_.set_value(Unit());
  }

  void timeout_expired() final {
    send_error(Status::Error(400, "Timeout expired"));
    stop();
  }

 public:
  TestProxyRequest(ActorShared<Td> td, uint64 request_id, Proxy proxy, int32 dc_id, double timeout)
      : RequestOnceActor(std::move(td), request_id)
      , proxy_(std::move(proxy))
      , dc_id_(static_cast<int16>(dc_id))
      , timeout_(timeout) {
  }
};

class GetMeRequest final : public RequestActor<> {
  UserId user_id_;

  void do_run(Promise<Unit> &&promise) final {
    user_id_ = td_->user_manager_->get_me(std::move(promise));
  }

  void do_send_result() final {
    send_result(td_->user_manager_->get_user_object(user_id_));
  }

 public:
  GetMeRequest(ActorShared<Td> td, uint64 request_id) : RequestActor(std::move(td), request_id) {
  }
};

class GetUserRequest final : public RequestActor<> {
  UserId user_id_;

  void do_run(Promise<Unit> &&promise) final {
    td_->user_manager_->get_user(user_id_, get_tries(), std::move(promise));
  }

  void do_send_result() final {
    send_result(td_->user_manager_->get_user_object(user_id_));
  }

 public:
  GetUserRequest(ActorShared<Td> td, uint64 request_id, int64 user_id)
      : RequestActor(std::move(td), request_id), user_id_(user_id) {
    set_tries(3);
  }
};

class GetUserFullInfoRequest final : public RequestActor<> {
  UserId user_id_;

  void do_run(Promise<Unit> &&promise) final {
    td_->user_manager_->load_user_full(user_id_, get_tries() < 2, std::move(promise), "GetUserFullInfoRequest");
  }

  void do_send_result() final {
    send_result(td_->user_manager_->get_user_full_info_object(user_id_));
  }

 public:
  GetUserFullInfoRequest(ActorShared<Td> td, uint64 request_id, int64 user_id)
      : RequestActor(std::move(td), request_id), user_id_(user_id) {
  }
};

class GetGroupRequest final : public RequestActor<> {
  ChatId chat_id_;

  void do_run(Promise<Unit> &&promise) final {
    td_->chat_manager_->get_chat(chat_id_, get_tries(), std::move(promise));
  }

  void do_send_result() final {
    send_result(td_->chat_manager_->get_basic_group_object(chat_id_));
  }

 public:
  GetGroupRequest(ActorShared<Td> td, uint64 request_id, int64 chat_id)
      : RequestActor(std::move(td), request_id), chat_id_(chat_id) {
    set_tries(3);
  }
};

class GetGroupFullInfoRequest final : public RequestActor<> {
  ChatId chat_id_;

  void do_run(Promise<Unit> &&promise) final {
    td_->chat_manager_->load_chat_full(chat_id_, get_tries() < 2, std::move(promise), "getBasicGroupFullInfo");
  }

  void do_send_result() final {
    send_result(td_->chat_manager_->get_basic_group_full_info_object(chat_id_));
  }

 public:
  GetGroupFullInfoRequest(ActorShared<Td> td, uint64 request_id, int64 chat_id)
      : RequestActor(std::move(td), request_id), chat_id_(chat_id) {
  }
};

class GetSupergroupRequest final : public RequestActor<> {
  ChannelId channel_id_;

  void do_run(Promise<Unit> &&promise) final {
    td_->chat_manager_->get_channel(channel_id_, get_tries(), std::move(promise));
  }

  void do_send_result() final {
    send_result(td_->chat_manager_->get_supergroup_object(channel_id_));
  }

 public:
  GetSupergroupRequest(ActorShared<Td> td, uint64 request_id, int64 channel_id)
      : RequestActor(std::move(td), request_id), channel_id_(channel_id) {
    set_tries(3);
  }
};

class GetSupergroupFullInfoRequest final : public RequestActor<> {
  ChannelId channel_id_;

  void do_run(Promise<Unit> &&promise) final {
    td_->chat_manager_->load_channel_full(channel_id_, get_tries() < 2, std::move(promise),
                                          "GetSupergroupFullInfoRequest");
  }

  void do_send_result() final {
    send_result(td_->chat_manager_->get_supergroup_full_info_object(channel_id_));
  }

 public:
  GetSupergroupFullInfoRequest(ActorShared<Td> td, uint64 request_id, int64 channel_id)
      : RequestActor(std::move(td), request_id), channel_id_(channel_id) {
  }
};

class GetSecretChatRequest final : public RequestActor<> {
  SecretChatId secret_chat_id_;

  void do_run(Promise<Unit> &&promise) final {
    td_->user_manager_->get_secret_chat(secret_chat_id_, get_tries() < 2, std::move(promise));
  }

  void do_send_result() final {
    send_result(td_->user_manager_->get_secret_chat_object(secret_chat_id_));
  }

 public:
  GetSecretChatRequest(ActorShared<Td> td, uint64 request_id, int32 secret_chat_id)
      : RequestActor(std::move(td), request_id), secret_chat_id_(secret_chat_id) {
  }
};

class GetChatRequest final : public RequestActor<> {
  DialogId dialog_id_;

  bool dialog_found_ = false;

  void do_run(Promise<Unit> &&promise) final {
    dialog_found_ = td_->messages_manager_->load_dialog(dialog_id_, get_tries(), std::move(promise));
  }

  void do_send_result() final {
    if (!dialog_found_) {
      send_error(Status::Error(400, "Chat is not accessible"));
    } else {
      send_result(td_->messages_manager_->get_chat_object(dialog_id_, "GetChatRequest"));
    }
  }

 public:
  GetChatRequest(ActorShared<Td> td, uint64 request_id, int64 dialog_id)
      : RequestActor(std::move(td), request_id), dialog_id_(dialog_id) {
    set_tries(3);
  }
};

class SearchUserByPhoneNumberRequest final : public RequestActor<> {
  string phone_number_;
  bool only_local_;

  UserId user_id_;

  void do_run(Promise<Unit> &&promise) final {
    user_id_ = td_->user_manager_->search_user_by_phone_number(phone_number_, only_local_, std::move(promise));
  }

  void do_send_result() final {
    send_result(td_->user_manager_->get_user_object(user_id_));
  }

 public:
  SearchUserByPhoneNumberRequest(ActorShared<Td> td, uint64 request_id, string &&phone_number, bool only_local)
      : RequestActor(std::move(td), request_id), phone_number_(std::move(phone_number)), only_local_(only_local) {
  }
};

class LoadChatsRequest final : public RequestActor<> {
  DialogListId dialog_list_id_;
  DialogDate offset_;
  int32 limit_;

  void do_run(Promise<Unit> &&promise) final {
    td_->messages_manager_->get_dialogs(dialog_list_id_, offset_, limit_, false, get_tries() < 2, std::move(promise));
  }

 public:
  LoadChatsRequest(ActorShared<Td> td, uint64 request_id, DialogListId dialog_list_id, DialogDate offset, int32 limit)
      : RequestActor(std::move(td), request_id), dialog_list_id_(dialog_list_id), offset_(offset), limit_(limit) {
    // 1 for database + 1 for server request + 1 for server request at the end + 1 for return + 1 just in case
    set_tries(5);

    if (limit_ > 100) {
      limit_ = 100;
    }
  }
};

class SearchPublicChatRequest final : public RequestActor<> {
  string username_;

  DialogId dialog_id_;

  void do_run(Promise<Unit> &&promise) final {
    dialog_id_ = td_->dialog_manager_->search_public_dialog(username_, get_tries() < 3, std::move(promise));
  }

  void do_send_result() final {
    send_result(td_->messages_manager_->get_chat_object(dialog_id_, "SearchPublicChatRequest"));
  }

 public:
  SearchPublicChatRequest(ActorShared<Td> td, uint64 request_id, string username)
      : RequestActor(std::move(td), request_id), username_(std::move(username)) {
    set_tries(4);  // 1 for server request + 1 for reload voice chat + 1 for reload dialog + 1 for result
  }
};

class SearchPublicChatsRequest final : public RequestActor<> {
  string query_;

  vector<DialogId> dialog_ids_;

  void do_run(Promise<Unit> &&promise) final {
    dialog_ids_ = td_->messages_manager_->search_public_dialogs(query_, std::move(promise));
  }

  void do_send_result() final {
    send_result(td_->dialog_manager_->get_chats_object(-1, dialog_ids_, "SearchPublicChatsRequest"));
  }

 public:
  SearchPublicChatsRequest(ActorShared<Td> td, uint64 request_id, string query)
      : RequestActor(std::move(td), request_id), query_(std::move(query)) {
  }
};

class SearchChatsRequest final : public RequestActor<> {
  string query_;
  int32 limit_;

  std::pair<int32, vector<DialogId>> dialog_ids_;

  void do_run(Promise<Unit> &&promise) final {
    dialog_ids_ = td_->messages_manager_->search_dialogs(query_, limit_, std::move(promise));
  }

  void do_send_result() final {
    send_result(td_->dialog_manager_->get_chats_object(dialog_ids_, "SearchChatsRequest"));
  }

 public:
  SearchChatsRequest(ActorShared<Td> td, uint64 request_id, string query, int32 limit)
      : RequestActor(std::move(td), request_id), query_(std::move(query)), limit_(limit) {
  }
};

class SearchChatsOnServerRequest final : public RequestActor<> {
  string query_;
  int32 limit_;

  vector<DialogId> dialog_ids_;

  void do_run(Promise<Unit> &&promise) final {
    dialog_ids_ = td_->messages_manager_->search_dialogs_on_server(query_, limit_, std::move(promise));
  }

  void do_send_result() final {
    send_result(td_->dialog_manager_->get_chats_object(-1, dialog_ids_, "SearchChatsOnServerRequest"));
  }

 public:
  SearchChatsOnServerRequest(ActorShared<Td> td, uint64 request_id, string query, int32 limit)
      : RequestActor(std::move(td), request_id), query_(std::move(query)), limit_(limit) {
  }
};

class GetGroupsInCommonRequest final : public RequestActor<> {
  UserId user_id_;
  DialogId offset_dialog_id_;
  int32 limit_;

  std::pair<int32, vector<DialogId>> dialog_ids_;

  void do_run(Promise<Unit> &&promise) final {
    dialog_ids_ = td_->common_dialog_manager_->get_common_dialogs(user_id_, offset_dialog_id_, limit_, get_tries() < 2,
                                                                  std::move(promise));
  }

  void do_send_result() final {
    send_result(td_->dialog_manager_->get_chats_object(dialog_ids_, "GetGroupsInCommonRequest"));
  }

 public:
  GetGroupsInCommonRequest(ActorShared<Td> td, uint64 request_id, int64 user_id, int64 offset_dialog_id, int32 limit)
      : RequestActor(std::move(td), request_id), user_id_(user_id), offset_dialog_id_(offset_dialog_id), limit_(limit) {
  }
};

class GetSuitableDiscussionChatsRequest final : public RequestActor<> {
  vector<DialogId> dialog_ids_;

  void do_run(Promise<Unit> &&promise) final {
    dialog_ids_ = td_->chat_manager_->get_dialogs_for_discussion(std::move(promise));
  }

  void do_send_result() final {
    send_result(td_->dialog_manager_->get_chats_object(-1, dialog_ids_, "GetSuitableDiscussionChatsRequest"));
  }

 public:
  GetSuitableDiscussionChatsRequest(ActorShared<Td> td, uint64 request_id) : RequestActor(std::move(td), request_id) {
  }
};

class GetInactiveSupergroupChatsRequest final : public RequestActor<> {
  vector<DialogId> dialog_ids_;

  void do_run(Promise<Unit> &&promise) final {
    dialog_ids_ = td_->chat_manager_->get_inactive_channels(std::move(promise));
  }

  void do_send_result() final {
    send_result(td_->dialog_manager_->get_chats_object(-1, dialog_ids_, "GetInactiveSupergroupChatsRequest"));
  }

 public:
  GetInactiveSupergroupChatsRequest(ActorShared<Td> td, uint64 request_id) : RequestActor(std::move(td), request_id) {
  }
};

class SearchRecentlyFoundChatsRequest final : public RequestActor<> {
  string query_;
  int32 limit_;

  std::pair<int32, vector<DialogId>> dialog_ids_;

  void do_run(Promise<Unit> &&promise) final {
    dialog_ids_ = td_->messages_manager_->search_recently_found_dialogs(query_, limit_, std::move(promise));
  }

  void do_send_result() final {
    send_result(td_->dialog_manager_->get_chats_object(dialog_ids_, "SearchRecentlyFoundChatsRequest"));
  }

 public:
  SearchRecentlyFoundChatsRequest(ActorShared<Td> td, uint64 request_id, string query, int32 limit)
      : RequestActor(std::move(td), request_id), query_(std::move(query)), limit_(limit) {
  }
};

class GetRecentlyOpenedChatsRequest final : public RequestActor<> {
  int32 limit_;

  std::pair<int32, vector<DialogId>> dialog_ids_;

  void do_run(Promise<Unit> &&promise) final {
    dialog_ids_ = td_->messages_manager_->get_recently_opened_dialogs(limit_, std::move(promise));
  }

  void do_send_result() final {
    send_result(td_->dialog_manager_->get_chats_object(dialog_ids_, "GetRecentlyOpenedChatsRequest"));
  }

 public:
  GetRecentlyOpenedChatsRequest(ActorShared<Td> td, uint64 request_id, int32 limit)
      : RequestActor(std::move(td), request_id), limit_(limit) {
  }
};

class GetMessageRequest final : public RequestOnceActor {
  MessageFullId message_full_id_;

  void do_run(Promise<Unit> &&promise) final {
    td_->messages_manager_->get_message(message_full_id_, std::move(promise));
  }

  void do_send_result() final {
    send_result(td_->messages_manager_->get_message_object(message_full_id_, "GetMessageRequest"));
  }

 public:
  GetMessageRequest(ActorShared<Td> td, uint64 request_id, int64 dialog_id, int64 message_id)
      : RequestOnceActor(std::move(td), request_id), message_full_id_(DialogId(dialog_id), MessageId(message_id)) {
  }
};

class GetRepliedMessageRequest final : public RequestOnceActor {
  DialogId dialog_id_;
  MessageId message_id_;

  MessageFullId replied_message_id_;

  void do_run(Promise<Unit> &&promise) final {
    replied_message_id_ =
        td_->messages_manager_->get_replied_message(dialog_id_, message_id_, get_tries() < 3, std::move(promise));
  }

  void do_send_result() final {
    send_result(td_->messages_manager_->get_message_object(replied_message_id_, "GetRepliedMessageRequest"));
  }

 public:
  GetRepliedMessageRequest(ActorShared<Td> td, uint64 request_id, int64 dialog_id, int64 message_id)
      : RequestOnceActor(std::move(td), request_id), dialog_id_(dialog_id), message_id_(message_id) {
    set_tries(3);  // 1 to get initial message, 1 to get the reply and 1 for result
  }
};

class GetMessageThreadRequest final : public RequestActor<MessageThreadInfo> {
  DialogId dialog_id_;
  MessageId message_id_;

  MessageThreadInfo message_thread_info_;

  void do_run(Promise<MessageThreadInfo> &&promise) final {
    if (get_tries() < 2) {
      promise.set_value(std::move(message_thread_info_));
      return;
    }
    td_->messages_manager_->get_message_thread(dialog_id_, message_id_, std::move(promise));
  }

  void do_set_result(MessageThreadInfo &&result) final {
    message_thread_info_ = std::move(result);
  }

  void do_send_result() final {
    send_result(td_->messages_manager_->get_message_thread_info_object(message_thread_info_));
  }

 public:
  GetMessageThreadRequest(ActorShared<Td> td, uint64 request_id, int64 dialog_id, int64 message_id)
      : RequestActor(std::move(td), request_id), dialog_id_(dialog_id), message_id_(message_id) {
  }
};

class GetChatPinnedMessageRequest final : public RequestOnceActor {
  DialogId dialog_id_;

  MessageId pinned_message_id_;

  void do_run(Promise<Unit> &&promise) final {
    pinned_message_id_ = td_->messages_manager_->get_dialog_pinned_message(dialog_id_, std::move(promise));
  }

  void do_send_result() final {
    send_result(
        td_->messages_manager_->get_message_object({dialog_id_, pinned_message_id_}, "GetChatPinnedMessageRequest"));
  }

 public:
  GetChatPinnedMessageRequest(ActorShared<Td> td, uint64 request_id, int64 dialog_id)
      : RequestOnceActor(std::move(td), request_id), dialog_id_(dialog_id) {
    set_tries(3);  // 1 to get pinned_message_id, 1 to get the message and 1 for result
  }
};

class GetCallbackQueryMessageRequest final : public RequestOnceActor {
  DialogId dialog_id_;
  MessageId message_id_;
  int64 callback_query_id_;

  void do_run(Promise<Unit> &&promise) final {
    td_->messages_manager_->get_callback_query_message(dialog_id_, message_id_, callback_query_id_, std::move(promise));
  }

  void do_send_result() final {
    send_result(
        td_->messages_manager_->get_message_object({dialog_id_, message_id_}, "GetCallbackQueryMessageRequest"));
  }

 public:
  GetCallbackQueryMessageRequest(ActorShared<Td> td, uint64 request_id, int64 dialog_id, int64 message_id,
                                 int64 callback_query_id)
      : RequestOnceActor(std::move(td), request_id)
      , dialog_id_(dialog_id)
      , message_id_(message_id)
      , callback_query_id_(callback_query_id) {
  }
};

class GetMessagesRequest final : public RequestOnceActor {
  DialogId dialog_id_;
  vector<MessageId> message_ids_;

  void do_run(Promise<Unit> &&promise) final {
    td_->messages_manager_->get_messages(dialog_id_, message_ids_, std::move(promise));
  }

  void do_send_result() final {
    send_result(td_->messages_manager_->get_messages_object(-1, dialog_id_, message_ids_, false, "GetMessagesRequest"));
  }

 public:
  GetMessagesRequest(ActorShared<Td> td, uint64 request_id, int64 dialog_id, const vector<int64> &message_ids)
      : RequestOnceActor(std::move(td), request_id)
      , dialog_id_(dialog_id)
      , message_ids_(MessageId::get_message_ids(message_ids)) {
  }
};

class GetMessageEmbeddingCodeRequest final : public RequestActor<> {
  MessageFullId message_full_id_;
  bool for_group_;

  string html_;

  void do_run(Promise<Unit> &&promise) final {
    html_ = td_->messages_manager_->get_message_embedding_code(message_full_id_, for_group_, std::move(promise));
  }

  void do_send_result() final {
    send_result(make_tl_object<td_api::text>(html_));
  }

 public:
  GetMessageEmbeddingCodeRequest(ActorShared<Td> td, uint64 request_id, int64 dialog_id, int64 message_id,
                                 bool for_group)
      : RequestActor(std::move(td), request_id)
      , message_full_id_(DialogId(dialog_id), MessageId(message_id))
      , for_group_(for_group) {
  }
};

class GetMessageLinkInfoRequest final : public RequestActor<MessageLinkInfo> {
  string url_;

  MessageLinkInfo message_link_info_;

  void do_run(Promise<MessageLinkInfo> &&promise) final {
    if (get_tries() < 2) {
      promise.set_value(std::move(message_link_info_));
      return;
    }
    td_->messages_manager_->get_message_link_info(url_, std::move(promise));
  }

  void do_set_result(MessageLinkInfo &&result) final {
    message_link_info_ = std::move(result);
  }

  void do_send_result() final {
    send_result(td_->messages_manager_->get_message_link_info_object(message_link_info_));
  }

 public:
  GetMessageLinkInfoRequest(ActorShared<Td> td, uint64 request_id, string url)
      : RequestActor(std::move(td), request_id), url_(std::move(url)) {
  }
};

class GetDialogBoostLinkInfoRequest final : public RequestActor<DialogBoostLinkInfo> {
  string url_;

  DialogBoostLinkInfo dialog_boost_link_info_;

  void do_run(Promise<DialogBoostLinkInfo> &&promise) final {
    if (get_tries() < 2) {
      promise.set_value(std::move(dialog_boost_link_info_));
      return;
    }
    td_->boost_manager_->get_dialog_boost_link_info(url_, std::move(promise));
  }

  void do_set_result(DialogBoostLinkInfo &&result) final {
    dialog_boost_link_info_ = std::move(result);
  }

  void do_send_result() final {
    send_result(td_->boost_manager_->get_chat_boost_link_info_object(dialog_boost_link_info_));
  }

 public:
  GetDialogBoostLinkInfoRequest(ActorShared<Td> td, uint64 request_id, string url)
      : RequestActor(std::move(td), request_id), url_(std::move(url)) {
  }
};

class EditMessageTextRequest final : public RequestOnceActor {
  MessageFullId message_full_id_;
  tl_object_ptr<td_api::ReplyMarkup> reply_markup_;
  tl_object_ptr<td_api::InputMessageContent> input_message_content_;

  void do_run(Promise<Unit> &&promise) final {
    td_->messages_manager_->edit_message_text(message_full_id_, std::move(reply_markup_),
                                              std::move(input_message_content_), std::move(promise));
  }

  void do_send_result() final {
    send_result(td_->messages_manager_->get_message_object(message_full_id_, "EditMessageTextRequest"));
  }

 public:
  EditMessageTextRequest(ActorShared<Td> td, uint64 request_id, int64 dialog_id, int64 message_id,
                         tl_object_ptr<td_api::ReplyMarkup> reply_markup,
                         tl_object_ptr<td_api::InputMessageContent> input_message_content)
      : RequestOnceActor(std::move(td), request_id)
      , message_full_id_(DialogId(dialog_id), MessageId(message_id))
      , reply_markup_(std::move(reply_markup))
      , input_message_content_(std::move(input_message_content)) {
  }
};

class EditMessageLiveLocationRequest final : public RequestOnceActor {
  MessageFullId message_full_id_;
  tl_object_ptr<td_api::ReplyMarkup> reply_markup_;
  tl_object_ptr<td_api::location> location_;
  int32 live_period_;
  int32 heading_;
  int32 proximity_alert_radius_;

  void do_run(Promise<Unit> &&promise) final {
    td_->messages_manager_->edit_message_live_location(message_full_id_, std::move(reply_markup_), std::move(location_),
                                                       live_period_, heading_, proximity_alert_radius_,
                                                       std::move(promise));
  }

  void do_send_result() final {
    send_result(td_->messages_manager_->get_message_object(message_full_id_, "EditMessageLiveLocationRequest"));
  }

 public:
  EditMessageLiveLocationRequest(ActorShared<Td> td, uint64 request_id, int64 dialog_id, int64 message_id,
                                 tl_object_ptr<td_api::ReplyMarkup> reply_markup,
                                 tl_object_ptr<td_api::location> location, int32 live_period, int32 heading,
                                 int32 proximity_alert_radius)
      : RequestOnceActor(std::move(td), request_id)
      , message_full_id_(DialogId(dialog_id), MessageId(message_id))
      , reply_markup_(std::move(reply_markup))
      , location_(std::move(location))
      , live_period_(live_period)
      , heading_(heading)
      , proximity_alert_radius_(proximity_alert_radius) {
  }
};

class EditMessageMediaRequest final : public RequestOnceActor {
  MessageFullId message_full_id_;
  tl_object_ptr<td_api::ReplyMarkup> reply_markup_;
  tl_object_ptr<td_api::InputMessageContent> input_message_content_;

  void do_run(Promise<Unit> &&promise) final {
    td_->messages_manager_->edit_message_media(message_full_id_, std::move(reply_markup_),
                                               std::move(input_message_content_), std::move(promise));
  }

  void do_send_result() final {
    send_result(td_->messages_manager_->get_message_object(message_full_id_, "EditMessageMediaRequest"));
  }

 public:
  EditMessageMediaRequest(ActorShared<Td> td, uint64 request_id, int64 dialog_id, int64 message_id,
                          tl_object_ptr<td_api::ReplyMarkup> reply_markup,
                          tl_object_ptr<td_api::InputMessageContent> input_message_content)
      : RequestOnceActor(std::move(td), request_id)
      , message_full_id_(DialogId(dialog_id), MessageId(message_id))
      , reply_markup_(std::move(reply_markup))
      , input_message_content_(std::move(input_message_content)) {
  }
};

class EditMessageCaptionRequest final : public RequestOnceActor {
  MessageFullId message_full_id_;
  tl_object_ptr<td_api::ReplyMarkup> reply_markup_;
  tl_object_ptr<td_api::formattedText> caption_;
  bool invert_media_;

  void do_run(Promise<Unit> &&promise) final {
    td_->messages_manager_->edit_message_caption(message_full_id_, std::move(reply_markup_), std::move(caption_),
                                                 invert_media_, std::move(promise));
  }

  void do_send_result() final {
    send_result(td_->messages_manager_->get_message_object(message_full_id_, "EditMessageCaptionRequest"));
  }

 public:
  EditMessageCaptionRequest(ActorShared<Td> td, uint64 request_id, int64 dialog_id, int64 message_id,
                            tl_object_ptr<td_api::ReplyMarkup> reply_markup,
                            tl_object_ptr<td_api::formattedText> caption, bool invert_media)
      : RequestOnceActor(std::move(td), request_id)
      , message_full_id_(DialogId(dialog_id), MessageId(message_id))
      , reply_markup_(std::move(reply_markup))
      , caption_(std::move(caption))
      , invert_media_(invert_media) {
  }
};

class EditMessageReplyMarkupRequest final : public RequestOnceActor {
  MessageFullId message_full_id_;
  tl_object_ptr<td_api::ReplyMarkup> reply_markup_;

  void do_run(Promise<Unit> &&promise) final {
    td_->messages_manager_->edit_message_reply_markup(message_full_id_, std::move(reply_markup_), std::move(promise));
  }

  void do_send_result() final {
    send_result(td_->messages_manager_->get_message_object(message_full_id_, "EditMessageReplyMarkupRequest"));
  }

 public:
  EditMessageReplyMarkupRequest(ActorShared<Td> td, uint64 request_id, int64 dialog_id, int64 message_id,
                                tl_object_ptr<td_api::ReplyMarkup> reply_markup)
      : RequestOnceActor(std::move(td), request_id)
      , message_full_id_(DialogId(dialog_id), MessageId(message_id))
      , reply_markup_(std::move(reply_markup)) {
  }
};

class GetChatHistoryRequest final : public RequestActor<> {
  DialogId dialog_id_;
  MessageId from_message_id_;
  int32 offset_;
  int32 limit_;
  bool only_local_;

  tl_object_ptr<td_api::messages> messages_;

  void do_run(Promise<Unit> &&promise) final {
    messages_ = td_->messages_manager_->get_dialog_history(dialog_id_, from_message_id_, offset_, limit_,
                                                           get_tries() - 1, only_local_, std::move(promise));
  }

  void do_send_result() final {
    send_result(std::move(messages_));
  }

 public:
  GetChatHistoryRequest(ActorShared<Td> td, uint64 request_id, int64 dialog_id, int64 from_message_id, int32 offset,
                        int32 limit, bool only_local)
      : RequestActor(std::move(td), request_id)
      , dialog_id_(dialog_id)
      , from_message_id_(from_message_id)
      , offset_(offset)
      , limit_(limit)
      , only_local_(only_local) {
    if (!only_local_) {
      set_tries(4);
    }
  }
};

class GetMessageThreadHistoryRequest final : public RequestActor<> {
  DialogId dialog_id_;
  MessageId message_id_;
  MessageId from_message_id_;
  int32 offset_;
  int32 limit_;
  int64 random_id_;

  std::pair<DialogId, vector<MessageId>> messages_;

  void do_run(Promise<Unit> &&promise) final {
    messages_ = td_->messages_manager_->get_message_thread_history(dialog_id_, message_id_, from_message_id_, offset_,
                                                                   limit_, random_id_, std::move(promise));
  }

  void do_send_result() final {
    send_result(td_->messages_manager_->get_messages_object(-1, messages_.first, messages_.second, true,
                                                            "GetMessageThreadHistoryRequest"));
  }

 public:
  GetMessageThreadHistoryRequest(ActorShared<Td> td, uint64 request_id, int64 dialog_id, int64 message_id,
                                 int64 from_message_id, int32 offset, int32 limit)
      : RequestActor(std::move(td), request_id)
      , dialog_id_(dialog_id)
      , message_id_(message_id)
      , from_message_id_(from_message_id)
      , offset_(offset)
      , limit_(limit)
      , random_id_(0) {
    set_tries(3);
  }
};

class SearchChatMessagesRequest final : public RequestActor<> {
  DialogId dialog_id_;
  string query_;
  td_api::object_ptr<td_api::MessageSender> sender_id_;
  MessageId from_message_id_;
  int32 offset_;
  int32 limit_;
  MessageSearchFilter filter_;
  MessageId top_thread_message_id_;
  SavedMessagesTopicId saved_messages_topic_id_;
  ReactionType tag_;
  int64 random_id_;

  MessagesManager::FoundDialogMessages messages_;

  void do_run(Promise<Unit> &&promise) final {
    messages_ = td_->messages_manager_->search_dialog_messages(
        dialog_id_, query_, sender_id_, from_message_id_, offset_, limit_, filter_, top_thread_message_id_,
        saved_messages_topic_id_, tag_, random_id_, get_tries() == 3, std::move(promise));
  }

  void do_send_result() final {
    send_result(
        td_->messages_manager_->get_found_chat_messages_object(dialog_id_, messages_, "SearchChatMessagesRequest"));
  }

  void do_send_error(Status &&status) final {
    if (status.message() == "SEARCH_QUERY_EMPTY") {
      messages_ = {};
      return do_send_result();
    }
    send_error(std::move(status));
  }

 public:
  SearchChatMessagesRequest(ActorShared<Td> td, uint64 request_id, int64 dialog_id, string query,
                            td_api::object_ptr<td_api::MessageSender> sender_id, int64 from_message_id, int32 offset,
                            int32 limit, tl_object_ptr<td_api::SearchMessagesFilter> filter, int64 message_thread_id,
                            SavedMessagesTopicId saved_messages_topic_id, ReactionType tag)
      : RequestActor(std::move(td), request_id)
      , dialog_id_(dialog_id)
      , query_(std::move(query))
      , sender_id_(std::move(sender_id))
      , from_message_id_(from_message_id)
      , offset_(offset)
      , limit_(limit)
      , filter_(get_message_search_filter(filter))
      , top_thread_message_id_(message_thread_id)
      , saved_messages_topic_id_(saved_messages_topic_id)
      , tag_(std::move(tag))
      , random_id_(0) {
    set_tries(3);
  }
};

class GetActiveLiveLocationMessagesRequest final : public RequestActor<> {
  vector<MessageFullId> message_full_ids_;

  void do_run(Promise<Unit> &&promise) final {
    message_full_ids_ = td_->messages_manager_->get_active_live_location_messages(std::move(promise));
  }

  void do_send_result() final {
    send_result(td_->messages_manager_->get_messages_object(-1, message_full_ids_, true,
                                                            "GetActiveLiveLocationMessagesRequest"));
  }

 public:
  GetActiveLiveLocationMessagesRequest(ActorShared<Td> td, uint64 request_id)
      : RequestActor(std::move(td), request_id) {
  }
};

class GetChatScheduledMessagesRequest final : public RequestActor<> {
  DialogId dialog_id_;

  vector<MessageId> message_ids_;

  void do_run(Promise<Unit> &&promise) final {
    message_ids_ =
        td_->messages_manager_->get_dialog_scheduled_messages(dialog_id_, get_tries() < 2, false, std::move(promise));
  }

  void do_send_result() final {
    send_result(td_->messages_manager_->get_messages_object(-1, dialog_id_, message_ids_, true,
                                                            "GetChatScheduledMessagesRequest"));
  }

 public:
  GetChatScheduledMessagesRequest(ActorShared<Td> td, uint64 request_id, int64 dialog_id)
      : RequestActor(std::move(td), request_id), dialog_id_(dialog_id) {
    set_tries(4);
  }
};

class GetWebPageInstantViewRequest final : public RequestActor<WebPageId> {
  string url_;
  bool force_full_;

  WebPageId web_page_id_;

  void do_run(Promise<WebPageId> &&promise) final {
    if (get_tries() < 2) {
      promise.set_value(std::move(web_page_id_));
      return;
    }
    td_->web_pages_manager_->get_web_page_instant_view(url_, force_full_, std::move(promise));
  }

  void do_set_result(WebPageId &&result) final {
    web_page_id_ = result;
  }

  void do_send_result() final {
    send_result(td_->web_pages_manager_->get_web_page_instant_view_object(web_page_id_));
  }

 public:
  GetWebPageInstantViewRequest(ActorShared<Td> td, uint64 request_id, string url, bool force_full)
      : RequestActor(std::move(td), request_id), url_(std::move(url)), force_full_(force_full) {
  }
};

class CreateChatRequest final : public RequestActor<> {
  DialogId dialog_id_;
  bool force_;

  void do_run(Promise<Unit> &&promise) final {
    td_->messages_manager_->create_dialog(dialog_id_, force_, std::move(promise));
  }

  void do_send_result() final {
    send_result(td_->messages_manager_->get_chat_object(dialog_id_, "CreateChatRequest"));
  }

 public:
  CreateChatRequest(ActorShared<Td> td, uint64 request_id, DialogId dialog_id, bool force)
      : RequestActor<>(std::move(td), request_id), dialog_id_(dialog_id), force_(force) {
  }
};

class CheckChatInviteLinkRequest final : public RequestActor<> {
  string invite_link_;

  void do_run(Promise<Unit> &&promise) final {
    td_->dialog_invite_link_manager_->check_dialog_invite_link(invite_link_, get_tries() < 2, std::move(promise));
  }

  void do_send_result() final {
    auto result = td_->dialog_invite_link_manager_->get_chat_invite_link_info_object(invite_link_);
    CHECK(result != nullptr);
    send_result(std::move(result));
  }

 public:
  CheckChatInviteLinkRequest(ActorShared<Td> td, uint64 request_id, string invite_link)
      : RequestActor(std::move(td), request_id), invite_link_(std::move(invite_link)) {
  }
};

class JoinChatByInviteLinkRequest final : public RequestActor<DialogId> {
  string invite_link_;

  DialogId dialog_id_;

  void do_run(Promise<DialogId> &&promise) final {
    if (get_tries() < 2) {
      promise.set_value(std::move(dialog_id_));
      return;
    }
    td_->dialog_invite_link_manager_->import_dialog_invite_link(invite_link_, std::move(promise));
  }

  void do_set_result(DialogId &&result) final {
    dialog_id_ = result;
  }

  void do_send_result() final {
    CHECK(dialog_id_.is_valid());
    td_->dialog_manager_->force_create_dialog(dialog_id_, "join chat via an invite link");
    send_result(td_->messages_manager_->get_chat_object(dialog_id_, "JoinChatByInviteLinkRequest"));
  }

 public:
  JoinChatByInviteLinkRequest(ActorShared<Td> td, uint64 request_id, string invite_link)
      : RequestActor(std::move(td), request_id), invite_link_(std::move(invite_link)) {
  }
};

class ImportContactsRequest final : public RequestActor<> {
  vector<Contact> contacts_;
  int64 random_id_;

  std::pair<vector<UserId>, vector<int32>> imported_contacts_;

  void do_run(Promise<Unit> &&promise) final {
    imported_contacts_ = td_->user_manager_->import_contacts(contacts_, random_id_, std::move(promise));
  }

  void do_send_result() final {
    CHECK(imported_contacts_.first.size() == contacts_.size());
    CHECK(imported_contacts_.second.size() == contacts_.size());
    send_result(make_tl_object<td_api::importedContacts>(transform(imported_contacts_.first,
                                                                   [this](UserId user_id) {
                                                                     return td_->user_manager_->get_user_id_object(
                                                                         user_id, "ImportContactsRequest");
                                                                   }),
                                                         std::move(imported_contacts_.second)));
  }

 public:
  ImportContactsRequest(ActorShared<Td> td, uint64 request_id, vector<Contact> &&contacts)
      : RequestActor(std::move(td), request_id), contacts_(std::move(contacts)), random_id_(0) {
    set_tries(3);  // load_contacts + import_contacts
  }
};

class SearchContactsRequest final : public RequestActor<> {
  string query_;
  int32 limit_;

  std::pair<int32, vector<UserId>> user_ids_;

  void do_run(Promise<Unit> &&promise) final {
    user_ids_ = td_->user_manager_->search_contacts(query_, limit_, std::move(promise));
  }

  void do_send_result() final {
    send_result(td_->user_manager_->get_users_object(user_ids_.first, user_ids_.second));
  }

 public:
  SearchContactsRequest(ActorShared<Td> td, uint64 request_id, string query, int32 limit)
      : RequestActor(std::move(td), request_id), query_(std::move(query)), limit_(limit) {
  }
};

class RemoveContactsRequest final : public RequestActor<> {
  vector<UserId> user_ids_;

  void do_run(Promise<Unit> &&promise) final {
    td_->user_manager_->remove_contacts(user_ids_, std::move(promise));
  }

 public:
  RemoveContactsRequest(ActorShared<Td> td, uint64 request_id, vector<UserId> &&user_ids)
      : RequestActor(std::move(td), request_id), user_ids_(std::move(user_ids)) {
    set_tries(3);  // load_contacts + delete_contacts
  }
};

class GetImportedContactCountRequest final : public RequestActor<> {
  int32 imported_contact_count_ = 0;

  void do_run(Promise<Unit> &&promise) final {
    imported_contact_count_ = td_->user_manager_->get_imported_contact_count(std::move(promise));
  }

  void do_send_result() final {
    send_result(td_api::make_object<td_api::count>(imported_contact_count_));
  }

 public:
  GetImportedContactCountRequest(ActorShared<Td> td, uint64 request_id) : RequestActor(std::move(td), request_id) {
  }
};

class ChangeImportedContactsRequest final : public RequestActor<> {
  vector<Contact> contacts_;
  size_t contacts_size_;
  int64 random_id_;

  std::pair<vector<UserId>, vector<int32>> imported_contacts_;

  void do_run(Promise<Unit> &&promise) final {
    imported_contacts_ = td_->user_manager_->change_imported_contacts(contacts_, random_id_, std::move(promise));
  }

  void do_send_result() final {
    CHECK(imported_contacts_.first.size() == contacts_size_);
    CHECK(imported_contacts_.second.size() == contacts_size_);
    send_result(make_tl_object<td_api::importedContacts>(transform(imported_contacts_.first,
                                                                   [this](UserId user_id) {
                                                                     return td_->user_manager_->get_user_id_object(
                                                                         user_id, "ChangeImportedContactsRequest");
                                                                   }),
                                                         std::move(imported_contacts_.second)));
  }

 public:
  ChangeImportedContactsRequest(ActorShared<Td> td, uint64 request_id, vector<Contact> &&contacts)
      : RequestActor(std::move(td), request_id)
      , contacts_(std::move(contacts))
      , contacts_size_(contacts_.size())
      , random_id_(0) {
    set_tries(4);  // load_contacts + load_local_contacts + (import_contacts + delete_contacts)
  }
};

class GetCloseFriendsRequest final : public RequestActor<> {
  vector<UserId> user_ids_;

  void do_run(Promise<Unit> &&promise) final {
    user_ids_ = td_->user_manager_->get_close_friends(std::move(promise));
  }

  void do_send_result() final {
    send_result(td_->user_manager_->get_users_object(-1, user_ids_));
  }

 public:
  GetCloseFriendsRequest(ActorShared<Td> td, uint64 request_id) : RequestActor(std::move(td), request_id) {
  }
};

class GetRecentInlineBotsRequest final : public RequestActor<> {
  vector<UserId> user_ids_;

  void do_run(Promise<Unit> &&promise) final {
    user_ids_ = td_->inline_queries_manager_->get_recent_inline_bots(std::move(promise));
  }

  void do_send_result() final {
    send_result(td_->user_manager_->get_users_object(-1, user_ids_));
  }

 public:
  GetRecentInlineBotsRequest(ActorShared<Td> td, uint64 request_id) : RequestActor(std::move(td), request_id) {
  }
};

class GetChatNotificationSettingsExceptionsRequest final : public RequestActor<> {
  NotificationSettingsScope scope_;
  bool filter_scope_;
  bool compare_sound_;

  vector<DialogId> dialog_ids_;

  void do_run(Promise<Unit> &&promise) final {
    dialog_ids_ = td_->messages_manager_->get_dialog_notification_settings_exceptions(
        scope_, filter_scope_, compare_sound_, get_tries() < 3, std::move(promise));
  }

  void do_send_result() final {
    send_result(
        td_->dialog_manager_->get_chats_object(-1, dialog_ids_, "GetChatNotificationSettingsExceptionsRequest"));
  }

 public:
  GetChatNotificationSettingsExceptionsRequest(ActorShared<Td> td, uint64 request_id, NotificationSettingsScope scope,
                                               bool filter_scope, bool compare_sound)
      : RequestActor(std::move(td), request_id)
      , scope_(scope)
      , filter_scope_(filter_scope)
      , compare_sound_(compare_sound) {
    set_tries(3);
  }
};

class GetScopeNotificationSettingsRequest final : public RequestActor<> {
  NotificationSettingsScope scope_;

  const ScopeNotificationSettings *notification_settings_ = nullptr;

  void do_run(Promise<Unit> &&promise) final {
    notification_settings_ =
        td_->notification_settings_manager_->get_scope_notification_settings(scope_, std::move(promise));
  }

  void do_send_result() final {
    CHECK(notification_settings_ != nullptr);
    send_result(get_scope_notification_settings_object(notification_settings_));
  }

 public:
  GetScopeNotificationSettingsRequest(ActorShared<Td> td, uint64 request_id, NotificationSettingsScope scope)
      : RequestActor(std::move(td), request_id), scope_(scope) {
  }
};

class GetStickersRequest final : public RequestActor<> {
  StickerType sticker_type_;
  string query_;
  int32 limit_;
  DialogId dialog_id_;

  vector<FileId> sticker_ids_;

  void do_run(Promise<Unit> &&promise) final {
    sticker_ids_ = td_->stickers_manager_->get_stickers(sticker_type_, query_, limit_, dialog_id_, get_tries() < 2,
                                                        std::move(promise));
  }

  void do_send_result() final {
    send_result(td_->stickers_manager_->get_stickers_object(sticker_ids_));
  }

 public:
  GetStickersRequest(ActorShared<Td> td, uint64 request_id, StickerType sticker_type, string &&query, int32 limit,
                     int64 dialog_id)
      : RequestActor(std::move(td), request_id)
      , sticker_type_(sticker_type)
      , query_(std::move(query))
      , limit_(limit)
      , dialog_id_(dialog_id) {
    set_tries(4);
  }
};

class GetAllStickerEmojisRequest final : public RequestActor<> {
  StickerType sticker_type_;
  string query_;
  DialogId dialog_id_;
  bool return_only_main_emoji_;

  vector<FileId> sticker_ids_;

  void do_run(Promise<Unit> &&promise) final {
    sticker_ids_ = td_->stickers_manager_->get_stickers(sticker_type_, query_, 1000000, dialog_id_, get_tries() < 2,
                                                        std::move(promise));
  }

  void do_send_result() final {
    send_result(td_->stickers_manager_->get_sticker_emojis_object(sticker_ids_, return_only_main_emoji_));
  }

 public:
  GetAllStickerEmojisRequest(ActorShared<Td> td, uint64 request_id, StickerType sticker_type, string &&query,
                             int64 dialog_id, bool return_only_main_emoji)
      : RequestActor(std::move(td), request_id)
      , sticker_type_(sticker_type)
      , query_(std::move(query))
      , dialog_id_(dialog_id)
      , return_only_main_emoji_(return_only_main_emoji) {
    set_tries(4);
  }
};

class GetInstalledStickerSetsRequest final : public RequestActor<> {
  StickerType sticker_type_;

  vector<StickerSetId> sticker_set_ids_;

  void do_run(Promise<Unit> &&promise) final {
    sticker_set_ids_ = td_->stickers_manager_->get_installed_sticker_sets(sticker_type_, std::move(promise));
  }

  void do_send_result() final {
    send_result(td_->stickers_manager_->get_sticker_sets_object(-1, sticker_set_ids_, 1));
  }

 public:
  GetInstalledStickerSetsRequest(ActorShared<Td> td, uint64 request_id, StickerType sticker_type)
      : RequestActor(std::move(td), request_id), sticker_type_(sticker_type) {
  }
};

class GetArchivedStickerSetsRequest final : public RequestActor<> {
  StickerType sticker_type_;
  StickerSetId offset_sticker_set_id_;
  int32 limit_;

  int32 total_count_ = -1;
  vector<StickerSetId> sticker_set_ids_;

  void do_run(Promise<Unit> &&promise) final {
    std::tie(total_count_, sticker_set_ids_) = td_->stickers_manager_->get_archived_sticker_sets(
        sticker_type_, offset_sticker_set_id_, limit_, get_tries() < 2, std::move(promise));
  }

  void do_send_result() final {
    send_result(td_->stickers_manager_->get_sticker_sets_object(total_count_, sticker_set_ids_, 1));
  }

 public:
  GetArchivedStickerSetsRequest(ActorShared<Td> td, uint64 request_id, StickerType sticker_type,
                                int64 offset_sticker_set_id, int32 limit)
      : RequestActor(std::move(td), request_id)
      , sticker_type_(sticker_type)
      , offset_sticker_set_id_(offset_sticker_set_id)
      , limit_(limit) {
  }
};

class GetTrendingStickerSetsRequest final : public RequestActor<> {
  td_api::object_ptr<td_api::trendingStickerSets> result_;
  StickerType sticker_type_;
  int32 offset_;
  int32 limit_;

  void do_run(Promise<Unit> &&promise) final {
    result_ = td_->stickers_manager_->get_featured_sticker_sets(sticker_type_, offset_, limit_, std::move(promise));
  }

  void do_send_result() final {
    send_result(std::move(result_));
  }

 public:
  GetTrendingStickerSetsRequest(ActorShared<Td> td, uint64 request_id, StickerType sticker_type, int32 offset,
                                int32 limit)
      : RequestActor(std::move(td), request_id), sticker_type_(sticker_type), offset_(offset), limit_(limit) {
    set_tries(3);
  }
};

class GetAttachedStickerSetsRequest final : public RequestActor<> {
  FileId file_id_;

  vector<StickerSetId> sticker_set_ids_;

  void do_run(Promise<Unit> &&promise) final {
    sticker_set_ids_ = td_->stickers_manager_->get_attached_sticker_sets(file_id_, std::move(promise));
  }

  void do_send_result() final {
    send_result(td_->stickers_manager_->get_sticker_sets_object(-1, sticker_set_ids_, 5));
  }

 public:
  GetAttachedStickerSetsRequest(ActorShared<Td> td, uint64 request_id, int32 file_id)
      : RequestActor(std::move(td), request_id), file_id_(file_id, 0) {
  }
};

class GetStickerSetRequest final : public RequestActor<> {
  StickerSetId set_id_;

  StickerSetId sticker_set_id_;

  void do_run(Promise<Unit> &&promise) final {
    sticker_set_id_ = td_->stickers_manager_->get_sticker_set(set_id_, std::move(promise));
  }

  void do_send_result() final {
    send_result(td_->stickers_manager_->get_sticker_set_object(sticker_set_id_));
  }

 public:
  GetStickerSetRequest(ActorShared<Td> td, uint64 request_id, int64 set_id)
      : RequestActor(std::move(td), request_id), set_id_(set_id) {
    set_tries(3);
  }
};

class SearchStickerSetRequest final : public RequestActor<> {
  string name_;

  StickerSetId sticker_set_id_;

  void do_run(Promise<Unit> &&promise) final {
    sticker_set_id_ = td_->stickers_manager_->search_sticker_set(name_, std::move(promise));
  }

  void do_send_result() final {
    send_result(td_->stickers_manager_->get_sticker_set_object(sticker_set_id_));
  }

 public:
  SearchStickerSetRequest(ActorShared<Td> td, uint64 request_id, string &&name)
      : RequestActor(std::move(td), request_id), name_(std::move(name)) {
    set_tries(3);
  }
};

class SearchInstalledStickerSetsRequest final : public RequestActor<> {
  StickerType sticker_type_;
  string query_;
  int32 limit_;

  std::pair<int32, vector<StickerSetId>> sticker_set_ids_;

  void do_run(Promise<Unit> &&promise) final {
    sticker_set_ids_ =
        td_->stickers_manager_->search_installed_sticker_sets(sticker_type_, query_, limit_, std::move(promise));
  }

  void do_send_result() final {
    send_result(td_->stickers_manager_->get_sticker_sets_object(sticker_set_ids_.first, sticker_set_ids_.second, 5));
  }

 public:
  SearchInstalledStickerSetsRequest(ActorShared<Td> td, uint64 request_id, StickerType sticker_type, string &&query,
                                    int32 limit)
      : RequestActor(std::move(td), request_id), sticker_type_(sticker_type), query_(std::move(query)), limit_(limit) {
  }
};

class SearchStickerSetsRequest final : public RequestActor<> {
  StickerType sticker_type_;
  string query_;

  vector<StickerSetId> sticker_set_ids_;

  void do_run(Promise<Unit> &&promise) final {
    sticker_set_ids_ = td_->stickers_manager_->search_sticker_sets(sticker_type_, query_, std::move(promise));
  }

  void do_send_result() final {
    send_result(td_->stickers_manager_->get_sticker_sets_object(-1, sticker_set_ids_, 5));
  }

 public:
  SearchStickerSetsRequest(ActorShared<Td> td, uint64 request_id, StickerType sticker_type, string &&query)
      : RequestActor(std::move(td), request_id), sticker_type_(sticker_type), query_(std::move(query)) {
  }
};

class ChangeStickerSetRequest final : public RequestOnceActor {
  StickerSetId set_id_;
  bool is_installed_;
  bool is_archived_;

  void do_run(Promise<Unit> &&promise) final {
    td_->stickers_manager_->change_sticker_set(set_id_, is_installed_, is_archived_, std::move(promise));
  }

 public:
  ChangeStickerSetRequest(ActorShared<Td> td, uint64 request_id, int64 set_id, bool is_installed, bool is_archived)
      : RequestOnceActor(std::move(td), request_id)
      , set_id_(set_id)
      , is_installed_(is_installed)
      , is_archived_(is_archived) {
    set_tries(4);
  }
};

class UploadStickerFileRequest final : public RequestOnceActor {
  UserId user_id_;
  StickerFormat sticker_format_;
  td_api::object_ptr<td_api::InputFile> input_file_;

  FileId file_id;

  void do_run(Promise<Unit> &&promise) final {
    file_id = td_->stickers_manager_->upload_sticker_file(user_id_, sticker_format_, input_file_, std::move(promise));
  }

  void do_send_result() final {
    send_result(td_->file_manager_->get_file_object(file_id));
  }

 public:
  UploadStickerFileRequest(ActorShared<Td> td, uint64 request_id, int64 user_id, StickerFormat sticker_format,
                           td_api::object_ptr<td_api::InputFile> input_file)
      : RequestOnceActor(std::move(td), request_id)
      , user_id_(user_id)
      , sticker_format_(sticker_format)
      , input_file_(std::move(input_file)) {
  }
};

class GetRecentStickersRequest final : public RequestActor<> {
  bool is_attached_;

  vector<FileId> sticker_ids_;

  void do_run(Promise<Unit> &&promise) final {
    sticker_ids_ = td_->stickers_manager_->get_recent_stickers(is_attached_, std::move(promise));
  }

  void do_send_result() final {
    send_result(td_->stickers_manager_->get_stickers_object(sticker_ids_));
  }

 public:
  GetRecentStickersRequest(ActorShared<Td> td, uint64 request_id, bool is_attached)
      : RequestActor(std::move(td), request_id), is_attached_(is_attached) {
  }
};

class AddRecentStickerRequest final : public RequestActor<> {
  bool is_attached_;
  tl_object_ptr<td_api::InputFile> input_file_;

  void do_run(Promise<Unit> &&promise) final {
    td_->stickers_manager_->add_recent_sticker(is_attached_, input_file_, std::move(promise));
  }

 public:
  AddRecentStickerRequest(ActorShared<Td> td, uint64 request_id, bool is_attached,
                          tl_object_ptr<td_api::InputFile> &&input_file)
      : RequestActor(std::move(td), request_id), is_attached_(is_attached), input_file_(std::move(input_file)) {
    set_tries(3);
  }
};

class RemoveRecentStickerRequest final : public RequestActor<> {
  bool is_attached_;
  tl_object_ptr<td_api::InputFile> input_file_;

  void do_run(Promise<Unit> &&promise) final {
    td_->stickers_manager_->remove_recent_sticker(is_attached_, input_file_, std::move(promise));
  }

 public:
  RemoveRecentStickerRequest(ActorShared<Td> td, uint64 request_id, bool is_attached,
                             tl_object_ptr<td_api::InputFile> &&input_file)
      : RequestActor(std::move(td), request_id), is_attached_(is_attached), input_file_(std::move(input_file)) {
    set_tries(3);
  }
};

class ClearRecentStickersRequest final : public RequestActor<> {
  bool is_attached_;

  void do_run(Promise<Unit> &&promise) final {
    td_->stickers_manager_->clear_recent_stickers(is_attached_, std::move(promise));
  }

 public:
  ClearRecentStickersRequest(ActorShared<Td> td, uint64 request_id, bool is_attached)
      : RequestActor(std::move(td), request_id), is_attached_(is_attached) {
    set_tries(3);
  }
};

class GetFavoriteStickersRequest final : public RequestActor<> {
  vector<FileId> sticker_ids_;

  void do_run(Promise<Unit> &&promise) final {
    sticker_ids_ = td_->stickers_manager_->get_favorite_stickers(std::move(promise));
  }

  void do_send_result() final {
    send_result(td_->stickers_manager_->get_stickers_object(sticker_ids_));
  }

 public:
  GetFavoriteStickersRequest(ActorShared<Td> td, uint64 request_id) : RequestActor(std::move(td), request_id) {
  }
};

class AddFavoriteStickerRequest final : public RequestOnceActor {
  tl_object_ptr<td_api::InputFile> input_file_;

  void do_run(Promise<Unit> &&promise) final {
    td_->stickers_manager_->add_favorite_sticker(input_file_, std::move(promise));
  }

 public:
  AddFavoriteStickerRequest(ActorShared<Td> td, uint64 request_id, tl_object_ptr<td_api::InputFile> &&input_file)
      : RequestOnceActor(std::move(td), request_id), input_file_(std::move(input_file)) {
    set_tries(3);
  }
};

class RemoveFavoriteStickerRequest final : public RequestOnceActor {
  tl_object_ptr<td_api::InputFile> input_file_;

  void do_run(Promise<Unit> &&promise) final {
    td_->stickers_manager_->remove_favorite_sticker(input_file_, std::move(promise));
  }

 public:
  RemoveFavoriteStickerRequest(ActorShared<Td> td, uint64 request_id, tl_object_ptr<td_api::InputFile> &&input_file)
      : RequestOnceActor(std::move(td), request_id), input_file_(std::move(input_file)) {
    set_tries(3);
  }
};

class GetStickerEmojisRequest final : public RequestActor<> {
  tl_object_ptr<td_api::InputFile> input_file_;

  vector<string> emojis_;

  void do_run(Promise<Unit> &&promise) final {
    emojis_ = td_->stickers_manager_->get_sticker_emojis(input_file_, std::move(promise));
  }

  void do_send_result() final {
    send_result(td_api::make_object<td_api::emojis>(std::move(emojis_)));
  }

 public:
  GetStickerEmojisRequest(ActorShared<Td> td, uint64 request_id, tl_object_ptr<td_api::InputFile> &&input_file)
      : RequestActor(std::move(td), request_id), input_file_(std::move(input_file)) {
    set_tries(3);
  }
};

class SearchEmojisRequest final : public RequestActor<> {
  string text_;
  vector<string> input_language_codes_;

  vector<std::pair<string, string>> emoji_keywords_;

  void do_run(Promise<Unit> &&promise) final {
    emoji_keywords_ =
        td_->stickers_manager_->search_emojis(text_, input_language_codes_, get_tries() < 2, std::move(promise));
  }

  void do_send_result() final {
    send_result(td_api::make_object<td_api::emojiKeywords>(
        transform(emoji_keywords_, [](const std::pair<string, string> &emoji_keyword) {
          return td_api::make_object<td_api::emojiKeyword>(emoji_keyword.first, emoji_keyword.second);
        })));
  }

 public:
  SearchEmojisRequest(ActorShared<Td> td, uint64 request_id, string &&text, vector<string> &&input_language_codes)
      : RequestActor(std::move(td), request_id)
      , text_(std::move(text))
      , input_language_codes_(std::move(input_language_codes)) {
    set_tries(3);
  }
};

class GetKeywordEmojisRequest final : public RequestActor<> {
  string text_;
  vector<string> input_language_codes_;

  vector<string> emojis_;

  void do_run(Promise<Unit> &&promise) final {
    emojis_ =
        td_->stickers_manager_->get_keyword_emojis(text_, input_language_codes_, get_tries() < 2, std::move(promise));
  }

  void do_send_result() final {
    send_result(td_api::make_object<td_api::emojis>(std::move(emojis_)));
  }

 public:
  GetKeywordEmojisRequest(ActorShared<Td> td, uint64 request_id, string &&text, vector<string> &&input_language_codes)
      : RequestActor(std::move(td), request_id)
      , text_(std::move(text))
      , input_language_codes_(std::move(input_language_codes)) {
    set_tries(3);
  }
};

class GetEmojiSuggestionsUrlRequest final : public RequestOnceActor {
  string language_code_;

  int64 random_id_;

  void do_run(Promise<Unit> &&promise) final {
    random_id_ = td_->stickers_manager_->get_emoji_suggestions_url(language_code_, std::move(promise));
  }

  void do_send_result() final {
    send_result(td_->stickers_manager_->get_emoji_suggestions_url_result(random_id_));
  }

 public:
  GetEmojiSuggestionsUrlRequest(ActorShared<Td> td, uint64 request_id, string &&language_code)
      : RequestOnceActor(std::move(td), request_id), language_code_(std::move(language_code)), random_id_(0) {
  }
};

class GetSavedAnimationsRequest final : public RequestActor<> {
  vector<FileId> animation_ids_;

  void do_run(Promise<Unit> &&promise) final {
    animation_ids_ = td_->animations_manager_->get_saved_animations(std::move(promise));
  }

  void do_send_result() final {
    send_result(make_tl_object<td_api::animations>(transform(animation_ids_, [td = td_](FileId animation_id) {
      return td->animations_manager_->get_animation_object(animation_id);
    })));
  }

 public:
  GetSavedAnimationsRequest(ActorShared<Td> td, uint64 request_id) : RequestActor(std::move(td), request_id) {
  }
};

class AddSavedAnimationRequest final : public RequestOnceActor {
  tl_object_ptr<td_api::InputFile> input_file_;

  void do_run(Promise<Unit> &&promise) final {
    td_->animations_manager_->add_saved_animation(input_file_, std::move(promise));
  }

 public:
  AddSavedAnimationRequest(ActorShared<Td> td, uint64 request_id, tl_object_ptr<td_api::InputFile> &&input_file)
      : RequestOnceActor(std::move(td), request_id), input_file_(std::move(input_file)) {
    set_tries(3);
  }
};

class RemoveSavedAnimationRequest final : public RequestOnceActor {
  tl_object_ptr<td_api::InputFile> input_file_;

  void do_run(Promise<Unit> &&promise) final {
    td_->animations_manager_->remove_saved_animation(input_file_, std::move(promise));
  }

 public:
  RemoveSavedAnimationRequest(ActorShared<Td> td, uint64 request_id, tl_object_ptr<td_api::InputFile> &&input_file)
      : RequestOnceActor(std::move(td), request_id), input_file_(std::move(input_file)) {
    set_tries(3);
  }
};

class GetSavedNotificationSoundRequest final : public RequestActor<> {
  int64 ringtone_id_;
  FileId ringtone_file_id_;

  void do_run(Promise<Unit> &&promise) final {
    ringtone_file_id_ = td_->notification_settings_manager_->get_saved_ringtone(ringtone_id_, std::move(promise));
  }

  void do_send_result() final {
    send_result(td_->audios_manager_->get_notification_sound_object(ringtone_file_id_));
  }

 public:
  GetSavedNotificationSoundRequest(ActorShared<Td> td, uint64 request_id, int64 ringtone_id)
      : RequestActor(std::move(td), request_id), ringtone_id_(ringtone_id) {
  }
};

class GetSavedNotificationSoundsRequest final : public RequestActor<> {
  vector<FileId> ringtone_file_ids_;

  void do_run(Promise<Unit> &&promise) final {
    ringtone_file_ids_ = td_->notification_settings_manager_->get_saved_ringtones(std::move(promise));
  }

  void do_send_result() final {
    send_result(td_api::make_object<td_api::notificationSounds>(
        transform(ringtone_file_ids_, [td = td_](FileId ringtone_file_id) {
          return td->audios_manager_->get_notification_sound_object(ringtone_file_id);
        })));
  }

 public:
  GetSavedNotificationSoundsRequest(ActorShared<Td> td, uint64 request_id) : RequestActor(std::move(td), request_id) {
  }
};

class RemoveSavedNotificationSoundRequest final : public RequestOnceActor {
  int64 ringtone_id_;

  void do_run(Promise<Unit> &&promise) final {
    td_->notification_settings_manager_->remove_saved_ringtone(ringtone_id_, std::move(promise));
  }

 public:
  RemoveSavedNotificationSoundRequest(ActorShared<Td> td, uint64 request_id, int64 ringtone_id)
      : RequestOnceActor(std::move(td), request_id), ringtone_id_(ringtone_id) {
    set_tries(3);
  }
};

class GetInlineQueryResultsRequest final : public RequestOnceActor {
  UserId bot_user_id_;
  DialogId dialog_id_;
  Location user_location_;
  string query_;
  string offset_;

  uint64 query_hash_;

  void do_run(Promise<Unit> &&promise) final {
    query_hash_ = td_->inline_queries_manager_->send_inline_query(bot_user_id_, dialog_id_, user_location_, query_,
                                                                  offset_, std::move(promise));
  }

  void do_send_result() final {
    send_result(td_->inline_queries_manager_->get_inline_query_results_object(query_hash_));
  }

 public:
  GetInlineQueryResultsRequest(ActorShared<Td> td, uint64 request_id, int64 bot_user_id, int64 dialog_id,
                               const tl_object_ptr<td_api::location> &user_location, string query, string offset)
      : RequestOnceActor(std::move(td), request_id)
      , bot_user_id_(bot_user_id)
      , dialog_id_(dialog_id)
      , user_location_(user_location)
      , query_(std::move(query))
      , offset_(std::move(offset))
      , query_hash_(0) {
  }
};

class SearchBackgroundRequest final : public RequestActor<> {
  string name_;

  std::pair<BackgroundId, BackgroundType> background_;

  void do_run(Promise<Unit> &&promise) final {
    background_ = td_->background_manager_->search_background(name_, std::move(promise));
  }

  void do_send_result() final {
    send_result(td_->background_manager_->get_background_object(background_.first, false, &background_.second));
  }

 public:
  SearchBackgroundRequest(ActorShared<Td> td, uint64 request_id, string &&name)
      : RequestActor(std::move(td), request_id), name_(std::move(name)) {
    set_tries(3);
  }
};

Td::Td(unique_ptr<TdCallback> callback, Options options)
    : callback_(std::move(callback)), td_options_(std::move(options)) {
  CHECK(callback_ != nullptr);
  LOG(INFO) << "Create Td with layer " << MTPROTO_LAYER << ", database version " << current_db_version()
            << " and version " << static_cast<int32>(Version::Next) - 1 << " on "
            << Scheduler::instance()->sched_count() << " threads";
}

Td::~Td() = default;

void Td::on_alarm_timeout_callback(void *td_ptr, int64 alarm_id) {
  auto td = static_cast<Td *>(td_ptr);
  auto td_id = td->actor_id(td);
  send_closure_later(td_id, &Td::on_alarm_timeout, alarm_id);
}

void Td::on_alarm_timeout(int64 alarm_id) {
  if (alarm_id == ONLINE_ALARM_ID) {
    on_online_updated(false, true);
    return;
  }
  if (alarm_id == PING_SERVER_ALARM_ID) {
    if (!close_flag_ && updates_manager_ != nullptr && auth_manager_->is_authorized()) {
      updates_manager_->ping_server();
      set_is_bot_online(false);
    }
    return;
  }
  if (alarm_id == TERMS_OF_SERVICE_ALARM_ID) {
    if (!close_flag_ && !auth_manager_->is_bot()) {
      get_terms_of_service(
          this, PromiseCreator::lambda([actor_id = actor_id(this)](Result<std::pair<int32, TermsOfService>> result) {
            send_closure(actor_id, &Td::on_get_terms_of_service, std::move(result), false);
          }));
    }
    return;
  }
  if (alarm_id == PROMO_DATA_ALARM_ID) {
    if (!close_flag_ && !auth_manager_->is_bot()) {
      reloading_promo_data_ = true;
      auto promise = PromiseCreator::lambda(
          [actor_id = actor_id(this)](Result<telegram_api::object_ptr<telegram_api::help_PromoData>> result) {
            send_closure(actor_id, &Td::on_get_promo_data, std::move(result), false);
          });
      create_handler<GetPromoDataQuery>(std::move(promise))->send();
    }
    return;
  }
  if (close_flag_ >= 2) {
    // pending_alarms_ was already cleared
    return;
  }

  auto it = pending_alarms_.find(alarm_id);
  CHECK(it != pending_alarms_.end());
  uint64 request_id = it->second;
  pending_alarms_.erase(alarm_id);
  send_result(request_id, make_tl_object<td_api::ok>());
}

void Td::on_online_updated(bool force, bool send_update) {
  if (close_flag_ >= 2 || !auth_manager_->is_authorized() || auth_manager_->is_bot()) {
    return;
  }
  if (force || is_online_) {
    user_manager_->set_my_online_status(is_online_, send_update, true);
    if (!update_status_query_.empty()) {
      LOG(INFO) << "Cancel previous update status query";
      cancel_query(update_status_query_);
    }
    update_status_query_ = create_handler<UpdateStatusQuery>()->send(!is_online_);
  }
  if (is_online_) {
    alarm_timeout_.set_timeout_in(
        ONLINE_ALARM_ID, static_cast<double>(G()->get_option_integer("online_update_period_ms", 210000)) * 1e-3);
  } else {
    alarm_timeout_.cancel_timeout(ONLINE_ALARM_ID);
  }
}

void Td::on_update_status_success(bool is_online) {
  if (is_online == is_online_) {
    if (!update_status_query_.empty()) {
      update_status_query_ = NetQueryRef();
    }
    user_manager_->set_my_online_status(is_online_, true, false);
  }
}

td_api::object_ptr<td_api::updateTermsOfService> Td::get_update_terms_of_service_object() const {
  auto terms_of_service = pending_terms_of_service_.get_terms_of_service_object();
  if (terms_of_service == nullptr) {
    return nullptr;
  }
  return td_api::make_object<td_api::updateTermsOfService>(pending_terms_of_service_.get_id().str(),
                                                           std::move(terms_of_service));
}

void Td::on_get_terms_of_service(Result<std::pair<int32, TermsOfService>> result, bool dummy) {
  int32 expires_in = 0;
  if (result.is_error()) {
    expires_in = Random::fast(10, 60);
  } else {
    auto terms = result.move_as_ok();
    pending_terms_of_service_ = std::move(terms.second);
    auto update = get_update_terms_of_service_object();
    if (update == nullptr) {
      expires_in = min(max(terms.first, G()->unix_time() + 3600) - G()->unix_time(), 86400);
    } else {
      send_update(std::move(update));
    }
  }
  if (expires_in > 0) {
    schedule_get_terms_of_service(expires_in);
  }
}

void Td::schedule_get_terms_of_service(int32 expires_in) {
  if (expires_in == 0) {
    // drop pending Terms of Service after successful accept
    pending_terms_of_service_ = TermsOfService();
  }
  if (!close_flag_ && !auth_manager_->is_bot()) {
    alarm_timeout_.set_timeout_in(TERMS_OF_SERVICE_ALARM_ID, expires_in);
  }
}

void Td::on_get_promo_data(Result<telegram_api::object_ptr<telegram_api::help_PromoData>> r_promo_data, bool dummy) {
  if (G()->close_flag()) {
    return;
  }
  reloading_promo_data_ = false;

  if (r_promo_data.is_error()) {
    LOG(ERROR) << "Receive error for GetPromoData: " << r_promo_data.error();
    return schedule_get_promo_data(60);
  }

  auto promo_data_ptr = r_promo_data.move_as_ok();
  CHECK(promo_data_ptr != nullptr);
  LOG(DEBUG) << "Receive " << to_string(promo_data_ptr);
  int32 expires_at = 0;
  switch (promo_data_ptr->get_id()) {
    case telegram_api::help_promoDataEmpty::ID: {
      auto promo = telegram_api::move_object_as<telegram_api::help_promoDataEmpty>(promo_data_ptr);
      expires_at = promo->expires_;
      messages_manager_->remove_sponsored_dialog();
      break;
    }
    case telegram_api::help_promoData::ID: {
      auto promo = telegram_api::move_object_as<telegram_api::help_promoData>(promo_data_ptr);
      expires_at = promo->expires_;
      bool is_proxy = promo->proxy_;
      messages_manager_->on_get_sponsored_dialog(
          std::move(promo->peer_),
          is_proxy ? DialogSource::mtproto_proxy()
                   : DialogSource::public_service_announcement(promo->psa_type_, promo->psa_message_),
          std::move(promo->users_), std::move(promo->chats_));
      break;
    }
    default:
      UNREACHABLE();
  }
  if (need_reload_promo_data_) {
    need_reload_promo_data_ = false;
    expires_at = 0;
  }
  schedule_get_promo_data(expires_at == 0 ? 0 : expires_at - G()->unix_time());
}

void Td::reload_promo_data() {
  if (reloading_promo_data_) {
    need_reload_promo_data_ = true;
    return;
  }
  schedule_get_promo_data(0);
}

void Td::schedule_get_promo_data(int32 expires_in) {
  expires_in = expires_in <= 0 ? 0 : clamp(expires_in, 60, 86400);
  if (!close_flag_ && auth_manager_->is_authorized() && !auth_manager_->is_bot()) {
    LOG(INFO) << "Schedule getPromoData in " << expires_in;
    alarm_timeout_.set_timeout_in(PROMO_DATA_ALARM_ID, expires_in);
  }
}

bool Td::is_online() const {
  return is_online_;
}

void Td::set_is_online(bool is_online) {
  if (is_online == is_online_) {
    return;
  }

  is_online_ = is_online;
  if (auth_manager_ != nullptr) {  // postpone if there is no AuthManager yet
    on_online_updated(true, true);
  }
}

void Td::set_is_bot_online(bool is_bot_online) {
  alarm_timeout_.set_timeout_in(PING_SERVER_ALARM_ID, PING_SERVER_TIMEOUT + Random::fast(0, PING_SERVER_TIMEOUT / 5));

  if (G()->get_option_integer("session_count") > 1) {
    is_bot_online = false;
  }

  if (is_bot_online == is_bot_online_) {
    return;
  }

  is_bot_online_ = is_bot_online;
  send_closure(G()->state_manager(), &StateManager::on_online, is_bot_online_);
}

bool Td::ignore_background_updates() const {
  return can_ignore_background_updates_ && option_manager_->get_option_boolean("ignore_background_updates");
}

bool Td::is_authentication_request(int32 id) {
  switch (id) {
    case td_api::setTdlibParameters::ID:
    case td_api::getAuthorizationState::ID:
    case td_api::setAuthenticationPhoneNumber::ID:
    case td_api::sendAuthenticationFirebaseSms::ID:
    case td_api::reportAuthenticationCodeMissing::ID:
    case td_api::setAuthenticationEmailAddress::ID:
    case td_api::resendAuthenticationCode::ID:
    case td_api::checkAuthenticationEmailCode::ID:
    case td_api::checkAuthenticationCode::ID:
    case td_api::registerUser::ID:
    case td_api::requestQrCodeAuthentication::ID:
    case td_api::resetAuthenticationEmailAddress::ID:
    case td_api::checkAuthenticationPassword::ID:
    case td_api::requestAuthenticationPasswordRecovery::ID:
    case td_api::checkAuthenticationPasswordRecoveryCode::ID:
    case td_api::recoverAuthenticationPassword::ID:
    case td_api::deleteAccount::ID:
    case td_api::logOut::ID:
    case td_api::close::ID:
    case td_api::destroy::ID:
    case td_api::checkAuthenticationBotToken::ID:
      return true;
    default:
      return false;
  }
}

bool Td::is_synchronous_request(const td_api::Function *function) {
  switch (function->get_id()) {
    case td_api::searchQuote::ID:
    case td_api::getTextEntities::ID:
    case td_api::parseTextEntities::ID:
    case td_api::parseMarkdown::ID:
    case td_api::getMarkdownText::ID:
    case td_api::searchStringsByPrefix::ID:
    case td_api::checkQuickReplyShortcutName::ID:
    case td_api::getCountryFlagEmoji::ID:
    case td_api::getFileMimeType::ID:
    case td_api::getFileExtension::ID:
    case td_api::cleanFileName::ID:
    case td_api::getLanguagePackString::ID:
    case td_api::getPhoneNumberInfoSync::ID:
    case td_api::getChatFolderDefaultIconName::ID:
    case td_api::getJsonValue::ID:
    case td_api::getJsonString::ID:
    case td_api::getThemeParametersJsonString::ID:
    case td_api::getPushReceiverId::ID:
    case td_api::setLogStream::ID:
    case td_api::getLogStream::ID:
    case td_api::setLogVerbosityLevel::ID:
    case td_api::getLogVerbosityLevel::ID:
    case td_api::getLogTags::ID:
    case td_api::setLogTagVerbosityLevel::ID:
    case td_api::getLogTagVerbosityLevel::ID:
    case td_api::addLogMessage::ID:
    case td_api::testReturnError::ID:
      return true;
    case td_api::getOption::ID:
      return OptionManager::is_synchronous_option(static_cast<const td_api::getOption *>(function)->name_);
    default:
      return false;
  }
}

bool Td::is_preinitialization_request(int32 id) {
  switch (id) {
    case td_api::getCurrentState::ID:
    case td_api::setAlarm::ID:
    case td_api::testUseUpdate::ID:
    case td_api::testCallEmpty::ID:
    case td_api::testSquareInt::ID:
    case td_api::testCallString::ID:
    case td_api::testCallBytes::ID:
    case td_api::testCallVectorInt::ID:
    case td_api::testCallVectorIntObject::ID:
    case td_api::testCallVectorString::ID:
    case td_api::testCallVectorStringObject::ID:
    case td_api::testProxy::ID:
      return true;
    default:
      return false;
  }
}

bool Td::is_preauthentication_request(int32 id) {
  switch (id) {
    case td_api::getInternalLink::ID:
    case td_api::getInternalLinkType::ID:
    case td_api::getLocalizationTargetInfo::ID:
    case td_api::getLanguagePackInfo::ID:
    case td_api::getLanguagePackStrings::ID:
    case td_api::synchronizeLanguagePack::ID:
    case td_api::addCustomServerLanguagePack::ID:
    case td_api::setCustomLanguagePack::ID:
    case td_api::editCustomLanguagePackInfo::ID:
    case td_api::setCustomLanguagePackString::ID:
    case td_api::deleteLanguagePack::ID:
    case td_api::processPushNotification::ID:
    case td_api::getOption::ID:
    case td_api::setOption::ID:
    case td_api::getStorageStatistics::ID:
    case td_api::getStorageStatisticsFast::ID:
    case td_api::getDatabaseStatistics::ID:
    case td_api::setNetworkType::ID:
    case td_api::getNetworkStatistics::ID:
    case td_api::addNetworkStatistics::ID:
    case td_api::resetNetworkStatistics::ID:
    case td_api::setApplicationVerificationToken::ID:
    case td_api::getCountries::ID:
    case td_api::getCountryCode::ID:
    case td_api::getPhoneNumberInfo::ID:
    case td_api::getDeepLinkInfo::ID:
    case td_api::getApplicationConfig::ID:
    case td_api::saveApplicationLogEvent::ID:
    case td_api::addProxy::ID:
    case td_api::editProxy::ID:
    case td_api::enableProxy::ID:
    case td_api::disableProxy::ID:
    case td_api::removeProxy::ID:
    case td_api::getProxies::ID:
    case td_api::getProxyLink::ID:
    case td_api::pingProxy::ID:
    case td_api::testNetwork::ID:
      return true;
    default:
      return false;
  }
}

td_api::object_ptr<td_api::AuthorizationState> Td::get_fake_authorization_state_object() const {
  switch (state_) {
    case State::WaitParameters:
      return td_api::make_object<td_api::authorizationStateWaitTdlibParameters>();
    case State::Run:
      UNREACHABLE();
      return nullptr;
    case State::Close:
      if (close_flag_ == 5) {
        return td_api::make_object<td_api::authorizationStateClosed>();
      } else {
        return td_api::make_object<td_api::authorizationStateClosing>();
      }
    default:
      UNREACHABLE();
      return nullptr;
  }
}

vector<td_api::object_ptr<td_api::Update>> Td::get_fake_current_state() const {
  CHECK(state_ != State::Run);
  vector<td_api::object_ptr<td_api::Update>> updates;
  OptionManager::get_common_state(updates);
  updates.push_back(td_api::make_object<td_api::updateAuthorizationState>(get_fake_authorization_state_object()));
  return updates;
}

DbKey Td::as_db_key(string key) {
  // Database will still be effectively not encrypted, but
  // 1. SQLite database will be protected from corruption, because that's how sqlcipher works
  // 2. security through obscurity
  // 3. no need for reencryption of SQLite database
  if (key.empty()) {
    return DbKey::raw_key("cucumber");
  }
  return DbKey::raw_key(std::move(key));
}

void Td::request(uint64 id, tl_object_ptr<td_api::Function> function) {
  if (id == 0) {
    LOG(ERROR) << "Ignore request with ID == 0: " << to_string(function);
    return;
  }

  if (function == nullptr) {
    return callback_->on_error(id, make_error(400, "Request is empty"));
  }

  VLOG(td_requests) << "Receive request " << id << ": " << to_string(function);
  request_set_.emplace(id, function->get_id());
  if (is_synchronous_request(function.get())) {
    // send response synchronously
    return send_result(id, static_request(std::move(function)));
  }

  run_request(id, std::move(function));
}

void Td::run_request(uint64 id, tl_object_ptr<td_api::Function> function) {
  if (set_parameters_request_id_ > 0) {
    pending_set_parameters_requests_.emplace_back(id, std::move(function));
    return;
  }

  int32 function_id = function->get_id();
  if (state_ != State::Run) {
    switch (function_id) {
      case td_api::getAuthorizationState::ID:
        // send response synchronously to prevent "Request aborted"
        return send_result(id, get_fake_authorization_state_object());
      case td_api::getCurrentState::ID:
        // send response synchronously to prevent "Request aborted"
        return send_result(id, td_api::make_object<td_api::updates>(get_fake_current_state()));
      case td_api::close::ID:
        // need to send response before actual closing
        send_closure(actor_id(this), &Td::send_result, id, td_api::make_object<td_api::ok>());
        send_closure(actor_id(this), &Td::close);
        return;
      default:
        break;
    }
  }
  switch (state_) {
    case State::WaitParameters: {
      switch (function_id) {
        case td_api::setTdlibParameters::ID: {
          auto r_parameters = get_parameters(move_tl_object_as<td_api::setTdlibParameters>(function));
          if (r_parameters.is_error()) {
            return send_closure(actor_id(this), &Td::send_error, id, r_parameters.move_as_error());
          }
          auto parameters = r_parameters.move_as_ok();

          VLOG(td_init) << "Begin to open database";
          set_parameters_request_id_ = id;
          can_ignore_background_updates_ = !parameters.second.use_chat_info_database_ &&
                                           !parameters.second.use_message_database_ &&
                                           !parameters.first.use_secret_chats_;

          auto promise = PromiseCreator::lambda(
              [actor_id = actor_id(this), parameters = std::move(parameters.first),
               parent = create_reference()](Result<TdDb::OpenedDatabase> r_opened_database) mutable {
                send_closure(actor_id, &Td::init, std::move(parameters), std::move(r_opened_database));
              });
          auto use_sqlite_pmc = parameters.second.use_message_database_ || parameters.second.use_chat_info_database_ ||
                                parameters.second.use_file_database_;
          return TdDb::open(use_sqlite_pmc ? G()->get_database_scheduler_id() : G()->get_slow_net_scheduler_id(),
                            std::move(parameters.second), std::move(promise));
        }
        default:
          if (is_preinitialization_request(function_id)) {
            break;
          }
          if (is_preauthentication_request(function_id)) {
            pending_preauthentication_requests_.emplace_back(id, std::move(function));
            return;
          }
          return send_error_impl(
              id, make_error(400, "Initialization parameters are needed: call setTdlibParameters first"));
      }
      break;
    }
    case State::Close:
      if (destroy_flag_) {
        return send_error_impl(id, make_error(401, "Unauthorized"));
      } else {
        return send_error_impl(id, make_error(500, "Request aborted"));
      }
    case State::Run:
      break;
  }

  if ((auth_manager_ == nullptr || !auth_manager_->is_authorized()) && !is_preauthentication_request(function_id) &&
      !is_preinitialization_request(function_id) && !is_authentication_request(function_id)) {
    return send_error_impl(id, make_error(401, "Unauthorized"));
  }
  downcast_call(*function, [this, id](auto &request) { this->on_request(id, request); });
}

td_api::object_ptr<td_api::Object> Td::static_request(td_api::object_ptr<td_api::Function> function) {
  if (function == nullptr) {
    return td_api::make_object<td_api::error>(400, "Request is empty");
  }

  auto function_id = function->get_id();
  bool need_logging = [function_id] {
    switch (function_id) {
      case td_api::parseTextEntities::ID:
      case td_api::parseMarkdown::ID:
      case td_api::getMarkdownText::ID:
      case td_api::searchStringsByPrefix::ID:
      case td_api::checkQuickReplyShortcutName::ID:
      case td_api::getCountryFlagEmoji::ID:
      case td_api::getFileMimeType::ID:
      case td_api::getFileExtension::ID:
      case td_api::cleanFileName::ID:
      case td_api::getChatFolderDefaultIconName::ID:
      case td_api::getJsonValue::ID:
      case td_api::getJsonString::ID:
      case td_api::getThemeParametersJsonString::ID:
      case td_api::testReturnError::ID:
        return true;
      default:
        return false;
    }
  }();

  if (need_logging) {
    VLOG(td_requests) << "Receive static request: " << to_string(function);
  }

  td_api::object_ptr<td_api::Object> response;
  downcast_call(*function, [&response](auto &request) { response = Td::do_static_request(request); });
  LOG_CHECK(response != nullptr) << function_id;

  if (need_logging) {
    VLOG(td_requests) << "Sending result for static request: " << to_string(response);
  }
  return response;
}

void Td::add_handler(uint64 id, std::shared_ptr<ResultHandler> handler) {
  result_handlers_[id] = std::move(handler);
}

std::shared_ptr<Td::ResultHandler> Td::extract_handler(uint64 id) {
  auto it = result_handlers_.find(id);
  if (it == result_handlers_.end()) {
    return nullptr;
  }
  auto result = std::move(it->second);
  result_handlers_.erase(it);
  return result;
}

void Td::on_update(telegram_api::object_ptr<telegram_api::Updates> updates, uint64 auth_key_id) {
  if (close_flag_ > 1) {
    return;
  }

  if (updates == nullptr) {
    if (auth_manager_->is_bot()) {
      G()->net_query_dispatcher().update_mtproto_header();
    } else {
      // this could be a min-channel update
      updates_manager_->schedule_get_difference("failed to fetch updates");
    }
  } else {
    updates_manager_->on_update_from_auth_key_id(auth_key_id);
    updates_manager_->on_get_updates(std::move(updates), Promise<Unit>());
    if (auth_manager_->is_bot() && auth_manager_->is_authorized()) {
      set_is_bot_online(true);
    }
  }
}

void Td::on_result(NetQueryPtr query) {
  query->debug("Td: received from DcManager");
  VLOG(net_query) << "Receive result of " << query;
  if (close_flag_ > 1) {
    return;
  }

  auto handler = extract_handler(query->id());
  if (handler != nullptr) {
    CHECK(query->is_ready());
    if (query->is_ok()) {
      handler->on_result(query->move_as_ok());
    } else {
      handler->on_error(query->move_as_error());
    }
  } else {
    if (!query->is_ok() || query->ok_tl_constructor() != telegram_api::upload_file::ID) {
      LOG(WARNING) << query << " is ignored: no handlers found";
    }
    query->clear();
  }
}

void Td::on_connection_state_changed(ConnectionState new_state) {
  if (G()->close_flag()) {
    return;
  }
  if (new_state == connection_state_) {
    LOG(ERROR) << "State manager sends update about unchanged state " << static_cast<int32>(new_state);
    return;
  }
  connection_state_ = new_state;

  send_closure(actor_id(this), &Td::send_update, get_update_connection_state_object(connection_state_));
}

void Td::start_up() {
  uint64 check_endianness = 0x0706050403020100;
  auto check_endianness_raw = reinterpret_cast<const unsigned char *>(&check_endianness);
  for (unsigned char c = 0; c < 8; c++) {
    auto symbol = check_endianness_raw[static_cast<size_t>(c)];
    LOG_IF(FATAL, symbol != c) << "TDLib requires little-endian platform";
  }

  VLOG(td_init) << "Create Global";
  old_context_ = set_context(std::make_shared<Global>());
  G()->set_net_query_stats(td_options_.net_query_stats);
  inc_request_actor_refcnt();  // guard
  inc_actor_refcnt();          // guard

  alarm_timeout_.set_callback(on_alarm_timeout_callback);
  alarm_timeout_.set_callback_data(static_cast<void *>(this));

  CHECK(state_ == State::WaitParameters);
  for (auto &update : get_fake_current_state()) {
    send_update(std::move(update));
  }
}

void Td::tear_down() {
  LOG_CHECK(close_flag_ == 5) << close_flag_;
}

void Td::hangup_shared() {
  auto token = get_link_token();
  auto type = Container<int>::type_from_id(token);

  if (type == RequestActorIdType) {
    request_actors_.erase(token);
    dec_request_actor_refcnt();
  } else if (type == ActorIdType) {
    dec_actor_refcnt();
  } else {
    LOG(FATAL) << "Unknown hangup_shared of type " << type;
  }
}

void Td::hangup() {
  LOG(INFO) << "Receive Td::hangup";
  close();
  dec_stop_cnt();
}

ActorShared<Td> Td::create_reference() {
  inc_actor_refcnt();
  return actor_shared(this, ActorIdType);
}

void Td::inc_actor_refcnt() {
  actor_refcnt_++;
}

void Td::dec_actor_refcnt() {
  actor_refcnt_--;
  if (actor_refcnt_ < 3) {
    LOG(DEBUG) << "Decrease reference count to " << actor_refcnt_;
  }
  if (actor_refcnt_ == 0) {
    if (close_flag_ == 2) {
      create_reference();
      close_flag_ = 3;
    } else if (close_flag_ == 3) {
      LOG(INFO) << "All actors were closed";
      Timer timer;
      auto reset_manager = [&timer](auto &manager, Slice name) {
        manager.reset();
        LOG(DEBUG) << name << " was cleared" << timer;
      };
      reset_manager(account_manager_, "AccountManager");
      reset_manager(animations_manager_, "AnimationsManager");
      reset_manager(attach_menu_manager_, "AttachMenuManager");
      reset_manager(audios_manager_, "AudiosManager");
      reset_manager(auth_manager_, "AuthManager");
      reset_manager(autosave_manager_, "AutosaveManager");
      reset_manager(background_manager_, "BackgroundManager");
      reset_manager(boost_manager_, "BoostManager");
      reset_manager(bot_info_manager_, "BotInfoManager");
      reset_manager(business_connection_manager_, "BusinessConnectionManager");
      reset_manager(business_manager_, "BusinessManager");
      reset_manager(callback_queries_manager_, "CallbackQueriesManager");
      reset_manager(channel_recommendation_manager_, "ChannelRecommendationManager");
      reset_manager(common_dialog_manager_, "CommonDialogManager");
      reset_manager(chat_manager_, "ChatManager");
      reset_manager(country_info_manager_, "CountryInfoManager");
      reset_manager(dialog_action_manager_, "DialogActionManager");
      reset_manager(dialog_filter_manager_, "DialogFilterManager");
      reset_manager(dialog_invite_link_manager_, "DialogInviteLinkManager");
      reset_manager(dialog_manager_, "DialogManager");
      reset_manager(dialog_participant_manager_, "DialogParticipantManager");
      reset_manager(documents_manager_, "DocumentsManager");
      reset_manager(download_manager_, "DownloadManager");
      reset_manager(file_manager_, "FileManager");
      reset_manager(file_reference_manager_, "FileReferenceManager");
      reset_manager(forum_topic_manager_, "ForumTopicManager");
      reset_manager(game_manager_, "GameManager");
      reset_manager(group_call_manager_, "GroupCallManager");
      reset_manager(inline_message_manager_, "InlineMessageManager");
      reset_manager(inline_queries_manager_, "InlineQueriesManager");
      reset_manager(link_manager_, "LinkManager");
      reset_manager(message_import_manager_, "MessageImportManager");
      reset_manager(messages_manager_, "MessagesManager");
      reset_manager(notification_manager_, "NotificationManager");
      reset_manager(notification_settings_manager_, "NotificationSettingsManager");
      reset_manager(people_nearby_manager_, "PeopleNearbyManager");
      reset_manager(phone_number_manager_, "PhoneNumberManager");
      reset_manager(poll_manager_, "PollManager");
      reset_manager(privacy_manager_, "PrivacyManager");
      reset_manager(quick_reply_manager_, "QuickReplyManager");
      reset_manager(reaction_manager_, "ReactionManager");
      reset_manager(saved_messages_manager_, "SavedMessagesManager");
      reset_manager(sponsored_message_manager_, "SponsoredMessageManager");
      reset_manager(star_manager_, "StarManager");
      reset_manager(statistics_manager_, "StatisticsManager");
      reset_manager(stickers_manager_, "StickersManager");
      reset_manager(story_manager_, "StoryManager");
      reset_manager(theme_manager_, "ThemeManager");
      reset_manager(time_zone_manager_, "TimeZoneManager");
      reset_manager(top_dialog_manager_, "TopDialogManager");
      reset_manager(transcription_manager_, "TranscriptionManager");
      reset_manager(translation_manager_, "TranslationManager");
      reset_manager(updates_manager_, "UpdatesManager");
      reset_manager(user_manager_, "UserManager");
      reset_manager(video_notes_manager_, "VideoNotesManager");
      reset_manager(videos_manager_, "VideosManager");
      reset_manager(voice_notes_manager_, "VoiceNotesManager");
      reset_manager(web_pages_manager_, "WebPagesManager");

      G()->set_option_manager(nullptr);
      option_manager_.reset();
      LOG(DEBUG) << "OptionManager was cleared" << timer;

      G()->close_all(destroy_flag_,
                     PromiseCreator::lambda([actor_id = create_reference()](Unit) mutable { actor_id.reset(); }));

      // NetQueryDispatcher will be closed automatically
      close_flag_ = 4;
    } else if (close_flag_ == 4) {
      on_closed();
    } else {
      UNREACHABLE();
    }
  }
}

void Td::on_closed() {
  close_flag_ = 5;
  send_update(
      td_api::make_object<td_api::updateAuthorizationState>(td_api::make_object<td_api::authorizationStateClosed>()));
  dec_stop_cnt();
}

void Td::dec_stop_cnt() {
  stop_cnt_--;
  if (stop_cnt_ == 0) {
    LOG(INFO) << "Stop Td";
    set_context(std::move(old_context_));
    stop();
  }
}

void Td::inc_request_actor_refcnt() {
  request_actor_refcnt_++;
}

void Td::dec_request_actor_refcnt() {
  request_actor_refcnt_--;
  LOG(DEBUG) << "Decrease request actor count to " << request_actor_refcnt_;
  if (request_actor_refcnt_ == 0) {
    clear();
    dec_actor_refcnt();  // remove guard
  }
}

void Td::clear_requests() {
  while (!pending_alarms_.empty()) {
    auto it = pending_alarms_.begin();
    auto alarm_id = it->first;
    pending_alarms_.erase(it);
    alarm_timeout_.cancel_timeout(alarm_id);
  }
  while (!request_set_.empty()) {
    uint64 id = request_set_.begin()->first;
    if (destroy_flag_) {
      send_error_impl(id, make_error(401, "Unauthorized"));
    } else {
      send_error_impl(id, make_error(500, "Request aborted"));
    }
  }
}

void Td::clear() {
  if (close_flag_ >= 2) {
    return;
  }

  LOG(INFO) << "Clear Td";
  close_flag_ = 2;

  Timer timer;
  if (!auth_manager_->is_bot()) {
    if (destroy_flag_) {
      notification_manager_->destroy_all_notifications();
    } else {
      notification_manager_->flush_all_notifications();
    }
  }

  G()->net_query_creator().stop_check();
  result_handlers_.clear();
  LOG(DEBUG) << "Handlers were cleared" << timer;
  G()->net_query_dispatcher().stop();
  LOG(DEBUG) << "NetQueryDispatcher was stopped" << timer;
  state_manager_.reset();
  LOG(DEBUG) << "StateManager was cleared" << timer;
  clear_requests();
  if (is_online_) {
    is_online_ = false;
    alarm_timeout_.cancel_timeout(ONLINE_ALARM_ID);
  }
  alarm_timeout_.cancel_timeout(PING_SERVER_ALARM_ID);
  alarm_timeout_.cancel_timeout(TERMS_OF_SERVICE_ALARM_ID);
  alarm_timeout_.cancel_timeout(PROMO_DATA_ALARM_ID);

  auto reset_actor = [&timer](ActorOwn<Actor> actor) {
    if (!actor.empty()) {
      LOG(DEBUG) << "Start clearing " << actor.get().get_name() << timer;
    }
  };

  // close all pure actors
  reset_actor(ActorOwn<Actor>(std::move(call_manager_)));
  reset_actor(ActorOwn<Actor>(std::move(cashtag_search_hints_)));
  reset_actor(ActorOwn<Actor>(std::move(config_manager_)));
  reset_actor(ActorOwn<Actor>(std::move(device_token_manager_)));
  reset_actor(ActorOwn<Actor>(std::move(hashtag_hints_)));
  reset_actor(ActorOwn<Actor>(std::move(hashtag_search_hints_)));
  reset_actor(ActorOwn<Actor>(std::move(language_pack_manager_)));
  reset_actor(ActorOwn<Actor>(std::move(net_stats_manager_)));
  reset_actor(ActorOwn<Actor>(std::move(password_manager_)));
  reset_actor(ActorOwn<Actor>(std::move(secure_manager_)));
  reset_actor(ActorOwn<Actor>(std::move(secret_chats_manager_)));
  reset_actor(ActorOwn<Actor>(std::move(storage_manager_)));

  G()->set_connection_creator(ActorOwn<ConnectionCreator>());
  LOG(DEBUG) << "ConnectionCreator was cleared" << timer;
  G()->set_temp_auth_key_watchdog(ActorOwn<TempAuthKeyWatchdog>());
  LOG(DEBUG) << "TempAuthKeyWatchdog was cleared" << timer;

  // clear actors which are unique pointers
  reset_actor(ActorOwn<Actor>(std::move(account_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(animations_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(attach_menu_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(auth_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(autosave_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(background_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(boost_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(bot_info_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(business_connection_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(business_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(channel_recommendation_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(common_dialog_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(chat_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(country_info_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(dialog_action_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(dialog_filter_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(dialog_invite_link_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(dialog_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(dialog_participant_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(download_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(file_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(file_reference_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(forum_topic_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(game_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(group_call_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(inline_message_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(inline_queries_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(link_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(message_import_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(messages_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(notification_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(notification_settings_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(people_nearby_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(phone_number_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(poll_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(privacy_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(quick_reply_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(reaction_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(saved_messages_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(sponsored_message_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(star_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(statistics_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(stickers_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(story_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(theme_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(time_zone_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(top_dialog_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(transcription_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(translation_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(updates_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(user_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(video_notes_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(voice_notes_manager_actor_)));
  reset_actor(ActorOwn<Actor>(std::move(web_pages_manager_actor_)));
  LOG(DEBUG) << "All actors were cleared" << timer;
}

void Td::close() {
  close_impl(false);
}

void Td::destroy() {
  close_impl(true);
}

void Td::close_impl(bool destroy_flag) {
  destroy_flag_ |= destroy_flag;
  if (close_flag_) {
    return;
  }

  LOG(WARNING) << (destroy_flag ? "Destroy" : "Close") << " Td in state " << static_cast<int32>(state_);
  if (state_ == State::WaitParameters) {
    state_ = State::Close;
    close_flag_ = 4;
    G()->set_close_flag();
    clear_requests();
    send_update(td_api::make_object<td_api::updateAuthorizationState>(
        td_api::make_object<td_api::authorizationStateClosing>()));

    request_actors_.clear();
    return send_closure_later(actor_id(this), &Td::dec_request_actor_refcnt);  // remove guard
  }

  state_ = State::Close;
  close_flag_ = 1;
  G()->set_close_flag();
  send_closure(auth_manager_actor_, &AuthManager::on_closing, destroy_flag);
  updates_manager_->timeout_expired();  // save PTS and QTS

  // wait till all request_actors will stop
  request_actors_.clear();
  G()->td_db()->flush_all();
  send_closure_later(actor_id(this), &Td::dec_request_actor_refcnt);  // remove guard
}

class Td::DownloadFileCallback final : public FileManager::DownloadCallback {
 public:
  void on_download_ok(FileId file_id) final {
    send_closure(G()->td(), &Td::on_file_download_finished, file_id);
  }

  void on_download_error(FileId file_id, Status error) final {
    send_closure(G()->td(), &Td::on_file_download_finished, file_id);
  }
};

class Td::UploadFileCallback final : public FileManager::UploadCallback {
 public:
  void on_upload_ok(FileId file_id, tl_object_ptr<telegram_api::InputFile> input_file) final {
    // cancel file upload of the file to allow next upload with the same file to succeed
    send_closure(G()->file_manager(), &FileManager::cancel_upload, file_id);
  }

  void on_upload_encrypted_ok(FileId file_id, tl_object_ptr<telegram_api::InputEncryptedFile> input_file) final {
    // cancel file upload of the file to allow next upload with the same file to succeed
    send_closure(G()->file_manager(), &FileManager::cancel_upload, file_id);
  }

  void on_upload_secure_ok(FileId file_id, tl_object_ptr<telegram_api::InputSecureFile> input_file) final {
    // cancel file upload of the file to allow next upload with the same file to succeed
    send_closure(G()->file_manager(), &FileManager::cancel_upload, file_id);
  }

  void on_upload_error(FileId file_id, Status error) final {
  }
};

template <class T>
void Td::complete_pending_preauthentication_requests(const T &func) {
  for (auto &request : pending_preauthentication_requests_) {
    if (request.second != nullptr && func(request.second->get_id())) {
      downcast_call(*request.second, [this, id = request.first](auto &request) { this->on_request(id, request); });
      request.second = nullptr;
    }
  }
}

void Td::finish_set_parameters() {
  CHECK(set_parameters_request_id_ != 0);
  set_parameters_request_id_ = 0;

  if (pending_set_parameters_requests_.empty()) {
    return;
  }

  VLOG(td_init) << "Continue to execute " << pending_set_parameters_requests_.size() << " pending requests";
  auto requests = std::move(pending_set_parameters_requests_);
  for (auto &request : requests) {
    run_request(request.first, std::move(request.second));
  }
  CHECK(pending_set_parameters_requests_.size() < requests.size());
}

void Td::init(Parameters parameters, Result<TdDb::OpenedDatabase> r_opened_database) {
  CHECK(set_parameters_request_id_ != 0);
  if (r_opened_database.is_error()) {
    LOG(WARNING) << "Failed to open database: " << r_opened_database.error();
    send_closure(actor_id(this), &Td::send_error, set_parameters_request_id_, r_opened_database.move_as_error());
    return finish_set_parameters();
  }
  auto events = r_opened_database.move_as_ok();

  VLOG(td_init) << "Successfully inited database";

  if (state_ == State::Close) {
    LOG(INFO) << "Close asynchronously opened database";
    auto database_ptr = events.database.get();
    auto promise = PromiseCreator::lambda([database = std::move(events.database)](Unit) {
      // destroy the database after closing
    });
    database_ptr->close(
        database_ptr->use_file_database() ? G()->get_database_scheduler_id() : G()->get_slow_net_scheduler_id(),
        destroy_flag_, std::move(promise));
    return finish_set_parameters();
  }

  G()->init(actor_id(this), std::move(events.database)).ensure();

  init_options_and_network();

  // we need to process td_api::getOption along with td_api::setOption for consistency
  // we need to process td_api::setOption before managers and MTProto header are created,
  // because their initialiation may be affected by the options
  complete_pending_preauthentication_requests([](int32 id) {
    switch (id) {
      case td_api::getOption::ID:
      case td_api::setOption::ID:
        return true;
      default:
        return false;
    }
  });

  if (!option_manager_->get_option_boolean("disable_network_statistics")) {
    net_stats_manager_ = create_actor<NetStatsManager>("NetStatsManager", create_reference());

    // How else could I let two actor know about each other, without quite complex async logic?
    auto net_stats_manager_ptr = net_stats_manager_.get_actor_unsafe();
    net_stats_manager_ptr->init();
    G()->connection_creator().get_actor_unsafe()->set_net_stats_callback(
        net_stats_manager_ptr->get_common_stats_callback(), net_stats_manager_ptr->get_media_stats_callback());
    G()->set_net_stats_file_callbacks(net_stats_manager_ptr->get_file_stats_callbacks());
  }

  complete_pending_preauthentication_requests([](int32 id) {
    switch (id) {
      case td_api::getNetworkStatistics::ID:
      case td_api::addNetworkStatistics::ID:
      case td_api::resetNetworkStatistics::ID:
        return true;
      default:
        return false;
    }
  });

  if (events.since_last_open >= 3600) {
    auto old_since_last_open = option_manager_->get_option_integer("since_last_open");
    if (events.since_last_open > old_since_last_open) {
      option_manager_->set_option_integer("since_last_open", events.since_last_open);
    }
  }

  options_.language_pack = option_manager_->get_option_string("localization_target");
  options_.language_code = option_manager_->get_option_string("language_pack_id");
  options_.parameters = option_manager_->get_option_string("connection_parameters");
  options_.tz_offset = static_cast<int32>(option_manager_->get_option_integer("utc_time_offset"));
  options_.is_emulator = option_manager_->get_option_boolean("is_emulator");
  // options_.proxy = Proxy();
  G()->set_mtproto_header(make_unique<MtprotoHeader>(options_));
  G()->set_store_all_files_in_files_directory(
      option_manager_->get_option_boolean("store_all_files_in_files_directory"));

  VLOG(td_init) << "Create NetQueryDispatcher";
  auto net_query_dispatcher = make_unique<NetQueryDispatcher>([&] { return create_reference(); });
  G()->set_net_query_dispatcher(std::move(net_query_dispatcher));

  complete_pending_preauthentication_requests([](int32 id) {
    // pingProxy uses NetQueryDispatcher to get main_dc_id, so must be called after NetQueryDispatcher is created
    return id == td_api::pingProxy::ID;
  });

  VLOG(td_init) << "Create AuthManager";
  auth_manager_ = td::make_unique<AuthManager>(parameters.api_id_, parameters.api_hash_, create_reference());
  auth_manager_actor_ = register_actor("AuthManager", auth_manager_.get());
  G()->set_auth_manager(auth_manager_actor_.get());

  init_file_manager();

  init_non_actor_managers();

  init_managers();

  init_pure_actor_managers();

  secret_chats_manager_ =
      create_actor<SecretChatsManager>("SecretChatsManager", create_reference(), parameters.use_secret_chats_);
  G()->set_secret_chats_manager(secret_chats_manager_.get());

  storage_manager_ = create_actor<StorageManager>("StorageManager", create_reference(), G()->get_gc_scheduler_id());
  G()->set_storage_manager(storage_manager_.get());

  option_manager_->on_td_inited();

  if (is_online_) {
    on_online_updated(true, true);
  }
  if (auth_manager_->is_bot()) {
    set_is_bot_online(true);
  }

  process_binlog_events(std::move(events));

  VLOG(td_init) << "Ping datacenter";
  if (!auth_manager_->is_authorized()) {
    country_info_manager_->get_current_country_code(Promise<string>());
  } else {
    updates_manager_->get_difference("init");
    schedule_get_terms_of_service(0);
    reload_promo_data();
  }

  complete_pending_preauthentication_requests([](int32 id) { return true; });

  VLOG(td_init) << "Finish initialization";

  state_ = State::Run;

  send_closure(actor_id(this), &Td::send_result, set_parameters_request_id_, td_api::make_object<td_api::ok>());
  return finish_set_parameters();
}

void Td::process_binlog_events(TdDb::OpenedDatabase &&events) {
  VLOG(td_init) << "Send binlog events";
  for (auto &event : events.user_events) {
    user_manager_->on_binlog_user_event(std::move(event));
  }

  for (auto &event : events.channel_events) {
    chat_manager_->on_binlog_channel_event(std::move(event));
  }

  // chats may contain links to channels, so should be inited after
  for (auto &event : events.chat_events) {
    chat_manager_->on_binlog_chat_event(std::move(event));
  }

  for (auto &event : events.secret_chat_events) {
    user_manager_->on_binlog_secret_chat_event(std::move(event));
  }

  for (auto &event : events.web_page_events) {
    web_pages_manager_->on_binlog_web_page_event(std::move(event));
  }

  for (auto &event : events.save_app_log_events) {
    on_save_app_log_binlog_event(this, std::move(event));
  }

  // Send binlog events to managers
  //
  // 1. Actors must receive all binlog events before other queries.
  //
  // -- All actors have one "entry point". So there is only one way to send query to them. So all queries are ordered
  // for each Actor.
  //
  // 2. An actor must not make some decisions before all binlog events are processed.
  // For example, SecretChatActor must not send RequestKey, before it receives log event with RequestKey and understands
  // that RequestKey was already sent.
  //
  // 3. During replay of binlog some queries may be sent to other actors. They shouldn't process such events before all
  // their binlog events are processed. So actor may receive some old queries. It must be in its actual state in
  // order to handle them properly.
  //
  // -- Use send_closure_later, so actors don't even start process binlog events, before all binlog events are sent

  for (auto &event : events.to_secret_chats_manager) {
    send_closure_later(secret_chats_manager_, &SecretChatsManager::replay_binlog_event, std::move(event));
  }

  send_closure_later(account_manager_actor_, &AccountManager::on_binlog_events, std::move(events.to_account_manager));

  send_closure_later(poll_manager_actor_, &PollManager::on_binlog_events, std::move(events.to_poll_manager));

  send_closure_later(messages_manager_actor_, &MessagesManager::on_binlog_events,
                     std::move(events.to_messages_manager));

  send_closure_later(story_manager_actor_, &StoryManager::on_binlog_events, std::move(events.to_story_manager));

  send_closure_later(notification_manager_actor_, &NotificationManager::on_binlog_events,
                     std::move(events.to_notification_manager));

  send_closure_later(notification_settings_manager_actor_, &NotificationSettingsManager::on_binlog_events,
                     std::move(events.to_notification_settings_manager));

  send_closure(secret_chats_manager_, &SecretChatsManager::binlog_replay_finish);
}

void Td::init_options_and_network() {
  VLOG(td_init) << "Create StateManager";
  class StateManagerCallback final : public StateManager::Callback {
   public:
    explicit StateManagerCallback(ActorShared<Td> td) : td_(std::move(td)) {
    }
    bool on_state(ConnectionState state) final {
      send_closure(td_, &Td::on_connection_state_changed, state);
      return td_.is_alive();
    }

   private:
    ActorShared<Td> td_;
  };
  state_manager_ = create_actor<StateManager>("State manager", create_reference());
  send_closure(state_manager_, &StateManager::add_callback, make_unique<StateManagerCallback>(create_reference()));
  G()->set_state_manager(state_manager_.get());

  VLOG(td_init) << "Create OptionManager";
  option_manager_ = make_unique<OptionManager>(this);
  G()->set_option_manager(option_manager_.get());

  VLOG(td_init) << "Create ConnectionCreator";
  G()->set_connection_creator(create_actor<ConnectionCreator>("ConnectionCreator", create_reference()));

  complete_pending_preauthentication_requests([](int32 id) {
    switch (id) {
      case td_api::setNetworkType::ID:
      case td_api::addProxy::ID:
      case td_api::editProxy::ID:
      case td_api::enableProxy::ID:
      case td_api::disableProxy::ID:
      case td_api::removeProxy::ID:
      case td_api::getProxies::ID:
      case td_api::getProxyLink::ID:
        return true;
      default:
        return false;
    }
  });

  VLOG(td_init) << "Create TempAuthKeyWatchdog";
  G()->set_temp_auth_key_watchdog(create_actor<TempAuthKeyWatchdog>("TempAuthKeyWatchdog", create_reference()));

  VLOG(td_init) << "Create ConfigManager";
  config_manager_ = create_actor<ConfigManager>("ConfigManager", create_reference());
  G()->set_config_manager(config_manager_.get());
}

void Td::init_file_manager() {
  VLOG(td_init) << "Create FileManager";
  download_file_callback_ = std::make_shared<DownloadFileCallback>();
  upload_file_callback_ = std::make_shared<UploadFileCallback>();

  class FileManagerContext final : public FileManager::Context {
   public:
    explicit FileManagerContext(Td *td) : td_(td) {
    }

    bool need_notify_on_new_files() final {
      return !td_->auth_manager_->is_bot();
    }

    void on_new_file(int64 size, int64 real_size, int32 cnt) final {
      send_closure(G()->storage_manager(), &StorageManager::on_new_file, size, real_size, cnt);
    }

    void on_file_updated(FileId file_id) final {
      send_closure(G()->td(), &Td::send_update,
                   make_tl_object<td_api::updateFile>(td_->file_manager_->get_file_object(file_id)));
    }

    bool add_file_source(FileId file_id, FileSourceId file_source_id) final {
      return td_->file_reference_manager_->add_file_source(file_id, file_source_id);
    }

    bool remove_file_source(FileId file_id, FileSourceId file_source_id) final {
      return td_->file_reference_manager_->remove_file_source(file_id, file_source_id);
    }

    void on_merge_files(FileId to_file_id, FileId from_file_id) final {
      td_->file_reference_manager_->merge(to_file_id, from_file_id);
    }

    vector<FileSourceId> get_some_file_sources(FileId file_id) final {
      return td_->file_reference_manager_->get_some_file_sources(file_id);
    }

    void repair_file_reference(FileId file_id, Promise<Unit> promise) final {
      send_closure(G()->file_reference_manager(), &FileReferenceManager::repair_file_reference, file_id,
                   std::move(promise));
    }

    void reload_photo(PhotoSizeSource source, Promise<Unit> promise) final {
      FileReferenceManager::reload_photo(std::move(source), std::move(promise));
    }

    bool keep_exact_remote_location() final {
      return !td_->auth_manager_->is_bot();
    }

    ActorShared<> create_reference() final {
      return td_->create_reference();
    }

   private:
    Td *td_;
  };

  file_manager_ = make_unique<FileManager>(make_unique<FileManagerContext>(this));
  file_manager_actor_ = register_actor("FileManager", file_manager_.get());
  file_manager_->init_actor();
  G()->set_file_manager(file_manager_actor_.get());

  file_reference_manager_ = make_unique<FileReferenceManager>(create_reference());
  file_reference_manager_actor_ = register_actor("FileReferenceManager", file_reference_manager_.get());
  G()->set_file_reference_manager(file_reference_manager_actor_.get());
}

void Td::init_non_actor_managers() {
  VLOG(td_init) << "Create Managers";
  audios_manager_ = make_unique<AudiosManager>(this);
  callback_queries_manager_ = make_unique<CallbackQueriesManager>(this);
  documents_manager_ = make_unique<DocumentsManager>(this);
  videos_manager_ = make_unique<VideosManager>(this);
}

void Td::init_managers() {
  account_manager_ = make_unique<AccountManager>(this, create_reference());
  account_manager_actor_ = register_actor("AccountManager", account_manager_.get());
  G()->set_account_manager(account_manager_actor_.get());
  animations_manager_ = make_unique<AnimationsManager>(this, create_reference());
  animations_manager_actor_ = register_actor("AnimationsManager", animations_manager_.get());
  G()->set_animations_manager(animations_manager_actor_.get());
  attach_menu_manager_ = make_unique<AttachMenuManager>(this, create_reference());
  attach_menu_manager_actor_ = register_actor("AttachMenuManager", attach_menu_manager_.get());
  G()->set_attach_menu_manager(attach_menu_manager_actor_.get());
  autosave_manager_ = make_unique<AutosaveManager>(this, create_reference());
  autosave_manager_actor_ = register_actor("AutosaveManager", autosave_manager_.get());
  G()->set_autosave_manager(autosave_manager_actor_.get());
  background_manager_ = make_unique<BackgroundManager>(this, create_reference());
  background_manager_actor_ = register_actor("BackgroundManager", background_manager_.get());
  G()->set_background_manager(background_manager_actor_.get());
  boost_manager_ = make_unique<BoostManager>(this, create_reference());
  boost_manager_actor_ = register_actor("BoostManager", boost_manager_.get());
  G()->set_boost_manager(boost_manager_actor_.get());
  bot_info_manager_ = make_unique<BotInfoManager>(this, create_reference());
  bot_info_manager_actor_ = register_actor("BotInfoManager", bot_info_manager_.get());
  business_connection_manager_ = make_unique<BusinessConnectionManager>(this, create_reference());
  business_connection_manager_actor_ = register_actor("BusinessConnectionManager", business_connection_manager_.get());
  G()->set_business_connection_manager(business_connection_manager_actor_.get());
  business_manager_ = make_unique<BusinessManager>(this, create_reference());
  business_manager_actor_ = register_actor("BusinessManager", business_manager_.get());
  G()->set_business_manager(business_manager_actor_.get());
  channel_recommendation_manager_ = make_unique<ChannelRecommendationManager>(this, create_reference());
  channel_recommendation_manager_actor_ =
      register_actor("ChannelRecommendationManager", channel_recommendation_manager_.get());
  common_dialog_manager_ = make_unique<CommonDialogManager>(this, create_reference());
  common_dialog_manager_actor_ = register_actor("CommonDialogManager", common_dialog_manager_.get());
  chat_manager_ = make_unique<ChatManager>(this, create_reference());
  chat_manager_actor_ = register_actor("ChatManager", chat_manager_.get());
  G()->set_chat_manager(chat_manager_actor_.get());
  country_info_manager_ = make_unique<CountryInfoManager>(this, create_reference());
  country_info_manager_actor_ = register_actor("CountryInfoManager", country_info_manager_.get());
  dialog_action_manager_ = make_unique<DialogActionManager>(this, create_reference());
  dialog_action_manager_actor_ = register_actor("DialogActionManager", dialog_action_manager_.get());
  G()->set_dialog_action_manager(dialog_action_manager_actor_.get());
  dialog_filter_manager_ = make_unique<DialogFilterManager>(this, create_reference());
  dialog_filter_manager_actor_ = register_actor("DialogFilterManager", dialog_filter_manager_.get());
  G()->set_dialog_filter_manager(dialog_filter_manager_actor_.get());
  dialog_invite_link_manager_ = make_unique<DialogInviteLinkManager>(this, create_reference());
  dialog_invite_link_manager_actor_ = register_actor("DialogInviteLinkManager", dialog_invite_link_manager_.get());
  G()->set_dialog_invite_link_manager(dialog_invite_link_manager_actor_.get());
  dialog_manager_ = make_unique<DialogManager>(this, create_reference());
  dialog_manager_actor_ = register_actor("DialogManager", dialog_manager_.get());
  G()->set_dialog_manager(dialog_manager_actor_.get());
  dialog_participant_manager_ = make_unique<DialogParticipantManager>(this, create_reference());
  dialog_participant_manager_actor_ = register_actor("DialogParticipantManager", dialog_participant_manager_.get());
  G()->set_dialog_participant_manager(dialog_participant_manager_actor_.get());
  download_manager_ = DownloadManager::create(td::make_unique<DownloadManagerCallback>(this, create_reference()));
  download_manager_actor_ = register_actor("DownloadManager", download_manager_.get());
  G()->set_download_manager(download_manager_actor_.get());
  forum_topic_manager_ = make_unique<ForumTopicManager>(this, create_reference());
  forum_topic_manager_actor_ = register_actor("ForumTopicManager", forum_topic_manager_.get());
  G()->set_forum_topic_manager(forum_topic_manager_actor_.get());
  game_manager_ = make_unique<GameManager>(this, create_reference());
  game_manager_actor_ = register_actor("GameManager", game_manager_.get());
  G()->set_game_manager(game_manager_actor_.get());
  group_call_manager_ = make_unique<GroupCallManager>(this, create_reference());
  group_call_manager_actor_ = register_actor("GroupCallManager", group_call_manager_.get());
  G()->set_group_call_manager(group_call_manager_actor_.get());
  inline_message_manager_ = make_unique<InlineMessageManager>(this, create_reference());
  inline_message_manager_actor_ = register_actor("InlineMessageManager", inline_message_manager_.get());
  G()->set_inline_message_manager(inline_message_manager_actor_.get());
  inline_queries_manager_ = make_unique<InlineQueriesManager>(this, create_reference());
  inline_queries_manager_actor_ = register_actor("InlineQueriesManager", inline_queries_manager_.get());
  link_manager_ = make_unique<LinkManager>(this, create_reference());
  link_manager_actor_ = register_actor("LinkManager", link_manager_.get());
  G()->set_link_manager(link_manager_actor_.get());
  message_import_manager_ = make_unique<MessageImportManager>(this, create_reference());
  message_import_manager_actor_ = register_actor("MessageImportManager", message_import_manager_.get());
  G()->set_message_import_manager(message_import_manager_actor_.get());
  messages_manager_ = make_unique<MessagesManager>(this, create_reference());
  messages_manager_actor_ = register_actor("MessagesManager", messages_manager_.get());
  G()->set_messages_manager(messages_manager_actor_.get());
  notification_manager_ = make_unique<NotificationManager>(this, create_reference());
  notification_manager_actor_ = register_actor("NotificationManager", notification_manager_.get());
  G()->set_notification_manager(notification_manager_actor_.get());
  notification_settings_manager_ = make_unique<NotificationSettingsManager>(this, create_reference());
  notification_settings_manager_actor_ =
      register_actor("NotificationSettingsManager", notification_settings_manager_.get());
  G()->set_notification_settings_manager(notification_settings_manager_actor_.get());
  people_nearby_manager_ = make_unique<PeopleNearbyManager>(this, create_reference());
  people_nearby_manager_actor_ = register_actor("PeopleNearbyManager", people_nearby_manager_.get());
  G()->set_people_nearby_manager(people_nearby_manager_actor_.get());
  phone_number_manager_ = make_unique<PhoneNumberManager>(this, create_reference());
  phone_number_manager_actor_ = register_actor("PhoneNumberManager", phone_number_manager_.get());
  poll_manager_ = make_unique<PollManager>(this, create_reference());
  poll_manager_actor_ = register_actor("PollManager", poll_manager_.get());
  privacy_manager_ = make_unique<PrivacyManager>(this, create_reference());
  privacy_manager_actor_ = register_actor("PrivacyManager", privacy_manager_.get());
  quick_reply_manager_ = make_unique<QuickReplyManager>(this, create_reference());
  quick_reply_manager_actor_ = register_actor("QuickReplyManager", quick_reply_manager_.get());
  G()->set_quick_reply_manager(quick_reply_manager_actor_.get());
  reaction_manager_ = make_unique<ReactionManager>(this, create_reference());
  reaction_manager_actor_ = register_actor("ReactionManager", reaction_manager_.get());
  G()->set_reaction_manager(reaction_manager_actor_.get());
  saved_messages_manager_ = make_unique<SavedMessagesManager>(this, create_reference());
  saved_messages_manager_actor_ = register_actor("SavedMessagesManager", saved_messages_manager_.get());
  G()->set_saved_messages_manager(saved_messages_manager_actor_.get());
  sponsored_message_manager_ = make_unique<SponsoredMessageManager>(this, create_reference());
  sponsored_message_manager_actor_ = register_actor("SponsoredMessageManager", sponsored_message_manager_.get());
  G()->set_sponsored_message_manager(sponsored_message_manager_actor_.get());
  star_manager_ = make_unique<StarManager>(this, create_reference());
  star_manager_actor_ = register_actor("StarManager", star_manager_.get());
  statistics_manager_ = make_unique<StatisticsManager>(this, create_reference());
  statistics_manager_actor_ = register_actor("StatisticsManager", statistics_manager_.get());
  stickers_manager_ = make_unique<StickersManager>(this, create_reference());
  stickers_manager_actor_ = register_actor("StickersManager", stickers_manager_.get());
  G()->set_stickers_manager(stickers_manager_actor_.get());
  story_manager_ = make_unique<StoryManager>(this, create_reference());
  story_manager_actor_ = register_actor("StoryManager", story_manager_.get());
  G()->set_story_manager(story_manager_actor_.get());
  theme_manager_ = make_unique<ThemeManager>(this, create_reference());
  theme_manager_actor_ = register_actor("ThemeManager", theme_manager_.get());
  G()->set_theme_manager(theme_manager_actor_.get());
  time_zone_manager_ = make_unique<TimeZoneManager>(this, create_reference());
  time_zone_manager_actor_ = register_actor("TimeZoneManager", time_zone_manager_.get());
  G()->set_time_zone_manager(time_zone_manager_actor_.get());
  top_dialog_manager_ = make_unique<TopDialogManager>(this, create_reference());
  top_dialog_manager_actor_ = register_actor("TopDialogManager", top_dialog_manager_.get());
  G()->set_top_dialog_manager(top_dialog_manager_actor_.get());
  transcription_manager_ = make_unique<TranscriptionManager>(this, create_reference());
  transcription_manager_actor_ = register_actor("TranscriptionManager", transcription_manager_.get());
  G()->set_transcription_manager(transcription_manager_actor_.get());
  translation_manager_ = make_unique<TranslationManager>(this, create_reference());
  translation_manager_actor_ = register_actor("TranslationManager", translation_manager_.get());
  updates_manager_ = make_unique<UpdatesManager>(this, create_reference());
  updates_manager_actor_ = register_actor("UpdatesManager", updates_manager_.get());
  G()->set_updates_manager(updates_manager_actor_.get());
  user_manager_ = make_unique<UserManager>(this, create_reference());
  user_manager_actor_ = register_actor("UserManager", user_manager_.get());
  G()->set_user_manager(user_manager_actor_.get());
  video_notes_manager_ = make_unique<VideoNotesManager>(this, create_reference());
  video_notes_manager_actor_ = register_actor("VideoNotesManager", video_notes_manager_.get());
  voice_notes_manager_ = make_unique<VoiceNotesManager>(this, create_reference());
  voice_notes_manager_actor_ = register_actor("VoiceNotesManager", voice_notes_manager_.get());
  web_pages_manager_ = make_unique<WebPagesManager>(this, create_reference());
  web_pages_manager_actor_ = register_actor("WebPagesManager", web_pages_manager_.get());
  G()->set_web_pages_manager(web_pages_manager_actor_.get());
}

void Td::init_pure_actor_managers() {
  call_manager_ = create_actor<CallManager>("CallManager", create_reference());
  G()->set_call_manager(call_manager_.get());
  cashtag_search_hints_ = create_actor<HashtagHints>("CashtagSearchHints", "cashtag_search", '$', create_reference());
  device_token_manager_ = create_actor<DeviceTokenManager>("DeviceTokenManager", create_reference());
  hashtag_hints_ = create_actor<HashtagHints>("HashtagHints", "text", '#', create_reference());
  hashtag_search_hints_ = create_actor<HashtagHints>("HashtagSearchHints", "search", '#', create_reference());
  language_pack_manager_ = create_actor<LanguagePackManager>("LanguagePackManager", create_reference());
  G()->set_language_pack_manager(language_pack_manager_.get());
  password_manager_ = create_actor<PasswordManager>("PasswordManager", create_reference());
  G()->set_password_manager(password_manager_.get());
  secure_manager_ = create_actor<SecureManager>("SecureManager", create_reference());
}

void Td::send_update(tl_object_ptr<td_api::Update> &&object) {
  CHECK(object != nullptr);
  auto object_id = object->get_id();
  if (close_flag_ >= 5 && object_id != td_api::updateAuthorizationState::ID) {
    // just in case
    return;
  }

  switch (object_id) {
    case td_api::updateAccentColors::ID:
    case td_api::updateChatThemes::ID:
    case td_api::updateFavoriteStickers::ID:
    case td_api::updateInstalledStickerSets::ID:
    case td_api::updateProfileAccentColors::ID:
    case td_api::updateRecentStickers::ID:
    case td_api::updateSavedAnimations::ID:
    case td_api::updateSavedNotificationSounds::ID:
    case td_api::updateUserStatus::ID:
      VLOG(td_requests) << "Sending update: " << oneline(to_string(object));
      break;
    case td_api::updateTrendingStickerSets::ID: {
      auto update = static_cast<const td_api::updateTrendingStickerSets *>(object.get());
      auto sticker_sets = update->sticker_sets_.get();
      VLOG(td_requests) << "Sending update: updateTrendingStickerSets { " << oneline(to_string(update->sticker_type_))
                        << ", total_count = " << sticker_sets->total_count_
                        << ", count = " << sticker_sets->sets_.size() << " }";
      break;
    }
    case td_api::updateOption::ID:
      if (auth_manager_ == nullptr || !auth_manager_->is_bot()) {
        VLOG(td_requests) << "Sending update: " << to_string(object);
      }
      break;
    case td_api::updateDefaultReactionType::ID / 2:
      LOG(ERROR) << "Sending update: " << oneline(to_string(object));
      break;
    default:
      VLOG(td_requests) << "Sending update: " << to_string(object);
  }

  callback_->on_result(0, std::move(object));
}

void Td::send_result(uint64 id, tl_object_ptr<td_api::Object> object) {
  if (id == 0) {
    LOG(ERROR) << "Sending " << to_string(object) << " through send_result";
    return;
  }

  auto it = request_set_.find(id);
  if (it != request_set_.end()) {
    if (object == nullptr) {
      object = make_tl_object<td_api::error>(404, "Not Found");
    }
    VLOG(td_requests) << "Sending result for request " << id << ": " << to_string(object);
    request_set_.erase(it);
    callback_->on_result(id, std::move(object));
  }
}

void Td::send_error_impl(uint64 id, tl_object_ptr<td_api::error> error) {
  CHECK(id != 0);
  CHECK(error != nullptr);
  auto it = request_set_.find(id);
  if (it != request_set_.end()) {
    if (error->code_ == 0 && error->message_ == "Lost promise") {
      LOG(FATAL) << "Lost promise for query " << id << " of type " << it->second << " in close state " << close_flag_;
    }
    VLOG(td_requests) << "Sending error for request " << id << ": " << oneline(to_string(error));
    request_set_.erase(it);
    callback_->on_error(id, std::move(error));
  }
}

void Td::send_error(uint64 id, Status error) {
  send_error_impl(id, make_tl_object<td_api::error>(error.code(), error.message().str()));
  error.ignore();
}

void Td::send_error_raw(uint64 id, int32 code, CSlice error) {
  send_closure(actor_id(this), &Td::send_error_impl, id, make_error(code, error));
}

void Td::answer_ok_query(uint64 id, Status status) {
  if (status.is_error()) {
    send_closure(actor_id(this), &Td::send_error, id, std::move(status));
  } else {
    send_closure(actor_id(this), &Td::send_result, id, make_tl_object<td_api::ok>());
  }
}

Promise<Unit> Td::create_ok_request_promise(uint64 id) {
  return PromiseCreator::lambda([actor_id = actor_id(this), id](Result<Unit> result) {
    if (result.is_error()) {
      send_closure(actor_id, &Td::send_error, id, result.move_as_error());
    } else {
      send_closure(actor_id, &Td::send_result, id, td_api::make_object<td_api::ok>());
    }
  });
}

#define CLEAN_INPUT_STRING(field_name)                                  \
  if (!clean_input_string(field_name)) {                                \
    return send_error_raw(id, 400, "Strings must be encoded in UTF-8"); \
  }
#define CHECK_IS_BOT()                                              \
  if (!auth_manager_->is_bot()) {                                   \
    return send_error_raw(id, 400, "Only bots can use the method"); \
  }
#define CHECK_IS_USER()                                                    \
  if (auth_manager_->is_bot()) {                                           \
    return send_error_raw(id, 400, "The method is not available to bots"); \
  }

#define CREATE_NO_ARGS_REQUEST(name)                                       \
  auto slot_id = request_actors_.create(ActorOwn<>(), RequestActorIdType); \
  inc_request_actor_refcnt();                                              \
  *request_actors_.get(slot_id) = create_actor<name>(#name, actor_shared(this, slot_id), id);
#define CREATE_REQUEST(name, ...)                                          \
  auto slot_id = request_actors_.create(ActorOwn<>(), RequestActorIdType); \
  inc_request_actor_refcnt();                                              \
  *request_actors_.get(slot_id) = create_actor<name>(#name, actor_shared(this, slot_id), id, __VA_ARGS__);
#define CREATE_REQUEST_PROMISE() auto promise = create_request_promise<std::decay_t<decltype(request)>::ReturnType>(id)
#define CREATE_OK_REQUEST_PROMISE()                                                                                    \
  static_assert(std::is_same<std::decay_t<decltype(request)>::ReturnType, td_api::object_ptr<td_api::ok>>::value, ""); \
  auto promise = create_ok_request_promise(id)

Result<std::pair<Td::Parameters, TdDb::Parameters>> Td::get_parameters(
    td_api::object_ptr<td_api::setTdlibParameters> parameters) {
  VLOG(td_init) << "Begin to set TDLib parameters";
  if (!clean_input_string(parameters->api_hash_) || !clean_input_string(parameters->system_language_code_) ||
      !clean_input_string(parameters->device_model_) || !clean_input_string(parameters->system_version_) ||
      !clean_input_string(parameters->application_version_)) {
    VLOG(td_init) << "Wrong string encoding";
    return Status::Error(400, "Strings must be encoded in UTF-8");
  }

  if (parameters->api_id_ <= 0) {
    return Status::Error(400, "Valid api_id must be provided. Can be obtained at https://my.telegram.org");
  }
  if (parameters->api_hash_.empty()) {
    return Status::Error(400, "Valid api_hash must be provided. Can be obtained at https://my.telegram.org");
  }

  std::pair<Parameters, TdDb::Parameters> result;
  result.first.api_id_ = parameters->api_id_;
  result.first.api_hash_ = std::move(parameters->api_hash_);
  result.first.use_secret_chats_ = parameters->use_secret_chats_;

  result.second.encryption_key_ = as_db_key(std::move(parameters->database_encryption_key_));
  result.second.database_directory_ = std::move(parameters->database_directory_);
  result.second.files_directory_ = std::move(parameters->files_directory_);
  result.second.is_test_dc_ = parameters->use_test_dc_;
  result.second.use_file_database_ = parameters->use_file_database_;
  result.second.use_chat_info_database_ = parameters->use_chat_info_database_;
  result.second.use_message_database_ = parameters->use_message_database_;

  VLOG(td_init) << "Create MtprotoHeader::Options";
  options_.api_id = parameters->api_id_;
  options_.system_language_code = trim(parameters->system_language_code_);
  options_.device_model = trim(parameters->device_model_);
  options_.system_version = trim(parameters->system_version_);
  options_.application_version = trim(parameters->application_version_);
  if (options_.system_language_code.empty()) {
    return Status::Error(400, "System language code must be non-empty");
  }
  if (options_.device_model.empty()) {
    return Status::Error(400, "Device model must be non-empty");
  }
  if (options_.system_version.empty()) {
    options_.system_version = get_operating_system_version().str();
    VLOG(td_init) << "Set system version to " << options_.system_version;
  }
  if (options_.application_version.empty()) {
    return Status::Error(400, "Application version must be non-empty");
  }
  if (options_.api_id != 21724) {
    options_.application_version += ", TDLib ";
    auto version = OptionManager::get_option_synchronously("version");
    CHECK(version->get_id() == td_api::optionValueString::ID);
    options_.application_version += static_cast<const td_api::optionValueString *>(version.get())->value_;
  }
  options_.language_pack = string();
  options_.language_code = string();
  options_.parameters = string();
  options_.is_emulator = false;
  options_.proxy = Proxy();

  return std::move(result);
}

void Td::on_request(uint64 id, const td_api::setTdlibParameters &request) {
  send_error_raw(id, 400, "Unexpected setTdlibParameters");
}

void Td::on_request(uint64 id, td_api::setDatabaseEncryptionKey &request) {
  CREATE_OK_REQUEST_PROMISE();
  G()->td_db()->get_binlog()->change_key(as_db_key(std::move(request.new_encryption_key_)), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getAuthorizationState &request) {
  send_closure(auth_manager_actor_, &AuthManager::get_state, id);
}

void Td::on_request(uint64 id, td_api::setAuthenticationPhoneNumber &request) {
  CLEAN_INPUT_STRING(request.phone_number_);
  send_closure(auth_manager_actor_, &AuthManager::set_phone_number, id, std::move(request.phone_number_),
               std::move(request.settings_));
}

void Td::on_request(uint64 id, td_api::sendAuthenticationFirebaseSms &request) {
  CLEAN_INPUT_STRING(request.token_);
  send_closure(auth_manager_actor_, &AuthManager::set_firebase_token, id, std::move(request.token_));
}

void Td::on_request(uint64 id, td_api::reportAuthenticationCodeMissing &request) {
  CLEAN_INPUT_STRING(request.mobile_network_code_);
  send_closure(auth_manager_actor_, &AuthManager::report_missing_code, id, std::move(request.mobile_network_code_));
}

void Td::on_request(uint64 id, td_api::setAuthenticationEmailAddress &request) {
  CLEAN_INPUT_STRING(request.email_address_);
  send_closure(auth_manager_actor_, &AuthManager::set_email_address, id, std::move(request.email_address_));
}

void Td::on_request(uint64 id, td_api::resendAuthenticationCode &request) {
  send_closure(auth_manager_actor_, &AuthManager::resend_authentication_code, id, std::move(request.reason_));
}

void Td::on_request(uint64 id, td_api::checkAuthenticationEmailCode &request) {
  send_closure(auth_manager_actor_, &AuthManager::check_email_code, id, EmailVerification(std::move(request.code_)));
}

void Td::on_request(uint64 id, td_api::checkAuthenticationCode &request) {
  CLEAN_INPUT_STRING(request.code_);
  send_closure(auth_manager_actor_, &AuthManager::check_code, id, std::move(request.code_));
}

void Td::on_request(uint64 id, td_api::registerUser &request) {
  CLEAN_INPUT_STRING(request.first_name_);
  CLEAN_INPUT_STRING(request.last_name_);
  send_closure(auth_manager_actor_, &AuthManager::register_user, id, std::move(request.first_name_),
               std::move(request.last_name_), request.disable_notification_);
}

void Td::on_request(uint64 id, td_api::requestQrCodeAuthentication &request) {
  send_closure(auth_manager_actor_, &AuthManager::request_qr_code_authentication, id,
               UserId::get_user_ids(request.other_user_ids_));
}

void Td::on_request(uint64 id, const td_api::resetAuthenticationEmailAddress &request) {
  send_closure(auth_manager_actor_, &AuthManager::reset_email_address, id);
}

void Td::on_request(uint64 id, td_api::checkAuthenticationPassword &request) {
  CLEAN_INPUT_STRING(request.password_);
  send_closure(auth_manager_actor_, &AuthManager::check_password, id, std::move(request.password_));
}

void Td::on_request(uint64 id, const td_api::requestAuthenticationPasswordRecovery &request) {
  send_closure(auth_manager_actor_, &AuthManager::request_password_recovery, id);
}

void Td::on_request(uint64 id, td_api::checkAuthenticationPasswordRecoveryCode &request) {
  CLEAN_INPUT_STRING(request.recovery_code_);
  send_closure(auth_manager_actor_, &AuthManager::check_password_recovery_code, id, std::move(request.recovery_code_));
}

void Td::on_request(uint64 id, td_api::recoverAuthenticationPassword &request) {
  CLEAN_INPUT_STRING(request.recovery_code_);
  CLEAN_INPUT_STRING(request.new_password_);
  CLEAN_INPUT_STRING(request.new_hint_);
  send_closure(auth_manager_actor_, &AuthManager::recover_password, id, std::move(request.recovery_code_),
               std::move(request.new_password_), std::move(request.new_hint_));
}

void Td::on_request(uint64 id, const td_api::logOut &request) {
  // will call Td::destroy later
  send_closure(auth_manager_actor_, &AuthManager::log_out, id);
}

void Td::on_request(uint64 id, const td_api::close &request) {
  // send response before actually closing
  send_closure(actor_id(this), &Td::send_result, id, td_api::make_object<td_api::ok>());
  send_closure(actor_id(this), &Td::close);
}

void Td::on_request(uint64 id, const td_api::destroy &request) {
  // send response before actually destroying
  send_closure(actor_id(this), &Td::send_result, id, td_api::make_object<td_api::ok>());
  send_closure(actor_id(this), &Td::destroy);
}

void Td::on_request(uint64 id, td_api::checkAuthenticationBotToken &request) {
  CLEAN_INPUT_STRING(request.token_);
  send_closure(auth_manager_actor_, &AuthManager::check_bot_token, id, std::move(request.token_));
}

void Td::on_request(uint64 id, td_api::confirmQrCodeAuthentication &request) {
  CLEAN_INPUT_STRING(request.link_);
  CREATE_REQUEST_PROMISE();
  account_manager_->confirm_qr_code_authentication(request.link_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getCurrentState &request) {
  vector<td_api::object_ptr<td_api::Update>> updates;

  option_manager_->get_current_state(updates);

  auto state = auth_manager_->get_current_authorization_state_object();
  if (state != nullptr) {
    updates.push_back(td_api::make_object<td_api::updateAuthorizationState>(std::move(state)));
  }

  updates.push_back(get_update_connection_state_object(connection_state_));

  if (auth_manager_->is_authorized()) {
    user_manager_->get_current_state(updates);

    chat_manager_->get_current_state(updates);

    background_manager_->get_current_state(updates);

    animations_manager_->get_current_state(updates);

    attach_menu_manager_->get_current_state(updates);

    stickers_manager_->get_current_state(updates);

    reaction_manager_->get_current_state(updates);

    notification_settings_manager_->get_current_state(updates);

    dialog_filter_manager_->get_current_state(updates);

    messages_manager_->get_current_state(updates);

    dialog_participant_manager_->get_current_state(updates);

    notification_manager_->get_current_state(updates);

    quick_reply_manager_->get_current_state(updates);

    saved_messages_manager_->get_current_state(updates);

    story_manager_->get_current_state(updates);

    config_manager_.get_actor_unsafe()->get_current_state(updates);

    transcription_manager_->get_current_state(updates);

    autosave_manager_->get_current_state(updates);

    account_manager_->get_current_state(updates);

    business_connection_manager_->get_current_state(updates);

    // TODO updateFileGenerationStart generation_id:int64 original_path:string destination_path:string conversion:string = Update;
    // TODO updateCall call:call = Update;
    // TODO updateGroupCall call:groupCall = Update;
  }

  auto update_terms_of_service = get_update_terms_of_service_object();
  if (update_terms_of_service != nullptr) {
    updates.push_back(std::move(update_terms_of_service));
  }

  // send response synchronously to prevent "Request aborted" or other changes of the current state
  send_result(id, td_api::make_object<td_api::updates>(std::move(updates)));
}

void Td::on_request(uint64 id, td_api::getPasswordState &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  send_closure(password_manager_, &PasswordManager::get_state, std::move(promise));
}

void Td::on_request(uint64 id, td_api::setPassword &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.old_password_);
  CLEAN_INPUT_STRING(request.new_password_);
  CLEAN_INPUT_STRING(request.new_hint_);
  CLEAN_INPUT_STRING(request.new_recovery_email_address_);
  CREATE_REQUEST_PROMISE();
  send_closure(password_manager_, &PasswordManager::set_password, std::move(request.old_password_),
               std::move(request.new_password_), std::move(request.new_hint_), request.set_recovery_email_address_,
               std::move(request.new_recovery_email_address_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::setLoginEmailAddress &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.new_login_email_address_);
  CREATE_REQUEST_PROMISE();
  auto query_promise = PromiseCreator::lambda([promise = std::move(promise)](Result<SentEmailCode> result) mutable {
    if (result.is_error()) {
      promise.set_error(result.move_as_error());
    } else {
      promise.set_value(result.ok().get_email_address_authentication_code_info_object());
    }
  });
  send_closure(password_manager_, &PasswordManager::set_login_email_address,
               std::move(request.new_login_email_address_), std::move(query_promise));
}

void Td::on_request(uint64 id, const td_api::resendLoginEmailAddressCode &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  auto query_promise = PromiseCreator::lambda([promise = std::move(promise)](Result<SentEmailCode> result) mutable {
    if (result.is_error()) {
      promise.set_error(result.move_as_error());
    } else {
      promise.set_value(result.ok().get_email_address_authentication_code_info_object());
    }
  });
  send_closure(password_manager_, &PasswordManager::resend_login_email_address_code, std::move(query_promise));
}

void Td::on_request(uint64 id, td_api::checkLoginEmailAddressCode &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  send_closure(password_manager_, &PasswordManager::check_login_email_address_code,
               EmailVerification(std::move(request.code_)), std::move(promise));
}

void Td::on_request(uint64 id, td_api::setRecoveryEmailAddress &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.password_);
  CLEAN_INPUT_STRING(request.new_recovery_email_address_);
  CREATE_REQUEST_PROMISE();
  send_closure(password_manager_, &PasswordManager::set_recovery_email_address, std::move(request.password_),
               std::move(request.new_recovery_email_address_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::getRecoveryEmailAddress &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.password_);
  CREATE_REQUEST_PROMISE();
  send_closure(password_manager_, &PasswordManager::get_recovery_email_address, std::move(request.password_),
               std::move(promise));
}

void Td::on_request(uint64 id, td_api::checkRecoveryEmailAddressCode &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.code_);
  CREATE_REQUEST_PROMISE();
  send_closure(password_manager_, &PasswordManager::check_recovery_email_address_code, std::move(request.code_),
               std::move(promise));
}

void Td::on_request(uint64 id, const td_api::resendRecoveryEmailAddressCode &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  send_closure(password_manager_, &PasswordManager::resend_recovery_email_address_code, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::cancelRecoveryEmailAddressVerification &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  send_closure(password_manager_, &PasswordManager::cancel_recovery_email_address_verification, std::move(promise));
}

void Td::on_request(uint64 id, td_api::requestPasswordRecovery &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  auto query_promise = PromiseCreator::lambda([promise = std::move(promise)](Result<SentEmailCode> result) mutable {
    if (result.is_error()) {
      promise.set_error(result.move_as_error());
    } else {
      promise.set_value(result.ok().get_email_address_authentication_code_info_object());
    }
  });
  send_closure(password_manager_, &PasswordManager::request_password_recovery, std::move(query_promise));
}

void Td::on_request(uint64 id, td_api::checkPasswordRecoveryCode &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.recovery_code_);
  CREATE_OK_REQUEST_PROMISE();
  send_closure(password_manager_, &PasswordManager::check_password_recovery_code, std::move(request.recovery_code_),
               std::move(promise));
}

void Td::on_request(uint64 id, td_api::recoverPassword &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.recovery_code_);
  CLEAN_INPUT_STRING(request.new_password_);
  CLEAN_INPUT_STRING(request.new_hint_);
  CREATE_REQUEST_PROMISE();
  send_closure(password_manager_, &PasswordManager::recover_password, std::move(request.recovery_code_),
               std::move(request.new_password_), std::move(request.new_hint_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::resetPassword &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  send_closure(password_manager_, &PasswordManager::reset_password, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::cancelPasswordReset &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  send_closure(password_manager_, &PasswordManager::cancel_password_reset, std::move(promise));
}

void Td::on_request(uint64 id, td_api::getTemporaryPasswordState &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  send_closure(password_manager_, &PasswordManager::get_temp_password_state, std::move(promise));
}

void Td::on_request(uint64 id, td_api::createTemporaryPassword &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.password_);
  CREATE_REQUEST_PROMISE();
  send_closure(password_manager_, &PasswordManager::create_temp_password, std::move(request.password_),
               request.valid_for_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::processPushNotification &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.payload_);
  CREATE_OK_REQUEST_PROMISE();
  send_closure(G()->notification_manager(), &NotificationManager::process_push_notification,
               std::move(request.payload_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::registerDevice &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  send_closure(device_token_manager_, &DeviceTokenManager::register_device, std::move(request.device_token_),
               UserId::get_user_ids(request.other_user_ids_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::getUserPrivacySettingRules &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  privacy_manager_->get_privacy(std::move(request.setting_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::setUserPrivacySettingRules &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  privacy_manager_->set_privacy(std::move(request.setting_), std::move(request.rules_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getDefaultMessageAutoDeleteTime &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  auto query_promise = PromiseCreator::lambda([promise = std::move(promise)](Result<int32> result) mutable {
    if (result.is_error()) {
      promise.set_error(result.move_as_error());
    } else {
      promise.set_value(td_api::make_object<td_api::messageAutoDeleteTime>(result.ok()));
    }
  });
  account_manager_->get_default_message_ttl(std::move(query_promise));
}

void Td::on_request(uint64 id, const td_api::setDefaultMessageAutoDeleteTime &request) {
  CHECK_IS_USER();
  if (request.message_auto_delete_time_ == nullptr) {
    return send_error_raw(id, 400, "New default message auto-delete time must be non-empty");
  }
  CREATE_OK_REQUEST_PROMISE();
  account_manager_->set_default_message_ttl(request.message_auto_delete_time_->time_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getAccountTtl &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  auto query_promise = PromiseCreator::lambda([promise = std::move(promise)](Result<int32> result) mutable {
    if (result.is_error()) {
      promise.set_error(result.move_as_error());
    } else {
      promise.set_value(td_api::make_object<td_api::accountTtl>(result.ok()));
    }
  });
  account_manager_->get_account_ttl(std::move(query_promise));
}

void Td::on_request(uint64 id, const td_api::setAccountTtl &request) {
  CHECK_IS_USER();
  if (request.ttl_ == nullptr) {
    return send_error_raw(id, 400, "New account TTL must be non-empty");
  }
  CREATE_OK_REQUEST_PROMISE();
  account_manager_->set_account_ttl(request.ttl_->days_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::deleteAccount &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.reason_);
  send_closure(auth_manager_actor_, &AuthManager::delete_account, id, request.reason_, request.password_);
}

void Td::on_request(uint64 id, td_api::sendPhoneNumberCode &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.phone_number_);
  CREATE_REQUEST_PROMISE();
  phone_number_manager_->set_phone_number(std::move(request.phone_number_), std::move(request.settings_),
                                          std::move(request.type_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::sendPhoneNumberFirebaseSms &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.token_);
  CREATE_OK_REQUEST_PROMISE();
  phone_number_manager_->send_firebase_sms(std::move(request.token_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::reportPhoneNumberCodeMissing &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.mobile_network_code_);
  CREATE_OK_REQUEST_PROMISE();
  phone_number_manager_->report_missing_code(std::move(request.mobile_network_code_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::resendPhoneNumberCode &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  phone_number_manager_->resend_authentication_code(std::move(request.reason_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::checkPhoneNumberCode &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.code_);
  CREATE_OK_REQUEST_PROMISE();
  phone_number_manager_->check_code(std::move(request.code_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getUserLink &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  account_manager_->get_user_link(std::move(promise));
}

void Td::on_request(uint64 id, td_api::searchUserByToken &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.token_);
  CREATE_REQUEST_PROMISE();
  account_manager_->import_contact_token(std::move(request.token_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getActiveSessions &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  account_manager_->get_active_sessions(std::move(promise));
}

void Td::on_request(uint64 id, const td_api::terminateSession &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  account_manager_->terminate_session(request.session_id_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::terminateAllOtherSessions &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  account_manager_->terminate_all_other_sessions(std::move(promise));
}

void Td::on_request(uint64 id, const td_api::confirmSession &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  account_manager_->confirm_session(request.session_id_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::toggleSessionCanAcceptCalls &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  account_manager_->toggle_session_can_accept_calls(request.session_id_, request.can_accept_calls_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::toggleSessionCanAcceptSecretChats &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  account_manager_->toggle_session_can_accept_secret_chats(request.session_id_, request.can_accept_secret_chats_,
                                                           std::move(promise));
}

void Td::on_request(uint64 id, const td_api::setInactiveSessionTtl &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  account_manager_->set_inactive_session_ttl_days(request.inactive_session_ttl_days_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getConnectedWebsites &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  account_manager_->get_connected_websites(std::move(promise));
}

void Td::on_request(uint64 id, const td_api::disconnectWebsite &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  account_manager_->disconnect_website(request.website_id_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::disconnectAllWebsites &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  account_manager_->disconnect_all_websites(std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getMe &request) {
  CREATE_NO_ARGS_REQUEST(GetMeRequest);
}

void Td::on_request(uint64 id, const td_api::getUser &request) {
  CREATE_REQUEST(GetUserRequest, request.user_id_);
}

void Td::on_request(uint64 id, const td_api::getUserFullInfo &request) {
  CREATE_REQUEST(GetUserFullInfoRequest, request.user_id_);
}

void Td::on_request(uint64 id, const td_api::getBasicGroup &request) {
  CREATE_REQUEST(GetGroupRequest, request.basic_group_id_);
}

void Td::on_request(uint64 id, const td_api::getBasicGroupFullInfo &request) {
  CREATE_REQUEST(GetGroupFullInfoRequest, request.basic_group_id_);
}

void Td::on_request(uint64 id, const td_api::getSupergroup &request) {
  CREATE_REQUEST(GetSupergroupRequest, request.supergroup_id_);
}

void Td::on_request(uint64 id, const td_api::getSupergroupFullInfo &request) {
  CREATE_REQUEST(GetSupergroupFullInfoRequest, request.supergroup_id_);
}

void Td::on_request(uint64 id, const td_api::getSecretChat &request) {
  CREATE_REQUEST(GetSecretChatRequest, request.secret_chat_id_);
}

void Td::on_request(uint64 id, const td_api::getChat &request) {
  CREATE_REQUEST(GetChatRequest, request.chat_id_);
}

void Td::on_request(uint64 id, const td_api::getMessage &request) {
  CREATE_REQUEST(GetMessageRequest, request.chat_id_, request.message_id_);
}

void Td::on_request(uint64 id, const td_api::getMessageLocally &request) {
  MessageFullId message_full_id(DialogId(request.chat_id_), MessageId(request.message_id_));
  send_closure(actor_id(this), &Td::send_result, id,
               messages_manager_->get_message_object(message_full_id, "getMessageLocally"));
}

void Td::on_request(uint64 id, const td_api::getRepliedMessage &request) {
  CREATE_REQUEST(GetRepliedMessageRequest, request.chat_id_, request.message_id_);
}

void Td::on_request(uint64 id, const td_api::getChatPinnedMessage &request) {
  CREATE_REQUEST(GetChatPinnedMessageRequest, request.chat_id_);
}

void Td::on_request(uint64 id, const td_api::getCallbackQueryMessage &request) {
  CHECK_IS_BOT();
  CREATE_REQUEST(GetCallbackQueryMessageRequest, request.chat_id_, request.message_id_, request.callback_query_id_);
}

void Td::on_request(uint64 id, const td_api::getMessages &request) {
  CREATE_REQUEST(GetMessagesRequest, request.chat_id_, request.message_ids_);
}

void Td::on_request(uint64 id, const td_api::getChatSponsoredMessages &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  sponsored_message_manager_->get_dialog_sponsored_messages(DialogId(request.chat_id_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::clickChatSponsoredMessage &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  sponsored_message_manager_->click_sponsored_message(DialogId(request.chat_id_), MessageId(request.message_id_),
                                                      std::move(promise));
}

void Td::on_request(uint64 id, const td_api::reportChatSponsoredMessage &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  sponsored_message_manager_->report_sponsored_message(DialogId(request.chat_id_), MessageId(request.message_id_),
                                                       request.option_id_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getMessageThread &request) {
  CHECK_IS_USER();
  CREATE_REQUEST(GetMessageThreadRequest, request.chat_id_, request.message_id_);
}

void Td::on_request(uint64 id, const td_api::getMessageReadDate &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  messages_manager_->get_message_read_date({DialogId(request.chat_id_), MessageId(request.message_id_)},
                                           std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getMessageViewers &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  messages_manager_->get_message_viewers({DialogId(request.chat_id_), MessageId(request.message_id_)},
                                         std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getMessageLink &request) {
  auto r_message_link =
      messages_manager_->get_message_link({DialogId(request.chat_id_), MessageId(request.message_id_)},
                                          request.media_timestamp_, request.for_album_, request.in_message_thread_);
  if (r_message_link.is_error()) {
    send_closure(actor_id(this), &Td::send_error, id, r_message_link.move_as_error());
  } else {
    send_closure(actor_id(this), &Td::send_result, id,
                 td_api::make_object<td_api::messageLink>(r_message_link.ok().first, r_message_link.ok().second));
  }
}

void Td::on_request(uint64 id, const td_api::getMessageEmbeddingCode &request) {
  CHECK_IS_USER();
  CREATE_REQUEST(GetMessageEmbeddingCodeRequest, request.chat_id_, request.message_id_, request.for_album_);
}

void Td::on_request(uint64 id, td_api::getMessageLinkInfo &request) {
  CLEAN_INPUT_STRING(request.url_);
  CREATE_REQUEST(GetMessageLinkInfoRequest, std::move(request.url_));
}

void Td::on_request(uint64 id, td_api::translateText &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.to_language_code_);
  CREATE_REQUEST_PROMISE();
  translation_manager_->translate_text(std::move(request.text_), request.to_language_code_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::translateMessageText &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.to_language_code_);
  CREATE_REQUEST_PROMISE();
  messages_manager_->translate_message_text({DialogId(request.chat_id_), MessageId(request.message_id_)},
                                            request.to_language_code_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::recognizeSpeech &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  transcription_manager_->recognize_speech({DialogId(request.chat_id_), MessageId(request.message_id_)},
                                           std::move(promise));
}

void Td::on_request(uint64 id, const td_api::rateSpeechRecognition &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  transcription_manager_->rate_speech_recognition({DialogId(request.chat_id_), MessageId(request.message_id_)},
                                                  request.is_good_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getFile &request) {
  auto file_object = file_manager_->get_file_object(FileId(request.file_id_, 0));
  if (file_object->id_ == 0) {
    file_object = nullptr;
  } else {
    file_object->id_ = request.file_id_;
  }
  send_closure(actor_id(this), &Td::send_result, id, std::move(file_object));
}

void Td::on_request(uint64 id, td_api::getRemoteFile &request) {
  CLEAN_INPUT_STRING(request.remote_file_id_);
  auto file_type = request.file_type_ == nullptr ? FileType::Temp : get_file_type(*request.file_type_);
  auto r_file_id = file_manager_->from_persistent_id(request.remote_file_id_, file_type);
  if (r_file_id.is_error()) {
    send_closure(actor_id(this), &Td::send_error, id, r_file_id.move_as_error());
  } else {
    send_closure(actor_id(this), &Td::send_result, id, file_manager_->get_file_object(r_file_id.ok()));
  }
}

void Td::on_request(uint64 id, td_api::getStorageStatistics &request) {
  CREATE_REQUEST_PROMISE();
  auto query_promise = PromiseCreator::lambda([promise = std::move(promise)](Result<FileStats> result) mutable {
    if (result.is_error()) {
      promise.set_error(result.move_as_error());
    } else {
      promise.set_value(result.ok().get_storage_statistics_object());
    }
  });
  send_closure(storage_manager_, &StorageManager::get_storage_stats, false /*need_all_files*/, request.chat_limit_,
               std::move(query_promise));
}

void Td::on_request(uint64 id, td_api::getStorageStatisticsFast &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  auto query_promise = PromiseCreator::lambda([promise = std::move(promise)](Result<FileStatsFast> result) mutable {
    if (result.is_error()) {
      promise.set_error(result.move_as_error());
    } else {
      promise.set_value(result.ok().get_storage_statistics_fast_object());
    }
  });
  send_closure(storage_manager_, &StorageManager::get_storage_stats_fast, std::move(query_promise));
}
void Td::on_request(uint64 id, td_api::getDatabaseStatistics &request) {
  CREATE_REQUEST_PROMISE();
  auto query_promise = PromiseCreator::lambda([promise = std::move(promise)](Result<DatabaseStats> result) mutable {
    if (result.is_error()) {
      promise.set_error(result.move_as_error());
    } else {
      promise.set_value(result.ok().get_database_statistics_object());
    }
  });
  send_closure(storage_manager_, &StorageManager::get_database_stats, std::move(query_promise));
}

void Td::on_request(uint64 id, td_api::optimizeStorage &request) {
  std::vector<FileType> file_types;
  for (auto &file_type : request.file_types_) {
    if (file_type == nullptr) {
      return send_error_raw(id, 400, "File type must be non-empty");
    }

    file_types.push_back(get_file_type(*file_type));
  }
  FileGcParameters parameters(request.size_, request.ttl_, request.count_, request.immunity_delay_,
                              std::move(file_types), DialogId::get_dialog_ids(request.chat_ids_),
                              DialogId::get_dialog_ids(request.exclude_chat_ids_), request.chat_limit_);

  CREATE_REQUEST_PROMISE();
  auto query_promise = PromiseCreator::lambda([promise = std::move(promise)](Result<FileStats> result) mutable {
    if (result.is_error()) {
      promise.set_error(result.move_as_error());
    } else {
      promise.set_value(result.ok().get_storage_statistics_object());
    }
  });
  send_closure(storage_manager_, &StorageManager::run_gc, std::move(parameters),
               request.return_deleted_file_statistics_, std::move(query_promise));
}

void Td::on_request(uint64 id, td_api::getNetworkStatistics &request) {
  if (net_stats_manager_.empty()) {
    return send_error_raw(id, 400, "Network statistics are disabled");
  }
  if (!request.only_current_ && G()->get_option_boolean("disable_persistent_network_statistics")) {
    return send_error_raw(id, 400, "Persistent network statistics are disabled");
  }
  CREATE_REQUEST_PROMISE();
  auto query_promise = PromiseCreator::lambda([promise = std::move(promise)](Result<NetworkStats> result) mutable {
    if (result.is_error()) {
      promise.set_error(result.move_as_error());
    } else {
      promise.set_value(result.ok().get_network_statistics_object());
    }
  });
  send_closure(net_stats_manager_, &NetStatsManager::get_network_stats, request.only_current_,
               std::move(query_promise));
}

void Td::on_request(uint64 id, td_api::resetNetworkStatistics &request) {
  if (net_stats_manager_.empty()) {
    return send_error_raw(id, 400, "Network statistics are disabled");
  }
  CREATE_OK_REQUEST_PROMISE();
  send_closure(net_stats_manager_, &NetStatsManager::reset_network_stats);
  promise.set_value(Unit());
}

void Td::on_request(uint64 id, td_api::addNetworkStatistics &request) {
  if (request.entry_ == nullptr) {
    return send_error_raw(id, 400, "Network statistics entry must be non-empty");
  }
  if (net_stats_manager_.empty()) {
    return send_error_raw(id, 400, "Network statistics are disabled");
  }

  NetworkStatsEntry entry;
  switch (request.entry_->get_id()) {
    case td_api::networkStatisticsEntryFile::ID: {
      auto file_entry = move_tl_object_as<td_api::networkStatisticsEntryFile>(request.entry_);
      entry.is_call = false;
      if (file_entry->file_type_ != nullptr) {
        entry.file_type = get_file_type(*file_entry->file_type_);
      }
      entry.net_type = get_net_type(file_entry->network_type_);
      entry.rx = file_entry->received_bytes_;
      entry.tx = file_entry->sent_bytes_;
      break;
    }
    case td_api::networkStatisticsEntryCall::ID: {
      auto call_entry = move_tl_object_as<td_api::networkStatisticsEntryCall>(request.entry_);
      entry.is_call = true;
      entry.net_type = get_net_type(call_entry->network_type_);
      entry.rx = call_entry->received_bytes_;
      entry.tx = call_entry->sent_bytes_;
      entry.duration = call_entry->duration_;
      break;
    }
    default:
      UNREACHABLE();
  }

  if (entry.net_type == NetType::None) {
    return send_error_raw(id, 400, "Network statistics entry can't be increased for NetworkTypeNone");
  }
  if (entry.rx > (static_cast<int64>(1) << 40) || entry.rx < 0) {
    return send_error_raw(id, 400, "Wrong received bytes value");
  }
  if (entry.tx > (static_cast<int64>(1) << 40) || entry.tx < 0) {
    return send_error_raw(id, 400, "Wrong sent bytes value");
  }
  if (entry.count > (1 << 30) || entry.count < 0) {
    return send_error_raw(id, 400, "Wrong count value");
  }
  if (entry.duration > (1 << 30) || entry.duration < 0) {
    return send_error_raw(id, 400, "Wrong duration value");
  }

  send_closure(net_stats_manager_, &NetStatsManager::add_network_stats, entry);
  send_closure(actor_id(this), &Td::send_result, id, make_tl_object<td_api::ok>());
}

void Td::on_request(uint64 id, const td_api::setNetworkType &request) {
  CREATE_OK_REQUEST_PROMISE();
  send_closure(state_manager_, &StateManager::on_network, get_net_type(request.type_));
  promise.set_value(Unit());
}

void Td::on_request(uint64 id, const td_api::getAutoDownloadSettingsPresets &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  get_auto_download_settings_presets(this, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::setAutoDownloadSettings &request) {
  CHECK_IS_USER();
  if (request.settings_ == nullptr) {
    return send_error_raw(id, 400, "New settings must be non-empty");
  }
  CREATE_OK_REQUEST_PROMISE();
  set_auto_download_settings(this, get_net_type(request.type_), get_auto_download_settings(request.settings_),
                             std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getAutosaveSettings &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  autosave_manager_->get_autosave_settings(std::move(promise));
}

void Td::on_request(uint64 id, td_api::setAutosaveSettings &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  autosave_manager_->set_autosave_settings(std::move(request.scope_), std::move(request.settings_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::clearAutosaveSettingsExceptions &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  autosave_manager_->clear_autosave_settings_exceptions(std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getRecommendedChats &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  channel_recommendation_manager_->get_recommended_channels(std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getChatSimilarChats &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  channel_recommendation_manager_->get_channel_recommendations(DialogId(request.chat_id_), false, std::move(promise),
                                                               Auto());
}

void Td::on_request(uint64 id, const td_api::getChatSimilarChatCount &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  channel_recommendation_manager_->get_channel_recommendations(DialogId(request.chat_id_), request.return_local_,
                                                               Auto(), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::openChatSimilarChat &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  channel_recommendation_manager_->open_channel_recommended_channel(
      DialogId(request.chat_id_), DialogId(request.opened_chat_id_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getTopChats &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  send_closure(top_dialog_manager_actor_, &TopDialogManager::get_top_dialogs,
               get_top_dialog_category(request.category_), request.limit_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::removeTopChat &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  send_closure(top_dialog_manager_actor_, &TopDialogManager::remove_dialog, get_top_dialog_category(request.category_),
               DialogId(request.chat_id_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::loadChats &request) {
  CHECK_IS_USER();

  DialogListId dialog_list_id(request.chat_list_);
  auto r_offset = messages_manager_->get_dialog_list_last_date(dialog_list_id);
  if (r_offset.is_error()) {
    return send_error_raw(id, 400, r_offset.error().message());
  }
  auto offset = r_offset.move_as_ok();
  if (offset == MAX_DIALOG_DATE) {
    return send_closure(actor_id(this), &Td::send_result, id, nullptr);
  }

  CREATE_REQUEST(LoadChatsRequest, dialog_list_id, offset, request.limit_);
}

void Td::on_request(uint64 id, const td_api::getChats &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  messages_manager_->get_dialogs_from_list(DialogListId(request.chat_list_), request.limit_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::loadSavedMessagesTopics &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  saved_messages_manager_->load_saved_messages_topics(request.limit_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getSavedMessagesTopicHistory &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  saved_messages_manager_->get_saved_messages_topic_history(
      saved_messages_manager_->get_topic_id(request.saved_messages_topic_id_), MessageId(request.from_message_id_),
      request.offset_, request.limit_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getSavedMessagesTopicMessageByDate &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  saved_messages_manager_->get_saved_messages_topic_message_by_date(
      saved_messages_manager_->get_topic_id(request.saved_messages_topic_id_), request.date_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::deleteSavedMessagesTopicHistory &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  saved_messages_manager_->delete_saved_messages_topic_history(
      saved_messages_manager_->get_topic_id(request.saved_messages_topic_id_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::deleteSavedMessagesTopicMessagesByDate &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  saved_messages_manager_->delete_saved_messages_topic_messages_by_date(
      saved_messages_manager_->get_topic_id(request.saved_messages_topic_id_), request.min_date_, request.max_date_,
      std::move(promise));
}

void Td::on_request(uint64 id, const td_api::toggleSavedMessagesTopicIsPinned &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  saved_messages_manager_->toggle_saved_messages_topic_is_pinned(
      saved_messages_manager_->get_topic_id(request.saved_messages_topic_id_), request.is_pinned_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::setPinnedSavedMessagesTopics &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  saved_messages_manager_->set_pinned_saved_messages_topics(
      saved_messages_manager_->get_topic_ids(request.saved_messages_topic_ids_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::searchPublicChat &request) {
  CLEAN_INPUT_STRING(request.username_);
  CREATE_REQUEST(SearchPublicChatRequest, request.username_);
}

void Td::on_request(uint64 id, td_api::searchPublicChats &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.query_);
  CREATE_REQUEST(SearchPublicChatsRequest, request.query_);
}

void Td::on_request(uint64 id, td_api::searchChats &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.query_);
  CREATE_REQUEST(SearchChatsRequest, request.query_, request.limit_);
}

void Td::on_request(uint64 id, td_api::searchChatsOnServer &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.query_);
  CREATE_REQUEST(SearchChatsOnServerRequest, request.query_, request.limit_);
}

void Td::on_request(uint64 id, const td_api::searchChatsNearby &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  people_nearby_manager_->search_dialogs_nearby(Location(request.location_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getGroupsInCommon &request) {
  CHECK_IS_USER();
  CREATE_REQUEST(GetGroupsInCommonRequest, request.user_id_, request.offset_chat_id_, request.limit_);
}

void Td::on_request(uint64 id, td_api::checkChatUsername &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.username_);
  CREATE_REQUEST_PROMISE();
  auto query_promise = PromiseCreator::lambda(
      [promise = std::move(promise)](Result<DialogManager::CheckDialogUsernameResult> result) mutable {
        if (result.is_error()) {
          promise.set_error(result.move_as_error());
        } else {
          promise.set_value(DialogManager::get_check_chat_username_result_object(result.ok()));
        }
      });
  dialog_manager_->check_dialog_username(DialogId(request.chat_id_), request.username_, std::move(query_promise));
}

void Td::on_request(uint64 id, const td_api::getCreatedPublicChats &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  chat_manager_->get_created_public_dialogs(get_public_dialog_type(request.type_), std::move(promise), false);
}

void Td::on_request(uint64 id, const td_api::checkCreatedPublicChatsLimit &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  chat_manager_->check_created_public_dialogs_limit(get_public_dialog_type(request.type_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getSuitableDiscussionChats &request) {
  CHECK_IS_USER();
  CREATE_NO_ARGS_REQUEST(GetSuitableDiscussionChatsRequest);
}

void Td::on_request(uint64 id, const td_api::getInactiveSupergroupChats &request) {
  CHECK_IS_USER();
  CREATE_NO_ARGS_REQUEST(GetInactiveSupergroupChatsRequest);
}

void Td::on_request(uint64 id, const td_api::getSuitablePersonalChats &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  chat_manager_->get_created_public_dialogs(PublicDialogType::ForPersonalDialog, std::move(promise), false);
}

void Td::on_request(uint64 id, td_api::searchRecentlyFoundChats &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.query_);
  CREATE_REQUEST(SearchRecentlyFoundChatsRequest, request.query_, request.limit_);
}

void Td::on_request(uint64 id, const td_api::addRecentlyFoundChat &request) {
  CHECK_IS_USER();
  answer_ok_query(id, messages_manager_->add_recently_found_dialog(DialogId(request.chat_id_)));
}

void Td::on_request(uint64 id, const td_api::removeRecentlyFoundChat &request) {
  CHECK_IS_USER();
  answer_ok_query(id, messages_manager_->remove_recently_found_dialog(DialogId(request.chat_id_)));
}

void Td::on_request(uint64 id, const td_api::clearRecentlyFoundChats &request) {
  CHECK_IS_USER();
  messages_manager_->clear_recently_found_dialogs();
  send_closure(actor_id(this), &Td::send_result, id, make_tl_object<td_api::ok>());
}

void Td::on_request(uint64 id, const td_api::getRecentlyOpenedChats &request) {
  CHECK_IS_USER();
  CREATE_REQUEST(GetRecentlyOpenedChatsRequest, request.limit_);
}

void Td::on_request(uint64 id, const td_api::openChat &request) {
  CHECK_IS_USER();
  answer_ok_query(id, messages_manager_->open_dialog(DialogId(request.chat_id_)));
}

void Td::on_request(uint64 id, const td_api::closeChat &request) {
  CHECK_IS_USER();
  answer_ok_query(id, messages_manager_->close_dialog(DialogId(request.chat_id_)));
}

void Td::on_request(uint64 id, const td_api::viewMessages &request) {
  CHECK_IS_USER();
  answer_ok_query(
      id, messages_manager_->view_messages(DialogId(request.chat_id_), MessageId::get_message_ids(request.message_ids_),
                                           get_message_source(request.source_), request.force_read_));
}

void Td::on_request(uint64 id, const td_api::openMessageContent &request) {
  CHECK_IS_USER();
  answer_ok_query(
      id, messages_manager_->open_message_content({DialogId(request.chat_id_), MessageId(request.message_id_)}));
}

void Td::on_request(uint64 id, const td_api::clickAnimatedEmojiMessage &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  messages_manager_->click_animated_emoji_message({DialogId(request.chat_id_), MessageId(request.message_id_)},
                                                  std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getInternalLink &request) {
  auto r_link = LinkManager::get_internal_link(request.type_, !request.is_http_);
  if (r_link.is_error()) {
    send_closure(actor_id(this), &Td::send_error, id, r_link.move_as_error());
  } else {
    send_closure(actor_id(this), &Td::send_result, id, td_api::make_object<td_api::httpUrl>(r_link.move_as_ok()));
  }
}

void Td::on_request(uint64 id, const td_api::getInternalLinkType &request) {
  auto type = LinkManager::parse_internal_link(request.link_);
  send_closure(actor_id(this), &Td::send_result, id, type == nullptr ? nullptr : type->get_internal_link_type_object());
}

void Td::on_request(uint64 id, td_api::getExternalLinkInfo &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.link_);
  CREATE_REQUEST_PROMISE();
  link_manager_->get_external_link_info(std::move(request.link_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::getExternalLink &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.link_);
  CREATE_REQUEST_PROMISE();
  link_manager_->get_link_login_url(request.link_, request.allow_write_access_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getChatHistory &request) {
  CHECK_IS_USER();
  CREATE_REQUEST(GetChatHistoryRequest, request.chat_id_, request.from_message_id_, request.offset_, request.limit_,
                 request.only_local_);
}

void Td::on_request(uint64 id, const td_api::deleteChatHistory &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->delete_dialog_history(DialogId(request.chat_id_), request.remove_from_chat_list_, request.revoke_,
                                           std::move(promise));
}

void Td::on_request(uint64 id, const td_api::deleteChat &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  DialogId dialog_id(request.chat_id_);
  auto query_promise = [actor_id = messages_manager_actor_.get(), dialog_id,
                        promise = std::move(promise)](Result<Unit> &&result) mutable {
    if (result.is_error()) {
      promise.set_error(result.move_as_error());
    } else {
      send_closure(actor_id, &MessagesManager::on_dialog_deleted, dialog_id, std::move(promise));
    }
  };
  dialog_manager_->delete_dialog(dialog_id, std::move(query_promise));
}

void Td::on_request(uint64 id, const td_api::getMessageThreadHistory &request) {
  CHECK_IS_USER();
  CREATE_REQUEST(GetMessageThreadHistoryRequest, request.chat_id_, request.message_id_, request.from_message_id_,
                 request.offset_, request.limit_);
}

void Td::on_request(uint64 id, const td_api::getChatMessageCalendar &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  messages_manager_->get_dialog_message_calendar(
      DialogId(request.chat_id_), saved_messages_manager_->get_topic_id(request.saved_messages_topic_id_),
      MessageId(request.from_message_id_), get_message_search_filter(request.filter_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::searchChatMessages &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.query_);
  CREATE_REQUEST(SearchChatMessagesRequest, request.chat_id_, std::move(request.query_), std::move(request.sender_id_),
                 request.from_message_id_, request.offset_, request.limit_, std::move(request.filter_),
                 request.message_thread_id_, saved_messages_manager_->get_topic_id(request.saved_messages_topic_id_),
                 ReactionType());
}

void Td::on_request(uint64 id, td_api::searchSecretMessages &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.query_);
  CLEAN_INPUT_STRING(request.offset_);
  CREATE_REQUEST_PROMISE();
  messages_manager_->offline_search_messages(DialogId(request.chat_id_), std::move(request.query_),
                                             std::move(request.offset_), request.limit_,
                                             get_message_search_filter(request.filter_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::searchMessages &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.query_);
  CLEAN_INPUT_STRING(request.offset_);
  CREATE_REQUEST_PROMISE();
  messages_manager_->search_messages(DialogListId(request.chat_list_), request.chat_list_ == nullptr,
                                     request.only_in_channels_, std::move(request.query_), std::move(request.offset_),
                                     request.limit_, get_message_search_filter(request.filter_), request.min_date_,
                                     request.max_date_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::searchSavedMessages &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.query_);
  CREATE_REQUEST(SearchChatMessagesRequest, dialog_manager_->get_my_dialog_id().get(), std::move(request.query_),
                 nullptr, request.from_message_id_, request.offset_, request.limit_, nullptr, 0,
                 saved_messages_manager_->get_topic_id(request.saved_messages_topic_id_), ReactionType(request.tag_));
}

void Td::on_request(uint64 id, const td_api::searchCallMessages &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  messages_manager_->search_call_messages(request.offset_, request.limit_, request.only_missed_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::searchOutgoingDocumentMessages &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.query_);
  CREATE_REQUEST_PROMISE();
  messages_manager_->search_outgoing_document_messages(request.query_, request.limit_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::searchPublicMessagesByTag &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.tag_);
  CLEAN_INPUT_STRING(request.offset_);
  CREATE_REQUEST_PROMISE();
  messages_manager_->search_hashtag_posts(std::move(request.tag_), std::move(request.offset_), request.limit_,
                                          std::move(promise));
}

void Td::on_request(uint64 id, td_api::searchPublicStoriesByTag &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.tag_);
  CLEAN_INPUT_STRING(request.offset_);
  CREATE_REQUEST_PROMISE();
  story_manager_->search_hashtag_posts(std::move(request.tag_), std::move(request.offset_), request.limit_,
                                       std::move(promise));
}

void Td::on_request(uint64 id, td_api::searchPublicStoriesByLocation &request) {
  CHECK_IS_USER();
  if (request.address_ == nullptr) {
    return send_error_raw(id, 400, "Address must be non-empty");
  }
  CLEAN_INPUT_STRING(request.address_->country_code_);
  CLEAN_INPUT_STRING(request.address_->state_);
  CLEAN_INPUT_STRING(request.address_->city_);
  CLEAN_INPUT_STRING(request.address_->street_);
  CLEAN_INPUT_STRING(request.offset_);
  CREATE_REQUEST_PROMISE();
  story_manager_->search_location_posts(std::move(request.address_), std::move(request.offset_), request.limit_,
                                        std::move(promise));
}

void Td::on_request(uint64 id, td_api::searchPublicStoriesByVenue &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.venue_provider_);
  CLEAN_INPUT_STRING(request.venue_id_);
  CLEAN_INPUT_STRING(request.offset_);
  CREATE_REQUEST_PROMISE();
  story_manager_->search_venue_posts(std::move(request.venue_provider_), std::move(request.venue_id_),
                                     std::move(request.offset_), request.limit_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::getSearchedForTags &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.tag_prefix_);
  CREATE_REQUEST_PROMISE();
  auto query_promise = PromiseCreator::lambda([promise = std::move(promise)](Result<vector<string>> result) mutable {
    if (result.is_error()) {
      promise.set_error(result.move_as_error());
    } else {
      promise.set_value(td_api::make_object<td_api::hashtags>(result.move_as_ok()));
    }
  });
  send_closure(request.tag_prefix_[0] == '$' ? cashtag_search_hints_ : hashtag_search_hints_, &HashtagHints::query,
               std::move(request.tag_prefix_), request.limit_, std::move(query_promise));
}

void Td::on_request(uint64 id, td_api::removeSearchedForTag &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.tag_);
  CREATE_OK_REQUEST_PROMISE();
  send_closure(request.tag_[0] == '$' ? cashtag_search_hints_ : hashtag_search_hints_, &HashtagHints::remove_hashtag,
               std::move(request.tag_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::clearSearchedForTags &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  send_closure(request.clear_cashtags_ ? cashtag_search_hints_ : hashtag_search_hints_, &HashtagHints::clear,
               std::move(promise));
}

void Td::on_request(uint64 id, const td_api::deleteAllCallMessages &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->delete_all_call_messages(request.revoke_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::searchChatRecentLocationMessages &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  messages_manager_->search_dialog_recent_location_messages(DialogId(request.chat_id_), request.limit_,
                                                            std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getActiveLiveLocationMessages &request) {
  CHECK_IS_USER();
  CREATE_NO_ARGS_REQUEST(GetActiveLiveLocationMessagesRequest);
}

void Td::on_request(uint64 id, const td_api::getChatMessageByDate &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  messages_manager_->get_dialog_message_by_date(DialogId(request.chat_id_), request.date_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getChatSparseMessagePositions &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  messages_manager_->get_dialog_sparse_message_positions(
      DialogId(request.chat_id_), saved_messages_manager_->get_topic_id(request.saved_messages_topic_id_),
      get_message_search_filter(request.filter_), MessageId(request.from_message_id_), request.limit_,
      std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getChatMessageCount &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  auto query_promise = PromiseCreator::lambda([promise = std::move(promise)](Result<int32> result) mutable {
    if (result.is_error()) {
      promise.set_error(result.move_as_error());
    } else {
      promise.set_value(make_tl_object<td_api::count>(result.move_as_ok()));
    }
  });
  messages_manager_->get_dialog_message_count(
      DialogId(request.chat_id_), saved_messages_manager_->get_topic_id(request.saved_messages_topic_id_),
      get_message_search_filter(request.filter_), request.return_local_, std::move(query_promise));
}

void Td::on_request(uint64 id, const td_api::getChatMessagePosition &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  auto query_promise = PromiseCreator::lambda([promise = std::move(promise)](Result<int32> result) mutable {
    if (result.is_error()) {
      promise.set_error(result.move_as_error());
    } else {
      promise.set_value(make_tl_object<td_api::count>(result.move_as_ok()));
    }
  });
  messages_manager_->get_dialog_message_position(
      {DialogId(request.chat_id_), MessageId(request.message_id_)}, get_message_search_filter(request.filter_),
      MessageId(request.message_thread_id_), saved_messages_manager_->get_topic_id(request.saved_messages_topic_id_),
      std::move(query_promise));
}

void Td::on_request(uint64 id, const td_api::getChatScheduledMessages &request) {
  CHECK_IS_USER();
  CREATE_REQUEST(GetChatScheduledMessagesRequest, request.chat_id_);
}

void Td::on_request(uint64 id, const td_api::getEmojiReaction &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  reaction_manager_->get_emoji_reaction(request.emoji_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getCustomEmojiReactionAnimations &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  stickers_manager_->get_custom_emoji_reaction_generic_animations(false, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getMessageAvailableReactions &request) {
  CHECK_IS_USER();
  auto r_reactions = messages_manager_->get_message_available_reactions(
      {DialogId(request.chat_id_), MessageId(request.message_id_)}, request.row_size_);
  if (r_reactions.is_error()) {
    send_closure(actor_id(this), &Td::send_error, id, r_reactions.move_as_error());
  } else {
    send_closure(actor_id(this), &Td::send_result, id, r_reactions.move_as_ok());
  }
}

void Td::on_request(uint64 id, const td_api::clearRecentReactions &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  reaction_manager_->clear_recent_reactions(std::move(promise));
}

void Td::on_request(uint64 id, const td_api::addMessageReaction &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->add_message_reaction({DialogId(request.chat_id_), MessageId(request.message_id_)},
                                          ReactionType(request.reaction_type_), request.is_big_,
                                          request.update_recent_reactions_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::removeMessageReaction &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->remove_message_reaction({DialogId(request.chat_id_), MessageId(request.message_id_)},
                                             ReactionType(request.reaction_type_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::setMessageReactions &request) {
  CHECK_IS_BOT();
  CREATE_OK_REQUEST_PROMISE();
  set_message_reactions(this, {DialogId(request.chat_id_), MessageId(request.message_id_)},
                        ReactionType::get_reaction_types(request.reaction_types_), request.is_big_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::getMessageAddedReactions &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.offset_);
  CREATE_REQUEST_PROMISE();
  get_message_added_reactions(this, {DialogId(request.chat_id_), MessageId(request.message_id_)},
                              ReactionType(request.reaction_type_), std::move(request.offset_), request.limit_,
                              std::move(promise));
}

void Td::on_request(uint64 id, const td_api::setDefaultReactionType &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  reaction_manager_->set_default_reaction(ReactionType(request.reaction_type_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getSavedMessagesTags &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  reaction_manager_->get_saved_messages_tags(saved_messages_manager_->get_topic_id(request.saved_messages_topic_id_),
                                             std::move(promise));
}

void Td::on_request(uint64 id, td_api::setSavedMessagesTagLabel &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.label_);
  CREATE_OK_REQUEST_PROMISE();
  reaction_manager_->set_saved_messages_tag_title(ReactionType(request.tag_), std::move(request.label_),
                                                  std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getMessageEffect &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  reaction_manager_->get_message_effect(MessageEffectId(request.effect_id_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::getMessagePublicForwards &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.offset_);
  CREATE_REQUEST_PROMISE();
  statistics_manager_->get_message_public_forwards({DialogId(request.chat_id_), MessageId(request.message_id_)},
                                                   std::move(request.offset_), request.limit_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::getStoryPublicForwards &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.offset_);
  CREATE_REQUEST_PROMISE();
  statistics_manager_->get_story_public_forwards({DialogId(request.story_sender_chat_id_), StoryId(request.story_id_)},
                                                 std::move(request.offset_), request.limit_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::removeNotification &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  send_closure(notification_manager_actor_, &NotificationManager::remove_notification,
               NotificationGroupId(request.notification_group_id_), NotificationId(request.notification_id_), false,
               true, std::move(promise), "td_api::removeNotification");
}

void Td::on_request(uint64 id, const td_api::removeNotificationGroup &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  send_closure(notification_manager_actor_, &NotificationManager::remove_notification_group,
               NotificationGroupId(request.notification_group_id_), NotificationId(request.max_notification_id_),
               NotificationObjectId(), -1, true, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::deleteMessages &request) {
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->delete_messages(DialogId(request.chat_id_), MessageId::get_message_ids(request.message_ids_),
                                     request.revoke_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::deleteChatMessagesBySender &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  TRY_RESULT_PROMISE(promise, sender_dialog_id, get_message_sender_dialog_id(this, request.sender_id_, false, false));
  messages_manager_->delete_dialog_messages_by_sender(DialogId(request.chat_id_), sender_dialog_id, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::deleteChatMessagesByDate &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->delete_dialog_messages_by_date(DialogId(request.chat_id_), request.min_date_, request.max_date_,
                                                    request.revoke_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::readAllChatMentions &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->read_all_dialog_mentions(DialogId(request.chat_id_), MessageId(), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::readAllMessageThreadMentions &request) {
  CHECK_IS_USER();
  if (request.message_thread_id_ == 0) {
    return send_error_raw(id, 400, "Invalid message thread identifier specified");
  }
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->read_all_dialog_mentions(DialogId(request.chat_id_), MessageId(request.message_thread_id_),
                                              std::move(promise));
}

void Td::on_request(uint64 id, const td_api::readAllChatReactions &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->read_all_dialog_reactions(DialogId(request.chat_id_), MessageId(), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::readAllMessageThreadReactions &request) {
  CHECK_IS_USER();
  if (request.message_thread_id_ == 0) {
    return send_error_raw(id, 400, "Invalid message thread identifier specified");
  }
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->read_all_dialog_reactions(DialogId(request.chat_id_), MessageId(request.message_thread_id_),
                                               std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getChatAvailableMessageSenders &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  messages_manager_->get_dialog_send_message_as_dialog_ids(DialogId(request.chat_id_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::setChatMessageSender &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  TRY_RESULT_PROMISE(promise, message_sender_dialog_id,
                     get_message_sender_dialog_id(this, request.message_sender_id_, true, false));
  messages_manager_->set_dialog_default_send_message_as_dialog_id(DialogId(request.chat_id_), message_sender_dialog_id,
                                                                  std::move(promise));
}

void Td::on_request(uint64 id, td_api::sendMessage &request) {
  auto r_sent_message = messages_manager_->send_message(
      DialogId(request.chat_id_), MessageId(request.message_thread_id_), std::move(request.reply_to_),
      std::move(request.options_), std::move(request.reply_markup_), std::move(request.input_message_content_));
  if (r_sent_message.is_error()) {
    send_closure(actor_id(this), &Td::send_error, id, r_sent_message.move_as_error());
  } else {
    send_closure(actor_id(this), &Td::send_result, id, r_sent_message.move_as_ok());
  }
}

void Td::on_request(uint64 id, td_api::sendMessageAlbum &request) {
  auto r_messages = messages_manager_->send_message_group(
      DialogId(request.chat_id_), MessageId(request.message_thread_id_), std::move(request.reply_to_),
      std::move(request.options_), std::move(request.input_message_contents_));
  if (r_messages.is_error()) {
    send_closure(actor_id(this), &Td::send_error, id, r_messages.move_as_error());
  } else {
    send_closure(actor_id(this), &Td::send_result, id, r_messages.move_as_ok());
  }
}

void Td::on_request(uint64 id, td_api::sendBotStartMessage &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.parameter_);

  DialogId dialog_id(request.chat_id_);
  auto r_new_message_id =
      messages_manager_->send_bot_start_message(UserId(request.bot_user_id_), dialog_id, request.parameter_);
  if (r_new_message_id.is_error()) {
    return send_closure(actor_id(this), &Td::send_error, id, r_new_message_id.move_as_error());
  }

  CHECK(r_new_message_id.ok().is_valid() || r_new_message_id.ok().is_valid_scheduled());
  send_closure(actor_id(this), &Td::send_result, id,
               messages_manager_->get_message_object({dialog_id, r_new_message_id.ok()}, "sendBotStartMessage"));
}

void Td::on_request(uint64 id, td_api::sendInlineQueryResultMessage &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.result_id_);

  auto r_sent_message = messages_manager_->send_inline_query_result_message(
      DialogId(request.chat_id_), MessageId(request.message_thread_id_), std::move(request.reply_to_),
      std::move(request.options_), request.query_id_, request.result_id_, request.hide_via_bot_);
  if (r_sent_message.is_error()) {
    send_closure(actor_id(this), &Td::send_error, id, r_sent_message.move_as_error());
  } else {
    send_closure(actor_id(this), &Td::send_result, id, r_sent_message.move_as_ok());
  }
}

void Td::on_request(uint64 id, td_api::addLocalMessage &request) {
  CHECK_IS_USER();

  DialogId dialog_id(request.chat_id_);
  auto r_new_message_id =
      messages_manager_->add_local_message(dialog_id, std::move(request.sender_id_), std::move(request.reply_to_),
                                           request.disable_notification_, std::move(request.input_message_content_));
  if (r_new_message_id.is_error()) {
    return send_closure(actor_id(this), &Td::send_error, id, r_new_message_id.move_as_error());
  }

  CHECK(r_new_message_id.ok().is_valid());
  send_closure(actor_id(this), &Td::send_result, id,
               messages_manager_->get_message_object({dialog_id, r_new_message_id.ok()}, "addLocalMessage"));
}

void Td::on_request(uint64 id, td_api::editMessageText &request) {
  CREATE_REQUEST(EditMessageTextRequest, request.chat_id_, request.message_id_, std::move(request.reply_markup_),
                 std::move(request.input_message_content_));
}

void Td::on_request(uint64 id, td_api::editMessageLiveLocation &request) {
  CREATE_REQUEST(EditMessageLiveLocationRequest, request.chat_id_, request.message_id_,
                 std::move(request.reply_markup_), std::move(request.location_), request.live_period_, request.heading_,
                 request.proximity_alert_radius_);
}

void Td::on_request(uint64 id, td_api::editMessageMedia &request) {
  CREATE_REQUEST(EditMessageMediaRequest, request.chat_id_, request.message_id_, std::move(request.reply_markup_),
                 std::move(request.input_message_content_));
}

void Td::on_request(uint64 id, td_api::editMessageCaption &request) {
  CREATE_REQUEST(EditMessageCaptionRequest, request.chat_id_, request.message_id_, std::move(request.reply_markup_),
                 std::move(request.caption_), request.show_caption_above_media_);
}

void Td::on_request(uint64 id, td_api::editMessageReplyMarkup &request) {
  CHECK_IS_BOT();
  CREATE_REQUEST(EditMessageReplyMarkupRequest, request.chat_id_, request.message_id_,
                 std::move(request.reply_markup_));
}

void Td::on_request(uint64 id, td_api::editInlineMessageText &request) {
  CHECK_IS_BOT();
  CLEAN_INPUT_STRING(request.inline_message_id_);
  CREATE_OK_REQUEST_PROMISE();
  inline_message_manager_->edit_inline_message_text(request.inline_message_id_, std::move(request.reply_markup_),
                                                    std::move(request.input_message_content_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::editInlineMessageLiveLocation &request) {
  CHECK_IS_BOT();
  CLEAN_INPUT_STRING(request.inline_message_id_);
  CREATE_OK_REQUEST_PROMISE();
  inline_message_manager_->edit_inline_message_live_location(
      request.inline_message_id_, std::move(request.reply_markup_), std::move(request.location_), request.live_period_,
      request.heading_, request.proximity_alert_radius_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::editInlineMessageMedia &request) {
  CHECK_IS_BOT();
  CLEAN_INPUT_STRING(request.inline_message_id_);
  CREATE_OK_REQUEST_PROMISE();
  inline_message_manager_->edit_inline_message_media(request.inline_message_id_, std::move(request.reply_markup_),
                                                     std::move(request.input_message_content_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::editInlineMessageCaption &request) {
  CHECK_IS_BOT();
  CLEAN_INPUT_STRING(request.inline_message_id_);
  CREATE_OK_REQUEST_PROMISE();
  inline_message_manager_->edit_inline_message_caption(request.inline_message_id_, std::move(request.reply_markup_),
                                                       std::move(request.caption_), request.show_caption_above_media_,
                                                       std::move(promise));
}

void Td::on_request(uint64 id, td_api::editInlineMessageReplyMarkup &request) {
  CHECK_IS_BOT();
  CLEAN_INPUT_STRING(request.inline_message_id_);
  CREATE_OK_REQUEST_PROMISE();
  inline_message_manager_->edit_inline_message_reply_markup(request.inline_message_id_,
                                                            std::move(request.reply_markup_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::editMessageSchedulingState &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->edit_message_scheduling_state({DialogId(request.chat_id_), MessageId(request.message_id_)},
                                                   std::move(request.scheduling_state_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::setMessageFactCheck &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->set_message_fact_check({DialogId(request.chat_id_), MessageId(request.message_id_)},
                                            std::move(request.text_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::sendBusinessMessage &request) {
  CHECK_IS_BOT();
  CREATE_REQUEST_PROMISE();
  business_connection_manager_->send_message(BusinessConnectionId(std::move(request.business_connection_id_)),
                                             DialogId(request.chat_id_), std::move(request.reply_to_),
                                             request.disable_notification_, request.protect_content_,
                                             MessageEffectId(request.effect_id_), std::move(request.reply_markup_),
                                             std::move(request.input_message_content_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::sendBusinessMessageAlbum &request) {
  CHECK_IS_BOT();
  CREATE_REQUEST_PROMISE();
  business_connection_manager_->send_message_album(
      BusinessConnectionId(std::move(request.business_connection_id_)), DialogId(request.chat_id_),
      std::move(request.reply_to_), request.disable_notification_, request.protect_content_,
      MessageEffectId(request.effect_id_), std::move(request.input_message_contents_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::editBusinessMessageText &request) {
  CHECK_IS_BOT();
  CREATE_REQUEST_PROMISE();
  business_connection_manager_->edit_business_message_text(
      BusinessConnectionId(std::move(request.business_connection_id_)), DialogId(request.chat_id_),
      MessageId(request.message_id_), std::move(request.reply_markup_), std::move(request.input_message_content_),
      std::move(promise));
}

void Td::on_request(uint64 id, td_api::editBusinessMessageLiveLocation &request) {
  CHECK_IS_BOT();
  CREATE_REQUEST_PROMISE();
  business_connection_manager_->edit_business_message_live_location(
      BusinessConnectionId(std::move(request.business_connection_id_)), DialogId(request.chat_id_),
      MessageId(request.message_id_), std::move(request.reply_markup_), std::move(request.location_),
      request.live_period_, request.heading_, request.proximity_alert_radius_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::editBusinessMessageMedia &request) {
  CHECK_IS_BOT();
  CREATE_REQUEST_PROMISE();
  business_connection_manager_->edit_business_message_media(
      BusinessConnectionId(std::move(request.business_connection_id_)), DialogId(request.chat_id_),
      MessageId(request.message_id_), std::move(request.reply_markup_), std::move(request.input_message_content_),
      std::move(promise));
}

void Td::on_request(uint64 id, td_api::editBusinessMessageCaption &request) {
  CHECK_IS_BOT();
  CREATE_REQUEST_PROMISE();
  business_connection_manager_->edit_business_message_caption(
      BusinessConnectionId(std::move(request.business_connection_id_)), DialogId(request.chat_id_),
      MessageId(request.message_id_), std::move(request.reply_markup_), std::move(request.caption_),
      request.show_caption_above_media_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::editBusinessMessageReplyMarkup &request) {
  CHECK_IS_BOT();
  CREATE_REQUEST_PROMISE();
  business_connection_manager_->edit_business_message_reply_markup(
      BusinessConnectionId(std::move(request.business_connection_id_)), DialogId(request.chat_id_),
      MessageId(request.message_id_), std::move(request.reply_markup_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::stopBusinessPoll &request) {
  CHECK_IS_BOT();
  CREATE_REQUEST_PROMISE();
  business_connection_manager_->stop_poll(BusinessConnectionId(std::move(request.business_connection_id_)),
                                          DialogId(request.chat_id_), MessageId(request.message_id_),
                                          std::move(request.reply_markup_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::loadQuickReplyShortcuts &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  quick_reply_manager_->get_quick_reply_shortcuts(std::move(promise));
}

void Td::on_request(uint64 id, const td_api::setQuickReplyShortcutName &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  quick_reply_manager_->set_quick_reply_shortcut_name(QuickReplyShortcutId(request.shortcut_id_), request.name_,
                                                      std::move(promise));
}

void Td::on_request(uint64 id, const td_api::deleteQuickReplyShortcut &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  quick_reply_manager_->delete_quick_reply_shortcut(QuickReplyShortcutId(request.shortcut_id_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::reorderQuickReplyShortcuts &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  quick_reply_manager_->reorder_quick_reply_shortcuts(
      QuickReplyShortcutId::get_quick_reply_shortcut_ids(request.shortcut_ids_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::loadQuickReplyShortcutMessages &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  quick_reply_manager_->get_quick_reply_shortcut_messages(QuickReplyShortcutId(request.shortcut_id_),
                                                          std::move(promise));
}

void Td::on_request(uint64 id, const td_api::deleteQuickReplyShortcutMessages &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  quick_reply_manager_->delete_quick_reply_shortcut_messages(
      QuickReplyShortcutId(request.shortcut_id_), MessageId::get_message_ids(request.message_ids_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::addQuickReplyShortcutMessage &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.shortcut_name_);
  auto r_sent_message = quick_reply_manager_->send_message(
      request.shortcut_name_, MessageId(request.reply_to_message_id_), std::move(request.input_message_content_));
  if (r_sent_message.is_error()) {
    send_closure(actor_id(this), &Td::send_error, id, r_sent_message.move_as_error());
  } else {
    send_closure(actor_id(this), &Td::send_result, id, r_sent_message.move_as_ok());
  }
}

void Td::on_request(uint64 id, td_api::addQuickReplyShortcutInlineQueryResultMessage &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.shortcut_name_);
  CLEAN_INPUT_STRING(request.result_id_);
  auto r_sent_message = quick_reply_manager_->send_inline_query_result_message(
      request.shortcut_name_, MessageId(request.reply_to_message_id_), request.query_id_, request.result_id_,
      request.hide_via_bot_);
  if (r_sent_message.is_error()) {
    send_closure(actor_id(this), &Td::send_error, id, r_sent_message.move_as_error());
  } else {
    send_closure(actor_id(this), &Td::send_result, id, r_sent_message.move_as_ok());
  }
}

void Td::on_request(uint64 id, td_api::addQuickReplyShortcutMessageAlbum &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.shortcut_name_);
  auto r_messages = quick_reply_manager_->send_message_group(
      request.shortcut_name_, MessageId(request.reply_to_message_id_), std::move(request.input_message_contents_));
  if (r_messages.is_error()) {
    send_closure(actor_id(this), &Td::send_error, id, r_messages.move_as_error());
  } else {
    send_closure(actor_id(this), &Td::send_result, id, r_messages.move_as_ok());
  }
}

void Td::on_request(uint64 id, td_api::readdQuickReplyShortcutMessages &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.shortcut_name_);
  auto r_messages =
      quick_reply_manager_->resend_messages(request.shortcut_name_, MessageId::get_message_ids(request.message_ids_));
  if (r_messages.is_error()) {
    return send_closure(actor_id(this), &Td::send_error, id, r_messages.move_as_error());
  }
  send_closure(actor_id(this), &Td::send_result, id, r_messages.move_as_ok());
}

void Td::on_request(uint64 id, td_api::editQuickReplyMessage &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  quick_reply_manager_->edit_quick_reply_message(QuickReplyShortcutId(request.shortcut_id_),
                                                 MessageId(request.message_id_),
                                                 std::move(request.input_message_content_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getStory &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  story_manager_->get_story(DialogId(request.story_sender_chat_id_), StoryId(request.story_id_), request.only_local_,
                            std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getChatsToSendStories &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  story_manager_->get_dialogs_to_send_stories(std::move(promise));
}

void Td::on_request(uint64 id, const td_api::canSendStory &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  story_manager_->can_send_story(DialogId(request.chat_id_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::sendStory &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  story_manager_->send_story(DialogId(request.chat_id_), std::move(request.content_), std::move(request.areas_),
                             std::move(request.caption_), std::move(request.privacy_settings_), request.active_period_,
                             std::move(request.from_story_full_id_), request.is_posted_to_chat_page_,
                             request.protect_content_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::editStory &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  story_manager_->edit_story(DialogId(request.story_sender_chat_id_), StoryId(request.story_id_),
                             std::move(request.content_), std::move(request.areas_), std::move(request.caption_),
                             std::move(promise));
}

void Td::on_request(uint64 id, td_api::setStoryPrivacySettings &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  story_manager_->set_story_privacy_settings(StoryId(request.story_id_), std::move(request.privacy_settings_),
                                             std::move(promise));
}

void Td::on_request(uint64 id, const td_api::toggleStoryIsPostedToChatPage &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  story_manager_->toggle_story_is_pinned(DialogId(request.story_sender_chat_id_), StoryId(request.story_id_),
                                         request.is_posted_to_chat_page_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::deleteStory &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  story_manager_->delete_story(DialogId(request.story_sender_chat_id_), StoryId(request.story_id_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::loadActiveStories &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  story_manager_->load_active_stories(StoryListId(request.story_list_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::setChatActiveStoriesList &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  story_manager_->toggle_dialog_stories_hidden(DialogId(request.chat_id_), StoryListId(request.story_list_),
                                               std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getForumTopicDefaultIcons &request) {
  CREATE_REQUEST_PROMISE();
  stickers_manager_->get_default_topic_icons(false, std::move(promise));
}

void Td::on_request(uint64 id, td_api::createForumTopic &request) {
  CLEAN_INPUT_STRING(request.name_);
  CREATE_REQUEST_PROMISE();
  forum_topic_manager_->create_forum_topic(DialogId(request.chat_id_), std::move(request.name_),
                                           std::move(request.icon_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::editForumTopic &request) {
  CLEAN_INPUT_STRING(request.name_);
  CREATE_OK_REQUEST_PROMISE();
  forum_topic_manager_->edit_forum_topic(DialogId(request.chat_id_), MessageId(request.message_thread_id_),
                                         std::move(request.name_), request.edit_icon_custom_emoji_,
                                         CustomEmojiId(request.icon_custom_emoji_id_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getForumTopic &request) {
  CREATE_REQUEST_PROMISE();
  forum_topic_manager_->get_forum_topic(DialogId(request.chat_id_), MessageId(request.message_thread_id_),
                                        std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getForumTopicLink &request) {
  CREATE_REQUEST_PROMISE();
  forum_topic_manager_->get_forum_topic_link(DialogId(request.chat_id_), MessageId(request.message_thread_id_),
                                             std::move(promise));
}

void Td::on_request(uint64 id, td_api::getForumTopics &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.query_);
  CREATE_REQUEST_PROMISE();
  forum_topic_manager_->get_forum_topics(DialogId(request.chat_id_), std::move(request.query_), request.offset_date_,
                                         MessageId(request.offset_message_id_),
                                         MessageId(request.offset_message_thread_id_), request.limit_,
                                         std::move(promise));
}

void Td::on_request(uint64 id, const td_api::toggleForumTopicIsClosed &request) {
  CREATE_OK_REQUEST_PROMISE();
  forum_topic_manager_->toggle_forum_topic_is_closed(DialogId(request.chat_id_), MessageId(request.message_thread_id_),
                                                     request.is_closed_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::toggleGeneralForumTopicIsHidden &request) {
  CREATE_OK_REQUEST_PROMISE();
  forum_topic_manager_->toggle_forum_topic_is_hidden(DialogId(request.chat_id_), request.is_hidden_,
                                                     std::move(promise));
}

void Td::on_request(uint64 id, const td_api::toggleForumTopicIsPinned &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  forum_topic_manager_->toggle_forum_topic_is_pinned(DialogId(request.chat_id_), MessageId(request.message_thread_id_),
                                                     request.is_pinned_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::setPinnedForumTopics &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  forum_topic_manager_->set_pinned_forum_topics(
      DialogId(request.chat_id_), MessageId::get_message_ids(request.message_thread_ids_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::deleteForumTopic &request) {
  CREATE_OK_REQUEST_PROMISE();
  forum_topic_manager_->delete_forum_topic(DialogId(request.chat_id_), MessageId(request.message_thread_id_),
                                           std::move(promise));
}

void Td::on_request(uint64 id, td_api::setGameScore &request) {
  CHECK_IS_BOT();
  CREATE_REQUEST_PROMISE();
  game_manager_->set_game_score({DialogId(request.chat_id_), MessageId(request.message_id_)}, request.edit_message_,
                                UserId(request.user_id_), request.score_, request.force_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::setInlineGameScore &request) {
  CHECK_IS_BOT();
  CLEAN_INPUT_STRING(request.inline_message_id_);
  CREATE_OK_REQUEST_PROMISE();
  inline_message_manager_->set_inline_game_score(request.inline_message_id_, request.edit_message_,
                                                 UserId(request.user_id_), request.score_, request.force_,
                                                 std::move(promise));
}

void Td::on_request(uint64 id, td_api::getGameHighScores &request) {
  CHECK_IS_BOT();
  CREATE_REQUEST_PROMISE();
  game_manager_->get_game_high_scores({DialogId(request.chat_id_), MessageId(request.message_id_)},
                                      UserId(request.user_id_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::getInlineGameHighScores &request) {
  CHECK_IS_BOT();
  CLEAN_INPUT_STRING(request.inline_message_id_);
  CREATE_REQUEST_PROMISE();
  inline_message_manager_->get_inline_game_high_scores(request.inline_message_id_, UserId(request.user_id_),
                                                       std::move(promise));
}

void Td::on_request(uint64 id, const td_api::deleteChatReplyMarkup &request) {
  CHECK_IS_USER();
  answer_ok_query(
      id, messages_manager_->delete_dialog_reply_markup(DialogId(request.chat_id_), MessageId(request.message_id_)));
}

void Td::on_request(uint64 id, td_api::sendChatAction &request) {
  CLEAN_INPUT_STRING(request.business_connection_id_);
  CREATE_OK_REQUEST_PROMISE();
  dialog_action_manager_->send_dialog_action(DialogId(request.chat_id_), MessageId(request.message_thread_id_),
                                             BusinessConnectionId(std::move(request.business_connection_id_)),
                                             DialogAction(std::move(request.action_)), std::move(promise));
}

void Td::on_request(uint64 id, td_api::forwardMessages &request) {
  auto input_message_ids = MessageId::get_message_ids(request.message_ids_);
  auto message_copy_options =
      transform(input_message_ids, [send_copy = request.send_copy_, remove_caption = request.remove_caption_](
                                       MessageId) { return MessageCopyOptions(send_copy, remove_caption); });
  auto r_messages = messages_manager_->forward_messages(
      DialogId(request.chat_id_), MessageId(request.message_thread_id_), DialogId(request.from_chat_id_),
      std::move(input_message_ids), std::move(request.options_), false, std::move(message_copy_options));
  if (r_messages.is_error()) {
    send_closure(actor_id(this), &Td::send_error, id, r_messages.move_as_error());
  } else {
    send_closure(actor_id(this), &Td::send_result, id, r_messages.move_as_ok());
  }
}

void Td::on_request(uint64 id, const td_api::sendQuickReplyShortcutMessages &request) {
  auto r_messages = messages_manager_->send_quick_reply_shortcut_messages(
      DialogId(request.chat_id_), QuickReplyShortcutId(request.shortcut_id_), request.sending_id_);
  if (r_messages.is_error()) {
    send_closure(actor_id(this), &Td::send_error, id, r_messages.move_as_error());
  } else {
    send_closure(actor_id(this), &Td::send_result, id, r_messages.move_as_ok());
  }
}

void Td::on_request(uint64 id, td_api::resendMessages &request) {
  DialogId dialog_id(request.chat_id_);
  auto r_message_ids = messages_manager_->resend_messages(dialog_id, MessageId::get_message_ids(request.message_ids_),
                                                          std::move(request.quote_));
  if (r_message_ids.is_error()) {
    return send_closure(actor_id(this), &Td::send_error, id, r_message_ids.move_as_error());
  }

  send_closure(actor_id(this), &Td::send_result, id,
               messages_manager_->get_messages_object(-1, dialog_id, r_message_ids.ok(), false, "resendMessages"));
}

void Td::on_request(uint64 id, td_api::getWebPagePreview &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  web_pages_manager_->get_web_page_preview(std::move(request.text_), std::move(request.link_preview_options_),
                                           std::move(promise));
}

void Td::on_request(uint64 id, td_api::getWebPageInstantView &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.url_);
  CREATE_REQUEST(GetWebPageInstantViewRequest, std::move(request.url_), request.force_full_);
}

void Td::on_request(uint64 id, const td_api::createPrivateChat &request) {
  CREATE_REQUEST(CreateChatRequest, DialogId(UserId(request.user_id_)), request.force_);
}

void Td::on_request(uint64 id, const td_api::createBasicGroupChat &request) {
  CREATE_REQUEST(CreateChatRequest, DialogId(ChatId(request.basic_group_id_)), request.force_);
}

void Td::on_request(uint64 id, const td_api::createSupergroupChat &request) {
  CREATE_REQUEST(CreateChatRequest, DialogId(ChannelId(request.supergroup_id_)), request.force_);
}

void Td::on_request(uint64 id, const td_api::createSecretChat &request) {
  CREATE_REQUEST(CreateChatRequest, DialogId(SecretChatId(request.secret_chat_id_)), true);
}

void Td::on_request(uint64 id, td_api::createNewBasicGroupChat &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.title_);
  CREATE_REQUEST_PROMISE();
  chat_manager_->create_new_chat(UserId::get_user_ids(request.user_ids_), std::move(request.title_),
                                 MessageTtl(request.message_auto_delete_time_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::createNewSupergroupChat &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.title_);
  CLEAN_INPUT_STRING(request.description_);
  CREATE_REQUEST_PROMISE();
  chat_manager_->create_new_channel(std::move(request.title_), request.is_forum_, !request.is_channel_,
                                    std::move(request.description_), DialogLocation(std::move(request.location_)),
                                    request.for_import_, MessageTtl(request.message_auto_delete_time_),
                                    std::move(promise));
}

void Td::on_request(uint64 id, const td_api::createNewSecretChat &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  user_manager_->create_new_secret_chat(UserId(request.user_id_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::createCall &request) {
  CHECK_IS_USER();

  if (request.protocol_ == nullptr) {
    return send_error_raw(id, 400, "Call protocol must be non-empty");
  }

  UserId user_id(request.user_id_);
  auto r_input_user = user_manager_->get_input_user(user_id);
  if (r_input_user.is_error()) {
    return send_error_raw(id, r_input_user.error().code(), r_input_user.error().message());
  }

  CREATE_REQUEST_PROMISE();
  auto query_promise = PromiseCreator::lambda([promise = std::move(promise)](Result<CallId> result) mutable {
    if (result.is_error()) {
      promise.set_error(result.move_as_error());
    } else {
      promise.set_value(result.ok().get_call_id_object());
    }
  });
  send_closure(G()->call_manager(), &CallManager::create_call, user_id, r_input_user.move_as_ok(),
               CallProtocol(*request.protocol_), request.is_video_, std::move(query_promise));
}

void Td::on_request(uint64 id, const td_api::acceptCall &request) {
  CHECK_IS_USER();
  if (request.protocol_ == nullptr) {
    return send_error_raw(id, 400, "Call protocol must be non-empty");
  }
  CREATE_OK_REQUEST_PROMISE();
  send_closure(G()->call_manager(), &CallManager::accept_call, CallId(request.call_id_),
               CallProtocol(*request.protocol_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::sendCallSignalingData &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  send_closure(G()->call_manager(), &CallManager::send_call_signaling_data, CallId(request.call_id_),
               std::move(request.data_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::discardCall &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  send_closure(G()->call_manager(), &CallManager::discard_call, CallId(request.call_id_), request.is_disconnected_,
               request.duration_, request.is_video_, request.connection_id_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::sendCallRating &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.comment_);
  CREATE_OK_REQUEST_PROMISE();
  send_closure(G()->call_manager(), &CallManager::rate_call, CallId(request.call_id_), request.rating_,
               std::move(request.comment_), std::move(request.problems_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::sendCallDebugInformation &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.debug_information_);
  CREATE_OK_REQUEST_PROMISE();
  send_closure(G()->call_manager(), &CallManager::send_call_debug_information, CallId(request.call_id_),
               std::move(request.debug_information_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::sendCallLog &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  send_closure(G()->call_manager(), &CallManager::send_call_log, CallId(request.call_id_), std::move(request.log_file_),
               std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getVideoChatAvailableParticipants &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  group_call_manager_->get_group_call_join_as(DialogId(request.chat_id_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::setVideoChatDefaultParticipant &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  TRY_RESULT_PROMISE(promise, default_join_as_dialog_id,
                     get_message_sender_dialog_id(this, request.default_participant_id_, true, false));
  group_call_manager_->set_group_call_default_join_as(DialogId(request.chat_id_), default_join_as_dialog_id,
                                                      std::move(promise));
}

void Td::on_request(uint64 id, td_api::createVideoChat &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.title_);
  CREATE_REQUEST_PROMISE();
  auto query_promise = PromiseCreator::lambda([promise = std::move(promise)](Result<GroupCallId> result) mutable {
    if (result.is_error()) {
      promise.set_error(result.move_as_error());
    } else {
      promise.set_value(td_api::make_object<td_api::groupCallId>(result.ok().get()));
    }
  });
  group_call_manager_->create_voice_chat(DialogId(request.chat_id_), std::move(request.title_), request.start_date_,
                                         request.is_rtmp_stream_, std::move(query_promise));
}

void Td::on_request(uint64 id, const td_api::getVideoChatRtmpUrl &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  group_call_manager_->get_voice_chat_rtmp_stream_url(DialogId(request.chat_id_), false, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::replaceVideoChatRtmpUrl &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  group_call_manager_->get_voice_chat_rtmp_stream_url(DialogId(request.chat_id_), true, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getGroupCall &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  group_call_manager_->get_group_call(GroupCallId(request.group_call_id_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::startScheduledGroupCall &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  group_call_manager_->start_scheduled_group_call(GroupCallId(request.group_call_id_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::toggleGroupCallEnabledStartNotification &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  group_call_manager_->toggle_group_call_start_subscribed(GroupCallId(request.group_call_id_),
                                                          request.enabled_start_notification_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::joinGroupCall &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.invite_hash_);
  CLEAN_INPUT_STRING(request.payload_);
  CREATE_REQUEST_PROMISE();
  TRY_RESULT_PROMISE(promise, join_as_dialog_id,
                     get_message_sender_dialog_id(this, request.participant_id_, true, true));
  auto query_promise = PromiseCreator::lambda([promise = std::move(promise)](Result<string> result) mutable {
    if (result.is_error()) {
      promise.set_error(result.move_as_error());
    } else {
      promise.set_value(make_tl_object<td_api::text>(result.move_as_ok()));
    }
  });
  group_call_manager_->join_group_call(GroupCallId(request.group_call_id_), join_as_dialog_id, request.audio_source_id_,
                                       std::move(request.payload_), request.is_muted_, request.is_my_video_enabled_,
                                       request.invite_hash_, std::move(query_promise));
}

void Td::on_request(uint64 id, td_api::startGroupCallScreenSharing &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.payload_);
  CREATE_REQUEST_PROMISE();
  auto query_promise = PromiseCreator::lambda([promise = std::move(promise)](Result<string> result) mutable {
    if (result.is_error()) {
      promise.set_error(result.move_as_error());
    } else {
      promise.set_value(make_tl_object<td_api::text>(result.move_as_ok()));
    }
  });
  group_call_manager_->start_group_call_screen_sharing(GroupCallId(request.group_call_id_), request.audio_source_id_,
                                                       std::move(request.payload_), std::move(query_promise));
}

void Td::on_request(uint64 id, const td_api::toggleGroupCallScreenSharingIsPaused &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  group_call_manager_->toggle_group_call_is_my_presentation_paused(GroupCallId(request.group_call_id_),
                                                                   request.is_paused_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::endGroupCallScreenSharing &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  group_call_manager_->end_group_call_screen_sharing(GroupCallId(request.group_call_id_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::setGroupCallTitle &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.title_);
  CREATE_OK_REQUEST_PROMISE();
  group_call_manager_->set_group_call_title(GroupCallId(request.group_call_id_), std::move(request.title_),
                                            std::move(promise));
}

void Td::on_request(uint64 id, const td_api::toggleGroupCallMuteNewParticipants &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  group_call_manager_->toggle_group_call_mute_new_participants(GroupCallId(request.group_call_id_),
                                                               request.mute_new_participants_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::revokeGroupCallInviteLink &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  group_call_manager_->revoke_group_call_invite_link(GroupCallId(request.group_call_id_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::inviteGroupCallParticipants &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  group_call_manager_->invite_group_call_participants(GroupCallId(request.group_call_id_),
                                                      UserId::get_user_ids(request.user_ids_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getGroupCallInviteLink &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  auto query_promise = PromiseCreator::lambda([promise = std::move(promise)](Result<string> result) mutable {
    if (result.is_error()) {
      promise.set_error(result.move_as_error());
    } else {
      promise.set_value(td_api::make_object<td_api::httpUrl>(result.move_as_ok()));
    }
  });
  group_call_manager_->get_group_call_invite_link(GroupCallId(request.group_call_id_), request.can_self_unmute_,
                                                  std::move(query_promise));
}

void Td::on_request(uint64 id, td_api::startGroupCallRecording &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.title_);
  CREATE_OK_REQUEST_PROMISE();
  group_call_manager_->toggle_group_call_recording(GroupCallId(request.group_call_id_), true, std::move(request.title_),
                                                   request.record_video_, request.use_portrait_orientation_,
                                                   std::move(promise));
}

void Td::on_request(uint64 id, const td_api::endGroupCallRecording &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  group_call_manager_->toggle_group_call_recording(GroupCallId(request.group_call_id_), false, string(), false, false,
                                                   std::move(promise));
}

void Td::on_request(uint64 id, const td_api::toggleGroupCallIsMyVideoPaused &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  group_call_manager_->toggle_group_call_is_my_video_paused(GroupCallId(request.group_call_id_),
                                                            request.is_my_video_paused_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::toggleGroupCallIsMyVideoEnabled &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  group_call_manager_->toggle_group_call_is_my_video_enabled(GroupCallId(request.group_call_id_),
                                                             request.is_my_video_enabled_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::setGroupCallParticipantIsSpeaking &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  group_call_manager_->set_group_call_participant_is_speaking(
      GroupCallId(request.group_call_id_), request.audio_source_, request.is_speaking_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::toggleGroupCallParticipantIsMuted &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  TRY_RESULT_PROMISE(promise, participant_dialog_id,
                     get_message_sender_dialog_id(this, request.participant_id_, true, false));
  group_call_manager_->toggle_group_call_participant_is_muted(
      GroupCallId(request.group_call_id_), participant_dialog_id, request.is_muted_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::setGroupCallParticipantVolumeLevel &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  TRY_RESULT_PROMISE(promise, participant_dialog_id,
                     get_message_sender_dialog_id(this, request.participant_id_, true, false));
  group_call_manager_->set_group_call_participant_volume_level(
      GroupCallId(request.group_call_id_), participant_dialog_id, request.volume_level_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::toggleGroupCallParticipantIsHandRaised &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  TRY_RESULT_PROMISE(promise, participant_dialog_id,
                     get_message_sender_dialog_id(this, request.participant_id_, true, false));
  group_call_manager_->toggle_group_call_participant_is_hand_raised(
      GroupCallId(request.group_call_id_), participant_dialog_id, request.is_hand_raised_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::loadGroupCallParticipants &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  group_call_manager_->load_group_call_participants(GroupCallId(request.group_call_id_), request.limit_,
                                                    std::move(promise));
}

void Td::on_request(uint64 id, const td_api::leaveGroupCall &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  group_call_manager_->leave_group_call(GroupCallId(request.group_call_id_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::endGroupCall &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  group_call_manager_->discard_group_call(GroupCallId(request.group_call_id_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getGroupCallStreams &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  group_call_manager_->get_group_call_streams(GroupCallId(request.group_call_id_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::getGroupCallStreamSegment &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  auto query_promise = PromiseCreator::lambda([promise = std::move(promise)](Result<string> result) mutable {
    if (result.is_error()) {
      promise.set_error(result.move_as_error());
    } else {
      auto file_part = td_api::make_object<td_api::filePart>();
      file_part->data_ = result.move_as_ok();
      promise.set_value(std::move(file_part));
    }
  });
  group_call_manager_->get_group_call_stream_segment(GroupCallId(request.group_call_id_), request.time_offset_,
                                                     request.scale_, request.channel_id_,
                                                     std::move(request.video_quality_), std::move(query_promise));
}

void Td::on_request(uint64 id, const td_api::upgradeBasicGroupChatToSupergroupChat &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  dialog_manager_->migrate_dialog_to_megagroup(DialogId(request.chat_id_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getChatListsToAddChat &request) {
  CHECK_IS_USER();
  auto dialog_lists = messages_manager_->get_dialog_lists_to_add_dialog(DialogId(request.chat_id_));
  auto chat_lists =
      transform(dialog_lists, [](DialogListId dialog_list_id) { return dialog_list_id.get_chat_list_object(); });
  send_closure(actor_id(this), &Td::send_result, id, td_api::make_object<td_api::chatLists>(std::move(chat_lists)));
}

void Td::on_request(uint64 id, const td_api::addChatToList &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->add_dialog_to_list(DialogId(request.chat_id_), DialogListId(request.chat_list_),
                                        std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getChatFolder &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  dialog_filter_manager_->get_dialog_filter(DialogFilterId(request.chat_folder_id_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getRecommendedChatFolders &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  dialog_filter_manager_->get_recommended_dialog_filters(std::move(promise));
}

void Td::on_request(uint64 id, td_api::createChatFolder &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  dialog_filter_manager_->create_dialog_filter(std::move(request.folder_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::editChatFolder &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  dialog_filter_manager_->edit_dialog_filter(DialogFilterId(request.chat_folder_id_), std::move(request.folder_),
                                             std::move(promise));
}

void Td::on_request(uint64 id, const td_api::deleteChatFolder &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  dialog_filter_manager_->delete_dialog_filter(DialogFilterId(request.chat_folder_id_),
                                               DialogId::get_dialog_ids(request.leave_chat_ids_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getChatFolderChatsToLeave &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  dialog_filter_manager_->get_leave_dialog_filter_suggestions(DialogFilterId(request.chat_folder_id_),
                                                              std::move(promise));
}

void Td::on_request(uint64 id, td_api::getChatFolderChatCount &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  auto query_promise = PromiseCreator::lambda([promise = std::move(promise)](Result<int32> result) mutable {
    if (result.is_error()) {
      promise.set_error(result.move_as_error());
    } else {
      promise.set_value(make_tl_object<td_api::count>(result.move_as_ok()));
    }
  });
  messages_manager_->get_dialog_filter_dialog_count(std::move(request.folder_), std::move(query_promise));
}

void Td::on_request(uint64 id, const td_api::reorderChatFolders &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  dialog_filter_manager_->reorder_dialog_filters(
      transform(request.chat_folder_ids_, [](int32 id) { return DialogFilterId(id); }),
      request.main_chat_list_position_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::toggleChatFolderTags &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  dialog_filter_manager_->toggle_dialog_filter_tags(request.are_tags_enabled_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getChatsForChatFolderInviteLink &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  dialog_filter_manager_->get_dialogs_for_dialog_filter_invite_link(DialogFilterId(request.chat_folder_id_),
                                                                    std::move(promise));
}

void Td::on_request(uint64 id, td_api::createChatFolderInviteLink &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.name_);
  CREATE_REQUEST_PROMISE();
  dialog_filter_manager_->create_dialog_filter_invite_link(
      DialogFilterId(request.chat_folder_id_), std::move(request.name_), DialogId::get_dialog_ids(request.chat_ids_),
      std::move(promise));
}

void Td::on_request(uint64 id, td_api::getChatFolderInviteLinks &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  dialog_filter_manager_->get_dialog_filter_invite_links(DialogFilterId(request.chat_folder_id_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::editChatFolderInviteLink &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.invite_link_);
  CLEAN_INPUT_STRING(request.name_);
  CREATE_REQUEST_PROMISE();
  dialog_filter_manager_->edit_dialog_filter_invite_link(
      DialogFilterId(request.chat_folder_id_), std::move(request.invite_link_), std::move(request.name_),
      DialogId::get_dialog_ids(request.chat_ids_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::deleteChatFolderInviteLink &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.invite_link_);
  CREATE_OK_REQUEST_PROMISE();
  dialog_filter_manager_->delete_dialog_filter_invite_link(DialogFilterId(request.chat_folder_id_),
                                                           std::move(request.invite_link_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::checkChatFolderInviteLink &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.invite_link_);
  CREATE_REQUEST_PROMISE();
  dialog_filter_manager_->check_dialog_filter_invite_link(std::move(request.invite_link_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::addChatFolderByInviteLink &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.invite_link_);
  CREATE_OK_REQUEST_PROMISE();
  dialog_filter_manager_->add_dialog_filter_by_invite_link(
      std::move(request.invite_link_), DialogId::get_dialog_ids(request.chat_ids_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getChatFolderNewChats &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  dialog_filter_manager_->get_dialog_filter_new_chats(DialogFilterId(request.chat_folder_id_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::processChatFolderNewChats &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  dialog_filter_manager_->add_dialog_filter_new_chats(
      DialogFilterId(request.chat_folder_id_), DialogId::get_dialog_ids(request.added_chat_ids_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getArchiveChatListSettings &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  auto query_promise =
      PromiseCreator::lambda([promise = std::move(promise)](Result<GlobalPrivacySettings> result) mutable {
        if (result.is_error()) {
          promise.set_error(result.move_as_error());
        } else {
          promise.set_value(result.ok().get_archive_chat_list_settings_object());
        }
      });
  GlobalPrivacySettings::get_global_privacy_settings(this, std::move(query_promise));
}

void Td::on_request(uint64 id, td_api::setArchiveChatListSettings &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  GlobalPrivacySettings::set_global_privacy_settings(this, GlobalPrivacySettings(std::move(request.settings_)),
                                                     std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getReadDatePrivacySettings &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  auto query_promise =
      PromiseCreator::lambda([promise = std::move(promise)](Result<GlobalPrivacySettings> result) mutable {
        if (result.is_error()) {
          promise.set_error(result.move_as_error());
        } else {
          promise.set_value(result.ok().get_read_date_privacy_settings_object());
        }
      });
  GlobalPrivacySettings::get_global_privacy_settings(this, std::move(query_promise));
}

void Td::on_request(uint64 id, td_api::setReadDatePrivacySettings &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  GlobalPrivacySettings::set_global_privacy_settings(this, GlobalPrivacySettings(std::move(request.settings_)),
                                                     std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getNewChatPrivacySettings &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  auto query_promise =
      PromiseCreator::lambda([promise = std::move(promise)](Result<GlobalPrivacySettings> result) mutable {
        if (result.is_error()) {
          promise.set_error(result.move_as_error());
        } else {
          promise.set_value(result.ok().get_new_chat_privacy_settings_object());
        }
      });
  GlobalPrivacySettings::get_global_privacy_settings(this, std::move(query_promise));
}

void Td::on_request(uint64 id, td_api::setNewChatPrivacySettings &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  GlobalPrivacySettings::set_global_privacy_settings(this, GlobalPrivacySettings(std::move(request.settings_)),
                                                     std::move(promise));
}

void Td::on_request(uint64 id, const td_api::canSendMessageToUser &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  user_manager_->can_send_message_to_user(UserId(request.user_id_), request.only_local_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::setChatTitle &request) {
  CLEAN_INPUT_STRING(request.title_);
  CREATE_OK_REQUEST_PROMISE();
  dialog_manager_->set_dialog_title(DialogId(request.chat_id_), request.title_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::setChatPhoto &request) {
  CREATE_OK_REQUEST_PROMISE();
  dialog_manager_->set_dialog_photo(DialogId(request.chat_id_), request.photo_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::setChatAccentColor &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  dialog_manager_->set_dialog_accent_color(DialogId(request.chat_id_), AccentColorId(request.accent_color_id_),
                                           CustomEmojiId(request.background_custom_emoji_id_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::setChatProfileAccentColor &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  dialog_manager_->set_dialog_profile_accent_color(
      DialogId(request.chat_id_), AccentColorId(request.profile_accent_color_id_),
      CustomEmojiId(request.profile_background_custom_emoji_id_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::setChatMessageAutoDeleteTime &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->set_dialog_message_ttl(DialogId(request.chat_id_), request.message_auto_delete_time_,
                                            std::move(promise));
}

void Td::on_request(uint64 id, const td_api::setChatEmojiStatus &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  dialog_manager_->set_dialog_emoji_status(DialogId(request.chat_id_), EmojiStatus(request.emoji_status_),
                                           std::move(promise));
}

void Td::on_request(uint64 id, const td_api::setChatPermissions &request) {
  CREATE_OK_REQUEST_PROMISE();
  dialog_manager_->set_dialog_permissions(DialogId(request.chat_id_), request.permissions_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::setChatBackground &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  background_manager_->set_dialog_background(DialogId(request.chat_id_), request.background_.get(), request.type_.get(),
                                             request.dark_theme_dimming_, !request.only_for_self_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::deleteChatBackground &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  background_manager_->delete_dialog_background(DialogId(request.chat_id_), request.restore_previous_,
                                                std::move(promise));
}

void Td::on_request(uint64 id, td_api::setChatTheme &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.theme_name_);
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->set_dialog_theme(DialogId(request.chat_id_), request.theme_name_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::setChatDraftMessage &request) {
  CHECK_IS_USER();
  answer_ok_query(
      id, messages_manager_->set_dialog_draft_message(DialogId(request.chat_id_), MessageId(request.message_thread_id_),
                                                      std::move(request.draft_message_)));
}

void Td::on_request(uint64 id, const td_api::toggleChatHasProtectedContent &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  dialog_manager_->toggle_dialog_has_protected_content(DialogId(request.chat_id_), request.has_protected_content_,
                                                       std::move(promise));
}

void Td::on_request(uint64 id, const td_api::toggleChatIsPinned &request) {
  CHECK_IS_USER();
  answer_ok_query(id, messages_manager_->toggle_dialog_is_pinned(DialogListId(request.chat_list_),
                                                                 DialogId(request.chat_id_), request.is_pinned_));
}

void Td::on_request(uint64 id, const td_api::toggleChatViewAsTopics &request) {
  CHECK_IS_USER();
  answer_ok_query(
      id, messages_manager_->toggle_dialog_view_as_messages(DialogId(request.chat_id_), !request.view_as_topics_));
}

void Td::on_request(uint64 id, const td_api::toggleChatIsTranslatable &request) {
  CHECK_IS_USER();
  answer_ok_query(
      id, messages_manager_->toggle_dialog_is_translatable(DialogId(request.chat_id_), request.is_translatable_));
}

void Td::on_request(uint64 id, const td_api::toggleChatIsMarkedAsUnread &request) {
  CHECK_IS_USER();
  answer_ok_query(id, messages_manager_->toggle_dialog_is_marked_as_unread(DialogId(request.chat_id_),
                                                                           request.is_marked_as_unread_));
}

void Td::on_request(uint64 id, const td_api::setMessageSenderBlockList &request) {
  CHECK_IS_USER();
  answer_ok_query(id, messages_manager_->set_message_sender_block_list(request.sender_id_, request.block_list_));
}

void Td::on_request(uint64 id, const td_api::toggleChatDefaultDisableNotification &request) {
  CHECK_IS_USER();
  answer_ok_query(id, messages_manager_->toggle_dialog_silent_send_message(DialogId(request.chat_id_),
                                                                           request.default_disable_notification_));
}

void Td::on_request(uint64 id, const td_api::setPinnedChats &request) {
  CHECK_IS_USER();
  answer_ok_query(id, messages_manager_->set_pinned_dialogs(DialogListId(request.chat_list_),
                                                            DialogId::get_dialog_ids(request.chat_ids_)));
}

void Td::on_request(uint64 id, const td_api::readChatList &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->read_all_dialogs_from_list(DialogListId(request.chat_list_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getStoryNotificationSettingsExceptions &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  notification_settings_manager_->get_story_notification_settings_exceptions(std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getChatActiveStories &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  story_manager_->get_dialog_expiring_stories(DialogId(request.chat_id_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getChatPostedToChatPageStories &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  story_manager_->get_dialog_pinned_stories(DialogId(request.chat_id_), StoryId(request.from_story_id_), request.limit_,
                                            std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getChatArchivedStories &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  story_manager_->get_story_archive(DialogId(request.chat_id_), StoryId(request.from_story_id_), request.limit_,
                                    std::move(promise));
}

void Td::on_request(uint64 id, const td_api::setChatPinnedStories &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  story_manager_->set_pinned_stories(DialogId(request.chat_id_), StoryId::get_story_ids(request.story_ids_),
                                     std::move(promise));
}

void Td::on_request(uint64 id, const td_api::openStory &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  story_manager_->open_story(DialogId(request.story_sender_chat_id_), StoryId(request.story_id_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::closeStory &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  story_manager_->close_story(DialogId(request.story_sender_chat_id_), StoryId(request.story_id_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getStoryAvailableReactions &request) {
  CHECK_IS_USER();
  send_closure(actor_id(this), &Td::send_result, id, reaction_manager_->get_available_reactions(request.row_size_));
}

void Td::on_request(uint64 id, const td_api::setStoryReaction &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  story_manager_->set_story_reaction({DialogId(request.story_sender_chat_id_), StoryId(request.story_id_)},
                                     ReactionType(request.reaction_type_), request.update_recent_reactions_,
                                     std::move(promise));
}

void Td::on_request(uint64 id, td_api::getStoryInteractions &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.query_);
  CLEAN_INPUT_STRING(request.offset_);
  CREATE_REQUEST_PROMISE();
  story_manager_->get_story_interactions(StoryId(request.story_id_), request.query_, request.only_contacts_,
                                         request.prefer_forwards_, request.prefer_with_reaction_, request.offset_,
                                         request.limit_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::getChatStoryInteractions &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.offset_);
  CREATE_REQUEST_PROMISE();
  story_manager_->get_dialog_story_interactions({DialogId(request.story_sender_chat_id_), StoryId(request.story_id_)},
                                                ReactionType(request.reaction_type_), request.prefer_forwards_,
                                                request.offset_, request.limit_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::reportStory &request) {
  CHECK_IS_USER();
  auto r_report_reason = ReportReason::get_report_reason(std::move(request.reason_), std::move(request.text_));
  if (r_report_reason.is_error()) {
    return send_error_raw(id, r_report_reason.error().code(), r_report_reason.error().message());
  }
  CREATE_OK_REQUEST_PROMISE();
  story_manager_->report_story({DialogId(request.story_sender_chat_id_), StoryId(request.story_id_)},
                               r_report_reason.move_as_ok(), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::activateStoryStealthMode &request) {
  CREATE_OK_REQUEST_PROMISE();
  story_manager_->activate_stealth_mode(std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getChatBoostLevelFeatures &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  promise.set_value(boost_manager_->get_chat_boost_level_features_object(!request.is_channel_, request.level_));
}

void Td::on_request(uint64 id, const td_api::getChatBoostFeatures &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  promise.set_value(boost_manager_->get_chat_boost_features_object(!request.is_channel_));
}

void Td::on_request(uint64 id, const td_api::getAvailableChatBoostSlots &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  boost_manager_->get_boost_slots(std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getChatBoostStatus &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  boost_manager_->get_dialog_boost_status(DialogId(request.chat_id_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::boostChat &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  boost_manager_->boost_dialog(DialogId(request.chat_id_), std::move(request.slot_ids_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getChatBoostLink &request) {
  auto r_boost_link = boost_manager_->get_dialog_boost_link(DialogId(request.chat_id_));
  if (r_boost_link.is_error()) {
    send_closure(actor_id(this), &Td::send_error, id, r_boost_link.move_as_error());
  } else {
    send_closure(actor_id(this), &Td::send_result, id,
                 td_api::make_object<td_api::chatBoostLink>(r_boost_link.ok().first, r_boost_link.ok().second));
  }
}

void Td::on_request(uint64 id, td_api::getChatBoostLinkInfo &request) {
  CLEAN_INPUT_STRING(request.url_);
  CREATE_REQUEST(GetDialogBoostLinkInfoRequest, std::move(request.url_));
}

void Td::on_request(uint64 id, td_api::getChatBoosts &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.offset_);
  CREATE_REQUEST_PROMISE();
  boost_manager_->get_dialog_boosts(DialogId(request.chat_id_), request.only_gift_codes_, request.offset_,
                                    request.limit_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getUserChatBoosts &request) {
  CHECK_IS_BOT();
  CREATE_REQUEST_PROMISE();
  boost_manager_->get_user_dialog_boosts(DialogId(request.chat_id_), UserId(request.user_id_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getAttachmentMenuBot &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  attach_menu_manager_->get_attach_menu_bot(UserId(request.bot_user_id_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::toggleBotIsAddedToAttachmentMenu &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  attach_menu_manager_->toggle_bot_is_added_to_attach_menu(UserId(request.bot_user_id_), request.is_added_,
                                                           request.allow_write_access_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::setChatAvailableReactions &request) {
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->set_dialog_available_reactions(DialogId(request.chat_id_), std::move(request.available_reactions_),
                                                    std::move(promise));
}

void Td::on_request(uint64 id, td_api::setChatClientData &request) {
  answer_ok_query(
      id, messages_manager_->set_dialog_client_data(DialogId(request.chat_id_), std::move(request.client_data_)));
}

void Td::on_request(uint64 id, td_api::setChatDescription &request) {
  CLEAN_INPUT_STRING(request.description_);
  CREATE_OK_REQUEST_PROMISE();
  dialog_manager_->set_dialog_description(DialogId(request.chat_id_), request.description_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::setChatDiscussionGroup &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  chat_manager_->set_channel_discussion_group(DialogId(request.chat_id_), DialogId(request.discussion_chat_id_),
                                              std::move(promise));
}

void Td::on_request(uint64 id, td_api::setChatLocation &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  dialog_manager_->set_dialog_location(DialogId(request.chat_id_), DialogLocation(std::move(request.location_)),
                                       std::move(promise));
}

void Td::on_request(uint64 id, const td_api::setChatSlowModeDelay &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  chat_manager_->set_channel_slow_mode_delay(DialogId(request.chat_id_), request.slow_mode_delay_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::pinChatMessage &request) {
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->pin_dialog_message(DialogId(request.chat_id_), MessageId(request.message_id_),
                                        request.disable_notification_, request.only_for_self_, false,
                                        std::move(promise));
}

void Td::on_request(uint64 id, const td_api::unpinChatMessage &request) {
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->pin_dialog_message(DialogId(request.chat_id_), MessageId(request.message_id_), false, false, true,
                                        std::move(promise));
}

void Td::on_request(uint64 id, const td_api::unpinAllChatMessages &request) {
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->unpin_all_dialog_messages(DialogId(request.chat_id_), MessageId(), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::unpinAllMessageThreadMessages &request) {
  if (request.message_thread_id_ == 0) {
    return send_error_raw(id, 400, "Invalid message thread identifier specified");
  }
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->unpin_all_dialog_messages(DialogId(request.chat_id_), MessageId(request.message_thread_id_),
                                               std::move(promise));
}

void Td::on_request(uint64 id, const td_api::joinChat &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  dialog_participant_manager_->add_dialog_participant(
      DialogId(request.chat_id_), user_manager_->get_my_id(), 0,
      DialogParticipantManager::wrap_failed_to_add_members_promise(std::move(promise)));
}

void Td::on_request(uint64 id, const td_api::leaveChat &request) {
  CREATE_OK_REQUEST_PROMISE();
  DialogId dialog_id(request.chat_id_);
  td_api::object_ptr<td_api::ChatMemberStatus> new_status = td_api::make_object<td_api::chatMemberStatusLeft>();
  if (dialog_id.get_type() == DialogType::Channel && dialog_manager_->have_dialog_force(dialog_id, "leaveChat")) {
    auto status = chat_manager_->get_channel_status(dialog_id.get_channel_id());
    if (status.is_creator()) {
      if (!status.is_member()) {
        return promise.set_value(Unit());
      }

      new_status =
          td_api::make_object<td_api::chatMemberStatusCreator>(status.get_rank(), status.is_anonymous(), false);
    }
  }
  dialog_participant_manager_->set_dialog_participant_status(dialog_id, dialog_manager_->get_my_dialog_id(),
                                                             std::move(new_status), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::addChatMember &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  dialog_participant_manager_->add_dialog_participant(DialogId(request.chat_id_), UserId(request.user_id_),
                                                      request.forward_limit_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::addChatMembers &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  dialog_participant_manager_->add_dialog_participants(DialogId(request.chat_id_),
                                                       UserId::get_user_ids(request.user_ids_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::setChatMemberStatus &request) {
  CREATE_OK_REQUEST_PROMISE();
  TRY_RESULT_PROMISE(promise, participant_dialog_id,
                     get_message_sender_dialog_id(this, request.member_id_, false, false));
  dialog_participant_manager_->set_dialog_participant_status(DialogId(request.chat_id_), participant_dialog_id,
                                                             std::move(request.status_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::banChatMember &request) {
  CREATE_OK_REQUEST_PROMISE();
  TRY_RESULT_PROMISE(promise, participant_dialog_id,
                     get_message_sender_dialog_id(this, request.member_id_, false, false));
  dialog_participant_manager_->ban_dialog_participant(DialogId(request.chat_id_), participant_dialog_id,
                                                      request.banned_until_date_, request.revoke_messages_,
                                                      std::move(promise));
}

void Td::on_request(uint64 id, const td_api::canTransferOwnership &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  auto query_promise = PromiseCreator::lambda(
      [promise = std::move(promise)](Result<DialogParticipantManager::CanTransferOwnershipResult> result) mutable {
        if (result.is_error()) {
          promise.set_error(result.move_as_error());
        } else {
          promise.set_value(DialogParticipantManager::get_can_transfer_ownership_result_object(result.ok()));
        }
      });
  dialog_participant_manager_->can_transfer_ownership(std::move(query_promise));
}

void Td::on_request(uint64 id, td_api::transferChatOwnership &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.password_);
  CREATE_OK_REQUEST_PROMISE();
  dialog_participant_manager_->transfer_dialog_ownership(DialogId(request.chat_id_), UserId(request.user_id_),
                                                         request.password_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getChatMember &request) {
  CREATE_REQUEST_PROMISE();
  TRY_RESULT_PROMISE(promise, participant_dialog_id,
                     get_message_sender_dialog_id(this, request.member_id_, false, false));
  dialog_participant_manager_->get_dialog_participant(DialogId(request.chat_id_), participant_dialog_id,
                                                      std::move(promise));
}

void Td::on_request(uint64 id, td_api::searchChatMembers &request) {
  CLEAN_INPUT_STRING(request.query_);
  CREATE_REQUEST_PROMISE();
  auto query_promise =
      PromiseCreator::lambda([promise = std::move(promise), td = this](Result<DialogParticipants> result) mutable {
        if (result.is_error()) {
          promise.set_error(result.move_as_error());
        } else {
          promise.set_value(result.ok().get_chat_members_object(td, "searchChatMembers"));
        }
      });
  dialog_participant_manager_->search_dialog_participants(DialogId(request.chat_id_), request.query_, request.limit_,
                                                          DialogParticipantFilter(request.filter_),
                                                          std::move(query_promise));
}

void Td::on_request(uint64 id, const td_api::getChatAdministrators &request) {
  CREATE_REQUEST_PROMISE();
  dialog_participant_manager_->get_dialog_administrators(DialogId(request.chat_id_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::replacePrimaryChatInviteLink &request) {
  CREATE_REQUEST_PROMISE();
  dialog_invite_link_manager_->export_dialog_invite_link(DialogId(request.chat_id_), string(), 0, 0, false, true,
                                                         std::move(promise));
}

void Td::on_request(uint64 id, td_api::createChatInviteLink &request) {
  CLEAN_INPUT_STRING(request.name_);
  CREATE_REQUEST_PROMISE();
  dialog_invite_link_manager_->export_dialog_invite_link(DialogId(request.chat_id_), std::move(request.name_),
                                                         request.expiration_date_, request.member_limit_,
                                                         request.creates_join_request_, false, std::move(promise));
}

void Td::on_request(uint64 id, td_api::editChatInviteLink &request) {
  CLEAN_INPUT_STRING(request.name_);
  CLEAN_INPUT_STRING(request.invite_link_);
  CREATE_REQUEST_PROMISE();
  dialog_invite_link_manager_->edit_dialog_invite_link(
      DialogId(request.chat_id_), request.invite_link_, std::move(request.name_), request.expiration_date_,
      request.member_limit_, request.creates_join_request_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::getChatInviteLink &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.invite_link_);
  CREATE_REQUEST_PROMISE();
  dialog_invite_link_manager_->get_dialog_invite_link(DialogId(request.chat_id_), request.invite_link_,
                                                      std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getChatInviteLinkCounts &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  dialog_invite_link_manager_->get_dialog_invite_link_counts(DialogId(request.chat_id_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::getChatInviteLinks &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.offset_invite_link_);
  CREATE_REQUEST_PROMISE();
  dialog_invite_link_manager_->get_dialog_invite_links(DialogId(request.chat_id_), UserId(request.creator_user_id_),
                                                       request.is_revoked_, request.offset_date_,
                                                       request.offset_invite_link_, request.limit_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::getChatInviteLinkMembers &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.invite_link_);
  CREATE_REQUEST_PROMISE();
  dialog_invite_link_manager_->get_dialog_invite_link_users(DialogId(request.chat_id_), request.invite_link_,
                                                            std::move(request.offset_member_), request.limit_,
                                                            std::move(promise));
}

void Td::on_request(uint64 id, td_api::getChatJoinRequests &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.invite_link_);
  CLEAN_INPUT_STRING(request.query_);
  CREATE_REQUEST_PROMISE();
  dialog_participant_manager_->get_dialog_join_requests(DialogId(request.chat_id_), request.invite_link_,
                                                        request.query_, std::move(request.offset_request_),
                                                        request.limit_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::processChatJoinRequest &request) {
  CREATE_OK_REQUEST_PROMISE();
  dialog_participant_manager_->process_dialog_join_request(DialogId(request.chat_id_), UserId(request.user_id_),
                                                           request.approve_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::processChatJoinRequests &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.invite_link_);
  CREATE_OK_REQUEST_PROMISE();
  dialog_participant_manager_->process_dialog_join_requests(DialogId(request.chat_id_), request.invite_link_,
                                                            request.approve_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::revokeChatInviteLink &request) {
  CLEAN_INPUT_STRING(request.invite_link_);
  CREATE_REQUEST_PROMISE();
  dialog_invite_link_manager_->revoke_dialog_invite_link(DialogId(request.chat_id_), request.invite_link_,
                                                         std::move(promise));
}

void Td::on_request(uint64 id, td_api::deleteRevokedChatInviteLink &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.invite_link_);
  CREATE_OK_REQUEST_PROMISE();
  dialog_invite_link_manager_->delete_revoked_dialog_invite_link(DialogId(request.chat_id_), request.invite_link_,
                                                                 std::move(promise));
}

void Td::on_request(uint64 id, const td_api::deleteAllRevokedChatInviteLinks &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  dialog_invite_link_manager_->delete_all_revoked_dialog_invite_links(
      DialogId(request.chat_id_), UserId(request.creator_user_id_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::checkChatInviteLink &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.invite_link_);
  CREATE_REQUEST(CheckChatInviteLinkRequest, request.invite_link_);
}

void Td::on_request(uint64 id, td_api::joinChatByInviteLink &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.invite_link_);
  CREATE_REQUEST(JoinChatByInviteLinkRequest, request.invite_link_);
}

void Td::on_request(uint64 id, td_api::getChatEventLog &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.query_);
  CREATE_REQUEST_PROMISE();
  get_dialog_event_log(this, DialogId(request.chat_id_), std::move(request.query_), request.from_event_id_,
                       request.limit_, std::move(request.filters_), UserId::get_user_ids(request.user_ids_),
                       std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getTimeZones &request) {
  CREATE_REQUEST_PROMISE();
  time_zone_manager_->get_time_zones(std::move(promise));
}

void Td::on_request(uint64 id, const td_api::clearAllDraftMessages &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->clear_all_draft_messages(request.exclude_secret_chats_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::downloadFile &request) {
  auto priority = request.priority_;
  if (!(1 <= priority && priority <= 32)) {
    return send_error_raw(id, 400, "Download priority must be between 1 and 32");
  }
  auto offset = request.offset_;
  if (offset < 0) {
    return send_error_raw(id, 400, "Download offset must be non-negative");
  }
  auto limit = request.limit_;
  if (limit < 0) {
    return send_error_raw(id, 400, "Download limit must be non-negative");
  }

  FileId file_id(request.file_id_, 0);
  auto file_view = file_manager_->get_file_view(file_id);
  if (file_view.empty()) {
    return send_error_raw(id, 400, "Invalid file identifier");
  }

  auto info_it = pending_file_downloads_.find(file_id);
  DownloadInfo *info = info_it == pending_file_downloads_.end() ? nullptr : &info_it->second;
  if (info != nullptr && (offset != info->offset || limit != info->limit)) {
    // we can't have two pending requests with different offset and limit, so cancel all previous requests
    auto request_ids = std::move(info->request_ids);
    info->request_ids.clear();
    for (auto request_id : request_ids) {
      send_closure(actor_id(this), &Td::send_error, request_id,
                   Status::Error(200, "Canceled by another downloadFile request"));
    }
  }
  if (request.synchronous_) {
    if (info == nullptr) {
      info = &pending_file_downloads_[file_id];
    }
    info->offset = offset;
    info->limit = limit;
    info->request_ids.push_back(id);
  }
  Promise<td_api::object_ptr<td_api::file>> download_promise;
  if (!request.synchronous_) {
    CREATE_REQUEST_PROMISE();
    download_promise = std::move(promise);
  }
  file_manager_->download(file_id, download_file_callback_, priority, offset, limit, std::move(download_promise));
}

void Td::on_file_download_finished(FileId file_id) {
  auto it = pending_file_downloads_.find(file_id);
  if (it == pending_file_downloads_.end()) {
    return;
  }
  for (auto id : it->second.request_ids) {
    // there was send_closure to call this function
    auto file_object = file_manager_->get_file_object(file_id, false);
    CHECK(file_object != nullptr);
    auto download_offset = file_object->local_->download_offset_;
    auto downloaded_size = file_object->local_->downloaded_prefix_size_;
    auto file_size = file_object->size_;
    auto limit = it->second.limit;
    if (limit == 0) {
      limit = std::numeric_limits<int64>::max();
    }
    if (file_object->local_->is_downloading_completed_ ||
        (download_offset <= it->second.offset && download_offset + downloaded_size >= it->second.offset &&
         ((file_size != 0 && download_offset + downloaded_size == file_size) ||
          download_offset + downloaded_size - it->second.offset >= limit))) {
      send_result(id, std::move(file_object));
    } else {
      send_error_impl(id, td_api::make_object<td_api::error>(400, "File download has failed or was canceled"));
    }
  }
  pending_file_downloads_.erase(it);
}

void Td::on_request(uint64 id, const td_api::getFileDownloadedPrefixSize &request) {
  if (request.offset_ < 0) {
    return send_error_raw(id, 400, "Parameter offset must be non-negative");
  }
  auto file_view = file_manager_->get_file_view(FileId(request.file_id_, 0));
  if (file_view.empty()) {
    return send_closure(actor_id(this), &Td::send_error, id, Status::Error(400, "Unknown file ID"));
  }
  send_closure(actor_id(this), &Td::send_result, id,
               td_api::make_object<td_api::fileDownloadedPrefixSize>(file_view.downloaded_prefix(request.offset_)));
}

void Td::on_request(uint64 id, const td_api::cancelDownloadFile &request) {
  file_manager_->download(FileId(request.file_id_, 0), nullptr, request.only_if_pending_ ? -1 : 0,
                          FileManager::KEEP_DOWNLOAD_OFFSET, FileManager::KEEP_DOWNLOAD_LIMIT,
                          Promise<td_api::object_ptr<td_api::file>>());
  send_closure(actor_id(this), &Td::send_result, id, make_tl_object<td_api::ok>());
}

void Td::on_request(uint64 id, const td_api::getSuggestedFileName &request) {
  Result<string> r_file_name = file_manager_->get_suggested_file_name(FileId(request.file_id_, 0), request.directory_);
  if (r_file_name.is_error()) {
    return send_closure(actor_id(this), &Td::send_error, id, r_file_name.move_as_error());
  }
  send_closure(actor_id(this), &Td::send_result, id, td_api::make_object<td_api::text>(r_file_name.ok()));
}

void Td::on_request(uint64 id, td_api::preliminaryUploadFile &request) {
  auto priority = request.priority_;
  if (!(1 <= priority && priority <= 32)) {
    return send_error_raw(id, 400, "Upload priority must be between 1 and 32");
  }

  auto file_type = request.file_type_ == nullptr ? FileType::Temp : get_file_type(*request.file_type_);
  bool is_secret = file_type == FileType::Encrypted || file_type == FileType::EncryptedThumbnail;
  bool is_secure = file_type == FileType::SecureEncrypted;
  auto r_file_id = file_manager_->get_input_file_id(file_type, request.file_, DialogId(), false, is_secret,
                                                    !is_secure && !is_secret, is_secure);
  if (r_file_id.is_error()) {
    return send_error_raw(id, r_file_id.error().code(), r_file_id.error().message());
  }
  auto file_id = r_file_id.ok();
  auto upload_file_id = file_manager_->dup_file_id(file_id, "preliminaryUploadFile");

  file_manager_->upload(upload_file_id, upload_file_callback_, priority, 0);

  send_closure(actor_id(this), &Td::send_result, id, file_manager_->get_file_object(upload_file_id, false));
}

void Td::on_request(uint64 id, const td_api::cancelPreliminaryUploadFile &request) {
  file_manager_->cancel_upload(FileId(request.file_id_, 0));

  send_closure(actor_id(this), &Td::send_result, id, make_tl_object<td_api::ok>());
}

void Td::on_request(uint64 id, td_api::writeGeneratedFilePart &request) {
  CREATE_OK_REQUEST_PROMISE();
  send_closure(file_manager_actor_, &FileManager::external_file_generate_write_part, request.generation_id_,
               request.offset_, std::move(request.data_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::setFileGenerationProgress &request) {
  CREATE_OK_REQUEST_PROMISE();
  send_closure(file_manager_actor_, &FileManager::external_file_generate_progress, request.generation_id_,
               request.expected_size_, request.local_prefix_size_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::finishFileGeneration &request) {
  Status status;
  if (request.error_ != nullptr) {
    CLEAN_INPUT_STRING(request.error_->message_);
    status = Status::Error(request.error_->code_, request.error_->message_);
  }
  CREATE_OK_REQUEST_PROMISE();
  send_closure(file_manager_actor_, &FileManager::external_file_generate_finish, request.generation_id_,
               std::move(status), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::readFilePart &request) {
  CREATE_REQUEST_PROMISE();
  send_closure(file_manager_actor_, &FileManager::read_file_part, FileId(request.file_id_, 0), request.offset_,
               request.count_, 2, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::deleteFile &request) {
  CREATE_OK_REQUEST_PROMISE();
  send_closure(file_manager_actor_, &FileManager::delete_file, FileId(request.file_id_, 0), std::move(promise),
               "td_api::deleteFile");
}

void Td::on_request(uint64 id, const td_api::addFileToDownloads &request) {
  if (!(1 <= request.priority_ && request.priority_ <= 32)) {
    return send_error_raw(id, 400, "Download priority must be between 1 and 32");
  }
  CREATE_REQUEST_PROMISE();
  messages_manager_->add_message_file_to_downloads(
      MessageFullId(DialogId(request.chat_id_), MessageId(request.message_id_)), FileId(request.file_id_, 0),
      request.priority_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::toggleDownloadIsPaused &request) {
  CREATE_OK_REQUEST_PROMISE();
  send_closure(download_manager_actor_, &DownloadManager::toggle_is_paused, FileId(request.file_id_, 0),
               request.is_paused_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::toggleAllDownloadsArePaused &request) {
  CREATE_OK_REQUEST_PROMISE();
  send_closure(download_manager_actor_, &DownloadManager::toggle_all_is_paused, request.are_paused_,
               std::move(promise));
}

void Td::on_request(uint64 id, const td_api::removeFileFromDownloads &request) {
  CREATE_OK_REQUEST_PROMISE();
  send_closure(download_manager_actor_, &DownloadManager::remove_file, FileId(request.file_id_, 0), FileSourceId(),
               request.delete_from_cache_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::removeAllFilesFromDownloads &request) {
  CREATE_OK_REQUEST_PROMISE();
  send_closure(download_manager_actor_, &DownloadManager::remove_all_files, request.only_active_,
               request.only_completed_, request.delete_from_cache_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::searchFileDownloads &request) {
  CLEAN_INPUT_STRING(request.query_);
  CLEAN_INPUT_STRING(request.offset_);
  CREATE_REQUEST_PROMISE();
  send_closure(download_manager_actor_, &DownloadManager::search, std::move(request.query_), request.only_active_,
               request.only_completed_, std::move(request.offset_), request.limit_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::setApplicationVerificationToken &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.token_);
  CREATE_OK_REQUEST_PROMISE();
  G()->net_query_dispatcher().set_verification_token(request.verification_id_, std::move(request.token_),
                                                     std::move(promise));
}

void Td::on_request(uint64 id, td_api::getMessageFileType &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.message_file_head_);
  CREATE_REQUEST_PROMISE();
  message_import_manager_->get_message_file_type(request.message_file_head_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getMessageImportConfirmationText &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  auto query_promise = PromiseCreator::lambda([promise = std::move(promise)](Result<string> result) mutable {
    if (result.is_error()) {
      promise.set_error(result.move_as_error());
    } else {
      promise.set_value(make_tl_object<td_api::text>(result.move_as_ok()));
    }
  });
  message_import_manager_->get_message_import_confirmation_text(DialogId(request.chat_id_), std::move(query_promise));
}

void Td::on_request(uint64 id, const td_api::importMessages &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  message_import_manager_->import_messages(DialogId(request.chat_id_), request.message_file_, request.attached_files_,
                                           std::move(promise));
}

void Td::on_request(uint64 id, const td_api::blockMessageSenderFromReplies &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->block_message_sender_from_replies(MessageId(request.message_id_), request.delete_message_,
                                                       request.delete_all_messages_, request.report_spam_,
                                                       std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getBlockedMessageSenders &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  messages_manager_->get_blocked_dialogs(request.block_list_, request.offset_, request.limit_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::addContact &request) {
  CHECK_IS_USER();
  auto r_contact = get_contact(this, std::move(request.contact_));
  if (r_contact.is_error()) {
    return send_closure(actor_id(this), &Td::send_error, id, r_contact.move_as_error());
  }
  CREATE_OK_REQUEST_PROMISE();
  user_manager_->add_contact(r_contact.move_as_ok(), request.share_phone_number_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::importContacts &request) {
  CHECK_IS_USER();
  vector<Contact> contacts;
  contacts.reserve(request.contacts_.size());
  for (auto &contact : request.contacts_) {
    auto r_contact = get_contact(this, std::move(contact));
    if (r_contact.is_error()) {
      return send_closure(actor_id(this), &Td::send_error, id, r_contact.move_as_error());
    }
    contacts.push_back(r_contact.move_as_ok());
  }
  CREATE_REQUEST(ImportContactsRequest, std::move(contacts));
}

void Td::on_request(uint64 id, const td_api::getContacts &request) {
  CHECK_IS_USER();
  CREATE_REQUEST(SearchContactsRequest, string(), 1000000);
}

void Td::on_request(uint64 id, td_api::searchContacts &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.query_);
  CREATE_REQUEST(SearchContactsRequest, request.query_, request.limit_);
}

void Td::on_request(uint64 id, td_api::removeContacts &request) {
  CHECK_IS_USER();
  CREATE_REQUEST(RemoveContactsRequest, UserId::get_user_ids(request.user_ids_));
}

void Td::on_request(uint64 id, const td_api::getImportedContactCount &request) {
  CHECK_IS_USER();
  CREATE_NO_ARGS_REQUEST(GetImportedContactCountRequest);
}

void Td::on_request(uint64 id, td_api::changeImportedContacts &request) {
  CHECK_IS_USER();
  vector<Contact> contacts;
  contacts.reserve(request.contacts_.size());
  for (auto &contact : request.contacts_) {
    auto r_contact = get_contact(this, std::move(contact));
    if (r_contact.is_error()) {
      return send_closure(actor_id(this), &Td::send_error, id, r_contact.move_as_error());
    }
    contacts.push_back(r_contact.move_as_ok());
  }
  CREATE_REQUEST(ChangeImportedContactsRequest, std::move(contacts));
}

void Td::on_request(uint64 id, const td_api::clearImportedContacts &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  user_manager_->clear_imported_contacts(std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getCloseFriends &request) {
  CHECK_IS_USER();
  CREATE_NO_ARGS_REQUEST(GetCloseFriendsRequest);
}

void Td::on_request(uint64 id, const td_api::setCloseFriends &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  user_manager_->set_close_friends(UserId::get_user_ids(request.user_ids_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::setUserPersonalProfilePhoto &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  user_manager_->set_user_profile_photo(UserId(request.user_id_), request.photo_, false, std::move(promise));
}

void Td::on_request(uint64 id, td_api::suggestUserProfilePhoto &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  user_manager_->set_user_profile_photo(UserId(request.user_id_), request.photo_, true, std::move(promise));
}

void Td::on_request(uint64 id, td_api::searchUserByPhoneNumber &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.phone_number_);
  CREATE_REQUEST(SearchUserByPhoneNumberRequest, std::move(request.phone_number_), request.only_local_);
}

void Td::on_request(uint64 id, const td_api::sharePhoneNumber &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  user_manager_->share_phone_number(UserId(request.user_id_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getRecentInlineBots &request) {
  CHECK_IS_USER();
  CREATE_NO_ARGS_REQUEST(GetRecentInlineBotsRequest);
}

void Td::on_request(uint64 id, td_api::setName &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.first_name_);
  CLEAN_INPUT_STRING(request.last_name_);
  CREATE_OK_REQUEST_PROMISE();
  user_manager_->set_name(request.first_name_, request.last_name_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::setBio &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.bio_);
  CREATE_OK_REQUEST_PROMISE();
  user_manager_->set_bio(request.bio_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::setUsername &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.username_);
  CREATE_OK_REQUEST_PROMISE();
  user_manager_->set_username(request.username_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::toggleUsernameIsActive &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.username_);
  CREATE_OK_REQUEST_PROMISE();
  user_manager_->toggle_username_is_active(std::move(request.username_), request.is_active_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::reorderActiveUsernames &request) {
  CHECK_IS_USER();
  for (auto &username : request.usernames_) {
    CLEAN_INPUT_STRING(username);
  }
  CREATE_OK_REQUEST_PROMISE();
  user_manager_->reorder_usernames(std::move(request.usernames_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::setBirthdate &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  user_manager_->set_birthdate(Birthdate(std::move(request.birthdate_)), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::setPersonalChat &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  user_manager_->set_personal_channel(DialogId(request.chat_id_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::setEmojiStatus &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  user_manager_->set_emoji_status(EmojiStatus(request.emoji_status_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::toggleHasSponsoredMessagesEnabled &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  user_manager_->toggle_sponsored_messages(request.has_sponsored_messages_enabled_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getThemedEmojiStatuses &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  stickers_manager_->get_default_emoji_statuses(false, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getThemedChatEmojiStatuses &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  stickers_manager_->get_default_channel_emoji_statuses(false, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getDefaultEmojiStatuses &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  get_default_emoji_statuses(this, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getDefaultChatEmojiStatuses &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  get_default_channel_emoji_statuses(this, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getRecentEmojiStatuses &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  get_recent_emoji_statuses(this, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::clearRecentEmojiStatuses &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  clear_recent_emoji_statuses(this, std::move(promise));
}

void Td::on_request(uint64 id, td_api::setCommands &request) {
  CHECK_IS_BOT();
  CREATE_OK_REQUEST_PROMISE();
  set_commands(this, std::move(request.scope_), std::move(request.language_code_), std::move(request.commands_),
               std::move(promise));
}

void Td::on_request(uint64 id, td_api::deleteCommands &request) {
  CHECK_IS_BOT();
  CREATE_OK_REQUEST_PROMISE();
  delete_commands(this, std::move(request.scope_), std::move(request.language_code_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::getCommands &request) {
  CHECK_IS_BOT();
  CREATE_REQUEST_PROMISE();
  get_commands(this, std::move(request.scope_), std::move(request.language_code_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::setMenuButton &request) {
  CHECK_IS_BOT();
  CREATE_OK_REQUEST_PROMISE();
  set_menu_button(this, UserId(request.user_id_), std::move(request.menu_button_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getMenuButton &request) {
  CHECK_IS_BOT();
  CREATE_REQUEST_PROMISE();
  get_menu_button(this, UserId(request.user_id_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::setDefaultGroupAdministratorRights &request) {
  CHECK_IS_BOT();
  CREATE_OK_REQUEST_PROMISE();
  bot_info_manager_->set_default_group_administrator_rights(
      AdministratorRights(request.default_group_administrator_rights_, ChannelType::Megagroup), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::setDefaultChannelAdministratorRights &request) {
  CHECK_IS_BOT();
  CREATE_OK_REQUEST_PROMISE();
  bot_info_manager_->set_default_channel_administrator_rights(
      AdministratorRights(request.default_channel_administrator_rights_, ChannelType::Broadcast), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::canBotSendMessages &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  bot_info_manager_->can_bot_send_messages(UserId(request.bot_user_id_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::allowBotToSendMessages &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  bot_info_manager_->allow_bot_to_send_messages(UserId(request.bot_user_id_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::sendWebAppCustomRequest &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.method_);
  CLEAN_INPUT_STRING(request.parameters_);
  CREATE_REQUEST_PROMISE();
  attach_menu_manager_->invoke_web_view_custom_method(UserId(request.bot_user_id_), request.method_,
                                                      request.parameters_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::setBotName &request) {
  CLEAN_INPUT_STRING(request.name_);
  CREATE_OK_REQUEST_PROMISE();
  bot_info_manager_->set_bot_name(UserId(request.bot_user_id_), request.language_code_, request.name_,
                                  std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getBotName &request) {
  CREATE_REQUEST_PROMISE();
  auto query_promise = PromiseCreator::lambda([promise = std::move(promise)](Result<string> result) mutable {
    if (result.is_error()) {
      promise.set_error(result.move_as_error());
    } else {
      promise.set_value(td_api::make_object<td_api::text>(result.move_as_ok()));
    }
  });
  bot_info_manager_->get_bot_name(UserId(request.bot_user_id_), request.language_code_, std::move(query_promise));
}

void Td::on_request(uint64 id, td_api::setBotProfilePhoto &request) {
  CREATE_OK_REQUEST_PROMISE();
  user_manager_->set_bot_profile_photo(UserId(request.bot_user_id_), request.photo_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::toggleBotUsernameIsActive &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.username_);
  CREATE_OK_REQUEST_PROMISE();
  user_manager_->toggle_bot_username_is_active(UserId(request.bot_user_id_), std::move(request.username_),
                                               request.is_active_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::reorderBotActiveUsernames &request) {
  CHECK_IS_USER();
  for (auto &username : request.usernames_) {
    CLEAN_INPUT_STRING(username);
  }
  CREATE_OK_REQUEST_PROMISE();
  user_manager_->reorder_bot_usernames(UserId(request.bot_user_id_), std::move(request.usernames_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::setBotInfoDescription &request) {
  CLEAN_INPUT_STRING(request.description_);
  CREATE_OK_REQUEST_PROMISE();
  bot_info_manager_->set_bot_info_description(UserId(request.bot_user_id_), request.language_code_,
                                              request.description_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getBotInfoDescription &request) {
  CREATE_REQUEST_PROMISE();
  auto query_promise = PromiseCreator::lambda([promise = std::move(promise)](Result<string> result) mutable {
    if (result.is_error()) {
      promise.set_error(result.move_as_error());
    } else {
      promise.set_value(td_api::make_object<td_api::text>(result.move_as_ok()));
    }
  });
  bot_info_manager_->get_bot_info_description(UserId(request.bot_user_id_), request.language_code_,
                                              std::move(query_promise));
}

void Td::on_request(uint64 id, td_api::setBotInfoShortDescription &request) {
  CLEAN_INPUT_STRING(request.short_description_);
  CREATE_OK_REQUEST_PROMISE();
  bot_info_manager_->set_bot_info_about(UserId(request.bot_user_id_), request.language_code_,
                                        request.short_description_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getBotInfoShortDescription &request) {
  CREATE_REQUEST_PROMISE();
  auto query_promise = PromiseCreator::lambda([promise = std::move(promise)](Result<string> result) mutable {
    if (result.is_error()) {
      promise.set_error(result.move_as_error());
    } else {
      promise.set_value(td_api::make_object<td_api::text>(result.move_as_ok()));
    }
  });
  bot_info_manager_->get_bot_info_about(UserId(request.bot_user_id_), request.language_code_, std::move(query_promise));
}

void Td::on_request(uint64 id, const td_api::setLocation &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  people_nearby_manager_->set_location(Location(request.location_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::setBusinessLocation &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  business_manager_->set_business_location(DialogLocation(std::move(request.location_)), std::move(promise));
}

void Td::on_request(uint64 id, td_api::setBusinessOpeningHours &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  business_manager_->set_business_work_hours(BusinessWorkHours(std::move(request.opening_hours_)), std::move(promise));
}

void Td::on_request(uint64 id, td_api::setBusinessGreetingMessageSettings &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  business_manager_->set_business_greeting_message(
      BusinessGreetingMessage(std::move(request.greeting_message_settings_)), std::move(promise));
}

void Td::on_request(uint64 id, td_api::setBusinessAwayMessageSettings &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  business_manager_->set_business_away_message(BusinessAwayMessage(std::move(request.away_message_settings_)),
                                               std::move(promise));
}

void Td::on_request(uint64 id, td_api::setBusinessStartPage &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  business_manager_->set_business_intro(BusinessIntro(this, std::move(request.start_page_)), std::move(promise));
}

void Td::on_request(uint64 id, td_api::setProfilePhoto &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  user_manager_->set_profile_photo(request.photo_, request.is_public_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::deleteProfilePhoto &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  user_manager_->delete_profile_photo(request.profile_photo_id_, false, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getUserProfilePhotos &request) {
  CREATE_REQUEST_PROMISE();
  user_manager_->get_user_profile_photos(UserId(request.user_id_), request.offset_, request.limit_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::setAccentColor &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  user_manager_->set_accent_color(AccentColorId(request.accent_color_id_),
                                  CustomEmojiId(request.background_custom_emoji_id_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::setProfileAccentColor &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  user_manager_->set_profile_accent_color(AccentColorId(request.profile_accent_color_id_),
                                          CustomEmojiId(request.profile_background_custom_emoji_id_),
                                          std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getBusinessConnectedBot &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  business_manager_->get_business_connected_bot(std::move(promise));
}

void Td::on_request(uint64 id, td_api::setBusinessConnectedBot &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  business_manager_->set_business_connected_bot(std::move(request.bot_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::deleteBusinessConnectedBot &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  business_manager_->delete_business_connected_bot(UserId(request.bot_user_id_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::toggleBusinessConnectedBotChatIsPaused &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  business_manager_->toggle_business_connected_bot_dialog_is_paused(DialogId(request.chat_id_), request.is_paused_,
                                                                    std::move(promise));
}

void Td::on_request(uint64 id, const td_api::removeBusinessConnectedBotFromChat &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  business_manager_->remove_business_connected_bot_from_dialog(DialogId(request.chat_id_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getBusinessChatLinks &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  business_manager_->get_business_chat_links(std::move(promise));
}

void Td::on_request(uint64 id, td_api::createBusinessChatLink &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  business_manager_->create_business_chat_link(std::move(request.link_info_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::editBusinessChatLink &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.link_);
  CREATE_REQUEST_PROMISE();
  business_manager_->edit_business_chat_link(request.link_, std::move(request.link_info_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::deleteBusinessChatLink &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.link_);
  CREATE_OK_REQUEST_PROMISE();
  business_manager_->delete_business_chat_link(request.link_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::getBusinessChatLinkInfo &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.link_name_);
  CREATE_REQUEST_PROMISE();
  business_manager_->get_business_chat_link_info(request.link_name_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::setSupergroupUsername &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.username_);
  CREATE_OK_REQUEST_PROMISE();
  chat_manager_->set_channel_username(ChannelId(request.supergroup_id_), request.username_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::toggleSupergroupUsernameIsActive &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.username_);
  CREATE_OK_REQUEST_PROMISE();
  chat_manager_->toggle_channel_username_is_active(ChannelId(request.supergroup_id_), std::move(request.username_),
                                                   request.is_active_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::disableAllSupergroupUsernames &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  chat_manager_->disable_all_channel_usernames(ChannelId(request.supergroup_id_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::reorderSupergroupActiveUsernames &request) {
  CHECK_IS_USER();
  for (auto &username : request.usernames_) {
    CLEAN_INPUT_STRING(username);
  }
  CREATE_OK_REQUEST_PROMISE();
  chat_manager_->reorder_channel_usernames(ChannelId(request.supergroup_id_), std::move(request.usernames_),
                                           std::move(promise));
}

void Td::on_request(uint64 id, const td_api::setSupergroupStickerSet &request) {
  CREATE_OK_REQUEST_PROMISE();
  chat_manager_->set_channel_sticker_set(ChannelId(request.supergroup_id_), StickerSetId(request.sticker_set_id_),
                                         std::move(promise));
}

void Td::on_request(uint64 id, const td_api::setSupergroupCustomEmojiStickerSet &request) {
  CREATE_OK_REQUEST_PROMISE();
  chat_manager_->set_channel_emoji_sticker_set(ChannelId(request.supergroup_id_),
                                               StickerSetId(request.custom_emoji_sticker_set_id_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::setSupergroupUnrestrictBoostCount &request) {
  CREATE_OK_REQUEST_PROMISE();
  chat_manager_->set_channel_unrestrict_boost_count(ChannelId(request.supergroup_id_), request.unrestrict_boost_count_,
                                                    std::move(promise));
}

void Td::on_request(uint64 id, const td_api::toggleSupergroupSignMessages &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  chat_manager_->toggle_channel_sign_messages(ChannelId(request.supergroup_id_), request.sign_messages_,
                                              std::move(promise));
}

void Td::on_request(uint64 id, const td_api::toggleSupergroupJoinToSendMessages &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  chat_manager_->toggle_channel_join_to_send(ChannelId(request.supergroup_id_), request.join_to_send_messages_,
                                             std::move(promise));
}

void Td::on_request(uint64 id, const td_api::toggleSupergroupJoinByRequest &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  chat_manager_->toggle_channel_join_request(ChannelId(request.supergroup_id_), request.join_by_request_,
                                             std::move(promise));
}

void Td::on_request(uint64 id, const td_api::toggleSupergroupIsAllHistoryAvailable &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  chat_manager_->toggle_channel_is_all_history_available(ChannelId(request.supergroup_id_),
                                                         request.is_all_history_available_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::toggleSupergroupCanHaveSponsoredMessages &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  chat_manager_->toggle_channel_can_have_sponsored_messages(ChannelId(request.supergroup_id_),
                                                            request.can_have_sponsored_messages_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::toggleSupergroupHasHiddenMembers &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  chat_manager_->toggle_channel_has_hidden_participants(ChannelId(request.supergroup_id_), request.has_hidden_members_,
                                                        std::move(promise));
}

void Td::on_request(uint64 id, const td_api::toggleSupergroupHasAggressiveAntiSpamEnabled &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  chat_manager_->toggle_channel_has_aggressive_anti_spam_enabled(
      ChannelId(request.supergroup_id_), request.has_aggressive_anti_spam_enabled_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::toggleSupergroupIsForum &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  chat_manager_->toggle_channel_is_forum(ChannelId(request.supergroup_id_), request.is_forum_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::toggleSupergroupIsBroadcastGroup &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  chat_manager_->convert_channel_to_gigagroup(ChannelId(request.supergroup_id_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::reportSupergroupSpam &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  chat_manager_->report_channel_spam(ChannelId(request.supergroup_id_),
                                     MessageId::get_message_ids(request.message_ids_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::reportSupergroupAntiSpamFalsePositive &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  chat_manager_->report_channel_anti_spam_false_positive(ChannelId(request.supergroup_id_),
                                                         MessageId(request.message_id_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::getSupergroupMembers &request) {
  CREATE_REQUEST_PROMISE();
  auto query_promise =
      PromiseCreator::lambda([promise = std::move(promise), td = this](Result<DialogParticipants> result) mutable {
        if (result.is_error()) {
          promise.set_error(result.move_as_error());
        } else {
          promise.set_value(result.ok().get_chat_members_object(td, "getSupergroupMembers"));
        }
      });
  dialog_participant_manager_->get_channel_participants(ChannelId(request.supergroup_id_), std::move(request.filter_),
                                                        string(), request.offset_, request.limit_, -1,
                                                        std::move(query_promise));
}

void Td::on_request(uint64 id, td_api::closeSecretChat &request) {
  CREATE_OK_REQUEST_PROMISE();
  send_closure(secret_chats_manager_, &SecretChatsManager::cancel_chat, SecretChatId(request.secret_chat_id_), false,
               std::move(promise));
}

void Td::on_request(uint64 id, td_api::getStickers &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.query_);
  CREATE_REQUEST(GetStickersRequest, get_sticker_type(request.sticker_type_), std::move(request.query_), request.limit_,
                 request.chat_id_);
}

void Td::on_request(uint64 id, td_api::getAllStickerEmojis &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.query_);
  CREATE_REQUEST(GetAllStickerEmojisRequest, get_sticker_type(request.sticker_type_), std::move(request.query_),
                 request.chat_id_, request.return_only_main_emoji_);
}

void Td::on_request(uint64 id, td_api::searchStickers &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.emojis_);
  CREATE_REQUEST_PROMISE();
  auto sticker_type = get_sticker_type(request.sticker_type_);
  if (sticker_type == StickerType::Regular) {
    // legacy
    if (request.emojis_ == "⭐️⭐️") {
      request.emojis_ = "⭐️";
    } else if (request.emojis_ == "📂⭐️") {
      request.emojis_ = "📂";
    } else if (request.emojis_ == "👋⭐️") {
      request.emojis_ = "👋";
    }
  }
  stickers_manager_->search_stickers(sticker_type, std::move(request.emojis_), request.limit_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getGreetingStickers &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  stickers_manager_->search_stickers(StickerType::Regular, "👋⭐️", 100, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getPremiumStickers &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  stickers_manager_->get_premium_stickers(request.limit_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getInstalledStickerSets &request) {
  CHECK_IS_USER();
  CREATE_REQUEST(GetInstalledStickerSetsRequest, get_sticker_type(request.sticker_type_));
}

void Td::on_request(uint64 id, const td_api::getArchivedStickerSets &request) {
  CHECK_IS_USER();
  CREATE_REQUEST(GetArchivedStickerSetsRequest, get_sticker_type(request.sticker_type_), request.offset_sticker_set_id_,
                 request.limit_);
}

void Td::on_request(uint64 id, const td_api::getTrendingStickerSets &request) {
  CHECK_IS_USER();
  CREATE_REQUEST(GetTrendingStickerSetsRequest, get_sticker_type(request.sticker_type_), request.offset_,
                 request.limit_);
}

void Td::on_request(uint64 id, const td_api::getAttachedStickerSets &request) {
  CHECK_IS_USER();
  CREATE_REQUEST(GetAttachedStickerSetsRequest, request.file_id_);
}

void Td::on_request(uint64 id, const td_api::getStickerSet &request) {
  CREATE_REQUEST(GetStickerSetRequest, request.set_id_);
}

void Td::on_request(uint64 id, td_api::searchStickerSet &request) {
  CLEAN_INPUT_STRING(request.name_);
  CREATE_REQUEST(SearchStickerSetRequest, std::move(request.name_));
}

void Td::on_request(uint64 id, td_api::searchInstalledStickerSets &request) {
  CLEAN_INPUT_STRING(request.query_);
  CREATE_REQUEST(SearchInstalledStickerSetsRequest, get_sticker_type(request.sticker_type_), std::move(request.query_),
                 request.limit_);
}

void Td::on_request(uint64 id, td_api::searchStickerSets &request) {
  CLEAN_INPUT_STRING(request.query_);
  CREATE_REQUEST(SearchStickerSetsRequest, get_sticker_type(request.sticker_type_), std::move(request.query_));
}

void Td::on_request(uint64 id, const td_api::changeStickerSet &request) {
  CHECK_IS_USER();
  CREATE_REQUEST(ChangeStickerSetRequest, request.set_id_, request.is_installed_, request.is_archived_);
}

void Td::on_request(uint64 id, const td_api::viewTrendingStickerSets &request) {
  CHECK_IS_USER();
  stickers_manager_->view_featured_sticker_sets(StickersManager::convert_sticker_set_ids(request.sticker_set_ids_));
  send_closure(actor_id(this), &Td::send_result, id, make_tl_object<td_api::ok>());
}

void Td::on_request(uint64 id, td_api::reorderInstalledStickerSets &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  stickers_manager_->reorder_installed_sticker_sets(get_sticker_type(request.sticker_type_),
                                                    StickersManager::convert_sticker_set_ids(request.sticker_set_ids_),
                                                    std::move(promise));
}

void Td::on_request(uint64 id, td_api::uploadStickerFile &request) {
  CREATE_REQUEST(UploadStickerFileRequest, request.user_id_, get_sticker_format(request.sticker_format_),
                 std::move(request.sticker_));
}

void Td::on_request(uint64 id, td_api::getSuggestedStickerSetName &request) {
  CLEAN_INPUT_STRING(request.title_);
  CREATE_REQUEST_PROMISE();
  auto query_promise = PromiseCreator::lambda([promise = std::move(promise)](Result<string> result) mutable {
    if (result.is_error()) {
      promise.set_error(result.move_as_error());
    } else {
      promise.set_value(make_tl_object<td_api::text>(result.move_as_ok()));
    }
  });
  stickers_manager_->get_suggested_sticker_set_name(std::move(request.title_), std::move(query_promise));
}

void Td::on_request(uint64 id, td_api::checkStickerSetName &request) {
  CLEAN_INPUT_STRING(request.name_);
  CREATE_REQUEST_PROMISE();
  auto query_promise = PromiseCreator::lambda(
      [promise = std::move(promise)](Result<StickersManager::CheckStickerSetNameResult> result) mutable {
        if (result.is_error()) {
          promise.set_error(result.move_as_error());
        } else {
          promise.set_value(StickersManager::get_check_sticker_set_name_result_object(result.ok()));
        }
      });
  stickers_manager_->check_sticker_set_name(request.name_, std::move(query_promise));
}

void Td::on_request(uint64 id, td_api::createNewStickerSet &request) {
  CLEAN_INPUT_STRING(request.title_);
  CLEAN_INPUT_STRING(request.name_);
  CLEAN_INPUT_STRING(request.source_);
  CREATE_REQUEST_PROMISE();
  stickers_manager_->create_new_sticker_set(UserId(request.user_id_), std::move(request.title_),
                                            std::move(request.name_), get_sticker_type(request.sticker_type_),
                                            request.needs_repainting_, std::move(request.stickers_),
                                            std::move(request.source_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::addStickerToSet &request) {
  CLEAN_INPUT_STRING(request.name_);
  CREATE_OK_REQUEST_PROMISE();
  stickers_manager_->add_sticker_to_set(UserId(request.user_id_), std::move(request.name_), std::move(request.sticker_),
                                        nullptr, std::move(promise));
}

void Td::on_request(uint64 id, td_api::replaceStickerInSet &request) {
  CLEAN_INPUT_STRING(request.name_);
  if (request.old_sticker_ == nullptr) {
    return send_error_raw(id, 400, "Old sticker must be non-empty");
  }
  CREATE_OK_REQUEST_PROMISE();
  stickers_manager_->add_sticker_to_set(UserId(request.user_id_), std::move(request.name_),
                                        std::move(request.new_sticker_), std::move(request.old_sticker_),
                                        std::move(promise));
}

void Td::on_request(uint64 id, td_api::setStickerSetThumbnail &request) {
  CLEAN_INPUT_STRING(request.name_);
  CREATE_OK_REQUEST_PROMISE();
  stickers_manager_->set_sticker_set_thumbnail(UserId(request.user_id_), std::move(request.name_),
                                               std::move(request.thumbnail_), get_sticker_format(request.format_),
                                               std::move(promise));
}

void Td::on_request(uint64 id, td_api::setCustomEmojiStickerSetThumbnail &request) {
  CLEAN_INPUT_STRING(request.name_);
  CREATE_OK_REQUEST_PROMISE();
  stickers_manager_->set_custom_emoji_sticker_set_thumbnail(
      std::move(request.name_), CustomEmojiId(request.custom_emoji_id_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::setStickerSetTitle &request) {
  CLEAN_INPUT_STRING(request.name_);
  CLEAN_INPUT_STRING(request.title_);
  CREATE_OK_REQUEST_PROMISE();
  stickers_manager_->set_sticker_set_title(std::move(request.name_), std::move(request.title_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::deleteStickerSet &request) {
  CLEAN_INPUT_STRING(request.name_);
  CREATE_OK_REQUEST_PROMISE();
  stickers_manager_->delete_sticker_set(std::move(request.name_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::setStickerPositionInSet &request) {
  CREATE_OK_REQUEST_PROMISE();
  stickers_manager_->set_sticker_position_in_set(request.sticker_, request.position_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::removeStickerFromSet &request) {
  CREATE_OK_REQUEST_PROMISE();
  stickers_manager_->remove_sticker_from_set(request.sticker_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::setStickerEmojis &request) {
  CLEAN_INPUT_STRING(request.emojis_);
  CREATE_OK_REQUEST_PROMISE();
  stickers_manager_->set_sticker_emojis(request.sticker_, request.emojis_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::setStickerKeywords &request) {
  for (auto &keyword : request.keywords_) {
    CLEAN_INPUT_STRING(keyword);
  }
  CREATE_OK_REQUEST_PROMISE();
  stickers_manager_->set_sticker_keywords(request.sticker_, std::move(request.keywords_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::setStickerMaskPosition &request) {
  CREATE_OK_REQUEST_PROMISE();
  stickers_manager_->set_sticker_mask_position(request.sticker_, std::move(request.mask_position_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getOwnedStickerSets &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  stickers_manager_->get_created_sticker_sets(StickerSetId(request.offset_sticker_set_id_), request.limit_,
                                              std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getRecentStickers &request) {
  CHECK_IS_USER();
  CREATE_REQUEST(GetRecentStickersRequest, request.is_attached_);
}

void Td::on_request(uint64 id, td_api::addRecentSticker &request) {
  CHECK_IS_USER();
  CREATE_REQUEST(AddRecentStickerRequest, request.is_attached_, std::move(request.sticker_));
}

void Td::on_request(uint64 id, td_api::removeRecentSticker &request) {
  CHECK_IS_USER();
  CREATE_REQUEST(RemoveRecentStickerRequest, request.is_attached_, std::move(request.sticker_));
}

void Td::on_request(uint64 id, td_api::clearRecentStickers &request) {
  CHECK_IS_USER();
  CREATE_REQUEST(ClearRecentStickersRequest, request.is_attached_);
}

void Td::on_request(uint64 id, const td_api::getFavoriteStickers &request) {
  CHECK_IS_USER();
  CREATE_NO_ARGS_REQUEST(GetFavoriteStickersRequest);
}

void Td::on_request(uint64 id, td_api::addFavoriteSticker &request) {
  CHECK_IS_USER();
  CREATE_REQUEST(AddFavoriteStickerRequest, std::move(request.sticker_));
}

void Td::on_request(uint64 id, td_api::removeFavoriteSticker &request) {
  CHECK_IS_USER();
  CREATE_REQUEST(RemoveFavoriteStickerRequest, std::move(request.sticker_));
}

void Td::on_request(uint64 id, td_api::getStickerEmojis &request) {
  CHECK_IS_USER();
  CREATE_REQUEST(GetStickerEmojisRequest, std::move(request.sticker_));
}

void Td::on_request(uint64 id, td_api::searchEmojis &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.text_);
  for (auto &input_language_code : request.input_language_codes_) {
    CLEAN_INPUT_STRING(input_language_code);
  }
  CREATE_REQUEST(SearchEmojisRequest, std::move(request.text_), std::move(request.input_language_codes_));
}

void Td::on_request(uint64 id, td_api::getKeywordEmojis &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.text_);
  for (auto &input_language_code : request.input_language_codes_) {
    CLEAN_INPUT_STRING(input_language_code);
  }
  CREATE_REQUEST(GetKeywordEmojisRequest, std::move(request.text_), std::move(request.input_language_codes_));
}

void Td::on_request(uint64 id, const td_api::getEmojiCategories &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  stickers_manager_->get_emoji_groups(get_emoji_group_type(request.type_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::getAnimatedEmoji &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.emoji_);
  CREATE_REQUEST_PROMISE();
  stickers_manager_->get_animated_emoji(std::move(request.emoji_), false, std::move(promise));
}

void Td::on_request(uint64 id, td_api::getEmojiSuggestionsUrl &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.language_code_);
  CREATE_REQUEST(GetEmojiSuggestionsUrlRequest, std::move(request.language_code_));
}

void Td::on_request(uint64 id, const td_api::getCustomEmojiStickers &request) {
  CREATE_REQUEST_PROMISE();
  stickers_manager_->get_custom_emoji_stickers(CustomEmojiId::get_custom_emoji_ids(request.custom_emoji_ids_), true,
                                               std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getDefaultChatPhotoCustomEmojiStickers &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  stickers_manager_->get_default_custom_emoji_stickers(StickerListType::DialogPhoto, false, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getDefaultProfilePhotoCustomEmojiStickers &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  stickers_manager_->get_default_custom_emoji_stickers(StickerListType::UserProfilePhoto, false, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getDefaultBackgroundCustomEmojiStickers &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  stickers_manager_->get_default_custom_emoji_stickers(StickerListType::Background, false, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getDisallowedChatEmojiStatuses &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  stickers_manager_->get_sticker_list_emoji_statuses(StickerListType::DisallowedChannelEmojiStatus, false,
                                                     std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getSavedAnimations &request) {
  CHECK_IS_USER();
  CREATE_NO_ARGS_REQUEST(GetSavedAnimationsRequest);
}

void Td::on_request(uint64 id, td_api::addSavedAnimation &request) {
  CHECK_IS_USER();
  CREATE_REQUEST(AddSavedAnimationRequest, std::move(request.animation_));
}

void Td::on_request(uint64 id, td_api::removeSavedAnimation &request) {
  CHECK_IS_USER();
  CREATE_REQUEST(RemoveSavedAnimationRequest, std::move(request.animation_));
}

void Td::on_request(uint64 id, const td_api::getSavedNotificationSound &request) {
  CHECK_IS_USER();
  CREATE_REQUEST(GetSavedNotificationSoundRequest, request.notification_sound_id_);
}

void Td::on_request(uint64 id, const td_api::getSavedNotificationSounds &request) {
  CHECK_IS_USER();
  CREATE_NO_ARGS_REQUEST(GetSavedNotificationSoundsRequest);
}

void Td::on_request(uint64 id, td_api::addSavedNotificationSound &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  notification_settings_manager_->add_saved_ringtone(std::move(request.sound_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::removeSavedNotificationSound &request) {
  CHECK_IS_USER();
  CREATE_REQUEST(RemoveSavedNotificationSoundRequest, request.notification_sound_id_);
}

void Td::on_request(uint64 id, const td_api::getChatNotificationSettingsExceptions &request) {
  CHECK_IS_USER();
  bool filter_scope = false;
  NotificationSettingsScope scope = NotificationSettingsScope::Private;
  if (request.scope_ != nullptr) {
    filter_scope = true;
    scope = get_notification_settings_scope(request.scope_);
  }
  CREATE_REQUEST(GetChatNotificationSettingsExceptionsRequest, scope, filter_scope, request.compare_sound_);
}

void Td::on_request(uint64 id, const td_api::getScopeNotificationSettings &request) {
  CHECK_IS_USER();
  if (request.scope_ == nullptr) {
    return send_error_raw(id, 400, "Scope must be non-empty");
  }
  CREATE_REQUEST(GetScopeNotificationSettingsRequest, get_notification_settings_scope(request.scope_));
}

void Td::on_request(uint64 id, const td_api::removeChatActionBar &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->remove_dialog_action_bar(DialogId(request.chat_id_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::reportChat &request) {
  CHECK_IS_USER();
  auto r_report_reason = ReportReason::get_report_reason(std::move(request.reason_), std::move(request.text_));
  if (r_report_reason.is_error()) {
    return send_error_raw(id, r_report_reason.error().code(), r_report_reason.error().message());
  }
  CREATE_OK_REQUEST_PROMISE();
  dialog_manager_->report_dialog(DialogId(request.chat_id_), MessageId::get_message_ids(request.message_ids_),
                                 r_report_reason.move_as_ok(), std::move(promise));
}

void Td::on_request(uint64 id, td_api::reportChatPhoto &request) {
  CHECK_IS_USER();
  auto r_report_reason = ReportReason::get_report_reason(std::move(request.reason_), std::move(request.text_));
  if (r_report_reason.is_error()) {
    return send_error_raw(id, r_report_reason.error().code(), r_report_reason.error().message());
  }
  CREATE_OK_REQUEST_PROMISE();
  dialog_manager_->report_dialog_photo(DialogId(request.chat_id_), FileId(request.file_id_, 0),
                                       r_report_reason.move_as_ok(), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::reportMessageReactions &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  TRY_RESULT_PROMISE(promise, sender_dialog_id, get_message_sender_dialog_id(this, request.sender_id_, false, false));
  report_message_reactions(this, {DialogId(request.chat_id_), MessageId(request.message_id_)}, sender_dialog_id,
                           std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getChatStatistics &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  statistics_manager_->get_channel_statistics(DialogId(request.chat_id_), request.is_dark_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getChatRevenueStatistics &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  statistics_manager_->get_channel_revenue_statistics(DialogId(request.chat_id_), request.is_dark_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getChatRevenueWithdrawalUrl &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  auto query_promise = PromiseCreator::lambda([promise = std::move(promise)](Result<string> result) mutable {
    if (result.is_error()) {
      promise.set_error(result.move_as_error());
    } else {
      promise.set_value(td_api::make_object<td_api::httpUrl>(result.move_as_ok()));
    }
  });
  statistics_manager_->get_channel_revenue_withdrawal_url(DialogId(request.chat_id_), request.password_,
                                                          std::move(query_promise));
}

void Td::on_request(uint64 id, const td_api::getChatRevenueTransactions &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  statistics_manager_->get_channel_revenue_transactions(DialogId(request.chat_id_), request.offset_, request.limit_,
                                                        std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getStarRevenueStatistics &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  star_manager_->get_star_revenue_statistics(request.owner_id_, request.is_dark_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getStarWithdrawalUrl &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  auto query_promise = PromiseCreator::lambda([promise = std::move(promise)](Result<string> result) mutable {
    if (result.is_error()) {
      promise.set_error(result.move_as_error());
    } else {
      promise.set_value(td_api::make_object<td_api::httpUrl>(result.move_as_ok()));
    }
  });
  star_manager_->get_star_withdrawal_url(request.owner_id_, request.star_count_, request.password_,
                                         std::move(query_promise));
}

void Td::on_request(uint64 id, const td_api::getMessageStatistics &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  statistics_manager_->get_channel_message_statistics({DialogId(request.chat_id_), MessageId(request.message_id_)},
                                                      request.is_dark_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getStoryStatistics &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  statistics_manager_->get_channel_story_statistics({DialogId(request.chat_id_), StoryId(request.story_id_)},
                                                    request.is_dark_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::getStatisticalGraph &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.token_);
  CREATE_REQUEST_PROMISE();
  statistics_manager_->load_statistics_graph(DialogId(request.chat_id_), std::move(request.token_), request.x_,
                                             std::move(promise));
}

void Td::on_request(uint64 id, td_api::setChatNotificationSettings &request) {
  CHECK_IS_USER();
  answer_ok_query(id, messages_manager_->set_dialog_notification_settings(DialogId(request.chat_id_),
                                                                          std::move(request.notification_settings_)));
}

void Td::on_request(uint64 id, td_api::setForumTopicNotificationSettings &request) {
  CHECK_IS_USER();
  answer_ok_query(id, forum_topic_manager_->set_forum_topic_notification_settings(
                          DialogId(request.chat_id_), MessageId(request.message_thread_id_),
                          std::move(request.notification_settings_)));
}

void Td::on_request(uint64 id, td_api::setScopeNotificationSettings &request) {
  CHECK_IS_USER();
  if (request.scope_ == nullptr) {
    return send_error_raw(id, 400, "Scope must be non-empty");
  }
  answer_ok_query(id, notification_settings_manager_->set_scope_notification_settings(
                          get_notification_settings_scope(request.scope_), std::move(request.notification_settings_)));
}

void Td::on_request(uint64 id, td_api::setReactionNotificationSettings &request) {
  CHECK_IS_USER();
  answer_ok_query(id, notification_settings_manager_->set_reaction_notification_settings(
                          ReactionNotificationSettings(std::move(request.notification_settings_))));
}

void Td::on_request(uint64 id, const td_api::resetAllNotificationSettings &request) {
  CHECK_IS_USER();
  messages_manager_->reset_all_notification_settings();
  send_closure(actor_id(this), &Td::send_result, id, make_tl_object<td_api::ok>());
}

void Td::on_request(uint64 id, const td_api::getMapThumbnailFile &request) {
  DialogId dialog_id(request.chat_id_);
  if (!dialog_manager_->have_dialog_force(dialog_id, "getMapThumbnailFile")) {
    dialog_id = DialogId();
  }

  auto r_file_id = file_manager_->get_map_thumbnail_file_id(Location(request.location_), request.zoom_, request.width_,
                                                            request.height_, request.scale_, dialog_id);
  if (r_file_id.is_error()) {
    send_closure(actor_id(this), &Td::send_error, id, r_file_id.move_as_error());
  } else {
    send_closure(actor_id(this), &Td::send_result, id, file_manager_->get_file_object(r_file_id.ok()));
  }
}

void Td::on_request(uint64 id, const td_api::getLocalizationTargetInfo &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  send_closure(language_pack_manager_, &LanguagePackManager::get_languages, request.only_local_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::getLanguagePackInfo &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.language_pack_id_);
  CREATE_REQUEST_PROMISE();
  send_closure(language_pack_manager_, &LanguagePackManager::search_language_info, request.language_pack_id_,
               std::move(promise));
}

void Td::on_request(uint64 id, td_api::getLanguagePackStrings &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.language_pack_id_);
  for (auto &key : request.keys_) {
    CLEAN_INPUT_STRING(key);
  }
  CREATE_REQUEST_PROMISE();
  send_closure(language_pack_manager_, &LanguagePackManager::get_language_pack_strings,
               std::move(request.language_pack_id_), std::move(request.keys_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::synchronizeLanguagePack &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.language_pack_id_);
  CREATE_OK_REQUEST_PROMISE();
  send_closure(language_pack_manager_, &LanguagePackManager::synchronize_language_pack,
               std::move(request.language_pack_id_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::addCustomServerLanguagePack &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.language_pack_id_);
  CREATE_OK_REQUEST_PROMISE();
  send_closure(language_pack_manager_, &LanguagePackManager::add_custom_server_language,
               std::move(request.language_pack_id_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::setCustomLanguagePack &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  send_closure(language_pack_manager_, &LanguagePackManager::set_custom_language, std::move(request.info_),
               std::move(request.strings_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::editCustomLanguagePackInfo &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  send_closure(language_pack_manager_, &LanguagePackManager::edit_custom_language_info, std::move(request.info_),
               std::move(promise));
}

void Td::on_request(uint64 id, td_api::setCustomLanguagePackString &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.language_pack_id_);
  CREATE_OK_REQUEST_PROMISE();
  send_closure(language_pack_manager_, &LanguagePackManager::set_custom_language_string,
               std::move(request.language_pack_id_), std::move(request.new_string_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::deleteLanguagePack &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.language_pack_id_);
  CREATE_OK_REQUEST_PROMISE();
  send_closure(language_pack_manager_, &LanguagePackManager::delete_language, std::move(request.language_pack_id_),
               std::move(promise));
}

void Td::on_request(uint64 id, td_api::getOption &request) {
  CLEAN_INPUT_STRING(request.name_);
  CREATE_REQUEST_PROMISE();
  option_manager_->get_option(request.name_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::setOption &request) {
  CLEAN_INPUT_STRING(request.name_);
  CREATE_OK_REQUEST_PROMISE();
  option_manager_->set_option(request.name_, std::move(request.value_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::setPollAnswer &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->set_poll_answer({DialogId(request.chat_id_), MessageId(request.message_id_)},
                                     std::move(request.option_ids_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::getPollVoters &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  messages_manager_->get_poll_voters({DialogId(request.chat_id_), MessageId(request.message_id_)}, request.option_id_,
                                     request.offset_, request.limit_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::stopPoll &request) {
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->stop_poll({DialogId(request.chat_id_), MessageId(request.message_id_)},
                               std::move(request.reply_markup_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::hideSuggestedAction &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  dismiss_suggested_action(SuggestedAction(request.action_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::hideContactCloseBirthdays &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  option_manager_->set_option_boolean("dismiss_birthday_contact_today", true);
  user_manager_->hide_contact_birthdays(std::move(promise));
}

void Td::on_request(uint64 id, td_api::getBusinessConnection &request) {
  CHECK_IS_BOT();
  CLEAN_INPUT_STRING(request.connection_id_);
  CREATE_REQUEST_PROMISE();
  business_connection_manager_->get_business_connection(BusinessConnectionId(std::move(request.connection_id_)),
                                                        std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getLoginUrlInfo &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  link_manager_->get_login_url_info({DialogId(request.chat_id_), MessageId(request.message_id_)}, request.button_id_,
                                    std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getLoginUrl &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  link_manager_->get_login_url({DialogId(request.chat_id_), MessageId(request.message_id_)}, request.button_id_,
                               request.allow_write_access_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::shareUsersWithBot &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  auto user_ids = UserId::get_user_ids(request.shared_user_ids_);
  auto dialog_ids = transform(user_ids, [](UserId user_id) { return DialogId(user_id); });
  messages_manager_->share_dialogs_with_bot({DialogId(request.chat_id_), MessageId(request.message_id_)},
                                            request.button_id_, std::move(dialog_ids), true, request.only_check_,
                                            std::move(promise));
}

void Td::on_request(uint64 id, const td_api::shareChatWithBot &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->share_dialogs_with_bot({DialogId(request.chat_id_), MessageId(request.message_id_)},
                                            request.button_id_, {DialogId(request.shared_chat_id_)}, false,
                                            request.only_check_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::getInlineQueryResults &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.query_);
  CLEAN_INPUT_STRING(request.offset_);
  CREATE_REQUEST(GetInlineQueryResultsRequest, request.bot_user_id_, request.chat_id_, request.user_location_,
                 std::move(request.query_), std::move(request.offset_));
}

void Td::on_request(uint64 id, td_api::answerInlineQuery &request) {
  CHECK_IS_BOT();
  CLEAN_INPUT_STRING(request.next_offset_);
  CREATE_OK_REQUEST_PROMISE();
  inline_queries_manager_->answer_inline_query(request.inline_query_id_, request.is_personal_,
                                               std::move(request.button_), std::move(request.results_),
                                               request.cache_time_, request.next_offset_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::searchWebApp &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.web_app_short_name_);
  CREATE_REQUEST_PROMISE();
  attach_menu_manager_->get_web_app(UserId(request.bot_user_id_), request.web_app_short_name_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::getWebAppLinkUrl &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.web_app_short_name_);
  CLEAN_INPUT_STRING(request.start_parameter_);
  CLEAN_INPUT_STRING(request.application_name_);
  CREATE_REQUEST_PROMISE();
  auto query_promise = PromiseCreator::lambda([promise = std::move(promise)](Result<string> result) mutable {
    if (result.is_error()) {
      promise.set_error(result.move_as_error());
    } else {
      promise.set_value(td_api::make_object<td_api::httpUrl>(result.move_as_ok()));
    }
  });
  attach_menu_manager_->request_app_web_view(
      DialogId(request.chat_id_), UserId(request.bot_user_id_), std::move(request.web_app_short_name_),
      std::move(request.start_parameter_), std::move(request.theme_), std::move(request.application_name_),
      request.allow_write_access_, std::move(query_promise));
}

void Td::on_request(uint64 id, td_api::getWebAppUrl &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.url_);
  CLEAN_INPUT_STRING(request.application_name_);
  CREATE_REQUEST_PROMISE();
  auto query_promise = PromiseCreator::lambda([promise = std::move(promise)](Result<string> result) mutable {
    if (result.is_error()) {
      promise.set_error(result.move_as_error());
    } else {
      promise.set_value(td_api::make_object<td_api::httpUrl>(result.move_as_ok()));
    }
  });
  inline_queries_manager_->get_simple_web_view_url(UserId(request.bot_user_id_), std::move(request.url_),
                                                   std::move(request.theme_), std::move(request.application_name_),
                                                   std::move(query_promise));
}

void Td::on_request(uint64 id, td_api::sendWebAppData &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.button_text_);
  CLEAN_INPUT_STRING(request.data_);
  CREATE_OK_REQUEST_PROMISE();
  inline_queries_manager_->send_web_view_data(UserId(request.bot_user_id_), std::move(request.button_text_),
                                              std::move(request.data_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::openWebApp &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.url_);
  CLEAN_INPUT_STRING(request.application_name_);
  CREATE_REQUEST_PROMISE();
  attach_menu_manager_->request_web_view(DialogId(request.chat_id_), UserId(request.bot_user_id_),
                                         MessageId(request.message_thread_id_), std::move(request.reply_to_),
                                         std::move(request.url_), std::move(request.theme_),
                                         std::move(request.application_name_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::closeWebApp &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  attach_menu_manager_->close_web_view(request.web_app_launch_id_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::answerWebAppQuery &request) {
  CHECK_IS_BOT();
  CLEAN_INPUT_STRING(request.web_app_query_id_);
  CREATE_REQUEST_PROMISE();
  inline_queries_manager_->answer_web_view_query(request.web_app_query_id_, std::move(request.result_),
                                                 std::move(promise));
}

void Td::on_request(uint64 id, td_api::getCallbackQueryAnswer &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  callback_queries_manager_->send_callback_query({DialogId(request.chat_id_), MessageId(request.message_id_)},
                                                 std::move(request.payload_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::answerCallbackQuery &request) {
  CHECK_IS_BOT();
  CLEAN_INPUT_STRING(request.text_);
  CLEAN_INPUT_STRING(request.url_);
  CREATE_OK_REQUEST_PROMISE();
  callback_queries_manager_->answer_callback_query(request.callback_query_id_, request.text_, request.show_alert_,
                                                   request.url_, request.cache_time_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::answerShippingQuery &request) {
  CHECK_IS_BOT();
  CLEAN_INPUT_STRING(request.error_message_);
  CREATE_OK_REQUEST_PROMISE();
  answer_shipping_query(this, request.shipping_query_id_, std::move(request.shipping_options_), request.error_message_,
                        std::move(promise));
}

void Td::on_request(uint64 id, td_api::answerPreCheckoutQuery &request) {
  CHECK_IS_BOT();
  CLEAN_INPUT_STRING(request.error_message_);
  CREATE_OK_REQUEST_PROMISE();
  answer_pre_checkout_query(this, request.pre_checkout_query_id_, request.error_message_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::getBankCardInfo &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.bank_card_number_);
  CREATE_REQUEST_PROMISE();
  get_bank_card_info(this, request.bank_card_number_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::getPaymentForm &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  get_payment_form(this, std::move(request.input_invoice_), request.theme_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::validateOrderInfo &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  validate_order_info(this, std::move(request.input_invoice_), std::move(request.order_info_), request.allow_save_,
                      std::move(promise));
}

void Td::on_request(uint64 id, td_api::sendPaymentForm &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.order_info_id_);
  CLEAN_INPUT_STRING(request.shipping_option_id_);
  CREATE_REQUEST_PROMISE();
  send_payment_form(this, std::move(request.input_invoice_), request.payment_form_id_, request.order_info_id_,
                    request.shipping_option_id_, request.credentials_, request.tip_amount_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getPaymentReceipt &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  get_payment_receipt(this, {DialogId(request.chat_id_), MessageId(request.message_id_)}, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getSavedOrderInfo &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  get_saved_order_info(this, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::deleteSavedOrderInfo &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  delete_saved_order_info(this, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::deleteSavedCredentials &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  delete_saved_credentials(this, std::move(promise));
}

void Td::on_request(uint64 id, td_api::createInvoiceLink &request) {
  CHECK_IS_BOT();
  CREATE_REQUEST_PROMISE();
  auto query_promise = PromiseCreator::lambda([promise = std::move(promise)](Result<string> result) mutable {
    if (result.is_error()) {
      promise.set_error(result.move_as_error());
    } else {
      promise.set_value(td_api::make_object<td_api::httpUrl>(result.move_as_ok()));
    }
  });
  export_invoice(this, std::move(request.invoice_), std::move(query_promise));
}

void Td::on_request(uint64 id, td_api::refundStarPayment &request) {
  CHECK_IS_BOT();
  CLEAN_INPUT_STRING(request.telegram_payment_charge_id_);
  CREATE_OK_REQUEST_PROMISE();
  star_manager_->refund_star_payment(UserId(request.user_id_), request.telegram_payment_charge_id_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::getPassportElement &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.password_);
  if (request.type_ == nullptr) {
    return send_error_raw(id, 400, "Type must be non-empty");
  }
  CREATE_REQUEST_PROMISE();
  send_closure(secure_manager_, &SecureManager::get_secure_value, std::move(request.password_),
               get_secure_value_type_td_api(request.type_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::getAllPassportElements &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.password_);
  CREATE_REQUEST_PROMISE();
  send_closure(secure_manager_, &SecureManager::get_all_secure_values, std::move(request.password_),
               std::move(promise));
}

void Td::on_request(uint64 id, td_api::setPassportElement &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.password_);
  auto r_secure_value = get_secure_value(file_manager_.get(), std::move(request.element_));
  if (r_secure_value.is_error()) {
    return send_error_raw(id, 400, r_secure_value.error().message());
  }
  CREATE_REQUEST_PROMISE();
  send_closure(secure_manager_, &SecureManager::set_secure_value, std::move(request.password_),
               r_secure_value.move_as_ok(), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::deletePassportElement &request) {
  CHECK_IS_USER();
  if (request.type_ == nullptr) {
    return send_error_raw(id, 400, "Type must be non-empty");
  }
  CREATE_OK_REQUEST_PROMISE();
  send_closure(secure_manager_, &SecureManager::delete_secure_value, get_secure_value_type_td_api(request.type_),
               std::move(promise));
}

void Td::on_request(uint64 id, td_api::setPassportElementErrors &request) {
  CHECK_IS_BOT();
  auto r_input_user = user_manager_->get_input_user(UserId(request.user_id_));
  if (r_input_user.is_error()) {
    return send_error_raw(id, r_input_user.error().code(), r_input_user.error().message());
  }
  CREATE_OK_REQUEST_PROMISE();
  send_closure(secure_manager_, &SecureManager::set_secure_value_errors, this, r_input_user.move_as_ok(),
               std::move(request.errors_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::getPreferredCountryLanguage &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.country_code_);
  CREATE_REQUEST_PROMISE();
  send_closure(secure_manager_, &SecureManager::get_preferred_country_language, std::move(request.country_code_),
               std::move(promise));
}

void Td::on_request(uint64 id, td_api::sendEmailAddressVerificationCode &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.email_address_);
  CREATE_REQUEST_PROMISE();
  auto query_promise = PromiseCreator::lambda([promise = std::move(promise)](Result<SentEmailCode> result) mutable {
    if (result.is_error()) {
      promise.set_error(result.move_as_error());
    } else {
      promise.set_value(result.ok().get_email_address_authentication_code_info_object());
    }
  });
  send_closure(password_manager_, &PasswordManager::send_email_address_verification_code,
               std::move(request.email_address_), std::move(query_promise));
}

void Td::on_request(uint64 id, const td_api::resendEmailAddressVerificationCode &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  auto query_promise = PromiseCreator::lambda([promise = std::move(promise)](Result<SentEmailCode> result) mutable {
    if (result.is_error()) {
      promise.set_error(result.move_as_error());
    } else {
      promise.set_value(result.ok().get_email_address_authentication_code_info_object());
    }
  });
  send_closure(password_manager_, &PasswordManager::resend_email_address_verification_code, std::move(query_promise));
}

void Td::on_request(uint64 id, td_api::checkEmailAddressVerificationCode &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.code_);
  CREATE_OK_REQUEST_PROMISE();
  send_closure(password_manager_, &PasswordManager::check_email_address_verification_code, std::move(request.code_),
               std::move(promise));
}

void Td::on_request(uint64 id, td_api::getPassportAuthorizationForm &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.public_key_);
  CLEAN_INPUT_STRING(request.scope_);
  CLEAN_INPUT_STRING(request.nonce_);
  UserId bot_user_id(request.bot_user_id_);
  if (!bot_user_id.is_valid()) {
    return send_error_raw(id, 400, "Bot user identifier invalid");
  }
  if (request.nonce_.empty()) {
    return send_error_raw(id, 400, "Nonce must be non-empty");
  }
  CREATE_REQUEST_PROMISE();
  send_closure(secure_manager_, &SecureManager::get_passport_authorization_form, bot_user_id, std::move(request.scope_),
               std::move(request.public_key_), std::move(request.nonce_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::getPassportAuthorizationFormAvailableElements &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.password_);
  CREATE_REQUEST_PROMISE();
  send_closure(secure_manager_, &SecureManager::get_passport_authorization_form_available_elements,
               request.authorization_form_id_, std::move(request.password_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::sendPassportAuthorizationForm &request) {
  CHECK_IS_USER();
  for (auto &type : request.types_) {
    if (type == nullptr) {
      return send_error_raw(id, 400, "Type must be non-empty");
    }
  }

  CREATE_OK_REQUEST_PROMISE();
  send_closure(secure_manager_, &SecureManager::send_passport_authorization_form, request.authorization_form_id_,
               get_secure_value_types_td_api(request.types_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getSupportUser &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  user_manager_->get_support_user(std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getInstalledBackgrounds &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  background_manager_->get_backgrounds(request.for_dark_theme_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::getBackgroundUrl &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.name_);
  Result<string> r_url = LinkManager::get_background_url(request.name_, std::move(request.type_));
  if (r_url.is_error()) {
    return send_closure(actor_id(this), &Td::send_error, id, r_url.move_as_error());
  }

  send_closure(actor_id(this), &Td::send_result, id, td_api::make_object<td_api::httpUrl>(r_url.ok()));
}

void Td::on_request(uint64 id, td_api::searchBackground &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.name_);
  CREATE_REQUEST(SearchBackgroundRequest, std::move(request.name_));
}

void Td::on_request(uint64 id, td_api::setDefaultBackground &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  background_manager_->set_background(request.background_.get(), request.type_.get(), request.for_dark_theme_,
                                      std::move(promise));
}

void Td::on_request(uint64 id, const td_api::deleteDefaultBackground &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  background_manager_->delete_background(request.for_dark_theme_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::removeInstalledBackground &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  background_manager_->remove_background(BackgroundId(request.background_id_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::resetInstalledBackgrounds &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  background_manager_->reset_backgrounds(std::move(promise));
}

void Td::on_request(uint64 id, td_api::getRecentlyVisitedTMeUrls &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.referrer_);
  CREATE_REQUEST_PROMISE();
  create_handler<GetRecentMeUrlsQuery>(std::move(promise))->send(request.referrer_);
}

void Td::on_request(uint64 id, td_api::setBotUpdatesStatus &request) {
  CHECK_IS_BOT();
  CLEAN_INPUT_STRING(request.error_message_);
  create_handler<SetBotUpdatesStatusQuery>()->send(request.pending_update_count_, request.error_message_);
  send_closure(actor_id(this), &Td::send_result, id, make_tl_object<td_api::ok>());
}

void Td::on_request(uint64 id, td_api::sendCustomRequest &request) {
  CHECK_IS_BOT();
  CLEAN_INPUT_STRING(request.method_);
  CLEAN_INPUT_STRING(request.parameters_);
  CREATE_REQUEST_PROMISE();
  create_handler<SendCustomRequestQuery>(std::move(promise))->send(request.method_, request.parameters_);
}

void Td::on_request(uint64 id, td_api::answerCustomQuery &request) {
  CHECK_IS_BOT();
  CLEAN_INPUT_STRING(request.data_);
  CREATE_OK_REQUEST_PROMISE();
  create_handler<AnswerCustomQueryQuery>(std::move(promise))->send(request.custom_query_id_, request.data_);
}

void Td::on_request(uint64 id, const td_api::setAlarm &request) {
  if (request.seconds_ < 0 || request.seconds_ > 3e9) {
    return send_error_raw(id, 400, "Wrong parameter seconds specified");
  }

  int64 alarm_id = alarm_id_++;
  pending_alarms_.emplace(alarm_id, id);
  alarm_timeout_.set_timeout_in(alarm_id, request.seconds_);
}

void Td::on_request(uint64 id, td_api::searchHashtags &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.prefix_);
  CREATE_REQUEST_PROMISE();
  auto query_promise =
      PromiseCreator::lambda([promise = std::move(promise)](Result<std::vector<string>> result) mutable {
        if (result.is_error()) {
          promise.set_error(result.move_as_error());
        } else {
          promise.set_value(make_tl_object<td_api::hashtags>(result.move_as_ok()));
        }
      });
  send_closure(hashtag_hints_, &HashtagHints::query, std::move(request.prefix_), request.limit_,
               std::move(query_promise));
}

void Td::on_request(uint64 id, td_api::removeRecentHashtag &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.hashtag_);
  CREATE_OK_REQUEST_PROMISE();
  send_closure(hashtag_hints_, &HashtagHints::remove_hashtag, std::move(request.hashtag_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getPremiumLimit &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  get_premium_limit(request.limit_type_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getPremiumFeatures &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  get_premium_features(this, request.source_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getPremiumStickerExamples &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  stickers_manager_->search_stickers(StickerType::Regular, "⭐️⭐️", 100, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::viewPremiumFeature &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  view_premium_feature(this, request.feature_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::clickPremiumSubscriptionButton &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  click_premium_subscription_button(this, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getPremiumState &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  get_premium_state(this, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getPremiumGiftCodePaymentOptions &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  get_premium_gift_code_options(this, DialogId(request.boosted_chat_id_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::checkPremiumGiftCode &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.code_);
  CREATE_REQUEST_PROMISE();
  check_premium_gift_code(this, request.code_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::applyPremiumGiftCode &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.code_);
  CREATE_OK_REQUEST_PROMISE();
  apply_premium_gift_code(this, request.code_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::launchPrepaidPremiumGiveaway &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  launch_prepaid_premium_giveaway(this, request.giveaway_id_, std::move(request.parameters_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getPremiumGiveawayInfo &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  get_premium_giveaway_info(this, {DialogId(request.chat_id_), MessageId(request.message_id_)}, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getStarPaymentOptions &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  star_manager_->get_star_payment_options(std::move(promise));
}

void Td::on_request(uint64 id, td_api::getStarTransactions &request) {
  CLEAN_INPUT_STRING(request.offset_);
  CREATE_REQUEST_PROMISE();
  star_manager_->get_star_transactions(std::move(request.owner_id_), request.offset_, request.limit_,
                                       std::move(request.direction_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::canPurchaseFromStore &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  can_purchase_premium(this, std::move(request.purpose_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::assignAppStoreTransaction &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  assign_app_store_transaction(this, request.receipt_, std::move(request.purpose_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::assignGooglePlayTransaction &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.package_name_);
  CLEAN_INPUT_STRING(request.store_product_id_);
  CLEAN_INPUT_STRING(request.purchase_token_);
  CREATE_OK_REQUEST_PROMISE();
  assign_play_market_transaction(this, request.package_name_, request.store_product_id_, request.purchase_token_,
                                 std::move(request.purpose_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getBusinessFeatures &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  get_business_features(this, request.source_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::acceptTermsOfService &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.terms_of_service_id_);
  auto promise = PromiseCreator::lambda([actor_id = actor_id(this), id](Result<> result) {
    if (result.is_error()) {
      send_closure(actor_id, &Td::send_error, id, result.move_as_error());
    } else {
      send_closure(actor_id, &Td::send_result, id, td_api::make_object<td_api::ok>());
      send_closure(actor_id, &Td::schedule_get_terms_of_service, 0);
    }
  });
  accept_terms_of_service(this, std::move(request.terms_of_service_id_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getCountries &request) {
  CREATE_REQUEST_PROMISE();
  country_info_manager_->get_countries(std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getCountryCode &request) {
  CREATE_REQUEST_PROMISE();
  auto query_promise = PromiseCreator::lambda([promise = std::move(promise)](Result<string> result) mutable {
    if (result.is_error()) {
      promise.set_error(result.move_as_error());
    } else {
      promise.set_value(make_tl_object<td_api::text>(result.move_as_ok()));
    }
  });
  country_info_manager_->get_current_country_code(std::move(query_promise));
}

void Td::on_request(uint64 id, const td_api::getPhoneNumberInfo &request) {
  CREATE_REQUEST_PROMISE();
  country_info_manager_->get_phone_number_info(request.phone_number_prefix_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::getCollectibleItemInfo &request) {
  CREATE_REQUEST_PROMISE();
  get_collectible_info(this, std::move(request.type_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getApplicationDownloadLink &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  auto query_promise = PromiseCreator::lambda([promise = std::move(promise)](Result<string> result) mutable {
    if (result.is_error()) {
      promise.set_error(result.move_as_error());
    } else {
      promise.set_value(make_tl_object<td_api::httpUrl>(result.move_as_ok()));
    }
  });
  get_invite_text(this, std::move(query_promise));
}

void Td::on_request(uint64 id, td_api::getDeepLinkInfo &request) {
  CLEAN_INPUT_STRING(request.link_);
  CREATE_REQUEST_PROMISE();
  link_manager_->get_deep_link_info(request.link_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getApplicationConfig &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  send_closure(G()->config_manager(), &ConfigManager::get_app_config, std::move(promise));
}

void Td::on_request(uint64 id, td_api::saveApplicationLogEvent &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.type_);
  CREATE_OK_REQUEST_PROMISE();
  save_app_log(this, request.type_, DialogId(request.chat_id_), convert_json_value(std::move(request.data_)),
               std::move(promise));
}

void Td::on_request(uint64 id, td_api::addProxy &request) {
  CLEAN_INPUT_STRING(request.server_);
  CREATE_REQUEST_PROMISE();
  send_closure(G()->connection_creator(), &ConnectionCreator::add_proxy, -1, std::move(request.server_), request.port_,
               request.enable_, std::move(request.type_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::editProxy &request) {
  if (request.proxy_id_ < 0) {
    return send_error_raw(id, 400, "Proxy identifier invalid");
  }
  CLEAN_INPUT_STRING(request.server_);
  CREATE_REQUEST_PROMISE();
  send_closure(G()->connection_creator(), &ConnectionCreator::add_proxy, request.proxy_id_, std::move(request.server_),
               request.port_, request.enable_, std::move(request.type_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::enableProxy &request) {
  CREATE_OK_REQUEST_PROMISE();
  send_closure(G()->connection_creator(), &ConnectionCreator::enable_proxy, request.proxy_id_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::disableProxy &request) {
  CREATE_OK_REQUEST_PROMISE();
  send_closure(G()->connection_creator(), &ConnectionCreator::disable_proxy, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::removeProxy &request) {
  CREATE_OK_REQUEST_PROMISE();
  send_closure(G()->connection_creator(), &ConnectionCreator::remove_proxy, request.proxy_id_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getProxies &request) {
  CREATE_REQUEST_PROMISE();
  send_closure(G()->connection_creator(), &ConnectionCreator::get_proxies, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getProxyLink &request) {
  CREATE_REQUEST_PROMISE();
  auto query_promise = PromiseCreator::lambda([promise = std::move(promise)](Result<string> result) mutable {
    if (result.is_error()) {
      promise.set_error(result.move_as_error());
    } else {
      promise.set_value(make_tl_object<td_api::httpUrl>(result.move_as_ok()));
    }
  });
  send_closure(G()->connection_creator(), &ConnectionCreator::get_proxy_link, request.proxy_id_,
               std::move(query_promise));
}

void Td::on_request(uint64 id, const td_api::pingProxy &request) {
  CREATE_REQUEST_PROMISE();
  auto query_promise = PromiseCreator::lambda([promise = std::move(promise)](Result<double> result) mutable {
    if (result.is_error()) {
      promise.set_error(result.move_as_error());
    } else {
      promise.set_value(make_tl_object<td_api::seconds>(result.move_as_ok()));
    }
  });
  send_closure(G()->connection_creator(), &ConnectionCreator::ping_proxy, request.proxy_id_, std::move(query_promise));
}

void Td::on_request(uint64 id, const td_api::getUserSupportInfo &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  get_user_info(this, UserId(request.user_id_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::setUserSupportInfo &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  set_user_info(this, UserId(request.user_id_), std::move(request.message_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getSupportName &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  auto query_promise = PromiseCreator::lambda([promise = std::move(promise)](Result<string> result) mutable {
    if (result.is_error()) {
      promise.set_error(result.move_as_error());
    } else {
      promise.set_value(make_tl_object<td_api::text>(result.move_as_ok()));
    }
  });
  get_support_name(this, std::move(query_promise));
}

void Td::on_request(uint64 id, const td_api::searchQuote &request) {
  UNREACHABLE();
}

void Td::on_request(uint64 id, const td_api::getTextEntities &request) {
  UNREACHABLE();
}

void Td::on_request(uint64 id, const td_api::parseTextEntities &request) {
  UNREACHABLE();
}

void Td::on_request(uint64 id, const td_api::parseMarkdown &request) {
  UNREACHABLE();
}

void Td::on_request(uint64 id, const td_api::getMarkdownText &request) {
  UNREACHABLE();
}

void Td::on_request(uint64 id, const td_api::searchStringsByPrefix &request) {
  UNREACHABLE();
}

void Td::on_request(uint64 id, const td_api::checkQuickReplyShortcutName &request) {
  UNREACHABLE();
}

void Td::on_request(uint64 id, const td_api::getCountryFlagEmoji &request) {
  UNREACHABLE();
}

void Td::on_request(uint64 id, const td_api::getFileMimeType &request) {
  UNREACHABLE();
}

void Td::on_request(uint64 id, const td_api::getFileExtension &request) {
  UNREACHABLE();
}

void Td::on_request(uint64 id, const td_api::cleanFileName &request) {
  UNREACHABLE();
}

void Td::on_request(uint64 id, const td_api::getLanguagePackString &request) {
  UNREACHABLE();
}

void Td::on_request(uint64 id, const td_api::getPhoneNumberInfoSync &request) {
  UNREACHABLE();
}

void Td::on_request(uint64 id, const td_api::getPushReceiverId &request) {
  UNREACHABLE();
}

void Td::on_request(uint64 id, const td_api::getChatFolderDefaultIconName &request) {
  UNREACHABLE();
}

void Td::on_request(uint64 id, const td_api::getJsonValue &request) {
  UNREACHABLE();
}

void Td::on_request(uint64 id, const td_api::getJsonString &request) {
  UNREACHABLE();
}

void Td::on_request(uint64 id, const td_api::getThemeParametersJsonString &request) {
  UNREACHABLE();
}

void Td::on_request(uint64 id, const td_api::setLogStream &request) {
  UNREACHABLE();
}

void Td::on_request(uint64 id, const td_api::getLogStream &request) {
  UNREACHABLE();
}

void Td::on_request(uint64 id, const td_api::setLogVerbosityLevel &request) {
  UNREACHABLE();
}

void Td::on_request(uint64 id, const td_api::getLogVerbosityLevel &request) {
  UNREACHABLE();
}

void Td::on_request(uint64 id, const td_api::getLogTags &request) {
  UNREACHABLE();
}

void Td::on_request(uint64 id, const td_api::setLogTagVerbosityLevel &request) {
  UNREACHABLE();
}

void Td::on_request(uint64 id, const td_api::getLogTagVerbosityLevel &request) {
  UNREACHABLE();
}

void Td::on_request(uint64 id, const td_api::addLogMessage &request) {
  UNREACHABLE();
}

td_api::object_ptr<td_api::Object> Td::do_static_request(td_api::searchQuote &request) {
  if (request.text_ == nullptr || request.quote_ == nullptr) {
    return make_error(400, "Text and quote must be non-empty");
  }
  if (!check_utf8(request.text_->text_) || !check_utf8(request.quote_->text_)) {
    return make_error(400, "Strings must be encoded in UTF-8");
  }
  auto r_text_entities = get_message_entities(nullptr, std::move(request.text_->entities_), false);
  if (r_text_entities.is_error()) {
    return make_error(400, r_text_entities.error().message());
  }
  auto r_quote_entities = get_message_entities(nullptr, std::move(request.quote_->entities_), false);
  if (r_quote_entities.is_error()) {
    return make_error(400, r_quote_entities.error().message());
  }
  auto position = MessageQuote::search_quote({std::move(request.text_->text_), r_text_entities.move_as_ok()},
                                             {std::move(request.quote_->text_), r_quote_entities.move_as_ok()},
                                             request.quote_position_);
  if (position == -1) {
    return make_error(404, "Not Found");
  }
  return td_api::make_object<td_api::foundPosition>(position);
}

td_api::object_ptr<td_api::Object> Td::do_static_request(const td_api::getTextEntities &request) {
  if (!check_utf8(request.text_)) {
    return make_error(400, "Text must be encoded in UTF-8");
  }
  auto text_entities = find_entities(request.text_, false, false);
  return make_tl_object<td_api::textEntities>(
      get_text_entities_object(text_entities, false, std::numeric_limits<int32>::max()));
}

td_api::object_ptr<td_api::Object> Td::do_static_request(td_api::parseTextEntities &request) {
  if (!check_utf8(request.text_)) {  // must not use clean_input_string, because \r may be used as a separator
    return make_error(400, "Text must be encoded in UTF-8");
  }
  if (request.parse_mode_ == nullptr) {
    return make_error(400, "Parse mode must be non-empty");
  }

  auto r_entities = [&]() -> Result<vector<MessageEntity>> {
    if (utf8_length(request.text_) > 65536) {
      return Status::Error("Text is too long");
    }
    switch (request.parse_mode_->get_id()) {
      case td_api::textParseModeHTML::ID:
        return parse_html(request.text_);
      case td_api::textParseModeMarkdown::ID: {
        auto version = static_cast<const td_api::textParseModeMarkdown *>(request.parse_mode_.get())->version_;
        if (version == 0 || version == 1) {
          return parse_markdown(request.text_);
        }
        if (version == 2) {
          return parse_markdown_v2(request.text_);
        }
        return Status::Error("Wrong Markdown version specified");
      }
      default:
        UNREACHABLE();
        return Status::Error(500, "Unknown parse mode");
    }
  }();
  if (r_entities.is_error()) {
    return make_error(400, PSLICE() << "Can't parse entities: " << r_entities.error().message());
  }

  return make_tl_object<td_api::formattedText>(std::move(request.text_),
                                               get_text_entities_object(r_entities.ok(), false, -1));
}

td_api::object_ptr<td_api::Object> Td::do_static_request(td_api::parseMarkdown &request) {
  if (request.text_ == nullptr) {
    return make_error(400, "Text must be non-empty");
  }

  auto r_entities = get_message_entities(nullptr, std::move(request.text_->entities_), true);
  if (r_entities.is_error()) {
    return make_error(400, r_entities.error().message());
  }
  auto entities = r_entities.move_as_ok();
  auto status = fix_formatted_text(request.text_->text_, entities, true, true, true, true, true);
  if (status.is_error()) {
    return make_error(400, status.message());
  }

  auto parsed_text = parse_markdown_v3({std::move(request.text_->text_), std::move(entities)});
  fix_formatted_text(parsed_text.text, parsed_text.entities, true, true, true, true, true).ensure();
  return get_formatted_text_object(parsed_text, false, std::numeric_limits<int32>::max());
}

td_api::object_ptr<td_api::Object> Td::do_static_request(const td_api::getOption &request) {
  if (!is_synchronous_request(&request)) {
    return make_error(400, "The option can't be get synchronously");
  }
  return OptionManager::get_option_synchronously(request.name_);
}

td_api::object_ptr<td_api::Object> Td::do_static_request(td_api::getMarkdownText &request) {
  if (request.text_ == nullptr) {
    return make_error(400, "Text must be non-empty");
  }

  auto r_entities = get_message_entities(nullptr, std::move(request.text_->entities_));
  if (r_entities.is_error()) {
    return make_error(400, r_entities.error().message());
  }
  auto entities = r_entities.move_as_ok();
  auto status = fix_formatted_text(request.text_->text_, entities, true, true, true, true, true);
  if (status.is_error()) {
    return make_error(400, status.message());
  }

  return get_formatted_text_object(get_markdown_v3({std::move(request.text_->text_), std::move(entities)}), false,
                                   std::numeric_limits<int32>::max());
}

td_api::object_ptr<td_api::Object> Td::do_static_request(td_api::searchStringsByPrefix &request) {
  if (!clean_input_string(request.query_)) {
    return make_error(400, "Strings must be encoded in UTF-8");
  }
  for (auto &str : request.strings_) {
    if (!clean_input_string(str)) {
      return make_error(400, "Strings must be encoded in UTF-8");
    }
  }
  int32 total_count = 0;
  auto result = search_strings_by_prefix(std::move(request.strings_), std::move(request.query_), request.limit_,
                                         !request.return_none_for_empty_query_, total_count);
  return td_api::make_object<td_api::foundPositions>(total_count, std::move(result));
}

td_api::object_ptr<td_api::Object> Td::do_static_request(const td_api::checkQuickReplyShortcutName &request) {
  // don't check name UTF-8 correctness
  auto status = QuickReplyManager::check_shortcut_name(request.name_);
  if (status.is_ok()) {
    return td_api::make_object<td_api::ok>();
  }
  return make_error(200, status.message());
}

td_api::object_ptr<td_api::Object> Td::do_static_request(const td_api::getCountryFlagEmoji &request) {
  // don't check country code UTF-8 correctness
  return td_api::make_object<td_api::text>(CountryInfoManager::get_country_flag_emoji(request.country_code_));
}

td_api::object_ptr<td_api::Object> Td::do_static_request(const td_api::getFileMimeType &request) {
  // don't check file name UTF-8 correctness
  return make_tl_object<td_api::text>(MimeType::from_extension(PathView(request.file_name_).extension()));
}

td_api::object_ptr<td_api::Object> Td::do_static_request(const td_api::getFileExtension &request) {
  // don't check MIME type UTF-8 correctness
  return make_tl_object<td_api::text>(MimeType::to_extension(request.mime_type_));
}

td_api::object_ptr<td_api::Object> Td::do_static_request(const td_api::cleanFileName &request) {
  // don't check file name UTF-8 correctness
  return make_tl_object<td_api::text>(clean_filename(request.file_name_));
}

td_api::object_ptr<td_api::Object> Td::do_static_request(const td_api::getLanguagePackString &request) {
  return LanguagePackManager::get_language_pack_string(
      request.language_pack_database_path_, request.localization_target_, request.language_pack_id_, request.key_);
}

td_api::object_ptr<td_api::Object> Td::do_static_request(td_api::getPhoneNumberInfoSync &request) {
  // don't check language_code/phone number UTF-8 correctness
  return CountryInfoManager::get_phone_number_info_sync(request.language_code_,
                                                        std::move(request.phone_number_prefix_));
}

td_api::object_ptr<td_api::Object> Td::do_static_request(const td_api::getPushReceiverId &request) {
  // don't check push payload UTF-8 correctness
  auto r_push_receiver_id = NotificationManager::get_push_receiver_id(request.payload_);
  if (r_push_receiver_id.is_error()) {
    VLOG(notifications) << "Failed to get push notification receiver from \"" << format::escaped(request.payload_)
                        << '"';
    return make_error(r_push_receiver_id.error().code(), r_push_receiver_id.error().message());
  }
  return td_api::make_object<td_api::pushReceiverId>(r_push_receiver_id.ok());
}

td_api::object_ptr<td_api::Object> Td::do_static_request(const td_api::getChatFolderDefaultIconName &request) {
  if (request.folder_ == nullptr) {
    return make_error(400, "Chat folder must be non-empty");
  }
  if (!check_utf8(request.folder_->title_)) {
    return make_error(400, "Chat folder title must be encoded in UTF-8");
  }
  if (request.folder_->icon_ != nullptr && !check_utf8(request.folder_->icon_->name_)) {
    return make_error(400, "Chat folder icon name must be encoded in UTF-8");
  }
  return td_api::make_object<td_api::chatFolderIcon>(DialogFilter::get_default_icon_name(request.folder_.get()));
}

td_api::object_ptr<td_api::Object> Td::do_static_request(td_api::getJsonValue &request) {
  if (!check_utf8(request.json_)) {
    return make_error(400, "JSON has invalid encoding");
  }
  auto result = get_json_value(request.json_);
  if (result.is_error()) {
    return make_error(400, result.error().message());
  } else {
    return result.move_as_ok();
  }
}

td_api::object_ptr<td_api::Object> Td::do_static_request(const td_api::getJsonString &request) {
  return td_api::make_object<td_api::text>(get_json_string(request.json_value_.get()));
}

td_api::object_ptr<td_api::Object> Td::do_static_request(const td_api::getThemeParametersJsonString &request) {
  return td_api::make_object<td_api::text>(ThemeManager::get_theme_parameters_json_string(request.theme_));
}

td_api::object_ptr<td_api::Object> Td::do_static_request(td_api::setLogStream &request) {
  auto result = Logging::set_current_stream(std::move(request.log_stream_));
  if (result.is_ok()) {
    return td_api::make_object<td_api::ok>();
  } else {
    return make_error(400, result.message());
  }
}

td_api::object_ptr<td_api::Object> Td::do_static_request(const td_api::getLogStream &request) {
  auto result = Logging::get_current_stream();
  if (result.is_ok()) {
    return result.move_as_ok();
  } else {
    return make_error(400, result.error().message());
  }
}

td_api::object_ptr<td_api::Object> Td::do_static_request(const td_api::setLogVerbosityLevel &request) {
  auto result = Logging::set_verbosity_level(static_cast<int>(request.new_verbosity_level_));
  if (result.is_ok()) {
    return td_api::make_object<td_api::ok>();
  } else {
    return make_error(400, result.message());
  }
}

td_api::object_ptr<td_api::Object> Td::do_static_request(const td_api::getLogVerbosityLevel &request) {
  return td_api::make_object<td_api::logVerbosityLevel>(Logging::get_verbosity_level());
}

td_api::object_ptr<td_api::Object> Td::do_static_request(const td_api::getLogTags &request) {
  return td_api::make_object<td_api::logTags>(Logging::get_tags());
}

td_api::object_ptr<td_api::Object> Td::do_static_request(const td_api::setLogTagVerbosityLevel &request) {
  auto result = Logging::set_tag_verbosity_level(request.tag_, static_cast<int>(request.new_verbosity_level_));
  if (result.is_ok()) {
    return td_api::make_object<td_api::ok>();
  } else {
    return make_error(400, result.message());
  }
}

td_api::object_ptr<td_api::Object> Td::do_static_request(const td_api::getLogTagVerbosityLevel &request) {
  auto result = Logging::get_tag_verbosity_level(request.tag_);
  if (result.is_ok()) {
    return td_api::make_object<td_api::logVerbosityLevel>(result.ok());
  } else {
    return make_error(400, result.error().message());
  }
}

td_api::object_ptr<td_api::Object> Td::do_static_request(const td_api::addLogMessage &request) {
  Logging::add_message(request.verbosity_level_, request.text_);
  return td_api::make_object<td_api::ok>();
}

td_api::object_ptr<td_api::Object> Td::do_static_request(td_api::testReturnError &request) {
  if (request.error_ == nullptr) {
    return td_api::make_object<td_api::error>(404, "Not Found");
  }

  return std::move(request.error_);
}

// test
void Td::on_request(uint64 id, const td_api::testNetwork &request) {
  CREATE_OK_REQUEST_PROMISE();
  create_handler<TestNetworkQuery>(std::move(promise))->send();
}

void Td::on_request(uint64 id, td_api::testProxy &request) {
  auto r_proxy = Proxy::create_proxy(std::move(request.server_), request.port_, request.type_.get());
  if (r_proxy.is_error()) {
    return send_closure(actor_id(this), &Td::send_error, id, r_proxy.move_as_error());
  }
  CREATE_REQUEST(TestProxyRequest, r_proxy.move_as_ok(), request.dc_id_, request.timeout_);
}

void Td::on_request(uint64 id, const td_api::testGetDifference &request) {
  updates_manager_->get_difference("testGetDifference");
  send_closure(actor_id(this), &Td::send_result, id, make_tl_object<td_api::ok>());
}

void Td::on_request(uint64 id, const td_api::testUseUpdate &request) {
  send_closure(actor_id(this), &Td::send_result, id, nullptr);
}

void Td::on_request(uint64 id, const td_api::testReturnError &request) {
  UNREACHABLE();
}

void Td::on_request(uint64 id, const td_api::testCallEmpty &request) {
  send_closure(actor_id(this), &Td::send_result, id, make_tl_object<td_api::ok>());
}

void Td::on_request(uint64 id, const td_api::testSquareInt &request) {
  send_closure(actor_id(this), &Td::send_result, id, make_tl_object<td_api::testInt>(request.x_ * request.x_));
}

void Td::on_request(uint64 id, td_api::testCallString &request) {
  send_closure(actor_id(this), &Td::send_result, id, make_tl_object<td_api::testString>(std::move(request.x_)));
}

void Td::on_request(uint64 id, td_api::testCallBytes &request) {
  send_closure(actor_id(this), &Td::send_result, id, make_tl_object<td_api::testBytes>(std::move(request.x_)));
}

void Td::on_request(uint64 id, td_api::testCallVectorInt &request) {
  send_closure(actor_id(this), &Td::send_result, id, make_tl_object<td_api::testVectorInt>(std::move(request.x_)));
}

void Td::on_request(uint64 id, td_api::testCallVectorIntObject &request) {
  send_closure(actor_id(this), &Td::send_result, id,
               make_tl_object<td_api::testVectorIntObject>(std::move(request.x_)));
}

void Td::on_request(uint64 id, td_api::testCallVectorString &request) {
  send_closure(actor_id(this), &Td::send_result, id, make_tl_object<td_api::testVectorString>(std::move(request.x_)));
}

void Td::on_request(uint64 id, td_api::testCallVectorStringObject &request) {
  send_closure(actor_id(this), &Td::send_result, id,
               make_tl_object<td_api::testVectorStringObject>(std::move(request.x_)));
}

#undef CLEAN_INPUT_STRING
#undef CHECK_IS_BOT
#undef CHECK_IS_USER
#undef CREATE_NO_ARGS_REQUEST
#undef CREATE_REQUEST
#undef CREATE_REQUEST_PROMISE
#undef CREATE_OK_REQUEST_PROMISE

}  // namespace td
