//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/Premium.h"

#include "td/telegram/AnimationsManager.h"
#include "td/telegram/Application.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/Document.h"
#include "td/telegram/DocumentsManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/misc.h"
#include "td/telegram/PremiumGiftOption.h"
#include "td/telegram/SuggestedAction.h"
#include "td/telegram/Td.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UpdatesManager.h"
#include "td/telegram/UserId.h"

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"
#include "td/utils/JsonBuilder.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/Status.h"

namespace td {

static td_api::object_ptr<td_api::PremiumFeature> get_premium_feature_object(Slice premium_feature) {
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
  if (premium_feature == "unique_reactions" || premium_feature == "infinite_reactions") {
    return td_api::make_object<td_api::premiumFeatureUniqueReactions>();
  }
  if (premium_feature == "premium_stickers") {
    return td_api::make_object<td_api::premiumFeatureUniqueStickers>();
  }
  if (premium_feature == "animated_emoji") {
    return td_api::make_object<td_api::premiumFeatureCustomEmoji>();
  }
  if (premium_feature == "advanced_chat_management") {
    return td_api::make_object<td_api::premiumFeatureAdvancedChatManagement>();
  }
  if (premium_feature == "profile_badge") {
    return td_api::make_object<td_api::premiumFeatureProfileBadge>();
  }
  if (premium_feature == "emoji_status") {
    return td_api::make_object<td_api::premiumFeatureEmojiStatus>();
  }
  if (premium_feature == "animated_userpics") {
    return td_api::make_object<td_api::premiumFeatureAnimatedProfilePhoto>();
  }
  if (premium_feature == "forum_topic_icon") {
    return td_api::make_object<td_api::premiumFeatureForumTopicIcon>();
  }
  if (premium_feature == "app_icons") {
    return td_api::make_object<td_api::premiumFeatureAppIcons>();
  }
  if (premium_feature == "translations") {
    return td_api::make_object<td_api::premiumFeatureRealTimeChatTranslation>();
  }
  return nullptr;
}

static Result<tl_object_ptr<telegram_api::InputStorePaymentPurpose>> get_input_store_payment_purpose(
    Td *td, const td_api::object_ptr<td_api::StorePaymentPurpose> &purpose) {
  if (purpose == nullptr) {
    return Status::Error(400, "Purchase purpose must be non-empty");
  }

  switch (purpose->get_id()) {
    case td_api::storePaymentPurposePremiumSubscription::ID: {
      auto p = static_cast<const td_api::storePaymentPurposePremiumSubscription *>(purpose.get());
      int32 flags = 0;
      if (p->is_restore_) {
        flags |= telegram_api::inputStorePaymentPremiumSubscription::RESTORE_MASK;
      }
      if (p->is_upgrade_) {
        flags |= telegram_api::inputStorePaymentPremiumSubscription::UPGRADE_MASK;
      }
      return make_tl_object<telegram_api::inputStorePaymentPremiumSubscription>(flags, false /*ignored*/,
                                                                                false /*ignored*/);
    }
    case td_api::storePaymentPurposeGiftedPremium::ID: {
      auto p = static_cast<const td_api::storePaymentPurposeGiftedPremium *>(purpose.get());
      UserId user_id(p->user_id_);
      TRY_RESULT(input_user, td->contacts_manager_->get_input_user(user_id));
      if (p->amount_ <= 0 || !check_currency_amount(p->amount_)) {
        return Status::Error(400, "Invalid amount of the currency specified");
      }
      return make_tl_object<telegram_api::inputStorePaymentGiftPremium>(std::move(input_user), p->currency_,
                                                                        p->amount_);
    }
    default:
      UNREACHABLE();
      return nullptr;
  }
}

class GetPremiumPromoQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::premiumState>> promise_;

 public:
  explicit GetPremiumPromoQuery(Promise<td_api::object_ptr<td_api::premiumState>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create(telegram_api::help_getPremiumPromo()));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::help_getPremiumPromo>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto promo = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetPremiumPromoQuery: " << to_string(promo);

    td_->contacts_manager_->on_get_users(std::move(promo->users_), "GetPremiumPromoQuery");

    auto state = get_message_text(td_->contacts_manager_.get(), std::move(promo->status_text_),
                                  std::move(promo->status_entities_), true, true, 0, false, "GetPremiumPromoQuery");

    if (promo->video_sections_.size() != promo->videos_.size()) {
      return on_error(Status::Error(500, "Receive wrong number of videos"));
    }

    vector<td_api::object_ptr<td_api::premiumFeaturePromotionAnimation>> animations;
    for (size_t i = 0; i < promo->video_sections_.size(); i++) {
      auto feature = get_premium_feature_object(promo->video_sections_[i]);
      if (feature == nullptr) {
        continue;
      }

      auto video = std::move(promo->videos_[i]);
      if (video->get_id() != telegram_api::document::ID) {
        LOG(ERROR) << "Receive " << to_string(video) << " for " << promo->video_sections_[i];
        continue;
      }

      auto parsed_document = td_->documents_manager_->on_get_document(move_tl_object_as<telegram_api::document>(video),
                                                                      DialogId(), nullptr, Document::Type::Animation);

      if (parsed_document.type != Document::Type::Animation) {
        LOG(ERROR) << "Receive " << parsed_document.type << " for " << promo->video_sections_[i];
        continue;
      }

      auto animation_object = td_->animations_manager_->get_animation_object(parsed_document.file_id);
      animations.push_back(td_api::make_object<td_api::premiumFeaturePromotionAnimation>(std::move(feature),
                                                                                         std::move(animation_object)));
    }

    auto period_options = get_premium_gift_options(std::move(promo->period_options_));
    promise_.set_value(td_api::make_object<td_api::premiumState>(
        get_formatted_text_object(state, true, 0), get_premium_state_payment_options_object(period_options),
        std::move(animations)));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class CanPurchasePremiumQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit CanPurchasePremiumQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(td_api::object_ptr<td_api::StorePaymentPurpose> &&purpose) {
    auto r_input_purpose = get_input_store_payment_purpose(td_, purpose);
    if (r_input_purpose.is_error()) {
      return on_error(r_input_purpose.move_as_error());
    }

    send_query(
        G()->net_query_creator().create(telegram_api::payments_canPurchasePremium(r_input_purpose.move_as_ok())));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_canPurchasePremium>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    bool result = result_ptr.ok();
    if (!result) {
      return on_error(Status::Error(400, "Premium can't be purchased"));
    }
    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class AssignAppStoreTransactionQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit AssignAppStoreTransactionQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(const string &receipt, td_api::object_ptr<td_api::StorePaymentPurpose> &&purpose) {
    auto r_input_purpose = get_input_store_payment_purpose(td_, purpose);
    if (r_input_purpose.is_error()) {
      return on_error(r_input_purpose.move_as_error());
    }

    send_query(G()->net_query_creator().create(
        telegram_api::payments_assignAppStoreTransaction(BufferSlice(receipt), r_input_purpose.move_as_ok())));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_assignAppStoreTransaction>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for AssignAppStoreTransactionQuery: " << to_string(ptr);
    td_->updates_manager_->on_get_updates(std::move(ptr), std::move(promise_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class AssignPlayMarketTransactionQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit AssignPlayMarketTransactionQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(const string &package_name, const string &store_product_id, const string &purchase_token,
            td_api::object_ptr<td_api::StorePaymentPurpose> &&purpose) {
    auto r_input_purpose = get_input_store_payment_purpose(td_, purpose);
    if (r_input_purpose.is_error()) {
      return on_error(r_input_purpose.move_as_error());
    }
    auto receipt = make_tl_object<telegram_api::dataJSON>(string());
    receipt->data_ = json_encode<string>(json_object([&](auto &o) {
      o("packageName", package_name);
      o("purchaseToken", purchase_token);
      o("productId", store_product_id);
    }));
    send_query(G()->net_query_creator().create(
        telegram_api::payments_assignPlayMarketTransaction(std::move(receipt), r_input_purpose.move_as_ok())));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_assignPlayMarketTransaction>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for AssignPlayMarketTransactionQuery: " << to_string(ptr);
    td_->updates_manager_->on_get_updates(std::move(ptr), std::move(promise_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

const vector<Slice> &get_premium_limit_keys() {
  static const vector<Slice> limit_keys{"channels",
                                        "saved_gifs",
                                        "stickers_faved",
                                        "dialog_filters",
                                        "dialog_filters_chats",
                                        "dialogs_pinned",
                                        "dialogs_folder_pinned",
                                        "channels_public",
                                        "caption_length",
                                        "about_length",
                                        "chatlist_invites",
                                        "chatlists_joined"};
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
    case td_api::premiumLimitTypeChatFolderCount::ID:
      return Slice("dialog_filters");
    case td_api::premiumLimitTypeChatFolderChosenChatCount::ID:
      return Slice("dialog_filters_chats");
    case td_api::premiumLimitTypePinnedChatCount::ID:
      return Slice("dialogs_pinned");
    case td_api::premiumLimitTypePinnedArchivedChatCount::ID:
      return Slice("dialogs_folder_pinned");
    case td_api::premiumLimitTypeCreatedPublicChatCount::ID:
      return Slice("channels_public");
    case td_api::premiumLimitTypeCaptionLength::ID:
      return Slice("caption_length");
    case td_api::premiumLimitTypeBioLength::ID:
      return Slice("about_length");
    case td_api::premiumLimitTypeChatFolderInviteLinkCount::ID:
      return Slice("chatlist_invites");
    case td_api::premiumLimitTypeShareableChatFolderCount::ID:
      return Slice("chatlists_joined");
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
      return "infinite_reactions";
    case td_api::premiumFeatureUniqueStickers::ID:
      return "premium_stickers";
    case td_api::premiumFeatureCustomEmoji::ID:
      return "animated_emoji";
    case td_api::premiumFeatureAdvancedChatManagement::ID:
      return "advanced_chat_management";
    case td_api::premiumFeatureProfileBadge::ID:
      return "profile_badge";
    case td_api::premiumFeatureEmojiStatus::ID:
      return "emoji_status";
    case td_api::premiumFeatureAnimatedProfilePhoto::ID:
      return "animated_userpics";
    case td_api::premiumFeatureForumTopicIcon::ID:
      return "forum_topic_icon";
    case td_api::premiumFeatureAppIcons::ID:
      return "app_icons";
    case td_api::premiumFeatureRealTimeChatTranslation::ID:
      return "translations";
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
  auto default_limit = static_cast<int32>(G()->get_option_integer(PSLICE() << key << "_limit_default"));
  auto premium_limit = static_cast<int32>(G()->get_option_integer(PSLICE() << key << "_limit_premium"));
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
      return td_api::make_object<td_api::premiumLimitTypeChatFolderCount>();
    }
    if (key == "dialog_filters_chats") {
      return td_api::make_object<td_api::premiumLimitTypeChatFolderChosenChatCount>();
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
    if (key == "about_length") {
      return td_api::make_object<td_api::premiumLimitTypeBioLength>();
    }
    if (key == "chatlist_invites") {
      return td_api::make_object<td_api::premiumLimitTypeChatFolderInviteLinkCount>();
    }
    if (key == "chatlists_joined") {
      return td_api::make_object<td_api::premiumLimitTypeShareableChatFolderCount>();
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
  auto premium_features = full_split(
      G()->get_option_string(
          "premium_features",
          "double_limits,more_upload,faster_download,voice_to_text,no_ads,infinite_reactions,premium_stickers,"
          "animated_emoji,advanced_chat_management,profile_badge,emoji_status,animated_userpics,app_icons,"
          "translations"),
      ',');
  vector<td_api::object_ptr<td_api::PremiumFeature>> features;
  for (const auto &premium_feature : premium_features) {
    auto feature = get_premium_feature_object(premium_feature);
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
  auto premium_bot_username = G()->get_option_string("premium_bot_username");
  if (!premium_bot_username.empty()) {
    payment_link = td_api::make_object<td_api::internalLinkTypeBotStart>(premium_bot_username, source_str, true);
  } else {
    auto premium_invoice_slug = G()->get_option_string("premium_invoice_slug");
    if (!premium_invoice_slug.empty()) {
      payment_link = td_api::make_object<td_api::internalLinkTypeInvoice>(premium_invoice_slug);
    }
  }

  promise.set_value(
      td_api::make_object<td_api::premiumFeatures>(std::move(features), std::move(limits), std::move(payment_link)));
}

void view_premium_feature(Td *td, const td_api::object_ptr<td_api::PremiumFeature> &feature, Promise<Unit> &&promise) {
  auto source = get_premium_source(feature.get());
  if (source.empty()) {
    return promise.set_error(Status::Error(400, "Feature must be non-empty"));
  }

  vector<tl_object_ptr<telegram_api::jsonObjectValue>> data;
  data.push_back(
      make_tl_object<telegram_api::jsonObjectValue>("item", make_tl_object<telegram_api::jsonString>(source)));
  save_app_log(td, "premium.promo_screen_tap", DialogId(), make_tl_object<telegram_api::jsonObject>(std::move(data)),
               std::move(promise));
}

void click_premium_subscription_button(Td *td, Promise<Unit> &&promise) {
  vector<tl_object_ptr<telegram_api::jsonObjectValue>> data;
  save_app_log(td, "premium.promo_screen_accept", DialogId(), make_tl_object<telegram_api::jsonObject>(std::move(data)),
               std::move(promise));
}

void get_premium_state(Td *td, Promise<td_api::object_ptr<td_api::premiumState>> &&promise) {
  td->create_handler<GetPremiumPromoQuery>(std::move(promise))->send();
}

void can_purchase_premium(Td *td, td_api::object_ptr<td_api::StorePaymentPurpose> &&purpose, Promise<Unit> &&promise) {
  td->create_handler<CanPurchasePremiumQuery>(std::move(promise))->send(std::move(purpose));
}

void assign_app_store_transaction(Td *td, const string &receipt,
                                  td_api::object_ptr<td_api::StorePaymentPurpose> &&purpose, Promise<Unit> &&promise) {
  if (purpose != nullptr && purpose->get_id() == td_api::storePaymentPurposePremiumSubscription::ID) {
    dismiss_suggested_action(SuggestedAction{SuggestedAction::Type::UpgradePremium}, Promise<Unit>());
    dismiss_suggested_action(SuggestedAction{SuggestedAction::Type::SubscribeToAnnualPremium}, Promise<Unit>());
  }
  td->create_handler<AssignAppStoreTransactionQuery>(std::move(promise))->send(receipt, std::move(purpose));
}

void assign_play_market_transaction(Td *td, const string &package_name, const string &store_product_id,
                                    const string &purchase_token,
                                    td_api::object_ptr<td_api::StorePaymentPurpose> &&purpose,
                                    Promise<Unit> &&promise) {
  if (purpose != nullptr && purpose->get_id() == td_api::storePaymentPurposePremiumSubscription::ID) {
    dismiss_suggested_action(SuggestedAction{SuggestedAction::Type::UpgradePremium}, Promise<Unit>());
    dismiss_suggested_action(SuggestedAction{SuggestedAction::Type::SubscribeToAnnualPremium}, Promise<Unit>());
  }
  td->create_handler<AssignPlayMarketTransactionQuery>(std::move(promise))
      ->send(package_name, store_product_id, purchase_token, std::move(purpose));
}

}  // namespace td
