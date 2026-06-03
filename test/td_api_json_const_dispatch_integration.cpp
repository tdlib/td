// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "test/stealth/SourceContractFileReader.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <format>

namespace {

td::string load_repo_text(td::Slice relative_path) {
  return td::mtproto::test::read_repo_text_file(relative_path);
}

td::string load_td_api_json_all_shards_text() {
  td::string text;
  for (int shard = 0; shard <= 9; shard++) {
    if (!text.empty()) {
      text += '\n';
    }
    text += load_repo_text(std::format("td/generate/auto/td/telegram/td_api_json_{}.cpp", shard));
  }
  return text;
}

}  // namespace

TEST(TdApiJsonConstDispatchIntegration, first_ten_level1_json_sites_do_not_use_const_cast) {
  const auto td_api_json_1 = load_repo_text("td/generate/auto/td/telegram/td_api_json_1.cpp");

  ASSERT_EQ(td::string::npos, td_api_json_1.find("const_cast<td_api::ActiveStoryState &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_1.find("const_cast<td_api::AuthorizationState &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_1.find("const_cast<td_api::CallDiscardReason &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_1.find("const_cast<td_api::CanSendGiftResult &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_1.find("const_cast<td_api::ChatSource &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_1.find("const_cast<td_api::GiftResaleResult &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_1.find("const_cast<td_api::GiveawayParticipantStatus &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_1.find("const_cast<td_api::InputMessageContent &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_1.find("const_cast<td_api::InternalLinkType &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_1.find("const_cast<td_api::MaskPoint &>(object)"));
}

TEST(TdApiJsonConstDispatchIntegration, generator_uses_const_safe_dispatch_in_to_json) {
  const auto generator_cpp = load_repo_text("td/generate/tl_json_converter.cpp");
  ASSERT_EQ(td::string::npos, generator_cpp.find("downcast_call(const_cast<td_api::"));
}

TEST(TdApiJsonConstDispatchIntegration, first_ten_level1_json_sites_use_const_overload_dispatch) {
  const auto td_api_json_1 = load_repo_text("td/generate/auto/td/telegram/td_api_json_1.cpp");

  ASSERT_NE(td::string::npos,
            td_api_json_1.find("downcast_call(object, [&jv](const auto &object) { to_json(jv, object); });"));
}

TEST(TdApiJsonConstDispatchIntegration, second_ten_level1_json_sites_do_not_use_const_cast) {
  const auto td_api_json_all = load_td_api_json_all_shards_text();

  ASSERT_EQ(td::string::npos, td_api_json_all.find("const_cast<td_api::BlockList &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_all.find("const_cast<td_api::CanSendMessageToUserResult &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_all.find("const_cast<td_api::ChatAvailableReactions &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_all.find("const_cast<td_api::ChatBoostSource &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_all.find("const_cast<td_api::ChatMemberStatus &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_all.find("const_cast<td_api::ChatPhotoStickerType &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_all.find("const_cast<td_api::ChatStatistics &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_all.find("const_cast<td_api::CheckChatUsernameResult &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_all.find("const_cast<td_api::GiveawayPrize &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_all.find("const_cast<td_api::InlineKeyboardButtonType &>(object)"));
}

TEST(TdApiJsonConstDispatchIntegration, second_ten_level1_json_sites_use_const_overload_dispatch) {
  const auto td_api_json_all = load_td_api_json_all_shards_text();

  ASSERT_NE(
      td::string::npos,
      td_api_json_all.find("to_json(JsonValueScope &jv, const td_api::BlockList &object) {\n"
                           "  td_api::downcast_call(object, [&jv](const auto &object) { to_json(jv, object); });\n"
                           "}"));
  ASSERT_NE(
      td::string::npos,
      td_api_json_all.find("to_json(JsonValueScope &jv, const td_api::CanSendMessageToUserResult &object) {\n"
                           "  td_api::downcast_call(object, [&jv](const auto &object) { to_json(jv, object); });\n"
                           "}"));
  ASSERT_NE(
      td::string::npos,
      td_api_json_all.find("to_json(JsonValueScope &jv, const td_api::ChatAvailableReactions &object) {\n"
                           "  td_api::downcast_call(object, [&jv](const auto &object) { to_json(jv, object); });\n"
                           "}"));
  ASSERT_NE(
      td::string::npos,
      td_api_json_all.find("to_json(JsonValueScope &jv, const td_api::ChatBoostSource &object) {\n"
                           "  td_api::downcast_call(object, [&jv](const auto &object) { to_json(jv, object); });\n"
                           "}"));
  ASSERT_NE(
      td::string::npos,
      td_api_json_all.find("to_json(JsonValueScope &jv, const td_api::ChatMemberStatus &object) {\n"
                           "  td_api::downcast_call(object, [&jv](const auto &object) { to_json(jv, object); });\n"
                           "}"));
  ASSERT_NE(
      td::string::npos,
      td_api_json_all.find("to_json(JsonValueScope &jv, const td_api::ChatPhotoStickerType &object) {\n"
                           "  td_api::downcast_call(object, [&jv](const auto &object) { to_json(jv, object); });\n"
                           "}"));
  ASSERT_NE(
      td::string::npos,
      td_api_json_all.find("to_json(JsonValueScope &jv, const td_api::ChatStatistics &object) {\n"
                           "  td_api::downcast_call(object, [&jv](const auto &object) { to_json(jv, object); });\n"
                           "}"));
  ASSERT_NE(
      td::string::npos,
      td_api_json_all.find("to_json(JsonValueScope &jv, const td_api::CheckChatUsernameResult &object) {\n"
                           "  td_api::downcast_call(object, [&jv](const auto &object) { to_json(jv, object); });\n"
                           "}"));
  ASSERT_NE(
      td::string::npos,
      td_api_json_all.find("to_json(JsonValueScope &jv, const td_api::GiveawayPrize &object) {\n"
                           "  td_api::downcast_call(object, [&jv](const auto &object) { to_json(jv, object); });\n"
                           "}"));
  ASSERT_NE(
      td::string::npos,
      td_api_json_all.find("to_json(JsonValueScope &jv, const td_api::InlineKeyboardButtonType &object) {\n"
                           "  td_api::downcast_call(object, [&jv](const auto &object) { to_json(jv, object); });\n"
                           "}"));
}

TEST(TdApiJsonConstDispatchIntegration, third_ten_level1_json_sites_do_not_use_const_cast) {
  const auto td_api_json_all = load_td_api_json_all_shards_text();

  ASSERT_EQ(td::string::npos, td_api_json_all.find("const_cast<td_api::BackgroundFill &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_all.find("const_cast<td_api::BotWriteAccessAllowReason &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_all.find("const_cast<td_api::CanTransferOwnershipResult &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_all.find("const_cast<td_api::CheckStickerSetNameResult &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_all.find("const_cast<td_api::DiceStickers &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_all.find("const_cast<td_api::EmailAddressResetState &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_all.find("const_cast<td_api::InlineQueryResult &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_all.find("const_cast<td_api::InviteLinkChatType &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_all.find("const_cast<td_api::LanguagePackStringValue &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_all.find("const_cast<td_api::MessageSendingState &>(object)"));
}

TEST(TdApiJsonConstDispatchIntegration, third_ten_level1_json_sites_use_const_overload_dispatch) {
  const auto td_api_json_all = load_td_api_json_all_shards_text();

  ASSERT_NE(
      td::string::npos,
      td_api_json_all.find("to_json(JsonValueScope &jv, const td_api::BackgroundFill &object) {\n"
                           "  td_api::downcast_call(object, [&jv](const auto &object) { to_json(jv, object); });\n"
                           "}"));
  ASSERT_NE(
      td::string::npos,
      td_api_json_all.find("to_json(JsonValueScope &jv, const td_api::BotWriteAccessAllowReason &object) {\n"
                           "  td_api::downcast_call(object, [&jv](const auto &object) { to_json(jv, object); });\n"
                           "}"));
  ASSERT_NE(
      td::string::npos,
      td_api_json_all.find("to_json(JsonValueScope &jv, const td_api::CanTransferOwnershipResult &object) {\n"
                           "  td_api::downcast_call(object, [&jv](const auto &object) { to_json(jv, object); });\n"
                           "}"));
  ASSERT_NE(
      td::string::npos,
      td_api_json_all.find("to_json(JsonValueScope &jv, const td_api::CheckStickerSetNameResult &object) {\n"
                           "  td_api::downcast_call(object, [&jv](const auto &object) { to_json(jv, object); });\n"
                           "}"));
  ASSERT_NE(
      td::string::npos,
      td_api_json_all.find("to_json(JsonValueScope &jv, const td_api::DiceStickers &object) {\n"
                           "  td_api::downcast_call(object, [&jv](const auto &object) { to_json(jv, object); });\n"
                           "}"));
  ASSERT_NE(
      td::string::npos,
      td_api_json_all.find("to_json(JsonValueScope &jv, const td_api::EmailAddressResetState &object) {\n"
                           "  td_api::downcast_call(object, [&jv](const auto &object) { to_json(jv, object); });\n"
                           "}"));
  ASSERT_NE(
      td::string::npos,
      td_api_json_all.find("to_json(JsonValueScope &jv, const td_api::InlineQueryResult &object) {\n"
                           "  td_api::downcast_call(object, [&jv](const auto &object) { to_json(jv, object); });\n"
                           "}"));
  ASSERT_NE(
      td::string::npos,
      td_api_json_all.find("to_json(JsonValueScope &jv, const td_api::InviteLinkChatType &object) {\n"
                           "  td_api::downcast_call(object, [&jv](const auto &object) { to_json(jv, object); });\n"
                           "}"));
  ASSERT_NE(
      td::string::npos,
      td_api_json_all.find("to_json(JsonValueScope &jv, const td_api::LanguagePackStringValue &object) {\n"
                           "  td_api::downcast_call(object, [&jv](const auto &object) { to_json(jv, object); });\n"
                           "}"));
  ASSERT_NE(
      td::string::npos,
      td_api_json_all.find("to_json(JsonValueScope &jv, const td_api::MessageSendingState &object) {\n"
                           "  td_api::downcast_call(object, [&jv](const auto &object) { to_json(jv, object); });\n"
                           "}"));
}

TEST(TdApiJsonConstDispatchIntegration, fourth_batch_json1_sites_do_not_use_const_cast) {
  const auto td_api_json_1 = load_repo_text("td/generate/auto/td/telegram/td_api_json_1.cpp");

  ASSERT_EQ(td::string::npos, td_api_json_1.find("const_cast<td_api::MessageEffectType &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_1.find("const_cast<td_api::MessageSender &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_1.find("const_cast<td_api::PublicForward &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_1.find("const_cast<td_api::ReactionNotificationSource &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_1.find("const_cast<td_api::RichText &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_1.find("const_cast<td_api::StoryList &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_1.find("const_cast<td_api::SuggestedPostRefundReason &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_1.find("const_cast<td_api::UserType &>(object)"));
}

TEST(TdApiJsonConstDispatchIntegration, fifth_batch_json2_sites_do_not_use_const_cast) {
  const auto td_api_json_2 = load_repo_text("td/generate/auto/td/telegram/td_api_json_2.cpp");

  ASSERT_EQ(td::string::npos, td_api_json_2.find("const_cast<td_api::InputMessageReplyTo &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_2.find("const_cast<td_api::InviteGroupCallParticipantResult &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_2.find("const_cast<td_api::MessageFileType &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_2.find("const_cast<td_api::PageBlockHorizontalAlignment &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_2.find("const_cast<td_api::PollType &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_2.find("const_cast<td_api::ReactionType &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_2.find("const_cast<td_api::ReplyMarkup &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_2.find("const_cast<td_api::StoryAreaType &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_2.find("const_cast<td_api::StoryOrigin &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_2.find("const_cast<td_api::SuggestedPostState &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_2.find("const_cast<td_api::Update &>(object)"));
}

TEST(TdApiJsonConstDispatchIntegration, sixth_batch_json3_sites_do_not_use_const_cast) {
  const auto td_api_json_3 = load_repo_text("td/generate/auto/td/telegram/td_api_json_3.cpp");

  ASSERT_EQ(td::string::npos, td_api_json_3.find("const_cast<td_api::NetworkStatisticsEntry &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_3.find("const_cast<td_api::NotificationType &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_3.find("const_cast<td_api::ReactionUnavailabilityReason &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_3.find("const_cast<td_api::ReportChatResult &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_3.find("const_cast<td_api::SecretChatState &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_3.find("const_cast<td_api::StarSubscriptionType &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_3.find("const_cast<td_api::StickerFormat &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_3.find("const_cast<td_api::StoryContent &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_3.find("const_cast<td_api::StoryPrivacySettings &>(object)"));
}

TEST(TdApiJsonConstDispatchIntegration, seventh_batch_json4_sites_do_not_use_const_cast) {
  const auto td_api_json_4 = load_repo_text("td/generate/auto/td/telegram/td_api_json_4.cpp");

  ASSERT_EQ(td::string::npos, td_api_json_4.find("const_cast<td_api::BackgroundType &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_4.find("const_cast<td_api::BuiltInTheme &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_4.find("const_cast<td_api::ConnectionState &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_4.find("const_cast<td_api::EmojiStatusType &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_4.find("const_cast<td_api::InputPaidMediaType &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_4.find("const_cast<td_api::NetworkType &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_4.find("const_cast<td_api::PassportElement &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_4.find("const_cast<td_api::PushMessageContent &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_4.find("const_cast<td_api::SentGift &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_4.find("const_cast<td_api::SpeechRecognitionResult &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_4.find("const_cast<td_api::StickerFullType &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_4.find("const_cast<td_api::StoryContentType &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_4.find("const_cast<td_api::UpgradedGiftOrigin &>(object)"));
  ASSERT_EQ(td::string::npos, td_api_json_4.find("const_cast<td_api::UserPrivacySetting &>(object)"));
}

TEST(TdApiJsonConstDispatchIntegration, actor_info_operator_ostream_does_not_use_const_cast) {
  const auto actor_info_h = load_repo_text("tdactor/td/actor/impl/ActorInfo.h");

  ASSERT_EQ(td::string::npos, actor_info_h.find("const_cast<void *>(static_cast<const void *>(&info))"));
  ASSERT_EQ(td::string::npos, actor_info_h.find("const_cast<void *>(static_cast<const void *>(info.get_context()))"));
}
