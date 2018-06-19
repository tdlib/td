//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/StringBuilder.h"

namespace td {

class HttpUrl {
 public:
  enum class Protocol { HTTP, HTTPS } protocol_;
  string userinfo_;
  string host_;
  bool is_ipv6_;
  int specified_port_;
  int port_;
  string query_;

  string get_url() const;
};

// TODO Slice instead of MutableSlice
Result<HttpUrl> parse_url(MutableSlice url,
                          HttpUrl::Protocol default_protocol = HttpUrl::Protocol::HTTP) TD_WARN_UNUSED_RESULT;

StringBuilder &operator<<(StringBuilder &sb, const HttpUrl &url);

string get_url_query_file_name(const string &query);

string get_url_file_name(const string &url);

}  // namespace td
