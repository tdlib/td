//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/SuggestedPost.h"

#include "td/utils/logging.h"

namespace td {

unique_ptr<SuggestedPost> SuggestedPost::get_suggested_post(
    telegram_api::object_ptr<telegram_api::suggestedPost> &&post) {
  if (post == nullptr) {
    return nullptr;
  }
  auto result = make_unique<SuggestedPost>();
  result->is_accepted_ = post->accepted_;
  result->is_rejected_ = post->rejected_;
  result->price_ = SuggestedPostPrice(std::move(post->price_));
  result->schedule_date_ = post->schedule_date_;
  if (result->is_accepted_ && result->is_rejected_) {
    LOG(ERROR) << "Receive accepted and rejected suggested post";
  }
  return result;
}

Result<unique_ptr<SuggestedPost>> SuggestedPost::get_suggested_post(
    const Td *td, td_api::object_ptr<td_api::inputSuggestedPostInfo> &&post) {
  if (post == nullptr) {
    return nullptr;
  }
  TRY_RESULT(price, SuggestedPostPrice::get_suggested_post_price(td, std::move(post->price_)));
  auto send_date = post->send_date_;
  if (send_date < 0) {
    return Status::Error(400, "Invalid post send date specified");
  }
  auto result = make_unique<SuggestedPost>();
  result->price_ = std::move(price);
  result->schedule_date_ = send_date;
  return std::move(result);
}

telegram_api::object_ptr<telegram_api::suggestedPost> SuggestedPost::get_input_suggested_post() const {
  int32 flags = 0;
  auto price = price_.get_input_stars_amount();
  if (price != nullptr) {
    flags |= telegram_api::suggestedPost::PRICE_MASK;
  }
  if (schedule_date_ != 0) {
    flags |= telegram_api::suggestedPost::SCHEDULE_DATE_MASK;
  }
  return telegram_api::make_object<telegram_api::suggestedPost>(flags, is_accepted_, is_rejected_, std::move(price),
                                                                schedule_date_);
}

td_api::object_ptr<td_api::SuggestedPostState> SuggestedPost::get_suggested_post_state_object() const {
  if (is_accepted_) {
    return td_api::make_object<td_api::suggestedPostStateApproved>();
  }
  if (is_rejected_) {
    return td_api::make_object<td_api::suggestedPostStateDeclined>();
  }
  return td_api::make_object<td_api::suggestedPostStatePending>();
}

td_api::object_ptr<td_api::suggestedPostInfo> SuggestedPost::get_suggested_post_info_object(
    bool can_be_accepted, bool can_be_rejected) const {
  return td_api::make_object<td_api::suggestedPostInfo>(price_.get_suggested_post_price_object(), schedule_date_,
                                                        get_suggested_post_state_object(), can_be_accepted,
                                                        can_be_rejected);
}

td_api::object_ptr<td_api::inputSuggestedPostInfo> SuggestedPost::get_input_suggested_post_info_object() const {
  return td_api::make_object<td_api::inputSuggestedPostInfo>(price_.get_suggested_post_price_object(), schedule_date_);
}

unique_ptr<SuggestedPost> SuggestedPost::clone(const unique_ptr<SuggestedPost> &post) {
  if (post == nullptr) {
    return nullptr;
  }
  return make_unique<SuggestedPost>(*post);
}

bool operator==(const SuggestedPost &lhs, const SuggestedPost &rhs) {
  return lhs.price_ == rhs.price_ && lhs.schedule_date_ == rhs.schedule_date_ && lhs.is_accepted_ == rhs.is_accepted_ &&
         lhs.is_rejected_ == rhs.is_rejected_;
}

bool operator!=(const SuggestedPost &lhs, const SuggestedPost &rhs) {
  return !(lhs == rhs);
}

}  // namespace td
