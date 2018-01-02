//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "tl_string_outputer.h"

namespace td {
namespace tl {

void tl_string_outputer::append(const std::string &str) {
  result += str;
}

const std::string &tl_string_outputer::get_result() const {
  return result;
}

}  // namespace tl
}  // namespace td
