//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogFilterId.h"
#include "td/telegram/InputDialogId.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/Status.h"
#include "td/utils/StringBuilder.h"

#include <unordered_map>

namespace td {

class DialogFilter {
 public:
  static constexpr int32 MAX_INCLUDED_FILTER_DIALOGS = 100;  // server side limit

  DialogFilterId dialog_filter_id;
  string title;
  string emoji;
  vector<InputDialogId> pinned_dialog_ids;
  vector<InputDialogId> included_dialog_ids;
  vector<InputDialogId> excluded_dialog_ids;
  bool exclude_muted = false;
  bool exclude_read = false;
  bool exclude_archived = false;
  bool include_contacts = false;
  bool include_non_contacts = false;
  bool include_bots = false;
  bool include_groups = false;
  bool include_channels = false;

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);

  static unique_ptr<DialogFilter> get_dialog_filter(telegram_api::object_ptr<telegram_api::dialogFilter> filter,
                                                    bool with_id);

  void remove_secret_chat_dialog_ids();

  bool is_empty(bool for_server) const;

  Status check_limits() const;

  static string get_emoji_by_icon_name(const string &icon_name);

  string get_icon_name() const;

  static string get_default_icon_name(const td_api::chatFilter *filter);

  telegram_api::object_ptr<telegram_api::dialogFilter> get_input_dialog_filter() const;

  td_api::object_ptr<td_api::chatFilterInfo> get_chat_filter_info_object() const;

  // merges changes from old_server_filter to new_server_filter in old_filter
  static unique_ptr<DialogFilter> merge_dialog_filter_changes(const DialogFilter *old_filter,
                                                              const DialogFilter *old_server_filter,
                                                              const DialogFilter *new_server_filter);

  static bool are_similar(const DialogFilter &lhs, const DialogFilter &rhs);

  static bool are_equivalent(const DialogFilter &lhs, const DialogFilter &rhs);

  static bool are_flags_equal(const DialogFilter &lhs, const DialogFilter &rhs);

 private:
  static std::unordered_map<string, string> emoji_to_icon_name_;
  static std::unordered_map<string, string> icon_name_to_emoji_;

  static void init_icon_names();
};

inline bool operator==(const DialogFilter &lhs, const DialogFilter &rhs) {
  return lhs.dialog_filter_id == rhs.dialog_filter_id && lhs.title == rhs.title && lhs.emoji == rhs.emoji &&
         lhs.pinned_dialog_ids == rhs.pinned_dialog_ids && lhs.included_dialog_ids == rhs.included_dialog_ids &&
         lhs.excluded_dialog_ids == rhs.excluded_dialog_ids && DialogFilter::are_flags_equal(lhs, rhs);
}

inline bool operator!=(const DialogFilter &lhs, const DialogFilter &rhs) {
  return !(lhs == rhs);
}

inline bool operator==(const unique_ptr<DialogFilter> &lhs, const unique_ptr<DialogFilter> &rhs) {
  return *lhs == *rhs;
}

inline bool operator!=(const unique_ptr<DialogFilter> &lhs, const unique_ptr<DialogFilter> &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const DialogFilter &filter);

}  // namespace td
