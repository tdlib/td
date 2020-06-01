//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"

namespace td {

// cleans user name/dialog title
string clean_name(string str, size_t max_length) TD_WARN_UNUSED_RESULT;

// prepares username/stickername for search
string clean_username(string str) TD_WARN_UNUSED_RESULT;

// replaces some offending characters without changing string length
void replace_offending_characters(string &str);

// removes control characters from the string, will fail if input string is not in UTF-8
bool clean_input_string(string &str) TD_WARN_UNUSED_RESULT;

// strips empty characters and ensures that string length is no more than max_length
string strip_empty_characters(string str, size_t max_length, bool strip_rtlo = false) TD_WARN_UNUSED_RESULT;

// checks if string is empty after strip_empty_characters
bool is_empty_string(const string &str) TD_WARN_UNUSED_RESULT;

// calculates hash of list of uint32
int32 get_vector_hash(const vector<uint32> &numbers) TD_WARN_UNUSED_RESULT;

// returns emoji corresponding to the specified number
string get_emoji_fingerprint(uint64 num);

// checks whether url is a valid tg, ton or HTTP(S) URL and returns its in a canonical form
Result<string> check_url(Slice url);

// removes all emoji modifiers
string remove_emoji_modifiers(string emoji);

}  // namespace td
