//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/tl_parsers.h"

namespace td {

alignas(4) const unsigned char TlParser::empty_data[sizeof(UInt256)] = {};  // static zero-initialized

void TlParser::set_error(const string &error_message) {
  if (error.empty()) {
    CHECK(!error_message.empty());
    error = error_message;
    error_pos = data_len - left_len;
    data = empty_data;
    left_len = 0;
    data_len = 0;
  } else {
    data = empty_data;
    CHECK(error_pos != std::numeric_limits<size_t>::max());
    CHECK(data_len == 0) << data_len << " " << left_len << " " << data << " " << &empty_data[0] << " " << error_pos
                         << " " << error;
    CHECK(left_len == 0);
  }
}

}  // namespace td
