//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/mtproto/utils.h"

#include "td/mtproto/mtproto_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/logging.h"

namespace td {

TLStorer<mtproto_api::Function> create_storer(const mtproto_api::Function &function) {
  return TLStorer<mtproto_api::Function>(function);
}

TLStorer<telegram_api::Function> create_storer(const telegram_api::Function &function) {
  LOG(DEBUG) << "Create storer for " << to_string(function);
  return TLStorer<telegram_api::Function>(function);
}

TLObjectStorer<mtproto_api::Object> create_storer(const mtproto_api::Object &object) {
  return TLObjectStorer<mtproto_api::Object>(object);
}

TLObjectStorer<telegram_api::Object> create_storer(const telegram_api::Object &object) {
  return TLObjectStorer<telegram_api::Object>(object);
}

}  // namespace td
