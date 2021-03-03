//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/Td.h"

#include "td/telegram/AccessRights.h"
#include "td/telegram/AnimationsManager.h"
#include "td/telegram/AudiosManager.h"
#include "td/telegram/AuthManager.h"
#include "td/telegram/AutoDownloadSettings.h"
#include "td/telegram/BackgroundId.h"
#include "td/telegram/BackgroundManager.h"
#include "td/telegram/BackgroundType.h"
#include "td/telegram/CallbackQueriesManager.h"
#include "td/telegram/CallId.h"
#include "td/telegram/CallManager.h"
#include "td/telegram/ChannelId.h"
#include "td/telegram/ChatId.h"
#include "td/telegram/ConfigManager.h"
#include "td/telegram/ConfigShared.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/CountryInfoManager.h"
#include "td/telegram/DeviceTokenManager.h"
#include "td/telegram/DialogAction.h"
#include "td/telegram/DialogAdministrator.h"
#include "td/telegram/DialogFilter.h"
#include "td/telegram/DialogFilterId.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/DialogListId.h"
#include "td/telegram/DialogLocation.h"
#include "td/telegram/DialogParticipant.h"
#include "td/telegram/DialogSource.h"
#include "td/telegram/DocumentsManager.h"
#include "td/telegram/FileReferenceManager.h"
#include "td/telegram/files/FileGcParameters.h"
#include "td/telegram/files/FileId.h"
#include "td/telegram/files/FileManager.h"
#include "td/telegram/files/FileSourceId.h"
#include "td/telegram/files/FileType.h"
#include "td/telegram/FolderId.h"
#include "td/telegram/FullMessageId.h"
#include "td/telegram/Global.h"
#include "td/telegram/GroupCallId.h"
#include "td/telegram/GroupCallManager.h"
#include "td/telegram/HashtagHints.h"
#include "td/telegram/InlineQueriesManager.h"
#include "td/telegram/JsonValue.h"
#include "td/telegram/LanguagePackManager.h"
#include "td/telegram/Location.h"
#include "td/telegram/Logging.h"
#include "td/telegram/MessageCopyOptions.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/MessageSearchFilter.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/misc.h"
#include "td/telegram/net/ConnectionCreator.h"
#include "td/telegram/net/DcId.h"
#include "td/telegram/net/MtprotoHeader.h"
#include "td/telegram/net/NetQuery.h"
#include "td/telegram/net/NetQueryDelayer.h"
#include "td/telegram/net/NetQueryDispatcher.h"
#include "td/telegram/net/NetStatsManager.h"
#include "td/telegram/net/NetType.h"
#include "td/telegram/net/Proxy.h"
#include "td/telegram/net/PublicRsaKeyShared.h"
#include "td/telegram/net/TempAuthKeyWatchdog.h"
#include "td/telegram/NotificationGroupId.h"
#include "td/telegram/NotificationId.h"
#include "td/telegram/NotificationManager.h"
#include "td/telegram/NotificationSettings.h"
#include "td/telegram/PasswordManager.h"
#include "td/telegram/Payments.h"
#include "td/telegram/PhoneNumberManager.h"
#include "td/telegram/Photo.h"
#include "td/telegram/PhotoSizeSource.h"
#include "td/telegram/PollManager.h"
#include "td/telegram/PrivacyManager.h"
#include "td/telegram/PublicDialogType.h"
#include "td/telegram/ReportReason.h"
#include "td/telegram/RequestActor.h"
#include "td/telegram/SecretChatId.h"
#include "td/telegram/SecretChatsManager.h"
#include "td/telegram/SecureManager.h"
#include "td/telegram/SecureValue.h"
#include "td/telegram/StateManager.h"
#include "td/telegram/StickerSetId.h"
#include "td/telegram/StickersManager.h"
#include "td/telegram/StorageManager.h"
#include "td/telegram/SuggestedAction.h"
#include "td/telegram/TdDb.h"
#include "td/telegram/TopDialogCategory.h"
#include "td/telegram/TopDialogManager.h"
#include "td/telegram/UpdatesManager.h"
#include "td/telegram/VideoNotesManager.h"
#include "td/telegram/VideosManager.h"
#include "td/telegram/VoiceNotesManager.h"
#include "td/telegram/WebPageId.h"
#include "td/telegram/WebPagesManager.h"

#include "td/telegram/td_api.hpp"
#include "td/telegram/telegram_api.h"
#include "td/telegram/telegram_api.hpp"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/db/binlog/BinlogEvent.h"

#include "td/mtproto/DhHandshake.h"
#include "td/mtproto/Handshake.h"
#include "td/mtproto/HandshakeActor.h"
#include "td/mtproto/RawConnection.h"
#include "td/mtproto/RSA.h"
#include "td/mtproto/TransportType.h"

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"
#include "td/utils/filesystem.h"
#include "td/utils/format.h"
#include "td/utils/MimeType.h"
#include "td/utils/misc.h"
#include "td/utils/PathView.h"
#include "td/utils/port/IPAddress.h"
#include "td/utils/port/path.h"
#include "td/utils/port/SocketFd.h"
#include "td/utils/port/uname.h"
#include "td/utils/Random.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/Timer.h"
#include "td/utils/tl_parsers.h"
#include "td/utils/utf8.h"

#include <cmath>
#include <limits>
#include <tuple>
#include <type_traits>

namespace td {

int VERBOSITY_NAME(td_init) = VERBOSITY_NAME(DEBUG) + 3;
int VERBOSITY_NAME(td_requests) = VERBOSITY_NAME(INFO);

void Td::ResultHandler::set_td(Td *new_td) {
  CHECK(td == nullptr);
  td = new_td;
}

void Td::ResultHandler::on_result(NetQueryPtr query) {
  CHECK(query->is_ready());
  if (query->is_ok()) {
    on_result(query->id(), std::move(query->ok()));
  } else {
    on_error(query->id(), std::move(query->error()));
  }
  query->clear();
}

void Td::ResultHandler::send_query(NetQueryPtr query) {
  td->add_handler(query->id(), shared_from_this());
  td->send(std::move(query));
}

class GetPromoDataQuery : public Td::ResultHandler {
  Promise<telegram_api::object_ptr<telegram_api::help_PromoData>> promise_;

 public:
  explicit GetPromoDataQuery(Promise<telegram_api::object_ptr<telegram_api::help_PromoData>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send() {
    // we don't poll promo data before authorization
    send_query(G()->net_query_creator().create(telegram_api::help_getPromoData()));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::help_getPromoData>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    promise_.set_value(result_ptr.move_as_ok());
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class GetRecentMeUrlsQuery : public Td::ResultHandler {
  Promise<tl_object_ptr<td_api::tMeUrls>> promise_;

 public:
  explicit GetRecentMeUrlsQuery(Promise<tl_object_ptr<td_api::tMeUrls>> &&promise) : promise_(std::move(promise)) {
  }

  void send(const string &referrer) {
    send_query(G()->net_query_creator().create(telegram_api::help_getRecentMeUrls(referrer)));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::help_getRecentMeUrls>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto urls_full = result_ptr.move_as_ok();
    td->contacts_manager_->on_get_users(std::move(urls_full->users_), "GetRecentMeUrlsQuery");
    td->contacts_manager_->on_get_chats(std::move(urls_full->chats_), "GetRecentMeUrlsQuery");

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
          result->type_ = make_tl_object<td_api::tMeUrlTypeUser>(
              td->contacts_manager_->get_user_id_object(user_id, "tMeUrlTypeUser"));
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
              td->contacts_manager_->get_supergroup_id_object(channel_id, "tMeUrlTypeSupergroup"));
          break;
        }
        case telegram_api::recentMeUrlChatInvite::ID: {
          auto url = move_tl_object_as<telegram_api::recentMeUrlChatInvite>(url_ptr);
          result->url_ = std::move(url->url_);
          td->contacts_manager_->on_get_dialog_invite_link_info(result->url_, std::move(url->chat_invite_),
                                                                Promise<Unit>());
          auto info_object = td->contacts_manager_->get_chat_invite_link_info_object(result->url_);
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
              td->stickers_manager_->on_get_sticker_set_covered(std::move(url->set_), false, "recentMeUrlStickerSet");
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

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class SendCustomRequestQuery : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::customRequestResult>> promise_;

 public:
  explicit SendCustomRequestQuery(Promise<td_api::object_ptr<td_api::customRequestResult>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(const string &method, const string &parameters) {
    send_query(G()->net_query_creator().create(
        telegram_api::bots_sendCustomRequest(method, make_tl_object<telegram_api::dataJSON>(parameters))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::bots_sendCustomRequest>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    promise_.set_value(td_api::make_object<td_api::customRequestResult>(result->data_));
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class AnswerCustomQueryQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit AnswerCustomQueryQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(int64 custom_query_id, const string &data) {
    send_query(G()->net_query_creator().create(
        telegram_api::bots_answerWebhookJSONQuery(custom_query_id, make_tl_object<telegram_api::dataJSON>(data))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::bots_answerWebhookJSONQuery>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    bool result = result_ptr.ok();
    if (!result) {
      LOG(INFO) << "Sending answer to a custom query has failed";
    }
    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class SetBotUpdatesStatusQuery : public Td::ResultHandler {
 public:
  void send(int32 pending_update_count, const string &error_message) {
    send_query(
        G()->net_query_creator().create(telegram_api::help_setBotUpdatesStatus(pending_update_count, error_message)));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::help_setBotUpdatesStatus>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    bool result = result_ptr.ok();
    LOG_IF(WARNING, !result) << "Set bot updates status has failed";
  }

  void on_error(uint64 id, Status status) override {
    if (!G()->is_expected_error(status)) {
      LOG(WARNING) << "Receive error for SetBotUpdatesStatusQuery: " << status;
    }
    status.ignore();
  }
};

class UpdateStatusQuery : public Td::ResultHandler {
  bool is_offline_;

 public:
  NetQueryRef send(bool is_offline) {
    is_offline_ = is_offline;
    auto net_query = G()->net_query_creator().create(telegram_api::account_updateStatus(is_offline));
    auto result = net_query.get_weak();
    send_query(std::move(net_query));
    return result;
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::account_updateStatus>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    bool result = result_ptr.ok();
    LOG(INFO) << "UpdateStatus returned " << result;
    td->on_update_status_success(!is_offline_);
  }

  void on_error(uint64 id, Status status) override {
    if (status.code() != NetQuery::Cancelled && !G()->is_expected_error(status)) {
      LOG(ERROR) << "Receive error for UpdateStatusQuery: " << status;
    }
    status.ignore();
  }
};

class GetInviteTextQuery : public Td::ResultHandler {
  Promise<string> promise_;

 public:
  explicit GetInviteTextQuery(Promise<string> &&promise) : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create(telegram_api::help_getInviteText()));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::help_getInviteText>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    promise_.set_value(std::move(result->message_));
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class GetDeepLinkInfoQuery : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::deepLinkInfo>> promise_;

 public:
  explicit GetDeepLinkInfoQuery(Promise<td_api::object_ptr<td_api::deepLinkInfo>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(Slice link) {
    Slice link_scheme("tg:");
    if (begins_with(link, link_scheme)) {
      link.remove_prefix(link_scheme.size());
      if (begins_with(link, "//")) {
        link.remove_prefix(2);
      }
    }
    size_t pos = 0;
    while (pos < link.size() && link[pos] != '/' && link[pos] != '?' && link[pos] != '#') {
      pos++;
    }
    link.truncate(pos);
    send_query(G()->net_query_creator().create_unauth(telegram_api::help_getDeepLinkInfo(link.str())));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::help_getDeepLinkInfo>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    switch (result->get_id()) {
      case telegram_api::help_deepLinkInfoEmpty::ID:
        return promise_.set_value(nullptr);
      case telegram_api::help_deepLinkInfo::ID: {
        auto info = telegram_api::move_object_as<telegram_api::help_deepLinkInfo>(result);
        bool need_update = (info->flags_ & telegram_api::help_deepLinkInfo::UPDATE_APP_MASK) != 0;

        auto entities = get_message_entities(nullptr, std::move(info->entities_), "GetDeepLinkInfoQuery");
        auto status = fix_formatted_text(info->message_, entities, true, true, true, true);
        if (status.is_error()) {
          LOG(ERROR) << "Receive error " << status << " while parsing deep link info " << info->message_;
          if (!clean_input_string(info->message_)) {
            info->message_.clear();
          }
          entities = find_entities(info->message_, true);
        }
        FormattedText text{std::move(info->message_), std::move(entities)};
        return promise_.set_value(
            td_api::make_object<td_api::deepLinkInfo>(get_formatted_text_object(text), need_update));
      }
      default:
        UNREACHABLE();
    }
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class SaveAppLogQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit SaveAppLogQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(const string &type, int64 peer_id, tl_object_ptr<telegram_api::JSONValue> &&data) {
    CHECK(data != nullptr);
    vector<telegram_api::object_ptr<telegram_api::inputAppEvent>> input_app_events;
    input_app_events.push_back(
        make_tl_object<telegram_api::inputAppEvent>(G()->server_time_cached(), type, peer_id, std::move(data)));
    send_query(G()->net_query_creator().create_unauth(telegram_api::help_saveAppLog(std::move(input_app_events))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::help_saveAppLog>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    bool result = result_ptr.move_as_ok();
    LOG_IF(ERROR, !result) << "Receive false from help.saveAppLog";
    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class TestQuery : public Td::ResultHandler {
 public:
  explicit TestQuery(uint64 request_id) : request_id_(request_id) {
  }

  void send() {
    send_query(G()->net_query_creator().create_unauth(telegram_api::help_getConfig()));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::help_getConfig>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, Status::Error(500, "Fetch failed"));
    }

    LOG(DEBUG) << "TestOK: " << to_string(result_ptr.ok());
    send_closure(G()->td(), &Td::send_result, request_id_, make_tl_object<td_api::ok>());
  }

  void on_error(uint64 id, Status status) override {
    status.ignore();
    LOG(ERROR) << "Test query failed: " << status;
  }

 private:
  uint64 request_id_;
};

class TestProxyRequest : public RequestOnceActor {
  Proxy proxy_;
  int16 dc_id_;
  double timeout_;
  ActorOwn<> child_;
  Promise<> promise_;

  auto get_transport() {
    return mtproto::TransportType{mtproto::TransportType::ObfuscatedTcp, dc_id_, proxy_.secret()};
  }

  void do_run(Promise<Unit> &&promise) override {
    set_timeout_in(timeout_);

    promise_ = std::move(promise);
    IPAddress ip;
    auto status = ip.init_host_port(proxy_.server(), proxy_.port());
    if (status.is_error()) {
      return promise_.set_error(Status::Error(400, status.public_message()));
    }
    auto r_socket_fd = SocketFd::open(ip);
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

    child_ =
        ConnectionCreator::prepare_connection(r_socket_fd.move_as_ok(), proxy_, mtproto_ip_address, get_transport(),
                                              "Test", "TestPingDC2", nullptr, {}, false, std::move(connection_promise));
  }

  void on_connection_data(Result<ConnectionCreator::ConnectionData> r_data) {
    if (r_data.is_error()) {
      return promise_.set_error(r_data.move_as_error());
    }
    class HandshakeContext : public mtproto::AuthKeyHandshakeContext {
     public:
      DhCallback *get_dh_callback() override {
        return nullptr;
      }
      PublicRsaKeyInterface *get_public_rsa_key_interface() override {
        return &public_rsa_key;
      }

     private:
      PublicRsaKeyShared public_rsa_key{DcId::empty(), false};
    };
    auto handshake = make_unique<mtproto::AuthKeyHandshake>(dc_id_, 3600);
    auto data = r_data.move_as_ok();
    auto raw_connection = make_unique<mtproto::RawConnection>(std::move(data.socket_fd), get_transport(), nullptr);
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

  void timeout_expired() override {
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

class GetMeRequest : public RequestActor<> {
  UserId user_id_;

  void do_run(Promise<Unit> &&promise) override {
    user_id_ = td->contacts_manager_->get_me(std::move(promise));
  }

  void do_send_result() override {
    send_result(td->contacts_manager_->get_user_object(user_id_));
  }

 public:
  GetMeRequest(ActorShared<Td> td, uint64 request_id) : RequestActor(std::move(td), request_id) {
  }
};

class GetUserRequest : public RequestActor<> {
  UserId user_id_;

  void do_run(Promise<Unit> &&promise) override {
    td->contacts_manager_->get_user(user_id_, get_tries(), std::move(promise));
  }

  void do_send_result() override {
    send_result(td->contacts_manager_->get_user_object(user_id_));
  }

 public:
  GetUserRequest(ActorShared<Td> td, uint64 request_id, int32 user_id)
      : RequestActor(std::move(td), request_id), user_id_(user_id) {
    set_tries(3);
  }
};

class GetUserFullInfoRequest : public RequestActor<> {
  UserId user_id_;

  void do_run(Promise<Unit> &&promise) override {
    td->contacts_manager_->load_user_full(user_id_, get_tries() < 2, std::move(promise));
  }

  void do_send_result() override {
    send_result(td->contacts_manager_->get_user_full_info_object(user_id_));
  }

 public:
  GetUserFullInfoRequest(ActorShared<Td> td, uint64 request_id, int32 user_id)
      : RequestActor(std::move(td), request_id), user_id_(user_id) {
  }
};

class GetGroupRequest : public RequestActor<> {
  ChatId chat_id_;

  void do_run(Promise<Unit> &&promise) override {
    td->contacts_manager_->get_chat(chat_id_, get_tries(), std::move(promise));
  }

  void do_send_result() override {
    send_result(td->contacts_manager_->get_basic_group_object(chat_id_));
  }

 public:
  GetGroupRequest(ActorShared<Td> td, uint64 request_id, int32 chat_id)
      : RequestActor(std::move(td), request_id), chat_id_(chat_id) {
    set_tries(3);
  }
};

class GetGroupFullInfoRequest : public RequestActor<> {
  ChatId chat_id_;

  void do_run(Promise<Unit> &&promise) override {
    td->contacts_manager_->load_chat_full(chat_id_, get_tries() < 2, std::move(promise), "getBasicGroupFullInfo");
  }

  void do_send_result() override {
    send_result(td->contacts_manager_->get_basic_group_full_info_object(chat_id_));
  }

 public:
  GetGroupFullInfoRequest(ActorShared<Td> td, uint64 request_id, int32 chat_id)
      : RequestActor(std::move(td), request_id), chat_id_(chat_id) {
  }
};

class GetSupergroupRequest : public RequestActor<> {
  ChannelId channel_id_;

  void do_run(Promise<Unit> &&promise) override {
    td->contacts_manager_->get_channel(channel_id_, get_tries(), std::move(promise));
  }

  void do_send_result() override {
    send_result(td->contacts_manager_->get_supergroup_object(channel_id_));
  }

 public:
  GetSupergroupRequest(ActorShared<Td> td, uint64 request_id, int32 channel_id)
      : RequestActor(std::move(td), request_id), channel_id_(channel_id) {
    set_tries(3);
  }
};

class GetSupergroupFullInfoRequest : public RequestActor<> {
  ChannelId channel_id_;

  void do_run(Promise<Unit> &&promise) override {
    td->contacts_manager_->load_channel_full(channel_id_, get_tries() < 2, std::move(promise));
  }

  void do_send_result() override {
    send_result(td->contacts_manager_->get_supergroup_full_info_object(channel_id_));
  }

 public:
  GetSupergroupFullInfoRequest(ActorShared<Td> td, uint64 request_id, int32 channel_id)
      : RequestActor(std::move(td), request_id), channel_id_(channel_id) {
  }
};

class GetSecretChatRequest : public RequestActor<> {
  SecretChatId secret_chat_id_;

  void do_run(Promise<Unit> &&promise) override {
    td->contacts_manager_->get_secret_chat(secret_chat_id_, get_tries() < 2, std::move(promise));
  }

  void do_send_result() override {
    send_result(td->contacts_manager_->get_secret_chat_object(secret_chat_id_));
  }

 public:
  GetSecretChatRequest(ActorShared<Td> td, uint64 request_id, int32 secret_chat_id)
      : RequestActor(std::move(td), request_id), secret_chat_id_(secret_chat_id) {
  }
};

class GetChatRequest : public RequestActor<> {
  DialogId dialog_id_;

  bool dialog_found_ = false;

  void do_run(Promise<Unit> &&promise) override {
    dialog_found_ = td->messages_manager_->load_dialog(dialog_id_, get_tries(), std::move(promise));
  }

  void do_send_result() override {
    if (!dialog_found_) {
      send_error(Status::Error(400, "Chat is not accessible"));
    } else {
      send_result(td->messages_manager_->get_chat_object(dialog_id_));
    }
  }

 public:
  GetChatRequest(ActorShared<Td> td, uint64 request_id, int64 dialog_id)
      : RequestActor(std::move(td), request_id), dialog_id_(dialog_id) {
    set_tries(3);
  }
};

class GetChatFilterRequest : public RequestActor<> {
  DialogFilterId dialog_filter_id_;

  void do_run(Promise<Unit> &&promise) override {
    td->messages_manager_->load_dialog_filter(dialog_filter_id_, get_tries() < 2, std::move(promise));
  }

  void do_send_result() override {
    send_result(td->messages_manager_->get_chat_filter_object(dialog_filter_id_));
  }

 public:
  GetChatFilterRequest(ActorShared<Td> td, uint64 request_id, int32 dialog_filter_id)
      : RequestActor(std::move(td), request_id), dialog_filter_id_(dialog_filter_id) {
    set_tries(3);
  }
};

class GetChatsRequest : public RequestActor<> {
  DialogListId dialog_list_id_;
  DialogDate offset_;
  int32 limit_;

  std::pair<int32, vector<DialogId>> dialog_ids_;

  void do_run(Promise<Unit> &&promise) override {
    dialog_ids_ =
        td->messages_manager_->get_dialogs(dialog_list_id_, offset_, limit_, get_tries() < 2, std::move(promise));
  }

  void do_send_result() override {
    send_result(MessagesManager::get_chats_object(dialog_ids_));
  }

 public:
  GetChatsRequest(ActorShared<Td> td, uint64 request_id, DialogListId dialog_list_id, int64 offset_order,
                  int64 offset_dialog_id, int32 limit)
      : RequestActor(std::move(td), request_id)
      , dialog_list_id_(dialog_list_id)
      , offset_(offset_order, DialogId(offset_dialog_id))
      , limit_(limit) {
    // 1 for database + 1 for server request + 1 for server request at the end + 1 for return + 1 just in case
    set_tries(5);
  }
};

class SearchPublicChatRequest : public RequestActor<> {
  string username_;

  DialogId dialog_id_;

  void do_run(Promise<Unit> &&promise) override {
    dialog_id_ = td->messages_manager_->search_public_dialog(username_, get_tries() < 3, std::move(promise));
  }

  void do_send_result() override {
    send_result(td->messages_manager_->get_chat_object(dialog_id_));
  }

 public:
  SearchPublicChatRequest(ActorShared<Td> td, uint64 request_id, string username)
      : RequestActor(std::move(td), request_id), username_(std::move(username)) {
    set_tries(3);
  }
};

class SearchPublicChatsRequest : public RequestActor<> {
  string query_;

  vector<DialogId> dialog_ids_;

  void do_run(Promise<Unit> &&promise) override {
    dialog_ids_ = td->messages_manager_->search_public_dialogs(query_, std::move(promise));
  }

  void do_send_result() override {
    send_result(MessagesManager::get_chats_object(-1, dialog_ids_));
  }

 public:
  SearchPublicChatsRequest(ActorShared<Td> td, uint64 request_id, string query)
      : RequestActor(std::move(td), request_id), query_(std::move(query)) {
  }
};

class SearchChatsRequest : public RequestActor<> {
  string query_;
  int32 limit_;

  std::pair<int32, vector<DialogId>> dialog_ids_;

  void do_run(Promise<Unit> &&promise) override {
    dialog_ids_ = td->messages_manager_->search_dialogs(query_, limit_, std::move(promise));
  }

  void do_send_result() override {
    send_result(MessagesManager::get_chats_object(dialog_ids_));
  }

 public:
  SearchChatsRequest(ActorShared<Td> td, uint64 request_id, string query, int32 limit)
      : RequestActor(std::move(td), request_id), query_(std::move(query)), limit_(limit) {
  }
};

class SearchChatsOnServerRequest : public RequestActor<> {
  string query_;
  int32 limit_;

  vector<DialogId> dialog_ids_;

  void do_run(Promise<Unit> &&promise) override {
    dialog_ids_ = td->messages_manager_->search_dialogs_on_server(query_, limit_, std::move(promise));
  }

  void do_send_result() override {
    send_result(MessagesManager::get_chats_object(-1, dialog_ids_));
  }

 public:
  SearchChatsOnServerRequest(ActorShared<Td> td, uint64 request_id, string query, int32 limit)
      : RequestActor(std::move(td), request_id), query_(std::move(query)), limit_(limit) {
  }
};

class GetGroupsInCommonRequest : public RequestActor<> {
  UserId user_id_;
  DialogId offset_dialog_id_;
  int32 limit_;

  std::pair<int32, vector<DialogId>> dialog_ids_;

  void do_run(Promise<Unit> &&promise) override {
    dialog_ids_ = td->messages_manager_->get_common_dialogs(user_id_, offset_dialog_id_, limit_, get_tries() < 2,
                                                            std::move(promise));
  }

  void do_send_result() override {
    send_result(MessagesManager::get_chats_object(dialog_ids_));
  }

 public:
  GetGroupsInCommonRequest(ActorShared<Td> td, uint64 request_id, int32 user_id, int64 offset_dialog_id, int32 limit)
      : RequestActor(std::move(td), request_id), user_id_(user_id), offset_dialog_id_(offset_dialog_id), limit_(limit) {
  }
};

class GetCreatedPublicChatsRequest : public RequestActor<> {
  vector<DialogId> dialog_ids_;
  PublicDialogType type_;

  void do_run(Promise<Unit> &&promise) override {
    dialog_ids_ = td->contacts_manager_->get_created_public_dialogs(type_, std::move(promise));
  }

  void do_send_result() override {
    send_result(MessagesManager::get_chats_object(-1, dialog_ids_));
  }

 public:
  GetCreatedPublicChatsRequest(ActorShared<Td> td, uint64 request_id, PublicDialogType type)
      : RequestActor(std::move(td), request_id), type_(type) {
  }
};

class GetSuitableDiscussionChatsRequest : public RequestActor<> {
  vector<DialogId> dialog_ids_;

  void do_run(Promise<Unit> &&promise) override {
    dialog_ids_ = td->contacts_manager_->get_dialogs_for_discussion(std::move(promise));
  }

  void do_send_result() override {
    send_result(MessagesManager::get_chats_object(-1, dialog_ids_));
  }

 public:
  GetSuitableDiscussionChatsRequest(ActorShared<Td> td, uint64 request_id) : RequestActor(std::move(td), request_id) {
  }
};

class GetInactiveSupergroupChatsRequest : public RequestActor<> {
  vector<DialogId> dialog_ids_;

  void do_run(Promise<Unit> &&promise) override {
    dialog_ids_ = td->contacts_manager_->get_inactive_channels(std::move(promise));
  }

  void do_send_result() override {
    send_result(MessagesManager::get_chats_object(-1, dialog_ids_));
  }

 public:
  GetInactiveSupergroupChatsRequest(ActorShared<Td> td, uint64 request_id) : RequestActor(std::move(td), request_id) {
  }
};

class GetMessageRequest : public RequestOnceActor {
  FullMessageId full_message_id_;

  void do_run(Promise<Unit> &&promise) override {
    td->messages_manager_->get_message(full_message_id_, std::move(promise));
  }

  void do_send_result() override {
    send_result(td->messages_manager_->get_message_object(full_message_id_));
  }

 public:
  GetMessageRequest(ActorShared<Td> td, uint64 request_id, int64 dialog_id, int64 message_id)
      : RequestOnceActor(std::move(td), request_id), full_message_id_(DialogId(dialog_id), MessageId(message_id)) {
  }
};

class GetRepliedMessageRequest : public RequestOnceActor {
  DialogId dialog_id_;
  MessageId message_id_;

  FullMessageId replied_message_id_;

  void do_run(Promise<Unit> &&promise) override {
    replied_message_id_ =
        td->messages_manager_->get_replied_message(dialog_id_, message_id_, get_tries() < 3, std::move(promise));
  }

  void do_send_result() override {
    send_result(td->messages_manager_->get_message_object(replied_message_id_));
  }

 public:
  GetRepliedMessageRequest(ActorShared<Td> td, uint64 request_id, int64 dialog_id, int64 message_id)
      : RequestOnceActor(std::move(td), request_id), dialog_id_(dialog_id), message_id_(message_id) {
    set_tries(3);  // 1 to get initial message, 1 to get the reply and 1 for result
  }
};

class GetMessageThreadRequest : public RequestActor<MessagesManager::MessageThreadInfo> {
  DialogId dialog_id_;
  MessageId message_id_;

  MessagesManager::MessageThreadInfo message_thread_info_;

  void do_run(Promise<MessagesManager::MessageThreadInfo> &&promise) override {
    if (get_tries() < 2) {
      promise.set_value(std::move(message_thread_info_));
      return;
    }
    td->messages_manager_->get_message_thread(dialog_id_, message_id_, std::move(promise));
  }

  void do_set_result(MessagesManager::MessageThreadInfo &&result) override {
    message_thread_info_ = std::move(result);
  }

  void do_send_result() override {
    send_result(td->messages_manager_->get_message_thread_info_object(message_thread_info_));
  }

 public:
  GetMessageThreadRequest(ActorShared<Td> td, uint64 request_id, int64 dialog_id, int64 message_id)
      : RequestActor(std::move(td), request_id), dialog_id_(dialog_id), message_id_(message_id) {
  }
};

class GetChatPinnedMessageRequest : public RequestOnceActor {
  DialogId dialog_id_;

  MessageId pinned_message_id_;

  void do_run(Promise<Unit> &&promise) override {
    pinned_message_id_ = td->messages_manager_->get_dialog_pinned_message(dialog_id_, std::move(promise));
  }

  void do_send_result() override {
    send_result(td->messages_manager_->get_message_object({dialog_id_, pinned_message_id_}));
  }

 public:
  GetChatPinnedMessageRequest(ActorShared<Td> td, uint64 request_id, int64 dialog_id)
      : RequestOnceActor(std::move(td), request_id), dialog_id_(dialog_id) {
    set_tries(3);  // 1 to get pinned_message_id, 1 to get the message and 1 for result
  }
};

class GetCallbackQueryMessageRequest : public RequestOnceActor {
  DialogId dialog_id_;
  MessageId message_id_;
  int64 callback_query_id_;

  void do_run(Promise<Unit> &&promise) override {
    td->messages_manager_->get_callback_query_message(dialog_id_, message_id_, callback_query_id_, std::move(promise));
  }

  void do_send_result() override {
    send_result(td->messages_manager_->get_message_object({dialog_id_, message_id_}));
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

class GetMessagesRequest : public RequestOnceActor {
  DialogId dialog_id_;
  vector<MessageId> message_ids_;

  void do_run(Promise<Unit> &&promise) override {
    td->messages_manager_->get_messages(dialog_id_, message_ids_, std::move(promise));
  }

  void do_send_result() override {
    send_result(td->messages_manager_->get_messages_object(-1, dialog_id_, message_ids_, false));
  }

 public:
  GetMessagesRequest(ActorShared<Td> td, uint64 request_id, int64 dialog_id, const vector<int64> &message_ids)
      : RequestOnceActor(std::move(td), request_id)
      , dialog_id_(dialog_id)
      , message_ids_(MessagesManager::get_message_ids(message_ids)) {
  }
};

class GetMessageEmbeddingCodeRequest : public RequestActor<> {
  FullMessageId full_message_id_;
  bool for_group_;

  string html_;

  void do_run(Promise<Unit> &&promise) override {
    html_ = td->messages_manager_->get_message_embedding_code(full_message_id_, for_group_, std::move(promise));
  }

  void do_send_result() override {
    send_result(make_tl_object<td_api::text>(html_));
  }

 public:
  GetMessageEmbeddingCodeRequest(ActorShared<Td> td, uint64 request_id, int64 dialog_id, int64 message_id,
                                 bool for_group)
      : RequestActor(std::move(td), request_id)
      , full_message_id_(DialogId(dialog_id), MessageId(message_id))
      , for_group_(for_group) {
  }
};

class GetMessageLinkInfoRequest : public RequestActor<MessagesManager::MessageLinkInfo> {
  string url_;

  MessagesManager::MessageLinkInfo message_link_info_;

  void do_run(Promise<MessagesManager::MessageLinkInfo> &&promise) override {
    if (get_tries() < 2) {
      promise.set_value(std::move(message_link_info_));
      return;
    }
    td->messages_manager_->get_message_link_info(url_, std::move(promise));
  }

  void do_set_result(MessagesManager::MessageLinkInfo &&result) override {
    message_link_info_ = std::move(result);
  }

  void do_send_result() override {
    send_result(td->messages_manager_->get_message_link_info_object(message_link_info_));
  }

 public:
  GetMessageLinkInfoRequest(ActorShared<Td> td, uint64 request_id, string url)
      : RequestActor(std::move(td), request_id), url_(std::move(url)) {
  }
};

class EditMessageTextRequest : public RequestOnceActor {
  FullMessageId full_message_id_;
  tl_object_ptr<td_api::ReplyMarkup> reply_markup_;
  tl_object_ptr<td_api::InputMessageContent> input_message_content_;

  void do_run(Promise<Unit> &&promise) override {
    td->messages_manager_->edit_message_text(full_message_id_, std::move(reply_markup_),
                                             std::move(input_message_content_), std::move(promise));
  }

  void do_send_result() override {
    send_result(td->messages_manager_->get_message_object(full_message_id_));
  }

 public:
  EditMessageTextRequest(ActorShared<Td> td, uint64 request_id, int64 dialog_id, int64 message_id,
                         tl_object_ptr<td_api::ReplyMarkup> reply_markup,
                         tl_object_ptr<td_api::InputMessageContent> input_message_content)
      : RequestOnceActor(std::move(td), request_id)
      , full_message_id_(DialogId(dialog_id), MessageId(message_id))
      , reply_markup_(std::move(reply_markup))
      , input_message_content_(std::move(input_message_content)) {
  }
};

class EditMessageLiveLocationRequest : public RequestOnceActor {
  FullMessageId full_message_id_;
  tl_object_ptr<td_api::ReplyMarkup> reply_markup_;
  tl_object_ptr<td_api::location> location_;
  int32 heading_;
  int32 proximity_alert_radius_;

  void do_run(Promise<Unit> &&promise) override {
    td->messages_manager_->edit_message_live_location(full_message_id_, std::move(reply_markup_), std::move(location_),
                                                      heading_, proximity_alert_radius_, std::move(promise));
  }

  void do_send_result() override {
    send_result(td->messages_manager_->get_message_object(full_message_id_));
  }

 public:
  EditMessageLiveLocationRequest(ActorShared<Td> td, uint64 request_id, int64 dialog_id, int64 message_id,
                                 tl_object_ptr<td_api::ReplyMarkup> reply_markup,
                                 tl_object_ptr<td_api::location> location, int32 heading, int32 proximity_alert_radius)
      : RequestOnceActor(std::move(td), request_id)
      , full_message_id_(DialogId(dialog_id), MessageId(message_id))
      , reply_markup_(std::move(reply_markup))
      , location_(std::move(location))
      , heading_(heading)
      , proximity_alert_radius_(proximity_alert_radius) {
  }
};

class EditMessageMediaRequest : public RequestOnceActor {
  FullMessageId full_message_id_;
  tl_object_ptr<td_api::ReplyMarkup> reply_markup_;
  tl_object_ptr<td_api::InputMessageContent> input_message_content_;

  void do_run(Promise<Unit> &&promise) override {
    td->messages_manager_->edit_message_media(full_message_id_, std::move(reply_markup_),
                                              std::move(input_message_content_), std::move(promise));
  }

  void do_send_result() override {
    send_result(td->messages_manager_->get_message_object(full_message_id_));
  }

 public:
  EditMessageMediaRequest(ActorShared<Td> td, uint64 request_id, int64 dialog_id, int64 message_id,
                          tl_object_ptr<td_api::ReplyMarkup> reply_markup,
                          tl_object_ptr<td_api::InputMessageContent> input_message_content)
      : RequestOnceActor(std::move(td), request_id)
      , full_message_id_(DialogId(dialog_id), MessageId(message_id))
      , reply_markup_(std::move(reply_markup))
      , input_message_content_(std::move(input_message_content)) {
  }
};

class EditMessageCaptionRequest : public RequestOnceActor {
  FullMessageId full_message_id_;
  tl_object_ptr<td_api::ReplyMarkup> reply_markup_;
  tl_object_ptr<td_api::formattedText> caption_;

  void do_run(Promise<Unit> &&promise) override {
    td->messages_manager_->edit_message_caption(full_message_id_, std::move(reply_markup_), std::move(caption_),
                                                std::move(promise));
  }

  void do_send_result() override {
    send_result(td->messages_manager_->get_message_object(full_message_id_));
  }

 public:
  EditMessageCaptionRequest(ActorShared<Td> td, uint64 request_id, int64 dialog_id, int64 message_id,
                            tl_object_ptr<td_api::ReplyMarkup> reply_markup,
                            tl_object_ptr<td_api::formattedText> caption)
      : RequestOnceActor(std::move(td), request_id)
      , full_message_id_(DialogId(dialog_id), MessageId(message_id))
      , reply_markup_(std::move(reply_markup))
      , caption_(std::move(caption)) {
  }
};

class EditMessageReplyMarkupRequest : public RequestOnceActor {
  FullMessageId full_message_id_;
  tl_object_ptr<td_api::ReplyMarkup> reply_markup_;

  void do_run(Promise<Unit> &&promise) override {
    td->messages_manager_->edit_message_reply_markup(full_message_id_, std::move(reply_markup_), std::move(promise));
  }

  void do_send_result() override {
    send_result(td->messages_manager_->get_message_object(full_message_id_));
  }

 public:
  EditMessageReplyMarkupRequest(ActorShared<Td> td, uint64 request_id, int64 dialog_id, int64 message_id,
                                tl_object_ptr<td_api::ReplyMarkup> reply_markup)
      : RequestOnceActor(std::move(td), request_id)
      , full_message_id_(DialogId(dialog_id), MessageId(message_id))
      , reply_markup_(std::move(reply_markup)) {
  }
};

class SetGameScoreRequest : public RequestOnceActor {
  FullMessageId full_message_id_;
  bool edit_message_;
  UserId user_id_;
  int32 score_;
  bool force_;

  void do_run(Promise<Unit> &&promise) override {
    td->messages_manager_->set_game_score(full_message_id_, edit_message_, user_id_, score_, force_,
                                          std::move(promise));
  }

  void do_send_result() override {
    send_result(td->messages_manager_->get_message_object(full_message_id_));
  }

 public:
  SetGameScoreRequest(ActorShared<Td> td, uint64 request_id, int64 dialog_id, int64 message_id, bool edit_message,
                      int32 user_id, int32 score, bool force)
      : RequestOnceActor(std::move(td), request_id)
      , full_message_id_(DialogId(dialog_id), MessageId(message_id))
      , edit_message_(edit_message)
      , user_id_(user_id)
      , score_(score)
      , force_(force) {
  }
};

class GetGameHighScoresRequest : public RequestOnceActor {
  FullMessageId full_message_id_;
  UserId user_id_;

  int64 random_id_;

  void do_run(Promise<Unit> &&promise) override {
    random_id_ = td->messages_manager_->get_game_high_scores(full_message_id_, user_id_, std::move(promise));
  }

  void do_send_result() override {
    CHECK(random_id_ != 0);
    send_result(td->messages_manager_->get_game_high_scores_object(random_id_));
  }

 public:
  GetGameHighScoresRequest(ActorShared<Td> td, uint64 request_id, int64 dialog_id, int64 message_id, int32 user_id)
      : RequestOnceActor(std::move(td), request_id)
      , full_message_id_(DialogId(dialog_id), MessageId(message_id))
      , user_id_(user_id)
      , random_id_(0) {
  }
};

class GetInlineGameHighScoresRequest : public RequestOnceActor {
  string inline_message_id_;
  UserId user_id_;

  int64 random_id_;

  void do_run(Promise<Unit> &&promise) override {
    random_id_ = td->messages_manager_->get_inline_game_high_scores(inline_message_id_, user_id_, std::move(promise));
  }

  void do_send_result() override {
    CHECK(random_id_ != 0);
    send_result(td->messages_manager_->get_game_high_scores_object(random_id_));
  }

 public:
  GetInlineGameHighScoresRequest(ActorShared<Td> td, uint64 request_id, string inline_message_id, int32 user_id)
      : RequestOnceActor(std::move(td), request_id)
      , inline_message_id_(std::move(inline_message_id))
      , user_id_(user_id)
      , random_id_(0) {
  }
};

class GetChatHistoryRequest : public RequestActor<> {
  DialogId dialog_id_;
  MessageId from_message_id_;
  int32 offset_;
  int32 limit_;
  bool only_local_;

  tl_object_ptr<td_api::messages> messages_;

  void do_run(Promise<Unit> &&promise) override {
    messages_ = td->messages_manager_->get_dialog_history(dialog_id_, from_message_id_, offset_, limit_,
                                                          get_tries() - 1, only_local_, std::move(promise));
  }

  void do_send_result() override {
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

class GetMessageThreadHistoryRequest : public RequestActor<> {
  DialogId dialog_id_;
  MessageId message_id_;
  MessageId from_message_id_;
  int32 offset_;
  int32 limit_;
  int64 random_id_;

  std::pair<DialogId, vector<MessageId>> messages_;

  void do_run(Promise<Unit> &&promise) override {
    messages_ = td->messages_manager_->get_message_thread_history(dialog_id_, message_id_, from_message_id_, offset_,
                                                                  limit_, random_id_, std::move(promise));
  }

  void do_send_result() override {
    send_result(td->messages_manager_->get_messages_object(-1, messages_.first, messages_.second, true));
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

class SearchChatMessagesRequest : public RequestActor<> {
  DialogId dialog_id_;
  string query_;
  td_api::object_ptr<td_api::MessageSender> sender_;
  MessageId from_message_id_;
  int32 offset_;
  int32 limit_;
  MessageSearchFilter filter_;
  MessageId top_thread_message_id_;
  int64 random_id_;

  std::pair<int32, vector<MessageId>> messages_;

  void do_run(Promise<Unit> &&promise) override {
    messages_ = td->messages_manager_->search_dialog_messages(dialog_id_, query_, sender_, from_message_id_, offset_,
                                                              limit_, filter_, top_thread_message_id_, random_id_,
                                                              get_tries() == 3, std::move(promise));
  }

  void do_send_result() override {
    send_result(td->messages_manager_->get_messages_object(messages_.first, dialog_id_, messages_.second, true));
  }

  void do_send_error(Status &&status) override {
    if (status.message() == "SEARCH_QUERY_EMPTY") {
      messages_.first = 0;
      messages_.second.clear();
      return do_send_result();
    }
    send_error(std::move(status));
  }

 public:
  SearchChatMessagesRequest(ActorShared<Td> td, uint64 request_id, int64 dialog_id, string query,
                            td_api::object_ptr<td_api::MessageSender> sender, int64 from_message_id, int32 offset,
                            int32 limit, tl_object_ptr<td_api::SearchMessagesFilter> filter, int64 message_thread_id)
      : RequestActor(std::move(td), request_id)
      , dialog_id_(dialog_id)
      , query_(std::move(query))
      , sender_(std::move(sender))
      , from_message_id_(from_message_id)
      , offset_(offset)
      , limit_(limit)
      , filter_(get_message_search_filter(filter))
      , top_thread_message_id_(message_thread_id)
      , random_id_(0) {
    set_tries(3);
  }
};

class SearchSecretMessagesRequest : public RequestActor<> {
  DialogId dialog_id_;
  string query_;
  string offset_;
  int32 limit_;
  MessageSearchFilter filter_;
  int64 random_id_;

  MessagesManager::FoundMessages found_messages_;

  void do_run(Promise<Unit> &&promise) override {
    found_messages_ = td->messages_manager_->offline_search_messages(dialog_id_, query_, offset_, limit_, filter_,
                                                                     random_id_, std::move(promise));
  }

  void do_send_result() override {
    send_result(td->messages_manager_->get_found_messages_object(found_messages_));
  }

 public:
  SearchSecretMessagesRequest(ActorShared<Td> td, uint64 request_id, int64 dialog_id, string query, string offset,
                              int32 limit, tl_object_ptr<td_api::SearchMessagesFilter> filter)
      : RequestActor(std::move(td), request_id)
      , dialog_id_(dialog_id)
      , query_(std::move(query))
      , offset_(std::move(offset))
      , limit_(limit)
      , filter_(get_message_search_filter(filter))
      , random_id_(0) {
  }
};

class SearchMessagesRequest : public RequestActor<> {
  FolderId folder_id_;
  bool ignore_folder_id_;
  string query_;
  int32 offset_date_;
  DialogId offset_dialog_id_;
  MessageId offset_message_id_;
  int32 limit_;
  MessageSearchFilter filter_;
  int32 min_date_;
  int32 max_date_;
  int64 random_id_;

  std::pair<int32, vector<FullMessageId>> messages_;

  void do_run(Promise<Unit> &&promise) override {
    messages_ = td->messages_manager_->search_messages(folder_id_, ignore_folder_id_, query_, offset_date_,
                                                       offset_dialog_id_, offset_message_id_, limit_, filter_,
                                                       min_date_, max_date_, random_id_, std::move(promise));
  }

  void do_send_result() override {
    send_result(td->messages_manager_->get_messages_object(messages_.first, messages_.second, true));
  }

  void do_send_error(Status &&status) override {
    if (status.message() == "SEARCH_QUERY_EMPTY") {
      messages_.first = 0;
      messages_.second.clear();
      return do_send_result();
    }
    send_error(std::move(status));
  }

 public:
  SearchMessagesRequest(ActorShared<Td> td, uint64 request_id, FolderId folder_id, bool ignore_folder_id, string query,
                        int32 offset_date, int64 offset_dialog_id, int64 offset_message_id, int32 limit,
                        tl_object_ptr<td_api::SearchMessagesFilter> &&filter, int32 min_date, int32 max_date)
      : RequestActor(std::move(td), request_id)
      , folder_id_(folder_id)
      , ignore_folder_id_(ignore_folder_id)
      , query_(std::move(query))
      , offset_date_(offset_date)
      , offset_dialog_id_(offset_dialog_id)
      , offset_message_id_(offset_message_id)
      , limit_(limit)
      , filter_(get_message_search_filter(filter))
      , min_date_(min_date)
      , max_date_(max_date)
      , random_id_(0) {
  }
};

class SearchCallMessagesRequest : public RequestActor<> {
  MessageId from_message_id_;
  int32 limit_;
  bool only_missed_;
  int64 random_id_;

  std::pair<int32, vector<FullMessageId>> messages_;

  void do_run(Promise<Unit> &&promise) override {
    messages_ = td->messages_manager_->search_call_messages(from_message_id_, limit_, only_missed_, random_id_,
                                                            get_tries() == 3, std::move(promise));
  }

  void do_send_result() override {
    send_result(td->messages_manager_->get_messages_object(messages_.first, messages_.second, true));
  }

 public:
  SearchCallMessagesRequest(ActorShared<Td> td, uint64 request_id, int64 from_message_id, int32 limit, bool only_missed)
      : RequestActor(std::move(td), request_id)
      , from_message_id_(from_message_id)
      , limit_(limit)
      , only_missed_(only_missed)
      , random_id_(0) {
    set_tries(3);
  }
};

class SearchChatRecentLocationMessagesRequest : public RequestActor<> {
  DialogId dialog_id_;
  int32 limit_;
  int64 random_id_;

  std::pair<int32, vector<MessageId>> messages_;

  void do_run(Promise<Unit> &&promise) override {
    messages_ = td->messages_manager_->search_dialog_recent_location_messages(dialog_id_, limit_, random_id_,
                                                                              std::move(promise));
  }

  void do_send_result() override {
    send_result(td->messages_manager_->get_messages_object(messages_.first, dialog_id_, messages_.second, true));
  }

 public:
  SearchChatRecentLocationMessagesRequest(ActorShared<Td> td, uint64 request_id, int64 dialog_id, int32 limit)
      : RequestActor(std::move(td), request_id), dialog_id_(dialog_id), limit_(limit), random_id_(0) {
  }
};

class GetActiveLiveLocationMessagesRequest : public RequestActor<> {
  vector<FullMessageId> full_message_ids_;

  void do_run(Promise<Unit> &&promise) override {
    full_message_ids_ = td->messages_manager_->get_active_live_location_messages(std::move(promise));
  }

  void do_send_result() override {
    send_result(td->messages_manager_->get_messages_object(-1, full_message_ids_, true));
  }

 public:
  GetActiveLiveLocationMessagesRequest(ActorShared<Td> td, uint64 request_id)
      : RequestActor(std::move(td), request_id) {
  }
};

class GetChatMessageByDateRequest : public RequestOnceActor {
  DialogId dialog_id_;
  int32 date_;

  int64 random_id_;

  void do_run(Promise<Unit> &&promise) override {
    random_id_ = td->messages_manager_->get_dialog_message_by_date(dialog_id_, date_, std::move(promise));
  }

  void do_send_result() override {
    send_result(td->messages_manager_->get_dialog_message_by_date_object(random_id_));
  }

 public:
  GetChatMessageByDateRequest(ActorShared<Td> td, uint64 request_id, int64 dialog_id, int32 date)
      : RequestOnceActor(std::move(td), request_id), dialog_id_(dialog_id), date_(date), random_id_(0) {
  }
};

class GetChatMessageCountRequest : public RequestActor<> {
  DialogId dialog_id_;
  MessageSearchFilter filter_;
  bool return_local_;
  int64 random_id_;

  int32 result_ = 0;

  void do_run(Promise<Unit> &&promise) override {
    result_ = td->messages_manager_->get_dialog_message_count(dialog_id_, filter_, return_local_, random_id_,
                                                              std::move(promise));
  }

  void do_send_result() override {
    send_result(td_api::make_object<td_api::count>(result_));
  }

 public:
  GetChatMessageCountRequest(ActorShared<Td> td, uint64 request_id, int64 dialog_id,
                             tl_object_ptr<td_api::SearchMessagesFilter> filter, bool return_local)
      : RequestActor(std::move(td), request_id)
      , dialog_id_(dialog_id)
      , filter_(get_message_search_filter(filter))
      , return_local_(return_local)
      , random_id_(0) {
  }
};

class GetChatScheduledMessagesRequest : public RequestActor<> {
  DialogId dialog_id_;

  vector<MessageId> message_ids_;

  void do_run(Promise<Unit> &&promise) override {
    message_ids_ =
        td->messages_manager_->get_dialog_scheduled_messages(dialog_id_, get_tries() < 2, false, std::move(promise));
  }

  void do_send_result() override {
    send_result(td->messages_manager_->get_messages_object(-1, dialog_id_, message_ids_, true));
  }

 public:
  GetChatScheduledMessagesRequest(ActorShared<Td> td, uint64 request_id, int64 dialog_id)
      : RequestActor(std::move(td), request_id), dialog_id_(dialog_id) {
    set_tries(4);
  }
};

class GetMessagePublicForwardsRequest : public RequestActor<> {
  FullMessageId full_message_id_;
  string offset_;
  int32 limit_;
  int64 random_id_;

  MessagesManager::FoundMessages messages_;

  void do_run(Promise<Unit> &&promise) override {
    messages_ = td->messages_manager_->get_message_public_forwards(full_message_id_, offset_, limit_, random_id_,
                                                                   std::move(promise));
  }

  void do_send_result() override {
    send_result(td->messages_manager_->get_found_messages_object(messages_));
  }

 public:
  GetMessagePublicForwardsRequest(ActorShared<Td> td, uint64 request_id, int64 dialog_id, int64 message_id,
                                  string offset, int32 limit)
      : RequestActor(std::move(td), request_id)
      , full_message_id_(DialogId(dialog_id), MessageId(message_id))
      , offset_(std::move(offset))
      , limit_(limit)
      , random_id_(0) {
  }
};

class GetWebPagePreviewRequest : public RequestOnceActor {
  td_api::object_ptr<td_api::formattedText> text_;

  int64 request_id_ = 0;

  void do_run(Promise<Unit> &&promise) override {
    request_id_ = td->web_pages_manager_->get_web_page_preview(std::move(text_), std::move(promise));
  }

  void do_send_result() override {
    send_result(td->web_pages_manager_->get_web_page_preview_result(request_id_));
  }

 public:
  GetWebPagePreviewRequest(ActorShared<Td> td, uint64 request_id, td_api::object_ptr<td_api::formattedText> text)
      : RequestOnceActor(std::move(td), request_id), text_(std::move(text)) {
  }
};

class GetWebPageInstantViewRequest : public RequestActor<> {
  string url_;
  bool force_full_;

  WebPageId web_page_id_;

  void do_run(Promise<Unit> &&promise) override {
    web_page_id_ =
        td->web_pages_manager_->get_web_page_instant_view(url_, force_full_, get_tries() < 3, std::move(promise));
  }

  void do_send_result() override {
    send_result(td->web_pages_manager_->get_web_page_instant_view_object(web_page_id_));
  }

 public:
  GetWebPageInstantViewRequest(ActorShared<Td> td, uint64 request_id, string url, bool force_full)
      : RequestActor(std::move(td), request_id), url_(std::move(url)), force_full_(force_full) {
    set_tries(3);
  }
};

class CreateChatRequest : public RequestActor<> {
  DialogId dialog_id_;
  bool force_;

  void do_run(Promise<Unit> &&promise) override {
    td->messages_manager_->create_dialog(dialog_id_, force_, std::move(promise));
  }

  void do_send_result() override {
    send_result(td->messages_manager_->get_chat_object(dialog_id_));
  }

 public:
  CreateChatRequest(ActorShared<Td> td, uint64 request_id, DialogId dialog_id, bool force)
      : RequestActor<>(std::move(td), request_id), dialog_id_(dialog_id), force_(force) {
  }
};

class CreateNewGroupChatRequest : public RequestActor<> {
  vector<UserId> user_ids_;
  string title_;
  int64 random_id_;

  DialogId dialog_id_;

  void do_run(Promise<Unit> &&promise) override {
    dialog_id_ = td->messages_manager_->create_new_group_chat(user_ids_, title_, random_id_, std::move(promise));
  }

  void do_send_result() override {
    CHECK(dialog_id_.is_valid());
    send_result(td->messages_manager_->get_chat_object(dialog_id_));
  }

 public:
  CreateNewGroupChatRequest(ActorShared<Td> td, uint64 request_id, vector<int32> user_ids, string title)
      : RequestActor(std::move(td), request_id), title_(std::move(title)), random_id_(0) {
    for (auto user_id : user_ids) {
      user_ids_.emplace_back(user_id);
    }
  }
};

class CreateNewSecretChatRequest : public RequestActor<SecretChatId> {
  UserId user_id_;
  SecretChatId secret_chat_id_;

  void do_run(Promise<SecretChatId> &&promise) override {
    if (get_tries() < 2) {
      promise.set_value(std::move(secret_chat_id_));
      return;
    }
    td->messages_manager_->create_new_secret_chat(user_id_, std::move(promise));
  }

  void do_set_result(SecretChatId &&result) override {
    secret_chat_id_ = result;
    LOG(INFO) << "New " << secret_chat_id_ << " created";
  }

  void do_send_result() override {
    CHECK(secret_chat_id_.is_valid());
    // SecretChatActor will send this update by himself.
    // But since the update may still be on its way, we will update essential fields here.
    td->contacts_manager_->on_update_secret_chat(
        secret_chat_id_, 0 /* no access_hash */, user_id_, SecretChatState::Unknown, true /* it is outbound chat */,
        -1 /* unknown TTL */, 0 /* unknown creation date */, "" /* no key_hash */, 0, FolderId());
    DialogId dialog_id(secret_chat_id_);
    td->messages_manager_->force_create_dialog(dialog_id, "create new secret chat", true);
    send_result(td->messages_manager_->get_chat_object(dialog_id));
  }

 public:
  CreateNewSecretChatRequest(ActorShared<Td> td, uint64 request_id, int32 user_id)
      : RequestActor(std::move(td), request_id), user_id_(user_id) {
  }
};

class CreateNewSupergroupChatRequest : public RequestActor<> {
  string title_;
  bool is_megagroup_;
  string description_;
  DialogLocation location_;
  bool for_import_;
  int64 random_id_;

  DialogId dialog_id_;

  void do_run(Promise<Unit> &&promise) override {
    dialog_id_ = td->messages_manager_->create_new_channel_chat(title_, is_megagroup_, description_, location_,
                                                                for_import_, random_id_, std::move(promise));
  }

  void do_send_result() override {
    CHECK(dialog_id_.is_valid());
    send_result(td->messages_manager_->get_chat_object(dialog_id_));
  }

 public:
  CreateNewSupergroupChatRequest(ActorShared<Td> td, uint64 request_id, string title, bool is_megagroup,
                                 string description, td_api::object_ptr<td_api::chatLocation> &&location,
                                 bool for_import)
      : RequestActor(std::move(td), request_id)
      , title_(std::move(title))
      , is_megagroup_(is_megagroup)
      , description_(std::move(description))
      , location_(std::move(location))
      , for_import_(for_import)
      , random_id_(0) {
  }
};

class UpgradeGroupChatToSupergroupChatRequest : public RequestActor<> {
  string title_;
  DialogId dialog_id_;

  DialogId result_dialog_id_;

  void do_run(Promise<Unit> &&promise) override {
    result_dialog_id_ = td->messages_manager_->migrate_dialog_to_megagroup(dialog_id_, std::move(promise));
  }

  void do_send_result() override {
    CHECK(result_dialog_id_.is_valid());
    send_result(td->messages_manager_->get_chat_object(result_dialog_id_));
  }

 public:
  UpgradeGroupChatToSupergroupChatRequest(ActorShared<Td> td, uint64 request_id, int64 dialog_id)
      : RequestActor(std::move(td), request_id), dialog_id_(dialog_id) {
  }
};

class GetChatMemberRequest : public RequestActor<> {
  DialogId dialog_id_;
  UserId user_id_;
  int64 random_id_;

  DialogParticipant dialog_participant_;

  void do_run(Promise<Unit> &&promise) override {
    dialog_participant_ = td->contacts_manager_->get_dialog_participant(dialog_id_, user_id_, random_id_,
                                                                        get_tries() < 3, std::move(promise));
  }

  void do_send_result() override {
    if (!td->contacts_manager_->have_user(user_id_)) {
      return send_error(Status::Error(3, "User not found"));
    }
    send_result(td->contacts_manager_->get_chat_member_object(dialog_participant_));
  }

 public:
  GetChatMemberRequest(ActorShared<Td> td, uint64 request_id, int64 dialog_id, int32 user_id)
      : RequestActor(std::move(td), request_id), dialog_id_(dialog_id), user_id_(user_id), random_id_(0) {
    set_tries(3);
  }
};

class GetChatAdministratorsRequest : public RequestActor<> {
  DialogId dialog_id_;

  vector<DialogAdministrator> administrators_;

  void do_run(Promise<Unit> &&promise) override {
    administrators_ = td->contacts_manager_->get_dialog_administrators(dialog_id_, get_tries(), std::move(promise));
  }

  void do_send_result() override {
    auto administrator_objects = transform(
        administrators_, [contacts_manager = td->contacts_manager_.get()](const DialogAdministrator &administrator) {
          return administrator.get_chat_administrator_object(contacts_manager);
        });
    send_result(td_api::make_object<td_api::chatAdministrators>(std::move(administrator_objects)));
  }

 public:
  GetChatAdministratorsRequest(ActorShared<Td> td, uint64 request_id, int64 dialog_id)
      : RequestActor(std::move(td), request_id), dialog_id_(dialog_id) {
    set_tries(3);
  }
};

class CheckChatInviteLinkRequest : public RequestActor<> {
  string invite_link_;

  void do_run(Promise<Unit> &&promise) override {
    td->contacts_manager_->check_dialog_invite_link(invite_link_, std::move(promise));
  }

  void do_send_result() override {
    auto result = td->contacts_manager_->get_chat_invite_link_info_object(invite_link_);
    CHECK(result != nullptr);
    send_result(std::move(result));
  }

 public:
  CheckChatInviteLinkRequest(ActorShared<Td> td, uint64 request_id, string invite_link)
      : RequestActor(std::move(td), request_id), invite_link_(std::move(invite_link)) {
  }
};

class JoinChatByInviteLinkRequest : public RequestActor<DialogId> {
  string invite_link_;

  DialogId dialog_id_;

  void do_run(Promise<DialogId> &&promise) override {
    if (get_tries() < 2) {
      promise.set_value(std::move(dialog_id_));
      return;
    }
    td->contacts_manager_->import_dialog_invite_link(invite_link_, std::move(promise));
  }

  void do_set_result(DialogId &&result) override {
    dialog_id_ = result;
  }

  void do_send_result() override {
    CHECK(dialog_id_.is_valid());
    td->messages_manager_->force_create_dialog(dialog_id_, "join chat by invite link");
    send_result(td->messages_manager_->get_chat_object(dialog_id_));
  }

 public:
  JoinChatByInviteLinkRequest(ActorShared<Td> td, uint64 request_id, string invite_link)
      : RequestActor(std::move(td), request_id), invite_link_(std::move(invite_link)) {
  }
};

class GetChatEventLogRequest : public RequestOnceActor {
  DialogId dialog_id_;
  string query_;
  int64 from_event_id_;
  int32 limit_;
  tl_object_ptr<td_api::chatEventLogFilters> filters_;
  vector<UserId> user_ids_;

  int64 random_id_ = 0;

  void do_run(Promise<Unit> &&promise) override {
    random_id_ = td->messages_manager_->get_dialog_event_log(dialog_id_, query_, from_event_id_, limit_, filters_,
                                                             user_ids_, std::move(promise));
  }

  void do_send_result() override {
    CHECK(random_id_ != 0);
    send_result(td->messages_manager_->get_chat_events_object(random_id_));
  }

 public:
  GetChatEventLogRequest(ActorShared<Td> td, uint64 request_id, int64 dialog_id, string &&query, int64 from_event_id,
                         int32 limit, tl_object_ptr<td_api::chatEventLogFilters> &&filters, vector<int32> user_ids)
      : RequestOnceActor(std::move(td), request_id)
      , dialog_id_(dialog_id)
      , query_(std::move(query))
      , from_event_id_(from_event_id)
      , limit_(limit)
      , filters_(std::move(filters)) {
    for (auto user_id : user_ids) {
      user_ids_.emplace_back(user_id);
    }
  }
};

class GetBlockedMessageSendersRequest : public RequestActor<> {
  int32 offset_;
  int32 limit_;
  int64 random_id_;

  std::pair<int32, vector<DialogId>> message_senders_;

  void do_run(Promise<Unit> &&promise) override {
    message_senders_ = td->messages_manager_->get_blocked_dialogs(offset_, limit_, random_id_, std::move(promise));
  }

  void do_send_result() override {
    auto senders =
        transform(message_senders_.second, [messages_manager = td->messages_manager_.get()](DialogId dialog_id) {
          return messages_manager->get_message_sender_object(dialog_id);
        });
    send_result(td_api::make_object<td_api::messageSenders>(message_senders_.first, std::move(senders)));
  }

 public:
  GetBlockedMessageSendersRequest(ActorShared<Td> td, uint64 request_id, int32 offset, int32 limit)
      : RequestActor(std::move(td), request_id), offset_(offset), limit_(limit), random_id_(0) {
  }
};

class ImportContactsRequest : public RequestActor<> {
  vector<tl_object_ptr<td_api::contact>> contacts_;
  int64 random_id_;

  std::pair<vector<UserId>, vector<int32>> imported_contacts_;

  void do_run(Promise<Unit> &&promise) override {
    imported_contacts_ = td->contacts_manager_->import_contacts(contacts_, random_id_, std::move(promise));
  }

  void do_send_result() override {
    CHECK(imported_contacts_.first.size() == contacts_.size());
    CHECK(imported_contacts_.second.size() == contacts_.size());
    send_result(make_tl_object<td_api::importedContacts>(transform(imported_contacts_.first,
                                                                   [this](UserId user_id) {
                                                                     return td->contacts_manager_->get_user_id_object(
                                                                         user_id, "ImportContactsRequest");
                                                                   }),
                                                         std::move(imported_contacts_.second)));
  }

 public:
  ImportContactsRequest(ActorShared<Td> td, uint64 request_id, vector<tl_object_ptr<td_api::contact>> &&contacts)
      : RequestActor(std::move(td), request_id), contacts_(std::move(contacts)), random_id_(0) {
    set_tries(3);  // load_contacts + import_contacts
  }
};

class SearchContactsRequest : public RequestActor<> {
  string query_;
  int32 limit_;

  std::pair<int32, vector<UserId>> user_ids_;

  void do_run(Promise<Unit> &&promise) override {
    user_ids_ = td->contacts_manager_->search_contacts(query_, limit_, std::move(promise));
  }

  void do_send_result() override {
    send_result(td->contacts_manager_->get_users_object(user_ids_.first, user_ids_.second));
  }

 public:
  SearchContactsRequest(ActorShared<Td> td, uint64 request_id, string query, int32 limit)
      : RequestActor(std::move(td), request_id), query_(std::move(query)), limit_(limit) {
  }
};

class RemoveContactsRequest : public RequestActor<> {
  vector<UserId> user_ids_;

  void do_run(Promise<Unit> &&promise) override {
    td->contacts_manager_->remove_contacts(user_ids_, std::move(promise));
  }

 public:
  RemoveContactsRequest(ActorShared<Td> td, uint64 request_id, vector<int32> &&user_ids)
      : RequestActor(std::move(td), request_id) {
    for (auto user_id : user_ids) {
      user_ids_.emplace_back(user_id);
    }
    set_tries(3);  // load_contacts + delete_contacts
  }
};

class GetImportedContactCountRequest : public RequestActor<> {
  int32 imported_contact_count_ = 0;

  void do_run(Promise<Unit> &&promise) override {
    imported_contact_count_ = td->contacts_manager_->get_imported_contact_count(std::move(promise));
  }

  void do_send_result() override {
    send_result(td_api::make_object<td_api::count>(imported_contact_count_));
  }

 public:
  GetImportedContactCountRequest(ActorShared<Td> td, uint64 request_id) : RequestActor(std::move(td), request_id) {
  }
};

class ChangeImportedContactsRequest : public RequestActor<> {
  vector<tl_object_ptr<td_api::contact>> contacts_;
  size_t contacts_size_;
  int64 random_id_;

  std::pair<vector<UserId>, vector<int32>> imported_contacts_;

  void do_run(Promise<Unit> &&promise) override {
    imported_contacts_ =
        td->contacts_manager_->change_imported_contacts(std::move(contacts_), random_id_, std::move(promise));
  }

  void do_send_result() override {
    CHECK(imported_contacts_.first.size() == contacts_size_);
    CHECK(imported_contacts_.second.size() == contacts_size_);
    send_result(make_tl_object<td_api::importedContacts>(transform(imported_contacts_.first,
                                                                   [this](UserId user_id) {
                                                                     return td->contacts_manager_->get_user_id_object(
                                                                         user_id, "ChangeImportedContactsRequest");
                                                                   }),
                                                         std::move(imported_contacts_.second)));
  }

 public:
  ChangeImportedContactsRequest(ActorShared<Td> td, uint64 request_id,
                                vector<tl_object_ptr<td_api::contact>> &&contacts)
      : RequestActor(std::move(td), request_id)
      , contacts_(std::move(contacts))
      , contacts_size_(contacts_.size())
      , random_id_(0) {
    set_tries(4);  // load_contacts + load_local_contacts + (import_contacts + delete_contacts)
  }
};

class GetRecentInlineBotsRequest : public RequestActor<> {
  vector<UserId> user_ids_;

  void do_run(Promise<Unit> &&promise) override {
    user_ids_ = td->inline_queries_manager_->get_recent_inline_bots(std::move(promise));
  }

  void do_send_result() override {
    send_result(td->contacts_manager_->get_users_object(-1, user_ids_));
  }

 public:
  GetRecentInlineBotsRequest(ActorShared<Td> td, uint64 request_id) : RequestActor(std::move(td), request_id) {
  }
};

class GetUserProfilePhotosRequest : public RequestActor<> {
  UserId user_id_;
  int32 offset_;
  int32 limit_;
  std::pair<int32, vector<const Photo *>> photos;

  void do_run(Promise<Unit> &&promise) override {
    photos = td->contacts_manager_->get_user_profile_photos(user_id_, offset_, limit_, std::move(promise));
  }

  void do_send_result() override {
    // TODO create function get_user_profile_photos_object
    auto result = transform(photos.second, [file_manager = td->file_manager_.get()](const Photo *photo) {
      CHECK(photo != nullptr);
      CHECK(!photo->is_empty());
      return get_chat_photo_object(file_manager, *photo);
    });

    send_result(make_tl_object<td_api::chatPhotos>(photos.first, std::move(result)));
  }

 public:
  GetUserProfilePhotosRequest(ActorShared<Td> td, uint64 request_id, int32 user_id, int32 offset, int32 limit)
      : RequestActor(std::move(td), request_id), user_id_(user_id), offset_(offset), limit_(limit) {
  }
};

class GetChatNotificationSettingsExceptionsRequest : public RequestActor<> {
  NotificationSettingsScope scope_;
  bool filter_scope_;
  bool compare_sound_;

  vector<DialogId> dialog_ids_;

  void do_run(Promise<Unit> &&promise) override {
    dialog_ids_ = td->messages_manager_->get_dialog_notification_settings_exceptions(
        scope_, filter_scope_, compare_sound_, get_tries() < 3, std::move(promise));
  }

  void do_send_result() override {
    send_result(MessagesManager::get_chats_object(-1, dialog_ids_));
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

class GetScopeNotificationSettingsRequest : public RequestActor<> {
  NotificationSettingsScope scope_;

  const ScopeNotificationSettings *notification_settings_ = nullptr;

  void do_run(Promise<Unit> &&promise) override {
    notification_settings_ = td->messages_manager_->get_scope_notification_settings(scope_, std::move(promise));
  }

  void do_send_result() override {
    CHECK(notification_settings_ != nullptr);
    send_result(get_scope_notification_settings_object(notification_settings_));
  }

 public:
  GetScopeNotificationSettingsRequest(ActorShared<Td> td, uint64 request_id, NotificationSettingsScope scope)
      : RequestActor(std::move(td), request_id), scope_(scope) {
  }
};

class GetStickersRequest : public RequestActor<> {
  string emoji_;
  int32 limit_;

  vector<FileId> sticker_ids_;

  void do_run(Promise<Unit> &&promise) override {
    sticker_ids_ = td->stickers_manager_->get_stickers(emoji_, limit_, get_tries() < 2, std::move(promise));
  }

  void do_send_result() override {
    send_result(td->stickers_manager_->get_stickers_object(sticker_ids_));
  }

 public:
  GetStickersRequest(ActorShared<Td> td, uint64 request_id, string &&emoji, int32 limit)
      : RequestActor(std::move(td), request_id), emoji_(std::move(emoji)), limit_(limit) {
    set_tries(5);
  }
};

class SearchStickersRequest : public RequestActor<> {
  string emoji_;
  int32 limit_;

  vector<FileId> sticker_ids_;

  void do_run(Promise<Unit> &&promise) override {
    sticker_ids_ = td->stickers_manager_->search_stickers(emoji_, limit_, std::move(promise));
  }

  void do_send_result() override {
    send_result(td->stickers_manager_->get_stickers_object(sticker_ids_));
  }

 public:
  SearchStickersRequest(ActorShared<Td> td, uint64 request_id, string &&emoji, int32 limit)
      : RequestActor(std::move(td), request_id), emoji_(std::move(emoji)), limit_(limit) {
  }
};

class GetInstalledStickerSetsRequest : public RequestActor<> {
  bool is_masks_;

  vector<StickerSetId> sticker_set_ids_;

  void do_run(Promise<Unit> &&promise) override {
    sticker_set_ids_ = td->stickers_manager_->get_installed_sticker_sets(is_masks_, std::move(promise));
  }

  void do_send_result() override {
    send_result(td->stickers_manager_->get_sticker_sets_object(-1, sticker_set_ids_, 1));
  }

 public:
  GetInstalledStickerSetsRequest(ActorShared<Td> td, uint64 request_id, bool is_masks)
      : RequestActor(std::move(td), request_id), is_masks_(is_masks) {
  }
};

class GetArchivedStickerSetsRequest : public RequestActor<> {
  bool is_masks_;
  StickerSetId offset_sticker_set_id_;
  int32 limit_;

  int32 total_count_;
  vector<StickerSetId> sticker_set_ids_;

  void do_run(Promise<Unit> &&promise) override {
    std::tie(total_count_, sticker_set_ids_) = td->stickers_manager_->get_archived_sticker_sets(
        is_masks_, offset_sticker_set_id_, limit_, get_tries() < 2, std::move(promise));
  }

  void do_send_result() override {
    send_result(td->stickers_manager_->get_sticker_sets_object(total_count_, sticker_set_ids_, 1));
  }

 public:
  GetArchivedStickerSetsRequest(ActorShared<Td> td, uint64 request_id, bool is_masks, int64 offset_sticker_set_id,
                                int32 limit)
      : RequestActor(std::move(td), request_id)
      , is_masks_(is_masks)
      , offset_sticker_set_id_(offset_sticker_set_id)
      , limit_(limit) {
  }
};

class GetTrendingStickerSetsRequest : public RequestActor<> {
  std::pair<int32, vector<StickerSetId>> sticker_set_ids_;
  int32 offset_;
  int32 limit_;

  void do_run(Promise<Unit> &&promise) override {
    sticker_set_ids_ = td->stickers_manager_->get_featured_sticker_sets(offset_, limit_, std::move(promise));
  }

  void do_send_result() override {
    send_result(td->stickers_manager_->get_sticker_sets_object(sticker_set_ids_.first, sticker_set_ids_.second, 5));
  }

 public:
  GetTrendingStickerSetsRequest(ActorShared<Td> td, uint64 request_id, int32 offset, int32 limit)
      : RequestActor(std::move(td), request_id), offset_(offset), limit_(limit) {
    set_tries(3);
  }
};

class GetAttachedStickerSetsRequest : public RequestActor<> {
  FileId file_id_;

  vector<StickerSetId> sticker_set_ids_;

  void do_run(Promise<Unit> &&promise) override {
    sticker_set_ids_ = td->stickers_manager_->get_attached_sticker_sets(file_id_, std::move(promise));
  }

  void do_send_result() override {
    send_result(td->stickers_manager_->get_sticker_sets_object(-1, sticker_set_ids_, 5));
  }

 public:
  GetAttachedStickerSetsRequest(ActorShared<Td> td, uint64 request_id, int32 file_id)
      : RequestActor(std::move(td), request_id), file_id_(file_id, 0) {
  }
};

class GetStickerSetRequest : public RequestActor<> {
  StickerSetId set_id_;

  StickerSetId sticker_set_id_;

  void do_run(Promise<Unit> &&promise) override {
    sticker_set_id_ = td->stickers_manager_->get_sticker_set(set_id_, std::move(promise));
  }

  void do_send_result() override {
    send_result(td->stickers_manager_->get_sticker_set_object(sticker_set_id_));
  }

 public:
  GetStickerSetRequest(ActorShared<Td> td, uint64 request_id, int64 set_id)
      : RequestActor(std::move(td), request_id), set_id_(set_id) {
    set_tries(3);
  }
};

class SearchStickerSetRequest : public RequestActor<> {
  string name_;

  StickerSetId sticker_set_id_;

  void do_run(Promise<Unit> &&promise) override {
    sticker_set_id_ = td->stickers_manager_->search_sticker_set(name_, std::move(promise));
  }

  void do_send_result() override {
    send_result(td->stickers_manager_->get_sticker_set_object(sticker_set_id_));
  }

 public:
  SearchStickerSetRequest(ActorShared<Td> td, uint64 request_id, string &&name)
      : RequestActor(std::move(td), request_id), name_(std::move(name)) {
    set_tries(3);
  }
};

class SearchInstalledStickerSetsRequest : public RequestActor<> {
  bool is_masks_;
  string query_;
  int32 limit_;

  std::pair<int32, vector<StickerSetId>> sticker_set_ids_;

  void do_run(Promise<Unit> &&promise) override {
    sticker_set_ids_ =
        td->stickers_manager_->search_installed_sticker_sets(is_masks_, query_, limit_, std::move(promise));
  }

  void do_send_result() override {
    send_result(td->stickers_manager_->get_sticker_sets_object(sticker_set_ids_.first, sticker_set_ids_.second, 5));
  }

 public:
  SearchInstalledStickerSetsRequest(ActorShared<Td> td, uint64 request_id, bool is_masks, string &&query, int32 limit)
      : RequestActor(std::move(td), request_id), is_masks_(is_masks), query_(std::move(query)), limit_(limit) {
  }
};

class SearchStickerSetsRequest : public RequestActor<> {
  string query_;

  vector<StickerSetId> sticker_set_ids_;

  void do_run(Promise<Unit> &&promise) override {
    sticker_set_ids_ = td->stickers_manager_->search_sticker_sets(query_, std::move(promise));
  }

  void do_send_result() override {
    send_result(td->stickers_manager_->get_sticker_sets_object(-1, sticker_set_ids_, 5));
  }

 public:
  SearchStickerSetsRequest(ActorShared<Td> td, uint64 request_id, string &&query)
      : RequestActor(std::move(td), request_id), query_(std::move(query)) {
  }
};

class ChangeStickerSetRequest : public RequestOnceActor {
  StickerSetId set_id_;
  bool is_installed_;
  bool is_archived_;

  void do_run(Promise<Unit> &&promise) override {
    td->stickers_manager_->change_sticker_set(set_id_, is_installed_, is_archived_, std::move(promise));
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

class UploadStickerFileRequest : public RequestOnceActor {
  UserId user_id_;
  tl_object_ptr<td_api::InputFile> sticker_;

  FileId file_id;

  void do_run(Promise<Unit> &&promise) override {
    file_id = td->stickers_manager_->upload_sticker_file(user_id_, sticker_, std::move(promise));
  }

  void do_send_result() override {
    send_result(td->file_manager_->get_file_object(file_id));
  }

 public:
  UploadStickerFileRequest(ActorShared<Td> td, uint64 request_id, int32 user_id,
                           tl_object_ptr<td_api::InputFile> &&sticker)
      : RequestOnceActor(std::move(td), request_id), user_id_(user_id), sticker_(std::move(sticker)) {
  }
};

class CreateNewStickerSetRequest : public RequestOnceActor {
  UserId user_id_;
  string title_;
  string name_;
  bool is_masks_;
  vector<tl_object_ptr<td_api::InputSticker>> stickers_;

  void do_run(Promise<Unit> &&promise) override {
    td->stickers_manager_->create_new_sticker_set(user_id_, title_, name_, is_masks_, std::move(stickers_),
                                                  std::move(promise));
  }

  void do_send_result() override {
    auto set_id = td->stickers_manager_->search_sticker_set(name_, Auto());
    if (!set_id.is_valid()) {
      return send_error(Status::Error(500, "Created sticker set not found"));
    }
    send_result(td->stickers_manager_->get_sticker_set_object(set_id));
  }

 public:
  CreateNewStickerSetRequest(ActorShared<Td> td, uint64 request_id, int32 user_id, string &&title, string &&name,
                             bool is_masks, vector<tl_object_ptr<td_api::InputSticker>> &&stickers)
      : RequestOnceActor(std::move(td), request_id)
      , user_id_(user_id)
      , title_(std::move(title))
      , name_(std::move(name))
      , is_masks_(is_masks)
      , stickers_(std::move(stickers)) {
  }
};

class AddStickerToSetRequest : public RequestOnceActor {
  UserId user_id_;
  string name_;
  tl_object_ptr<td_api::InputSticker> sticker_;

  void do_run(Promise<Unit> &&promise) override {
    td->stickers_manager_->add_sticker_to_set(user_id_, name_, std::move(sticker_), std::move(promise));
  }

  void do_send_result() override {
    auto set_id = td->stickers_manager_->search_sticker_set(name_, Auto());
    if (!set_id.is_valid()) {
      return send_error(Status::Error(500, "Sticker set not found"));
    }
    send_result(td->stickers_manager_->get_sticker_set_object(set_id));
  }

 public:
  AddStickerToSetRequest(ActorShared<Td> td, uint64 request_id, int32 user_id, string &&name,
                         tl_object_ptr<td_api::InputSticker> &&sticker)
      : RequestOnceActor(std::move(td), request_id)
      , user_id_(user_id)
      , name_(std::move(name))
      , sticker_(std::move(sticker)) {
  }
};

class SetStickerSetThumbnailRequest : public RequestOnceActor {
  UserId user_id_;
  string name_;
  tl_object_ptr<td_api::InputFile> thumbnail_;

  void do_run(Promise<Unit> &&promise) override {
    td->stickers_manager_->set_sticker_set_thumbnail(user_id_, name_, std::move(thumbnail_), std::move(promise));
  }

  void do_send_result() override {
    auto set_id = td->stickers_manager_->search_sticker_set(name_, Auto());
    if (!set_id.is_valid()) {
      return send_error(Status::Error(500, "Sticker set not found"));
    }
    send_result(td->stickers_manager_->get_sticker_set_object(set_id));
  }

 public:
  SetStickerSetThumbnailRequest(ActorShared<Td> td, uint64 request_id, int32 user_id, string &&name,
                                tl_object_ptr<td_api::InputFile> &&thumbnail)
      : RequestOnceActor(std::move(td), request_id)
      , user_id_(user_id)
      , name_(std::move(name))
      , thumbnail_(std::move(thumbnail)) {
  }
};

class GetRecentStickersRequest : public RequestActor<> {
  bool is_attached_;

  vector<FileId> sticker_ids_;

  void do_run(Promise<Unit> &&promise) override {
    sticker_ids_ = td->stickers_manager_->get_recent_stickers(is_attached_, std::move(promise));
  }

  void do_send_result() override {
    send_result(td->stickers_manager_->get_stickers_object(sticker_ids_));
  }

 public:
  GetRecentStickersRequest(ActorShared<Td> td, uint64 request_id, bool is_attached)
      : RequestActor(std::move(td), request_id), is_attached_(is_attached) {
  }
};

class AddRecentStickerRequest : public RequestActor<> {
  bool is_attached_;
  tl_object_ptr<td_api::InputFile> input_file_;

  void do_run(Promise<Unit> &&promise) override {
    td->stickers_manager_->add_recent_sticker(is_attached_, input_file_, std::move(promise));
  }

 public:
  AddRecentStickerRequest(ActorShared<Td> td, uint64 request_id, bool is_attached,
                          tl_object_ptr<td_api::InputFile> &&input_file)
      : RequestActor(std::move(td), request_id), is_attached_(is_attached), input_file_(std::move(input_file)) {
    set_tries(3);
  }
};

class RemoveRecentStickerRequest : public RequestActor<> {
  bool is_attached_;
  tl_object_ptr<td_api::InputFile> input_file_;

  void do_run(Promise<Unit> &&promise) override {
    td->stickers_manager_->remove_recent_sticker(is_attached_, input_file_, std::move(promise));
  }

 public:
  RemoveRecentStickerRequest(ActorShared<Td> td, uint64 request_id, bool is_attached,
                             tl_object_ptr<td_api::InputFile> &&input_file)
      : RequestActor(std::move(td), request_id), is_attached_(is_attached), input_file_(std::move(input_file)) {
    set_tries(3);
  }
};

class ClearRecentStickersRequest : public RequestActor<> {
  bool is_attached_;

  void do_run(Promise<Unit> &&promise) override {
    td->stickers_manager_->clear_recent_stickers(is_attached_, std::move(promise));
  }

 public:
  ClearRecentStickersRequest(ActorShared<Td> td, uint64 request_id, bool is_attached)
      : RequestActor(std::move(td), request_id), is_attached_(is_attached) {
    set_tries(3);
  }
};

class GetFavoriteStickersRequest : public RequestActor<> {
  vector<FileId> sticker_ids_;

  void do_run(Promise<Unit> &&promise) override {
    sticker_ids_ = td->stickers_manager_->get_favorite_stickers(std::move(promise));
  }

  void do_send_result() override {
    send_result(td->stickers_manager_->get_stickers_object(sticker_ids_));
  }

 public:
  GetFavoriteStickersRequest(ActorShared<Td> td, uint64 request_id) : RequestActor(std::move(td), request_id) {
  }
};

class AddFavoriteStickerRequest : public RequestOnceActor {
  tl_object_ptr<td_api::InputFile> input_file_;

  void do_run(Promise<Unit> &&promise) override {
    td->stickers_manager_->add_favorite_sticker(input_file_, std::move(promise));
  }

 public:
  AddFavoriteStickerRequest(ActorShared<Td> td, uint64 request_id, tl_object_ptr<td_api::InputFile> &&input_file)
      : RequestOnceActor(std::move(td), request_id), input_file_(std::move(input_file)) {
    set_tries(3);
  }
};

class RemoveFavoriteStickerRequest : public RequestOnceActor {
  tl_object_ptr<td_api::InputFile> input_file_;

  void do_run(Promise<Unit> &&promise) override {
    td->stickers_manager_->remove_favorite_sticker(input_file_, std::move(promise));
  }

 public:
  RemoveFavoriteStickerRequest(ActorShared<Td> td, uint64 request_id, tl_object_ptr<td_api::InputFile> &&input_file)
      : RequestOnceActor(std::move(td), request_id), input_file_(std::move(input_file)) {
    set_tries(3);
  }
};

class GetStickerEmojisRequest : public RequestActor<> {
  tl_object_ptr<td_api::InputFile> input_file_;

  vector<string> emojis_;

  void do_run(Promise<Unit> &&promise) override {
    emojis_ = td->stickers_manager_->get_sticker_emojis(input_file_, std::move(promise));
  }

  void do_send_result() override {
    send_result(td_api::make_object<td_api::emojis>(std::move(emojis_)));
  }

 public:
  GetStickerEmojisRequest(ActorShared<Td> td, uint64 request_id, tl_object_ptr<td_api::InputFile> &&input_file)
      : RequestActor(std::move(td), request_id), input_file_(std::move(input_file)) {
    set_tries(3);
  }
};

class SearchEmojisRequest : public RequestActor<> {
  string text_;
  bool exact_match_;
  vector<string> input_language_codes_;

  vector<string> emojis_;

  void do_run(Promise<Unit> &&promise) override {
    emojis_ = td->stickers_manager_->search_emojis(text_, exact_match_, input_language_codes_, get_tries() < 2,
                                                   std::move(promise));
  }

  void do_send_result() override {
    send_result(td_api::make_object<td_api::emojis>(std::move(emojis_)));
  }

 public:
  SearchEmojisRequest(ActorShared<Td> td, uint64 request_id, string &&text, bool exact_match,
                      vector<string> &&input_language_codes)
      : RequestActor(std::move(td), request_id)
      , text_(std::move(text))
      , exact_match_(exact_match)
      , input_language_codes_(std::move(input_language_codes)) {
    set_tries(3);
  }
};

class GetEmojiSuggestionsUrlRequest : public RequestOnceActor {
  string language_code_;

  int64 random_id_;

  void do_run(Promise<Unit> &&promise) override {
    random_id_ = td->stickers_manager_->get_emoji_suggestions_url(language_code_, std::move(promise));
  }

  void do_send_result() override {
    send_result(td->stickers_manager_->get_emoji_suggestions_url_result(random_id_));
  }

 public:
  GetEmojiSuggestionsUrlRequest(ActorShared<Td> td, uint64 request_id, string &&language_code)
      : RequestOnceActor(std::move(td), request_id), language_code_(std::move(language_code)) {
  }
};

class GetSavedAnimationsRequest : public RequestActor<> {
  vector<FileId> animation_ids_;

  void do_run(Promise<Unit> &&promise) override {
    animation_ids_ = td->animations_manager_->get_saved_animations(std::move(promise));
  }

  void do_send_result() override {
    send_result(make_tl_object<td_api::animations>(transform(std::move(animation_ids_), [td = td](FileId animation_id) {
      return td->animations_manager_->get_animation_object(animation_id, "GetSavedAnimationsRequest");
    })));
  }

 public:
  GetSavedAnimationsRequest(ActorShared<Td> td, uint64 request_id) : RequestActor(std::move(td), request_id) {
  }
};

class AddSavedAnimationRequest : public RequestOnceActor {
  tl_object_ptr<td_api::InputFile> input_file_;

  void do_run(Promise<Unit> &&promise) override {
    td->animations_manager_->add_saved_animation(input_file_, std::move(promise));
  }

 public:
  AddSavedAnimationRequest(ActorShared<Td> td, uint64 request_id, tl_object_ptr<td_api::InputFile> &&input_file)
      : RequestOnceActor(std::move(td), request_id), input_file_(std::move(input_file)) {
    set_tries(3);
  }
};

class RemoveSavedAnimationRequest : public RequestOnceActor {
  tl_object_ptr<td_api::InputFile> input_file_;

  void do_run(Promise<Unit> &&promise) override {
    td->animations_manager_->remove_saved_animation(input_file_, std::move(promise));
  }

 public:
  RemoveSavedAnimationRequest(ActorShared<Td> td, uint64 request_id, tl_object_ptr<td_api::InputFile> &&input_file)
      : RequestOnceActor(std::move(td), request_id), input_file_(std::move(input_file)) {
    set_tries(3);
  }
};

class GetInlineQueryResultsRequest : public RequestOnceActor {
  UserId bot_user_id_;
  DialogId dialog_id_;
  Location user_location_;
  string query_;
  string offset_;

  uint64 query_hash_;

  void do_run(Promise<Unit> &&promise) override {
    query_hash_ = td->inline_queries_manager_->send_inline_query(bot_user_id_, dialog_id_, user_location_, query_,
                                                                 offset_, std::move(promise));
  }

  void do_send_result() override {
    send_result(td->inline_queries_manager_->get_inline_query_results_object(query_hash_));
  }

 public:
  GetInlineQueryResultsRequest(ActorShared<Td> td, uint64 request_id, int32 bot_user_id, int64 dialog_id,
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

class GetCallbackQueryAnswerRequest : public RequestOnceActor {
  FullMessageId full_message_id_;
  tl_object_ptr<td_api::CallbackQueryPayload> payload_;

  int64 result_id_;

  void do_run(Promise<Unit> &&promise) override {
    result_id_ =
        td->callback_queries_manager_->send_callback_query(full_message_id_, std::move(payload_), std::move(promise));
  }

  void do_send_result() override {
    send_result(td->callback_queries_manager_->get_callback_query_answer_object(result_id_));
  }

  void do_send_error(Status &&status) override {
    if (status.code() == 502 && td->messages_manager_->is_message_edited_recently(full_message_id_, 31)) {
      return send_result(make_tl_object<td_api::callbackQueryAnswer>());
    }
    send_error(std::move(status));
  }

 public:
  GetCallbackQueryAnswerRequest(ActorShared<Td> td, uint64 request_id, int64 dialog_id, int64 message_id,
                                tl_object_ptr<td_api::CallbackQueryPayload> payload)
      : RequestOnceActor(std::move(td), request_id)
      , full_message_id_(DialogId(dialog_id), MessageId(message_id))
      , payload_(std::move(payload))
      , result_id_(0) {
  }
};

class GetSupportUserRequest : public RequestActor<> {
  UserId user_id_;

  void do_run(Promise<Unit> &&promise) override {
    user_id_ = td->contacts_manager_->get_support_user(std::move(promise));
  }

  void do_send_result() override {
    send_result(td->contacts_manager_->get_user_object(user_id_));
  }

 public:
  GetSupportUserRequest(ActorShared<Td> td, uint64 request_id) : RequestActor(std::move(td), request_id) {
  }
};

class GetBackgroundsRequest : public RequestOnceActor {
  bool for_dark_theme_;

  void do_run(Promise<Unit> &&promise) override {
    td->background_manager_->get_backgrounds(std::move(promise));
  }

  void do_send_result() override {
    send_result(td->background_manager_->get_backgrounds_object(for_dark_theme_));
  }

 public:
  GetBackgroundsRequest(ActorShared<Td> td, uint64 request_id, bool for_dark_theme)
      : RequestOnceActor(std::move(td), request_id), for_dark_theme_(for_dark_theme) {
  }
};

class SearchBackgroundRequest : public RequestActor<> {
  string name_;

  BackgroundId background_id_;

  void do_run(Promise<Unit> &&promise) override {
    background_id_ = td->background_manager_->search_background(name_, std::move(promise));
  }

  void do_send_result() override {
    send_result(td->background_manager_->get_background_object(background_id_, false));
  }

 public:
  SearchBackgroundRequest(ActorShared<Td> td, uint64 request_id, string &&name)
      : RequestActor(std::move(td), request_id), name_(std::move(name)) {
    set_tries(3);
  }
};

class SetBackgroundRequest : public RequestActor<> {
  td_api::object_ptr<td_api::InputBackground> input_background_;
  td_api::object_ptr<td_api::BackgroundType> background_type_;
  bool for_dark_theme_ = false;

  BackgroundId background_id_;

  void do_run(Promise<Unit> &&promise) override {
    background_id_ = td->background_manager_->set_background(input_background_.get(), background_type_.get(),
                                                             for_dark_theme_, std::move(promise));
  }

  void do_send_result() override {
    send_result(td->background_manager_->get_background_object(background_id_, for_dark_theme_));
  }

 public:
  SetBackgroundRequest(ActorShared<Td> td, uint64 request_id,
                       td_api::object_ptr<td_api::InputBackground> &&input_background,
                       td_api::object_ptr<td_api::BackgroundType> background_type, bool for_dark_theme)
      : RequestActor(std::move(td), request_id)
      , input_background_(std::move(input_background))
      , background_type_(std::move(background_type))
      , for_dark_theme_(for_dark_theme) {
  }
};

Td::Td(unique_ptr<TdCallback> callback, Options options)
    : callback_(std::move(callback)), td_options_(std::move(options)) {
  CHECK(callback_ != nullptr);
}

Td::~Td() = default;

void Td::on_alarm_timeout_callback(void *td_ptr, int64 alarm_id) {
  auto td = static_cast<Td *>(td_ptr);
  auto td_id = td->actor_id(td);
  send_closure_later(td_id, &Td::on_alarm_timeout, alarm_id);
}

void Td::on_update_server_time_difference() {
  auto diff = G()->get_server_time_difference();
  if (std::abs(diff - last_sent_server_time_difference_) < 0.5) {
    return;
  }

  last_sent_server_time_difference_ = diff;
  send_update(td_api::make_object<td_api::updateOption>(
      "unix_time", td_api::make_object<td_api::optionValueInteger>(G()->unix_time())));
}

void Td::on_alarm_timeout(int64 alarm_id) {
  if (alarm_id == ONLINE_ALARM_ID) {
    on_online_updated(false, true);
    return;
  }
  if (alarm_id == PING_SERVER_ALARM_ID) {
    if (!close_flag_ && updates_manager_ != nullptr && auth_manager_->is_authorized()) {
      updates_manager_->ping_server();
      alarm_timeout_.set_timeout_in(PING_SERVER_ALARM_ID,
                                    PING_SERVER_TIMEOUT + Random::fast(0, PING_SERVER_TIMEOUT / 5));
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
    contacts_manager_->set_my_online_status(is_online_, send_update, true);
    if (!update_status_query_.empty()) {
      LOG(INFO) << "Cancel previous update status query";
      cancel_query(update_status_query_);
    }
    update_status_query_ = create_handler<UpdateStatusQuery>()->send(!is_online_);
  }
  if (is_online_) {
    alarm_timeout_.set_timeout_in(
        ONLINE_ALARM_ID,
        static_cast<double>(G()->shared_config().get_option_integer("online_update_period_ms", 210000)) * 1e-3);
  } else {
    alarm_timeout_.cancel_timeout(ONLINE_ALARM_ID);
  }
}

void Td::on_update_status_success(bool is_online) {
  if (is_online == is_online_) {
    if (!update_status_query_.empty()) {
      update_status_query_ = NetQueryRef();
    }
    contacts_manager_->set_my_online_status(is_online_, true, false);
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
    pending_terms_of_service_ = std::move(result.ok().second);
    auto update = get_update_terms_of_service_object();
    if (update == nullptr) {
      expires_in = min(max(result.ok().first, G()->unix_time() + 3600) - G()->unix_time(), 86400);
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
      bool is_proxy = (promo->flags_ & telegram_api::help_promoData::PROXY_MASK) != 0;
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
  schedule_get_promo_data(expires_at == 0 ? 0 : expires_at - G()->unix_time());
}

void Td::schedule_get_promo_data(int32 expires_in) {
  expires_in = expires_in <= 0 ? 0 : clamp(expires_in, 60, 86400);
  if (!close_flag_ && auth_manager_->is_authorized() && !auth_manager_->is_bot()) {
    LOG(INFO) << "Schedule getPromoData in " << expires_in;
    alarm_timeout_.set_timeout_in(PROMO_DATA_ALARM_ID, expires_in);
  }
}

void Td::on_channel_unban_timeout(int64 channel_id_long) {
  if (close_flag_ >= 2) {
    return;
  }
  contacts_manager_->on_channel_unban_timeout(ChannelId(narrow_cast<int32>(channel_id_long)));
}

bool Td::is_online() const {
  return is_online_;
}

void Td::set_is_bot_online(bool is_bot_online) {
  if (is_bot_online == is_bot_online_) {
    return;
  }

  is_bot_online_ = is_bot_online;
  send_closure(G()->state_manager(), &StateManager::on_online, is_bot_online_);
}

bool Td::is_authentication_request(int32 id) {
  switch (id) {
    case td_api::setTdlibParameters::ID:
    case td_api::checkDatabaseEncryptionKey::ID:
    case td_api::setDatabaseEncryptionKey::ID:
    case td_api::getAuthorizationState::ID:
    case td_api::setAuthenticationPhoneNumber::ID:
    case td_api::resendAuthenticationCode::ID:
    case td_api::checkAuthenticationCode::ID:
    case td_api::registerUser::ID:
    case td_api::requestQrCodeAuthentication::ID:
    case td_api::checkAuthenticationPassword::ID:
    case td_api::requestAuthenticationPasswordRecovery::ID:
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

bool Td::is_synchronous_request(int32 id) {
  switch (id) {
    case td_api::getTextEntities::ID:
    case td_api::parseTextEntities::ID:
    case td_api::parseMarkdown::ID:
    case td_api::getMarkdownText::ID:
    case td_api::getFileMimeType::ID:
    case td_api::getFileExtension::ID:
    case td_api::cleanFileName::ID:
    case td_api::getLanguagePackString::ID:
    case td_api::getChatFilterDefaultIconName::ID:
    case td_api::getJsonValue::ID:
    case td_api::getJsonString::ID:
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
    case State::Decrypt:
      return td_api::make_object<td_api::authorizationStateWaitEncryptionKey>(is_database_encrypted_);
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
    LOG(ERROR) << "Ignore request with id == 0: " << to_string(function);
    return;
  }

  request_set_.insert(id);
  if (function == nullptr) {
    return send_error_impl(id, make_error(400, "Request is empty"));
  }

  VLOG(td_requests) << "Receive request " << id << ": " << to_string(function);
  int32 function_id = function->get_id();
  if (is_synchronous_request(function_id)) {
    // send response synchronously
    return send_result(id, static_request(std::move(function)));
  }
  if (state_ != State::Run) {
    switch (function_id) {
      case td_api::getAuthorizationState::ID:
        // send response synchronously to prevent "Request aborted"
        return send_result(id, get_fake_authorization_state_object());
      case td_api::getCurrentState::ID: {
        vector<td_api::object_ptr<td_api::Update>> updates;
        updates.push_back(td_api::make_object<td_api::updateOption>(
            "version", td_api::make_object<td_api::optionValueString>(TDLIB_VERSION)));
        updates.push_back(td_api::make_object<td_api::updateAuthorizationState>(get_fake_authorization_state_object()));
        // send response synchronously to prevent "Request aborted"
        return send_result(id, td_api::make_object<td_api::updates>(std::move(updates)));
      }
      case td_api::close::ID:
        // need to send response synchronously before actual closing
        send_result(id, td_api::make_object<td_api::ok>());
        return close();
      default:
        break;
    }
  }
  switch (state_) {
    case State::WaitParameters: {
      switch (function_id) {
        case td_api::setTdlibParameters::ID:
          return answer_ok_query(
              id, set_parameters(std::move(move_tl_object_as<td_api::setTdlibParameters>(function)->parameters_)));
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
    case State::Decrypt: {
      string encryption_key;
      switch (function_id) {
        case td_api::checkDatabaseEncryptionKey::ID: {
          auto check_key = move_tl_object_as<td_api::checkDatabaseEncryptionKey>(function);
          encryption_key = std::move(check_key->encryption_key_);
          break;
        }
        case td_api::setDatabaseEncryptionKey::ID: {
          auto set_key = move_tl_object_as<td_api::setDatabaseEncryptionKey>(function);
          encryption_key = std::move(set_key->new_encryption_key_);
          break;
        }
        case td_api::destroy::ID:
          // need to send response synchronously before actual destroying
          send_result(id, td_api::make_object<td_api::ok>());
          return destroy();
        default:
          if (is_preinitialization_request(function_id)) {
            break;
          }
          if (is_preauthentication_request(function_id)) {
            pending_preauthentication_requests_.emplace_back(id, std::move(function));
            return;
          }
          return send_error_impl(
              id, make_error(400, "Database encryption key is needed: call checkDatabaseEncryptionKey first"));
      }
      return answer_ok_query(id, init(as_db_key(encryption_key)));
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
      case td_api::getFileMimeType::ID:
      case td_api::getFileExtension::ID:
      case td_api::cleanFileName::ID:
      case td_api::getChatFilterDefaultIconName::ID:
      case td_api::getJsonValue::ID:
      case td_api::getJsonString::ID:
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
  result_handlers_.emplace_back(id, handler);
}

std::shared_ptr<Td::ResultHandler> Td::extract_handler(uint64 id) {
  std::shared_ptr<Td::ResultHandler> result;
  for (size_t i = 0; i < result_handlers_.size(); i++) {
    if (result_handlers_[i].first == id) {
      result = std::move(result_handlers_[i].second);
      result_handlers_.erase(result_handlers_.begin() + i);
      break;
    }
  }
  return result;
}

void Td::invalidate_handler(ResultHandler *handler) {
  for (size_t i = 0; i < result_handlers_.size(); i++) {
    if (result_handlers_[i].second.get() == handler) {
      result_handlers_.erase(result_handlers_.begin() + i);
      i--;
    }
  }
}

void Td::send(NetQueryPtr &&query) {
  VLOG(net_query) << "Send " << query << " to dispatcher";
  query->debug("Td: send to NetQueryDispatcher");
  query->set_callback(actor_shared(this, 1));
  G()->net_query_dispatcher().dispatch(std::move(query));
}

void Td::on_result(NetQueryPtr query) {
  query->debug("Td: received from DcManager");
  VLOG(net_query) << "Receive result of " << query;
  if (close_flag_ > 1) {
    return;
  }

  if (query->id() == 0) {
    if (query->is_error()) {
      query->clear();
      updates_manager_->schedule_get_difference("error in update");
      LOG(ERROR) << "Error in update";
      return;
    }
    auto ok = query->move_as_ok();
    TlBufferParser parser(&ok);
    auto ptr = telegram_api::Updates::fetch(parser);
    parser.fetch_end();
    if (parser.get_error()) {
      LOG(ERROR) << "Failed to fetch update: " << parser.get_error() << format::as_hex_dump<4>(ok.as_slice());
      updates_manager_->schedule_get_difference("failed to fetch update");
    } else {
      updates_manager_->on_get_updates(std::move(ptr), Promise<Unit>());
      if (auth_manager_->is_bot() && auth_manager_->is_authorized()) {
        alarm_timeout_.set_timeout_in(PING_SERVER_ALARM_ID,
                                      PING_SERVER_TIMEOUT + Random::fast(0, PING_SERVER_TIMEOUT / 5));
        set_is_bot_online(true);
      }
    }
    return;
  }
  auto handler = extract_handler(query->id());
  if (handler == nullptr) {
    query->clear();
    LOG_IF(WARNING, !query->is_ok() || query->ok_tl_constructor() != telegram_api::upload_file::ID)
        << tag("NetQuery", query) << " is ignored: no handlers found";
    return;
  }
  handler->on_result(std::move(query));
}

bool Td::is_internal_config_option(Slice name) {
  switch (name[0]) {
    case 'a':
      return name == "animation_search_emojis" || name == "animation_search_provider" || name == "auth";
    case 'b':
      return name == "base_language_pack_version";
    case 'c':
      return name == "call_ring_timeout_ms" || name == "call_receive_timeout_ms" ||
             name == "channels_read_media_period";
    case 'd':
      return name == "dc_txt_domain_name" || name == "dice_emojis" || name == "dice_success_values";
    case 'e':
      return name == "edit_time_limit";
    case 'i':
      return name == "ignored_restriction_reasons";
    case 'l':
      return name == "language_pack_version";
    case 'm':
      return name == "my_phone_number";
    case 'n':
      return name == "notification_cloud_delay_ms" || name == "notification_default_delay_ms";
    case 'o':
      return name == "online_update_period_ms" || name == "online_cloud_timeout_ms";
    case 'r':
      return name == "revoke_pm_inbox" || name == "revoke_time_limit" || name == "revoke_pm_time_limit" ||
             name == "rating_e_decay" || name == "recent_stickers_limit";
    case 's':
      return name == "saved_animations_limit";
    case 'w':
      return name == "webfile_dc_id";
    default:
      return false;
  }
}

void Td::on_config_option_updated(const string &name) {
  if (close_flag_) {
    return;
  }
  if (name == "auth") {
    return on_authorization_lost();
  } else if (name == "saved_animations_limit") {
    return animations_manager_->on_update_saved_animations_limit(
        narrow_cast<int32>(G()->shared_config().get_option_integer(name)));
  } else if (name == "animation_search_emojis") {
    return animations_manager_->on_update_animation_search_emojis(G()->shared_config().get_option_string(name));
  } else if (name == "animation_search_provider") {
    return animations_manager_->on_update_animation_search_provider(G()->shared_config().get_option_string(name));
  } else if (name == "recent_stickers_limit") {
    return stickers_manager_->on_update_recent_stickers_limit(
        narrow_cast<int32>(G()->shared_config().get_option_integer(name)));
  } else if (name == "favorite_stickers_limit") {
    stickers_manager_->on_update_favorite_stickers_limit(
        narrow_cast<int32>(G()->shared_config().get_option_integer(name)));
  } else if (name == "my_id") {
    G()->set_my_id(static_cast<int32>(G()->shared_config().get_option_integer(name)));
  } else if (name == "session_count") {
    G()->net_query_dispatcher().update_session_count();
  } else if (name == "use_pfs") {
    G()->net_query_dispatcher().update_use_pfs();
  } else if (name == "use_storage_optimizer") {
    send_closure(storage_manager_, &StorageManager::update_use_storage_optimizer);
  } else if (name == "rating_e_decay") {
    return send_closure(top_dialog_manager_, &TopDialogManager::update_rating_e_decay);
  } else if (name == "disable_contact_registered_notifications") {
    send_closure(notification_manager_actor_,
                 &NotificationManager::on_disable_contact_registered_notifications_changed);
  } else if (name == "disable_top_chats") {
    send_closure(top_dialog_manager_, &TopDialogManager::update_is_enabled,
                 !G()->shared_config().get_option_boolean(name));
  } else if (name == "connection_parameters") {
    if (G()->mtproto_header().set_parameters(G()->shared_config().get_option_string(name))) {
      G()->net_query_dispatcher().update_mtproto_header();
    }
  } else if (name == "is_emulator") {
    if (G()->mtproto_header().set_is_emulator(G()->shared_config().get_option_boolean(name))) {
      G()->net_query_dispatcher().update_mtproto_header();
    }
  } else if (name == "localization_target") {
    send_closure(language_pack_manager_, &LanguagePackManager::on_language_pack_changed);
    if (G()->mtproto_header().set_language_pack(G()->shared_config().get_option_string(name))) {
      G()->net_query_dispatcher().update_mtproto_header();
    }
  } else if (name == "language_pack_id") {
    send_closure(language_pack_manager_, &LanguagePackManager::on_language_code_changed);
    if (G()->mtproto_header().set_language_code(G()->shared_config().get_option_string(name))) {
      G()->net_query_dispatcher().update_mtproto_header();
    }
  } else if (name == "language_pack_version") {
    return send_closure(language_pack_manager_, &LanguagePackManager::on_language_pack_version_changed, false, -1);
  } else if (name == "base_language_pack_version") {
    return send_closure(language_pack_manager_, &LanguagePackManager::on_language_pack_version_changed, true, -1);
  } else if (name == "notification_group_count_max") {
    send_closure(notification_manager_actor_, &NotificationManager::on_notification_group_count_max_changed, true);
  } else if (name == "notification_group_size_max") {
    send_closure(notification_manager_actor_, &NotificationManager::on_notification_group_size_max_changed);
  } else if (name == "online_cloud_timeout_ms") {
    return send_closure(notification_manager_actor_, &NotificationManager::on_online_cloud_timeout_changed);
  } else if (name == "notification_cloud_delay_ms") {
    return send_closure(notification_manager_actor_, &NotificationManager::on_notification_cloud_delay_changed);
  } else if (name == "notification_default_delay_ms") {
    return send_closure(notification_manager_actor_, &NotificationManager::on_notification_default_delay_changed);
  } else if (name == "ignored_restriction_reasons") {
    return send_closure(contacts_manager_actor_, &ContactsManager::on_ignored_restriction_reasons_changed);
  } else if (name == "dice_emojis") {
    return send_closure(stickers_manager_actor_, &StickersManager::on_update_dice_emojis);
  } else if (name == "dice_success_values") {
    return send_closure(stickers_manager_actor_, &StickersManager::on_update_dice_success_values);
  } else if (is_internal_config_option(name)) {
    return;
  }

  // send_closure was already used in the callback
  send_update(make_tl_object<td_api::updateOption>(name, G()->shared_config().get_option_value(name)));
}

tl_object_ptr<td_api::ConnectionState> Td::get_connection_state_object(StateManager::State state) {
  switch (state) {
    case StateManager::State::Empty:
      UNREACHABLE();
      return nullptr;
    case StateManager::State::WaitingForNetwork:
      return make_tl_object<td_api::connectionStateWaitingForNetwork>();
    case StateManager::State::ConnectingToProxy:
      return make_tl_object<td_api::connectionStateConnectingToProxy>();
    case StateManager::State::Connecting:
      return make_tl_object<td_api::connectionStateConnecting>();
    case StateManager::State::Updating:
      return make_tl_object<td_api::connectionStateUpdating>();
    case StateManager::State::Ready:
      return make_tl_object<td_api::connectionStateReady>();
    default:
      UNREACHABLE();
      return nullptr;
  }
}

void Td::on_connection_state_changed(StateManager::State new_state) {
  if (new_state == connection_state_) {
    LOG(ERROR) << "State manager sends update about unchanged state " << static_cast<int32>(new_state);
    return;
  }
  connection_state_ = new_state;

  send_closure(actor_id(this), &Td::send_update,
               make_tl_object<td_api::updateConnectionState>(get_connection_state_object(connection_state_)));
}

void Td::on_authorization_lost() {
  LOG(WARNING) << "Lost authorization";
  send_closure(auth_manager_actor_, &AuthManager::on_authorization_lost);
}

void Td::start_up() {
  always_wait_for_mailbox();

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
  send_update(td_api::make_object<td_api::updateOption>("version",
                                                        td_api::make_object<td_api::optionValueString>(TDLIB_VERSION)));
  send_update(td_api::make_object<td_api::updateAuthorizationState>(
      td_api::make_object<td_api::authorizationStateWaitTdlibParameters>()));
}

void Td::tear_down() {
  LOG_CHECK(close_flag_ == 5) << close_flag_;
}

void Td::hangup_shared() {
  auto token = get_link_token();
  auto type = Container<int>::type_from_id(token);

  if (type == RequestActorIdType) {
    request_actors_.erase(get_link_token());
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
      LOG(WARNING) << "ON_ACTORS_CLOSED";
      Timer timer;
      animations_manager_.reset();
      LOG(DEBUG) << "AnimationsManager was cleared" << timer;
      audios_manager_.reset();
      LOG(DEBUG) << "AudiosManager was cleared" << timer;
      auth_manager_.reset();
      LOG(DEBUG) << "AuthManager was cleared" << timer;
      background_manager_.reset();
      LOG(DEBUG) << "BackgroundManager was cleared" << timer;
      callback_queries_manager_.reset();
      LOG(DEBUG) << "CallbackQueriesManager was cleared" << timer;
      contacts_manager_.reset();
      LOG(DEBUG) << "ContactsManager was cleared" << timer;
      country_info_manager_.reset();
      LOG(DEBUG) << "CountryInfoManager was cleared" << timer;
      documents_manager_.reset();
      LOG(DEBUG) << "DocumentsManager was cleared" << timer;
      file_manager_.reset();
      LOG(DEBUG) << "FileManager was cleared" << timer;
      file_reference_manager_.reset();
      LOG(DEBUG) << "FileReferenceManager was cleared" << timer;
      group_call_manager_.reset();
      LOG(DEBUG) << "GroupCallManager was cleared" << timer;
      inline_queries_manager_.reset();
      LOG(DEBUG) << "InlineQueriesManager was cleared" << timer;
      messages_manager_.reset();
      LOG(DEBUG) << "MessagesManager was cleared" << timer;
      notification_manager_.reset();
      LOG(DEBUG) << "NotificationManager was cleared" << timer;
      poll_manager_.reset();
      LOG(DEBUG) << "PollManager was cleared" << timer;
      stickers_manager_.reset();
      LOG(DEBUG) << "StickersManager was cleared" << timer;
      updates_manager_.reset();
      LOG(DEBUG) << "UpdatesManager was cleared" << timer;
      video_notes_manager_.reset();
      LOG(DEBUG) << "VideoNotesManager was cleared" << timer;
      videos_manager_.reset();
      LOG(DEBUG) << "VideosManager was cleared" << timer;
      voice_notes_manager_.reset();
      LOG(DEBUG) << "VoiceNotesManager was cleared" << timer;
      web_pages_manager_.reset();
      LOG(DEBUG) << "WebPagesManager was cleared" << timer;
      Promise<> promise = PromiseCreator::lambda([actor_id = create_reference()](Unit) mutable { actor_id.reset(); });

      G()->set_shared_config(nullptr);
      if (destroy_flag_) {
        G()->close_and_destroy_all(std::move(promise));
      } else {
        G()->close_all(std::move(promise));
      }
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
  LOG(WARNING) << "ON_CLOSED";
  close_flag_ = 5;
  send_update(
      td_api::make_object<td_api::updateAuthorizationState>(td_api::make_object<td_api::authorizationStateClosed>()));
  dec_stop_cnt();
}

void Td::dec_stop_cnt() {
  stop_cnt_--;
  if (stop_cnt_ == 0) {
    LOG(WARNING) << "Stop Td";
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
    LOG(WARNING) << "Have no request actors";
    clear();
    dec_actor_refcnt();  // remove guard
  }
}

void Td::clear_handlers() {
  result_handlers_.clear();
}

void Td::clear_requests() {
  while (!pending_alarms_.empty()) {
    auto it = pending_alarms_.begin();
    auto alarm_id = it->first;
    pending_alarms_.erase(it);
    alarm_timeout_.cancel_timeout(alarm_id);
  }
  while (!request_set_.empty()) {
    uint64 id = *request_set_.begin();
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
  if (destroy_flag_) {
    for (auto &option : G()->shared_config().get_options()) {
      if (!is_internal_config_option(option.first)) {
        send_update(make_tl_object<td_api::updateOption>(option.first, make_tl_object<td_api::optionValueEmpty>()));
      }
    }
    if (!auth_manager_->is_bot()) {
      notification_manager_->destroy_all_notifications();
    }
  } else {
    if (!auth_manager_->is_bot()) {
      notification_manager_->flush_all_notifications();
    }
  }
  LOG(DEBUG) << "Options was cleared" << timer;

  G()->net_query_creator().stop_check();
  clear_handlers();
  LOG(DEBUG) << "Handlers was cleared" << timer;
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
  LOG(DEBUG) << "Requests was answered" << timer;

  // close all pure actors
  call_manager_.reset();
  LOG(DEBUG) << "CallManager was cleared" << timer;
  change_phone_number_manager_.reset();
  LOG(DEBUG) << "ChangePhoneNumberManager was cleared" << timer;
  config_manager_.reset();
  LOG(DEBUG) << "ConfigManager was cleared" << timer;
  confirm_phone_number_manager_.reset();
  LOG(DEBUG) << "ConfirmPhoneNumberManager was cleared" << timer;
  device_token_manager_.reset();
  LOG(DEBUG) << "DeviceTokenManager was cleared" << timer;
  hashtag_hints_.reset();
  LOG(DEBUG) << "HashtagHints was cleared" << timer;
  language_pack_manager_.reset();
  LOG(DEBUG) << "LanguagePackManager was cleared" << timer;
  net_stats_manager_.reset();
  LOG(DEBUG) << "NetStatsManager was cleared" << timer;
  password_manager_.reset();
  LOG(DEBUG) << "PasswordManager was cleared" << timer;
  privacy_manager_.reset();
  LOG(DEBUG) << "PrivacyManager was cleared" << timer;
  secure_manager_.reset();
  LOG(DEBUG) << "SecureManager was cleared" << timer;
  secret_chats_manager_.reset();
  LOG(DEBUG) << "SecretChatsManager was cleared" << timer;
  storage_manager_.reset();
  LOG(DEBUG) << "StorageManager was cleared" << timer;
  top_dialog_manager_.reset();
  LOG(DEBUG) << "TopDialogManager was cleared" << timer;
  verify_phone_number_manager_.reset();
  LOG(DEBUG) << "VerifyPhoneNumberManager was cleared" << timer;

  G()->set_connection_creator(ActorOwn<ConnectionCreator>());
  LOG(DEBUG) << "ConnectionCreator was cleared" << timer;
  G()->set_temp_auth_key_watchdog(ActorOwn<TempAuthKeyWatchdog>());
  LOG(DEBUG) << "TempAuthKeyWatchdog was cleared" << timer;

  // clear actors which are unique pointers
  animations_manager_actor_.reset();
  LOG(DEBUG) << "AnimationsManager actor was cleared" << timer;
  auth_manager_actor_.reset();
  LOG(DEBUG) << "AuthManager actor was cleared" << timer;
  background_manager_actor_.reset();
  LOG(DEBUG) << "BackgroundManager actor was cleared" << timer;
  contacts_manager_actor_.reset();
  LOG(DEBUG) << "ContactsManager actor was cleared" << timer;
  country_info_manager_actor_.reset();
  LOG(DEBUG) << "CountryInfoManager actor was cleared" << timer;
  file_manager_actor_.reset();
  LOG(DEBUG) << "FileManager actor was cleared" << timer;
  file_reference_manager_actor_.reset();
  LOG(DEBUG) << "FileReferenceManager actor was cleared" << timer;
  group_call_manager_actor_.reset();
  LOG(DEBUG) << "GroupCallManager actor was cleared" << timer;
  inline_queries_manager_actor_.reset();
  LOG(DEBUG) << "InlineQueriesManager actor was cleared" << timer;
  messages_manager_actor_.reset();  // TODO: Stop silent
  LOG(DEBUG) << "MessagesManager actor was cleared" << timer;
  notification_manager_actor_.reset();
  LOG(DEBUG) << "NotificationManager actor was cleared" << timer;
  poll_manager_actor_.reset();
  LOG(DEBUG) << "PollManager actor was cleared" << timer;
  stickers_manager_actor_.reset();
  LOG(DEBUG) << "StickersManager actor was cleared" << timer;
  updates_manager_actor_.reset();
  LOG(DEBUG) << "UpdatesManager actor was cleared" << timer;
  web_pages_manager_actor_.reset();
  LOG(DEBUG) << "WebPagesManager actor was cleared" << timer;
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
  if (state_ == State::WaitParameters || state_ == State::Decrypt) {
    clear_requests();
    if (destroy_flag && state_ == State::Decrypt) {
      TdDb::destroy(parameters_).ignore();
    }
    state_ = State::Close;
    close_flag_ = 4;
    G()->set_close_flag();

    request_actors_.clear();
    return send_closure_later(actor_id(this), &Td::dec_request_actor_refcnt);  // remove guard
  }

  state_ = State::Close;
  close_flag_ = 1;
  G()->set_close_flag();
  send_closure(auth_manager_actor_, &AuthManager::on_closing, destroy_flag);

  // wait till all request_actors will stop.
  request_actors_.clear();
  G()->td_db()->flush_all();
  send_closure_later(actor_id(this), &Td::dec_request_actor_refcnt);  // remove guard
}

class Td::DownloadFileCallback : public FileManager::DownloadCallback {
 public:
  void on_progress(FileId file_id) override {
  }

  void on_download_ok(FileId file_id) override {
    send_closure(G()->td(), &Td::on_file_download_finished, file_id);
  }

  void on_download_error(FileId file_id, Status error) override {
    send_closure(G()->td(), &Td::on_file_download_finished, file_id);
  }
};

class Td::UploadFileCallback : public FileManager::UploadCallback {
 public:
  void on_progress(FileId file_id) override {
  }

  void on_upload_ok(FileId file_id, tl_object_ptr<telegram_api::InputFile> input_file) override {
    // cancel file upload of the file to allow next upload with the same file to succeed
    send_closure(G()->file_manager(), &FileManager::cancel_upload, file_id);
  }

  void on_upload_encrypted_ok(FileId file_id, tl_object_ptr<telegram_api::InputEncryptedFile> input_file) override {
    // cancel file upload of the file to allow next upload with the same file to succeed
    send_closure(G()->file_manager(), &FileManager::cancel_upload, file_id);
  }

  void on_upload_secure_ok(FileId file_id, tl_object_ptr<telegram_api::InputSecureFile> input_file) override {
    // cancel file upload of the file to allow next upload with the same file to succeed
    send_closure(G()->file_manager(), &FileManager::cancel_upload, file_id);
  }

  void on_upload_error(FileId file_id, Status error) override {
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

Status Td::init(DbKey key) {
  auto current_scheduler_id = Scheduler::instance()->sched_id();
  auto scheduler_count = Scheduler::instance()->sched_count();

  VLOG(td_init) << "Begin to init database";
  TdDb::Events events;
  auto r_td_db = TdDb::open(min(current_scheduler_id + 1, scheduler_count - 1), parameters_, std::move(key), events);
  if (r_td_db.is_error()) {
    return Status::Error(400, r_td_db.error().message());
  }
  LOG(INFO) << "Successfully inited database in " << tag("database_directory", parameters_.database_directory)
            << " and " << tag("files_directory", parameters_.files_directory);
  VLOG(td_init) << "Successfully inited database";

  G()->init(parameters_, actor_id(this), r_td_db.move_as_ok()).ensure();
  last_sent_server_time_difference_ = G()->get_server_time_difference();
  send_update(td_api::make_object<td_api::updateOption>(
      "unix_time", td_api::make_object<td_api::optionValueInteger>(G()->unix_time())));

  init_options_and_network();

  complete_pending_preauthentication_requests([](int32 id) {
    switch (id) {
      case td_api::getOption::ID:
      case td_api::setOption::ID:
        return true;
      default:
        return false;
    }
  });

  options_.language_pack = G()->shared_config().get_option_string("localization_target");
  options_.language_code = G()->shared_config().get_option_string("language_pack_id");
  options_.parameters = G()->shared_config().get_option_string("connection_parameters");
  options_.is_emulator = G()->shared_config().get_option_boolean("is_emulator");
  // options_.proxy = Proxy();
  G()->set_mtproto_header(make_unique<MtprotoHeader>(options_));
  G()->set_store_all_files_in_files_directory(
      G()->shared_config().get_option_boolean("store_all_files_in_files_directory"));

  VLOG(td_init) << "Create NetQueryDispatcher";
  auto net_query_dispatcher = make_unique<NetQueryDispatcher>([&] { return create_reference(); });
  G()->set_net_query_dispatcher(std::move(net_query_dispatcher));

  complete_pending_preauthentication_requests([](int32 id) {
    // pingProxy uses NetQueryDispatcher to get main_dc_id, so must be called after NetQueryDispatcher is created
    return id == td_api::pingProxy::ID;
  });

  VLOG(td_init) << "Create AuthManager";
  auth_manager_ = td::make_unique<AuthManager>(parameters_.api_id, parameters_.api_hash, create_reference());
  auth_manager_actor_ = register_actor("AuthManager", auth_manager_.get());

  init_file_manager();

  init_managers();

  G()->set_my_id(static_cast<int32>(G()->shared_config().get_option_integer("my_id")));

  storage_manager_ = create_actor<StorageManager>("StorageManager", create_reference(),
                                                  min(current_scheduler_id + 2, scheduler_count - 1));
  G()->set_storage_manager(storage_manager_.get());

  VLOG(td_init) << "Send binlog events";
  for (auto &event : events.user_events) {
    contacts_manager_->on_binlog_user_event(std::move(event));
  }

  for (auto &event : events.channel_events) {
    contacts_manager_->on_binlog_channel_event(std::move(event));
  }

  // chats may contain links to channels, so should be inited after
  for (auto &event : events.chat_events) {
    contacts_manager_->on_binlog_chat_event(std::move(event));
  }

  for (auto &event : events.secret_chat_events) {
    contacts_manager_->on_binlog_secret_chat_event(std::move(event));
  }

  for (auto &event : events.web_page_events) {
    web_pages_manager_->on_binlog_web_page_event(std::move(event));
  }

  if (is_online_) {
    on_online_updated(true, true);
  }
  if (auth_manager_->is_bot()) {
    set_is_bot_online(true);
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
  // orded to handle them properly.
  //
  // -- Use send_closure_later, so actors don't even start process binlog events, before all binlog events are sent

  for (auto &event : events.to_secret_chats_manager) {
    send_closure_later(secret_chats_manager_, &SecretChatsManager::replay_binlog_event, std::move(event));
  }

  send_closure_later(poll_manager_actor_, &PollManager::on_binlog_events, std::move(events.to_poll_manager));

  send_closure_later(messages_manager_actor_, &MessagesManager::on_binlog_events,
                     std::move(events.to_messages_manager));

  send_closure_later(notification_manager_actor_, &NotificationManager::on_binlog_events,
                     std::move(events.to_notification_manager));

  send_closure(secret_chats_manager_, &SecretChatsManager::binlog_replay_finish);

  VLOG(td_init) << "Ping datacenter";
  if (!auth_manager_->is_authorized()) {
    country_info_manager_->get_current_country_code(Promise<string>());
  } else {
    updates_manager_->get_difference("init");
    schedule_get_terms_of_service(0);
    schedule_get_promo_data(0);
  }

  complete_pending_preauthentication_requests([](int32 id) { return true; });

  VLOG(td_init) << "Finish initialization";

  state_ = State::Run;
  return Status::OK();
}

void Td::init_options_and_network() {
  VLOG(td_init) << "Create StateManager";
  class StateManagerCallback : public StateManager::Callback {
   public:
    explicit StateManagerCallback(ActorShared<Td> td) : td_(std::move(td)) {
    }
    bool on_state(StateManager::State state) override {
      send_closure(td_, &Td::on_connection_state_changed, state);
      return td_.is_alive();
    }

   private:
    ActorShared<Td> td_;
  };
  state_manager_ = create_actor<StateManager>("State manager", create_reference());
  send_closure(state_manager_, &StateManager::add_callback, make_unique<StateManagerCallback>(create_reference()));
  G()->set_state_manager(state_manager_.get());
  connection_state_ = StateManager::State::Empty;

  VLOG(td_init) << "Create ConfigShared";
  G()->set_shared_config(td::make_unique<ConfigShared>(G()->td_db()->get_config_pmc_shared()));

  if (G()->shared_config().have_option("language_database_path")) {
    G()->shared_config().set_option_string("language_pack_database_path",
                                           G()->shared_config().get_option_string("language_database_path"));
    G()->shared_config().set_option_empty("language_database_path");
  }
  if (G()->shared_config().have_option("language_pack")) {
    G()->shared_config().set_option_string("localization_target",
                                           G()->shared_config().get_option_string("language_pack"));
    G()->shared_config().set_option_empty("language_pack");
  }
  if (G()->shared_config().have_option("language_code")) {
    G()->shared_config().set_option_string("language_pack_id", G()->shared_config().get_option_string("language_code"));
    G()->shared_config().set_option_empty("language_code");
  }
  if (!G()->shared_config().have_option("message_text_length_max")) {
    G()->shared_config().set_option_integer("message_text_length_max", 4096);
  }
  if (!G()->shared_config().have_option("message_caption_length_max")) {
    G()->shared_config().set_option_integer("message_caption_length_max", 1024);
  }

  init_connection_creator();

  VLOG(td_init) << "Create TempAuthKeyWatchdog";
  auto temp_auth_key_watchdog = create_actor<TempAuthKeyWatchdog>("TempAuthKeyWatchdog", create_reference());
  G()->set_temp_auth_key_watchdog(std::move(temp_auth_key_watchdog));

  VLOG(td_init) << "Create ConfigManager";
  config_manager_ = create_actor<ConfigManager>("ConfigManager", create_reference());
  G()->set_config_manager(config_manager_.get());

  VLOG(td_init) << "Set ConfigShared callback";
  class ConfigSharedCallback : public ConfigShared::Callback {
   public:
    void on_option_updated(const string &name, const string &value) const override {
      send_closure(G()->td(), &Td::on_config_option_updated, name);
    }
    ~ConfigSharedCallback() override {
      LOG(INFO) << "Destroy ConfigSharedCallback";
    }
  };
  // we need to set ConfigShared callback before td_api::getOption requests are processed for consistency
  // TODO currently they will be inconsistent anyway, because td_api::getOption returns current value,
  // but in td_api::updateOption there will be a newer value, obtained at the time of update creation
  // so, there can be even two succesive updateOption with the same value
  // we need to process td_api::getOption along with td_api::setOption for consistency
  // we need to process td_api::setOption before managers and MTProto header are created,
  // because their initialiation may be affected by the options
  G()->shared_config().set_callback(make_unique<ConfigSharedCallback>());
}

void Td::init_connection_creator() {
  VLOG(td_init) << "Create ConnectionCreator";
  auto connection_creator = create_actor<ConnectionCreator>("ConnectionCreator", create_reference());
  auto net_stats_manager = create_actor<NetStatsManager>("NetStatsManager", create_reference());

  // How else could I let two actor know about each other, without quite complex async logic?
  auto net_stats_manager_ptr = net_stats_manager->get_actor_unsafe();
  net_stats_manager_ptr->init();
  connection_creator->get_actor_unsafe()->set_net_stats_callback(net_stats_manager_ptr->get_common_stats_callback(),
                                                                 net_stats_manager_ptr->get_media_stats_callback());
  G()->set_net_stats_file_callbacks(net_stats_manager_ptr->get_file_stats_callbacks());

  G()->set_connection_creator(std::move(connection_creator));
  net_stats_manager_ = std::move(net_stats_manager);

  complete_pending_preauthentication_requests([](int32 id) {
    switch (id) {
      case td_api::setNetworkType::ID:
      case td_api::getNetworkStatistics::ID:
      case td_api::addNetworkStatistics::ID:
      case td_api::resetNetworkStatistics::ID:
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
}

void Td::init_file_manager() {
  VLOG(td_init) << "Create FileManager";
  download_file_callback_ = std::make_shared<DownloadFileCallback>();
  upload_file_callback_ = std::make_shared<UploadFileCallback>();

  class FileManagerContext : public FileManager::Context {
   public:
    explicit FileManagerContext(Td *td) : td_(td) {
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
      send_closure(G()->file_reference_manager(), &FileReferenceManager::reload_photo, source, std::move(promise));
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

  file_reference_manager_ = make_unique<FileReferenceManager>();
  file_reference_manager_actor_ = register_actor("FileReferenceManager", file_reference_manager_.get());
  G()->set_file_reference_manager(file_reference_manager_actor_.get());
}

void Td::init_managers() {
  VLOG(td_init) << "Create Managers";
  audios_manager_ = make_unique<AudiosManager>(this);
  callback_queries_manager_ = make_unique<CallbackQueriesManager>(this);
  documents_manager_ = make_unique<DocumentsManager>(this);
  video_notes_manager_ = make_unique<VideoNotesManager>(this);
  videos_manager_ = make_unique<VideosManager>(this);
  voice_notes_manager_ = make_unique<VoiceNotesManager>(this);

  animations_manager_ = make_unique<AnimationsManager>(this, create_reference());
  animations_manager_actor_ = register_actor("AnimationsManager", animations_manager_.get());
  G()->set_animations_manager(animations_manager_actor_.get());
  background_manager_ = make_unique<BackgroundManager>(this, create_reference());
  background_manager_actor_ = register_actor("BackgroundManager", background_manager_.get());
  G()->set_background_manager(background_manager_actor_.get());
  contacts_manager_ = make_unique<ContactsManager>(this, create_reference());
  contacts_manager_actor_ = register_actor("ContactsManager", contacts_manager_.get());
  G()->set_contacts_manager(contacts_manager_actor_.get());
  country_info_manager_ = make_unique<CountryInfoManager>(this, create_reference());
  country_info_manager_actor_ = register_actor("CountryInfoManager", country_info_manager_.get());
  group_call_manager_ = make_unique<GroupCallManager>(this, create_reference());
  group_call_manager_actor_ = register_actor("GroupCallManager", group_call_manager_.get());
  G()->set_group_call_manager(group_call_manager_actor_.get());
  inline_queries_manager_ = make_unique<InlineQueriesManager>(this, create_reference());
  inline_queries_manager_actor_ = register_actor("InlineQueriesManager", inline_queries_manager_.get());
  messages_manager_ = make_unique<MessagesManager>(this, create_reference());
  messages_manager_actor_ = register_actor("MessagesManager", messages_manager_.get());
  G()->set_messages_manager(messages_manager_actor_.get());
  notification_manager_ = make_unique<NotificationManager>(this, create_reference());
  notification_manager_actor_ = register_actor("NotificationManager", notification_manager_.get());
  poll_manager_ = make_unique<PollManager>(this, create_reference());
  poll_manager_actor_ = register_actor("PollManager", poll_manager_.get());
  G()->set_notification_manager(notification_manager_actor_.get());
  stickers_manager_ = make_unique<StickersManager>(this, create_reference());
  stickers_manager_actor_ = register_actor("StickersManager", stickers_manager_.get());
  G()->set_stickers_manager(stickers_manager_actor_.get());
  updates_manager_ = make_unique<UpdatesManager>(this, create_reference());
  updates_manager_actor_ = register_actor("UpdatesManager", updates_manager_.get());
  G()->set_updates_manager(updates_manager_actor_.get());
  web_pages_manager_ = make_unique<WebPagesManager>(this, create_reference());
  web_pages_manager_actor_ = register_actor("WebPagesManager", web_pages_manager_.get());
  G()->set_web_pages_manager(web_pages_manager_actor_.get());

  call_manager_ = create_actor<CallManager>("CallManager", create_reference());
  G()->set_call_manager(call_manager_.get());
  change_phone_number_manager_ = create_actor<PhoneNumberManager>(
      "ChangePhoneNumberManager", PhoneNumberManager::Type::ChangePhone, create_reference());
  confirm_phone_number_manager_ = create_actor<PhoneNumberManager>(
      "ConfirmPhoneNumberManager", PhoneNumberManager::Type::ConfirmPhone, create_reference());
  device_token_manager_ = create_actor<DeviceTokenManager>("DeviceTokenManager", create_reference());
  hashtag_hints_ = create_actor<HashtagHints>("HashtagHints", "text", create_reference());
  language_pack_manager_ = create_actor<LanguagePackManager>("LanguagePackManager", create_reference());
  G()->set_language_pack_manager(language_pack_manager_.get());
  password_manager_ = create_actor<PasswordManager>("PasswordManager", create_reference());
  G()->set_password_manager(password_manager_.get());
  privacy_manager_ = create_actor<PrivacyManager>("PrivacyManager", create_reference());
  secret_chats_manager_ = create_actor<SecretChatsManager>("SecretChatsManager", create_reference());
  G()->set_secret_chats_manager(secret_chats_manager_.get());
  secure_manager_ = create_actor<SecureManager>("SecureManager", create_reference());
  top_dialog_manager_ = create_actor<TopDialogManager>("TopDialogManager", create_reference());
  G()->set_top_dialog_manager(top_dialog_manager_.get());
  verify_phone_number_manager_ = create_actor<PhoneNumberManager>(
      "VerifyPhoneNumberManager", PhoneNumberManager::Type::VerifyPhone, create_reference());
}

void Td::send_update(tl_object_ptr<td_api::Update> &&object) {
  CHECK(object != nullptr);
  auto object_id = object->get_id();
  if (close_flag_ >= 5 && object_id != td_api::updateAuthorizationState::ID) {
    // just in case
    return;
  }

  switch (object_id) {
    case td_api::updateFavoriteStickers::ID:
    case td_api::updateInstalledStickerSets::ID:
    case td_api::updateRecentStickers::ID:
    case td_api::updateSavedAnimations::ID:
    case td_api::updateUserStatus::ID:
      VLOG(td_requests) << "Sending update: " << oneline(to_string(object));
      break;
    case td_api::updateTrendingStickerSets::ID: {
      auto sticker_sets = static_cast<const td_api::updateTrendingStickerSets *>(object.get())->sticker_sets_.get();
      VLOG(td_requests) << "Sending update: updateTrendingStickerSets { total_count = " << sticker_sets->total_count_
                        << ", count = " << sticker_sets->sets_.size() << " }";
      break;
    }
    case td_api::updateOption::ID / 2:
    case td_api::updateChatReadInbox::ID / 2:
    case td_api::updateUnreadMessageCount::ID / 2:
    case td_api::updateUnreadChatCount::ID / 2:
    case td_api::updateChatOnlineMemberCount::ID / 2:
    case td_api::updateUserChatAction::ID / 2:
    case td_api::updateChatFilters::ID / 2:
    case td_api::updateChatPosition::ID / 2:
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
    request_set_.erase(it);
    VLOG(td_requests) << "Sending result for request " << id << ": " << to_string(object);
    if (object == nullptr) {
      object = make_tl_object<td_api::error>(404, "Not Found");
    }
    callback_->on_result(id, std::move(object));
  }
}

void Td::send_error_impl(uint64 id, tl_object_ptr<td_api::error> error) {
  CHECK(id != 0);
  CHECK(error != nullptr);
  auto it = request_set_.find(id);
  if (it != request_set_.end()) {
    request_set_.erase(it);
    VLOG(td_requests) << "Sending error for request " << id << ": " << oneline(to_string(error));
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
  return PromiseCreator::lambda([id = id, actor_id = actor_id(this)](Result<Unit> result) {
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
#define CHECK_IS_USER()                                                     \
  if (auth_manager_->is_bot()) {                                            \
    return send_error_raw(id, 400, "The method is not available for bots"); \
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

Status Td::fix_parameters(TdParameters &parameters) {
  if (parameters.database_directory.empty()) {
    VLOG(td_init) << "Fix database_directory";
    parameters.database_directory = ".";
  }
  if (parameters.files_directory.empty()) {
    VLOG(td_init) << "Fix files_directory";
    parameters.files_directory = parameters.database_directory;
  }
  if (parameters.use_message_db && !parameters.use_chat_info_db) {
    VLOG(td_init) << "Fix use_chat_info_db";
    parameters.use_chat_info_db = true;
  }
  if (parameters.use_chat_info_db && !parameters.use_file_db) {
    VLOG(td_init) << "Fix use_file_db";
    parameters.use_file_db = true;
  }
  if (parameters.api_id <= 0) {
    VLOG(td_init) << "Invalid api_id";
    return Status::Error(400, "Valid api_id must be provided. Can be obtained at https://my.telegram.org");
  }
  if (parameters.api_hash.empty()) {
    VLOG(td_init) << "Invalid api_hash";
    return Status::Error(400, "Valid api_hash must be provided. Can be obtained at https://my.telegram.org");
  }

  auto prepare_dir = [](string dir) -> Result<string> {
    CHECK(!dir.empty());
    if (dir.back() != TD_DIR_SLASH) {
      dir += TD_DIR_SLASH;
    }
    TRY_STATUS(mkpath(dir, 0750));
    TRY_RESULT(real_dir, realpath(dir, true));
    if (dir.back() != TD_DIR_SLASH) {
      dir += TD_DIR_SLASH;
    }
    return real_dir;
  };

  auto r_database_directory = prepare_dir(parameters.database_directory);
  if (r_database_directory.is_error()) {
    VLOG(td_init) << "Invalid database_directory";
    return Status::Error(400, PSLICE() << "Can't init database in the directory \"" << parameters.database_directory
                                       << "\": " << r_database_directory.error());
  }
  parameters.database_directory = r_database_directory.move_as_ok();
  auto r_files_directory = prepare_dir(parameters.files_directory);
  if (r_files_directory.is_error()) {
    VLOG(td_init) << "Invalid files_directory";
    return Status::Error(400, PSLICE() << "Can't init files directory \"" << parameters.files_directory
                                       << "\": " << r_files_directory.error());
  }
  parameters.files_directory = r_files_directory.move_as_ok();

  return Status::OK();
}

Status Td::set_parameters(td_api::object_ptr<td_api::tdlibParameters> parameters) {
  VLOG(td_init) << "Begin to set TDLib parameters";
  if (parameters == nullptr) {
    VLOG(td_init) << "Empty parameters";
    return Status::Error(400, "Parameters aren't specified");
  }

  if (!clean_input_string(parameters->api_hash_) && !clean_input_string(parameters->system_language_code_) &&
      !clean_input_string(parameters->device_model_) && !clean_input_string(parameters->system_version_) &&
      !clean_input_string(parameters->application_version_)) {
    VLOG(td_init) << "Wrong string encoding";
    return Status::Error(400, "Strings must be encoded in UTF-8");
  }

  parameters_.use_test_dc = parameters->use_test_dc_;
  parameters_.database_directory = parameters->database_directory_;
  parameters_.files_directory = parameters->files_directory_;
  parameters_.api_id = parameters->api_id_;
  parameters_.api_hash = parameters->api_hash_;
  parameters_.use_file_db = parameters->use_file_database_;
  parameters_.enable_storage_optimizer = parameters->enable_storage_optimizer_;
  parameters_.ignore_file_names = parameters->ignore_file_names_;
  parameters_.use_secret_chats = parameters->use_secret_chats_;
  parameters_.use_chat_info_db = parameters->use_chat_info_database_;
  parameters_.use_message_db = parameters->use_message_database_;

  VLOG(td_init) << "Fix parameters...";
  TRY_STATUS(fix_parameters(parameters_));
  VLOG(td_init) << "Check binlog encryption...";
  TRY_RESULT(encryption_info, TdDb::check_encryption(parameters_));
  is_database_encrypted_ = encryption_info.is_encrypted;

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
    options_.application_version += TDLIB_VERSION;
  }
  options_.language_pack = "";
  options_.language_code = "";
  options_.parameters = "";
  options_.is_emulator = false;
  options_.proxy = Proxy();

  state_ = State::Decrypt;
  VLOG(td_init) << "Send authorizationStateWaitEncryptionKey";
  send_closure(actor_id(this), &Td::send_update,
               td_api::make_object<td_api::updateAuthorizationState>(
                   td_api::make_object<td_api::authorizationStateWaitEncryptionKey>(is_database_encrypted_)));
  VLOG(td_init) << "Finish set parameters";
  return Status::OK();
}

void Td::on_request(uint64 id, const td_api::setTdlibParameters &request) {
  send_error_raw(id, 400, "Unexpected setTdlibParameters");
}

void Td::on_request(uint64 id, const td_api::checkDatabaseEncryptionKey &request) {
  send_error_raw(id, 400, "Unexpected checkDatabaseEncryptionKey");
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

void Td::on_request(uint64 id, const td_api::resendAuthenticationCode &request) {
  send_closure(auth_manager_actor_, &AuthManager::resend_authentication_code, id);
}

void Td::on_request(uint64 id, td_api::checkAuthenticationCode &request) {
  CLEAN_INPUT_STRING(request.code_);
  send_closure(auth_manager_actor_, &AuthManager::check_code, id, std::move(request.code_));
}

void Td::on_request(uint64 id, td_api::registerUser &request) {
  CLEAN_INPUT_STRING(request.first_name_);
  CLEAN_INPUT_STRING(request.last_name_);
  send_closure(auth_manager_actor_, &AuthManager::register_user, id, std::move(request.first_name_),
               std::move(request.last_name_));
}

void Td::on_request(uint64 id, td_api::requestQrCodeAuthentication &request) {
  send_closure(auth_manager_actor_, &AuthManager::request_qr_code_authentication, id,
               std::move(request.other_user_ids_));
}

void Td::on_request(uint64 id, td_api::checkAuthenticationPassword &request) {
  CLEAN_INPUT_STRING(request.password_);
  send_closure(auth_manager_actor_, &AuthManager::check_password, id, std::move(request.password_));
}

void Td::on_request(uint64 id, const td_api::requestAuthenticationPasswordRecovery &request) {
  send_closure(auth_manager_actor_, &AuthManager::request_password_recovery, id);
}

void Td::on_request(uint64 id, td_api::recoverAuthenticationPassword &request) {
  CLEAN_INPUT_STRING(request.recovery_code_);
  send_closure(auth_manager_actor_, &AuthManager::recover_password, id, std::move(request.recovery_code_));
}

void Td::on_request(uint64 id, const td_api::logOut &request) {
  // will call Td::destroy later
  send_closure(auth_manager_actor_, &AuthManager::log_out, id);
}

void Td::on_request(uint64 id, const td_api::close &request) {
  // send response before actually closing
  send_closure(actor_id(this), &Td::send_result, id, td_api::make_object<td_api::ok>());
  close();
}

void Td::on_request(uint64 id, const td_api::destroy &request) {
  // send response before actually destroying
  send_closure(actor_id(this), &Td::send_result, id, td_api::make_object<td_api::ok>());
  destroy();
}

void Td::on_request(uint64 id, td_api::checkAuthenticationBotToken &request) {
  CLEAN_INPUT_STRING(request.token_);
  send_closure(auth_manager_actor_, &AuthManager::check_bot_token, id, std::move(request.token_));
}

void Td::on_request(uint64 id, td_api::confirmQrCodeAuthentication &request) {
  CLEAN_INPUT_STRING(request.link_);
  CREATE_REQUEST_PROMISE();
  contacts_manager_->confirm_qr_code_authentication(std::move(request.link_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getCurrentState &request) {
  vector<td_api::object_ptr<td_api::Update>> updates;

  updates.push_back(
      td_api::make_object<td_api::updateOption>("online", make_tl_object<td_api::optionValueBoolean>(is_online_)));
  updates.push_back(td_api::make_object<td_api::updateOption>(
      "unix_time", make_tl_object<td_api::optionValueInteger>(G()->unix_time())));
  updates.push_back(td_api::make_object<td_api::updateOption>(
      "version", td_api::make_object<td_api::optionValueString>(TDLIB_VERSION)));
  for (auto &option : G()->shared_config().get_options()) {
    if (!is_internal_config_option(option.first)) {
      updates.push_back(td_api::make_object<td_api::updateOption>(
          option.first, ConfigShared::get_option_value_object(option.second)));
    }
  }

  auto state = auth_manager_->get_current_authorization_state_object();
  if (state != nullptr) {
    updates.push_back(td_api::make_object<td_api::updateAuthorizationState>(std::move(state)));
  }

  updates.push_back(td_api::make_object<td_api::updateConnectionState>(get_connection_state_object(connection_state_)));

  if (auth_manager_->is_authorized()) {
    contacts_manager_->get_current_state(updates);

    background_manager_->get_current_state(updates);

    animations_manager_->get_current_state(updates);

    stickers_manager_->get_current_state(updates);

    messages_manager_->get_current_state(updates);

    notification_manager_->get_current_state(updates);

    config_manager_->get_actor_unsafe()->get_current_state(updates);

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
  send_closure(password_manager_, &PasswordManager::check_recovery_email_address_code, request.code_,
               std::move(promise));
}

void Td::on_request(uint64 id, const td_api::resendRecoveryEmailAddressCode &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  send_closure(password_manager_, &PasswordManager::resend_recovery_email_address_code, std::move(promise));
}

void Td::on_request(uint64 id, td_api::requestPasswordRecovery &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  send_closure(password_manager_, &PasswordManager::request_password_recovery, std::move(promise));
}

void Td::on_request(uint64 id, td_api::recoverPassword &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.recovery_code_);
  CREATE_REQUEST_PROMISE();
  send_closure(password_manager_, &PasswordManager::recover_password, std::move(request.recovery_code_),
               std::move(promise));
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
  if (request.device_token_ == nullptr) {
    return send_error_raw(id, 400, "Device token must be non-empty");
  }
  CREATE_REQUEST_PROMISE();
  send_closure(device_token_manager_, &DeviceTokenManager::register_device, std::move(request.device_token_),
               std::move(request.other_user_ids_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::getUserPrivacySettingRules &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  send_closure(privacy_manager_, &PrivacyManager::get_privacy, std::move(request.setting_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::setUserPrivacySettingRules &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  send_closure(privacy_manager_, &PrivacyManager::set_privacy, std::move(request.setting_), std::move(request.rules_),
               std::move(promise));
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
  contacts_manager_->get_account_ttl(std::move(query_promise));
}

void Td::on_request(uint64 id, const td_api::setAccountTtl &request) {
  CHECK_IS_USER();
  if (request.ttl_ == nullptr) {
    return send_error_raw(id, 400, "New account TTL must be non-empty");
  }
  CREATE_OK_REQUEST_PROMISE();
  contacts_manager_->set_account_ttl(request.ttl_->days_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::deleteAccount &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.reason_);
  send_closure(auth_manager_actor_, &AuthManager::delete_account, id, request.reason_);
}

void Td::on_request(uint64 id, td_api::changePhoneNumber &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.phone_number_);
  send_closure(change_phone_number_manager_, &PhoneNumberManager::set_phone_number, id,
               std::move(request.phone_number_), std::move(request.settings_));
}

void Td::on_request(uint64 id, td_api::checkChangePhoneNumberCode &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.code_);
  send_closure(change_phone_number_manager_, &PhoneNumberManager::check_code, id, std::move(request.code_));
}

void Td::on_request(uint64 id, td_api::resendChangePhoneNumberCode &request) {
  CHECK_IS_USER();
  send_closure(change_phone_number_manager_, &PhoneNumberManager::resend_authentication_code, id);
}

void Td::on_request(uint64 id, const td_api::getActiveSessions &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  contacts_manager_->get_active_sessions(std::move(promise));
}

void Td::on_request(uint64 id, const td_api::terminateSession &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  contacts_manager_->terminate_session(request.session_id_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::terminateAllOtherSessions &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  contacts_manager_->terminate_all_other_sessions(std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getConnectedWebsites &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  contacts_manager_->get_connected_websites(std::move(promise));
}

void Td::on_request(uint64 id, const td_api::disconnectWebsite &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  contacts_manager_->disconnect_website(request.website_id_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::disconnectAllWebsites &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  contacts_manager_->disconnect_all_websites(std::move(promise));
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
  FullMessageId full_message_id(DialogId(request.chat_id_), MessageId(request.message_id_));
  send_closure(actor_id(this), &Td::send_result, id, messages_manager_->get_message_object(full_message_id));
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

void Td::on_request(uint64 id, const td_api::getMessageThread &request) {
  CHECK_IS_USER();
  CREATE_REQUEST(GetMessageThreadRequest, request.chat_id_, request.message_id_);
}

void Td::on_request(uint64 id, const td_api::getMessageLink &request) {
  auto r_message_link = messages_manager_->get_message_link(
      {DialogId(request.chat_id_), MessageId(request.message_id_)}, request.for_album_, request.for_comment_);
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

void Td::on_request(uint64 id, const td_api::getFile &request) {
  send_closure(actor_id(this), &Td::send_result, id, file_manager_->get_file_object(FileId(request.file_id_, 0)));
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
  std::vector<DialogId> owner_dialog_ids;
  for (auto chat_id : request.chat_ids_) {
    DialogId dialog_id(chat_id);
    if (!dialog_id.is_valid() && dialog_id != DialogId()) {
      return send_error_raw(id, 400, "Wrong chat identifier");
    }
    owner_dialog_ids.push_back(dialog_id);
  }
  std::vector<DialogId> exclude_owner_dialog_ids;
  for (auto chat_id : request.exclude_chat_ids_) {
    DialogId dialog_id(chat_id);
    if (!dialog_id.is_valid() && dialog_id != DialogId()) {
      return send_error_raw(id, 400, "Wrong chat identifier");
    }
    exclude_owner_dialog_ids.push_back(dialog_id);
  }
  FileGcParameters parameters(request.size_, request.ttl_, request.count_, request.immunity_delay_,
                              std::move(file_types), std::move(owner_dialog_ids), std::move(exclude_owner_dialog_ids),
                              request.chat_limit_);

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
  if (!request.only_current_ && G()->shared_config().get_option_boolean("disable_persistent_network_statistics")) {
    return send_error_raw(id, 400, "Persistent network statistics is disabled");
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
  CREATE_OK_REQUEST_PROMISE();
  send_closure(net_stats_manager_, &NetStatsManager::reset_network_stats);
  promise.set_value(Unit());
}

void Td::on_request(uint64 id, td_api::addNetworkStatistics &request) {
  if (request.entry_ == nullptr) {
    return send_error_raw(id, 400, "Network statistics entry must be non-empty");
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
  if (entry.rx > (1ll << 40) || entry.rx < 0) {
    return send_error_raw(id, 400, "Wrong received bytes value");
  }
  if (entry.tx > (1ll << 40) || entry.tx < 0) {
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

void Td::on_request(uint64 id, td_api::getTopChats &request) {
  CHECK_IS_USER();
  if (request.category_ == nullptr) {
    return send_error_raw(id, 400, "Top chat category must be non-empty");
  }
  if (request.limit_ <= 0) {
    return send_error_raw(id, 400, "Limit must be positive");
  }
  CREATE_REQUEST_PROMISE();
  auto query_promise = PromiseCreator::lambda([promise = std::move(promise)](Result<vector<DialogId>> result) mutable {
    if (result.is_error()) {
      promise.set_error(result.move_as_error());
    } else {
      promise.set_value(MessagesManager::get_chats_object(-1, result.ok()));
    }
  });
  send_closure(top_dialog_manager_, &TopDialogManager::get_top_dialogs, get_top_dialog_category(*request.category_),
               narrow_cast<size_t>(request.limit_), std::move(query_promise));
}

void Td::on_request(uint64 id, const td_api::removeTopChat &request) {
  CHECK_IS_USER();
  if (request.category_ == nullptr) {
    return send_error_raw(id, 400, "Top chat category must be non-empty");
  }

  DialogId dialog_id(request.chat_id_);
  if (!dialog_id.is_valid()) {
    return send_error_raw(id, 400, "Invalid chat identifier");
  }
  send_closure(top_dialog_manager_, &TopDialogManager::remove_dialog, get_top_dialog_category(*request.category_),
               dialog_id, messages_manager_->get_input_peer(dialog_id, AccessRights::Read));
  send_closure(actor_id(this), &Td::send_result, id, td_api::make_object<td_api::ok>());
}

void Td::on_request(uint64 id, const td_api::getChats &request) {
  CHECK_IS_USER();
  CREATE_REQUEST(GetChatsRequest, DialogListId(request.chat_list_), request.offset_order_, request.offset_chat_id_,
                 request.limit_);
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
  contacts_manager_->search_dialogs_nearby(Location(request.location_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getGroupsInCommon &request) {
  CHECK_IS_USER();
  CREATE_REQUEST(GetGroupsInCommonRequest, request.user_id_, request.offset_chat_id_, request.limit_);
}

void Td::on_request(uint64 id, td_api::checkChatUsername &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.username_);
  CREATE_REQUEST_PROMISE();
  auto query_promise =
      PromiseCreator::lambda([promise = std::move(promise)](Result<CheckDialogUsernameResult> result) mutable {
        if (result.is_error()) {
          promise.set_error(result.move_as_error());
        } else {
          promise.set_value(ContactsManager::get_check_chat_username_result_object(result.ok()));
        }
      });
  contacts_manager_->check_dialog_username(DialogId(request.chat_id_), request.username_, std::move(query_promise));
}

void Td::on_request(uint64 id, const td_api::getCreatedPublicChats &request) {
  CHECK_IS_USER();
  CREATE_REQUEST(GetCreatedPublicChatsRequest, get_public_dialog_type(request.type_));
}

void Td::on_request(uint64 id, const td_api::checkCreatedPublicChatsLimit &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  contacts_manager_->check_created_public_dialogs_limit(get_public_dialog_type(request.type_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getSuitableDiscussionChats &request) {
  CHECK_IS_USER();
  CREATE_NO_ARGS_REQUEST(GetSuitableDiscussionChatsRequest);
}

void Td::on_request(uint64 id, const td_api::getInactiveSupergroupChats &request) {
  CHECK_IS_USER();
  CREATE_NO_ARGS_REQUEST(GetInactiveSupergroupChatsRequest);
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
  answer_ok_query(id, messages_manager_->view_messages(
                          DialogId(request.chat_id_), MessageId(request.message_thread_id_),
                          MessagesManager::get_message_ids(request.message_ids_), request.force_read_));
}

void Td::on_request(uint64 id, const td_api::openMessageContent &request) {
  CHECK_IS_USER();
  answer_ok_query(
      id, messages_manager_->open_message_content({DialogId(request.chat_id_), MessageId(request.message_id_)}));
}

void Td::on_request(uint64 id, td_api::getExternalLink &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.link_);
  CREATE_REQUEST_PROMISE();
  auto query_promise = [promise = std::move(promise)](Result<string> &&result) mutable {
    if (result.is_error()) {
      promise.set_error(result.move_as_error());
    } else {
      promise.set_value(td_api::make_object<td_api::httpUrl>(result.ok()));
    }
  };
  send_closure_later(G()->config_manager(), &ConfigManager::get_external_link, std::move(request.link_),
                     std::move(query_promise));
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
  contacts_manager_->delete_dialog(dialog_id, std::move(query_promise));
}

void Td::on_request(uint64 id, const td_api::getMessageThreadHistory &request) {
  CHECK_IS_USER();
  CREATE_REQUEST(GetMessageThreadHistoryRequest, request.chat_id_, request.message_id_, request.from_message_id_,
                 request.offset_, request.limit_);
}

void Td::on_request(uint64 id, td_api::searchChatMessages &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.query_);
  CREATE_REQUEST(SearchChatMessagesRequest, request.chat_id_, std::move(request.query_), std::move(request.sender_),
                 request.from_message_id_, request.offset_, request.limit_, std::move(request.filter_),
                 request.message_thread_id_);
}

void Td::on_request(uint64 id, td_api::searchSecretMessages &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.query_);
  CLEAN_INPUT_STRING(request.offset_);
  CREATE_REQUEST(SearchSecretMessagesRequest, request.chat_id_, std::move(request.query_), std::move(request.offset_),
                 request.limit_, std::move(request.filter_));
}

void Td::on_request(uint64 id, td_api::searchMessages &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.query_);
  DialogListId dialog_list_id(request.chat_list_);
  if (!dialog_list_id.is_folder()) {
    return send_error_raw(id, 400, "Wrong chat list specified");
  }
  CREATE_REQUEST(SearchMessagesRequest, dialog_list_id.get_folder_id(), request.chat_list_ == nullptr,
                 std::move(request.query_), request.offset_date_, request.offset_chat_id_, request.offset_message_id_,
                 request.limit_, std::move(request.filter_), request.min_date_, request.max_date_);
}

void Td::on_request(uint64 id, td_api::searchCallMessages &request) {
  CHECK_IS_USER();
  CREATE_REQUEST(SearchCallMessagesRequest, request.from_message_id_, request.limit_, request.only_missed_);
}

void Td::on_request(uint64 id, const td_api::deleteAllCallMessages &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->delete_all_call_messages(request.revoke_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::searchChatRecentLocationMessages &request) {
  CHECK_IS_USER();
  CREATE_REQUEST(SearchChatRecentLocationMessagesRequest, request.chat_id_, request.limit_);
}

void Td::on_request(uint64 id, const td_api::getActiveLiveLocationMessages &request) {
  CHECK_IS_USER();
  CREATE_NO_ARGS_REQUEST(GetActiveLiveLocationMessagesRequest);
}

void Td::on_request(uint64 id, const td_api::getChatMessageByDate &request) {
  CREATE_REQUEST(GetChatMessageByDateRequest, request.chat_id_, request.date_);
}

void Td::on_request(uint64 id, td_api::getChatMessageCount &request) {
  CHECK_IS_USER();
  CREATE_REQUEST(GetChatMessageCountRequest, request.chat_id_, std::move(request.filter_), request.return_local_);
}

void Td::on_request(uint64 id, const td_api::getChatScheduledMessages &request) {
  CHECK_IS_USER();
  CREATE_REQUEST(GetChatScheduledMessagesRequest, request.chat_id_);
}

void Td::on_request(uint64 id, td_api::getMessagePublicForwards &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.offset_);
  CREATE_REQUEST(GetMessagePublicForwardsRequest, request.chat_id_, request.message_id_, request.offset_,
                 request.limit_);
}

void Td::on_request(uint64 id, const td_api::removeNotification &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  notification_manager_->remove_notification(NotificationGroupId(request.notification_group_id_),
                                             NotificationId(request.notification_id_), false, true, std::move(promise),
                                             "td_api::removeNotification");
}

void Td::on_request(uint64 id, const td_api::removeNotificationGroup &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  notification_manager_->remove_notification_group(NotificationGroupId(request.notification_group_id_),
                                                   NotificationId(request.max_notification_id_), MessageId(), -1, true,
                                                   std::move(promise));
}

void Td::on_request(uint64 id, const td_api::deleteMessages &request) {
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->delete_messages(DialogId(request.chat_id_), MessagesManager::get_message_ids(request.message_ids_),
                                     request.revoke_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::deleteChatMessagesFromUser &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->delete_dialog_messages_from_user(DialogId(request.chat_id_), UserId(request.user_id_),
                                                      std::move(promise));
}

void Td::on_request(uint64 id, const td_api::readAllChatMentions &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->read_all_dialog_mentions(DialogId(request.chat_id_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::sendMessage &request) {
  DialogId dialog_id(request.chat_id_);
  auto r_new_message_id = messages_manager_->send_message(
      dialog_id, MessageId(request.message_thread_id_), MessageId(request.reply_to_message_id_),
      std::move(request.options_), std::move(request.reply_markup_), std::move(request.input_message_content_));
  if (r_new_message_id.is_error()) {
    return send_closure(actor_id(this), &Td::send_error, id, r_new_message_id.move_as_error());
  }

  CHECK(r_new_message_id.ok().is_valid() || r_new_message_id.ok().is_valid_scheduled());
  send_closure(actor_id(this), &Td::send_result, id,
               messages_manager_->get_message_object({dialog_id, r_new_message_id.ok()}));
}

void Td::on_request(uint64 id, td_api::sendMessageAlbum &request) {
  DialogId dialog_id(request.chat_id_);
  auto r_message_ids = messages_manager_->send_message_group(
      dialog_id, MessageId(request.message_thread_id_), MessageId(request.reply_to_message_id_),
      std::move(request.options_), std::move(request.input_message_contents_));
  if (r_message_ids.is_error()) {
    return send_closure(actor_id(this), &Td::send_error, id, r_message_ids.move_as_error());
  }

  send_closure(actor_id(this), &Td::send_result, id,
               messages_manager_->get_messages_object(-1, dialog_id, r_message_ids.ok(), false));
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
               messages_manager_->get_message_object({dialog_id, r_new_message_id.ok()}));
}

void Td::on_request(uint64 id, td_api::sendInlineQueryResultMessage &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.result_id_);

  DialogId dialog_id(request.chat_id_);
  auto r_new_message_id = messages_manager_->send_inline_query_result_message(
      dialog_id, MessageId(request.message_thread_id_), MessageId(request.reply_to_message_id_),
      std::move(request.options_), request.query_id_, request.result_id_, request.hide_via_bot_);
  if (r_new_message_id.is_error()) {
    return send_closure(actor_id(this), &Td::send_error, id, r_new_message_id.move_as_error());
  }

  CHECK(r_new_message_id.ok().is_valid() || r_new_message_id.ok().is_valid_scheduled());
  send_closure(actor_id(this), &Td::send_result, id,
               messages_manager_->get_message_object({dialog_id, r_new_message_id.ok()}));
}

void Td::on_request(uint64 id, td_api::addLocalMessage &request) {
  CHECK_IS_USER();

  DialogId dialog_id(request.chat_id_);
  auto r_new_message_id = messages_manager_->add_local_message(
      dialog_id, std::move(request.sender_), MessageId(request.reply_to_message_id_), request.disable_notification_,
      std::move(request.input_message_content_));
  if (r_new_message_id.is_error()) {
    return send_closure(actor_id(this), &Td::send_error, id, r_new_message_id.move_as_error());
  }

  CHECK(r_new_message_id.ok().is_valid());
  send_closure(actor_id(this), &Td::send_result, id,
               messages_manager_->get_message_object({dialog_id, r_new_message_id.ok()}));
}

void Td::on_request(uint64 id, td_api::editMessageText &request) {
  CREATE_REQUEST(EditMessageTextRequest, request.chat_id_, request.message_id_, std::move(request.reply_markup_),
                 std::move(request.input_message_content_));
}

void Td::on_request(uint64 id, td_api::editMessageLiveLocation &request) {
  CREATE_REQUEST(EditMessageLiveLocationRequest, request.chat_id_, request.message_id_,
                 std::move(request.reply_markup_), std::move(request.location_), request.heading_,
                 request.proximity_alert_radius_);
}

void Td::on_request(uint64 id, td_api::editMessageMedia &request) {
  CREATE_REQUEST(EditMessageMediaRequest, request.chat_id_, request.message_id_, std::move(request.reply_markup_),
                 std::move(request.input_message_content_));
}

void Td::on_request(uint64 id, td_api::editMessageCaption &request) {
  CREATE_REQUEST(EditMessageCaptionRequest, request.chat_id_, request.message_id_, std::move(request.reply_markup_),
                 std::move(request.caption_));
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
  messages_manager_->edit_inline_message_text(std::move(request.inline_message_id_), std::move(request.reply_markup_),
                                              std::move(request.input_message_content_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::editInlineMessageLiveLocation &request) {
  CHECK_IS_BOT();
  CLEAN_INPUT_STRING(request.inline_message_id_);
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->edit_inline_message_live_location(
      std::move(request.inline_message_id_), std::move(request.reply_markup_), std::move(request.location_),
      request.heading_, request.proximity_alert_radius_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::editInlineMessageMedia &request) {
  CHECK_IS_BOT();
  CLEAN_INPUT_STRING(request.inline_message_id_);
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->edit_inline_message_media(std::move(request.inline_message_id_), std::move(request.reply_markup_),
                                               std::move(request.input_message_content_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::editInlineMessageCaption &request) {
  CHECK_IS_BOT();
  CLEAN_INPUT_STRING(request.inline_message_id_);
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->edit_inline_message_caption(std::move(request.inline_message_id_),
                                                 std::move(request.reply_markup_), std::move(request.caption_),
                                                 std::move(promise));
}

void Td::on_request(uint64 id, td_api::editInlineMessageReplyMarkup &request) {
  CHECK_IS_BOT();
  CLEAN_INPUT_STRING(request.inline_message_id_);
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->edit_inline_message_reply_markup(std::move(request.inline_message_id_),
                                                      std::move(request.reply_markup_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::editMessageSchedulingState &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->edit_message_scheduling_state({DialogId(request.chat_id_), MessageId(request.message_id_)},
                                                   std::move(request.scheduling_state_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::setGameScore &request) {
  CHECK_IS_BOT();
  CREATE_REQUEST(SetGameScoreRequest, request.chat_id_, request.message_id_, request.edit_message_, request.user_id_,
                 request.score_, request.force_);
}

void Td::on_request(uint64 id, td_api::setInlineGameScore &request) {
  CHECK_IS_BOT();
  CLEAN_INPUT_STRING(request.inline_message_id_);
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->set_inline_game_score(std::move(request.inline_message_id_), request.edit_message_,
                                           UserId(request.user_id_), request.score_, request.force_,
                                           std::move(promise));
}

void Td::on_request(uint64 id, td_api::getGameHighScores &request) {
  CHECK_IS_BOT();
  CREATE_REQUEST(GetGameHighScoresRequest, request.chat_id_, request.message_id_, request.user_id_);
}

void Td::on_request(uint64 id, td_api::getInlineGameHighScores &request) {
  CHECK_IS_BOT();
  CLEAN_INPUT_STRING(request.inline_message_id_);
  CREATE_REQUEST(GetInlineGameHighScoresRequest, std::move(request.inline_message_id_), request.user_id_);
}

void Td::on_request(uint64 id, const td_api::deleteChatReplyMarkup &request) {
  CHECK_IS_USER();
  answer_ok_query(
      id, messages_manager_->delete_dialog_reply_markup(DialogId(request.chat_id_), MessageId(request.message_id_)));
}

void Td::on_request(uint64 id, td_api::sendChatAction &request) {
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->send_dialog_action(DialogId(request.chat_id_), MessageId(request.message_thread_id_),
                                        DialogAction(std::move(request.action_)), std::move(promise));
}

void Td::on_request(uint64 id, td_api::sendChatScreenshotTakenNotification &request) {
  CHECK_IS_USER();
  answer_ok_query(id, messages_manager_->send_screenshot_taken_notification_message(DialogId(request.chat_id_)));
}

void Td::on_request(uint64 id, td_api::forwardMessages &request) {
  DialogId dialog_id(request.chat_id_);
  auto input_message_ids = MessagesManager::get_message_ids(request.message_ids_);
  auto message_copy_options =
      transform(input_message_ids, [send_copy = request.send_copy_, remove_caption = request.remove_caption_](
                                       MessageId) { return MessageCopyOptions(send_copy, remove_caption); });
  auto r_message_ids =
      messages_manager_->forward_messages(dialog_id, DialogId(request.from_chat_id_), std::move(input_message_ids),
                                          std::move(request.options_), false, std::move(message_copy_options));
  if (r_message_ids.is_error()) {
    return send_closure(actor_id(this), &Td::send_error, id, r_message_ids.move_as_error());
  }

  send_closure(actor_id(this), &Td::send_result, id,
               messages_manager_->get_messages_object(-1, dialog_id, r_message_ids.ok(), false));
}

void Td::on_request(uint64 id, const td_api::resendMessages &request) {
  DialogId dialog_id(request.chat_id_);
  auto r_message_ids =
      messages_manager_->resend_messages(dialog_id, MessagesManager::get_message_ids(request.message_ids_));
  if (r_message_ids.is_error()) {
    return send_closure(actor_id(this), &Td::send_error, id, r_message_ids.move_as_error());
  }

  send_closure(actor_id(this), &Td::send_result, id,
               messages_manager_->get_messages_object(-1, dialog_id, r_message_ids.ok(), false));
}

void Td::on_request(uint64 id, td_api::getWebPagePreview &request) {
  CHECK_IS_USER();
  CREATE_REQUEST(GetWebPagePreviewRequest, std::move(request.text_));
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

void Td::on_request(uint64 id, td_api::createSecretChat &request) {
  CREATE_REQUEST(CreateChatRequest, DialogId(SecretChatId(request.secret_chat_id_)), true);
}

void Td::on_request(uint64 id, td_api::createNewBasicGroupChat &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.title_);
  CREATE_REQUEST(CreateNewGroupChatRequest, request.user_ids_, std::move(request.title_));
}

void Td::on_request(uint64 id, td_api::createNewSupergroupChat &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.title_);
  CLEAN_INPUT_STRING(request.description_);
  CREATE_REQUEST(CreateNewSupergroupChatRequest, std::move(request.title_), !request.is_channel_,
                 std::move(request.description_), std::move(request.location_), request.for_import_);
}
void Td::on_request(uint64 id, td_api::createNewSecretChat &request) {
  CREATE_REQUEST(CreateNewSecretChatRequest, request.user_id_);
}

void Td::on_request(uint64 id, const td_api::createCall &request) {
  CHECK_IS_USER();

  if (request.protocol_ == nullptr) {
    return send_error_raw(id, 400, "Call protocol must be non-empty");
  }

  UserId user_id(request.user_id_);
  auto input_user = contacts_manager_->get_input_user(user_id);
  if (input_user == nullptr) {
    return send_error_raw(id, 400, "User not found");
  }

  if (!G()->shared_config().get_option_boolean("calls_enabled")) {
    return send_error_raw(id, 400, "Calls are not enabled for the current user");
  }

  CREATE_REQUEST_PROMISE();
  auto query_promise = PromiseCreator::lambda([promise = std::move(promise)](Result<CallId> result) mutable {
    if (result.is_error()) {
      promise.set_error(result.move_as_error());
    } else {
      promise.set_value(result.ok().get_call_id_object());
    }
  });
  send_closure(G()->call_manager(), &CallManager::create_call, user_id, std::move(input_user),
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

void Td::on_request(uint64 id, const td_api::createVoiceChat &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  auto query_promise = PromiseCreator::lambda([promise = std::move(promise)](Result<GroupCallId> result) mutable {
    if (result.is_error()) {
      promise.set_error(result.move_as_error());
    } else {
      promise.set_value(td_api::make_object<td_api::groupCallId>(result.ok().get()));
    }
  });
  group_call_manager_->create_voice_chat(DialogId(request.chat_id_), std::move(query_promise));
}

void Td::on_request(uint64 id, const td_api::getGroupCall &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  group_call_manager_->get_group_call(GroupCallId(request.group_call_id_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::joinGroupCall &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  group_call_manager_->join_group_call(GroupCallId(request.group_call_id_), std::move(request.payload_),
                                       request.source_, request.is_muted_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::toggleGroupCallMuteNewParticipants &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  group_call_manager_->toggle_group_call_mute_new_participants(GroupCallId(request.group_call_id_),
                                                               request.mute_new_participants_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::inviteGroupCallParticipants &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  vector<UserId> user_ids;
  for (auto &user_id : request.user_ids_) {
    user_ids.emplace_back(user_id);
  }
  group_call_manager_->invite_group_call_participants(GroupCallId(request.group_call_id_), std::move(user_ids),
                                                      std::move(promise));
}

void Td::on_request(uint64 id, const td_api::setGroupCallParticipantIsSpeaking &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  group_call_manager_->set_group_call_participant_is_speaking(GroupCallId(request.group_call_id_), request.source_,
                                                              request.is_speaking_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::toggleGroupCallParticipantIsMuted &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  group_call_manager_->toggle_group_call_participant_is_muted(
      GroupCallId(request.group_call_id_), UserId(request.user_id_), request.is_muted_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::setGroupCallParticipantVolumeLevel &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  group_call_manager_->set_group_call_participant_volume_level(
      GroupCallId(request.group_call_id_), UserId(request.user_id_), request.volume_level_, std::move(promise));
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

void Td::on_request(uint64 id, const td_api::discardGroupCall &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  group_call_manager_->discard_group_call(GroupCallId(request.group_call_id_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::upgradeBasicGroupChatToSupergroupChat &request) {
  CHECK_IS_USER();
  CREATE_REQUEST(UpgradeGroupChatToSupergroupChatRequest, request.chat_id_);
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

void Td::on_request(uint64 id, const td_api::getChatFilter &request) {
  CHECK_IS_USER();
  CREATE_REQUEST(GetChatFilterRequest, request.chat_filter_id_);
}

void Td::on_request(uint64 id, const td_api::getRecommendedChatFilters &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  messages_manager_->get_recommended_dialog_filters(std::move(promise));
}

void Td::on_request(uint64 id, td_api::createChatFilter &request) {
  CHECK_IS_USER();
  if (request.filter_ == nullptr) {
    return send_error_raw(id, 400, "Chat filter must be non-empty");
  }
  CLEAN_INPUT_STRING(request.filter_->title_);
  CLEAN_INPUT_STRING(request.filter_->icon_name_);
  CREATE_REQUEST_PROMISE();
  messages_manager_->create_dialog_filter(std::move(request.filter_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::editChatFilter &request) {
  CHECK_IS_USER();
  if (request.filter_ == nullptr) {
    return send_error_raw(id, 400, "Chat filter must be non-empty");
  }
  CLEAN_INPUT_STRING(request.filter_->title_);
  CLEAN_INPUT_STRING(request.filter_->icon_name_);
  CREATE_REQUEST_PROMISE();
  messages_manager_->edit_dialog_filter(DialogFilterId(request.chat_filter_id_), std::move(request.filter_),
                                        std::move(promise));
}

void Td::on_request(uint64 id, const td_api::deleteChatFilter &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->delete_dialog_filter(DialogFilterId(request.chat_filter_id_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::reorderChatFilters &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->reorder_dialog_filters(
      transform(request.chat_filter_ids_, [](int32 id) { return DialogFilterId(id); }), std::move(promise));
}

void Td::on_request(uint64 id, td_api::setChatTitle &request) {
  CLEAN_INPUT_STRING(request.title_);
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->set_dialog_title(DialogId(request.chat_id_), request.title_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::setChatPhoto &request) {
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->set_dialog_photo(DialogId(request.chat_id_), request.photo_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::setChatMessageTtlSetting &request) {
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->set_dialog_message_ttl_setting(DialogId(request.chat_id_), request.ttl_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::setChatPermissions &request) {
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->set_dialog_permissions(DialogId(request.chat_id_), request.permissions_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::setChatDraftMessage &request) {
  CHECK_IS_USER();
  answer_ok_query(
      id, messages_manager_->set_dialog_draft_message(DialogId(request.chat_id_), MessageId(request.message_thread_id_),
                                                      std::move(request.draft_message_)));
}

void Td::on_request(uint64 id, const td_api::toggleChatIsPinned &request) {
  CHECK_IS_USER();
  answer_ok_query(id, messages_manager_->toggle_dialog_is_pinned(DialogListId(request.chat_list_),
                                                                 DialogId(request.chat_id_), request.is_pinned_));
}

void Td::on_request(uint64 id, const td_api::toggleChatIsMarkedAsUnread &request) {
  CHECK_IS_USER();
  answer_ok_query(id, messages_manager_->toggle_dialog_is_marked_as_unread(DialogId(request.chat_id_),
                                                                           request.is_marked_as_unread_));
}

void Td::on_request(uint64 id, const td_api::toggleMessageSenderIsBlocked &request) {
  CHECK_IS_USER();
  answer_ok_query(id, messages_manager_->toggle_message_sender_is_blocked(request.sender_, request.is_blocked_));
}

void Td::on_request(uint64 id, const td_api::toggleChatDefaultDisableNotification &request) {
  CHECK_IS_USER();
  answer_ok_query(id, messages_manager_->toggle_dialog_silent_send_message(DialogId(request.chat_id_),
                                                                           request.default_disable_notification_));
}

void Td::on_request(uint64 id, const td_api::setPinnedChats &request) {
  CHECK_IS_USER();
  answer_ok_query(id, messages_manager_->set_pinned_dialogs(
                          DialogListId(request.chat_list_),
                          transform(request.chat_ids_, [](int64 chat_id) { return DialogId(chat_id); })));
}

void Td::on_request(uint64 id, td_api::setChatClientData &request) {
  answer_ok_query(
      id, messages_manager_->set_dialog_client_data(DialogId(request.chat_id_), std::move(request.client_data_)));
}

void Td::on_request(uint64 id, td_api::setChatDescription &request) {
  CLEAN_INPUT_STRING(request.description_);
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->set_dialog_description(DialogId(request.chat_id_), request.description_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::setChatDiscussionGroup &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  contacts_manager_->set_channel_discussion_group(DialogId(request.chat_id_), DialogId(request.discussion_chat_id_),
                                                  std::move(promise));
}

void Td::on_request(uint64 id, td_api::setChatLocation &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  contacts_manager_->set_channel_location(DialogId(request.chat_id_), DialogLocation(std::move(request.location_)),
                                          std::move(promise));
}

void Td::on_request(uint64 id, const td_api::setChatSlowModeDelay &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  contacts_manager_->set_channel_slow_mode_delay(DialogId(request.chat_id_), request.slow_mode_delay_,
                                                 std::move(promise));
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
  messages_manager_->unpin_all_dialog_messages(DialogId(request.chat_id_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::joinChat &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  contacts_manager_->add_dialog_participant(DialogId(request.chat_id_), contacts_manager_->get_my_id(), 0,
                                            std::move(promise));
}

void Td::on_request(uint64 id, const td_api::leaveChat &request) {
  CREATE_OK_REQUEST_PROMISE();
  DialogId dialog_id(request.chat_id_);
  td_api::object_ptr<td_api::ChatMemberStatus> new_status = td_api::make_object<td_api::chatMemberStatusLeft>();
  if (dialog_id.get_type() == DialogType::Channel && messages_manager_->have_dialog_force(dialog_id)) {
    auto status = contacts_manager_->get_channel_status(dialog_id.get_channel_id());
    if (status.is_creator()) {
      if (!status.is_member()) {
        return promise.set_value(Unit());
      }

      new_status =
          td_api::make_object<td_api::chatMemberStatusCreator>(status.get_rank(), status.is_anonymous(), false);
    }
  }
  contacts_manager_->set_dialog_participant_status(dialog_id, contacts_manager_->get_my_id(), std::move(new_status),
                                                   std::move(promise));
}

void Td::on_request(uint64 id, const td_api::addChatMember &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  contacts_manager_->add_dialog_participant(DialogId(request.chat_id_), UserId(request.user_id_),
                                            request.forward_limit_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::addChatMembers &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  vector<UserId> user_ids;
  for (auto &user_id : request.user_ids_) {
    user_ids.emplace_back(user_id);
  }
  contacts_manager_->add_dialog_participants(DialogId(request.chat_id_), user_ids, std::move(promise));
}

void Td::on_request(uint64 id, td_api::setChatMemberStatus &request) {
  CREATE_OK_REQUEST_PROMISE();
  contacts_manager_->set_dialog_participant_status(DialogId(request.chat_id_), UserId(request.user_id_),
                                                   request.status_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::banChatMember &request) {
  CREATE_OK_REQUEST_PROMISE();
  contacts_manager_->ban_dialog_participant(DialogId(request.chat_id_), UserId(request.user_id_),
                                            request.banned_until_date_, request.revoke_messages_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::canTransferOwnership &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  auto query_promise =
      PromiseCreator::lambda([promise = std::move(promise)](Result<CanTransferOwnershipResult> result) mutable {
        if (result.is_error()) {
          promise.set_error(result.move_as_error());
        } else {
          promise.set_value(ContactsManager::get_can_transfer_ownership_result_object(result.ok()));
        }
      });
  contacts_manager_->can_transfer_ownership(std::move(query_promise));
}

void Td::on_request(uint64 id, td_api::transferChatOwnership &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.password_);
  CREATE_OK_REQUEST_PROMISE();
  contacts_manager_->transfer_dialog_ownership(DialogId(request.chat_id_), UserId(request.user_id_), request.password_,
                                               std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getChatMember &request) {
  CREATE_REQUEST(GetChatMemberRequest, request.chat_id_, request.user_id_);
}

void Td::on_request(uint64 id, td_api::searchChatMembers &request) {
  CLEAN_INPUT_STRING(request.query_);
  CREATE_REQUEST_PROMISE();
  auto query_promise =
      PromiseCreator::lambda([promise = std::move(promise), td = this](Result<DialogParticipants> result) mutable {
        if (result.is_error()) {
          promise.set_error(result.move_as_error());
        } else {
          promise.set_value(result.ok().get_chat_members_object(td));
        }
      });
  contacts_manager_->search_dialog_participants(DialogId(request.chat_id_), request.query_, request.limit_,
                                                get_dialog_participants_filter(request.filter_), false,
                                                std::move(query_promise));
}

void Td::on_request(uint64 id, td_api::getChatAdministrators &request) {
  CREATE_REQUEST(GetChatAdministratorsRequest, request.chat_id_);
}

void Td::on_request(uint64 id, const td_api::replacePrimaryChatInviteLink &request) {
  CREATE_REQUEST_PROMISE();
  contacts_manager_->export_dialog_invite_link(DialogId(request.chat_id_), 0, 0, true, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::createChatInviteLink &request) {
  CREATE_REQUEST_PROMISE();
  contacts_manager_->export_dialog_invite_link(DialogId(request.chat_id_), request.expire_date_, request.member_limit_,
                                               false, std::move(promise));
}

void Td::on_request(uint64 id, td_api::editChatInviteLink &request) {
  CLEAN_INPUT_STRING(request.invite_link_);
  CREATE_REQUEST_PROMISE();
  contacts_manager_->edit_dialog_invite_link(DialogId(request.chat_id_), request.invite_link_, request.expire_date_,
                                             request.member_limit_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::getChatInviteLink &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.invite_link_);
  CREATE_REQUEST_PROMISE();
  contacts_manager_->get_dialog_invite_link(DialogId(request.chat_id_), request.invite_link_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getChatInviteLinkCounts &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  contacts_manager_->get_dialog_invite_link_counts(DialogId(request.chat_id_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::getChatInviteLinks &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.offset_invite_link_);
  CREATE_REQUEST_PROMISE();
  contacts_manager_->get_dialog_invite_links(DialogId(request.chat_id_), UserId(request.creator_user_id_),
                                             request.is_revoked_, request.offset_date_, request.offset_invite_link_,
                                             request.limit_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::getChatInviteLinkMembers &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.invite_link_);
  CREATE_REQUEST_PROMISE();
  contacts_manager_->get_dialog_invite_link_users(DialogId(request.chat_id_), request.invite_link_,
                                                  std::move(request.offset_member_), request.limit_,
                                                  std::move(promise));
}

void Td::on_request(uint64 id, td_api::revokeChatInviteLink &request) {
  CLEAN_INPUT_STRING(request.invite_link_);
  CREATE_REQUEST_PROMISE();
  contacts_manager_->revoke_dialog_invite_link(DialogId(request.chat_id_), request.invite_link_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::deleteRevokedChatInviteLink &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.invite_link_);
  CREATE_OK_REQUEST_PROMISE();
  contacts_manager_->delete_revoked_dialog_invite_link(DialogId(request.chat_id_), request.invite_link_,
                                                       std::move(promise));
}

void Td::on_request(uint64 id, const td_api::deleteAllRevokedChatInviteLinks &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  contacts_manager_->delete_all_revoked_dialog_invite_links(DialogId(request.chat_id_),
                                                            UserId(request.creator_user_id_), std::move(promise));
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
  CREATE_REQUEST(GetChatEventLogRequest, request.chat_id_, std::move(request.query_), request.from_event_id_,
                 request.limit_, std::move(request.filters_), std::move(request.user_ids_));
}

void Td::on_request(uint64 id, const td_api::clearAllDraftMessages &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->clear_all_draft_messages(request.exclude_secret_chats_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::downloadFile &request) {
  auto priority = request.priority_;
  if (!(1 <= priority && priority <= 32)) {
    return send_error_raw(id, 5, "Download priority must be in [1;32] range");
  }
  auto offset = request.offset_;
  if (offset < 0) {
    return send_error_raw(id, 5, "Download offset must be non-negative");
  }
  auto limit = request.limit_;
  if (limit < 0) {
    return send_error_raw(id, 5, "Download limit must be non-negative");
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
    for (auto request_id : info->request_ids) {
      send_closure(actor_id(this), &Td::send_error, request_id,
                   Status::Error(200, "Cancelled by another downloadFile request"));
    }
    info->request_ids.clear();
  }
  if (request.synchronous_) {
    if (info == nullptr) {
      info = &pending_file_downloads_[file_id];
    }
    info->offset = offset;
    info->limit = limit;
    info->request_ids.push_back(id);
  }
  file_manager_->download(file_id, download_file_callback_, priority, offset, limit);
  if (!request.synchronous_) {
    send_closure(actor_id(this), &Td::send_result, id, file_manager_->get_file_object(file_id, false));
  }
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
      limit = std::numeric_limits<int32>::max();
    }
    if (file_object->local_->is_downloading_completed_ ||
        (download_offset <= it->second.offset && download_offset + downloaded_size >= it->second.offset &&
         ((file_size != 0 && download_offset + downloaded_size == file_size) ||
          download_offset + downloaded_size - it->second.offset >= limit))) {
      send_result(id, std::move(file_object));
    } else {
      send_error_impl(id, td_api::make_object<td_api::error>(400, "File download has failed or was cancelled"));
    }
  }
  pending_file_downloads_.erase(it);
}

void Td::on_request(uint64 id, const td_api::getFileDownloadedPrefixSize &request) {
  if (request.offset_ < 0) {
    return send_error_raw(id, 5, "Parameter offset must be non-negative");
  }
  auto file_view = file_manager_->get_file_view(FileId(request.file_id_, 0));
  if (file_view.empty()) {
    return send_closure(actor_id(this), &Td::send_error, id, Status::Error(10, "Unknown file ID"));
  }
  send_closure(actor_id(this), &Td::send_result, id,
               td_api::make_object<td_api::count>(narrow_cast<int32>(file_view.downloaded_prefix(request.offset_))));
}

void Td::on_request(uint64 id, const td_api::cancelDownloadFile &request) {
  file_manager_->download(FileId(request.file_id_, 0), nullptr, request.only_if_pending_ ? -1 : 0, -1, -1);

  send_closure(actor_id(this), &Td::send_result, id, make_tl_object<td_api::ok>());
}

void Td::on_request(uint64 id, td_api::uploadFile &request) {
  auto priority = request.priority_;
  if (!(1 <= priority && priority <= 32)) {
    return send_error_raw(id, 5, "Upload priority must be in [1;32] range");
  }

  auto file_type = request.file_type_ == nullptr ? FileType::Temp : get_file_type(*request.file_type_);
  bool is_secret = file_type == FileType::Encrypted || file_type == FileType::EncryptedThumbnail;
  bool is_secure = file_type == FileType::Secure;
  auto r_file_id = file_manager_->get_input_file_id(file_type, request.file_, DialogId(), false, is_secret,
                                                    !is_secure && !is_secret, is_secure);
  if (r_file_id.is_error()) {
    return send_error_raw(id, 400, r_file_id.error().message());
  }
  auto file_id = r_file_id.ok();
  auto upload_file_id = file_manager_->dup_file_id(file_id);

  file_manager_->upload(upload_file_id, upload_file_callback_, priority, 0);

  send_closure(actor_id(this), &Td::send_result, id, file_manager_->get_file_object(upload_file_id, false));
}

void Td::on_request(uint64 id, const td_api::cancelUploadFile &request) {
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

void Td::on_request(uint64 id, td_api::getMessageFileType &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.message_file_head_);
  CREATE_REQUEST_PROMISE();
  messages_manager_->get_message_file_type(request.message_file_head_, std::move(promise));
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
  messages_manager_->get_message_import_confirmation_text(DialogId(request.chat_id_), std::move(query_promise));
}

void Td::on_request(uint64 id, const td_api::importMessages &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->import_messages(DialogId(request.chat_id_), request.message_file_, request.attached_files_,
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
  CREATE_REQUEST(GetBlockedMessageSendersRequest, request.offset_, request.limit_);
}

void Td::on_request(uint64 id, td_api::addContact &request) {
  CHECK_IS_USER();
  if (request.contact_ == nullptr) {
    return send_error_raw(id, 5, "Contact must be non-empty");
  }
  CLEAN_INPUT_STRING(request.contact_->phone_number_);
  CLEAN_INPUT_STRING(request.contact_->first_name_);
  CLEAN_INPUT_STRING(request.contact_->last_name_);
  CREATE_OK_REQUEST_PROMISE();
  contacts_manager_->add_contact(std::move(request.contact_), request.share_phone_number_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::importContacts &request) {
  CHECK_IS_USER();
  for (auto &contact : request.contacts_) {
    if (contact == nullptr) {
      return send_error_raw(id, 5, "Contact must be non-empty");
    }
    CLEAN_INPUT_STRING(contact->phone_number_);
    CLEAN_INPUT_STRING(contact->first_name_);
    CLEAN_INPUT_STRING(contact->last_name_);
  }
  CREATE_REQUEST(ImportContactsRequest, std::move(request.contacts_));
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
  CREATE_REQUEST(RemoveContactsRequest, std::move(request.user_ids_));
}

void Td::on_request(uint64 id, const td_api::getImportedContactCount &request) {
  CHECK_IS_USER();
  CREATE_NO_ARGS_REQUEST(GetImportedContactCountRequest);
}

void Td::on_request(uint64 id, td_api::changeImportedContacts &request) {
  CHECK_IS_USER();
  for (auto &contact : request.contacts_) {
    if (contact == nullptr) {
      return send_error_raw(id, 5, "Contact must be non-empty");
    }
    CLEAN_INPUT_STRING(contact->phone_number_);
    CLEAN_INPUT_STRING(contact->first_name_);
    CLEAN_INPUT_STRING(contact->last_name_);
  }
  CREATE_REQUEST(ChangeImportedContactsRequest, std::move(request.contacts_));
}

void Td::on_request(uint64 id, const td_api::clearImportedContacts &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  contacts_manager_->clear_imported_contacts(std::move(promise));
}

void Td::on_request(uint64 id, const td_api::sharePhoneNumber &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  contacts_manager_->share_phone_number(UserId(request.user_id_), std::move(promise));
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
  contacts_manager_->set_name(request.first_name_, request.last_name_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::setBio &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.bio_);
  CREATE_OK_REQUEST_PROMISE();
  contacts_manager_->set_bio(request.bio_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::setUsername &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.username_);
  CREATE_OK_REQUEST_PROMISE();
  contacts_manager_->set_username(request.username_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::setCommands &request) {
  CHECK_IS_BOT();
  CREATE_OK_REQUEST_PROMISE();
  contacts_manager_->set_commands(std::move(request.commands_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::setLocation &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  contacts_manager_->set_location(Location(request.location_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::setProfilePhoto &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  contacts_manager_->set_profile_photo(request.photo_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::deleteProfilePhoto &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  contacts_manager_->delete_profile_photo(request.profile_photo_id_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getUserProfilePhotos &request) {
  CREATE_REQUEST(GetUserProfilePhotosRequest, request.user_id_, request.offset_, request.limit_);
}

void Td::on_request(uint64 id, td_api::setSupergroupUsername &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.username_);
  CREATE_OK_REQUEST_PROMISE();
  contacts_manager_->set_channel_username(ChannelId(request.supergroup_id_), request.username_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::setSupergroupStickerSet &request) {
  CREATE_OK_REQUEST_PROMISE();
  contacts_manager_->set_channel_sticker_set(ChannelId(request.supergroup_id_), StickerSetId(request.sticker_set_id_),
                                             std::move(promise));
}

void Td::on_request(uint64 id, const td_api::toggleSupergroupSignMessages &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  contacts_manager_->toggle_channel_sign_messages(ChannelId(request.supergroup_id_), request.sign_messages_,
                                                  std::move(promise));
}

void Td::on_request(uint64 id, const td_api::toggleSupergroupIsAllHistoryAvailable &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  contacts_manager_->toggle_channel_is_all_history_available(ChannelId(request.supergroup_id_),
                                                             request.is_all_history_available_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::toggleSupergroupIsBroadcastGroup &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  contacts_manager_->convert_channel_to_gigagroup(ChannelId(request.supergroup_id_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::reportSupergroupSpam &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  contacts_manager_->report_channel_spam(ChannelId(request.supergroup_id_), UserId(request.user_id_),
                                         MessagesManager::get_message_ids(request.message_ids_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::getSupergroupMembers &request) {
  CREATE_REQUEST_PROMISE();
  auto query_promise =
      PromiseCreator::lambda([promise = std::move(promise), td = this](Result<DialogParticipants> result) mutable {
        if (result.is_error()) {
          promise.set_error(result.move_as_error());
        } else {
          promise.set_value(result.ok().get_chat_members_object(td));
        }
      });
  contacts_manager_->get_channel_participants(ChannelId(request.supergroup_id_), std::move(request.filter_), string(),
                                              request.offset_, request.limit_, -1, false, std::move(query_promise));
}

void Td::on_request(uint64 id, td_api::closeSecretChat &request) {
  CREATE_OK_REQUEST_PROMISE();
  send_closure(secret_chats_manager_, &SecretChatsManager::cancel_chat, SecretChatId(request.secret_chat_id_), false,
               std::move(promise));
}

void Td::on_request(uint64 id, td_api::getStickers &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.emoji_);
  CREATE_REQUEST(GetStickersRequest, std::move(request.emoji_), request.limit_);
}

void Td::on_request(uint64 id, td_api::searchStickers &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.emoji_);
  CREATE_REQUEST(SearchStickersRequest, std::move(request.emoji_), request.limit_);
}

void Td::on_request(uint64 id, const td_api::getInstalledStickerSets &request) {
  CHECK_IS_USER();
  CREATE_REQUEST(GetInstalledStickerSetsRequest, request.is_masks_);
}

void Td::on_request(uint64 id, const td_api::getArchivedStickerSets &request) {
  CHECK_IS_USER();
  CREATE_REQUEST(GetArchivedStickerSetsRequest, request.is_masks_, request.offset_sticker_set_id_, request.limit_);
}

void Td::on_request(uint64 id, const td_api::getTrendingStickerSets &request) {
  CHECK_IS_USER();
  CREATE_REQUEST(GetTrendingStickerSetsRequest, request.offset_, request.limit_);
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
  CREATE_REQUEST(SearchInstalledStickerSetsRequest, request.is_masks_, std::move(request.query_), request.limit_);
}

void Td::on_request(uint64 id, td_api::searchStickerSets &request) {
  CLEAN_INPUT_STRING(request.query_);
  CREATE_REQUEST(SearchStickerSetsRequest, std::move(request.query_));
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
  stickers_manager_->reorder_installed_sticker_sets(
      request.is_masks_, StickersManager::convert_sticker_set_ids(request.sticker_set_ids_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::uploadStickerFile &request) {
  CHECK_IS_BOT();
  CREATE_REQUEST(UploadStickerFileRequest, request.user_id_, std::move(request.png_sticker_));
}

void Td::on_request(uint64 id, td_api::createNewStickerSet &request) {
  CHECK_IS_BOT();
  CLEAN_INPUT_STRING(request.title_);
  CLEAN_INPUT_STRING(request.name_);
  CREATE_REQUEST(CreateNewStickerSetRequest, request.user_id_, std::move(request.title_), std::move(request.name_),
                 request.is_masks_, std::move(request.stickers_));
}

void Td::on_request(uint64 id, td_api::addStickerToSet &request) {
  CHECK_IS_BOT();
  CLEAN_INPUT_STRING(request.name_);
  CREATE_REQUEST(AddStickerToSetRequest, request.user_id_, std::move(request.name_), std::move(request.sticker_));
}

void Td::on_request(uint64 id, td_api::setStickerSetThumbnail &request) {
  CHECK_IS_BOT();
  CLEAN_INPUT_STRING(request.name_);
  CREATE_REQUEST(SetStickerSetThumbnailRequest, request.user_id_, std::move(request.name_),
                 std::move(request.thumbnail_));
}

void Td::on_request(uint64 id, td_api::setStickerPositionInSet &request) {
  CHECK_IS_BOT();
  CREATE_OK_REQUEST_PROMISE();
  stickers_manager_->set_sticker_position_in_set(request.sticker_, request.position_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::removeStickerFromSet &request) {
  CHECK_IS_BOT();
  CREATE_OK_REQUEST_PROMISE();
  stickers_manager_->remove_sticker_from_set(request.sticker_, std::move(promise));
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
  CREATE_REQUEST(SearchEmojisRequest, std::move(request.text_), request.exact_match_,
                 std::move(request.input_language_codes_));
}

void Td::on_request(uint64 id, td_api::getEmojiSuggestionsUrl &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.language_code_);
  CREATE_REQUEST(GetEmojiSuggestionsUrlRequest, std::move(request.language_code_));
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
  messages_manager_->report_dialog(DialogId(request.chat_id_), MessagesManager::get_message_ids(request.message_ids_),
                                   r_report_reason.move_as_ok(), std::move(promise));
}

void Td::on_request(uint64 id, td_api::reportChatPhoto &request) {
  CHECK_IS_USER();
  auto r_report_reason = ReportReason::get_report_reason(std::move(request.reason_), std::move(request.text_));
  if (r_report_reason.is_error()) {
    return send_error_raw(id, r_report_reason.error().code(), r_report_reason.error().message());
  }
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->report_dialog_photo(DialogId(request.chat_id_), FileId(request.file_id_, 0),
                                         r_report_reason.move_as_ok(), std::move(promise));
}

void Td::on_request(uint64 id, td_api::getChatStatisticsUrl &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.parameters_);
  CREATE_REQUEST_PROMISE();
  messages_manager_->get_dialog_statistics_url(DialogId(request.chat_id_), request.parameters_, request.is_dark_,
                                               std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getChatStatistics &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  contacts_manager_->get_channel_statistics(DialogId(request.chat_id_), request.is_dark_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getMessageStatistics &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  contacts_manager_->get_channel_message_statistics({DialogId(request.chat_id_), MessageId(request.message_id_)},
                                                    request.is_dark_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::getStatisticalGraph &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.token_);
  CREATE_REQUEST_PROMISE();
  contacts_manager_->load_statistics_graph(DialogId(request.chat_id_), request.token_, request.x_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::setChatNotificationSettings &request) {
  CHECK_IS_USER();
  answer_ok_query(id, messages_manager_->set_dialog_notification_settings(DialogId(request.chat_id_),
                                                                          std::move(request.notification_settings_)));
}

void Td::on_request(uint64 id, td_api::setScopeNotificationSettings &request) {
  CHECK_IS_USER();
  if (request.scope_ == nullptr) {
    return send_error_raw(id, 400, "Scope must be non-empty");
  }
  answer_ok_query(id, messages_manager_->set_scope_notification_settings(
                          get_notification_settings_scope(request.scope_), std::move(request.notification_settings_)));
}

void Td::on_request(uint64 id, const td_api::resetAllNotificationSettings &request) {
  CHECK_IS_USER();
  messages_manager_->reset_all_notification_settings();
  send_closure(actor_id(this), &Td::send_result, id, make_tl_object<td_api::ok>());
}

void Td::on_request(uint64 id, const td_api::getMapThumbnailFile &request) {
  DialogId dialog_id(request.chat_id_);
  if (!messages_manager_->have_dialog_force(dialog_id)) {
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

  tl_object_ptr<td_api::OptionValue> option_value;
  bool is_bot = auth_manager_ != nullptr && auth_manager_->is_authorized() && auth_manager_->is_bot();
  switch (request.name_[0]) {
    // all these options should be added to getCurrentState
    case 'a':
      if (!is_bot && request.name_ == "archive_and_mute_new_chats_from_unknown_users") {
        auto promise = PromiseCreator::lambda([actor_id = actor_id(this), id](Result<Unit> &&result) {
          // the option is already updated on success, ignore errors
          send_closure(actor_id, &Td::send_result, id,
                       G()->shared_config().get_option_value("archive_and_mute_new_chats_from_unknown_users"));
        });
        send_closure_later(config_manager_, &ConfigManager::get_global_privacy_settings, std::move(promise));
        return;
      }
      break;
    case 'c':
      if (!is_bot && request.name_ == "can_ignore_sensitive_content_restrictions") {
        auto promise = PromiseCreator::lambda([actor_id = actor_id(this), id](Result<Unit> &&result) {
          // the option is already updated on success, ignore errors
          send_closure(actor_id, &Td::send_result, id,
                       G()->shared_config().get_option_value("can_ignore_sensitive_content_restrictions"));
        });
        send_closure_later(config_manager_, &ConfigManager::get_content_settings, std::move(promise));
        return;
      }
      break;
    case 'd':
      if (!is_bot && request.name_ == "disable_contact_registered_notifications") {
        auto promise = PromiseCreator::lambda([actor_id = actor_id(this), id](Result<Unit> &&result) {
          // the option is already updated on success, ignore errors
          send_closure(actor_id, &Td::send_result, id,
                       G()->shared_config().get_option_value("disable_contact_registered_notifications"));
        });
        send_closure_later(notification_manager_actor_,
                           &NotificationManager::get_disable_contact_registered_notifications, std::move(promise));
        return;
      }
      break;
    case 'i':
      if (!is_bot && request.name_ == "ignore_sensitive_content_restrictions") {
        auto promise = PromiseCreator::lambda([actor_id = actor_id(this), id](Result<Unit> &&result) {
          // the option is already updated on success, ignore errors
          send_closure(actor_id, &Td::send_result, id,
                       G()->shared_config().get_option_value("ignore_sensitive_content_restrictions"));
        });
        send_closure_later(config_manager_, &ConfigManager::get_content_settings, std::move(promise));
        return;
      }
      break;
    case 'o':
      if (request.name_ == "online") {
        option_value = make_tl_object<td_api::optionValueBoolean>(is_online_);
      }
      break;
    case 'u':
      if (request.name_ == "unix_time") {
        option_value = make_tl_object<td_api::optionValueInteger>(G()->unix_time());
      }
      break;
    case 'v':
      if (request.name_ == "version") {
        option_value = make_tl_object<td_api::optionValueString>(TDLIB_VERSION);
      }
      break;
  }
  if (option_value == nullptr) {
    option_value = G()->shared_config().get_option_value(request.name_);
  }
  send_closure(actor_id(this), &Td::send_result, id, std::move(option_value));
}

void Td::on_request(uint64 id, td_api::setOption &request) {
  CLEAN_INPUT_STRING(request.name_);
  int32 value_constructor_id = request.value_ == nullptr ? td_api::optionValueEmpty::ID : request.value_->get_id();

  LOG(INFO) << "Set option " << request.name_;

  auto set_integer_option = [&](Slice name, int64 min = 0, int64 max = std::numeric_limits<int32>::max()) {
    if (request.name_ != name) {
      return false;
    }
    if (value_constructor_id != td_api::optionValueInteger::ID &&
        value_constructor_id != td_api::optionValueEmpty::ID) {
      send_error_raw(id, 3, PSLICE() << "Option \"" << name << "\" must have integer value");
      return true;
    }
    if (value_constructor_id == td_api::optionValueEmpty::ID) {
      G()->shared_config().set_option_empty(name);
    } else {
      int64 value = static_cast<td_api::optionValueInteger *>(request.value_.get())->value_;
      if (value < min || value > max) {
        send_error_raw(id, 3,
                       PSLICE() << "Option's \"" << name << "\" value " << value << " is outside of a valid range ["
                                << min << ", " << max << "]");
        return true;
      }
      G()->shared_config().set_option_integer(name, clamp(value, min, max));
    }
    send_closure(actor_id(this), &Td::send_result, id, make_tl_object<td_api::ok>());
    return true;
  };

  auto set_boolean_option = [&](Slice name) {
    if (request.name_ != name) {
      return false;
    }
    if (value_constructor_id != td_api::optionValueBoolean::ID &&
        value_constructor_id != td_api::optionValueEmpty::ID) {
      send_error_raw(id, 3, PSLICE() << "Option \"" << name << "\" must have boolean value");
      return true;
    }
    if (value_constructor_id == td_api::optionValueEmpty::ID) {
      G()->shared_config().set_option_empty(name);
    } else {
      bool value = static_cast<td_api::optionValueBoolean *>(request.value_.get())->value_;
      G()->shared_config().set_option_boolean(name, value);
    }
    send_closure(actor_id(this), &Td::send_result, id, make_tl_object<td_api::ok>());
    return true;
  };

  auto set_string_option = [&](Slice name, auto check_value) {
    if (request.name_ != name) {
      return false;
    }
    if (value_constructor_id != td_api::optionValueString::ID && value_constructor_id != td_api::optionValueEmpty::ID) {
      send_error_raw(id, 3, PSLICE() << "Option \"" << name << "\" must have string value");
      return true;
    }
    if (value_constructor_id == td_api::optionValueEmpty::ID) {
      G()->shared_config().set_option_empty(name);
    } else {
      const string &value = static_cast<td_api::optionValueString *>(request.value_.get())->value_;
      if (value.empty()) {
        G()->shared_config().set_option_empty(name);
      } else {
        if (check_value(value)) {
          G()->shared_config().set_option_string(name, value);
        } else {
          send_error_raw(id, 3, PSLICE() << "Option \"" << name << "\" can't have specified value");
          return true;
        }
      }
    }
    send_closure(actor_id(this), &Td::send_result, id, make_tl_object<td_api::ok>());
    return true;
  };

  bool is_bot = auth_manager_ != nullptr && auth_manager_->is_authorized() && auth_manager_->is_bot();
  switch (request.name_[0]) {
    case 'a':
      if (set_boolean_option("always_parse_markdown")) {
        return;
      }
      if (!is_bot && request.name_ == "archive_and_mute_new_chats_from_unknown_users") {
        if (value_constructor_id != td_api::optionValueBoolean::ID &&
            value_constructor_id != td_api::optionValueEmpty::ID) {
          return send_error_raw(id, 3,
                                "Option \"archive_and_mute_new_chats_from_unknown_users\" must have boolean value");
        }

        auto archive_and_mute = value_constructor_id == td_api::optionValueBoolean::ID &&
                                static_cast<td_api::optionValueBoolean *>(request.value_.get())->value_;
        CREATE_OK_REQUEST_PROMISE();
        send_closure_later(config_manager_, &ConfigManager::set_archive_and_mute, archive_and_mute, std::move(promise));
        return;
      }
      break;
    case 'c':
      if (!is_bot && set_string_option("connection_parameters", [](Slice value) {
            string value_copy = value.str();
            auto r_json_value = get_json_value(value_copy);
            if (r_json_value.is_error()) {
              return false;
            }
            return r_json_value.ok()->get_id() == td_api::jsonValueObject::ID;
          })) {
        return;
      }
      break;
    case 'd':
      if (!is_bot && set_boolean_option("disable_contact_registered_notifications")) {
        return;
      }
      if (!is_bot && set_boolean_option("disable_sent_scheduled_message_notifications")) {
        return;
      }
      if (!is_bot && set_boolean_option("disable_top_chats")) {
        return;
      }
      if (set_boolean_option("disable_persistent_network_statistics")) {
        return;
      }
      if (set_boolean_option("disable_time_adjustment_protection")) {
        return;
      }
      if (request.name_ == "drop_notification_ids") {
        G()->td_db()->get_binlog_pmc()->erase("notification_id_current");
        G()->td_db()->get_binlog_pmc()->erase("notification_group_id_current");
        send_closure(actor_id(this), &Td::send_result, id, make_tl_object<td_api::ok>());
        return;
      }
      break;
    case 'i':
      if (set_boolean_option("ignore_background_updates")) {
        return;
      }
      if (set_boolean_option("ignore_default_disable_notification")) {
        return;
      }
      if (set_boolean_option("ignore_inline_thumbnails")) {
        return;
      }
      if (set_boolean_option("ignore_platform_restrictions")) {
        return;
      }
      if (set_boolean_option("is_emulator")) {
        return;
      }
      if (!is_bot && request.name_ == "ignore_sensitive_content_restrictions") {
        if (!G()->shared_config().get_option_boolean("can_ignore_sensitive_content_restrictions")) {
          return send_error_raw(id, 3, "Option \"ignore_sensitive_content_restrictions\" can't be changed by the user");
        }

        if (value_constructor_id != td_api::optionValueBoolean::ID &&
            value_constructor_id != td_api::optionValueEmpty::ID) {
          return send_error_raw(id, 3, "Option \"ignore_sensitive_content_restrictions\" must have boolean value");
        }

        auto ignore_sensitive_content_restrictions =
            value_constructor_id == td_api::optionValueBoolean::ID &&
            static_cast<td_api::optionValueBoolean *>(request.value_.get())->value_;
        CREATE_OK_REQUEST_PROMISE();
        send_closure_later(config_manager_, &ConfigManager::set_content_settings, ignore_sensitive_content_restrictions,
                           std::move(promise));
        return;
      }
      if (!is_bot && set_boolean_option("is_location_visible")) {
        contacts_manager_->set_location_visibility();
        return;
      }
      break;
    case 'l':
      if (!is_bot && set_string_option("language_pack_database_path", [](Slice value) { return true; })) {
        return;
      }
      if (!is_bot && set_string_option("localization_target", LanguagePackManager::check_language_pack_name)) {
        return;
      }
      if (!is_bot && set_string_option("language_pack_id", LanguagePackManager::check_language_code_name)) {
        return;
      }
      break;
    case 'm':
      if (set_integer_option("message_unload_delay", 60, 86400)) {
        return;
      }
      break;
    case 'n':
      if (!is_bot &&
          set_integer_option("notification_group_count_max", NotificationManager::MIN_NOTIFICATION_GROUP_COUNT_MAX,
                             NotificationManager::MAX_NOTIFICATION_GROUP_COUNT_MAX)) {
        return;
      }
      if (!is_bot &&
          set_integer_option("notification_group_size_max", NotificationManager::MIN_NOTIFICATION_GROUP_SIZE_MAX,
                             NotificationManager::MAX_NOTIFICATION_GROUP_SIZE_MAX)) {
        return;
      }
      break;
    case 'o':
      if (request.name_ == "online") {
        if (value_constructor_id != td_api::optionValueBoolean::ID &&
            value_constructor_id != td_api::optionValueEmpty::ID) {
          return send_error_raw(id, 3, "Option \"online\" must have boolean value");
        }
        bool is_online = value_constructor_id == td_api::optionValueEmpty::ID ||
                         static_cast<const td_api::optionValueBoolean *>(request.value_.get())->value_;
        if (!is_bot) {
          send_closure(G()->state_manager(), &StateManager::on_online, is_online);
        }
        if (is_online != is_online_) {
          is_online_ = is_online;
          if (auth_manager_ != nullptr) {  // postpone if there is no AuthManager yet
            on_online_updated(true, true);
          }
        }
        return send_closure(actor_id(this), &Td::send_result, id, make_tl_object<td_api::ok>());
      }
      break;
    case 'p':
      if (set_boolean_option("prefer_ipv6")) {
        send_closure(state_manager_, &StateManager::on_network_updated);
        return;
      }
      break;
    case 'r':
      // temporary option
      if (set_boolean_option("reuse_uploaded_photos_by_hash")) {
        return;
      }
      break;
    case 's':
      if (set_integer_option("session_count", 0, 50)) {
        return;
      }
      if (set_integer_option("storage_max_files_size")) {
        return;
      }
      if (set_integer_option("storage_max_time_from_last_access")) {
        return;
      }
      if (set_integer_option("storage_max_file_count")) {
        return;
      }
      if (set_integer_option("storage_immunity_delay")) {
        return;
      }
      if (set_boolean_option("store_all_files_in_files_directory")) {
        return;
      }
      break;
    case 't':
      if (set_boolean_option("test_flood_wait")) {
        return;
      }
      break;
    case 'X':
    case 'x': {
      if (request.name_.size() > 255) {
        return send_error_raw(id, 3, "Option name is too long");
      }
      switch (value_constructor_id) {
        case td_api::optionValueBoolean::ID:
          G()->shared_config().set_option_boolean(
              request.name_, static_cast<const td_api::optionValueBoolean *>(request.value_.get())->value_);
          break;
        case td_api::optionValueEmpty::ID:
          G()->shared_config().set_option_empty(request.name_);
          break;
        case td_api::optionValueInteger::ID:
          G()->shared_config().set_option_integer(
              request.name_, static_cast<const td_api::optionValueInteger *>(request.value_.get())->value_);
          break;
        case td_api::optionValueString::ID:
          G()->shared_config().set_option_string(
              request.name_, static_cast<const td_api::optionValueString *>(request.value_.get())->value_);
          break;
        default:
          UNREACHABLE();
      }
      return send_closure(actor_id(this), &Td::send_result, id, make_tl_object<td_api::ok>());
    }
    case 'u':
      if (set_boolean_option("use_pfs")) {
        return;
      }
      if (set_boolean_option("use_quick_ack")) {
        return;
      }
      if (set_boolean_option("use_storage_optimizer")) {
        return;
      }
      break;
  }

  return send_error_raw(id, 3, "Option can't be set");
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
  auto query_promise = PromiseCreator::lambda(
      [promise = std::move(promise), td = this](Result<std::pair<int32, vector<UserId>>> result) mutable {
        if (result.is_error()) {
          promise.set_error(result.move_as_error());
        } else {
          promise.set_value(td->contacts_manager_->get_users_object(result.ok().first, result.ok().second));
        }
      });
  messages_manager_->get_poll_voters({DialogId(request.chat_id_), MessageId(request.message_id_)}, request.option_id_,
                                     request.offset_, request.limit_, std::move(query_promise));
}

void Td::on_request(uint64 id, td_api::stopPoll &request) {
  CREATE_OK_REQUEST_PROMISE();
  messages_manager_->stop_poll({DialogId(request.chat_id_), MessageId(request.message_id_)},
                               std::move(request.reply_markup_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::hideSuggestedAction &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  contacts_manager_->dismiss_suggested_action(SuggestedAction(request.action_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getLoginUrlInfo &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  messages_manager_->get_login_url_info(DialogId(request.chat_id_), MessageId(request.message_id_), request.button_id_,
                                        std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getLoginUrl &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  messages_manager_->get_login_url(DialogId(request.chat_id_), MessageId(request.message_id_), request.button_id_,
                                   request.allow_write_access_, std::move(promise));
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
  CLEAN_INPUT_STRING(request.switch_pm_text_);
  CLEAN_INPUT_STRING(request.switch_pm_parameter_);
  CREATE_OK_REQUEST_PROMISE();
  inline_queries_manager_->answer_inline_query(
      request.inline_query_id_, request.is_personal_, std::move(request.results_), request.cache_time_,
      request.next_offset_, request.switch_pm_text_, request.switch_pm_parameter_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::getCallbackQueryAnswer &request) {
  CHECK_IS_USER();
  CREATE_REQUEST(GetCallbackQueryAnswerRequest, request.chat_id_, request.message_id_, std::move(request.payload_));
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
  answer_shipping_query(request.shipping_query_id_, std::move(request.shipping_options_), request.error_message_,
                        std::move(promise));
}

void Td::on_request(uint64 id, td_api::answerPreCheckoutQuery &request) {
  CHECK_IS_BOT();
  CLEAN_INPUT_STRING(request.error_message_);
  CREATE_OK_REQUEST_PROMISE();
  answer_pre_checkout_query(request.pre_checkout_query_id_, request.error_message_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::getBankCardInfo &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.bank_card_number_);
  CREATE_REQUEST_PROMISE();
  get_bank_card_info(request.bank_card_number_, std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getPaymentForm &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  messages_manager_->get_payment_form({DialogId(request.chat_id_), MessageId(request.message_id_)}, std::move(promise));
}

void Td::on_request(uint64 id, td_api::validateOrderInfo &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  messages_manager_->validate_order_info({DialogId(request.chat_id_), MessageId(request.message_id_)},
                                         std::move(request.order_info_), request.allow_save_, std::move(promise));
}

void Td::on_request(uint64 id, td_api::sendPaymentForm &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.order_info_id_);
  CLEAN_INPUT_STRING(request.shipping_option_id_);
  if (request.credentials_ == nullptr) {
    return send_error_raw(id, 400, "Input payments credentials must be non-empty");
  }
  CREATE_REQUEST_PROMISE();
  messages_manager_->send_payment_form({DialogId(request.chat_id_), MessageId(request.message_id_)},
                                       request.order_info_id_, request.shipping_option_id_, request.credentials_,
                                       std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getPaymentReceipt &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  messages_manager_->get_payment_receipt({DialogId(request.chat_id_), MessageId(request.message_id_)},
                                         std::move(promise));
}

void Td::on_request(uint64 id, const td_api::getSavedOrderInfo &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  get_saved_order_info(std::move(promise));
}

void Td::on_request(uint64 id, const td_api::deleteSavedOrderInfo &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  delete_saved_order_info(std::move(promise));
}

void Td::on_request(uint64 id, const td_api::deleteSavedCredentials &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  delete_saved_credentials(std::move(promise));
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
  UserId user_id(request.user_id_);
  auto input_user = contacts_manager_->get_input_user(user_id);
  if (input_user == nullptr) {
    return send_error_raw(id, 400, "User not found");
  }
  CREATE_OK_REQUEST_PROMISE();
  send_closure(secure_manager_, &SecureManager::set_secure_value_errors, this, std::move(input_user),
               std::move(request.errors_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::getPreferredCountryLanguage &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.country_code_);
  CREATE_REQUEST_PROMISE();
  send_closure(secure_manager_, &SecureManager::get_preferred_country_language, std::move(request.country_code_),
               std::move(promise));
}

void Td::on_request(uint64 id, td_api::sendPhoneNumberVerificationCode &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.phone_number_);
  send_closure(verify_phone_number_manager_, &PhoneNumberManager::set_phone_number, id,
               std::move(request.phone_number_), std::move(request.settings_));
}

void Td::on_request(uint64 id, const td_api::resendPhoneNumberVerificationCode &request) {
  CHECK_IS_USER();
  send_closure(verify_phone_number_manager_, &PhoneNumberManager::resend_authentication_code, id);
}

void Td::on_request(uint64 id, td_api::checkPhoneNumberVerificationCode &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.code_);
  send_closure(verify_phone_number_manager_, &PhoneNumberManager::check_code, id, std::move(request.code_));
}

void Td::on_request(uint64 id, td_api::sendEmailAddressVerificationCode &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.email_address_);
  CREATE_REQUEST_PROMISE();
  send_closure(password_manager_, &PasswordManager::send_email_address_verification_code, request.email_address_,
               std::move(promise));
}

void Td::on_request(uint64 id, const td_api::resendEmailAddressVerificationCode &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  send_closure(password_manager_, &PasswordManager::resend_email_address_verification_code, std::move(promise));
}

void Td::on_request(uint64 id, td_api::checkEmailAddressVerificationCode &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.code_);
  CREATE_OK_REQUEST_PROMISE();
  send_closure(password_manager_, &PasswordManager::check_email_address_verification_code, request.code_,
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
               request.autorization_form_id_, std::move(request.password_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::sendPassportAuthorizationForm &request) {
  CHECK_IS_USER();
  for (auto &type : request.types_) {
    if (type == nullptr) {
      return send_error_raw(id, 400, "Type must be non-empty");
    }
  }

  CREATE_OK_REQUEST_PROMISE();
  send_closure(secure_manager_, &SecureManager::send_passport_authorization_form, request.autorization_form_id_,
               get_secure_value_types_td_api(request.types_), std::move(promise));
}

void Td::on_request(uint64 id, td_api::sendPhoneNumberConfirmationCode &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.phone_number_);
  CLEAN_INPUT_STRING(request.hash_);
  send_closure(confirm_phone_number_manager_, &PhoneNumberManager::set_phone_number_and_hash, id,
               std::move(request.hash_), std::move(request.phone_number_), std::move(request.settings_));
}

void Td::on_request(uint64 id, const td_api::resendPhoneNumberConfirmationCode &request) {
  CHECK_IS_USER();
  send_closure(confirm_phone_number_manager_, &PhoneNumberManager::resend_authentication_code, id);
}

void Td::on_request(uint64 id, td_api::checkPhoneNumberConfirmationCode &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.code_);
  send_closure(confirm_phone_number_manager_, &PhoneNumberManager::check_code, id, std::move(request.code_));
}

void Td::on_request(uint64 id, const td_api::getSupportUser &request) {
  CHECK_IS_USER();
  CREATE_NO_ARGS_REQUEST(GetSupportUserRequest);
}

void Td::on_request(uint64 id, const td_api::getBackgrounds &request) {
  CHECK_IS_USER();
  CREATE_REQUEST(GetBackgroundsRequest, request.for_dark_theme_);
}

void Td::on_request(uint64 id, td_api::getBackgroundUrl &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.name_);
  Result<string> r_url = background_manager_->get_background_url(request.name_, std::move(request.type_));
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

void Td::on_request(uint64 id, td_api::setBackground &request) {
  CHECK_IS_USER();
  CREATE_REQUEST(SetBackgroundRequest, std::move(request.background_), std::move(request.type_),
                 request.for_dark_theme_);
}

void Td::on_request(uint64 id, const td_api::removeBackground &request) {
  CHECK_IS_USER();
  CREATE_OK_REQUEST_PROMISE();
  background_manager_->remove_background(BackgroundId(request.background_id_), std::move(promise));
}

void Td::on_request(uint64 id, const td_api::resetBackgrounds &request) {
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

void Td::on_request(uint64 id, td_api::acceptTermsOfService &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.terms_of_service_id_);
  auto promise = PromiseCreator::lambda([id = id, actor_id = actor_id(this)](Result<> result) {
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

void Td::on_request(uint64 id, const td_api::getInviteText &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  auto query_promise = PromiseCreator::lambda([promise = std::move(promise)](Result<string> result) mutable {
    if (result.is_error()) {
      promise.set_error(result.move_as_error());
    } else {
      promise.set_value(make_tl_object<td_api::text>(result.move_as_ok()));
    }
  });
  create_handler<GetInviteTextQuery>(std::move(query_promise))->send();
}

void Td::on_request(uint64 id, td_api::getDeepLinkInfo &request) {
  CLEAN_INPUT_STRING(request.link_);
  CREATE_REQUEST_PROMISE();
  create_handler<GetDeepLinkInfoQuery>(std::move(promise))->send(request.link_);
}

void Td::on_request(uint64 id, const td_api::getApplicationConfig &request) {
  CHECK_IS_USER();
  CREATE_REQUEST_PROMISE();
  send_closure(G()->config_manager(), &ConfigManager::get_app_config, std::move(promise));
}

void Td::on_request(uint64 id, td_api::saveApplicationLogEvent &request) {
  CHECK_IS_USER();
  CLEAN_INPUT_STRING(request.type_);
  auto result = convert_json_value(std::move(request.data_));
  CREATE_OK_REQUEST_PROMISE();
  create_handler<SaveAppLogQuery>(std::move(promise))->send(request.type_, request.chat_id_, std::move(result));
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

void Td::on_request(uint64 id, const td_api::getPushReceiverId &request) {
  UNREACHABLE();
}

void Td::on_request(uint64 id, const td_api::getChatFilterDefaultIconName &request) {
  UNREACHABLE();
}

void Td::on_request(uint64 id, const td_api::getJsonValue &request) {
  UNREACHABLE();
}

void Td::on_request(uint64 id, const td_api::getJsonString &request) {
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

td_api::object_ptr<td_api::Object> Td::do_static_request(const td_api::getTextEntities &request) {
  if (!check_utf8(request.text_)) {
    return make_error(400, "Text must be encoded in UTF-8");
  }
  auto text_entities = find_entities(request.text_, false);
  return make_tl_object<td_api::textEntities>(get_text_entities_object(text_entities));
}

td_api::object_ptr<td_api::Object> Td::do_static_request(td_api::parseTextEntities &request) {
  if (!check_utf8(request.text_)) {
    return make_error(400, "Text must be encoded in UTF-8");
  }
  if (request.parse_mode_ == nullptr) {
    return make_error(400, "Parse mode must be non-empty");
  }

  auto r_entities = [&]() -> Result<vector<MessageEntity>> {
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

  return make_tl_object<td_api::formattedText>(std::move(request.text_), get_text_entities_object(r_entities.ok()));
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
  auto status = fix_formatted_text(request.text_->text_, entities, true, true, true, true);
  if (status.is_error()) {
    return make_error(400, status.error().message());
  }

  auto parsed_text = parse_markdown_v3({std::move(request.text_->text_), std::move(entities)});
  fix_formatted_text(parsed_text.text, parsed_text.entities, true, true, true, true).ensure();
  return get_formatted_text_object(parsed_text);
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
  auto status = fix_formatted_text(request.text_->text_, entities, true, true, true, true);
  if (status.is_error()) {
    return make_error(400, status.error().message());
  }

  return get_formatted_text_object(get_markdown_v3({std::move(request.text_->text_), std::move(entities)}));
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

td_api::object_ptr<td_api::Object> Td::do_static_request(const td_api::getChatFilterDefaultIconName &request) {
  if (request.filter_ == nullptr) {
    return make_error(400, "Chat filter must be non-empty");
  }
  if (!check_utf8(request.filter_->title_)) {
    return make_error(400, "Chat filter title must be encoded in UTF-8");
  }
  if (!check_utf8(request.filter_->icon_name_)) {
    return make_error(400, "Chat filter icon name must be encoded in UTF-8");
  }
  return td_api::make_object<td_api::text>(DialogFilter::get_default_icon_name(request.filter_.get()));
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
  create_handler<TestQuery>(id)->send();
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

constexpr const char *Td::TDLIB_VERSION;

}  // namespace td
