//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/td_api.h"

#include "td/utils/common.h"
#include "td/utils/HashTableUtils.h"
#include "td/utils/StringBuilder.h"

namespace td {

class Dependencies;
class MessageForwardInfo;
class Td;

class SavedMessagesTopicId {
  DialogId dialog_id_;

  friend struct SavedMessagesTopicIdHash;

  friend StringBuilder &operator<<(StringBuilder &string_builder, SavedMessagesTopicId saved_messages_topic_id);

 public:
  SavedMessagesTopicId() = default;

  explicit SavedMessagesTopicId(DialogId dialog_id) : dialog_id_(dialog_id) {
  }

  SavedMessagesTopicId(DialogId my_dialog_id, const MessageForwardInfo *message_forward_info);

  bool is_valid() const {
    return dialog_id_.is_valid();
  }

  td_api::object_ptr<td_api::SavedMessagesTopic> get_saved_messages_topic_object(Td *td) const;

  void add_dependencies(Dependencies &dependencies) const;

  bool operator==(const SavedMessagesTopicId &other) const {
    return dialog_id_ == other.dialog_id_;
  }

  bool operator!=(const SavedMessagesTopicId &other) const {
    return dialog_id_ != other.dialog_id_;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    dialog_id_.store(storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    dialog_id_.parse(parser);
  }
};

struct SavedMessagesTopicIdHash {
  uint32 operator()(SavedMessagesTopicId saved_messages_topic_id) const {
    return Hash<DialogId>()(saved_messages_topic_id.dialog_id_);
  }
};

StringBuilder &operator<<(StringBuilder &string_builder, SavedMessagesTopicId saved_messages_topic_id);

}  // namespace td
