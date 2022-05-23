//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/Premium.h"

namespace td {

const vector<Slice> &get_premium_limit_keys() {
  static const vector<Slice> limit_keys{"channels_limit",
                                        "saved_gifs_limit",
                                        "stickers_faved_limit",
                                        "dialog_filters_limit",
                                        "dialog_filters_chats_limit",
                                        "dialogs_pinned_limit",
                                        "dialogs_folder_pinned_limit",
                                        "channels_public_limit",
                                        "caption_length_limit"};
  return limit_keys;
}

}  // namespace td
