//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/EphemeralMessageId.h"

#include "td/utils/common.h"
#include "td/utils/HashTableUtils.h"
#include "td/utils/StringBuilder.h"

namespace td {

struct EphemeralMessageFullId {
 private:
  DialogId dialog_id;
  EphemeralMessageId ephemeral_message_id;

 public:
  EphemeralMessageFullId() : dialog_id(), ephemeral_message_id() {
  }

  EphemeralMessageFullId(DialogId dialog_id, EphemeralMessageId ephemeral_message_id)
      : dialog_id(dialog_id), ephemeral_message_id(ephemeral_message_id) {
  }

  bool operator==(const EphemeralMessageFullId &other) const {
    return dialog_id == other.dialog_id && ephemeral_message_id == other.ephemeral_message_id;
  }

  bool operator!=(const EphemeralMessageFullId &other) const {
    return !(*this == other);
  }

  bool is_valid() const {
    return dialog_id.is_valid() && ephemeral_message_id.is_valid();
  }

  DialogId get_dialog_id() const {
    return dialog_id;
  }

  EphemeralMessageId get_ephemeral_message_id() const {
    return ephemeral_message_id;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    dialog_id.store(storer);
    ephemeral_message_id.store(storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    dialog_id.parse(parser);
    ephemeral_message_id.parse(parser);
  }
};

struct EphemeralMessageFullIdHash {
  uint32 operator()(EphemeralMessageFullId ephemeral_message_full_id) const {
    return combine_hashes(DialogIdHash()(ephemeral_message_full_id.get_dialog_id()),
                          EphemeralMessageIdHash()(ephemeral_message_full_id.get_ephemeral_message_id()));
  }
};

inline StringBuilder &operator<<(StringBuilder &string_builder, EphemeralMessageFullId ephemeral_message_full_id) {
  return string_builder << ephemeral_message_full_id.get_ephemeral_message_id() << " in "
                        << ephemeral_message_full_id.get_dialog_id();
}

}  // namespace td
