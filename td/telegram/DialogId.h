//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/telegram_api.h"

#include "td/telegram/ChannelId.h"
#include "td/telegram/ChatId.h"
#include "td/telegram/SecretChatId.h"
#include "td/telegram/UserId.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

#include <functional>
#include <type_traits>

namespace td {

enum class DialogType : int32 { None, User, Chat, Channel, SecretChat };

class DialogId {
  static constexpr int64 MIN_SECRET_ID = -2002147483648ll;
  static constexpr int64 ZERO_SECRET_ID = -2000000000000ll;
  static constexpr int64 MAX_SECRET_ID = -1997852516353ll;
  static constexpr int64 MIN_CHANNEL_ID = -1002147483647ll;
  static constexpr int64 MAX_CHANNEL_ID = -1000000000000ll;
  static constexpr int64 MIN_CHAT_ID = -2147483647ll;
  static constexpr int64 MAX_USER_ID = 2147483647ll;

  int64 id = 0;

  static int64 get_peer_id(const tl_object_ptr<telegram_api::Peer> &peer);

 public:
  DialogId() = default;

  explicit DialogId(int64 dialog_id) : id(dialog_id) {
  }
  template <class T, typename = std::enable_if_t<std::is_convertible<T, int64>::value>>
  DialogId(T dialog_id) = delete;

  explicit DialogId(const tl_object_ptr<telegram_api::DialogPeer> &dialog_peer);
  explicit DialogId(const tl_object_ptr<telegram_api::Peer> &peer);
  explicit DialogId(UserId user_id);
  explicit DialogId(ChatId chat_id);
  explicit DialogId(ChannelId channel_id);
  explicit DialogId(SecretChatId chat_id);

  int64 get() const {
    return id;
  }

  bool operator==(const DialogId &other) const {
    return id == other.id;
  }

  bool operator!=(const DialogId &other) const {
    return id != other.id;
  }

  bool is_valid() const;

  DialogType get_type() const;

  UserId get_user_id() const;
  ChatId get_chat_id() const;
  ChannelId get_channel_id() const;
  SecretChatId get_secret_chat_id() const;

  template <class StorerT>
  void store(StorerT &storer) const {
    storer.store_long(id);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    id = parser.fetch_long();
  }
};

struct DialogIdHash {
  std::size_t operator()(DialogId dialog_id) const {
    return std::hash<int64>()(dialog_id.get());
  }
};

inline StringBuilder &operator<<(StringBuilder &string_builder, DialogId dialog_id) {
  return string_builder << "chat " << dialog_id.get();
}

}  // namespace td
