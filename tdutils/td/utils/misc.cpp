// SPDX-FileCopyrightText: Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: BSL-1.0 AND MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
#include "td/utils/misc.h"

#include "td/utils/StackAllocator.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/utf8.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <locale.h>

namespace td {

namespace {

#if TD_WINDOWS
_locale_t get_to_double_c_locale() {
  static const auto c_locale = [] {
    auto locale = _create_locale(LC_NUMERIC, "C");
    CHECK(locale != nullptr);
    return locale;
  }();
  return c_locale;
}

double parse_to_double_c_locale(const char *begin, char **end) {
  return _strtod_l(begin, end, get_to_double_c_locale());
}
#else
locale_t get_to_double_c_locale() {
  static const auto c_locale = [] {
    auto locale = newlocale(LC_NUMERIC_MASK, "C", nullptr);
    CHECK(locale != nullptr);
    return locale;
  }();
  return c_locale;
}

double parse_to_double_c_locale(const char *begin, char **end) {
  return strtod_l(begin, end, get_to_double_c_locale());
}
#endif

}  // namespace

void replace_with_spaces(MutableSlice str, Slice characters) {
  for (auto &c : str) {
    if (characters.find(c) != Slice::npos) {
      c = ' ';
    }
  }
}

char *str_dup(Slice str) {
  auto *res = static_cast<char *>(std::malloc(str.size() + 1));
  if (res == nullptr) {
    return nullptr;
  }
  std::copy(str.begin(), str.end(), res);
  res[str.size()] = '\0';
  return res;
}

string implode(const vector<string> &v, char delimiter) {
  string result;
  for (size_t i = 0; i < v.size(); i++) {
    if (i != 0) {
      result += delimiter;
    }
    result += v[i];
  }
  return result;
}

string lpad(string str, size_t size, char c) {
  if (str.size() >= size) {
    return str;
  }
  return string(size - str.size(), c) + str;
}

string lpad0(string str, size_t size) {
  return lpad(std::move(str), size, '0');
}

string rpad(string str, size_t size, char c) {
  if (str.size() >= size) {
    return str;
  }
  return str + string(size - str.size(), c);
}

string oneline(Slice str) {
  string result;
  result.reserve(str.size());
  bool after_new_line = true;
  for (auto c : str) {
    if (c != '\n' && c != '\r') {
      if (after_new_line) {
        if (c == ' ') {
          continue;
        }
        after_new_line = false;
      }
      result += c;
    } else if (!after_new_line) {
      after_new_line = true;
      result += ' ';
    }
  }
  while (!result.empty() && result.back() == ' ') {
    result.pop_back();
  }
  return result;
}

namespace detail {
Status get_to_integer_safe_error(Slice str) {
  auto error_message = PSTRING() << "Can't parse as an integer string \"" << str << '"';
  if (!check_utf8(error_message)) {
    return Status::Error("Strings must be encoded in UTF-8");
  }
  return Status::Error(error_message);
}
}  // namespace detail

double to_double(Slice str) {
  auto input = str.str();
  auto *begin = input.c_str();
  while (*begin == ' ' || *begin == '\t' || *begin == '\n' || *begin == '\r' || *begin == '\f' || *begin == '\v') {
    begin++;
  }

  const auto is_ascii_space = [](char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
  };

  auto *token_begin = begin;
  const bool has_explicit_sign = *token_begin == '+' || *token_begin == '-';
  if (*token_begin == '+' || *token_begin == '-') {
    token_begin++;
  }
  if (token_begin[0] == '0' && (token_begin[1] == 'x' || token_begin[1] == 'X')) {
    return 0.0;
  }

  char *end = nullptr;
  auto result = parse_to_double_c_locale(begin, &end);
  if (end == begin) {
    return 0.0;
  }
  if (*end == 'e' || *end == 'E' || *end == 'p' || *end == 'P') {
    return 0.0;
  }

  const auto parsed_token_len = static_cast<size_t>(end - token_begin);
  const bool starts_with_inf = parsed_token_len >= 3 && static_cast<char>(token_begin[0] | 32) == 'i' &&
                               static_cast<char>(token_begin[1] | 32) == 'n' &&
                               static_cast<char>(token_begin[2] | 32) == 'f';
  const bool starts_with_nan = parsed_token_len >= 3 && static_cast<char>(token_begin[0] | 32) == 'n' &&
                               static_cast<char>(token_begin[1] | 32) == 'a' &&
                               static_cast<char>(token_begin[2] | 32) == 'n';
  if (starts_with_inf || starts_with_nan) {
    if (starts_with_nan && has_explicit_sign) {
      return 0.0;
    }
    if (parsed_token_len != 3) {
      return 0.0;
    }
    if (*end != '\0' && !is_ascii_space(*end)) {
      return 0.0;
    }
  }
  if (std::isinf(result)) {
    const bool is_explicit_inf = starts_with_inf && parsed_token_len == 3;
    if (!is_explicit_inf) {
      return result > 0 ? std::numeric_limits<double>::max() : -std::numeric_limits<double>::max();
    }
  }

  return result;
}

Result<string> hex_decode(Slice hex) {
  if (hex.size() % 2 != 0) {
    return Status::Error("Wrong hex string length");
  }
  string result(hex.size() / 2, '\0');
  for (size_t i = 0; i < result.size(); i++) {
    int high = hex_to_int(hex[i + i]);
    int low = hex_to_int(hex[i + i + 1]);
    if (high == 16 || low == 16) {
      return Status::Error("Wrong hex string");
    }
    result[i] = static_cast<char>(high * 16 + low);  // TODO implementation-defined
  }
  return std::move(result);
}

string hex_encode(Slice data) {
  const char *hex = "0123456789abcdef";
  string res;
  res.reserve(2 * data.size());
  for (unsigned char c : data) {
    res.push_back(hex[c >> 4]);
    res.push_back(hex[c & 15]);
  }
  return res;
}

static bool is_url_char(char c) {
  return is_alnum(c) || c == '-' || c == '.' || c == '_' || c == '~';
}

string url_encode(Slice data) {
  size_t length = 3 * data.size();
  for (auto c : data) {
    length -= 2 * is_url_char(c);
  }
  if (length == data.size()) {
    return data.str();
  }
  string result;
  result.reserve(length);
  for (auto c : data) {
    if (is_url_char(c)) {
      result += c;
    } else {
      auto ch = static_cast<unsigned char>(c);
      result += '%';
      result += "0123456789ABCDEF"[ch / 16];
      result += "0123456789ABCDEF"[ch % 16];
    }
  }
  CHECK(result.size() == length);
  return result;
}

size_t url_decode(Slice from, MutableSlice to, bool decode_plus_sign_as_space) {
  size_t to_i = 0;
  CHECK(to.size() >= from.size());
  for (size_t from_i = 0, n = from.size(); from_i < n; from_i++) {
    if (from[from_i] == '%' && from_i + 2 < n) {
      int high = hex_to_int(from[from_i + 1]);
      int low = hex_to_int(from[from_i + 2]);
      if (high < 16 && low < 16) {
        to[to_i++] = static_cast<char>(high * 16 + low);
        from_i += 2;
        continue;
      }
    }
    to[to_i++] = decode_plus_sign_as_space && from[from_i] == '+' ? ' ' : from[from_i];
  }
  return to_i;
}

string url_decode(Slice from, bool decode_plus_sign_as_space) {
  string to;
  to.resize(from.size());
  to.resize(url_decode(from, to, decode_plus_sign_as_space));
  return to;
}

MutableSlice url_decode_inplace(MutableSlice str, bool decode_plus_sign_as_space) {
  size_t result_size = url_decode(str, str, decode_plus_sign_as_space);
  str.truncate(result_size);
  return str;
}

string buffer_to_hex(Slice buffer) {
  const char *hex = "0123456789ABCDEF";
  string res(2 * buffer.size(), '\0');
  for (std::size_t i = 0; i < buffer.size(); i++) {
    auto c = buffer.ubegin()[i];
    res[2 * i] = hex[c & 15];
    res[2 * i + 1] = hex[c >> 4];
  }
  return res;
}

string zero_encode(Slice data) {
  auto buffer = StackAllocator::alloc(1024);
  auto res = StringBuilder(buffer.as_slice(), true);
  for (size_t n = data.size(), i = 0; i < n; i++) {
    res.push_back(data[i]);
    if (data[i] == 0) {
      unsigned char cnt = 1;
      while (cnt < 250 && i + cnt < n && data[i + cnt] == data[i]) {
        cnt++;
      }
      res.push_back(static_cast<char>(cnt));
      i += cnt - 1;
    }
  }
  return res.as_cslice().str();
}

string zero_decode(Slice data) {
  auto buffer = StackAllocator::alloc(1024);
  auto res = StringBuilder(buffer.as_slice(), true);
  for (size_t n = data.size(), i = 0; i < n; i++) {
    if (data[i] == 0 && i + 1 < n) {
      for (int cnt = static_cast<unsigned char>(data[i + 1]); cnt > 0; cnt--) {
        res.push_back(data[i]);
      }
      i++;
      continue;
    }
    res.push_back(data[i]);
  }
  return res.as_cslice().str();
}

string zero_one_encode(Slice data) {
  auto buffer = StackAllocator::alloc(1024);
  auto res = StringBuilder(buffer.as_slice(), true);
  for (size_t n = data.size(), i = 0; i < n; i++) {
    res.push_back(data[i]);
    auto c = static_cast<unsigned char>(data[i]);
    if (c == 0 || c == 0xff) {
      unsigned char cnt = 1;
      while (cnt < 250 && i + cnt < n && data[i + cnt] == data[i]) {
        cnt++;
      }
      res.push_back(static_cast<char>(cnt));
      i += cnt - 1;
    }
  }
  return res.as_cslice().str();
}

string zero_one_decode(Slice data) {
  auto buffer = StackAllocator::alloc(1024);
  auto res = StringBuilder(buffer.as_slice(), true);
  for (size_t n = data.size(), i = 0; i < n; i++) {
    auto c = static_cast<unsigned char>(data[i]);
    if ((c == 0 || c == 0xff) && i + 1 < n) {
      for (int cnt = static_cast<unsigned char>(data[i + 1]); cnt > 0; cnt--) {
        res.push_back(data[i]);
      }
      i++;
      continue;
    }
    res.push_back(data[i]);
  }
  return res.as_cslice().str();
}

}  // namespace td
