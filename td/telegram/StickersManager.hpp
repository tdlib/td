//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/StickersManager.h"

#include "td/telegram/files/FileId.hpp"
#include "td/telegram/misc.h"
#include "td/telegram/Photo.hpp"

#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Slice.h"
#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void StickersManager::store_sticker(FileId file_id, bool in_sticker_set, StorerT &storer) const {
  auto it = stickers_.find(file_id);
  CHECK(it != stickers_.end());
  const Sticker *sticker = it->second.get();
  bool has_sticker_set_access_hash = sticker->set_id.is_valid() && !in_sticker_set;
  BEGIN_STORE_FLAGS();
  STORE_FLAG(sticker->is_mask);
  STORE_FLAG(has_sticker_set_access_hash);
  STORE_FLAG(in_sticker_set);
  STORE_FLAG(sticker->is_animated);
  END_STORE_FLAGS();
  if (!in_sticker_set) {
    store(sticker->set_id.get(), storer);
    if (has_sticker_set_access_hash) {
      auto sticker_set = get_sticker_set(sticker->set_id);
      CHECK(sticker_set != nullptr);
      store(sticker_set->access_hash, storer);
    }
  }
  store(sticker->alt, storer);
  store(sticker->dimensions, storer);
  store(sticker->s_thumbnail, storer);
  store(sticker->m_thumbnail, storer);
  store(file_id, storer);
  if (sticker->is_mask) {
    store(sticker->point, storer);
    store(sticker->x_shift, storer);
    store(sticker->y_shift, storer);
    store(sticker->scale, storer);
  }
}

template <class ParserT>
FileId StickersManager::parse_sticker(bool in_sticker_set, ParserT &parser) {
  if (parser.get_error() != nullptr) {
    return FileId();
  }

  auto sticker = make_unique<Sticker>();
  bool has_sticker_set_access_hash;
  bool in_sticker_set_stored;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(sticker->is_mask);
  PARSE_FLAG(has_sticker_set_access_hash);
  PARSE_FLAG(in_sticker_set_stored);
  PARSE_FLAG(sticker->is_animated);
  END_PARSE_FLAGS();
  if (in_sticker_set_stored != in_sticker_set) {
    Slice data = parser.template fetch_string_raw<Slice>(parser.get_left_len());
    for (auto c : data) {
      if (c != '\0') {
        parser.set_error("Invalid sticker set is stored in the database");
        break;
      }
    }
    parser.set_error("Zero sticker set is stored in the database");
    return FileId();
  }
  if (!in_sticker_set) {
    int64 set_id;
    parse(set_id, parser);
    sticker->set_id = StickerSetId(set_id);
    if (has_sticker_set_access_hash) {
      int64 sticker_set_access_hash;
      parse(sticker_set_access_hash, parser);
      add_sticker_set(sticker->set_id, sticker_set_access_hash);
    } else {
      // backward compatibility
      sticker->set_id = StickerSetId();
    }
  }
  parse(sticker->alt, parser);
  parse(sticker->dimensions, parser);
  PhotoSize thumbnail;
  parse(thumbnail, parser);
  add_sticker_thumbnail(sticker.get(), thumbnail);
  parse(thumbnail, parser);
  add_sticker_thumbnail(sticker.get(), thumbnail);
  parse(sticker->file_id, parser);
  if (sticker->is_mask) {
    parse(sticker->point, parser);
    parse(sticker->x_shift, parser);
    parse(sticker->y_shift, parser);
    parse(sticker->scale, parser);
  }
  if (parser.get_error() != nullptr || !sticker->file_id.is_valid()) {
    return FileId();
  }
  return on_get_sticker(std::move(sticker), false);  // data in the database is always outdated
}

template <class StorerT>
void StickersManager::store_sticker_set(const StickerSet *sticker_set, bool with_stickers, StorerT &storer) const {
  size_t stickers_limit = with_stickers ? sticker_set->sticker_ids.size() : 5;
  bool is_full = sticker_set->sticker_ids.size() <= stickers_limit;
  bool was_loaded = sticker_set->was_loaded && is_full;
  bool is_loaded = sticker_set->is_loaded && is_full;
  bool has_expires_at = !sticker_set->is_installed && sticker_set->expires_at != 0;
  bool has_thumbnail = sticker_set->thumbnail.file_id.is_valid();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(sticker_set->is_inited);
  STORE_FLAG(was_loaded);
  STORE_FLAG(is_loaded);
  STORE_FLAG(sticker_set->is_installed);
  STORE_FLAG(sticker_set->is_archived);
  STORE_FLAG(sticker_set->is_official);
  STORE_FLAG(sticker_set->is_masks);
  STORE_FLAG(sticker_set->is_viewed);
  STORE_FLAG(has_expires_at);
  STORE_FLAG(has_thumbnail);
  STORE_FLAG(sticker_set->is_thumbnail_reloaded);
  STORE_FLAG(sticker_set->is_animated);
  STORE_FLAG(sticker_set->are_legacy_thumbnails_reloaded);
  END_STORE_FLAGS();
  store(sticker_set->id.get(), storer);
  store(sticker_set->access_hash, storer);
  if (sticker_set->is_inited) {
    store(sticker_set->title, storer);
    store(sticker_set->short_name, storer);
    store(sticker_set->sticker_count, storer);
    store(sticker_set->hash, storer);
    if (has_expires_at) {
      store(sticker_set->expires_at, storer);
    }
    if (has_thumbnail) {
      store(sticker_set->thumbnail, storer);
    }

    uint32 stored_sticker_count = narrow_cast<uint32>(is_full ? sticker_set->sticker_ids.size() : stickers_limit);
    store(stored_sticker_count, storer);
    for (uint32 i = 0; i < stored_sticker_count; i++) {
      auto sticker_id = sticker_set->sticker_ids[i];
      store_sticker(sticker_id, true, storer);

      if (was_loaded) {
        auto it = sticker_set->sticker_emojis_map_.find(sticker_id);
        if (it != sticker_set->sticker_emojis_map_.end()) {
          store(it->second, storer);
        } else {
          store(vector<string>(), storer);
        }
      }
    }
  }
}

template <class ParserT>
void StickersManager::parse_sticker_set(StickerSet *sticker_set, ParserT &parser) {
  CHECK(sticker_set != nullptr);
  CHECK(!sticker_set->was_loaded);
  bool was_inited = sticker_set->is_inited;
  bool is_installed;
  bool is_archived;
  bool is_official;
  bool is_masks;
  bool is_animated;
  bool has_expires_at;
  bool has_thumbnail;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(sticker_set->is_inited);
  PARSE_FLAG(sticker_set->was_loaded);
  PARSE_FLAG(sticker_set->is_loaded);
  PARSE_FLAG(is_installed);
  PARSE_FLAG(is_archived);
  PARSE_FLAG(is_official);
  PARSE_FLAG(is_masks);
  PARSE_FLAG(sticker_set->is_viewed);
  PARSE_FLAG(has_expires_at);
  PARSE_FLAG(has_thumbnail);
  PARSE_FLAG(sticker_set->is_thumbnail_reloaded);
  PARSE_FLAG(is_animated);
  PARSE_FLAG(sticker_set->are_legacy_thumbnails_reloaded);
  END_PARSE_FLAGS();
  int64 sticker_set_id;
  int64 access_hash;
  parse(sticker_set_id, parser);
  parse(access_hash, parser);
  CHECK(sticker_set->id.get() == sticker_set_id);
  if (sticker_set->access_hash != access_hash) {
    LOG(ERROR) << "Access hash of " << sticker_set->id << " has changed from " << access_hash << " to "
               << sticker_set->access_hash;
  }

  if (sticker_set->is_inited) {
    string title;
    string short_name;
    int32 sticker_count;
    int32 hash;
    int32 expires_at = 0;
    parse(title, parser);
    parse(short_name, parser);
    parse(sticker_count, parser);
    parse(hash, parser);
    if (has_expires_at) {
      parse(expires_at, parser);
    }
    if (has_thumbnail) {
      parse(sticker_set->thumbnail, parser);
    }
    if (!was_inited) {
      sticker_set->title = std::move(title);
      sticker_set->short_name = std::move(short_name);
      sticker_set->sticker_count = sticker_count;
      sticker_set->hash = hash;
      sticker_set->expires_at = expires_at;
      sticker_set->is_official = is_official;
      sticker_set->is_masks = is_masks;
      sticker_set->is_animated = is_animated;

      short_name_to_sticker_set_id_.emplace(clean_username(sticker_set->short_name), sticker_set->id);
      on_update_sticker_set(sticker_set, is_installed, is_archived, false, true);
    } else {
      if (sticker_set->title != title) {
        LOG(INFO) << "Title of " << sticker_set->id << " has changed";
      }
      if (sticker_set->short_name != short_name) {
        LOG(ERROR) << "Short name of " << sticker_set->id << " has changed from \"" << short_name << "\" to \""
                   << sticker_set->short_name << "\"";
      }
      if (sticker_set->sticker_count != sticker_count || sticker_set->hash != hash) {
        sticker_set->is_loaded = false;
      }
      if (sticker_set->is_animated != is_animated) {
        LOG(ERROR) << "Is animated of " << sticker_set->id << " has changed from \"" << is_animated << "\" to \""
                   << sticker_set->is_animated << "\"";
      }
      if (sticker_set->is_masks != is_masks) {
        LOG(ERROR) << "Is masks of " << sticker_set->id << " has changed from \"" << is_masks << "\" to \""
                   << sticker_set->is_masks << "\"";
      }
    }

    uint32 stored_sticker_count;
    parse(stored_sticker_count, parser);
    sticker_set->sticker_ids.clear();
    if (sticker_set->was_loaded) {
      sticker_set->emoji_stickers_map_.clear();
      sticker_set->sticker_emojis_map_.clear();
    }
    for (uint32 i = 0; i < stored_sticker_count; i++) {
      auto sticker_id = parse_sticker(true, parser);
      if (parser.get_error() != nullptr) {
        return;
      }
      if (!sticker_id.is_valid()) {
        return parser.set_error("Receive invalid sticker in a sticker set");
      }
      sticker_set->sticker_ids.push_back(sticker_id);

      Sticker *sticker = get_sticker(sticker_id);
      CHECK(sticker != nullptr);
      if (sticker->set_id != sticker_set->id) {
        LOG_IF(ERROR, sticker->set_id.is_valid()) << "Sticker " << sticker_id << " set_id has changed";
        sticker->set_id = sticker_set->id;
        sticker->is_changed = true;
      }

      if (sticker_set->was_loaded) {
        vector<string> emojis;
        parse(emojis, parser);
        for (auto &emoji : emojis) {
          auto &sticker_ids = sticker_set->emoji_stickers_map_[remove_emoji_modifiers(emoji)];
          if (sticker_ids.empty() || sticker_ids.back() != sticker_id) {
            sticker_ids.push_back(sticker_id);
          }
        }
        sticker_set->sticker_emojis_map_[sticker_id] = std::move(emojis);
      }
    }
    if (expires_at > sticker_set->expires_at) {
      sticker_set->expires_at = expires_at;
    }
  }
}

template <class StorerT>
void StickersManager::store_sticker_set_id(StickerSetId sticker_set_id, StorerT &storer) const {
  CHECK(sticker_set_id.is_valid());
  const StickerSet *sticker_set = get_sticker_set(sticker_set_id);
  CHECK(sticker_set != nullptr);
  store(sticker_set_id.get(), storer);
  store(sticker_set->access_hash, storer);
}

template <class ParserT>
void StickersManager::parse_sticker_set_id(StickerSetId &sticker_set_id, ParserT &parser) {
  int64 set_id;
  parse(set_id, parser);
  sticker_set_id = StickerSetId(set_id);
  int64 sticker_set_access_hash;
  parse(sticker_set_access_hash, parser);
  add_sticker_set(sticker_set_id, sticker_set_access_hash);
}

}  // namespace td
