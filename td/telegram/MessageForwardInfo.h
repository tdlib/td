//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/MessageOrigin.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

namespace td {

class Td;

struct MessageForwardInfo {
  MessageOrigin origin;
  int32 date = 0;
  DialogId from_dialog_id;
  MessageId from_message_id;
  string psa_type;
  bool is_imported = false;

  MessageForwardInfo() = default;

  MessageForwardInfo(MessageOrigin &&origin, int32 date, DialogId from_dialog_id, MessageId from_message_id,
                     string &&psa_type, bool is_imported)
      : origin(std::move(origin))
      , date(date)
      , from_dialog_id(from_dialog_id)
      , from_message_id(from_message_id)
      , psa_type(std::move(psa_type))
      , is_imported(is_imported) {
  }

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const MessageForwardInfo &lhs, const MessageForwardInfo &rhs);

bool operator!=(const MessageForwardInfo &lhs, const MessageForwardInfo &rhs);

StringBuilder &operator<<(StringBuilder &string_builder, const MessageForwardInfo &forward_info);

}  // namespace td
