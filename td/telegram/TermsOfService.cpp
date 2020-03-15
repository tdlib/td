//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/TermsOfService.h"

#include "td/telegram/Global.h"
#include "td/telegram/misc.h"
#include "td/telegram/net/NetQueryCreator.h"
#include "td/telegram/Td.h"

#include "td/utils/buffer.h"
#include "td/utils/logging.h"
#include "td/utils/Status.h"

namespace td {

class GetTermsOfServiceUpdateQuery : public Td::ResultHandler {
  Promise<std::pair<int32, TermsOfService>> promise_;

 public:
  explicit GetTermsOfServiceUpdateQuery(Promise<std::pair<int32, TermsOfService>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send() {
    // we don't poll terms of service before authorization
    send_query(G()->net_query_creator().create(telegram_api::help_getTermsOfServiceUpdate()));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::help_getTermsOfServiceUpdate>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    switch (result->get_id()) {
      case telegram_api::help_termsOfServiceUpdateEmpty::ID: {
        auto update = move_tl_object_as<telegram_api::help_termsOfServiceUpdateEmpty>(result);
        promise_.set_value(std::make_pair(update->expires_, TermsOfService()));
        break;
      }
      case telegram_api::help_termsOfServiceUpdate::ID: {
        auto update = move_tl_object_as<telegram_api::help_termsOfServiceUpdate>(result);
        promise_.set_value(std::make_pair(update->expires_, TermsOfService(std::move(update->terms_of_service_))));
        break;
      }
      default:
        UNREACHABLE();
    }
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class AcceptTermsOfServiceQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit AcceptTermsOfServiceQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(string terms_of_service_id) {
    send_query(G()->net_query_creator().create(telegram_api::help_acceptTermsOfService(
        telegram_api::make_object<telegram_api::dataJSON>(std::move(terms_of_service_id)))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::help_acceptTermsOfService>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto result = result_ptr.ok();
    if (!result) {
      LOG(ERROR) << "Failed to accept terms of service";
    }
    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

TermsOfService::TermsOfService(telegram_api::object_ptr<telegram_api::help_termsOfService> terms) {
  if (terms == nullptr) {
    return;
  }

  id_ = std::move(terms->id_->data_);
  auto entities = get_message_entities(nullptr, std::move(terms->entities_), "TermsOfService");
  auto status = fix_formatted_text(terms->text_, entities, true, true, true, false);
  if (status.is_error()) {
    if (!clean_input_string(terms->text_)) {
      terms->text_.clear();
    }
    entities = find_entities(terms->text_, true);
  }
  if (terms->text_.empty()) {
    id_.clear();
  }
  text_ = FormattedText{std::move(terms->text_), std::move(entities)};
  min_user_age_ =
      ((terms->flags_ & telegram_api::help_termsOfService::MIN_AGE_CONFIRM_MASK) != 0 ? terms->min_age_confirm_ : 0);
  show_popup_ = (terms->flags_ & telegram_api::help_termsOfService::POPUP_MASK) != 0;
}

void get_terms_of_service(Td *td, Promise<std::pair<int32, TermsOfService>> promise) {
  td->create_handler<GetTermsOfServiceUpdateQuery>(std::move(promise))->send();
}

void accept_terms_of_service(Td *td, string &&terms_of_service_id, Promise<Unit> &&promise) {
  td->create_handler<AcceptTermsOfServiceQuery>(std::move(promise))->send(std::move(terms_of_service_id));
}

}  // namespace td
