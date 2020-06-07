//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/DialogFilter.h"

#include "td/telegram/DialogId.h"
#include "td/telegram/misc.h"

#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"

#include <unordered_set>

namespace td {

unique_ptr<DialogFilter> DialogFilter::get_dialog_filter(telegram_api::object_ptr<telegram_api::dialogFilter> filter,
                                                         bool with_id) {
  DialogFilterId dialog_filter_id(filter->id_);
  if (with_id && !dialog_filter_id.is_valid()) {
    LOG(ERROR) << "Receive invalid " << to_string(filter);
    return nullptr;
  }
  auto dialog_filter = make_unique<DialogFilter>();
  dialog_filter->dialog_filter_id = dialog_filter_id;
  dialog_filter->title = std::move(filter->title_);
  dialog_filter->emoji = std::move(filter->emoticon_);
  std::unordered_set<DialogId, DialogIdHash> added_dialog_ids;
  dialog_filter->pinned_dialog_ids = InputDialogId::get_input_dialog_ids(filter->pinned_peers_, &added_dialog_ids);
  dialog_filter->included_dialog_ids = InputDialogId::get_input_dialog_ids(filter->include_peers_, &added_dialog_ids);
  dialog_filter->excluded_dialog_ids = InputDialogId::get_input_dialog_ids(filter->exclude_peers_, &added_dialog_ids);
  auto flags = filter->flags_;
  dialog_filter->exclude_muted = (flags & telegram_api::dialogFilter::EXCLUDE_MUTED_MASK) != 0;
  dialog_filter->exclude_read = (flags & telegram_api::dialogFilter::EXCLUDE_READ_MASK) != 0;
  dialog_filter->exclude_archived = (flags & telegram_api::dialogFilter::EXCLUDE_ARCHIVED_MASK) != 0;
  dialog_filter->include_contacts = (flags & telegram_api::dialogFilter::CONTACTS_MASK) != 0;
  dialog_filter->include_non_contacts = (flags & telegram_api::dialogFilter::NON_CONTACTS_MASK) != 0;
  dialog_filter->include_bots = (flags & telegram_api::dialogFilter::BOTS_MASK) != 0;
  dialog_filter->include_groups = (flags & telegram_api::dialogFilter::GROUPS_MASK) != 0;
  dialog_filter->include_channels = (flags & telegram_api::dialogFilter::BROADCASTS_MASK) != 0;
  return dialog_filter;
}

void DialogFilter::remove_secret_chat_dialog_ids() {
  auto remove_secret_chats = [](vector<InputDialogId> &input_dialog_ids) {
    td::remove_if(input_dialog_ids, [](InputDialogId input_dialog_id) {
      return input_dialog_id.get_dialog_id().get_type() == DialogType::SecretChat;
    });
  };
  remove_secret_chats(pinned_dialog_ids);
  remove_secret_chats(included_dialog_ids);
  remove_secret_chats(excluded_dialog_ids);
}

bool DialogFilter::is_empty(bool for_server) const {
  if (include_contacts || include_non_contacts || include_bots || include_groups || include_channels) {
    return false;
  }

  if (for_server) {
    vector<InputDialogId> empty_input_dialog_ids;
    return InputDialogId::are_equivalent(pinned_dialog_ids, empty_input_dialog_ids) &&
           InputDialogId::are_equivalent(included_dialog_ids, empty_input_dialog_ids);
  } else {
    return pinned_dialog_ids.empty() && included_dialog_ids.empty();
  }
}

Status DialogFilter::check_limits() const {
  auto get_server_dialog_count = [](const vector<InputDialogId> &input_dialog_ids) {
    int32 result = 0;
    for (auto &input_dialog_id : input_dialog_ids) {
      if (input_dialog_id.get_dialog_id().get_type() != DialogType::SecretChat) {
        result++;
      }
    }
    return result;
  };

  auto excluded_server_dialog_count = get_server_dialog_count(excluded_dialog_ids);
  auto included_server_dialog_count = get_server_dialog_count(included_dialog_ids);
  auto pinned_server_dialog_count = get_server_dialog_count(pinned_dialog_ids);

  auto excluded_secret_dialog_count = static_cast<int32>(excluded_dialog_ids.size()) - excluded_server_dialog_count;
  auto included_secret_dialog_count = static_cast<int32>(included_dialog_ids.size()) - included_server_dialog_count;
  auto pinned_secret_dialog_count = static_cast<int32>(pinned_dialog_ids.size()) - pinned_server_dialog_count;

  if (excluded_server_dialog_count > MAX_INCLUDED_FILTER_DIALOGS ||
      excluded_secret_dialog_count > MAX_INCLUDED_FILTER_DIALOGS) {
    return Status::Error(400, "Maximum number of excluded chats exceeded");
  }
  if (included_server_dialog_count > MAX_INCLUDED_FILTER_DIALOGS ||
      included_secret_dialog_count > MAX_INCLUDED_FILTER_DIALOGS) {
    return Status::Error(400, "Maximum number of included chats exceeded");
  }
  if (included_server_dialog_count + pinned_server_dialog_count > MAX_INCLUDED_FILTER_DIALOGS ||
      included_secret_dialog_count + pinned_secret_dialog_count > MAX_INCLUDED_FILTER_DIALOGS) {
    return Status::Error(400, "Maximum number of pinned chats exceeded");
  }

  if (is_empty(false)) {
    return Status::Error(400, "Folder must contain at least 1 chat");
  }

  if (include_contacts && include_non_contacts && include_bots && include_groups && include_channels &&
      exclude_archived && !exclude_read && !exclude_muted) {
    return Status::Error(400, "Folder must be different from the main chat list");
  }

  return Status::OK();
}

string DialogFilter::get_emoji_by_icon_name(const string &icon_name) {
  init_icon_names();
  auto it = icon_name_to_emoji_.find(icon_name);
  if (it != icon_name_to_emoji_.end()) {
    return it->second;
  }
  return string();
}

string DialogFilter::get_icon_name() const {
  init_icon_names();
  auto it = emoji_to_icon_name_.find(emoji);
  if (it != emoji_to_icon_name_.end()) {
    return it->second;
  }
  return string();
}

string DialogFilter::get_default_icon_name(const td_api::chatFilter *filter) {
  if (!filter->icon_name_.empty() && !get_emoji_by_icon_name(filter->icon_name_).empty()) {
    return filter->icon_name_;
  }

  if (!filter->pinned_chat_ids_.empty() || !filter->included_chat_ids_.empty() || !filter->excluded_chat_ids_.empty()) {
    return "Custom";
  }

  if (filter->include_contacts_ || filter->include_non_contacts_) {
    if (!filter->include_bots_ && !filter->include_groups_ && !filter->include_channels_) {
      return "Private";
    }
  } else {
    if (!filter->include_bots_ && !filter->include_channels_) {
      if (!filter->include_groups_) {
        // just in case
        return "Custom";
      }
      return "Groups";
    }
    if (!filter->include_bots_ && !filter->include_groups_) {
      return "Channels";
    }
    if (!filter->include_groups_ && !filter->include_channels_) {
      return "Bots";
    }
  }
  if (filter->exclude_read_ && !filter->exclude_muted_) {
    return "Unread";
  }
  if (filter->exclude_muted_ && !filter->exclude_read_) {
    return "Unmuted";
  }
  return "Custom";
}

telegram_api::object_ptr<telegram_api::dialogFilter> DialogFilter::get_input_dialog_filter() const {
  int32 flags = 0;
  if (!emoji.empty()) {
    flags |= telegram_api::dialogFilter::EMOTICON_MASK;
  }
  if (exclude_muted) {
    flags |= telegram_api::dialogFilter::EXCLUDE_MUTED_MASK;
  }
  if (exclude_read) {
    flags |= telegram_api::dialogFilter::EXCLUDE_READ_MASK;
  }
  if (exclude_archived) {
    flags |= telegram_api::dialogFilter::EXCLUDE_ARCHIVED_MASK;
  }
  if (include_contacts) {
    flags |= telegram_api::dialogFilter::CONTACTS_MASK;
  }
  if (include_non_contacts) {
    flags |= telegram_api::dialogFilter::NON_CONTACTS_MASK;
  }
  if (include_bots) {
    flags |= telegram_api::dialogFilter::BOTS_MASK;
  }
  if (include_groups) {
    flags |= telegram_api::dialogFilter::GROUPS_MASK;
  }
  if (include_channels) {
    flags |= telegram_api::dialogFilter::BROADCASTS_MASK;
  }

  return telegram_api::make_object<telegram_api::dialogFilter>(
      flags, false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/,
      false /*ignored*/, false /*ignored*/, false /*ignored*/, dialog_filter_id.get(), title, emoji,
      InputDialogId::get_input_peers(pinned_dialog_ids), InputDialogId::get_input_peers(included_dialog_ids),
      InputDialogId::get_input_peers(excluded_dialog_ids));
}

td_api::object_ptr<td_api::chatFilterInfo> DialogFilter::get_chat_filter_info_object() const {
  return td_api::make_object<td_api::chatFilterInfo>(dialog_filter_id.get(), title, get_icon_name());
}

// merges changes from old_server_filter to new_server_filter in old_filter
unique_ptr<DialogFilter> DialogFilter::merge_dialog_filter_changes(const DialogFilter *old_filter,
                                                                   const DialogFilter *old_server_filter,
                                                                   const DialogFilter *new_server_filter) {
  CHECK(old_filter != nullptr);
  CHECK(old_server_filter != nullptr);
  CHECK(new_server_filter != nullptr);
  CHECK(old_filter->dialog_filter_id == old_server_filter->dialog_filter_id);
  CHECK(old_filter->dialog_filter_id == new_server_filter->dialog_filter_id);
  auto dialog_filter_id = old_filter->dialog_filter_id;
  auto new_filter = make_unique<DialogFilter>(*old_filter);
  new_filter->dialog_filter_id = dialog_filter_id;

  auto merge_ordered_changes = [dialog_filter_id](auto &new_dialog_ids, auto old_server_dialog_ids,
                                                  auto new_server_dialog_ids) {
    if (old_server_dialog_ids == new_server_dialog_ids) {
      LOG(INFO) << "Pinned chats was not changed remotely in " << dialog_filter_id << ", keep local changes";
      return;
    }

    if (InputDialogId::are_equivalent(new_dialog_ids, old_server_dialog_ids)) {
      LOG(INFO) << "Pinned chats was not changed locally in " << dialog_filter_id << ", keep remote changes";

      size_t kept_server_dialogs = 0;
      std::unordered_set<DialogId, DialogIdHash> removed_dialog_ids;
      auto old_it = old_server_dialog_ids.rbegin();
      for (auto &input_dialog_id : reversed(new_server_dialog_ids)) {
        auto dialog_id = input_dialog_id.get_dialog_id();
        while (old_it < old_server_dialog_ids.rend()) {
          if (old_it->get_dialog_id() == dialog_id) {
            kept_server_dialogs++;
            ++old_it;
            break;
          }

          // remove the dialog, it could be added back later
          removed_dialog_ids.insert(old_it->get_dialog_id());
          ++old_it;
        }
      }
      while (old_it < old_server_dialog_ids.rend()) {
        // remove the dialog, it could be added back later
        removed_dialog_ids.insert(old_it->get_dialog_id());
        ++old_it;
      }
      td::remove_if(new_dialog_ids, [&removed_dialog_ids](auto input_dialog_id) {
        return removed_dialog_ids.count(input_dialog_id.get_dialog_id()) > 0;
      });
      new_dialog_ids.insert(new_dialog_ids.begin(), new_server_dialog_ids.begin(),
                            new_server_dialog_ids.end() - kept_server_dialogs);
    } else {
      LOG(WARNING) << "Ignore remote changes of pinned chats in " << dialog_filter_id;
      // there are both local and remote changes; ignore remote changes for now
    }
  };

  auto merge_changes = [](auto &new_dialog_ids, const auto &old_server_dialog_ids, const auto &new_server_dialog_ids) {
    if (old_server_dialog_ids == new_server_dialog_ids) {
      // fast path
      return;
    }

    // merge additions and deletions from other clients to the local changes
    std::unordered_set<DialogId, DialogIdHash> deleted_dialog_ids;
    for (auto old_dialog_id : old_server_dialog_ids) {
      deleted_dialog_ids.insert(old_dialog_id.get_dialog_id());
    }
    std::unordered_set<DialogId, DialogIdHash> added_dialog_ids;
    for (auto new_dialog_id : new_server_dialog_ids) {
      auto dialog_id = new_dialog_id.get_dialog_id();
      if (deleted_dialog_ids.erase(dialog_id) == 0) {
        added_dialog_ids.insert(dialog_id);
      }
    }
    vector<InputDialogId> result;
    for (auto input_dialog_id : new_dialog_ids) {
      // do not add dialog twice
      added_dialog_ids.erase(input_dialog_id.get_dialog_id());
    }
    for (auto new_dialog_id : new_server_dialog_ids) {
      if (added_dialog_ids.count(new_dialog_id.get_dialog_id()) == 1) {
        result.push_back(new_dialog_id);
      }
    }
    for (auto input_dialog_id : new_dialog_ids) {
      if (deleted_dialog_ids.count(input_dialog_id.get_dialog_id()) == 0) {
        result.push_back(input_dialog_id);
      }
    }
    new_dialog_ids = std::move(result);
  };

  merge_ordered_changes(new_filter->pinned_dialog_ids, old_server_filter->pinned_dialog_ids,
                        new_server_filter->pinned_dialog_ids);
  merge_changes(new_filter->included_dialog_ids, old_server_filter->included_dialog_ids,
                new_server_filter->included_dialog_ids);
  merge_changes(new_filter->excluded_dialog_ids, old_server_filter->excluded_dialog_ids,
                new_server_filter->excluded_dialog_ids);

  {
    std::unordered_set<DialogId, DialogIdHash> added_dialog_ids;
    auto remove_duplicates = [&added_dialog_ids](auto &input_dialog_ids) {
      td::remove_if(input_dialog_ids, [&added_dialog_ids](auto input_dialog_id) {
        return !added_dialog_ids.insert(input_dialog_id.get_dialog_id()).second;
      });
    };
    remove_duplicates(new_filter->pinned_dialog_ids);
    remove_duplicates(new_filter->included_dialog_ids);
    remove_duplicates(new_filter->excluded_dialog_ids);
  }

  auto update_value = [](auto &new_value, const auto &old_server_value, const auto &new_server_value) {
    // if the value was changed from other client and wasn't changed from the current client, update it
    if (new_server_value != old_server_value && old_server_value == new_value) {
      new_value = new_server_value;
    }
  };

  update_value(new_filter->exclude_muted, old_server_filter->exclude_muted, new_server_filter->exclude_muted);
  update_value(new_filter->exclude_read, old_server_filter->exclude_read, new_server_filter->exclude_read);
  update_value(new_filter->exclude_archived, old_server_filter->exclude_archived, new_server_filter->exclude_archived);
  update_value(new_filter->include_contacts, old_server_filter->include_contacts, new_server_filter->include_contacts);
  update_value(new_filter->include_non_contacts, old_server_filter->include_non_contacts,
               new_server_filter->include_non_contacts);
  update_value(new_filter->include_bots, old_server_filter->include_bots, new_server_filter->include_bots);
  update_value(new_filter->include_groups, old_server_filter->include_groups, new_server_filter->include_groups);
  update_value(new_filter->include_channels, old_server_filter->include_channels, new_server_filter->include_channels);

  if (new_filter->check_limits().is_error()) {
    LOG(WARNING) << "Failed to merge local and remote changes in " << new_filter->dialog_filter_id
                 << ", keep only local changes";
    *new_filter = *old_filter;
  }

  update_value(new_filter->title, old_server_filter->title, new_server_filter->title);
  update_value(new_filter->emoji, old_server_filter->emoji, new_server_filter->emoji);
  return new_filter;
}

bool DialogFilter::are_similar(const DialogFilter &lhs, const DialogFilter &rhs) {
  if (lhs.title == rhs.title) {
    return true;
  }
  if (!are_flags_equal(lhs, rhs)) {
    return false;
  }

  vector<InputDialogId> empty_input_dialog_ids;
  if (InputDialogId::are_equivalent(lhs.excluded_dialog_ids, empty_input_dialog_ids) !=
      InputDialogId::are_equivalent(rhs.excluded_dialog_ids, empty_input_dialog_ids)) {
    return false;
  }
  if ((InputDialogId::are_equivalent(lhs.pinned_dialog_ids, empty_input_dialog_ids) &&
       InputDialogId::are_equivalent(lhs.included_dialog_ids, empty_input_dialog_ids)) !=
      (InputDialogId::are_equivalent(rhs.pinned_dialog_ids, empty_input_dialog_ids) &&
       InputDialogId::are_equivalent(rhs.included_dialog_ids, empty_input_dialog_ids))) {
    return false;
  }

  return true;
}

bool DialogFilter::are_equivalent(const DialogFilter &lhs, const DialogFilter &rhs) {
  return lhs.title == rhs.title && lhs.emoji == rhs.emoji &&
         InputDialogId::are_equivalent(lhs.pinned_dialog_ids, rhs.pinned_dialog_ids) &&
         InputDialogId::are_equivalent(lhs.included_dialog_ids, rhs.included_dialog_ids) &&
         InputDialogId::are_equivalent(lhs.excluded_dialog_ids, rhs.excluded_dialog_ids) && are_flags_equal(lhs, rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const DialogFilter &filter) {
  return string_builder << filter.dialog_filter_id << " (pinned " << filter.pinned_dialog_ids << ", included "
                        << filter.included_dialog_ids << ", excluded " << filter.excluded_dialog_ids << ", "
                        << filter.exclude_muted << ' ' << filter.exclude_read << ' ' << filter.exclude_archived << '/'
                        << filter.include_contacts << ' ' << filter.include_non_contacts << ' ' << filter.include_bots
                        << ' ' << filter.include_groups << ' ' << filter.include_channels << ')';
}

void DialogFilter::init_icon_names() {
  static bool is_inited = [&] {
    vector<string> emojis{"\xF0\x9F\x92\xAC",         "\xE2\x9C\x85",     "\xF0\x9F\x94\x94",
                          "\xF0\x9F\xA4\x96",         "\xF0\x9F\x93\xA2", "\xF0\x9F\x91\xA5",
                          "\xF0\x9F\x91\xA4",         "\xF0\x9F\x93\x81", "\xF0\x9F\x93\x8B",
                          "\xF0\x9F\x90\xB1",         "\xF0\x9F\x91\x91", "\xE2\xAD\x90\xEF\xB8\x8F",
                          "\xF0\x9F\x8C\xB9",         "\xF0\x9F\x8E\xAE", "\xF0\x9F\x8F\xA0",
                          "\xE2\x9D\xA4\xEF\xB8\x8F", "\xF0\x9F\x8E\xAD", "\xF0\x9F\x8D\xB8",
                          "\xE2\x9A\xBD\xEF\xB8\x8F", "\xF0\x9F\x8E\x93", "\xF0\x9F\x93\x88",
                          "\xE2\x9C\x88\xEF\xB8\x8F", "\xF0\x9F\x92\xBC"};
    vector<string> icon_names{"All",   "Unread", "Unmuted", "Bots",     "Channels", "Groups", "Private", "Custom",
                              "Setup", "Cat",    "Crown",   "Favorite", "Flower",   "Game",   "Home",    "Love",
                              "Mask",  "Party",  "Sport",   "Study",    "Trade",    "Travel", "Work"};
    CHECK(emojis.size() == icon_names.size());
    for (size_t i = 0; i < emojis.size(); i++) {
      emoji_to_icon_name_[remove_emoji_modifiers(emojis[i])] = icon_names[i];
      icon_name_to_emoji_[icon_names[i]] = remove_emoji_modifiers(emojis[i]);
    }
    return true;
  }();
  CHECK(is_inited);
}

bool DialogFilter::are_flags_equal(const DialogFilter &lhs, const DialogFilter &rhs) {
  return lhs.exclude_muted == rhs.exclude_muted && lhs.exclude_read == rhs.exclude_read &&
         lhs.exclude_archived == rhs.exclude_archived && lhs.include_contacts == rhs.include_contacts &&
         lhs.include_non_contacts == rhs.include_non_contacts && lhs.include_bots == rhs.include_bots &&
         lhs.include_groups == rhs.include_groups && lhs.include_channels == rhs.include_channels;
}

std::unordered_map<string, string> DialogFilter::emoji_to_icon_name_;
std::unordered_map<string, string> DialogFilter::icon_name_to_emoji_;

}  // namespace td
