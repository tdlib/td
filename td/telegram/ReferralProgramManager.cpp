//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ReferralProgramManager.h"

#include "td/telegram/ChatManager.h"
#include "td/telegram/DialogManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/StarManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/UserManager.h"

#include "td/utils/buffer.h"
#include "td/utils/Status.h"

namespace td {

class ReferralProgramManager::GetSuggestedStarRefBotsQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::foundAffiliatePrograms>> promise_;
  DialogId dialog_id_;

 public:
  explicit GetSuggestedStarRefBotsQuery(Promise<td_api::object_ptr<td_api::foundAffiliatePrograms>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(DialogId dialog_id, ReferralProgramSortOrder sort_order, const string &offset, int32 limit) {
    dialog_id_ = dialog_id;
    auto input_peer = td_->dialog_manager_->get_input_peer(dialog_id, AccessRights::Read);
    CHECK(input_peer != nullptr);
    int32 flags = 0;
    switch (sort_order) {
      case ReferralProgramSortOrder::Profitability:
        break;
      case ReferralProgramSortOrder::Date:
        flags |= telegram_api::payments_getSuggestedStarRefBots::ORDER_BY_DATE_MASK;
        break;
      case ReferralProgramSortOrder::Revenue:
        flags |= telegram_api::payments_getSuggestedStarRefBots::ORDER_BY_REVENUE_MASK;
        break;
      default:
        UNREACHABLE();
    }
    send_query(G()->net_query_creator().create(telegram_api::payments_getSuggestedStarRefBots(
        flags, false /*ignored*/, false /*ignored*/, std::move(input_peer), offset, limit)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_getSuggestedStarRefBots>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(DEBUG) << "Receive result for GetSuggestedStarRefBotsQuery: " << to_string(ptr);

    td_->user_manager_->on_get_users(std::move(ptr->users_), "GetSuggestedStarRefBotsQuery");

    vector<td_api::object_ptr<td_api::foundAffiliateProgram>> programs;
    for (auto &ref : ptr->suggested_bots_) {
      SuggestedBotStarRef star_ref(std::move(ref));
      if (!star_ref.is_valid()) {
        LOG(ERROR) << "Receive invalid referral program for " << dialog_id_;
        continue;
      }
      programs.push_back(star_ref.get_found_affiliate_program_object(td_));
    }

    auto total_count = ptr->count_;
    if (total_count < static_cast<int32>(programs.size())) {
      LOG(ERROR) << "Receive total count = " << total_count << ", but " << programs.size() << " referral programs";
      total_count = static_cast<int32>(programs.size());
    }
    promise_.set_value(
        td_api::make_object<td_api::foundAffiliatePrograms>(total_count, std::move(programs), ptr->next_offset_));
  }

  void on_error(Status status) final {
    td_->dialog_manager_->on_get_dialog_error(dialog_id_, status, "GetSuggestedStarRefBotsQuery");
    promise_.set_error(std::move(status));
  }
};

ReferralProgramManager::SuggestedBotStarRef::SuggestedBotStarRef(
    telegram_api::object_ptr<telegram_api::starRefProgram> &&ref)
    : user_id_(ref->bot_id_), parameters_(ref->commission_permille_, ref->duration_months_) {
}

td_api::object_ptr<td_api::foundAffiliateProgram>
ReferralProgramManager::SuggestedBotStarRef::get_found_affiliate_program_object(Td *td) const {
  CHECK(is_valid());
  return td_api::make_object<td_api::foundAffiliateProgram>(
      td->user_manager_->get_user_id_object(user_id_, "foundAffiliateProgram"),
      parameters_.get_affiliate_program_parameters_object());
}

ReferralProgramManager::ConnectedBotStarRef::ConnectedBotStarRef(
    telegram_api::object_ptr<telegram_api::connectedBotStarRef> &&ref)
    : url_(std::move(ref->url_))
    , date_(ref->date_)
    , user_id_(ref->bot_id_)
    , parameters_(ref->commission_permille_, ref->duration_months_)
    , participant_count_(ref->participants_)
    , revenue_star_count_(StarManager::get_star_count(ref->revenue_)) {
}

td_api::object_ptr<td_api::connectedAffiliateProgram>
ReferralProgramManager::ConnectedBotStarRef::get_connected_affiliate_program_object(Td *td) const {
  CHECK(is_valid());
  return td_api::make_object<td_api::connectedAffiliateProgram>(
      url_, td->user_manager_->get_user_id_object(user_id_, "connectedAffiliateProgram"),
      parameters_.get_affiliate_program_parameters_object(), date_, participant_count_, revenue_star_count_);
}

ReferralProgramManager::ReferralProgramManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

void ReferralProgramManager::tear_down() {
  parent_.reset();
}

Status ReferralProgramManager::check_referable_dialog_id(DialogId dialog_id) const {
  TRY_STATUS(
      td_->dialog_manager_->check_dialog_access(dialog_id, false, AccessRights::Read, "check_referable_dialog_id"));
  switch (dialog_id.get_type()) {
    case DialogType::User: {
      if (dialog_id == td_->dialog_manager_->get_my_dialog_id()) {
        break;
      }
      TRY_RESULT(bot_data, td_->user_manager_->get_bot_data(dialog_id.get_user_id()));
      if (bot_data.can_be_edited) {
        break;
      }
      return Status::Error(400, "The bot isn't owned");
    }
    case DialogType::Chat:
      return Status::Error(400, "The chat must be a channel chat");
    case DialogType::Channel: {
      auto channel_id = dialog_id.get_channel_id();
      if (!td_->chat_manager_->is_broadcast_channel(channel_id)) {
        return Status::Error(400, "The chat must be a channel chat");
      }
      auto status = td_->chat_manager_->get_channel_permissions(channel_id);
      if (!status.can_post_messages()) {
        return Status::Error(400, "Not enough rights in the chat");
      }
      break;
    }
    case DialogType::SecretChat:
    case DialogType::None:
    default:
      UNREACHABLE();
  }
  return Status::OK();
}

void ReferralProgramManager::search_referral_programs(
    DialogId dialog_id, ReferralProgramSortOrder sort_order, const string &offset, int32 limit,
    Promise<td_api::object_ptr<td_api::foundAffiliatePrograms>> &&promise) {
  TRY_STATUS_PROMISE(promise, check_referable_dialog_id(dialog_id));
  if (limit <= 0) {
    return promise.set_error(Status::Error(400, "Limit must be positive"));
  }

  td_->create_handler<GetSuggestedStarRefBotsQuery>(std::move(promise))->send(dialog_id, sort_order, offset, limit);
}

}  // namespace td
