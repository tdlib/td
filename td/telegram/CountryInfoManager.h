//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/actor/PromiseFuture.h"

#include "td/utils/common.h"

namespace td {

class Td;

class CountryInfoManager {
 public:
  explicit CountryInfoManager(Td *td);

  void get_current_country_code(Promise<string> &&promise);

 private:
  Td *td_;
};

}  // namespace td
