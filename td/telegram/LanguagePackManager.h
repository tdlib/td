//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/net/NetQuery.h"

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/utils/Container.h"
#include "td/utils/Status.h"

namespace td {

class LanguagePackManager : public NetQueryCallback {
 public:
  explicit LanguagePackManager(ActorShared<> parent) : parent_(std::move(parent)) {
  }

  void on_language_pack_changed();

  void on_language_code_changed();

  void on_language_pack_version_changed();

  void get_languages(Promise<td_api::object_ptr<td_api::languagePack>> promise);

  void get_language_pack_strings(string language_code, vector<string> keys,
                                 Promise<td_api::object_ptr<td_api::languagePackStrings>> promise);

 private:
  ActorShared<> parent_;

  string language_pack_;
  string language_code_;
  uint32 generation_ = 0;

  int32 language_pack_version_ = -1;

  void inc_generation();

  void on_get_language_pack_strings(Result<vector<tl_object_ptr<telegram_api::LangPackString>>> r_result, bool ia_all,
                                    Promise<td_api::object_ptr<td_api::languagePackStrings>> promise);

  void on_result(NetQueryPtr query) override;

  void start_up() override;
  void hangup() override;

  Container<Promise<NetQueryPtr>> container_;
  void send_with_promise(NetQueryPtr query, Promise<NetQueryPtr> promise);
};

}  // namespace td
