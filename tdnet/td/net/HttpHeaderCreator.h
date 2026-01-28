//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/HttpUrl.h"
#include "td/utils/Slice.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/Status.h"
#include "td/utils/StringBuilder.h"

namespace td {

class HttpHeaderCreator {
  static constexpr size_t MAX_HEADER = 4096;

  void init() {
    sb_ = StringBuilder(MutableSlice{header_, MAX_HEADER});
  }

 public:
  HttpHeaderCreator() : sb_(MutableSlice{header_, MAX_HEADER}) {
  }

  void init_ok() {
    init();
    sb_ << "HTTP/1.1 200 OK\r\n";
  }

  void init_get(Slice url) {
    init();
    sb_ << "GET " << url << " HTTP/1.1\r\n";
  }

  void init_post(Slice url) {
    init();
    sb_ << "POST " << url << " HTTP/1.1\r\n";
  }

  void init_error(int code, Slice reason) {
    init();
    sb_ << "HTTP/1.1 " << code << ' ' << reason << "\r\n";
  }

  void init_status_line(int http_status_code) {
    init_error(http_status_code, get_status_line(http_status_code));
  }

  void add_header(Slice key, Slice value) {
    sb_ << key << ": " << value << "\r\n";
  }

  void set_content_type(Slice type) {
    add_header("Content-Type", type);
  }

  void set_content_size(size_t size) {
    add_header("Content-Length", PSLICE() << size);
  }

  void set_keep_alive() {
    add_header("Connection", "keep-alive");
  }

  void add_host_header(HttpUrl::Protocol protocol, Slice host, int32 port);

  static string get_host_header(HttpUrl::Protocol protocol, Slice host, int32 port);

  Result<Slice> finish(Slice content = {}) TD_WARN_UNUSED_RESULT;

 private:
  static CSlice get_status_line(int http_status_code);

  char header_[MAX_HEADER];
  StringBuilder sb_;
};

}  // namespace td
