//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/StringBuilder.h"

#include <functional>

namespace td {

class OptionsParser {
  class Option {
   public:
    enum class Type { NoArg, Arg, OptionalArg };
    Type type;
    char short_key;
    std::string long_key;
    std::string description;
    std::function<Status(Slice)> arg_callback;
  };

 public:
  void set_description(std::string description);

  void add_option(Option::Type type, char short_key, Slice long_key, Slice description,
                  std::function<Status(Slice)> callback);

  void add_option(char short_key, Slice long_key, Slice description, std::function<Status(Slice)> callback);

  void add_option(char short_key, Slice long_key, Slice description, std::function<Status(void)> callback);

  Result<int> run(int argc, char *argv[]) TD_WARN_UNUSED_RESULT;

  friend StringBuilder &operator<<(StringBuilder &sb, const OptionsParser &o);

 private:
  std::vector<Option> options_;
  std::string description_;
};

}  // namespace td
