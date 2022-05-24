//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/Premium.h"

#include "td/telegram/Application.h"
#include "td/telegram/ConfigShared.h"
#include "td/telegram/Global.h"

#include "td/utils/algorithm.h"
#include "td/utils/SliceBuilder.h"

namespace td {

const vector<Slice> &get_premium_limit_keys() {
  static const vector<Slice> limit_keys{"channels",
                                        "saved_gifs",
                                        "stickers_faved",
                                        "dialog_filters",
                                        "dialog_filters_chats",
                                        "dialogs_pinned",
                                        "dialogs_folder_pinned",
                                        "channels_public",
                                        "caption_length"};
  return limit_keys;
}

static Slice get_limit_type_key(const td_api::PremiumLimitType *limit_type) {
  CHECK(limit_type != nullptr);
  switch (limit_type->get_id()) {
    case td_api::premiumLimitTypeSupergroupCount::ID:
      return Slice("channels");
    case td_api::premiumLimitTypeSavedAnimationCount::ID:
      return Slice("saved_gifs");
    case td_api::premiumLimitTypeFavoriteStickerCount::ID:
      return Slice("stickers_faved");
    case td_api::premiumLimitTypeChatFilterCount::ID:
      return Slice("dialog_filters");
    case td_api::premiumLimitTypeChatFilterChosenChatCount::ID:
      return Slice("dialog_filters_chats");
    case td_api::premiumLimitTypePinnedChatCount::ID:
      return Slice("dialogs_pinned");
    case td_api::premiumLimitTypePinnedArchivedChatCount::ID:
      return Slice("dialogs_folder_pinned");
    case td_api::premiumLimitTypeCreatedPublicChatCount::ID:
      return Slice("channels_public");
    case td_api::premiumLimitTypeCaptionLength::ID:
      return Slice("caption_length");
    default:
      UNREACHABLE();
      return Slice();
  }
}

static string get_premium_source(const td_api::PremiumLimitType *limit_type) {
  if (limit_type == nullptr) {
    return string();
  }

  return PSTRING() << "double_limits__" << get_limit_type_key(limit_type);
}

static string get_premium_source(const td_api::PremiumFeature *feature) {
  if (feature == nullptr) {
    return string();
  }

  switch (feature->get_id()) {
    case td_api::premiumFeatureIncreasedLimits::ID:
      return "double_limits";
    case td_api::premiumFeatureIncreasedUploadFileSize::ID:
      return "more_upload";
    case td_api::premiumFeatureImprovedDownloadSpeed::ID:
      return "faster_download";
    case td_api::premiumFeatureVoiceRecognition::ID:
      return "voice_to_text";
    case td_api::premiumFeatureDisabledAds::ID:
      return "no_ads";
    case td_api::premiumFeatureUniqueReactions::ID:
      return "unique_reactions";
    case td_api::premiumFeatureUniqueStickers::ID:
      return "premium_stickers";
    case td_api::premiumFeatureAdvancedChatManagement::ID:
      return "advanced_chat_management";
    case td_api::premiumFeatureProfileBadge::ID:
      return "profile_badge";
    case td_api::premiumFeatureAnimatedProfilePhoto::ID:
      return "animated_userpics";
    default:
      UNREACHABLE();
  }
  return string();
}

static string get_premium_source(const td_api::object_ptr<td_api::PremiumSource> &source) {
  if (source == nullptr) {
    return string();
  }
  switch (source->get_id()) {
    case td_api::premiumSourceLimitExceeded::ID: {
      auto *limit_type = static_cast<const td_api::premiumSourceLimitExceeded *>(source.get())->limit_type_.get();
      return get_premium_source(limit_type);
    }
    case td_api::premiumSourceFeature::ID: {
      auto *feature = static_cast<const td_api::premiumSourceFeature *>(source.get())->feature_.get();
      return get_premium_source(feature);
    }
    case td_api::premiumSourceLink::ID: {
      auto &referrer = static_cast<const td_api::premiumSourceLink *>(source.get())->referrer_;
      if (referrer.empty()) {
        return "deeplink";
      }
      return PSTRING() << "deeplink_" << referrer;
    }
    case td_api::premiumSourceSettings::ID:
      return "settings";
    default:
      UNREACHABLE();
      return string();
  }
}

static td_api::object_ptr<td_api::premiumLimit> get_premium_limit_object(Slice key) {
  int32 default_limit =
      static_cast<int32>(G()->shared_config().get_option_integer(PSLICE() << key << "_limit_default"));
  int32 premium_limit =
      static_cast<int32>(G()->shared_config().get_option_integer(PSLICE() << key << "_limit_premium"));
  if (default_limit <= 0 || premium_limit <= default_limit) {
    return nullptr;
  }
  auto type = [&]() -> td_api::object_ptr<td_api::PremiumLimitType> {
    if (key == "channels") {
      return td_api::make_object<td_api::premiumLimitTypeSupergroupCount>();
    }
    if (key == "saved_gifs") {
      return td_api::make_object<td_api::premiumLimitTypeSavedAnimationCount>();
    }
    if (key == "stickers_faved") {
      return td_api::make_object<td_api::premiumLimitTypeFavoriteStickerCount>();
    }
    if (key == "dialog_filters") {
      return td_api::make_object<td_api::premiumLimitTypeChatFilterCount>();
    }
    if (key == "dialog_filters_chats") {
      return td_api::make_object<td_api::premiumLimitTypeChatFilterChosenChatCount>();
    }
    if (key == "dialogs_pinned") {
      return td_api::make_object<td_api::premiumLimitTypePinnedChatCount>();
    }
    if (key == "dialogs_folder_pinned") {
      return td_api::make_object<td_api::premiumLimitTypePinnedArchivedChatCount>();
    }
    if (key == "channels_public") {
      return td_api::make_object<td_api::premiumLimitTypeCreatedPublicChatCount>();
    }
    if (key == "caption_length") {
      return td_api::make_object<td_api::premiumLimitTypeCaptionLength>();
    }
    UNREACHABLE();
    return nullptr;
  }();
  return td_api::make_object<td_api::premiumLimit>(std::move(type), default_limit, premium_limit);
}

void get_premium_limit(const td_api::object_ptr<td_api::PremiumLimitType> &limit_type,
                       Promise<td_api::object_ptr<td_api::premiumLimit>> &&promise) {
  if (limit_type == nullptr) {
    return promise.set_error(Status::Error(400, "Limit type must be non-empty"));
  }

  promise.set_value(get_premium_limit_object(get_limit_type_key(limit_type.get())));
}

void get_premium_features(Td *td, const td_api::object_ptr<td_api::PremiumSource> &source,
                          Promise<td_api::object_ptr<td_api::premiumFeatures>> &&promise) {
  auto premium_features =
      full_split(G()->shared_config().get_option_string(
                     "premium_features",
                     "double_limits,more_upload,faster_download,voice_to_text,no_ads,unique_reactions,premium_stickers,"
                     "advanced_chat_management,profile_badge,animated_userpics"),
                 ',');
  vector<td_api::object_ptr<td_api::PremiumFeature>> features;
  for (const auto &premium_feature : premium_features) {
    auto feature = [&]() -> td_api::object_ptr<td_api::PremiumFeature> {
      if (premium_feature == "double_limits") {
        return td_api::make_object<td_api::premiumFeatureIncreasedLimits>();
      }
      if (premium_feature == "more_upload") {
        return td_api::make_object<td_api::premiumFeatureIncreasedUploadFileSize>();
      }
      if (premium_feature == "faster_download") {
        return td_api::make_object<td_api::premiumFeatureImprovedDownloadSpeed>();
      }
      if (premium_feature == "voice_to_text") {
        return td_api::make_object<td_api::premiumFeatureVoiceRecognition>();
      }
      if (premium_feature == "no_ads") {
        return td_api::make_object<td_api::premiumFeatureDisabledAds>();
      }
      if (premium_feature == "unique_reactions") {
        return td_api::make_object<td_api::premiumFeatureUniqueReactions>();
      }
      if (premium_feature == "premium_stickers") {
        return td_api::make_object<td_api::premiumFeatureUniqueStickers>();
      }
      if (premium_feature == "advanced_chat_management") {
        return td_api::make_object<td_api::premiumFeatureAdvancedChatManagement>();
      }
      if (premium_feature == "profile_badge") {
        return td_api::make_object<td_api::premiumFeatureProfileBadge>();
      }
      if (premium_feature == "animated_userpics") {
        return td_api::make_object<td_api::premiumFeatureAnimatedProfilePhoto>();
      }
      return nullptr;
    }();
    if (feature != nullptr) {
      features.push_back(std::move(feature));
    }
  }

  auto limits = transform(get_premium_limit_keys(), get_premium_limit_object);
  td::remove_if(limits, [](auto &limit) { return limit == nullptr; });

  auto source_str = get_premium_source(source);
  if (!source_str.empty()) {
    vector<tl_object_ptr<telegram_api::jsonObjectValue>> data;
    vector<tl_object_ptr<telegram_api::JSONValue>> promo_order;
    for (const auto &premium_feature : premium_features) {
      promo_order.push_back(make_tl_object<telegram_api::jsonString>(premium_feature));
    }
    data.push_back(make_tl_object<telegram_api::jsonObjectValue>(
        "premium_promo_order", make_tl_object<telegram_api::jsonArray>(std::move(promo_order))));
    data.push_back(
        make_tl_object<telegram_api::jsonObjectValue>("source", make_tl_object<telegram_api::jsonString>(source_str)));
    save_app_log(td, "premium.promo_screen_show", DialogId(), make_tl_object<telegram_api::jsonObject>(std::move(data)),
                 Promise<Unit>());
  }

  td_api::object_ptr<td_api::InternalLinkType> payment_link;
  auto premium_bot_username = G()->shared_config().get_option_string("premium_bot_username");
  if (!premium_bot_username.empty()) {
    payment_link = td_api::make_object<td_api::internalLinkTypeBotStart>(premium_bot_username, source_str);
  } else {
    auto premium_invoice_slug = G()->shared_config().get_option_string("premium_invoice_slug");
    if (!premium_invoice_slug.empty()) {
      payment_link = td_api::make_object<td_api::internalLinkTypeInvoice>(premium_invoice_slug);
    }
  }

  promise.set_value(
      td_api::make_object<td_api::premiumFeatures>(std::move(features), std::move(limits), std::move(payment_link)));
}

}  // namespace td
