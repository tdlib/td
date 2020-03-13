//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

namespace td {

constexpr int32 MTPROTO_LAYER = 111;

enum class Version : int32 {
  Initial,
  StoreFileId,
  AddKeyHashToSecretChat,
  AddDurationToAnimation,
  FixStoreGameWithoutAnimation,
  AddAccessHashToSecretChat,
  StoreFileOwnerId,
  StoreFileEncryptionKey,
  NetStatsCountDuration,
  FixWebPageInstantViewDatabase,
  FixMinUsers,
  FixPageBlockAudioEmptyFile,
  AddMessageInvoiceProviderData,
  AddCaptionEntities,
  AddVenueType,
  AddTermsOfService,
  AddContactVcard,
  AddMessageUnsupportedVersion,
  SupportInstantView2_0,
  AddNotificationGroupInfoMaxRemovedMessageId,
  SupportMinithumbnails,
  AddVideoCallsSupport,
  AddPhotoSizeSource,
  AddFolders,
  SupportPolls2_0,
  Next
};

enum class DbVersion : int32 {
  DialogDbCreated = 3,
  MessagesDbMediaIndex,
  MessagesDb30MediaIndex,
  MessagesDbFts,
  MessagesCallIndex,
  FixFileRemoteLocationKeyBug,
  AddNotificationsSupport,
  AddFolders,
  AddScheduledMessages,
  Next
};

inline constexpr int32 current_db_version() {
  return static_cast<int32>(DbVersion::Next) - 1;
}

}  // namespace td
