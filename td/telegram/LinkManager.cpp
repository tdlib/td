//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/LinkManager.h"

#include "td/telegram/ContactsManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/buffer.h"
#include "td/utils/HttpUrl.h"
#include "td/utils/logging.h"
#include "td/utils/SliceBuilder.h"

namespace td {

class RequestUrlAuthQuery : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::LoginUrlInfo>> promise_;
  string url_;
  DialogId dialog_id_;

 public:
  explicit RequestUrlAuthQuery(Promise<td_api::object_ptr<td_api::LoginUrlInfo>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(string url, DialogId dialog_id, MessageId message_id, int32 button_id) {
    url_ = std::move(url);
    int32 flags = 0;
    tl_object_ptr<telegram_api::InputPeer> input_peer;
    if (dialog_id.is_valid()) {
      dialog_id_ = dialog_id;
      input_peer = td->messages_manager_->get_input_peer(dialog_id, AccessRights::Read);
      CHECK(input_peer != nullptr);
      flags |= telegram_api::messages_requestUrlAuth::PEER_MASK;
    } else {
      flags |= telegram_api::messages_requestUrlAuth::URL_MASK;
    }
    send_query(G()->net_query_creator().create(telegram_api::messages_requestUrlAuth(
        flags, std::move(input_peer), message_id.get_server_message_id().get(), button_id, url_)));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_requestUrlAuth>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive " << to_string(result);
    switch (result->get_id()) {
      case telegram_api::urlAuthResultRequest::ID: {
        auto request = telegram_api::move_object_as<telegram_api::urlAuthResultRequest>(result);
        UserId bot_user_id = ContactsManager::get_user_id(request->bot_);
        if (!bot_user_id.is_valid()) {
          return on_error(id, Status::Error(500, "Receive invalid bot_user_id"));
        }
        td->contacts_manager_->on_get_user(std::move(request->bot_), "RequestUrlAuthQuery");
        bool request_write_access =
            (request->flags_ & telegram_api::urlAuthResultRequest::REQUEST_WRITE_ACCESS_MASK) != 0;
        promise_.set_value(td_api::make_object<td_api::loginUrlInfoRequestConfirmation>(
            url_, request->domain_, td->contacts_manager_->get_user_id_object(bot_user_id, "RequestUrlAuthQuery"),
            request_write_access));
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

  void on_error(uint64 id, Status status) override {
    if (!dialog_id_.is_valid() ||
        !td->messages_manager_->on_get_dialog_error(dialog_id_, status, "RequestUrlAuthQuery")) {
      LOG(INFO) << "RequestUrlAuthQuery returned " << status;
    }
    promise_.set_value(td_api::make_object<td_api::loginUrlInfoOpen>(url_, false));
  }
};

class AcceptUrlAuthQuery : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::httpUrl>> promise_;
  string url_;
  DialogId dialog_id_;

 public:
  explicit AcceptUrlAuthQuery(Promise<td_api::object_ptr<td_api::httpUrl>> &&promise) : promise_(std::move(promise)) {
  }

  void send(string url, DialogId dialog_id, MessageId message_id, int32 button_id, bool allow_write_access) {
    url_ = std::move(url);
    int32 flags = 0;
    tl_object_ptr<telegram_api::InputPeer> input_peer;
    if (dialog_id.is_valid()) {
      dialog_id_ = dialog_id;
      input_peer = td->messages_manager_->get_input_peer(dialog_id, AccessRights::Read);
      CHECK(input_peer != nullptr);
      flags |= telegram_api::messages_acceptUrlAuth::PEER_MASK;
    } else {
      flags |= telegram_api::messages_acceptUrlAuth::URL_MASK;
    }
    if (allow_write_access) {
      flags |= telegram_api::messages_acceptUrlAuth::WRITE_ALLOWED_MASK;
    }
    send_query(G()->net_query_creator().create(telegram_api::messages_acceptUrlAuth(
        flags, false /*ignored*/, std::move(input_peer), message_id.get_server_message_id().get(), button_id, url_)));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_acceptUrlAuth>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive " << to_string(result);
    switch (result->get_id()) {
      case telegram_api::urlAuthResultRequest::ID:
        LOG(ERROR) << "Receive unexpected " << to_string(result);
        return on_error(id, Status::Error(500, "Receive unexpected urlAuthResultRequest"));
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

  void on_error(uint64 id, Status status) override {
    if (!dialog_id_.is_valid() ||
        !td->messages_manager_->on_get_dialog_error(dialog_id_, status, "AcceptUrlAuthQuery")) {
      LOG(INFO) << "AcceptUrlAuthQuery returned " << status;
    }
    promise_.set_error(std::move(status));
  }
};

LinkManager::LinkManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

LinkManager::~LinkManager() = default;

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

Result<string> LinkManager::check_link(Slice link) {
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
  if (is_tg || is_ton) {
    if (tolower_begins_with(link, "http://") || http_url.protocol_ == HttpUrl::Protocol::Https ||
        !http_url.userinfo_.empty() || http_url.specified_port_ != 0 || http_url.is_ipv6_) {
      return Status::Error(is_tg ? Slice("Wrong tg URL") : Slice("Wrong ton URL"));
    }

    Slice query(http_url.query_);
    CHECK(query[0] == '/');
    if (query[1] == '?') {
      query.remove_prefix(1);
    }
    return PSTRING() << (is_tg ? "tg" : "ton") << "://" << http_url.host_ << query;
  }

  if (http_url.host_.find('.') == string::npos && !http_url.is_ipv6_) {
    return Status::Error("Wrong HTTP URL");
  }
  return http_url.get_url();
}

void LinkManager::get_login_url_info(DialogId dialog_id, MessageId message_id, int32 button_id,
                                     Promise<td_api::object_ptr<td_api::LoginUrlInfo>> &&promise) {
  TRY_RESULT_PROMISE(promise, url, td_->messages_manager_->get_login_button_url(dialog_id, message_id, button_id));
  td_->create_handler<RequestUrlAuthQuery>(std::move(promise))->send(std::move(url), dialog_id, message_id, button_id);
}

void LinkManager::get_login_url(DialogId dialog_id, MessageId message_id, int32 button_id, bool allow_write_access,
                                Promise<td_api::object_ptr<td_api::httpUrl>> &&promise) {
  TRY_RESULT_PROMISE(promise, url, td_->messages_manager_->get_login_button_url(dialog_id, message_id, button_id));
  td_->create_handler<AcceptUrlAuthQuery>(std::move(promise))
      ->send(std::move(url), dialog_id, message_id, button_id, allow_write_access);
}

void LinkManager::get_link_login_url_info(const string &url,
                                          Promise<td_api::object_ptr<td_api::LoginUrlInfo>> &&promise) {
  if (G()->close_flag()) {
    return promise.set_value(td_api::make_object<td_api::loginUrlInfoOpen>(url, false));
  }

  td_->create_handler<RequestUrlAuthQuery>(std::move(promise))->send(url, DialogId(), MessageId(), 0);
}

void LinkManager::get_link_login_url(const string &url, bool allow_write_access,
                                     Promise<td_api::object_ptr<td_api::httpUrl>> &&promise) {
  td_->create_handler<AcceptUrlAuthQuery>(std::move(promise))
      ->send(url, DialogId(), MessageId(), 0, allow_write_access);
}

}  // namespace td
