//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/LinkManager.h"

#include "td/telegram/AccessRights.h"
#include "td/telegram/ChannelId.h"
#include "td/telegram/ChannelType.h"
#include "td/telegram/ConfigManager.h"
#include "td/telegram/ConfigShared.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/DialogParticipant.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/misc.h"
#include "td/telegram/ServerMessageId.h"
#include "td/telegram/Td.h"
#include "td/telegram/TdDb.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UserId.h"

#include "td/mtproto/ProxySecret.h"

#include "td/utils/algorithm.h"
#include "td/utils/base64.h"
#include "td/utils/buffer.h"
#include "td/utils/HttpUrl.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/Time.h"

#include <tuple>

namespace td {

static bool is_valid_start_parameter(Slice start_parameter) {
  return start_parameter.size() <= 64 && is_base64url_characters(start_parameter);
}

static bool is_valid_username(Slice username) {
  if (username.empty() || username.size() > 32) {
    return false;
  }
  if (!is_alpha(username[0])) {
    return false;
  }
  for (auto c : username) {
    if (!is_alpha(c) && !is_digit(c) && c != '_') {
      return false;
    }
  }
  if (username.back() == '_') {
    return false;
  }
  for (size_t i = 1; i < username.size(); i++) {
    if (username[i - 1] == '_' && username[i] == '_') {
      return false;
    }
  }

  return true;
}

static bool is_valid_phone_number(Slice phone_number) {
  if (phone_number.empty() || phone_number.size() > 32) {
    return false;
  }
  for (auto c : phone_number) {
    if (!is_digit(c)) {
      return false;
    }
  }
  return true;
}

static string get_url_query_hash(bool is_tg, const HttpUrlQuery &url_query) {
  const auto &path = url_query.path_;
  if (is_tg) {
    if (path.size() == 1 && path[0] == "join" && !url_query.get_arg("invite").empty()) {
      // join?invite=abcdef
      return url_query.get_arg("invite").str();
    }
  } else {
    if (path.size() >= 2 && path[0] == "joinchat" && !path[1].empty()) {
      // /joinchat/<link>
      return path[1];
    }
    if (!path.empty() && path[0].size() >= 2 && (path[0][0] == ' ' || path[0][0] == '+')) {
      if (is_valid_phone_number(Slice(path[0]).substr(1))) {
        return string();
      }
      // /+<link>
      return path[0].substr(1);
    }
  }
  return string();
}

static AdministratorRights get_administrator_rights(Slice rights, bool for_channel) {
  bool can_manage_dialog = false;
  bool can_change_info = false;
  bool can_post_messages = false;
  bool can_edit_messages = false;
  bool can_delete_messages = false;
  bool can_invite_users = false;
  bool can_restrict_members = false;
  bool can_pin_messages = false;
  bool can_promote_members = false;
  bool can_manage_calls = false;
  bool is_anonymous = false;
  for (auto right : full_split(rights, ' ')) {
    if (right == "change_info") {
      can_change_info = true;
    } else if (right == "post_messages") {
      can_post_messages = true;
    } else if (right == "edit_messages") {
      can_edit_messages = true;
    } else if (right == "delete_messages") {
      can_delete_messages = true;
    } else if (right == "restrict_members") {
      can_restrict_members = true;
    } else if (right == "invite_users") {
      can_invite_users = true;
    } else if (right == "pin_messages") {
      can_pin_messages = true;
    } else if (right == "promote_members") {
      can_promote_members = true;
    } else if (right == "manage_video_chats") {
      can_manage_calls = true;
    } else if (right == "anonymous") {
      is_anonymous = true;
    } else if (right == "manage_chat") {
      can_manage_dialog = true;
    }
  }
  return AdministratorRights(is_anonymous, can_manage_dialog, can_change_info, can_post_messages, can_edit_messages,
                             can_delete_messages, can_invite_users, can_restrict_members, can_pin_messages,
                             can_promote_members, can_manage_calls,
                             for_channel ? ChannelType::Broadcast : ChannelType::Megagroup);
}

class LinkManager::InternalLinkActiveSessions final : public InternalLink {
  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeActiveSessions>();
  }
};

class LinkManager::InternalLinkAttachMenuBot final : public InternalLink {
  unique_ptr<InternalLink> dialog_link_;
  string bot_username_;
  string url_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeAttachmentMenuBot>(
        dialog_link_ == nullptr ? nullptr : dialog_link_->get_internal_link_type_object(), bot_username_, url_);
  }

 public:
  InternalLinkAttachMenuBot(unique_ptr<InternalLink> dialog_link, string bot_username, Slice start_parameter)
      : dialog_link_(std::move(dialog_link)), bot_username_(std::move(bot_username)) {
    if (!start_parameter.empty()) {
      url_ = PSTRING() << "start://" << start_parameter;
    }
  }
};

class LinkManager::InternalLinkAuthenticationCode final : public InternalLink {
  string code_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeAuthenticationCode>(code_);
  }

 public:
  explicit InternalLinkAuthenticationCode(string code) : code_(std::move(code)) {
  }
};

class LinkManager::InternalLinkBackground final : public InternalLink {
  string background_name_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeBackground>(background_name_);
  }

 public:
  explicit InternalLinkBackground(string background_name) : background_name_(std::move(background_name)) {
  }
};

class LinkManager::InternalLinkBotAddToChannel final : public InternalLink {
  string bot_username_;
  AdministratorRights administrator_rights_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeBotAddToChannel>(
        bot_username_, administrator_rights_.get_chat_administrator_rights_object());
  }

 public:
  InternalLinkBotAddToChannel(string bot_username, AdministratorRights &&administrator_rights)
      : bot_username_(std::move(bot_username)), administrator_rights_(std::move(administrator_rights)) {
  }
};

class LinkManager::InternalLinkBotStart final : public InternalLink {
  string bot_username_;
  string start_parameter_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeBotStart>(bot_username_, start_parameter_);
  }

 public:
  InternalLinkBotStart(string bot_username, string start_parameter)
      : bot_username_(std::move(bot_username)), start_parameter_(std::move(start_parameter)) {
  }
};

class LinkManager::InternalLinkBotStartInGroup final : public InternalLink {
  string bot_username_;
  string start_parameter_;
  AdministratorRights administrator_rights_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeBotStartInGroup>(
        bot_username_, start_parameter_,
        administrator_rights_ == AdministratorRights() ? nullptr
                                                       : administrator_rights_.get_chat_administrator_rights_object());
  }

 public:
  InternalLinkBotStartInGroup(string bot_username, string start_parameter, AdministratorRights &&administrator_rights)
      : bot_username_(std::move(bot_username))
      , start_parameter_(std::move(start_parameter))
      , administrator_rights_(std::move(administrator_rights)) {
  }
};

class LinkManager::InternalLinkChangePhoneNumber final : public InternalLink {
  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeChangePhoneNumber>();
  }
};

class LinkManager::InternalLinkConfirmPhone final : public InternalLink {
  string hash_;
  string phone_number_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypePhoneNumberConfirmation>(hash_, phone_number_);
  }

 public:
  InternalLinkConfirmPhone(string hash, string phone_number)
      : hash_(std::move(hash)), phone_number_(std::move(phone_number)) {
  }
};

class LinkManager::InternalLinkDialogInvite final : public InternalLink {
  string url_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeChatInvite>(url_);
  }

 public:
  explicit InternalLinkDialogInvite(string url) : url_(std::move(url)) {
  }
};

class LinkManager::InternalLinkFilterSettings final : public InternalLink {
  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeFilterSettings>();
  }
};

class LinkManager::InternalLinkGame final : public InternalLink {
  string bot_username_;
  string game_short_name_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeGame>(bot_username_, game_short_name_);
  }

 public:
  InternalLinkGame(string bot_username, string game_short_name)
      : bot_username_(std::move(bot_username)), game_short_name_(std::move(game_short_name)) {
  }
};

class LinkManager::InternalLinkLanguage final : public InternalLink {
  string language_pack_id_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeLanguagePack>(language_pack_id_);
  }

 public:
  explicit InternalLinkLanguage(string language_pack_id) : language_pack_id_(std::move(language_pack_id)) {
  }
};

class LinkManager::InternalLinkLanguageSettings final : public InternalLink {
  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeLanguageSettings>();
  }
};

class LinkManager::InternalLinkMessage final : public InternalLink {
  string url_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeMessage>(url_);
  }

 public:
  explicit InternalLinkMessage(string url) : url_(std::move(url)) {
  }
};

class LinkManager::InternalLinkMessageDraft final : public InternalLink {
  FormattedText text_;
  bool contains_link_ = false;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeMessageDraft>(get_formatted_text_object(text_, true, -1),
                                                                     contains_link_);
  }

 public:
  InternalLinkMessageDraft(FormattedText &&text, bool contains_link)
      : text_(std::move(text)), contains_link_(contains_link) {
  }
};

class LinkManager::InternalLinkPassportDataRequest final : public InternalLink {
  UserId bot_user_id_;
  string scope_;
  string public_key_;
  string nonce_;
  string callback_url_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypePassportDataRequest>(bot_user_id_.get(), scope_, public_key_,
                                                                            nonce_, callback_url_);
  }

 public:
  InternalLinkPassportDataRequest(UserId bot_user_id, string scope, string public_key, string nonce,
                                  string callback_url)
      : bot_user_id_(bot_user_id)
      , scope_(std::move(scope))
      , public_key_(std::move(public_key))
      , nonce_(std::move(nonce))
      , callback_url_(std::move(callback_url)) {
  }
};

class LinkManager::InternalLinkPrivacyAndSecuritySettings final : public InternalLink {
  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypePrivacyAndSecuritySettings>();
  }
};

class LinkManager::InternalLinkProxy final : public InternalLink {
  string server_;
  int32 port_;
  td_api::object_ptr<td_api::ProxyType> type_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    CHECK(type_ != nullptr);
    auto type = type_.get();
    auto proxy_type = [type]() -> td_api::object_ptr<td_api::ProxyType> {
      switch (type->get_id()) {
        case td_api::proxyTypeSocks5::ID: {
          auto type_socks = static_cast<const td_api::proxyTypeSocks5 *>(type);
          return td_api::make_object<td_api::proxyTypeSocks5>(type_socks->username_, type_socks->password_);
        }
        case td_api::proxyTypeMtproto::ID: {
          auto type_mtproto = static_cast<const td_api::proxyTypeMtproto *>(type);
          return td_api::make_object<td_api::proxyTypeMtproto>(type_mtproto->secret_);
        }
        default:
          UNREACHABLE();
          return nullptr;
      }
    }();
    return td_api::make_object<td_api::internalLinkTypeProxy>(server_, port_, std::move(proxy_type));
  }

 public:
  InternalLinkProxy(string server, int32 port, td_api::object_ptr<td_api::ProxyType> type)
      : server_(std::move(server)), port_(port), type_(std::move(type)) {
  }
};

class LinkManager::InternalLinkPublicDialog final : public InternalLink {
  string dialog_username_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypePublicChat>(dialog_username_);
  }

 public:
  explicit InternalLinkPublicDialog(string dialog_username) : dialog_username_(std::move(dialog_username)) {
  }
};

class LinkManager::InternalLinkQrCodeAuthentication final : public InternalLink {
  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeQrCodeAuthentication>();
  }
};

class LinkManager::InternalLinkSettings final : public InternalLink {
  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeSettings>();
  }
};

class LinkManager::InternalLinkStickerSet final : public InternalLink {
  string sticker_set_name_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeStickerSet>(sticker_set_name_);
  }

 public:
  explicit InternalLinkStickerSet(string sticker_set_name) : sticker_set_name_(std::move(sticker_set_name)) {
  }
};

class LinkManager::InternalLinkTheme final : public InternalLink {
  string theme_name_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeTheme>(theme_name_);
  }

 public:
  explicit InternalLinkTheme(string theme_name) : theme_name_(std::move(theme_name)) {
  }
};

class LinkManager::InternalLinkThemeSettings final : public InternalLink {
  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeThemeSettings>();
  }
};

class LinkManager::InternalLinkUnknownDeepLink final : public InternalLink {
  string link_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeUnknownDeepLink>(link_);
  }

 public:
  explicit InternalLinkUnknownDeepLink(string link) : link_(std::move(link)) {
  }
};

class LinkManager::InternalLinkUnsupportedProxy final : public InternalLink {
  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeUnsupportedProxy>();
  }
};

class LinkManager::InternalLinkUserPhoneNumber final : public InternalLink {
  string phone_number_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeUserPhoneNumber>(phone_number_);
  }

 public:
  explicit InternalLinkUserPhoneNumber(string phone_number) : phone_number_(std::move(phone_number)) {
  }
};

class LinkManager::InternalLinkVoiceChat final : public InternalLink {
  string dialog_username_;
  string invite_hash_;
  bool is_live_stream_;

  td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const final {
    return td_api::make_object<td_api::internalLinkTypeVideoChat>(dialog_username_, invite_hash_, is_live_stream_);
  }

 public:
  InternalLinkVoiceChat(string dialog_username, string invite_hash, bool is_live_stream)
      : dialog_username_(std::move(dialog_username))
      , invite_hash_(std::move(invite_hash))
      , is_live_stream_(is_live_stream) {
  }
};

class GetDeepLinkInfoQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::deepLinkInfo>> promise_;

 public:
  explicit GetDeepLinkInfoQuery(Promise<td_api::object_ptr<td_api::deepLinkInfo>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(Slice link) {
    send_query(G()->net_query_creator().create_unauth(telegram_api::help_getDeepLinkInfo(link.str())));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::help_getDeepLinkInfo>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    switch (result->get_id()) {
      case telegram_api::help_deepLinkInfoEmpty::ID:
        return promise_.set_value(nullptr);
      case telegram_api::help_deepLinkInfo::ID: {
        auto info = telegram_api::move_object_as<telegram_api::help_deepLinkInfo>(result);
        auto entities = get_message_entities(nullptr, std::move(info->entities_), "GetDeepLinkInfoQuery");
        auto status = fix_formatted_text(info->message_, entities, true, true, true, true, true);
        if (status.is_error()) {
          LOG(ERROR) << "Receive error " << status << " while parsing deep link info " << info->message_;
          if (!clean_input_string(info->message_)) {
            info->message_.clear();
          }
          entities = find_entities(info->message_, true, true);
        }
        FormattedText text{std::move(info->message_), std::move(entities)};
        return promise_.set_value(
            td_api::make_object<td_api::deepLinkInfo>(get_formatted_text_object(text, true, -1), info->update_app_));
      }
      default:
        UNREACHABLE();
    }
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class RequestUrlAuthQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::LoginUrlInfo>> promise_;
  string url_;
  DialogId dialog_id_;

 public:
  explicit RequestUrlAuthQuery(Promise<td_api::object_ptr<td_api::LoginUrlInfo>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(string url, FullMessageId full_message_id, int32 button_id) {
    url_ = std::move(url);
    int32 flags = 0;
    tl_object_ptr<telegram_api::InputPeer> input_peer;
    if (full_message_id.get_dialog_id().is_valid()) {
      dialog_id_ = full_message_id.get_dialog_id();
      input_peer = td_->messages_manager_->get_input_peer(dialog_id_, AccessRights::Read);
      CHECK(input_peer != nullptr);
      flags |= telegram_api::messages_requestUrlAuth::PEER_MASK;
    } else {
      flags |= telegram_api::messages_requestUrlAuth::URL_MASK;
    }
    send_query(G()->net_query_creator().create(telegram_api::messages_requestUrlAuth(
        flags, std::move(input_peer), full_message_id.get_message_id().get_server_message_id().get(), button_id,
        url_)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_requestUrlAuth>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for RequestUrlAuthQuery: " << to_string(result);
    switch (result->get_id()) {
      case telegram_api::urlAuthResultRequest::ID: {
        auto request = telegram_api::move_object_as<telegram_api::urlAuthResultRequest>(result);
        UserId bot_user_id = ContactsManager::get_user_id(request->bot_);
        if (!bot_user_id.is_valid()) {
          return on_error(Status::Error(500, "Receive invalid bot_user_id"));
        }
        td_->contacts_manager_->on_get_user(std::move(request->bot_), "RequestUrlAuthQuery");
        promise_.set_value(td_api::make_object<td_api::loginUrlInfoRequestConfirmation>(
            url_, request->domain_, td_->contacts_manager_->get_user_id_object(bot_user_id, "RequestUrlAuthQuery"),
            request->request_write_access_));
        break;
      }
      case telegram_api::urlAuthResultAccepted::ID: {
        auto accepted = telegram_api::move_object_as<telegram_api::urlAuthResultAccepted>(result);
        promise_.set_value(td_api::make_object<td_api::loginUrlInfoOpen>(accepted->url_, true));
        break;
      }
      case telegram_api::urlAuthResultDefault::ID:
        promise_.set_value(td_api::make_object<td_api::loginUrlInfoOpen>(url_, false));
        break;
    }
  }

  void on_error(Status status) final {
    if (!dialog_id_.is_valid() ||
        !td_->messages_manager_->on_get_dialog_error(dialog_id_, status, "RequestUrlAuthQuery")) {
      LOG(INFO) << "Receive error for RequestUrlAuthQuery: " << status;
    }
    promise_.set_value(td_api::make_object<td_api::loginUrlInfoOpen>(url_, false));
  }
};

class AcceptUrlAuthQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::httpUrl>> promise_;
  string url_;
  DialogId dialog_id_;

 public:
  explicit AcceptUrlAuthQuery(Promise<td_api::object_ptr<td_api::httpUrl>> &&promise) : promise_(std::move(promise)) {
  }

  void send(string url, FullMessageId full_message_id, int32 button_id, bool allow_write_access) {
    url_ = std::move(url);
    int32 flags = 0;
    tl_object_ptr<telegram_api::InputPeer> input_peer;
    if (full_message_id.get_dialog_id().is_valid()) {
      dialog_id_ = full_message_id.get_dialog_id();
      input_peer = td_->messages_manager_->get_input_peer(dialog_id_, AccessRights::Read);
      CHECK(input_peer != nullptr);
      flags |= telegram_api::messages_acceptUrlAuth::PEER_MASK;
    } else {
      flags |= telegram_api::messages_acceptUrlAuth::URL_MASK;
    }
    if (allow_write_access) {
      flags |= telegram_api::messages_acceptUrlAuth::WRITE_ALLOWED_MASK;
    }
    send_query(G()->net_query_creator().create(telegram_api::messages_acceptUrlAuth(
        flags, false /*ignored*/, std::move(input_peer), full_message_id.get_message_id().get_server_message_id().get(),
        button_id, url_)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_acceptUrlAuth>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive " << to_string(result);
    switch (result->get_id()) {
      case telegram_api::urlAuthResultRequest::ID:
        LOG(ERROR) << "Receive unexpected " << to_string(result);
        return on_error(Status::Error(500, "Receive unexpected urlAuthResultRequest"));
      case telegram_api::urlAuthResultAccepted::ID: {
        auto accepted = telegram_api::move_object_as<telegram_api::urlAuthResultAccepted>(result);
        promise_.set_value(td_api::make_object<td_api::httpUrl>(accepted->url_));
        break;
      }
      case telegram_api::urlAuthResultDefault::ID:
        promise_.set_value(td_api::make_object<td_api::httpUrl>(url_));
        break;
    }
  }

  void on_error(Status status) final {
    if (!dialog_id_.is_valid() ||
        !td_->messages_manager_->on_get_dialog_error(dialog_id_, status, "AcceptUrlAuthQuery")) {
      LOG(INFO) << "Receive error for AcceptUrlAuthQuery: " << status;
    }
    promise_.set_error(std::move(status));
  }
};

LinkManager::LinkManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

LinkManager::~LinkManager() = default;

void LinkManager::start_up() {
  autologin_update_time_ = Time::now() - 365 * 86400;
  autologin_domains_ = full_split(G()->td_db()->get_binlog_pmc()->get("autologin_domains"), '\xFF');

  url_auth_domains_ = full_split(G()->td_db()->get_binlog_pmc()->get("url_auth_domains"), '\xFF');
}

void LinkManager::tear_down() {
  parent_.reset();
}

static bool tolower_begins_with(Slice str, Slice prefix) {
  if (prefix.size() > str.size()) {
    return false;
  }
  for (size_t i = 0; i < prefix.size(); i++) {
    if (to_lower(str[i]) != prefix[i]) {
      return false;
    }
  }
  return true;
}

Result<string> LinkManager::check_link(Slice link, bool http_only, bool https_only) {
  bool is_tg = false;
  bool is_ton = false;
  if (tolower_begins_with(link, "tg:")) {
    link.remove_prefix(3);
    is_tg = true;
  } else if (tolower_begins_with(link, "ton:")) {
    link.remove_prefix(4);
    is_ton = true;
  }
  if ((is_tg || is_ton) && begins_with(link, "//")) {
    link.remove_prefix(2);
  }
  TRY_RESULT(http_url, parse_url(link));
  if (https_only && (http_url.protocol_ != HttpUrl::Protocol::Https || is_tg || is_ton)) {
    return Status::Error("Only HTTPS links are allowed");
  }
  if (is_tg || is_ton) {
    if (http_only) {
      return Status::Error("Only HTTP links are allowed");
    }
    if (tolower_begins_with(link, "http://") || http_url.protocol_ == HttpUrl::Protocol::Https ||
        !http_url.userinfo_.empty() || http_url.specified_port_ != 0 || http_url.is_ipv6_) {
      return Status::Error(is_tg ? Slice("Wrong tg URL") : Slice("Wrong ton URL"));
    }

    Slice query(http_url.query_);
    CHECK(query[0] == '/');
    if (query.size() > 1 && query[1] == '?') {
      query.remove_prefix(1);
    }
    for (auto c : http_url.host_) {
      if (!is_alnum(c) && c != '-' && c != '_') {
        return Status::Error("Unallowed characters in URL host");
      }
    }
    return PSTRING() << (is_tg ? "tg" : "ton") << "://" << http_url.host_ << query;
  }

  if (http_url.host_.find('.') == string::npos && !http_url.is_ipv6_) {
    return Status::Error("Wrong HTTP URL");
  }
  return http_url.get_url();
}

LinkManager::LinkInfo LinkManager::get_link_info(Slice link) {
  LinkInfo result;
  if (link.empty()) {
    return result;
  }
  link.truncate(link.find('#'));

  bool is_tg = false;
  if (tolower_begins_with(link, "tg:")) {
    link.remove_prefix(3);
    if (begins_with(link, "//")) {
      link.remove_prefix(2);
    }
    is_tg = true;
  }

  auto r_http_url = parse_url(link);
  if (r_http_url.is_error()) {
    return result;
  }
  auto http_url = r_http_url.move_as_ok();

  if (!http_url.userinfo_.empty() || http_url.is_ipv6_) {
    return result;
  }

  if (is_tg) {
    if (tolower_begins_with(link, "http://") || http_url.protocol_ == HttpUrl::Protocol::Https ||
        http_url.specified_port_ != 0) {
      return result;
    }

    result.is_internal_ = true;
    result.is_tg_ = true;
    result.query_ = link.str();
    return result;
  } else {
    if (http_url.port_ != 80 && http_url.port_ != 443) {
      return result;
    }

    vector<Slice> t_me_urls{Slice("t.me"), Slice("telegram.me"), Slice("telegram.dog")};
    if (Scheduler::context() != nullptr) {  // for tests only
      string cur_t_me_url = G()->shared_config().get_option_string("t_me_url");
      if (tolower_begins_with(cur_t_me_url, "http://") || tolower_begins_with(cur_t_me_url, "https://")) {
        Slice t_me_url = cur_t_me_url;
        t_me_url = t_me_url.substr(t_me_url[4] == 's' ? 8 : 7);
        if (!td::contains(t_me_urls, t_me_url)) {
          t_me_urls.push_back(t_me_url);
        }
      }
    }

    auto host = url_decode(http_url.host_, false);
    to_lower_inplace(host);
    if (begins_with(host, "www.")) {
      host = host.substr(4);
    }

    for (auto t_me_url : t_me_urls) {
      if (host == t_me_url) {
        result.is_internal_ = true;
        result.is_tg_ = false;

        Slice query = http_url.query_;
        while (true) {
          if (begins_with(query, "/s/")) {
            query.remove_prefix(2);
            continue;
          }
          if (begins_with(query, "/%73/")) {
            query.remove_prefix(4);
            continue;
          }
          break;
        }
        result.query_ = query.str();
        return result;
      }
    }
  }
  return result;
}

unique_ptr<LinkManager::InternalLink> LinkManager::parse_internal_link(Slice link) {
  auto info = get_link_info(link);
  if (!info.is_internal_) {
    return nullptr;
  }
  if (info.is_tg_) {
    return parse_tg_link_query(info.query_);
  } else {
    return parse_t_me_link_query(info.query_);
  }
}

namespace {
struct CopyArg {
  Slice name_;
  const HttpUrlQuery *url_query_;
  bool *is_first_;

  CopyArg(Slice name, const HttpUrlQuery *url_query, bool *is_first)
      : name_(name), url_query_(url_query), is_first_(is_first) {
  }
};

StringBuilder &operator<<(StringBuilder &string_builder, const CopyArg &copy_arg) {
  auto arg = copy_arg.url_query_->get_arg(copy_arg.name_);
  if (arg.empty()) {
    for (const auto &query_arg : copy_arg.url_query_->args_) {
      if (query_arg.first == copy_arg.name_) {
        char c = *copy_arg.is_first_ ? '?' : '&';
        *copy_arg.is_first_ = false;
        return string_builder << c << copy_arg.name_;
      }
    }
    return string_builder;
  }
  char c = *copy_arg.is_first_ ? '?' : '&';
  *copy_arg.is_first_ = false;
  return string_builder << c << copy_arg.name_ << '=' << url_encode(arg);
}
}  // namespace

unique_ptr<LinkManager::InternalLink> LinkManager::parse_tg_link_query(Slice query) {
  const auto url_query = parse_url_query(query);
  const auto &path = url_query.path_;

  bool is_first_arg = true;
  auto copy_arg = [&](Slice name) {
    return CopyArg(name, &url_query, &is_first_arg);
  };
  auto pass_arg = [&](Slice name) {
    return url_encode(url_query.get_arg(name));
  };
  auto get_arg = [&](Slice name) {
    return url_query.get_arg(name).str();
  };
  auto has_arg = [&](Slice name) {
    return !url_query.get_arg(name).empty();
  };

  if (path.size() == 1 && path[0] == "resolve") {
    if (is_valid_username(get_arg("domain"))) {
      if (has_arg("post")) {
        // resolve?domain=<username>&post=12345&single&thread=<thread_id>&comment=<message_id>&t=<media_timestamp>
        return td::make_unique<InternalLinkMessage>(PSTRING() << "tg:resolve" << copy_arg("domain") << copy_arg("post")
                                                              << copy_arg("single") << copy_arg("thread")
                                                              << copy_arg("comment") << copy_arg("t"));
      }
      auto username = get_arg("domain");
      for (auto &arg : url_query.args_) {
        if (arg.first == "voicechat" || arg.first == "videochat" || arg.first == "livestream") {
          // resolve?domain=<username>&videochat
          // resolve?domain=<username>&videochat=<invite_hash>
          if (Scheduler::context() != nullptr) {
            send_closure(G()->messages_manager(), &MessagesManager::reload_voice_chat_on_search, username);
          }
          return td::make_unique<InternalLinkVoiceChat>(std::move(username), arg.second, arg.first == "livestream");
        }
        if (arg.first == "start" && is_valid_start_parameter(arg.second)) {
          // resolve?domain=<bot_username>&start=<parameter>
          return td::make_unique<InternalLinkBotStart>(std::move(username), arg.second);
        }
        if (arg.first == "startgroup" && is_valid_start_parameter(arg.second)) {
          // resolve?domain=<bot_username>&startgroup=<parameter>
          // resolve?domain=<bot_username>&startgroup=>parameter>&admin=change_info+delete_messages+restrict_members
          // resolve?domain=<bot_username>&startgroup&admin=change_info+delete_messages+restrict_members
          auto administrator_rights = get_administrator_rights(url_query.get_arg("admin"), false);
          return td::make_unique<InternalLinkBotStartInGroup>(std::move(username), arg.second,
                                                              std::move(administrator_rights));
        }
        if (arg.first == "startchannel") {
          // resolve?domain=<bot_username>&startchannel&admin=change_info+post_messages+promote_members
          auto administrator_rights = get_administrator_rights(url_query.get_arg("admin"), true);
          if (administrator_rights != AdministratorRights()) {
            return td::make_unique<InternalLinkBotAddToChannel>(std::move(username), std::move(administrator_rights));
          }
        }
        if (arg.first == "game" && !arg.second.empty()) {
          // resolve?domain=<bot_username>&game=<short_name>
          return td::make_unique<InternalLinkGame>(std::move(username), arg.second);
        }
      }
      if (!url_query.get_arg("attach").empty()) {
        // resolve?domain=<username>&attach=<bot_username>
        // resolve?domain=<username>&attach=<bot_username>&startattach=<start_parameter>
        return td::make_unique<InternalLinkAttachMenuBot>(
            td::make_unique<InternalLinkPublicDialog>(std::move(username)), url_query.get_arg("attach").str(),
            url_query.get_arg("startattach"));
      } else if (url_query.has_arg("startattach")) {
        // resolve?domain=<bot_username>&startattach
        // resolve?domain=<bot_username>&startattach=<start_parameter>
        return td::make_unique<InternalLinkAttachMenuBot>(nullptr, std::move(username),
                                                          url_query.get_arg("startattach"));
      }
      if (username == "telegrampassport") {
        // resolve?domain=telegrampassport&bot_id=<bot_user_id>&scope=<scope>&public_key=<public_key>&nonce=<nonce>
        return get_internal_link_passport(query, url_query.args_);
      }
      // resolve?domain=<username>
      return td::make_unique<InternalLinkPublicDialog>(std::move(username));
    } else if (is_valid_phone_number(get_arg("phone"))) {
      auto user_link = td::make_unique<InternalLinkUserPhoneNumber>(get_arg("phone"));
      if (!url_query.get_arg("attach").empty()) {
        // resolve?phone=<phone_number>&attach=<bot_username>
        // resolve?phone=<phone_number>&attach=<bot_username>&startattach=<start_parameter>
        return td::make_unique<InternalLinkAttachMenuBot>(std::move(user_link), url_query.get_arg("attach").str(),
                                                          url_query.get_arg("startattach"));
      }
      // resolve?phone=12345
      return user_link;
    }
  } else if (path.size() == 1 && path[0] == "login") {
    // login?code=123456
    if (has_arg("code")) {
      return td::make_unique<InternalLinkAuthenticationCode>(get_arg("code"));
    }
    // login?token=<token>
    if (has_arg("token")) {
      return td::make_unique<InternalLinkQrCodeAuthentication>();
    }
  } else if (path.size() == 1 && path[0] == "passport") {
    // passport?bot_id=<bot_user_id>&scope=<scope>&public_key=<public_key>&nonce=<nonce>
    return get_internal_link_passport(query, url_query.args_);
  } else if (!path.empty() && path[0] == "settings") {
    if (path.size() == 2 && path[1] == "change_number") {
      // settings/change_number
      return td::make_unique<InternalLinkChangePhoneNumber>();
    }
    if (path.size() == 2 && path[1] == "devices") {
      // settings/devices
      return td::make_unique<InternalLinkActiveSessions>();
    }
    if (path.size() == 2 && path[1] == "folders") {
      // settings/folders
      return td::make_unique<InternalLinkFilterSettings>();
    }
    if (path.size() == 2 && path[1] == "language") {
      // settings/language
      return td::make_unique<InternalLinkLanguageSettings>();
    }
    if (path.size() == 2 && path[1] == "privacy") {
      // settings/privacy
      return td::make_unique<InternalLinkPrivacyAndSecuritySettings>();
    }
    if (path.size() == 2 && path[1] == "themes") {
      // settings/themes
      return td::make_unique<InternalLinkThemeSettings>();
    }
    // settings
    return td::make_unique<InternalLinkSettings>();
  } else if (path.size() == 1 && path[0] == "join") {
    // join?invite=<hash>
    if (has_arg("invite")) {
      return td::make_unique<InternalLinkDialogInvite>(PSTRING() << "tg:join?invite="
                                                                 << url_encode(get_url_query_hash(true, url_query)));
    }
  } else if (path.size() == 1 && path[0] == "addstickers") {
    // addstickers?set=<name>
    if (has_arg("set")) {
      return td::make_unique<InternalLinkStickerSet>(get_arg("set"));
    }
  } else if (path.size() == 1 && path[0] == "setlanguage") {
    // setlanguage?lang=<name>
    if (has_arg("lang")) {
      return td::make_unique<InternalLinkLanguage>(get_arg("lang"));
    }
  } else if (path.size() == 1 && path[0] == "addtheme") {
    // addtheme?slug=<name>
    if (has_arg("slug")) {
      return td::make_unique<InternalLinkTheme>(get_arg("slug"));
    }
  } else if (path.size() == 1 && path[0] == "confirmphone") {
    if (has_arg("hash") && has_arg("phone")) {
      // confirmphone?phone=<phone>&hash=<hash>
      return td::make_unique<InternalLinkConfirmPhone>(get_arg("hash"), get_arg("phone"));
    }
  } else if (path.size() == 1 && path[0] == "socks") {
    if (has_arg("server") && has_arg("port")) {
      // socks?server=<server>&port=<port>&user=<user>&pass=<pass>
      auto port = to_integer<int32>(get_arg("port"));
      if (0 < port && port < 65536) {
        return td::make_unique<InternalLinkProxy>(
            get_arg("server"), port, td_api::make_object<td_api::proxyTypeSocks5>(get_arg("user"), get_arg("pass")));
      } else {
        return td::make_unique<InternalLinkUnsupportedProxy>();
      }
    }
  } else if (path.size() == 1 && path[0] == "proxy") {
    if (has_arg("server") && has_arg("port")) {
      // proxy?server=<server>&port=<port>&secret=<secret>
      auto port = to_integer<int32>(get_arg("port"));
      if (0 < port && port < 65536 && mtproto::ProxySecret::from_link(get_arg("secret")).is_ok()) {
        return td::make_unique<InternalLinkProxy>(get_arg("server"), port,
                                                  td_api::make_object<td_api::proxyTypeMtproto>(get_arg("secret")));
      } else {
        return td::make_unique<InternalLinkUnsupportedProxy>();
      }
    }
  } else if (path.size() == 1 && path[0] == "privatepost") {
    // privatepost?channel=123456789&post=12345&single&thread=<thread_id>&comment=<message_id>&t=<media_timestamp>
    if (has_arg("channel") && has_arg("post")) {
      return td::make_unique<InternalLinkMessage>(
          PSTRING() << "tg:privatepost" << copy_arg("channel") << copy_arg("post") << copy_arg("single")
                    << copy_arg("thread") << copy_arg("comment") << copy_arg("t"));
    }
  } else if (path.size() == 1 && path[0] == "bg") {
    // bg?color=<color>
    // bg?gradient=<hex_color>-<hex_color>&rotation=...
    // bg?gradient=<hex_color>~<hex_color>~<hex_color>~<hex_color>
    // bg?slug=<background_name>&mode=blur+motion
    // bg?slug=<pattern_name>&intensity=...&bg_color=...&mode=blur+motion
    if (has_arg("color")) {
      return td::make_unique<InternalLinkBackground>(pass_arg("color"));
    }
    if (has_arg("gradient")) {
      return td::make_unique<InternalLinkBackground>(PSTRING() << pass_arg("gradient") << copy_arg("rotation"));
    }
    if (has_arg("slug")) {
      return td::make_unique<InternalLinkBackground>(PSTRING()
                                                     << pass_arg("slug") << copy_arg("mode") << copy_arg("intensity")
                                                     << copy_arg("bg_color") << copy_arg("rotation"));
    }
  } else if (path.size() == 1 && (path[0] == "share" || path[0] == "msg" || path[0] == "msg_url")) {
    // msg_url?url=<url>
    // msg_url?url=<url>&text=<text>
    return get_internal_link_message_draft(get_arg("url"), get_arg("text"));
  }
  if (!path.empty() && !path[0].empty()) {
    return td::make_unique<InternalLinkUnknownDeepLink>(PSTRING() << "tg://" << query);
  }
  return nullptr;
}

unique_ptr<LinkManager::InternalLink> LinkManager::parse_t_me_link_query(Slice query) {
  CHECK(query[0] == '/');
  const auto url_query = parse_url_query(query);
  const auto &path = url_query.path_;
  if (path.empty() || path[0].empty()) {
    return nullptr;
  }

  bool is_first_arg = true;
  auto copy_arg = [&](Slice name) {
    return CopyArg(name, &url_query, &is_first_arg);
  };

  auto get_arg = [&](Slice name) {
    return url_query.get_arg(name).str();
  };
  auto has_arg = [&](Slice name) {
    return !url_query.get_arg(name).empty();
  };

  if (path[0] == "c") {
    if (path.size() >= 3 && to_integer<int64>(path[1]) > 0 && to_integer<int64>(path[2]) > 0) {
      // /c/123456789/12345?single&thread=<thread_id>&comment=<message_id>&t=<media_timestamp>
      is_first_arg = false;
      return td::make_unique<InternalLinkMessage>(
          PSTRING() << "tg:privatepost?channel=" << to_integer<int64>(path[1]) << "&post=" << to_integer<int64>(path[2])
                    << copy_arg("single") << copy_arg("thread") << copy_arg("comment") << copy_arg("t"));
    }
  } else if (path[0] == "login") {
    if (path.size() >= 2 && !path[1].empty()) {
      // /login/<code>
      return td::make_unique<InternalLinkAuthenticationCode>(path[1]);
    }
  } else if (path[0] == "joinchat") {
    if (path.size() >= 2 && !path[1].empty()) {
      // /joinchat/<link>
      return td::make_unique<InternalLinkDialogInvite>(PSTRING() << "tg:join?invite="
                                                                 << url_encode(get_url_query_hash(false, url_query)));
    }
  } else if (path[0][0] == ' ' || path[0][0] == '+') {
    if (path[0].size() >= 2) {
      if (is_valid_phone_number(Slice(path[0]).substr(1))) {
        auto user_link = td::make_unique<InternalLinkUserPhoneNumber>(path[0].substr(1));
        if (!url_query.get_arg("attach").empty()) {
          // /+<phone_number>?attach=<bot_username>
          // /+<phone_number>?attach=<bot_username>&startattach=<start_parameter>
          return td::make_unique<InternalLinkAttachMenuBot>(std::move(user_link), url_query.get_arg("attach").str(),
                                                            url_query.get_arg("startattach"));
        }
        // /+<phone_number>
        return user_link;
      } else {
        // /+<link>
        return td::make_unique<InternalLinkDialogInvite>(PSTRING() << "tg:join?invite="
                                                                   << url_encode(get_url_query_hash(false, url_query)));
      }
    }
  } else if (path[0] == "addstickers") {
    if (path.size() >= 2 && !path[1].empty()) {
      // /addstickers/<name>
      return td::make_unique<InternalLinkStickerSet>(path[1]);
    }
  } else if (path[0] == "setlanguage") {
    if (path.size() >= 2 && !path[1].empty()) {
      // /setlanguage/<name>
      return td::make_unique<InternalLinkLanguage>(path[1]);
    }
  } else if (path[0] == "addtheme") {
    if (path.size() >= 2 && !path[1].empty()) {
      // /addtheme/<name>
      return td::make_unique<InternalLinkTheme>(path[1]);
    }
  } else if (path[0] == "confirmphone") {
    if (has_arg("hash") && has_arg("phone")) {
      // /confirmphone?phone=<phone>&hash=<hash>
      return td::make_unique<InternalLinkConfirmPhone>(get_arg("hash"), get_arg("phone"));
    }
  } else if (path[0] == "socks") {
    if (has_arg("server") && has_arg("port")) {
      // /socks?server=<server>&port=<port>&user=<user>&pass=<pass>
      auto port = to_integer<int32>(get_arg("port"));
      if (0 < port && port < 65536) {
        return td::make_unique<InternalLinkProxy>(
            get_arg("server"), port, td_api::make_object<td_api::proxyTypeSocks5>(get_arg("user"), get_arg("pass")));
      } else {
        return td::make_unique<InternalLinkUnsupportedProxy>();
      }
    }
  } else if (path[0] == "proxy") {
    if (has_arg("server") && has_arg("port")) {
      // /proxy?server=<server>&port=<port>&secret=<secret>
      auto port = to_integer<int32>(get_arg("port"));
      if (0 < port && port < 65536 && mtproto::ProxySecret::from_link(get_arg("secret")).is_ok()) {
        return td::make_unique<InternalLinkProxy>(get_arg("server"), port,
                                                  td_api::make_object<td_api::proxyTypeMtproto>(get_arg("secret")));
      } else {
        return td::make_unique<InternalLinkUnsupportedProxy>();
      }
    }
  } else if (path[0] == "bg") {
    if (path.size() >= 2 && !path[1].empty()) {
      // /bg/<hex_color>
      // /bg/<hex_color>-<hex_color>?rotation=...
      // /bg/<hex_color>~<hex_color>~<hex_color>~<hex_color>
      // /bg/<background_name>?mode=blur+motion
      // /bg/<pattern_name>?intensity=...&bg_color=...&mode=blur+motion
      return td::make_unique<InternalLinkBackground>(PSTRING()
                                                     << url_encode(path[1]) << copy_arg("mode") << copy_arg("intensity")
                                                     << copy_arg("bg_color") << copy_arg("rotation"));
    }
  } else if (path[0] == "share" || path[0] == "msg") {
    if (!(path.size() > 1 && (path[1] == "bookmarklet" || path[1] == "embed"))) {
      // /share?url=<url>
      // /share/url?url=<url>&text=<text>
      return get_internal_link_message_draft(get_arg("url"), get_arg("text"));
    }
  } else if (is_valid_username(path[0])) {
    if (path.size() >= 2 && to_integer<int64>(path[1]) > 0) {
      // /<username>/12345?single&thread=<thread_id>&comment=<message_id>&t=<media_timestamp>
      is_first_arg = false;
      return td::make_unique<InternalLinkMessage>(
          PSTRING() << "tg:resolve?domain=" << url_encode(path[0]) << "&post=" << to_integer<int64>(path[1])
                    << copy_arg("single") << copy_arg("thread") << copy_arg("comment") << copy_arg("t"));
    }
    auto username = path[0];
    for (auto &arg : url_query.args_) {
      if (arg.first == "voicechat" || arg.first == "videochat" || arg.first == "livestream") {
        // /<username>?videochat
        // /<username>?videochat=<invite_hash>
        if (Scheduler::context() != nullptr) {
          send_closure(G()->messages_manager(), &MessagesManager::reload_voice_chat_on_search, username);
        }
        return td::make_unique<InternalLinkVoiceChat>(std::move(username), arg.second, arg.first == "livestream");
      }
      if (arg.first == "start" && is_valid_start_parameter(arg.second)) {
        // /<bot_username>?start=<parameter>
        return td::make_unique<InternalLinkBotStart>(std::move(username), arg.second);
      }
      if (arg.first == "startgroup" && is_valid_start_parameter(arg.second)) {
        // /<bot_username>?startgroup=<parameter>
        // /<bot_username>?startgroup=<parameter>&admin=change_info+delete_messages+restrict_members
        // /<bot_username>?startgroup&admin=change_info+delete_messages+restrict_members
        auto administrator_rights = get_administrator_rights(url_query.get_arg("admin"), false);
        return td::make_unique<InternalLinkBotStartInGroup>(std::move(username), arg.second,
                                                            std::move(administrator_rights));
      }
      if (arg.first == "startchannel") {
        // /<bot_username>?startchannel&admin=change_info+post_messages+promote_members
        auto administrator_rights = get_administrator_rights(url_query.get_arg("admin"), true);
        if (administrator_rights != AdministratorRights()) {
          return td::make_unique<InternalLinkBotAddToChannel>(std::move(username), std::move(administrator_rights));
        }
      }
      if (arg.first == "game" && !arg.second.empty()) {
        // /<bot_username>?game=<short_name>
        return td::make_unique<InternalLinkGame>(std::move(username), arg.second);
      }
    }
    if (!url_query.get_arg("attach").empty()) {
      // /<username>?attach=<bot_username>
      // /<username>?attach=<bot_username>&startattach=<start_parameter>
      return td::make_unique<InternalLinkAttachMenuBot>(td::make_unique<InternalLinkPublicDialog>(std::move(username)),
                                                        url_query.get_arg("attach").str(),
                                                        url_query.get_arg("startattach"));
    } else if (url_query.has_arg("startattach")) {
      // /<bot_username>?startattach
      // /<bot_username>?startattach=<start_parameter>
      return td::make_unique<InternalLinkAttachMenuBot>(nullptr, std::move(username), url_query.get_arg("startattach"));
    }

    // /<username>
    return td::make_unique<InternalLinkPublicDialog>(std::move(username));
  }
  return nullptr;
}

unique_ptr<LinkManager::InternalLink> LinkManager::get_internal_link_message_draft(Slice url, Slice text) {
  if (url.empty() && text.empty()) {
    return nullptr;
  }
  while (!text.empty() && text.back() == '\n') {
    text.remove_suffix(1);
  }
  url = trim(url);
  if (url.empty()) {
    url = text;
    text = Slice();
  }
  FormattedText full_text;
  bool contains_url = false;
  if (!text.empty()) {
    contains_url = true;
    full_text.text = PSTRING() << url << '\n' << text;
  } else {
    full_text.text = url.str();
  }
  if (fix_formatted_text(full_text.text, full_text.entities, false, false, false, true, true).is_error()) {
    return nullptr;
  }
  if (full_text.text[0] == '@') {
    full_text.text = ' ' + full_text.text;
    for (auto &entity : full_text.entities) {
      entity.offset++;
    }
  }
  return td::make_unique<InternalLinkMessageDraft>(std::move(full_text), contains_url);
}

unique_ptr<LinkManager::InternalLink> LinkManager::get_internal_link_passport(
    Slice query, const vector<std::pair<string, string>> &args) {
  auto get_arg = [&args](Slice key) {
    for (auto &arg : args) {
      if (arg.first == key) {
        return Slice(arg.second);
      }
    }
    return Slice();
  };

  UserId bot_user_id(to_integer<int64>(get_arg("bot_id")));
  auto scope = get_arg("scope");
  auto public_key = get_arg("public_key");
  auto nonce = get_arg("nonce");
  if (nonce.empty()) {
    nonce = get_arg("payload");
  }
  auto callback_url = get_arg("callback_url");

  if (!bot_user_id.is_valid() || scope.empty() || public_key.empty() || nonce.empty()) {
    return td::make_unique<InternalLinkUnknownDeepLink>(PSTRING() << "tg://" << query);
  }
  return td::make_unique<InternalLinkPassportDataRequest>(bot_user_id, scope.str(), public_key.str(), nonce.str(),
                                                          callback_url.str());
}

void LinkManager::update_autologin_domains(string autologin_token, vector<string> autologin_domains,
                                           vector<string> url_auth_domains) {
  autologin_update_time_ = Time::now();
  autologin_token_ = std::move(autologin_token);
  if (autologin_domains_ != autologin_domains) {
    autologin_domains_ = std::move(autologin_domains);
    G()->td_db()->get_binlog_pmc()->set("autologin_domains", implode(autologin_domains_, '\xFF'));
  }
  if (url_auth_domains_ != url_auth_domains) {
    url_auth_domains_ = std::move(url_auth_domains);
    G()->td_db()->get_binlog_pmc()->set("url_auth_domains", implode(url_auth_domains_, '\xFF'));
  }
}

void LinkManager::get_deep_link_info(Slice link, Promise<td_api::object_ptr<td_api::deepLinkInfo>> &&promise) {
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
  td_->create_handler<GetDeepLinkInfoQuery>(std::move(promise))->send(link);
}

void LinkManager::get_external_link_info(string &&link, Promise<td_api::object_ptr<td_api::LoginUrlInfo>> &&promise) {
  auto default_result = td_api::make_object<td_api::loginUrlInfoOpen>(link, false);
  if (G()->close_flag()) {
    return promise.set_value(std::move(default_result));
  }

  auto r_url = parse_url(link);
  if (r_url.is_error()) {
    return promise.set_value(std::move(default_result));
  }

  if (!td::contains(autologin_domains_, r_url.ok().host_)) {
    if (td::contains(url_auth_domains_, r_url.ok().host_)) {
      td_->create_handler<RequestUrlAuthQuery>(std::move(promise))->send(link, FullMessageId(), 0);
      return;
    }
    return promise.set_value(std::move(default_result));
  }

  if (autologin_update_time_ < Time::now() - 10000) {
    auto query_promise =
        PromiseCreator::lambda([link = std::move(link), promise = std::move(promise)](Result<Unit> &&result) mutable {
          if (result.is_error()) {
            return promise.set_value(td_api::make_object<td_api::loginUrlInfoOpen>(link, false));
          }
          send_closure(G()->link_manager(), &LinkManager::get_external_link_info, std::move(link), std::move(promise));
        });
    return send_closure(G()->config_manager(), &ConfigManager::reget_app_config, std::move(query_promise));
  }

  if (autologin_token_.empty()) {
    return promise.set_value(std::move(default_result));
  }

  auto url = r_url.move_as_ok();
  url.protocol_ = HttpUrl::Protocol::Https;
  Slice path = url.query_;
  path.truncate(url.query_.find_first_of("?#"));
  Slice parameters_hash = Slice(url.query_).substr(path.size());
  Slice parameters = parameters_hash;
  parameters.truncate(parameters.find('#'));
  Slice hash = parameters_hash.substr(parameters.size());

  string added_parameter;
  if (parameters.empty()) {
    added_parameter = '?';
  } else if (parameters.size() == 1) {
    CHECK(parameters == "?");
  } else {
    added_parameter = '&';
  }
  added_parameter += "autologin_token=";
  added_parameter += autologin_token_;

  url.query_ = PSTRING() << path << parameters << added_parameter << hash;

  promise.set_value(td_api::make_object<td_api::loginUrlInfoOpen>(url.get_url(), false));
}

void LinkManager::get_login_url_info(FullMessageId full_message_id, int64 button_id,
                                     Promise<td_api::object_ptr<td_api::LoginUrlInfo>> &&promise) {
  TRY_RESULT_PROMISE(promise, url, td_->messages_manager_->get_login_button_url(full_message_id, button_id));
  td_->create_handler<RequestUrlAuthQuery>(std::move(promise))
      ->send(std::move(url), full_message_id, narrow_cast<int32>(button_id));
}

void LinkManager::get_login_url(FullMessageId full_message_id, int64 button_id, bool allow_write_access,
                                Promise<td_api::object_ptr<td_api::httpUrl>> &&promise) {
  TRY_RESULT_PROMISE(promise, url, td_->messages_manager_->get_login_button_url(full_message_id, button_id));
  td_->create_handler<AcceptUrlAuthQuery>(std::move(promise))
      ->send(std::move(url), full_message_id, narrow_cast<int32>(button_id), allow_write_access);
}

void LinkManager::get_link_login_url(const string &url, bool allow_write_access,
                                     Promise<td_api::object_ptr<td_api::httpUrl>> &&promise) {
  td_->create_handler<AcceptUrlAuthQuery>(std::move(promise))->send(url, FullMessageId(), 0, allow_write_access);
}

string LinkManager::get_dialog_invite_link_hash(Slice invite_link) {
  auto link_info = get_link_info(invite_link);
  if (!link_info.is_internal_) {
    return string();
  }
  const auto url_query = parse_url_query(link_info.query_);
  return get_url_query_hash(link_info.is_tg_, url_query);
}

string LinkManager::get_dialog_invite_link(Slice hash, bool is_internal) {
  if (!is_base64url_characters(hash)) {
    return string();
  }
  if (is_internal) {
    return PSTRING() << "tg:join?invite=" << hash;
  } else {
    return PSTRING() << G()->shared_config().get_option_string("t_me_url", "https://t.me/") << '+' << hash;
  }
}

UserId LinkManager::get_link_user_id(Slice url) {
  string lower_cased_url = to_lower(url);
  url = lower_cased_url;

  Slice link_scheme("tg:");
  if (!begins_with(url, link_scheme)) {
    return UserId();
  }
  url.remove_prefix(link_scheme.size());
  if (begins_with(url, "//")) {
    url.remove_prefix(2);
  }

  Slice host("user");
  if (!begins_with(url, host)) {
    return UserId();
  }
  url.remove_prefix(host.size());
  if (begins_with(url, "/")) {
    url.remove_prefix(1);
  }
  if (!begins_with(url, "?")) {
    return UserId();
  }
  url.remove_prefix(1);
  url.truncate(url.find('#'));

  for (auto parameter : full_split(url, '&')) {
    Slice key;
    Slice value;
    std::tie(key, value) = split(parameter, '=');
    if (key == Slice("id")) {
      auto r_user_id = to_integer_safe<int64>(value);
      if (r_user_id.is_error()) {
        return UserId();
      }
      return UserId(r_user_id.ok());
    }
  }
  return UserId();
}

Result<MessageLinkInfo> LinkManager::get_message_link_info(Slice url) {
  if (url.empty()) {
    return Status::Error("URL must be non-empty");
  }
  auto link_info = get_link_info(url);
  if (!link_info.is_internal_) {
    return Status::Error("Invalid message link URL");
  }
  url = link_info.query_;

  Slice username;
  Slice channel_id_slice;
  Slice message_id_slice;
  Slice comment_message_id_slice = "0";
  Slice media_timestamp_slice;
  bool is_single = false;
  bool for_comment = false;
  if (link_info.is_tg_) {
    // resolve?domain=username&post=12345&single&t=123&comment=12&thread=21
    // privatepost?channel=123456789&post=12345&single&t=123&comment=12&thread=21

    bool is_resolve = false;
    if (begins_with(url, "resolve")) {
      url = url.substr(7);
      is_resolve = true;
    } else if (begins_with(url, "privatepost")) {
      url = url.substr(11);
    } else {
      return Status::Error("Wrong message link URL");
    }

    if (begins_with(url, "/")) {
      url = url.substr(1);
    }
    if (!begins_with(url, "?")) {
      return Status::Error("Wrong message link URL");
    }
    url = url.substr(1);

    auto args = full_split(url, '&');
    for (auto arg : args) {
      auto key_value = split(arg, '=');
      if (is_resolve) {
        if (key_value.first == "domain") {
          username = key_value.second;
        }
      } else {
        if (key_value.first == "channel") {
          channel_id_slice = key_value.second;
        }
      }
      if (key_value.first == "post") {
        message_id_slice = key_value.second;
      }
      if (key_value.first == "t") {
        media_timestamp_slice = key_value.second;
      }
      if (key_value.first == "single") {
        is_single = true;
      }
      if (key_value.first == "comment") {
        comment_message_id_slice = key_value.second;
      }
      if (key_value.first == "thread") {
        for_comment = true;
      }
    }
  } else {
    // /c/123456789/12345
    // /username/12345?single

    CHECK(!url.empty() && url[0] == '/');
    url.remove_prefix(1);

    auto username_end_pos = url.find('/');
    if (username_end_pos == Slice::npos) {
      return Status::Error("Wrong message link URL");
    }
    username = url.substr(0, username_end_pos);
    url = url.substr(username_end_pos + 1);
    if (username == "c") {
      username = Slice();
      auto channel_id_end_pos = url.find('/');
      if (channel_id_end_pos == Slice::npos) {
        return Status::Error("Wrong message link URL");
      }
      channel_id_slice = url.substr(0, channel_id_end_pos);
      url = url.substr(channel_id_end_pos + 1);
    }

    auto query_pos = url.find('?');
    message_id_slice = url.substr(0, query_pos);
    if (query_pos != Slice::npos) {
      auto args = full_split(url.substr(query_pos + 1), '&');
      for (auto arg : args) {
        auto key_value = split(arg, '=');
        if (key_value.first == "t") {
          media_timestamp_slice = key_value.second;
        }
        if (key_value.first == "single") {
          is_single = true;
        }
        if (key_value.first == "comment") {
          comment_message_id_slice = key_value.second;
        }
        if (key_value.first == "thread") {
          for_comment = true;
        }
      }
    }
  }

  ChannelId channel_id;
  if (username.empty()) {
    auto r_channel_id = to_integer_safe<int64>(channel_id_slice);
    if (r_channel_id.is_error() || !ChannelId(r_channel_id.ok()).is_valid()) {
      return Status::Error("Wrong channel ID");
    }
    channel_id = ChannelId(r_channel_id.ok());
  }

  auto r_message_id = to_integer_safe<int32>(message_id_slice);
  if (r_message_id.is_error() || !ServerMessageId(r_message_id.ok()).is_valid()) {
    return Status::Error("Wrong message ID");
  }

  auto r_comment_message_id = to_integer_safe<int32>(comment_message_id_slice);
  if (r_comment_message_id.is_error() ||
      !(r_comment_message_id.ok() == 0 || ServerMessageId(r_comment_message_id.ok()).is_valid())) {
    return Status::Error("Wrong comment message ID");
  }

  bool is_media_timestamp_invalid = false;
  int32 media_timestamp = 0;
  const int32 MAX_MEDIA_TIMESTAMP = 10000000;
  if (!media_timestamp_slice.empty()) {
    int32 current_value = 0;
    for (size_t i = 0; i <= media_timestamp_slice.size(); i++) {
      auto c = i < media_timestamp_slice.size() ? media_timestamp_slice[i] : 's';
      if ('0' <= c && c <= '9') {
        current_value = current_value * 10 + c - '0';
        if (current_value > MAX_MEDIA_TIMESTAMP) {
          is_media_timestamp_invalid = true;
          break;
        }
      } else {
        auto mul = 0;
        switch (to_lower(c)) {
          case 'h':
            mul = 3600;
            break;
          case 'm':
            mul = 60;
            break;
          case 's':
            mul = 1;
            break;
        }
        if (mul == 0 || current_value > MAX_MEDIA_TIMESTAMP / mul ||
            media_timestamp + current_value * mul > MAX_MEDIA_TIMESTAMP) {
          is_media_timestamp_invalid = true;
          break;
        }
        media_timestamp += current_value * mul;
        current_value = 0;
      }
    }
  }

  MessageLinkInfo info;
  info.username = username.str();
  info.channel_id = channel_id;
  info.message_id = MessageId(ServerMessageId(r_message_id.ok()));
  info.comment_message_id = MessageId(ServerMessageId(r_comment_message_id.ok()));
  info.media_timestamp = is_media_timestamp_invalid ? 0 : media_timestamp;
  info.is_single = is_single;
  info.for_comment = for_comment;
  LOG(INFO) << "Have link to " << info.message_id << " in chat @" << info.username << "/" << channel_id.get();
  return std::move(info);
}

}  // namespace td
