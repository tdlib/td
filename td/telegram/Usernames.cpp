//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/Usernames.h"

#include "td/utils/algorithm.h"
#include "td/utils/FlatHashSet.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/utf8.h"

namespace td {

Usernames::Usernames(string &&first_username, vector<telegram_api::object_ptr<telegram_api::username>> &&usernames) {
  if (usernames.empty()) {
    if (!first_username.empty()) {
      active_usernames_.push_back(std::move(first_username));
      editable_username_pos_ = 0;
    }
    return;
  }

  if (!first_username.empty()) {
    LOG(ERROR) << "Receive first username " << first_username << " with " << to_string(usernames);
    return;
  }
  bool was_editable = false;
  bool is_editable_username_disabled = false;
  for (auto &username : usernames) {
    if (username->editable_) {
      if (was_editable) {
        username->editable_ = false;
      } else {
        is_editable_username_disabled = !username->active_;
        was_editable = true;
      }
    }
  }

  FlatHashSet<string> received_usernames;
  for (auto &username : usernames) {
    if (username->username_.empty()) {
      LOG(ERROR) << "Receive empty username";
      continue;
    }
    if (!received_usernames.insert(username->username_).second) {
      LOG(ERROR) << "Receive duplicate username";
      continue;
    }
    if (username->active_) {
      active_usernames_.push_back(std::move(username->username_));
      if (username->editable_) {
        CHECK(!is_editable_username_disabled);
        editable_username_pos_ = narrow_cast<int32>(active_usernames_.size() - 1);
      }
    } else {
      disabled_usernames_.push_back(std::move(username->username_));
      if (username->editable_) {
        CHECK(is_editable_username_disabled);
        editable_username_pos_ = narrow_cast<int32>(disabled_usernames_.size() - 1);
      }
    }
  }
  is_editable_username_disabled_ = is_editable_username_disabled;
  CHECK(has_editable_username() == was_editable);
  check_validness();
}

tl_object_ptr<td_api::usernames> Usernames::get_usernames_object() const {
  if (is_empty()) {
    return nullptr;
  }
  return make_tl_object<td_api::usernames>(vector<string>(active_usernames_), vector<string>(disabled_usernames_),
                                           get_editable_username());
}

Usernames Usernames::change_editable_username(string &&new_username) const {
  Usernames result = *this;
  if (has_editable_username()) {
    if (is_editable_username_disabled_) {
      LOG(INFO) << "Tried to change disabled editable username";
      return result;
    }
    if (new_username.empty()) {
      result.active_usernames_.erase(result.active_usernames_.begin() + editable_username_pos_);
      result.editable_username_pos_ = -1;
    } else {
      // keep position
      result.active_usernames_[editable_username_pos_] = std::move(new_username);
    }
  } else if (!new_username.empty()) {
    // add to the beginning
    CHECK(!is_editable_username_disabled_);
    result.active_usernames_.insert(result.active_usernames_.begin(), std::move(new_username));
    result.editable_username_pos_ = 0;
  }
  result.check_validness();
  return result;
}

bool Usernames::can_toggle(bool for_bot, const string &username) const {
  if (td::contains(active_usernames_, username)) {
    if (!has_editable_username() || is_editable_username_disabled_ ||
        active_usernames_[editable_username_pos_] != username) {
      // disabling of non-editable username is always allowed
      return true;
    }
    if (for_bot) {
      // bots can disable editable username if there is another active username
      return active_usernames_.size() >= 2u;
    }
    // otherwise, editable user can't be disabled
    return false;
  }
  if (td::contains(disabled_usernames_, username)) {
    return true;
  }
  return false;
}

Usernames Usernames::toggle(bool for_bot, const string &username, bool is_active) const {
  Usernames result = *this;
  if (!can_toggle(for_bot, username)) {
    return result;
  }
  for (size_t i = 0; i < active_usernames_.size(); i++) {
    if (active_usernames_[i] == username) {
      if (!is_active) {
        // disable the username
        result.active_usernames_.erase(result.active_usernames_.begin() + i);
        result.disabled_usernames_.insert(result.disabled_usernames_.begin(), username);
        if (has_editable_username()) {
          if (is_editable_username_disabled_) {
            result.editable_username_pos_++;
            if (for_bot && result.active_usernames_.empty()) {
              // activate the previously disabled editable username
              auto editable_username = result.disabled_usernames_[result.editable_username_pos_];
              result.disabled_usernames_.erase(result.disabled_usernames_.begin() + result.editable_username_pos_);
              result.active_usernames_.push_back(editable_username);
              result.is_editable_username_disabled_ = false;
              result.editable_username_pos_ = 0;
            }
          } else {
            if (i <= static_cast<size_t>(result.editable_username_pos_)) {
              if (i == static_cast<size_t>(result.editable_username_pos_)) {
                CHECK(for_bot);
                // the editable username is being disabled
                result.editable_username_pos_ = 0;
                result.is_editable_username_disabled_ = true;
              } else {
                CHECK(result.editable_username_pos_ > 0);
                result.editable_username_pos_--;
              }
            }
          }
        }
      }
      result.check_validness();
      return result;
    }
  }
  for (size_t i = 0; i < disabled_usernames_.size(); i++) {
    if (disabled_usernames_[i] == username) {
      if (is_active) {
        // activate the username
        result.disabled_usernames_.erase(result.disabled_usernames_.begin() + i);
        result.active_usernames_.push_back(username);
        if (is_editable_username_disabled_) {
          CHECK(has_editable_username());
          if (static_cast<size_t>(editable_username_pos_) == i) {
            // editable username was activated
            result.is_editable_username_disabled_ = false;
            result.editable_username_pos_ = static_cast<int32>(result.active_usernames_.size() - 1);
          } else if (i <= static_cast<size_t>(editable_username_pos_)) {
            // editable username position decreased
            CHECK(result.editable_username_pos_ > 0);
            result.editable_username_pos_--;
          }
        } else {
          // editable username position wasn't changed
        }
      }
      result.check_validness();
      return result;
    }
  }
  UNREACHABLE();
  return result;
}

Usernames Usernames::deactivate_all() const {
  Usernames result;
  for (size_t i = 0; i < active_usernames_.size(); i++) {
    if (!is_editable_username_disabled_ && i == static_cast<size_t>(editable_username_pos_)) {
      result.active_usernames_.push_back(active_usernames_[i]);
      result.editable_username_pos_ = 0;
    } else {
      result.disabled_usernames_.push_back(active_usernames_[i]);
    }
  }
  if (is_editable_username_disabled_) {
    CHECK(has_editable_username());
    result.editable_username_pos_ = editable_username_pos_ + static_cast<int32>(result.disabled_usernames_.size());
  }
  append(result.disabled_usernames_, disabled_usernames_);
  CHECK(result.has_editable_username() == has_editable_username());
  result.check_validness();
  return result;
}

bool Usernames::can_reorder_to(const vector<string> &new_username_order) const {
  if (new_username_order.size() != active_usernames_.size()) {
    return false;
  }
  FlatHashSet<string> active_usernames;
  for (auto &username : active_usernames_) {
    active_usernames.insert(username);
  }
  for (auto &username : new_username_order) {
    auto it = active_usernames.find(username);
    if (it == active_usernames.end()) {
      return false;
    }
    active_usernames.erase(it);
  }
  CHECK(active_usernames.empty());
  return true;
}

Usernames Usernames::reorder_to(vector<string> &&new_username_order) const {
  Usernames result;
  result.active_usernames_ = std::move(new_username_order);
  result.disabled_usernames_ = disabled_usernames_;
  if (has_editable_username()) {
    if (is_editable_username_disabled_) {
      result.editable_username_pos_ = editable_username_pos_;
    } else {
      const string &editable_username = active_usernames_[editable_username_pos_];
      for (size_t i = 0; i < result.active_usernames_.size(); i++) {
        if (result.active_usernames_[i] == editable_username) {
          result.editable_username_pos_ = narrow_cast<int32>(i);
          break;
        }
      }
    }
    CHECK(result.has_editable_username());
  }
  result.check_validness();
  return result;
}

void Usernames::check_validness() {
  FlatHashSet<string> usernames;
  for (auto &username : active_usernames_) {
    if (username.empty() || !check_utf8(username) || !usernames.insert(username).second) {
      LOG(ERROR) << "Have invalid active username \"" << username << '"';
      *this = Usernames();
      return;
    }
  }
  for (auto &username : disabled_usernames_) {
    if (username.empty() || !check_utf8(username) || !usernames.insert(username).second) {
      LOG(ERROR) << "Have invalid disabled username \"" << username << '"';
      *this = Usernames();
      return;
    }
  }
  if (is_editable_username_disabled_) {
    if (static_cast<size_t>(editable_username_pos_) >= disabled_usernames_.size()) {
      LOG(ERROR) << "Have invalid editable username position " << editable_username_pos_;
      *this = Usernames();
      return;
    }
  } else {
    if (editable_username_pos_ != -1 && static_cast<size_t>(editable_username_pos_) >= active_usernames_.size()) {
      LOG(ERROR) << "Have invalid editable username position " << editable_username_pos_;
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
  if (usernames.has_editable_username()) {
    string_builder << usernames.get_editable_username();
  }
  if (!usernames.active_usernames_.empty()) {
    string_builder << ", active " << usernames.active_usernames_;
  }
  if (!usernames.disabled_usernames_.empty()) {
    string_builder << ", disabled " << usernames.disabled_usernames_;
  }
  return string_builder << ']';
}

}  // namespace td
