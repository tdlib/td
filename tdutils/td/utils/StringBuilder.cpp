//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/StringBuilder.h"

#include "td/utils/misc.h"
#include "td/utils/port/thread_local.h"

#include <cstdio>
#include <limits>
#include <locale>
#include <sstream>
#include <utility>

namespace td {

template <class T>
static char *print_uint(char *current_ptr, T x) {
  if (x < 100) {
    if (x < 10) {
      *current_ptr++ = static_cast<char>('0' + x);
    } else {
      *current_ptr++ = static_cast<char>('0' + x / 10);
      *current_ptr++ = static_cast<char>('0' + x % 10);
    }
    return current_ptr;
  }

  auto begin_ptr = current_ptr;
  do {
    *current_ptr++ = static_cast<char>('0' + x % 10);
    x /= 10;
  } while (x > 0);

  auto end_ptr = current_ptr - 1;
  while (begin_ptr < end_ptr) {
    std::swap(*begin_ptr++, *end_ptr--);
  }

  return current_ptr;
}

template <class T>
static char *print_int(char *current_ptr, T x) {
  if (x < 0) {
    if (x == std::numeric_limits<T>::min()) {
      std::stringstream ss;
      ss << x;
      auto len = narrow_cast<int>(static_cast<std::streamoff>(ss.tellp()));
      ss.read(current_ptr, len);
      return current_ptr + len;
    }

    *current_ptr++ = '-';
    x = -x;
  }

  return print_uint(current_ptr, x);
}

StringBuilder &StringBuilder::operator<<(int x) {
  if (unlikely(end_ptr_ < current_ptr_)) {
    return on_error();
  }
  current_ptr_ = print_int(current_ptr_, x);
  return *this;
}

StringBuilder &StringBuilder::operator<<(unsigned int x) {
  if (unlikely(end_ptr_ < current_ptr_)) {
    return on_error();
  }
  current_ptr_ = print_uint(current_ptr_, x);
  return *this;
}

StringBuilder &StringBuilder::operator<<(long int x) {
  if (unlikely(end_ptr_ < current_ptr_)) {
    return on_error();
  }
  current_ptr_ = print_int(current_ptr_, x);
  return *this;
}

StringBuilder &StringBuilder::operator<<(long unsigned int x) {
  if (unlikely(end_ptr_ < current_ptr_)) {
    return on_error();
  }
  current_ptr_ = print_uint(current_ptr_, x);
  return *this;
}

StringBuilder &StringBuilder::operator<<(long long int x) {
  if (unlikely(end_ptr_ < current_ptr_)) {
    return on_error();
  }
  current_ptr_ = print_int(current_ptr_, x);
  return *this;
}

StringBuilder &StringBuilder::operator<<(long long unsigned int x) {
  if (unlikely(end_ptr_ < current_ptr_)) {
    return on_error();
  }
  current_ptr_ = print_uint(current_ptr_, x);
  return *this;
}

StringBuilder &StringBuilder::operator<<(FixedDouble x) {
  if (unlikely(end_ptr_ < current_ptr_)) {
    return on_error();
  }

  static TD_THREAD_LOCAL std::stringstream *ss;
  if (init_thread_local<std::stringstream>(ss)) {
    ss->imbue(std::locale::classic());
    ss->setf(std::ios_base::fixed, std::ios_base::floatfield);
  } else {
    ss->str(std::string());
    ss->clear();
  }
  ss->precision(x.precision);
  *ss << x.d;

  int len = narrow_cast<int>(static_cast<std::streamoff>(ss->tellp()));
  auto left = end_ptr_ + reserved_size - current_ptr_;
  if (unlikely(len >= left)) {
    error_flag_ = true;
    len = left ? narrow_cast<int>(left - 1) : 0;
  }
  ss->read(current_ptr_, len);
  current_ptr_ += len;
  return *this;
}

StringBuilder &StringBuilder::operator<<(const void *ptr) {
  if (unlikely(end_ptr_ < current_ptr_)) {
    return on_error();
  }
  current_ptr_ += std::snprintf(current_ptr_, reserved_size, "%p", ptr);
  return *this;
}

}  // namespace td
