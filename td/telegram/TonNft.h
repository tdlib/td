//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/files/FileId.h"
#include "td/telegram/PhotoSize.h"
#include "td/telegram/StarGift.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"

namespace td {

class Dependencies;
class Td;

class TonNftAttribute {
  string trait_type_;
  string value_;

  friend bool operator==(const TonNftAttribute &lhs, const TonNftAttribute &rhs);

 public:
  TonNftAttribute() = default;

  explicit TonNftAttribute(telegram_api::object_ptr<telegram_api::wallet_nftAttribute> &&attribute);

  td_api::object_ptr<td_api::tonNftAttribute> get_ton_nft_attribute_object() const;

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const TonNftAttribute &lhs, const TonNftAttribute &rhs);
bool operator!=(const TonNftAttribute &lhs, const TonNftAttribute &rhs);

class TonNft {
  string collection_address_;
  string address_;
  string owner_address_;
  string index_;
  string name_;
  string description_;
  PhotoSize image_;
  PhotoSize image_small_;
  FileId content_url_file_id_;
  FileId lottie_file_id_;
  vector<TonNftAttribute> attributes_;
  string extra_;
  StarGift gift_;

  friend bool operator==(const TonNft &lhs, const TonNft &rhs);

 public:
  TonNft() = default;

  TonNft(Td *td, telegram_api::object_ptr<telegram_api::wallet_nftItem> &&item);

  bool is_valid() const {
    return !address_.empty();
  }

  const string &get_address() const {
    return address_;
  }

  td_api::object_ptr<td_api::tonNft> get_ton_nft_object(Td *td) const;

  void add_dependencies(Dependencies &dependencies) const;

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const TonNft &lhs, const TonNft &rhs);
bool operator!=(const TonNft &lhs, const TonNft &rhs);

}  // namespace td
