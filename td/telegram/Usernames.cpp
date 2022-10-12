//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/Usernames.h"

#include "td/telegram/misc.h"
#include "td/telegram/secret_api.h"

namespace td {

Usernames::Usernames(string &&first_username, vector<telegram_api::object_ptr<telegram_api::username>> &&usernames) {
  if (usernames.empty()) {
    if (!first_username.empty()) {
      active_usernames_.push_back(std::move(first_username));
      editable_username_pos_ = 0;
    }
    return;
  }

  if (!first_username.empty() && usernames[0]->username_ != first_username) {
    LOG(ERROR) << "Receive first username " << first_username << " with " << to_string(usernames);
    return;
  }
  bool was_editable = false;
  for (auto &username : usernames) {
    if (username->username_.empty()) {
      LOG(ERROR) << "Receive empty username in " << to_string(usernames);
      return;
    }
    if (username->editable_) {
      if (was_editable) {
        LOG(ERROR) << "Receive two editable usernames in " << to_string(usernames);
        return;
      }
      if (!username->active_) {
        LOG(ERROR) << "Receive disabled editable usernames in " << to_string(usernames);
        return;
      }
      was_editable = true;
    }
  }
  if (!was_editable) {
    LOG(ERROR) << "Receive no editable username in " << to_string(usernames);
    return;
  }

  for (size_t i = 0; i < usernames.size(); i++) {
    if (usernames[i]->active_) {
      active_usernames_.push_back(std::move(usernames[i]->username_));
      if (usernames[i]->editable_) {
        editable_username_pos_ = narrow_cast<int32>(i);
      }
    } else {
      disabled_usernames_.push_back(std::move(usernames[i]->username_));
    }
  }
  CHECK(editable_username_pos_ != -1);
}

tl_object_ptr<td_api::usernames> Usernames::get_usernames_object() const {
  if (is_empty()) {
    return nullptr;
  }
  return make_tl_object<td_api::usernames>(vector<string>(active_usernames_), vector<string>(disabled_usernames_),
                                           active_usernames_[editable_username_pos_]);
}

void Usernames::check_utf8_validness() {
  for (auto &username : active_usernames_) {
    if (!check_utf8(username)) {
      LOG(ERROR) << "Have invalid active username \"" << username << '"';
      *this = Usernames();
      return;
    }
  }
  for (auto &username : disabled_usernames_) {
    if (!check_utf8(username)) {
      LOG(ERROR) << "Have invalid disabled username \"" << username << '"';
      *this = Usernames();
      return;
    }
  }
}

bool operator==(const Usernames &lhs, const Usernames &rhs) {
  return lhs.active_usernames_ == rhs.active_usernames_ && lhs.disabled_usernames_ == rhs.disabled_usernames_ &&
         lhs.editable_username_pos_ == rhs.editable_username_pos_;
}

bool operator!=(const Usernames &lhs, const Usernames &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const Usernames &usernames) {
  string_builder << "Usernames[";
  if (!usernames.active_usernames_.empty()) {
    string_builder << usernames.active_usernames_[usernames.editable_username_pos_] << ' '
                   << usernames.active_usernames_;
  }
  if (!usernames.disabled_usernames_.empty()) {
    string_builder << usernames.disabled_usernames_;
  }
  return string_builder << ']';
}

}  // namespace td
