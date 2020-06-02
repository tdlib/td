//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogFilter.h"

#include "td/utils/common.h"
#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void DialogFilter::store(StorerT &storer) const {
  using td::store;
  bool has_pinned_dialog_ids = !pinned_dialog_ids.empty();
  bool has_included_dialog_ids = !included_dialog_ids.empty();
  bool has_excluded_dialog_ids = !excluded_dialog_ids.empty();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(exclude_muted);
  STORE_FLAG(exclude_read);
  STORE_FLAG(exclude_archived);
  STORE_FLAG(include_contacts);
  STORE_FLAG(include_non_contacts);
  STORE_FLAG(include_bots);
  STORE_FLAG(include_groups);
  STORE_FLAG(include_channels);
  STORE_FLAG(has_pinned_dialog_ids);
  STORE_FLAG(has_included_dialog_ids);
  STORE_FLAG(has_excluded_dialog_ids);
  END_STORE_FLAGS();

  store(dialog_filter_id, storer);
  store(title, storer);
  store(emoji, storer);
  if (has_pinned_dialog_ids) {
    store(pinned_dialog_ids, storer);
  }
  if (has_included_dialog_ids) {
    store(included_dialog_ids, storer);
  }
  if (has_excluded_dialog_ids) {
    store(excluded_dialog_ids, storer);
  }
}

template <class ParserT>
void DialogFilter::parse(ParserT &parser) {
  using td::parse;
  bool has_pinned_dialog_ids = !pinned_dialog_ids.empty();
  bool has_included_dialog_ids = !included_dialog_ids.empty();
  bool has_excluded_dialog_ids = !excluded_dialog_ids.empty();
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(exclude_muted);
  PARSE_FLAG(exclude_read);
  PARSE_FLAG(exclude_archived);
  PARSE_FLAG(include_contacts);
  PARSE_FLAG(include_non_contacts);
  PARSE_FLAG(include_bots);
  PARSE_FLAG(include_groups);
  PARSE_FLAG(include_channels);
  PARSE_FLAG(has_pinned_dialog_ids);
  PARSE_FLAG(has_included_dialog_ids);
  PARSE_FLAG(has_excluded_dialog_ids);
  END_PARSE_FLAGS();

  parse(dialog_filter_id, parser);
  parse(title, parser);
  parse(emoji, parser);
  if (has_pinned_dialog_ids) {
    parse(pinned_dialog_ids, parser);
  }
  if (has_included_dialog_ids) {
    parse(included_dialog_ids, parser);
  }
  if (has_excluded_dialog_ids) {
    parse(excluded_dialog_ids, parser);
  }
}

}  // namespace td
