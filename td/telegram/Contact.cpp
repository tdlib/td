//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/Contact.h"

#include "td/telegram/misc.h"
#include "td/telegram/secret_api.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"

#include <tuple>

namespace td {

Contact::Contact(string phone_number, string first_name, string last_name, string vcard, int32 user_id)
    : phone_number_(std::move(phone_number))
    , first_name_(std::move(first_name))
    , last_name_(std::move(last_name))
    , vcard_(std::move(vcard))
    , user_id_(user_id) {
  if (!user_id_.is_valid()) {
    user_id_ = UserId();
  }
}

void Contact::set_user_id(UserId user_id) {
  user_id_ = user_id;
}

UserId Contact::get_user_id() const {
  return user_id_;
}

string Contact::get_phone_number() const {
  return phone_number_;
}

tl_object_ptr<td_api::contact> Contact::get_contact_object() const {
  return make_tl_object<td_api::contact>(phone_number_, first_name_, last_name_, vcard_, user_id_.get());
}

tl_object_ptr<telegram_api::inputMediaContact> Contact::get_input_media_contact() const {
  return make_tl_object<telegram_api::inputMediaContact>(phone_number_, first_name_, last_name_, vcard_);
}

SecretInputMedia Contact::get_secret_input_media_contact() const {
  return SecretInputMedia{nullptr, make_tl_object<secret_api::decryptedMessageMediaContact>(
                                       phone_number_, first_name_, last_name_, user_id_.get())};
}

tl_object_ptr<telegram_api::inputPhoneContact> Contact::get_input_phone_contact(int64 client_id) const {
  return make_tl_object<telegram_api::inputPhoneContact>(client_id, phone_number_, first_name_, last_name_);
}

tl_object_ptr<telegram_api::inputBotInlineMessageMediaContact> Contact::get_input_bot_inline_message_media_contact(
    int32 flags, tl_object_ptr<telegram_api::ReplyMarkup> &&reply_markup) const {
  return make_tl_object<telegram_api::inputBotInlineMessageMediaContact>(flags, phone_number_, first_name_, last_name_,
                                                                         vcard_, std::move(reply_markup));
}

bool operator==(const Contact &lhs, const Contact &rhs) {
  return std::tie(lhs.phone_number_, lhs.first_name_, lhs.last_name_, lhs.vcard_, lhs.user_id_) ==
         std::tie(rhs.phone_number_, rhs.first_name_, rhs.last_name_, rhs.vcard_, rhs.user_id_);
}

bool operator!=(const Contact &lhs, const Contact &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const Contact &contact) {
  return string_builder << "Contact[phone_number = " << contact.phone_number_
                        << ", first_name = " << contact.first_name_ << ", last_name = " << contact.last_name_
                        << ", vCard size = " << contact.vcard_.size() << contact.user_id_ << "]";
}

Result<Contact> process_input_message_contact(tl_object_ptr<td_api::InputMessageContent> &&input_message_content) {
  CHECK(input_message_content != nullptr);
  CHECK(input_message_content->get_id() == td_api::inputMessageContact::ID);
  auto contact = std::move(static_cast<td_api::inputMessageContact *>(input_message_content.get())->contact_);

  if (!clean_input_string(contact->phone_number_)) {
    return Status::Error(400, "Phone number must be encoded in UTF-8");
  }
  if (!clean_input_string(contact->first_name_)) {
    return Status::Error(400, "First name must be encoded in UTF-8");
  }
  if (!clean_input_string(contact->last_name_)) {
    return Status::Error(400, "Last name must be encoded in UTF-8");
  }
  if (!clean_input_string(contact->vcard_)) {
    return Status::Error(400, "vCard must be encoded in UTF-8");
  }

  return Contact(contact->phone_number_, contact->first_name_, contact->last_name_, contact->vcard_, contact->user_id_);
}

}  // namespace td
