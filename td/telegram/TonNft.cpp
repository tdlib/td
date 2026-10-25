//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/TonNft.h"

#include "td/telegram/Dependencies.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/Document.h"
#include "td/telegram/DocumentsManager.h"
#include "td/telegram/files/FileType.h"
#include "td/telegram/JsonValue.h"
#include "td/telegram/PhotoFormat.h"
#include "td/telegram/StickersManager.h"
#include "td/telegram/Td.h"

#include "td/utils/algorithm.h"
#include "td/utils/logging.h"

namespace td {

TonNftAttribute::TonNftAttribute(telegram_api::object_ptr<telegram_api::wallet_nftAttribute> &&attribute)
    : trait_type_(std::move(attribute->trait_type_)), value_(std::move(attribute->value_)) {
}

td_api::object_ptr<td_api::tonNftAttribute> TonNftAttribute::get_ton_nft_attribute_object() const {
  return td_api::make_object<td_api::tonNftAttribute>(trait_type_, value_);
}

bool operator==(const TonNftAttribute &lhs, const TonNftAttribute &rhs) {
  return lhs.trait_type_ == rhs.trait_type_ && lhs.value_ == rhs.value_;
}

bool operator!=(const TonNftAttribute &lhs, const TonNftAttribute &rhs) {
  return !(lhs == rhs);
}

TonNft::TonNft(Td *td, telegram_api::object_ptr<telegram_api::wallet_nftItem> &&item)
    : collection_address_(std::move(item->collection_address_))
    , address_(std::move(item->address_))
    , owner_address_(std::move(item->owner_address_))
    , index_(std::move(item->index_))
    , name_(std::move(item->name_))
    , description_(std::move(item->description_))
    , image_(get_web_document_photo_size(td->file_manager_.get(), FileType::Photo, DialogId(), std::move(item->image_)))
    , image_small_(get_web_document_photo_size(td->file_manager_.get(), FileType::Photo, DialogId(),
                                               std::move(item->image_small_)))
    , attributes_(transform(std::move(item->attributes_),
                            [](auto &&attribute) { return TonNftAttribute(std::move(attribute)); })) {
  if (item->content_url_ != nullptr) {
    auto parsed_document = td->documents_manager_->on_get_document({std::move(item->content_url_)}, DialogId(), false,
                                                                   false, nullptr, Document::Type::General);
    if (parsed_document.file_id.is_valid() && parsed_document.type == Document::Type::General) {
      content_url_file_id_ = parsed_document.file_id;
    }
  }
  if (item->lottie_ != nullptr) {
    auto parsed_document = td->documents_manager_->on_get_document({std::move(item->lottie_)}, DialogId(), false, false,
                                                                   nullptr, Document::Type::Sticker);
    if (parsed_document.file_id.is_valid() && parsed_document.type == Document::Type::Sticker) {
      lottie_file_id_ = parsed_document.file_id;
    }
  }
  if (item->extra_ != nullptr) {
    extra_ = std::move(item->extra_->data_);
  }
  if (item->gift_ != nullptr) {
    StarGift gift(td, std::move(item->gift_), true);
    if (gift.is_valid() && gift.is_unique()) {
      gift_ = std::move(gift);
    } else {
      LOG(ERROR) << "Receive invalid gift for NFT " << address_;
    }
  }
}

td_api::object_ptr<td_api::tonNft> TonNft::get_ton_nft_object(Td *td) const {
  string extra = extra_;
  auto r_json_value = get_json_value(extra);
  return td_api::make_object<td_api::tonNft>(
      collection_address_, address_, owner_address_, index_, name_, description_,
      image_.file_id.is_valid() ? get_photo_size_object(td->file_manager_.get(), image_) : nullptr,
      image_small_.file_id.is_valid() ? get_photo_size_object(td->file_manager_.get(), image_small_) : nullptr,
      td->documents_manager_->get_document_object(content_url_file_id_, PhotoFormat::Jpeg),
      td->stickers_manager_->get_sticker_object(lottie_file_id_),
      transform(attributes_, [](const auto &attribute) { return attribute.get_ton_nft_attribute_object(); }),
      r_json_value.is_ok() ? r_json_value.move_as_ok() : nullptr,
      gift_.is_valid() ? gift_.get_upgraded_gift_object(td) : nullptr);
}

void TonNft::add_dependencies(Dependencies &dependencies) const {
  if (gift_.is_valid()) {
    gift_.add_dependencies(dependencies);
  }
}

bool operator==(const TonNft &lhs, const TonNft &rhs) {
  return lhs.collection_address_ == rhs.collection_address_ && lhs.address_ == rhs.address_ &&
         lhs.owner_address_ == rhs.owner_address_ && lhs.index_ == rhs.index_ && lhs.name_ == rhs.name_ &&
         lhs.description_ == rhs.description_ && lhs.image_ == rhs.image_ && lhs.image_small_ == rhs.image_small_ &&
         lhs.content_url_file_id_ == rhs.content_url_file_id_ && lhs.lottie_file_id_ == rhs.lottie_file_id_ &&
         lhs.attributes_ == rhs.attributes_ && lhs.extra_ == rhs.extra_ && lhs.gift_ == rhs.gift_;
}

bool operator!=(const TonNft &lhs, const TonNft &rhs) {
  return !(lhs == rhs);
}

}  // namespace td
