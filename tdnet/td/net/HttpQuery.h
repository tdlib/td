//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/net/HttpFile.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/Slice.h"
#include "td/utils/StringBuilder.h"

#include <utility>

namespace td {

class HttpQuery {
 public:
  enum class Type : int8 { Empty, Get, Post, Response };

  td::vector<BufferSlice> container_;
  Type type_ = Type::Empty;
  int32 code_ = 0;
  MutableSlice url_path_;
  td::vector<std::pair<MutableSlice, MutableSlice>> args_;
  MutableSlice reason_;

  bool keep_alive_ = true;
  td::vector<std::pair<MutableSlice, MutableSlice>> headers_;
  td::vector<HttpFile> files_;
  MutableSlice content_;

  Slice get_header(Slice key) const;

  MutableSlice get_arg(Slice key) const;

  td::vector<std::pair<string, string>> get_args() const;

  int get_retry_after() const;
};

StringBuilder &operator<<(StringBuilder &sb, const HttpQuery &q);

}  // namespace td
