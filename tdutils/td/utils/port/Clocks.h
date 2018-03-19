//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

namespace td {

class ClocksBase {
 public:
  using Duration = double;
};

// TODO: (maybe) write system specific functions.
class ClocksDefault {
 public:
  using Duration = ClocksBase::Duration;

  static Duration monotonic();

  static Duration system();
};

using Clocks = ClocksDefault;

}  // namespace td
