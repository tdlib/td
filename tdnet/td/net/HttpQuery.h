//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
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
  enum class Type : int8 { EMPTY, GET, POST, RESPONSE };

  std::vector<BufferSlice> container_;
  Type type_;
  MutableSlice url_path_;
  std::vector<std::pair<MutableSlice, MutableSlice>> args_;
  int code_;
  MutableSlice reason_;

  bool keep_alive_;
  std::vector<std::pair<MutableSlice, MutableSlice>> headers_;
  std::vector<HttpFile> files_;
  MutableSlice content_;

  Slice get_header(Slice key) const;

  MutableSlice get_arg(Slice key) const;

  std::vector<std::pair<string, string>> get_args() const;

  int get_retry_after() const;
};

using HttpQueryPtr = std::unique_ptr<HttpQuery>;

StringBuilder &operator<<(StringBuilder &sb, const HttpQuery &q);

}  // namespace td
