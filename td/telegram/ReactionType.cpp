//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ReactionType.h"

#include "td/telegram/ConfigManager.h"
#include "td/telegram/misc.h"
#include "td/telegram/OptionManager.h"
#include "td/telegram/StickersManager.h"
#include "td/telegram/Td.h"

#include "td/utils/as.h"
#include "td/utils/base64.h"
#include "td/utils/buffer.h"
#include "td/utils/crypto.h"
#include "td/utils/emoji.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/Status.h"
#include "td/utils/utf8.h"

namespace td {

class SetDefaultReactionQuery final : public Td::ResultHandler {
  ReactionType reaction_type_;

 public:
  void send(const ReactionType &reaction_type) {
    reaction_type_ = reaction_type;
    send_query(
        G()->net_query_creator().create(telegram_api::messages_setDefaultReaction(reaction_type.get_input_reaction())));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_setDefaultReaction>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    if (!result_ptr.ok()) {
      return on_error(Status::Error(400, "Receive false"));
    }

    auto default_reaction = td_->option_manager_->get_option_string("default_reaction", "-");
    if (default_reaction != reaction_type_.get_string()) {
      send_set_default_reaction_query(td_);
    } else {
      td_->option_manager_->set_option_empty("default_reaction_needs_sync");
    }
  }

  void on_error(Status status) final {
    if (G()->close_flag()) {
      return;
    }

    LOG(INFO) << "Receive error for SetDefaultReactionQuery: " << status;
    td_->option_manager_->set_option_empty("default_reaction_needs_sync");
    send_closure(G()->config_manager(), &ConfigManager::request_config, false);
  }
};

static int64 get_custom_emoji_id(const string &reaction) {
  auto r_decoded = base64_decode(Slice(&reaction[1], reaction.size() - 1));
  CHECK(r_decoded.is_ok());
  CHECK(r_decoded.ok().size() == 8);
  return as<int64>(r_decoded.ok().c_str());
}

static string get_custom_emoji_string(int64 custom_emoji_id) {
  char s[8];
  as<int64>(&s) = custom_emoji_id;
  return PSTRING() << '#' << base64_encode(Slice(s, 8));
}

ReactionType::ReactionType(string &&emoji) : reaction_(std::move(emoji)) {
}

ReactionType::ReactionType(const telegram_api::object_ptr<telegram_api::Reaction> &reaction) {
  if (reaction == nullptr) {
    return;
  }
  switch (reaction->get_id()) {
    case telegram_api::reactionEmpty::ID:
      break;
    case telegram_api::reactionEmoji::ID:
      reaction_ = static_cast<const telegram_api::reactionEmoji *>(reaction.get())->emoticon_;
      if (is_custom_reaction()) {
        reaction_ = string();
      }
      break;
    case telegram_api::reactionCustomEmoji::ID:
      reaction_ =
          get_custom_emoji_string(static_cast<const telegram_api::reactionCustomEmoji *>(reaction.get())->document_id_);
      break;
    default:
      UNREACHABLE();
      break;
  }
}

ReactionType::ReactionType(const td_api::object_ptr<td_api::ReactionType> &type) {
  if (type == nullptr) {
    return;
  }
  switch (type->get_id()) {
    case td_api::reactionTypeEmoji::ID: {
      const string &emoji = static_cast<const td_api::reactionTypeEmoji *>(type.get())->emoji_;
      if (!check_utf8(emoji)) {
        break;
      }
      reaction_ = emoji;
      if (is_custom_reaction()) {
        reaction_ = string();
        break;
      }
      break;
    }
    case td_api::reactionTypeCustomEmoji::ID:
      reaction_ =
          get_custom_emoji_string(static_cast<const td_api::reactionTypeCustomEmoji *>(type.get())->custom_emoji_id_);
      break;
    default:
      UNREACHABLE();
      break;
  }
}

telegram_api::object_ptr<telegram_api::Reaction> ReactionType::get_input_reaction() const {
  if (is_empty()) {
    return telegram_api::make_object<telegram_api::reactionEmpty>();
  }
  if (is_custom_reaction()) {
    return telegram_api::make_object<telegram_api::reactionCustomEmoji>(get_custom_emoji_id(reaction_));
  }
  return telegram_api::make_object<telegram_api::reactionEmoji>(reaction_);
}

td_api::object_ptr<td_api::ReactionType> ReactionType::get_reaction_type_object() const {
  CHECK(!is_empty());
  if (is_custom_reaction()) {
    return td_api::make_object<td_api::reactionTypeCustomEmoji>(get_custom_emoji_id(reaction_));
  }
  return td_api::make_object<td_api::reactionTypeEmoji>(reaction_);
}

td_api::object_ptr<td_api::updateDefaultReactionType> ReactionType::get_update_default_reaction_type() const {
  if (is_empty()) {
    return nullptr;
  }
  return td_api::make_object<td_api::updateDefaultReactionType>(get_reaction_type_object());
}

bool ReactionType::is_custom_reaction() const {
  return reaction_[0] == '#';
}

bool ReactionType::is_active_reaction(
    const FlatHashMap<ReactionType, size_t, ReactionTypeHash> &active_reaction_pos) const {
  return !is_empty() && (is_custom_reaction() || active_reaction_pos.count(*this) > 0);
}

bool operator<(const ReactionType &lhs, const ReactionType &rhs) {
  return lhs.reaction_ < rhs.reaction_;
}

bool operator==(const ReactionType &lhs, const ReactionType &rhs) {
  return lhs.reaction_ == rhs.reaction_;
}

StringBuilder &operator<<(StringBuilder &string_builder, const ReactionType &reaction_type) {
  if (reaction_type.is_empty()) {
    return string_builder << "empty reaction";
  }
  if (reaction_type.is_custom_reaction()) {
    return string_builder << "custom reaction " << get_custom_emoji_id(reaction_type.reaction_);
  }
  return string_builder << "reaction " << reaction_type.reaction_;
}

void set_default_reaction(Td *td, ReactionType reaction_type, Promise<Unit> &&promise) {
  if (reaction_type.is_empty()) {
    return promise.set_error(Status::Error(400, "Default reaction must be non-empty"));
  }
  if (!reaction_type.is_custom_reaction() && !td->stickers_manager_->is_active_reaction(reaction_type)) {
    return promise.set_error(Status::Error(400, "Can't set incative reaction as default"));
  }

  if (td->option_manager_->get_option_string("default_reaction", "-") != reaction_type.get_string()) {
    td->option_manager_->set_option_string("default_reaction", reaction_type.get_string());
    if (!td->option_manager_->get_option_boolean("default_reaction_needs_sync")) {
      td->option_manager_->set_option_boolean("default_reaction_needs_sync", true);
      send_set_default_reaction_query(td);
    }
  }
  promise.set_value(Unit());
}

void send_set_default_reaction_query(Td *td) {
  td->create_handler<SetDefaultReactionQuery>()->send(
      ReactionType(td->option_manager_->get_option_string("default_reaction")));
}

vector<ReactionType> get_recent_reactions(Td *td) {
  return td->stickers_manager_->get_recent_reactions();
}

vector<ReactionType> get_top_reactions(Td *td) {
  return td->stickers_manager_->get_top_reactions();
}

void add_recent_reaction(Td *td, const ReactionType &reaction_type) {
  td->stickers_manager_->add_recent_reaction(reaction_type);
}

int64 get_reaction_types_hash(const vector<ReactionType> &reaction_types) {
  vector<uint64> numbers;
  for (auto &reaction_type : reaction_types) {
    if (reaction_type.is_custom_reaction()) {
      auto custom_emoji_id = static_cast<uint64>(get_custom_emoji_id(reaction_type.get_string()));
      numbers.push_back(custom_emoji_id >> 32);
      numbers.push_back(custom_emoji_id & 0xFFFFFFFF);
    } else {
      auto emoji = remove_emoji_selectors(reaction_type.get_string());
      unsigned char hash[16];
      md5(emoji, {hash, sizeof(hash)});
      auto get = [hash](int num) {
        return static_cast<uint32>(hash[num]);
      };

      numbers.push_back(0);
      numbers.push_back(static_cast<int32>((get(0) << 24) + (get(1) << 16) + (get(2) << 8) + get(3)));
    }
  }
  return get_vector_hash(numbers);
}

}  // namespace td
