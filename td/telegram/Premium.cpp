//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/Premium.h"

#include "td/telegram/ConfigShared.h"
#include "td/telegram/Global.h"

#include "td/utils/SliceBuilder.h"

namespace td {

const vector<Slice> &get_premium_limit_keys() {
  static const vector<Slice> limit_keys{"channels_limit",
                                        "saved_gifs_limit",
                                        "stickers_faved_limit",
                                        "dialog_filters_limit",
                                        "dialog_filters_chats_limit",
                                        "dialogs_pinned_limit",
                                        "dialogs_folder_pinned_limit",
                                        "channels_public_limit",
                                        "caption_length_limit"};
  return limit_keys;
}

void get_premium_features(Promise<td_api::object_ptr<td_api::premiumFeatures>> &&promise) {
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
        return td_api::make_object<td_api::premiumFeatureIncreasedFileSize>();
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

  auto &limit_keys = get_premium_limit_keys();
  vector<td_api::object_ptr<td_api::premiumLimit>> limits;
  for (auto key : limit_keys) {
    int32 default_limit = static_cast<int32>(G()->shared_config().get_option_integer(PSLICE() << key << "_default"));
    int32 premium_limit = static_cast<int32>(G()->shared_config().get_option_integer(PSLICE() << key << "_premium"));
    if (default_limit > 0 && premium_limit > 0) {
      auto type = [&]() -> td_api::object_ptr<td_api::PremiumLimitType> {
        if (key == "channels_limit") {
          return td_api::make_object<td_api::premiumLimitTypeSupergroupCount>();
        }
        if (key == "saved_gifs_limit") {
          return td_api::make_object<td_api::premiumLimitTypeSavedAnimationCount>();
        }
        if (key == "stickers_faved_limit") {
          return td_api::make_object<td_api::premiumLimitTypeFavoriteStickerCount>();
        }
        if (key == "dialog_filters_limit") {
          return td_api::make_object<td_api::premiumLimitTypeChatFilterCount>();
        }
        if (key == "dialog_filters_chats_limit") {
          return td_api::make_object<td_api::premiumLimitTypeChatFilterChosenChatCount>();
        }
        if (key == "dialogs_pinned_limit") {
          return td_api::make_object<td_api::premiumLimitTypePinnedChatCount>();
        }
        if (key == "dialogs_folder_pinned_limit") {
          return td_api::make_object<td_api::premiumLimitTypePinnedArchivedChatCount>();
        }
        if (key == "channels_public_limit") {
          return td_api::make_object<td_api::premiumLimitTypeCreatedPublicChatCount>();
        }
        if (key == "caption_length_limit") {
          return td_api::make_object<td_api::premiumLimitTypeCaptionLength>();
        }
        UNREACHABLE();
        return nullptr;
      }();
      limits.push_back(td_api::make_object<td_api::premiumLimit>(std::move(type), default_limit, premium_limit));
    }
  }
  promise.set_value(td_api::make_object<td_api::premiumFeatures>(std::move(features), std::move(limits)));
}

}  // namespace td
