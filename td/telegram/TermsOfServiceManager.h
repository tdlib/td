//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/TermsOfService.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"
#include "td/utils/Promise.h"

#include <utility>

namespace td {

class Td;

class TermsOfServiceManager final : public Actor {
 public:
  TermsOfServiceManager(Td *td, ActorShared<> parent);

  void get_terms_of_service(Promise<std::pair<int32, TermsOfService>> promise);

  void accept_terms_of_service(string &&terms_of_service_id, Promise<Unit> &&promise);

 private:
  void tear_down() final;

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
