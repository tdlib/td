//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/base64.h"

#include "td/utils/common.h"
#include "td/utils/logging.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"

#include <algorithm>
#include <iterator>

namespace td {
//TODO: fix copypaste

static const char *const symbols64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

string base64_encode(Slice input) {
  string base64;
  base64.reserve((input.size() + 2) / 3 * 4);
  for (size_t i = 0; i < input.size();) {
    size_t left = min(input.size() - i, static_cast<size_t>(3));
    int c = input.ubegin()[i++] << 16;
    base64 += symbols64[c >> 18];
    if (left != 1) {
      c |= input.ubegin()[i++] << 8;
    }
    base64 += symbols64[(c >> 12) & 63];
    if (left == 3) {
      c |= input.ubegin()[i++];
    }
    if (left != 1) {
      base64 += symbols64[(c >> 6) & 63];
    } else {
      base64 += '=';
    }
    if (left == 3) {
      base64 += symbols64[c & 63];
    } else {
      base64 += '=';
    }
  }
  return base64;
}

static unsigned char char_to_value[256];
static void init_base64_table() {
  static bool is_inited = []() {
    std::fill(std::begin(char_to_value), std::end(char_to_value), static_cast<unsigned char>(64));
    for (unsigned char i = 0; i < 64; i++) {
      char_to_value[static_cast<size_t>(symbols64[i])] = i;
    }
    return true;
  }();
  CHECK(is_inited);
}

Result<string> base64_decode(Slice base64) {
  init_base64_table();

  if ((base64.size() & 3) != 0) {
    return Status::Error("Wrong string length");
  }

  size_t padding_length = 0;
  while (!base64.empty() && base64.back() == '=') {
    base64.remove_suffix(1);
    padding_length++;
  }
  if (padding_length >= 3) {
    return Status::Error("Wrong string padding");
  }

  string output;
  output.reserve(((base64.size() + 3) >> 2) * 3);
  for (size_t i = 0; i < base64.size();) {
    size_t left = min(base64.size() - i, static_cast<size_t>(4));
    int c = 0;
    for (size_t t = 0; t < left; t++) {
      auto value = char_to_value[base64.ubegin()[i++]];
      if (value == 64) {
        return Status::Error("Wrong character in the string");
      }
      c |= value << ((3 - t) * 6);
    }
    output += static_cast<char>(static_cast<unsigned char>(c >> 16));  // implementation-defined
    if (left == 2) {
      if ((c & ((1 << 16) - 1)) != 0) {
        return Status::Error("Wrong padding in the string");
      }
    } else {
      output += static_cast<char>(static_cast<unsigned char>(c >> 8));  // implementation-defined
      if (left == 3) {
        if ((c & ((1 << 8) - 1)) != 0) {
          return Status::Error("Wrong padding in the string");
        }
      } else {
        output += static_cast<char>(static_cast<unsigned char>(c));  // implementation-defined
      }
    }
  }
  return output;
}

static const char *const url_symbols64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

string base64url_encode(Slice input) {
  string base64;
  base64.reserve((input.size() + 2) / 3 * 4);
  for (size_t i = 0; i < input.size();) {
    size_t left = min(input.size() - i, static_cast<size_t>(3));
    int c = input.ubegin()[i++] << 16;
    base64 += url_symbols64[c >> 18];
    if (left != 1) {
      c |= input.ubegin()[i++] << 8;
    }
    base64 += url_symbols64[(c >> 12) & 63];
    if (left == 3) {
      c |= input.ubegin()[i++];
    }
    if (left != 1) {
      base64 += url_symbols64[(c >> 6) & 63];
    }
    if (left == 3) {
      base64 += url_symbols64[c & 63];
    }
  }
  return base64;
}

static unsigned char url_char_to_value[256];
static void init_base64url_table() {
  static bool is_inited = []() {
    std::fill(std::begin(url_char_to_value), std::end(url_char_to_value), 64);
    for (unsigned char i = 0; i < 64; i++) {
      url_char_to_value[static_cast<size_t>(url_symbols64[i])] = i;
    }
    return true;
  }();
  CHECK(is_inited);
}

Result<string> base64url_decode(Slice base64) {
  init_base64url_table();

  size_t padding_length = 0;
  while (!base64.empty() && base64.back() == '=') {
    base64.remove_suffix(1);
    padding_length++;
  }
  if (padding_length >= 3 || (padding_length > 0 && ((base64.size() + padding_length) & 3) != 0)) {
    return Status::Error("Wrong string padding");
  }

  if ((base64.size() & 3) == 1) {
    return Status::Error("Wrong string length");
  }

  string output;
  output.reserve(((base64.size() + 3) >> 2) * 3);
  for (size_t i = 0; i < base64.size();) {
    size_t left = min(base64.size() - i, static_cast<size_t>(4));
    int c = 0;
    for (size_t t = 0; t < left; t++) {
      auto value = url_char_to_value[base64.ubegin()[i++]];
      if (value == 64) {
        return Status::Error("Wrong character in the string");
      }
      c |= value << ((3 - t) * 6);
    }
    output += static_cast<char>(static_cast<unsigned char>(c >> 16));  // implementation-defined
    if (left == 2) {
      if ((c & ((1 << 16) - 1)) != 0) {
        return Status::Error("Wrong padding in the string");
      }
    } else {
      output += static_cast<char>(static_cast<unsigned char>(c >> 8));  // implementation-defined
      if (left == 3) {
        if ((c & ((1 << 8) - 1)) != 0) {
          return Status::Error("Wrong padding in the string");
        }
      } else {
        output += static_cast<char>(static_cast<unsigned char>(c));  // implementation-defined
      }
    }
  }
  return output;
}

template <bool is_url>
static bool is_base64_impl(Slice input) {
  size_t padding_length = 0;
  while (!input.empty() && input.back() == '=') {
    input.remove_suffix(1);
    padding_length++;
  }
  if (padding_length >= 3) {
    return false;
  }
  if ((!is_url || padding_length > 0) && ((input.size() + padding_length) & 3) != 0) {
    return false;
  }
  if (is_url && (input.size() & 3) == 1) {
    return false;
  }

  unsigned char *table;
  if (is_url) {
    init_base64url_table();
    table = url_char_to_value;
  } else {
    init_base64_table();
    table = char_to_value;
  }
  for (auto c : input) {
    if (table[static_cast<unsigned char>(c)] == 64) {
      return false;
    }
  }

  if ((input.size() & 3) == 2) {
    auto value = table[static_cast<int>(input.back())];
    if ((value & 15) != 0) {
      return false;
    }
  }
  if ((input.size() & 3) == 3) {
    auto value = table[static_cast<int>(input.back())];
    if ((value & 3) != 0) {
      return false;
    }
  }

  return true;
}

bool is_base64(Slice input) {
  return is_base64_impl<false>(input);
}

bool is_base64url(Slice input) {
  return is_base64_impl<true>(input);
}

string base64_filter(Slice input) {
  string res;
  res.reserve(input.size());
  init_base64_table();
  for (auto c : input) {
    if (char_to_value[static_cast<unsigned char>(c)] != 64 || c == '=') {
      res += c;
    }
  }
  return res;
}

}  // namespace td
