//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/tl/tl_string_outputer.h"

#include <cstring>

#if defined(__has_feature)
#if __has_feature(memory_sanitizer)
#include <sanitizer/msan_interface.h>
#define TD_TL_STRING_OUTPUTER_MSAN_ACTIVE 1
#endif
#endif
#if defined(__SANITIZE_MEMORY__)
#include <sanitizer/msan_interface.h>
#define TD_TL_STRING_OUTPUTER_MSAN_ACTIVE 1
#endif
#ifndef TD_TL_STRING_OUTPUTER_MSAN_ACTIVE
#define TD_TL_STRING_OUTPUTER_MSAN_ACTIVE 0
#endif

namespace td {
namespace tl {

namespace {

template <class T>
void unpoison_object_if_msan(const T &value) {
#if TD_TL_STRING_OUTPUTER_MSAN_ACTIVE
  __msan_unpoison(const_cast<T *>(&value), sizeof(value));
#else
  (void)value;
#endif
}

void unpoison_if_msan(const std::string &value) {
#if TD_TL_STRING_OUTPUTER_MSAN_ACTIVE
  unpoison_object_if_msan(value);
  if (!value.empty()) {
    __msan_unpoison(const_cast<char *>(value.data()), value.size());
  }
  __msan_unpoison(const_cast<char *>(value.data() + value.size()), 1);
#else
  (void)value;
#endif
}

template <class T>
void unpoison_vector_data_if_msan(const std::vector<T> &value) {
#if TD_TL_STRING_OUTPUTER_MSAN_ACTIVE
  unpoison_object_if_msan(value);
  if (!value.empty()) {
    __msan_unpoison(const_cast<T *>(value.data()), value.size() * sizeof(value[0]));
  }
#else
  (void)value;
#endif
}

}  // namespace

tl_string_outputer::tl_string_outputer() {
  unpoison_object_if_msan(*this);
  unpoison_vector_data_if_msan(result);
}

void tl_string_outputer::append(const std::string &str) {
  unpoison_vector_data_if_msan(result);
  unpoison_if_msan(str);
  unpoison_object_if_msan(*this);

  const auto old_size = result.size();
  result.resize(old_size + str.size());
  if (!str.empty()) {
    std::memcpy(result.data() + old_size, str.data(), str.size());
  }
  unpoison_vector_data_if_msan(result);
}

std::string tl_string_outputer::get_result() const {
  unpoison_vector_data_if_msan(result);
  std::string result_string(result.size(), '\0');
  if (!result.empty()) {
    std::memcpy(result_string.data(), result.data(), result.size());
  }
  unpoison_if_msan(result_string);
#if defined(_WIN32)
  std::string fixed_result;
  for (std::size_t i = 0; i < result_string.size(); i++) {
    if (result_string[i] == '\n') {
      fixed_result += '\r';
    }
    fixed_result += result_string[i];
  }
  unpoison_if_msan(fixed_result);
  return fixed_result;
#else
  return result_string;
#endif
}

}  // namespace tl
}  // namespace td
