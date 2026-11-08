//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/WidgetManager.h"

#include "td/telegram/AccessRights.h"
#include "td/telegram/AuthManager.h"
#include "td/telegram/DialogManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/InlineQueriesManager.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/misc.h"
#include "td/telegram/net/NetQuery.h"
#include "td/telegram/Td.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/TonWalletCard.h"
#include "td/telegram/TonWalletManager.h"
#include "td/telegram/UpdatesManager.h"
#include "td/telegram/UserManager.h"

#include "td/utils/buffer.h"
#include "td/utils/logging.h"
#include "td/utils/Status.h"

namespace td {

static Result<telegram_api::object_ptr<telegram_api::userWallet>> get_input_user_wallet(
    td_api::object_ptr<td_api::widgetQueryWallet> &&wallet) {
  if (wallet == nullptr) {
    return Status::Error(400, "Wallet must be non-empty");
  }
  TRY_STATUS(TonWalletManager::check_ton_address(wallet->address_, true));
  if (wallet->gram_amount_ < 0) {
    return Status::Error(400, "Invalid wallet balance specified");
  }
  if (wallet->public_key_.empty() != wallet->state_init_.empty()) {
    return Status::Error(400, "Wallet public key and state init must be specified together");
  }
  return telegram_api::make_object<telegram_api::userWallet>(
      0, wallet->address_, wallet->gram_amount_, BufferSlice(wallet->public_key_), BufferSlice(wallet->state_init_));
}

static Result<telegram_api::object_ptr<telegram_api::WidgetQueryType>> get_input_widget_query_type(
    td_api::object_ptr<td_api::WidgetQueryType> &&query_type) {
  if (query_type == nullptr) {
    return Status::Error(400, "Query type must be non-empty");
  }
  switch (query_type->get_id()) {
    case td_api::widgetQueryTypeData::ID: {
      auto type = td_api::move_object_as<td_api::widgetQueryTypeData>(query_type);
      return telegram_api::make_object<telegram_api::widgetQueryTypeData>(0, type->with_wallet_);
    }
    case td_api::widgetQueryTypeSignData::ID: {
      auto type = td_api::move_object_as<td_api::widgetQueryTypeSignData>(query_type);
      TRY_RESULT(wallet, get_input_user_wallet(std::move(type->wallet_)));
      if (!clean_input_string(type->sign_data_)) {
        return Status::Error(400, "Strings must be encoded in UTF-8");
      }
      return telegram_api::make_object<telegram_api::widgetQueryTypeSignData>(
          std::move(wallet), telegram_api::make_object<telegram_api::dataJSON>(std::move(type->sign_data_)));
    }
    case td_api::widgetQueryTypeSentTransaction::ID: {
      auto type = td_api::move_object_as<td_api::widgetQueryTypeSentTransaction>(query_type);
      TRY_RESULT(wallet, get_input_user_wallet(std::move(type->wallet_)));
      if (!clean_input_string(type->msg_hash_)) {
        return Status::Error(400, "Strings must be encoded in UTF-8");
      }
      return telegram_api::make_object<telegram_api::widgetQueryTypeSentTransaction>(std::move(wallet),
                                                                                     std::move(type->msg_hash_));
    }
    default:
      UNREACHABLE();
      return nullptr;
  }
}

static td_api::object_ptr<td_api::widgetQueryWallet> get_widget_query_wallet_object(
    telegram_api::object_ptr<telegram_api::userWallet> &&wallet) {
  CHECK(wallet != nullptr);
  auto balance = wallet->balance_;
  if (balance < 0) {
    LOG(ERROR) << "Receive TON wallet balance " << balance;
    balance = 0;
  }
  return td_api::make_object<td_api::widgetQueryWallet>(
      std::move(wallet->address_), balance, wallet->public_key_.as_slice().str(), wallet->state_init_.as_slice().str());
}

static td_api::object_ptr<td_api::WidgetQueryType> get_widget_query_type_object(
    telegram_api::object_ptr<telegram_api::WidgetQueryType> &&query_type) {
  CHECK(query_type != nullptr);
  switch (query_type->get_id()) {
    case telegram_api::widgetQueryTypeData::ID: {
      auto type = telegram_api::move_object_as<telegram_api::widgetQueryTypeData>(query_type);
      return td_api::make_object<td_api::widgetQueryTypeData>(type->with_wallet_);
    }
    case telegram_api::widgetQueryTypeSignData::ID: {
      auto type = telegram_api::move_object_as<telegram_api::widgetQueryTypeSignData>(query_type);
      return td_api::make_object<td_api::widgetQueryTypeSignData>(
          get_widget_query_wallet_object(std::move(type->wallet_)), std::move(type->sign_data_->data_));
    }
    case telegram_api::widgetQueryTypeSentTransaction::ID: {
      auto type = telegram_api::move_object_as<telegram_api::widgetQueryTypeSentTransaction>(query_type);
      return td_api::make_object<td_api::widgetQueryTypeSentTransaction>(
          get_widget_query_wallet_object(std::move(type->wallet_)), std::move(type->msg_hash_));
    }
    default:
      UNREACHABLE();
      return nullptr;
  }
}

static td_api::object_ptr<td_api::tonWalletCard> get_widget_query_ton_wallet_card_object(
    Td *td, telegram_api::object_ptr<telegram_api::wallet_card> &&wallet_card) {
  auto card = TonWalletCard::get_ton_wallet_card(td, std::move(wallet_card));
  if (card == nullptr) {
    return nullptr;
  }
  return card->get_ton_wallet_card_object(td);
}

class GetBotWidgetQueryAnswerQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::widgetQueryAnswer>> promise_;
  DialogId dialog_id_;
  MessageId message_id_;

 public:
  explicit GetBotWidgetQueryAnswerQuery(Promise<td_api::object_ptr<td_api::widgetQueryAnswer>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(DialogId dialog_id, MessageId message_id,
            telegram_api::object_ptr<telegram_api::WidgetQueryType> &&query_type, const string &data) {
    dialog_id_ = dialog_id;
    message_id_ = message_id;

    auto input_peer = td_->dialog_manager_->get_input_peer(dialog_id, AccessRights::Read);
    CHECK(input_peer != nullptr);

    auto net_query = G()->net_query_creator().create(telegram_api::messages_getBotWidgetQueryAnswer(
        std::move(input_peer), message_id.get_server_message_id().get(), std::move(query_type), BufferSlice(data)));
    net_query->need_resend_on_503_ = false;
    send_query(std::move(net_query));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_getBotWidgetQueryAnswer>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto answer = result_ptr.move_as_ok();
    promise_.set_value(td_api::make_object<td_api::widgetQueryAnswer>(std::move(answer->result_)));
  }

  void on_error(Status status) final {
    td_->messages_manager_->on_get_message_error(dialog_id_, message_id_, status, "GetBotWidgetQueryAnswerQuery");
    if (status.message() == "BOT_RESPONSE_TIMEOUT") {
      status = Status::Error(502, "The bot is not responding");
    }
    promise_.set_error(std::move(status));
  }
};

class SetBotWidgetQueryAnswerQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit SetBotWidgetQueryAnswerQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(int64 widget_query_id, const string &data) {
    send_query(
        G()->net_query_creator().create(telegram_api::messages_setBotWidgetQueryAnswer(0, widget_query_id, data)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_setBotWidgetQueryAnswer>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    bool result = result_ptr.ok();
    if (!result) {
      LOG(INFO) << "Sending answer to a widget query has failed";
    }
    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class EditMessageWidgetDataQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  DialogId dialog_id_;
  MessageId message_id_;

 public:
  explicit EditMessageWidgetDataQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(DialogId dialog_id, MessageId message_id, bool edit_public_data, const string &public_data,
            bool edit_chat_data, const string &chat_data) {
    dialog_id_ = dialog_id;
    message_id_ = message_id;

    auto input_peer = td_->dialog_manager_->get_input_peer(dialog_id, AccessRights::Read);
    CHECK(input_peer != nullptr);

    int32 flags = 0;
    if (edit_public_data) {
      flags |= telegram_api::messages_editMessageWidgetData::PUBLIC_DATA_MASK;
    }
    if (edit_chat_data) {
      flags |= telegram_api::messages_editMessageWidgetData::CHAT_DATA_MASK;
    }
    send_query(G()->net_query_creator().create(
        telegram_api::messages_editMessageWidgetData(
            flags, std::move(input_peer), message_id.get_server_message_id().get(),
            edit_public_data ? public_data : string(), edit_chat_data ? chat_data : string()),
        {{dialog_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_editMessageWidgetData>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for EditMessageWidgetDataQuery: " << to_string(ptr);
    td_->updates_manager_->on_get_updates(std::move(ptr), std::move(promise_));
  }

  void on_error(Status status) final {
    td_->messages_manager_->on_get_message_error(dialog_id_, message_id_, status, "EditMessageWidgetDataQuery");
    promise_.set_error(std::move(status));
  }
};

class EditWidgetDataQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit EditWidgetDataQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(int64 widget_id, const string &public_data, int32 cache_time) {
    int32 flags = 0;
    if (cache_time > 0) {
      flags |= telegram_api::messages_editWidgetData::CACHE_TIME_MASK;
    }
    send_query(G()->net_query_creator().create(
        telegram_api::messages_editWidgetData(flags, widget_id, public_data, cache_time)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_editWidgetData>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    LOG_IF(ERROR, !result_ptr.ok()) << "Receive false in result of editWidgetData";
    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetWidgetPublicDataQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::widgetPublicData>> promise_;
  DialogId dialog_id_;
  MessageId message_id_;

 public:
  explicit GetWidgetPublicDataQuery(Promise<td_api::object_ptr<td_api::widgetPublicData>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(DialogId dialog_id, MessageId message_id, int64 hash) {
    dialog_id_ = dialog_id;
    message_id_ = message_id;

    auto input_peer = td_->dialog_manager_->get_input_peer(dialog_id, AccessRights::Read);
    CHECK(input_peer != nullptr);

    send_query(G()->net_query_creator().create(telegram_api::messages_getWidgetPublicData(
        std::move(input_peer), message_id.get_server_message_id().get(), hash)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_getWidgetPublicData>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetWidgetPublicDataQuery: " << to_string(ptr);
    td_->widget_manager_->on_get_widget_public_data({dialog_id_, message_id_}, std::move(ptr), std::move(promise_));
  }

  void on_error(Status status) final {
    td_->messages_manager_->on_get_message_error(dialog_id_, message_id_, status, "GetWidgetPublicDataQuery");
    promise_.set_error(std::move(status));
  }
};

WidgetManager::WidgetManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

WidgetManager::~WidgetManager() = default;

void WidgetManager::tear_down() {
  parent_.reset();
}

void WidgetManager::send_widget_query(MessageFullId message_full_id,
                                      td_api::object_ptr<td_api::WidgetQueryType> &&query_type, string &&data,
                                      Promise<td_api::object_ptr<td_api::widgetQueryAnswer>> &&promise) {
  auto dialog_id = message_full_id.get_dialog_id();
  TRY_STATUS_PROMISE(
      promise, td_->dialog_manager_->check_dialog_access(dialog_id, false, AccessRights::Read, "send_widget_query"));

  if (!td_->messages_manager_->have_message_force(message_full_id, "send_widget_query")) {
    return promise.set_error(400, "Message not found");
  }
  auto message_id = message_full_id.get_message_id();
  if (!message_id.is_server()) {
    return promise.set_error(400, "Wrong message identifier");
  }
  TRY_RESULT_PROMISE(promise, input_query_type, get_input_widget_query_type(std::move(query_type)));

  td_->create_handler<GetBotWidgetQueryAnswerQuery>(std::move(promise))
      ->send(dialog_id, message_id, std::move(input_query_type), data);
}

void WidgetManager::on_new_widget_query(int64 widget_query_id,
                                        telegram_api::object_ptr<telegram_api::WidgetQueryType> &&query_type,
                                        UserId sender_user_id, DialogId dialog_id, MessageId message_id,
                                        BufferSlice &&data, int64 chat_instance, string &&chat_type, int64 widget_id,
                                        telegram_api::object_ptr<telegram_api::wallet_card> &&wallet_card) {
  if (!dialog_id.is_valid()) {
    LOG(ERROR) << "Receive new widget query in invalid " << dialog_id;
    return;
  }
  if (!sender_user_id.is_valid()) {
    LOG(ERROR) << "Receive new widget query from invalid " << sender_user_id << " in " << dialog_id;
    return;
  }
  LOG_IF(ERROR, !td_->user_manager_->have_min_user(sender_user_id)) << "Receive unknown " << sender_user_id;
  if (!td_->auth_manager_->is_bot()) {
    LOG(ERROR) << "Receive new widget query";
    return;
  }
  if (!message_id.is_valid()) {
    LOG(ERROR) << "Receive new widget query from " << message_id << " in " << dialog_id << " sent by "
               << sender_user_id;
    return;
  }

  td_->dialog_manager_->force_create_dialog(dialog_id, "on_new_widget_query", true);
  send_closure(
      G()->td(), &Td::send_update,
      td_api::make_object<td_api::updateNewWidgetQuery>(
          widget_query_id, td_->user_manager_->get_user_id_object(sender_user_id, "updateNewWidgetQuery"),
          td_->dialog_manager_->get_chat_id_object(dialog_id, "updateNewWidgetQuery"), message_id.get(), chat_instance,
          std::move(chat_type), get_widget_query_type_object(std::move(query_type)), data.as_slice().str(), widget_id,
          get_widget_query_ton_wallet_card_object(td_, std::move(wallet_card))));
}

void WidgetManager::on_new_inline_widget_query(
    int64 widget_query_id, telegram_api::object_ptr<telegram_api::WidgetQueryType> &&query_type, UserId sender_user_id,
    telegram_api::object_ptr<telegram_api::InputBotInlineMessageID> &&inline_message_id, BufferSlice &&data,
    int64 chat_instance, string &&chat_type, int64 widget_id,
    telegram_api::object_ptr<telegram_api::wallet_card> &&wallet_card) {
  if (!sender_user_id.is_valid()) {
    LOG(ERROR) << "Receive new inline widget query from invalid " << sender_user_id;
    return;
  }
  LOG_IF(ERROR, !td_->user_manager_->have_min_user(sender_user_id)) << "Receive unknown " << sender_user_id;
  if (!td_->auth_manager_->is_bot()) {
    LOG(ERROR) << "Receive new inline widget query";
    return;
  }
  CHECK(inline_message_id != nullptr);

  send_closure(
      G()->td(), &Td::send_update,
      td_api::make_object<td_api::updateNewInlineWidgetQuery>(
          widget_query_id, td_->user_manager_->get_user_id_object(sender_user_id, "updateNewInlineWidgetQuery"),
          InlineQueriesManager::get_inline_message_id(std::move(inline_message_id)), chat_instance,
          std::move(chat_type), get_widget_query_type_object(std::move(query_type)), data.as_slice().str(), widget_id,
          get_widget_query_ton_wallet_card_object(td_, std::move(wallet_card))));
}

void WidgetManager::on_new_widget_copy_query(int64 widget_query_id,
                                             telegram_api::object_ptr<telegram_api::WidgetQueryType> &&query_type,
                                             UserId sender_user_id, BufferSlice &&data, int64 widget_id,
                                             telegram_api::object_ptr<telegram_api::wallet_card> &&wallet_card) {
  if (!sender_user_id.is_valid()) {
    LOG(ERROR) << "Receive new widget copy query from invalid " << sender_user_id;
    return;
  }
  LOG_IF(ERROR, !td_->user_manager_->have_min_user(sender_user_id)) << "Receive unknown " << sender_user_id;
  if (!td_->auth_manager_->is_bot()) {
    LOG(ERROR) << "Receive new widget copy query";
    return;
  }

  send_closure(G()->td(), &Td::send_update,
               td_api::make_object<td_api::updateNewWidgetCopyQuery>(
                   widget_query_id, td_->user_manager_->get_user_id_object(sender_user_id, "updateNewWidgetCopyQuery"),
                   get_widget_query_type_object(std::move(query_type)), data.as_slice().str(), widget_id,
                   get_widget_query_ton_wallet_card_object(td_, std::move(wallet_card))));
}

void WidgetManager::answer_widget_query(int64 widget_query_id, const string &data, Promise<Unit> &&promise) {
  td_->create_handler<SetBotWidgetQueryAnswerQuery>(std::move(promise))->send(widget_query_id, data);
}

void WidgetManager::edit_message_widget_data(MessageFullId message_full_id, bool edit_public_data,
                                             const string &public_data, bool edit_chat_data, const string &chat_data,
                                             Promise<Unit> &&promise) {
  auto dialog_id = message_full_id.get_dialog_id();
  TRY_STATUS_PROMISE(promise, td_->dialog_manager_->check_dialog_access(dialog_id, false, AccessRights::Read,
                                                                        "edit_message_widget_data"));

  if (!td_->messages_manager_->have_message_force(message_full_id, "edit_message_widget_data")) {
    return promise.set_error(400, "Message not found");
  }
  auto message_id = message_full_id.get_message_id();
  if (!message_id.is_server()) {
    return promise.set_error(400, "Wrong message identifier");
  }

  if (!edit_public_data && !edit_chat_data) {
    return promise.set_value(Unit());
  }

  td_->create_handler<EditMessageWidgetDataQuery>(std::move(promise))
      ->send(dialog_id, message_id, edit_public_data, public_data, edit_chat_data, chat_data);
}

void WidgetManager::edit_widget_data(int64 widget_id, const string &public_data, int32 cache_time,
                                     Promise<Unit> &&promise) {
  if (widget_id == 0) {
    return promise.set_error(400, "Invalid widget identifier specified");
  }
  if (cache_time != 0 && (cache_time < 5 || cache_time > 3600)) {
    return promise.set_error(400, "Invalid cache time specified");
  }

  td_->create_handler<EditWidgetDataQuery>(std::move(promise))->send(widget_id, public_data, cache_time);
}

void WidgetManager::get_widget_public_data(MessageFullId message_full_id,
                                           Promise<td_api::object_ptr<td_api::widgetPublicData>> &&promise) {
  auto dialog_id = message_full_id.get_dialog_id();
  TRY_STATUS_PROMISE(promise, td_->dialog_manager_->check_dialog_access(dialog_id, false, AccessRights::Read,
                                                                        "get_widget_public_data"));

  TRY_RESULT_PROMISE(promise, public_data, td_->messages_manager_->get_message_widget_public_data(message_full_id));
  auto message_id = message_full_id.get_message_id();
  if (!message_id.is_server()) {
    return promise.set_error(400, "Wrong message identifier");
  }

  td_->create_handler<GetWidgetPublicDataQuery>(std::move(promise))->send(dialog_id, message_id, public_data.second);
}

void WidgetManager::on_get_widget_public_data(
    MessageFullId message_full_id, telegram_api::object_ptr<telegram_api::messages_WidgetPublicData> &&public_data,
    Promise<td_api::object_ptr<td_api::widgetPublicData>> &&promise) {
  CHECK(public_data != nullptr);
  switch (public_data->get_id()) {
    case telegram_api::messages_widgetPublicDataNotModified::ID: {
      auto period = static_cast<const telegram_api::messages_widgetPublicDataNotModified *>(public_data.get())->period_;
      TRY_RESULT_PROMISE(promise, current_public_data,
                         td_->messages_manager_->get_message_widget_public_data(message_full_id));
      return promise.set_value(
          td_api::make_object<td_api::widgetPublicData>(std::move(current_public_data.first), max(period, 0)));
    }
    case telegram_api::messages_widgetPublicData::ID: {
      auto data = telegram_api::move_object_as<telegram_api::messages_widgetPublicData>(public_data);
      td_->messages_manager_->on_update_message_widget_data(message_full_id, string(data->data_), data->hash_, string(),
                                                            false);
      return promise.set_value(
          td_api::make_object<td_api::widgetPublicData>(std::move(data->data_), max(data->period_, 0)));
    }
    default:
      UNREACHABLE();
  }
}

}  // namespace td
