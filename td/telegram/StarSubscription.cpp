//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/StarSubscription.h"

#include "td/telegram/DialogManager.h"
#include "td/telegram/LinkManager.h"
#include "td/telegram/Td.h"

namespace td {

StarSubscription::StarSubscription(telegram_api::object_ptr<telegram_api::starsSubscription> &&subscription)
    : id_(std::move(subscription->id_))
    , dialog_id_(subscription->peer_)
    , until_date_(subscription->until_date_)
    , can_reuse_(subscription->can_refulfill_)
    , is_canceled_(subscription->canceled_)
    , missing_balance_(subscription->missing_balance_)
    , invite_hash_(std::move(subscription->chat_invite_hash_))
    , pricing_(std::move(subscription->pricing_)) {
}

td_api::object_ptr<td_api::starSubscription> StarSubscription::get_star_subscription_object(Td *td) const {
  td->dialog_manager_->force_create_dialog(dialog_id_, "starSubscription", true);
  return td_api::make_object<td_api::starSubscription>(
      id_, td->dialog_manager_->get_chat_id_object(dialog_id_, "starSubscription"), until_date_, can_reuse_,
      is_canceled_, missing_balance_, LinkManager::get_dialog_invite_link(invite_hash_, false),
      pricing_.get_star_subscription_pricing_object());
}

StringBuilder &operator<<(StringBuilder &string_builder, const StarSubscription &subscription) {
  return string_builder << (subscription.is_canceled_ ? "canceled " : "")
                        << (subscription.missing_balance_ ? "expiring " : "") << "subscription " << subscription.id_
                        << " to " << subscription.dialog_id_ << '/' << subscription.invite_hash_ << " until "
                        << subscription.until_date_ << " for " << subscription.pricing_;
}

}  // namespace td
