//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/Slice.h"

namespace td {

// checks whether the string is an emoji; variation selectors are ignored
bool is_emoji(Slice str);

// removes all emoji modifiers from the end of the string
Slice remove_emoji_modifiers(Slice emoji);

// removes all emoji modifiers from the end of the string in place
void remove_emoji_modifiers_in_place(string &emoji);

}  // namespace td
