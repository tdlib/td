//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/tl/tl_outputer.h"

#include <string>
#include <vector>

namespace td {
namespace tl {

class tl_string_outputer : public tl_outputer {
  std::vector<char> result;

 public:
  tl_string_outputer();

  virtual void append(const std::string &str);

  std::string get_result() const;
};

}  // namespace tl
}  // namespace td
