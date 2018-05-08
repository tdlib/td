//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/net/ConnectionCreator.h"

#include "td/utils/common.h"
#include "td/utils/Slice.h"

namespace td {

class MtprotoHeader {
 public:
  struct Options {
    int32 api_id;
    string system_language_code;
    string device_model;
    string system_version;
    string application_version;
    Proxy proxy;
  };

  explicit MtprotoHeader(const Options &options) : options_(options) {
    gen_headers();
  }

  void set_proxy(Proxy proxy) {
    options_.proxy = proxy;
    default_header_ = gen_header(options_, false);
  }

  Slice get_default_header() const {
    return default_header_;
  }
  Slice get_anonymous_header() const {
    return anonymous_header_;
  }

 private:
  Options options_;
  string default_header_;
  string anonymous_header_;

  void gen_headers() {
    default_header_ = gen_header(options_, false);
    anonymous_header_ = gen_header(options_, true);
  }

  static string gen_header(const Options &options, bool is_anonymous);
};

}  // namespace td
