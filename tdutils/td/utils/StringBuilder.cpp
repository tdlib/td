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
#include <locale>
#include <sstream>

namespace td {

// TODO: optimize
StringBuilder &StringBuilder::operator<<(int x) {
  if (unlikely(end_ptr_ < current_ptr_)) {
    return on_error();
  }
  current_ptr_ += std::snprintf(current_ptr_, reserved_size, "%d", x);
  return *this;
}

StringBuilder &StringBuilder::operator<<(unsigned int x) {
  if (unlikely(end_ptr_ < current_ptr_)) {
    return on_error();
  }
  current_ptr_ += std::snprintf(current_ptr_, reserved_size, "%u", x);
  return *this;
}

StringBuilder &StringBuilder::operator<<(long int x) {
  if (unlikely(end_ptr_ < current_ptr_)) {
    return on_error();
  }
  current_ptr_ += std::snprintf(current_ptr_, reserved_size, "%ld", x);
  return *this;
}

StringBuilder &StringBuilder::operator<<(long unsigned int x) {
  if (unlikely(end_ptr_ < current_ptr_)) {
    return on_error();
  }
  current_ptr_ += std::snprintf(current_ptr_, reserved_size, "%lu", x);
  return *this;
}

StringBuilder &StringBuilder::operator<<(long long int x) {
  if (unlikely(end_ptr_ < current_ptr_)) {
    return on_error();
  }
  current_ptr_ += std::snprintf(current_ptr_, reserved_size, "%lld", x);
  return *this;
}

StringBuilder &StringBuilder::operator<<(long long unsigned int x) {
  if (unlikely(end_ptr_ < current_ptr_)) {
    return on_error();
  }
  current_ptr_ += std::snprintf(current_ptr_, reserved_size, "%llu", x);
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
