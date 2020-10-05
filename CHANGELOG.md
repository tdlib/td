Changes in 1.6.0 (31 Jan 2020):

* Added support for multiple chat lists. Currently, only two chat lists Main and Archive are supported:
  - Added the class `ChatList`, which represents a chat list and could be `chatListMain` or `chatListArchive`.
  - Added the field `chat_list` to the class `chat`, denoting the chat list to which the chat belongs.
  - Added the parameter `chat_list` to the methods `getChats`, `searchMessages` and `setPinnedChats`.
  - Added the field `chat_list` to the updates `updateUnreadMessageCount` and `updateUnreadChatCount`.
  - Added the field `total_count` to the update `updateUnreadChatCount`, containing the total number of chats in
    the list.
  - Added the update `updateChatChatList`, which is sent after a chat is moved to or from a chat list.
  - Added the method `setChatChatList`, which can be used to move a chat between chat lists.
  - Added the option `pinned_archived_chat_count_max` for the maximum number of pinned chats in the Archive chat list.
* Added support for scheduled messages:
  - Added the classes `messageSchedulingStateSendAtDate` and `messageSchedulingStateSendWhenOnline`,
    representing the scheduling state of a message.
  - Added the field `scheduling_state` to the class `message`, which allows to distinguish between scheduled and
    ordinary messages.
  - The update `updateNewMessage` can now contain a scheduled message and must be handled appropriately.
  - The updates `updateMessageContent`, `updateDeleteMessages`, `updateMessageViews`, `updateMessageSendSucceeded`,
    `updateMessageSendFailed`, and `updateMessageSendAcknowledged` can now contain identifiers of scheduled messages.
  - Added the class `sendMessageOptions`, which contains options for sending messages,
    including the scheduling state of the messages.
  - Replaced the parameters `disable_notification` and `from_background` in the methods `sendMessage`,
    `sendMessageAlbum`, `sendInlineQueryResultMessage`, and `forwardMessages` with the new field `options` of
    the type `sendMessageOptions`.
  - Added the method `editMessageSchedulingState`, which can be used to reschedule a message or send it immediately.
  - Added the method `getChatScheduledMessages`, which returns all scheduled messages in a chat.
  - Added the field `has_scheduled_messages` to the class `chat`.
  - Added the update `updateChatHasScheduledMessages`, which is sent whenever the field `has_scheduled_messages`
    changes in a chat.
  - Added support for reminders in Saved Messages and notifications about other sent scheduled messages in
    the [Notification API](https://core.telegram.org/tdlib/notification-api/).
* Added support for adding users without a known phone number to the list of contacts:
  - Added the method `addContact` for adding or renaming contacts without a known phone number.
  - Added the field `need_phone_number_privacy_exception` to the class `userFullInfo`, containing the default value for
    the second parameter of the method `addContact`.
  - Added the fields `is_contact` and `is_mutual_contact` to the class `user`.
  - Removed the class `LinkState` and the fields `outgoing_link` and `incoming_link` from the class `user`.
* Improved support for the top chat action bar:
  - Added the class `ChatActionBar`, representing all possible types of the action bar.
  - Added the field `action_bar` to the class `chat`.
  - Removed the legacy class `chatReportSpamState`.
  - Removed the legacy methods `getChatReportSpamState` and `changeChatReportSpamState`.
  - Added the update `updateChatActionBar`.
  - Added the method `removeChatActionBar`, which allows to dismiss the action bar.
  - Added the method `sharePhoneNumber`, allowing to share the phone number of the current user with a mutual contact.
  - Added the new reason `chatReportReasonUnrelatedLocation` for reporting location-based groups unrelated to
    their stated location.
* Improved support for text entities:
  - Added the new types of text entities `textEntityTypeUnderline` and `textEntityTypeStrikethrough`.
  - Added support for nested entities. Entities can be nested, but must not mutually intersect with each other.
    Pre, Code and PreCode entities can't contain other entities. Bold, Italic, Underline and Strikethrough entities can
    contain and be contained in all other entities. All other entities can't contain each other.
  - Added the field `version` to the method `textParseModeMarkdown`. Versions 0 and 1 correspond to Bot API Markdown
    parse mode, version 2 to Bot API MarkdownV2 parse mode with underline, strikethrough and nested entities support.
  - The new entity types and nested entities are supported in secret chats also if its layer is at least 101.
* Added support for native non-anonymous, multiple answer, and quiz-style polls:
  - Added support for quiz-style polls, which has exactly one correct answer option and can be answered only once.
  - Added support for regular polls, which allows multiple answers.
  - Added the classes `pollTypeRegular` and `pollTypeQuiz`, representing the possible types of a poll.
  - Added the field `type` to the classes `poll` and `inputMessagePoll`.
  - Added support for non-anonymous polls with visible votes by adding the field `is_anonymous` to the classes `poll`
    and `inputMessagePoll`.
  - Added the method `getPollVoters` returning users that voted for the specified option in a non-anonymous poll.
  - Added the new reply markup keyboard button `keyboardButtonTypeRequestPoll`.
  - Added the field `is_regular` to the class `pushMessageContentPoll`.
  - Added the update `updatePollAnswer` for bots only.
  - Added the field `is_closed` to the class `inputMessagePoll`, which can be used by bots to send a closed poll.
* Clarified in the documentation that file remote ID is guaranteed to be usable only if the corresponding file is
  still accessible to the user and is known to TDLib. For example, if the file is from a message, then the message
  must be not deleted and accessible to the user. If the file database is disabled, then the corresponding object with
  the file must be preloaded by the client.
* Added support for administrator custom titles:
  - Added the field `custom_title` to `chatMemberStatusCreator` and `chatMemberStatusAdministrator` classes.
  - Added the classes `chatAdministrator` and `chatAdministrators`, containing user identifiers along with
    their custom administrator title and owner status.
  - Replaced the result type of the method `getChatAdministrators` with `chatAdministrators`.
* Improved Instant View support:
  - Added the new web page block `pageBlockVoiceNote`.
  - Changed value of invisible cells in `pageBlockTableCell` to null.
  - Added the field `is_cached` to the class `richTextUrl`.
* Improved support for chat backgrounds:
  - Added the classes `backgroundFillSolid` for solid color backgrounds and `backgroundFillGradient` for
    gradient backgrounds.
  - Added support for TGV (gzipped subset of SVG with MIME type "application/x-tgwallpattern") background patterns
    in addition to PNG patterns. Background pattern thumbnails are still always in PNG format.
  - Replaced the field `color` in the class `backgroundTypePattern` with the field `fill` of type `BackgroundFill`.
  - Replaced the class `backgroundTypeSolid` with the class `backgroundTypeFill`.
* Added support for discussion groups for channel chats:
  - Added the field `linked_chat_id` to the class `supergroupFullInfo` containing the identifier of a discussion
    supergroup for the channel, or a channel, for which the supergroup is the designated discussion supergroup.
  - Added the field `has_linked_chat` to the class `supergroup`.
  - Added the method `getSuitableDiscussionChats`, which returns a list of chats which can be assigned as
    a discussion group for a channel by the current user.
  - Added the method `setChatDiscussionGroup`, which can be used to add or remove a discussion group from a channel.
  - Added the class `chatEventLinkedChatChanged` representing a change of the linked chat in the chat event log.
* Added support for slow mode in supergroups:
  - Added the field `is_slow_mode_enabled` to the class `supergroup`.
  - Added the field `slow_mode_delay` to the class `supergroupFullInfo`.
  - Added the method `setChatSlowModeDelay`, which can be used to change the slow mode delay setting in a supergroup.
  - Added the class `chatEventSlowModeDelayChanged` representing a change of the slow mode delay setting in
    the chat event log.
* Improved privacy settings support:
  - Added the classes `userPrivacySettingRuleAllowChatMembers` and `userPrivacySettingRuleRestrictChatMembers`
    to include or exclude all group members in a privacy setting rule.
  - Added the class `userPrivacySettingShowPhoneNumber` for managing the visibility of the user's phone number.
  - Added the class `userPrivacySettingAllowFindingByPhoneNumber` for managing whether the user can be found by
    their phone number.
* Added the method `checkCreatedPublicChatsLimit` for checking whether the maximum number of owned public chats
  has been reached.
* Added support for transferring ownership of supergroup and channel chats:
  - Added the method `transferChatOwnership`.
  - Added the class `CanTransferOwnershipResult` and the method `canTransferOwnership` for checking
    whether chat ownership can be transferred from the current session.
* Added support for location-based supergroups:
  - Added the class `chatLocation`, which contains the location to which the supergroup is connected.
  - Added the field `has_location` to the class `supergroup`.
  - Added the field `location` to the class `supergroupFullInfo`.
  - Added the ability to create location-based supergroups via the new field `location` in
    the method `createNewSupergroupChat`.
  - Added the method `setChatLocation`, which allows to change location of location-based supergroups.
  - Added the field `can_set_location` to the class `supergroupFullInfo`.
  - Added the class `PublicChatType`, which can be one of `publicChatTypeHasUsername` or
    `publicChatTypeIsLocationBased`.
  - Added the parameter `type` to the method `getCreatedPublicChats`, which allows to get location-based supergroups
    owned by the user.
  - Supported location-based supergroups as public chats where appropriate.
  - Added the class `chatEventLocationChanged` representing a change of the location of a chat in the chat event log.
* Added support for searching chats and users nearby:
  - Added the classes `chatNearby` and `chatsNearby`, containing information about chats along with
    the distance to them.
  - Added the method `searchChatsNearby`, which returns chats and users nearby.
  - Added the update `updateUsersNearby`, which is sent 60 seconds after a successful `searchChatsNearby` request.
* Improved support for inline keyboard buttons of the type `inlineKeyboardButtonTypeLoginUrl`:
  - Added the class `LoginUrlInfo` and the method `getLoginUrlInfo`, which allows to get information about
    an inline button of the type `inlineKeyboardButtonTypeLoginUrl`.
  - Added the method `getLoginUrl` for automatic authorization on the target website.
* Improved support for content restrictions:
  - The field `restriction_reason` in the classes `user` and `channel` now contains only a human-readable description
    why access must be restricted. It is non-empty if and only if access to the chat needs to be restricted.
  - Added the field `restriction_reason` to the class `message`. It is non-empty if and only if access to the message
    needs to be restricted.
  - Added the writable option `ignore_platform_restrictions`, which can be set in non-store apps to ignore restrictions
    specific to the currently used operating system.
  - Added the writable option `ignore_sensitive_content_restrictions`, which can be set to show sensitive content on
    all user devices. `getOption("ignore_sensitive_content_restrictions")` can be used to fetch the actual value of
    the option, the option will not be immediately updated after a change from another device.
  - Added the read-only option `can_ignore_sensitive_content_restrictions`, which can be used to check, whether
    the option `ignore_sensitive_content_restrictions` can be changed.
* Added support for QR code authentication for already registered users:
  - Added the authorization state `authorizationStateWaitOtherDeviceConfirmation`.
  - Added the method `requestQrCodeAuthentication`, which can be used in the `authorizationStateWaitPhoneNumber` state
    instead of the method `setAuthenticationPhoneNumber` to request QR code authentication.
  - Added the method `confirmQrCodeAuthentication` for authentication confirmation from another device.
* Added the update `updateMessageLiveLocationViewed`, which is supposed to trigger an edit of the corresponding
  live location.
* Added the parameter `input_language_code` to the method `searchEmojis`.
* Added the method `getInactiveSupergroupChats`, to be used when the user receives a CHANNELS_TOO_MUCH error after
  reaching the limit on the number of joined supergroup and channel chats.
* Added the field `unique_id` to the class `remoteFile`, which can be used to identify the same file for
  different users.
* Added the new category of top chat list `topChatCategoryForwardChats`.
* Added the read-only option `animated_emoji_sticker_set_name`, containing name of a sticker set with animated emojis.
* Added the read-only option `unix_time`, containing an estimation of the current Unix timestamp.
  The option will not be updated automatically unless the difference between the previous estimation and
  the locally available monotonic clocks changes significantly.
* Added the field `is_silent` to the class `notification`, so silent notifications can be shown with
  the appropriate mark.
* Added the field `video_upload_bitrate` to the class `autoDownloadSettings`.
* Disallowed to call `setChatNotificationSettings` method on the chat with self, which never worked.
* Added support for `ton://` URLs in messages and inline keyboard buttons.

-----------------------------------------------------------------------------------------------------------------------

Changes in 1.5.0 (9 Sep 2019):

* Changed authorization workflow:
  - Added the state `authorizationStateWaitRegistration`, which will be received after `authorizationStateWaitCode` for
    users who are not registered yet.
  - Added the method `registerUser`, which must be used in the `authorizationStateWaitRegistration` state to finish
    registration of the user.
  - Removed the fields `is_registered` and `terms_of_service` from the class `authorizationStateWaitCode`.
  - Removed the parameters `first_name` and `last_name` from the method `checkAuthenticationCode`.
* Added support for messages with an unknown sender (zero `sender_user_id`) in private chats, basic groups and
  supergroups. Currently, the sender is unknown for posts in channels and for channel posts automatically forwarded to
  the discussion group.
* Added support for the new permission system for non-administrator users in groups:
  - Added the class `chatPermissions` containing all supported permissions, including new permissions `can_send_polls`,
    `can_change_info`, `can_invite_users` and `can_pin_messages`.
  - Added the field `permissions` to the class `chat`, describing actions that non-administrator chat members are
    allowed to take in the chat.
  - Added the update `updateChatPermissions`.
  - Added the method `setChatPermissions` for changing chat permissions.
  - Added the class `chatEventPermissionsChanged` representing a change of chat permissions in the chat event log.
  - Replaced the fields `can_send_messages`, `can_send_media_messages`, `can_send_other_messages`,
    `can_add_web_page_previews` in the class `chatMemberStatusRestricted` with the field `permissions` of
    the type `chatPermissions`.
  - Removed the field `everyone_is_administrator` from the `basicGroup` class in favor of the field `permissions` of
    the class `chat`.
  - Removed the field `anyone_can_invite` from the `supergroup` class in favor of the field `permissions` of
    the class `chat`.
  - Removed the method `toggleBasicGroupAdministrators` in favor of `setChatPermissions`.
  - Removed the method `toggleSupergroupInvites` in favor of `setChatPermissions`.
  - Renamed the field `anyone_can_invite` to `can_invite_users` in the class `chatEventInvitesToggled`.
  - The permissions `can_send_other_messages` and `can_add_web_page_previews` now imply only `can_send_messages`
    instead of `can_send_media_messages`.
  - Allowed administrators in basic groups to use the method `generateChatInviteLink`.
* Added out of the box `OpenBSD` and `NetBSD` operating systems support.
* Added possibility to use `LibreSSL` >= 2.7.0 instead of `OpenSSL` to build TDLib.
* Added instructions for building TDLib on `Debian 10`, `OpenBSD` and `NetBSD` to
  the [TDLib build instructions generator](https://tdlib.github.io/td/build.html).
* Added support for Backgrounds 2.0:
  - Added the classes `BackgroundType`, `background`, `backgrounds` and `InputBackground`.
  - Added the method `getBackground` returning the list of backgrounds installed by the user.
  - Added the method `setBackground` for changing the background selected by the user.
  - Added the update `updateSelectedBackground`, which is sent right after a successful initialization and whenever
    the selected background changes.
  - Added the method `removeBackground` for removing a background from the list of installed backgrounds.
  - Added the method `resetBackgrounds` for restoring the default list of installed backgrounds.
  - Added the method `searchBackground` returning a background by its name.
  - Added the method `getBackgroundUrl` returning a persistent URL for a background.
  - Removed the `getWallpapers` method.
  - Removed the `wallpaper` and the `wallpapers` classes.
  - The class `fileTypeWallpaper` can be used for remote file identifiers of both old wallpapers and new backgrounds.
* Added support for descriptions in basic groups:
  - Added the field `description` to the class `basicGroupFullInfo`.
  - Replaced the method `setSupergroupDescription` with `setChatDescription` which can be used for any chat type.
* Added support for emoji suggestions:
  - Added the method `searchEmojis` for searching emojis by keywords.
  - Added the method `getEmojiSuggestionsUrl`, which can be used to automatically log in to the translation platform
    and suggest new emoji replacements.
  - Renamed the class `stickerEmojis` to `emojis`.
* Changed type of the fields `old_photo` and `new_photo` in the class `chatEventPhotoChanged` from `chatPhoto` to
  `photo`.
* Changed recommended size for `inputThumbnail` from 90x90 to 320x320.
* Combined all supported settings for phone number authentication:
  - Added the class `phoneNumberAuthenticationSettings` which contains all the settings.
  - Replaced the parameters `is_current_phone_number` and `allow_flash_call` in the methods
    `setAuthenticationPhoneNumber`, `sendPhoneNumberConfirmationCode`, `sendPhoneNumberVerificationCode` and
    `changePhoneNumber` with the parameter `settings` of the type `phoneNumberAuthenticationSettings`.
  - Added support for automatic SMS code verification for official applications via the new field `allow_app_hash` in
    the class `phoneNumberAuthenticationSettings`.
* Added support for auto-download settings presets.
  - Added the classes `autoDownloadSettings` and `autoDownloadSettingsPresets`.
  - Added the method `getAutoDownloadSettingsPresets` for getting the settings.
  - Added the method `setAutoDownloadSettings`, which needs to be called whenever the user changes the settings.
* Added support for minithumbnails - thumbnail images of a very poor quality and low resolution:
  - Added the class `minithumbnail`.
  - Added the field `minithumbnail` to `animation`, `document`, `photo`, `video` and `videoNote` classes.
  - Added the field `audio_cover_minithumbnail` to the class `audio`.
* Added support for resending messages which failed to send:
  - Added the fields `error_code`, `error_message`, `can_retry` and `retry_after` to
    the class `messageSendingStateFailed`.
  - Added the method `resendMessages`.
* Added the field `is_animated` to the `sticker`, `stickerSet` and `stickerSetInfo` classes.
  Animated stickers can be received anywhere where non-animated stickers can appear.
* Added the parameters `send_copy` and `remove_caption` to the `forwardMessages` method to allow forwarding of
  messages without links to the originals.
* Added the fields `send_copy` and `remove_caption` to `inputMessageForwarded` method to allow forwarding of
  a message without link to the original message.
* Added the method `getMessageLinkInfo` for getting information about a link to a message in a chat.
* Added the class `userPrivacySettingShowProfilePhoto` for managing visibility of the user's profile photo.
* Added the class `userPrivacySettingShowLinkInForwardedMessages` for managing whether a link to the user's account is
  included with forwarded messages.
* Added the field `thumbnail` to the classes `stickerSet` and `stickerSetInfo`, containing a thumbnail for
  the sticker set.
* Added the field `is_scam` to the classes `user` and `supergroup`.
* Added a new kind of inline keyboard button `inlineKeyboardButtonTypeLoginUrl`, which for the moment must be processed
  in the same way as an `inlineKeyboardButtonTypeUrl`.
* Added the new class `supergroupMembersFilterContacts`, allowing to only search for contacts
  in `getSupergroupMembers`.
* Added the new class `chatMembersFilterContacts`, allowing to only search for contacts in `searchChatMembers`.
* Added the class `chatEventPollStopped` representing the closing of a poll in a message in the chat event log.
* Added ability to specify the exact types of problems with a call in the method `sendCallRating` and
  the new class `CallProblem`.
* Changes in [tdweb](https://github.com/tdlib/td/blob/master/example/web/):
  - Supported non-zero `offset` and `limit` in `readFilePart`.

-----------------------------------------------------------------------------------------------------------------------

Changes in 1.4.0 (1 May 2019):

* Added a [TDLib build instructions generator](https://tdlib.github.io/td/build.html), covering in details
  TDLib building on the most popular operating systems.
* Added an example of TDLib building and usage from a browser.
  See https://github.com/tdlib/td/blob/master/example/web/ for more details.
* Allowed to pass NULL pointer to `td_json_client_execute` instead of a previously created JSON client.
  Now you can use synchronous TDLib methods through a JSON interface before creating a TDLib JSON client.
* Added support for media streaming by allowing to download any part of a file:
  - Added the `offset` parameter to `downloadFile` which specifies the starting position
    from which the file should be downloaded.
  - Added the `limit` parameter to `downloadFile` which specifies how many bytes should be downloaded starting from
    the `offset` position.
  - Added the field `download_offset` to the class `localFile` which contains the current download offset.
  - The field `downloaded_prefix_size` of the `localFile` class now contains the number of available bytes
    from the position `download_offset` instead of from the beginning of the file.
  - Added the method `getFileDownloadedPrefixSize` which can be used to get the number of locally available file bytes
    from a given offset without actually changing the download offset.
* Added the parameter `synchronous` to `downloadFile` which causes the request to return the result only after
  the download is completed.
* Added support for native polls in messages:
  - Added `messagePoll` to the types of message content; contains a poll.
  - Added the classes `poll` and `pollOption` describing a poll and a poll answer option respectively.
  - Added `inputMessagePoll` to the types of new input message content; can be used to send a poll.
  - Added the method `setPollAnswer` which can be used for voting in polls.
  - Added the method `stopPoll` which can be used to stop polls. Use the `Message.can_be_edited` field to check whether
    this method can be called on a message.
  - Added the update `updatePoll` for bots only. Ordinary users receive poll updates through `updateMessageContent`.
* Added a Notification API. See article https://core.telegram.org/tdlib/notification-api for a detailed description.
  - Added the class `pushReceiverId` which contains a globally unique identifier of the push notification subscription.
  - Changed the return type of the method `registerDevice` to `pushReceiverId` to allow matching of push notifications
    with TDLib instances.
  - Removed the fields `disable_notification` and `contains_mention` from `updateNewMessage`.
  - Renamed the class `deviceTokenGoogleCloudMessaging` to `deviceTokenFirebaseCloudMessaging`.
  - Added the field `encrypt` to classes `deviceTokenApplePushVoIP` and `deviceTokenFirebaseCloudMessaging`
    which allows to subscribe for end-to-end encrypted push notifications.
  - Added the option `notification_group_count_max` which can be used to enable the Notification API and set
    the maximum number of notification groups to be shown simultaneously.
  - Added the option `notification_group_size_max` which can be used to set the maximum number of simultaneously shown
    notifications in a group.
  - Added the synchronous method `getPushReceiverId` for matching a push notification with a TDLib instance.
  - Added the method `processPushNotification` for handling of push notifications.
  - Removed the method `processDcUpdate` in favor of the general `processPushNotification` method.
  - Added the update `updateNotificationGroup`, sent whenever a notification group changes.
  - Added the update `updateNotification`, sent whenever a notification changes.
  - Added the update `updateActiveNotifications` for syncing the list of active notifications on startup.
  - Added the update `updateHavePendingNotifications` which can be used to improve lifetime handling of
    the TDLib instance.
  - Added the possibility to disable special handling of notifications about pinned messages via the new settings
    `use_default_disable_pinned_message_notifications`, `disable_pinned_message_notifications` in
    the class `chatNotificationSettings` and the new setting `disable_pinned_message_notifications` in
    the class `scopeNotificationSettings`.
  - Added the possibility to disable special handling of notifications about mentions and replies via the new settings
    `use_default_disable_mention_notifications`, `disable_mention_notifications` in
    the class `chatNotificationSettings` and the new setting `disable_mention_notifications` in
    the class `scopeNotificationSettings`.
  - Added the class `PushMessageContent` describing the content of a notification, received through
    a push notification.
  - Added the class `NotificationType` describing a type of a notification.
  - Added the class `notification` containing information about a notification.
  - Added the class `NotificationGroupType` describing a type of a notification group.
  - Added the class `notificationGroup` describing a state of a notification group.
  - Added the methods `removeNotification` and `removeNotificationGroup` for handling notifications removal
    by the user.
  - Added the separate notification scope `notificationSettingsScopeChannelChats` for channel chats.
* Added support for pinned notifications in basic groups and Saved Messages:
  - Added the field `pinned_message_id` to the class `chat`.
  - Removed the field `pinned_message_id` from the class `supergroupFullInfo` in favor of `chat.pinned_message_id`.
  - Added the update `updateChatPinnedMessage`.
  - The right `can_pin_messages` is now applicable to both basic groups and supergroups.
  - Replaced the method `pinSupergroupMessage` with `pinChatMessage` which can be used for any chat type.
  - Replaced the method `unpinSupergroupMessage` with `unpinChatMessage` which can be used for any chat type.
* Added new synchronous methods for managing TDLib internal logging. The old functions are deprecated and
  will be removed in TDLib 2.0.0.
  - Added the synchronous method `setLogStream` for changing the stream to which the TDLib internal log is written.
  - Added the synchronous method `getLogStream` for getting information about the currently used log stream.
  - Added the classes `logStreamDefault`, `logStreamFile` and `logStreamEmpty` describing different supported kinds of
    log streams.
  - Added the class `logVerbosityLevel` containing the verbosity level of the TDLib internal log.
  - Added the class `logTags` containing a list of available TDLib internal log tags.
  - Added the synchronous method `setLogVerbosityLevel` for changing verbosity level of logging.
  - Added the synchronous method `getLogVerbosityLevel` for getting the current verbosity level of logging.
  - Added the synchronous method `getLogTags` returning all currently supported log tags.
  - Added the synchronous method `setLogTagVerbosityLevel` for changing the verbosity level of logging for
    some specific part of the code.
  - Added the synchronous method `getLogTagVerbosityLevel` for getting the current verbosity level for a specific part
    of the code.
  - Added the synchronous method `addLogMessage` for using the TDLib internal log by the application.
* Added support for Instant View 2.0:
  - Replaced the field `has_instant_view` in class `webPage` with the `instant_view_version` field.
  - Added the field `version` to the class `webPageInstantView`.
  - Added the class `pageBlockCaption`.
  - Changed the type of `caption` fields in `pageBlockAnimation`, `pageBlockAudio`, `pageBlockPhoto`, `pageBlockVideo`,
    `pageBlockEmbedded`, `pageBlockEmbeddedPost`, `pageBlockCollage` and `pageBlockSlideshow` from
    `RichText` to `pageBlockCaption`.
  - Added the class `pageBlockListItem` and replaced the content of the `pageBlockList` class with a list of
    `pageBlockListItem`.
  - Added 6 new kinds of `RichText`: `richTextSubscript`, `richTextSuperscript`, `richTextMarked`,
    `richTextPhoneNumber`, `richTextIcon` and `richTextAnchor`.
  - Added new classes `pageBlockRelatedArticle`, `PageBlockHorizontalAlignment`, `PageBlockVerticalAlignment` and
    `pageBlockTableCell`.
  - Added new block types `pageBlockKicker`, `pageBlockRelatedArticles`, `pageBlockTable`, `pageBlockDetails` and
    `pageBlockMap`.
  - Added the flag `is_rtl` to the class `webPageInstantView`.
  - Renamed the field `caption` in classes `pageBlockBlockQuote` and `pageBlockPullQuote` to `credit`.
  - Dimensions in `pageBlockEmbedded` can now be unknown.
  - Added the field `url` to `pageBlockPhoto` which contains a URL that needs to be opened when the photo is clicked.
  - Added the field `url` to `webPageInstantView` which must be used for the correct handling of anchors.
* Added methods for confirmation of the 2-step verification recovery email address:
  - Added the method `checkRecoveryEmailAddressCode` for checking the verification code.
  - Added the method `resendRecoveryEmailAddressCode` for resending the verification code.
  - Replaced the field `unconfirmed_recovery_email_address_pattern` in the class `passwordState` with
    the `recovery_email_address_code_info` field containing full information about the code.
  - The necessity of recovery email address confirmation in `setPassword` and `setRecoveryEmailAddress` methods
    is now returned by the corresponding `passwordState` and not by the error `EMAIL_UNCONFIRMED`.
* Improved the `MessageForwardInfo` class and added support for hidden original senders:
  - Removed the old `messageForwardedPost` and `messageForwardedFromUser` classes.
  - Added the class `messageForwardInfo` which contains information about the origin of the message, original sending
    date and identifies the place from which the message was forwarded the last time for messages forwarded to
    Saved Messages.
  - Added the classes `messageForwardOriginUser`, `messageForwardOriginHiddenUser` and `messageForwardOriginChannel`
    which describe the exact origins of a message.
* Improved getting the list of user profile photos:
  - Added the class `userProfilePhoto`, containing `id`, `added_date` and `sizes` of a profile photo.
  - Changed the type of the field `photos` in `userProfilePhotos` to a list of `userProfilePhoto` instead of
    a list of `photo`. `getUserProfilePhotos` now returns a date for each profile photo.
  - Removed the field `id` from the class `photo` (this field was only needed in the result of `getUserProfilePhotos`).
* Added the possibility to get a Telegram Passport authorization form before asking the user for a password:
  - Removed the parameter `password` from the method `getPassportAuthorizationForm`.
  - Moved the fields `elements` and `errors` from the class `passportAuthorizationForm` to
    the new class `passportElementsWithErrors`.
  - Added the method `getPassportAuthorizationFormAvailableElements` that takes the user's password and
    returns previously uploaded Telegram Passport elements and errors in them.
* Added the field `file_index` to the classes `passportElementErrorSourceFile` and
  `passportElementErrorSourceTranslationFile`.
* Added the method `getCurrentState` returning all updates describing the current `TDLib` state. It can be used to
  restore the correct state after connecting to a running TDLib instance.
* Added the class `updates` which contains a list of updates and is returned by the `getCurrentState` method.
* Added the update `updateChatOnlineMemberCount` which is automatically sent for open group chats if the number of
  online members in a group changes.
* Added support for custom language packs downloaded from the server:
  - Added the fields `base_language_pack_id`` to the class `languagePackInfo`. Strings from the base language pack
    must be used for untranslated keys from the chosen language pack.
  - Added the fields `plural_code`, `is_official`, `is_rtl`, `is_beta`, `is_installed`, `total_string_count`,
    `translated_string_count`, `translation_url` to the class `languagePackInfo`.
  - Added the method `addCustomServerLanguagePack` which adds a custom server language pack to the list of
    installed language packs.
  - Added the method `getLanguagePackInfo` which can be used for handling `https://t.me/setlanguage/...` links.
  - Added the method `synchronizeLanguagePack` which can be used to fetch the latest versions of all strings from
    a language pack.
    The method doesn't need to be called explicitly for the current used/base language packs.
  - The method `deleteLanguagePack` now also removes the language pack from the list of installed language packs.
* Added the method `getChatNotificationSettingsExceptions` which can be used to get chats with
  non-default notification settings.
* Added the parameter `hide_via_bot` to `sendInlineQueryResultMessage` which can be used for
  `getOption("animation_search_bot_username")`, `getOption("photo_search_bot_username")` and
  `getOption("venue_search_bot_username")` bots to hide that the message was sent via the bot.
* Added the class `chatReportReasonChildAbuse` which can be used to report a chat for child abuse.
* Added the method `getMessageLocally` which returns a message only if it is available locally without
  a network request.
* Added the method `writeGeneratedFilePart` which can be used to write a generated file if there is no direct access to
  TDLib's file system.
* Added the method `readFilePart` which can be used to read a file from the TDLib file cache.
* Added the class `filePart` to represent the result of the new `readFilePart` method.
* Added the field `log_size` to the `storageStatisticsFast` class which contains the size of the TDLib internal log.
  Previously the size was included into the value of the `database_size` field.
* Added the field `language_pack_database_size` to the `storageStatisticsFast` class which contains the size of the
  language pack database.
* Added the field `is_support` to the class `user` which can be used to identify Telegram Support accounts.
* Added the class `HttpUrl` encapsulating an HTTP URL.
* Added the method `getMessageLink` which can be used to create a private link (which works only for members) to
  a message in a supergroup or channel.
* Added support for channel statistics (coming soon):
  - Added the field `can_view_statistics` to the `supergroupFullInfo` class.
  - Added the method `getChatStatisticsUrl` which returns a URL with the chat statistics.
* Added support for server-side peer-to-peer calls privacy:
  - Added the class `userPrivacySettingAllowPeerToPeerCalls` for managing privacy.
  - Added the field `allow_p2p` to `callStateReady` class which must be used to determine whether
    a peer-to-peer connection can be used.
* Added the option `ignore_background_updates` which allows to skip all updates received while the TDLib instance was
  not running. The option does nothing if the database or secret chats are used.
* Added the read-only option `expect_blocking`, suggesting whether Telegram is blocked for the user.
* Added the read-only option `enabled_proxy_id`, containing the ID of the enabled proxy.
* Added the ability to identify password pending sessions (where the code was entered but not
  the two-step verification password) via the flag `is_password_pending` in the `session` class.
  TDLib guarantees that the sessions will be returned by the `getActiveSessions` method in the correct order.
* Added the classes `JsonValue` and `jsonObjectMember` which represent a JSON value and
  a member of a JSON object respectively as TDLib API objects.
* Added the synchronous methods `getJsonValue` and `getJsonString` for simple conversion between
  a JSON-encoded string and `JsonValue` TDLib API class.
* Added the methods `getApplicationConfig` and `saveApplicationLogEvent` to be used for testing purposes.
* Added the temporarily class `databaseStatistics` and the method `getDatabaseStatistics` for rough estimations of
  database tables size in a human-readable format.
* Made the method `Client.Execute` static in .NET interface.
* Removed the `on_closed` callback virtual method from low-level C++ ClientActor interface.
  Callback destructor can be used instead.
* Updated dependencies in the prebuilt TDLib for Android:
  - Updated SDK to SDK 28 in which helper classes was moved from `android.support.` to `androidx.` package.
  - Updated NDK to r19c, which dropped support for Android versions up to 4.0.4, so the minimum supported version is
    Android 4.1.
  - Updated OpenSSL to version 1.1.1.
  - Added x86_64 libraries.
* Added out of the box `FreeBSD` support.
* Significantly improved TDLib compilation time and decreased compiler RAM usage:
  - In native C++ interface `td_api::object_ptr` is now a simple homebrew const-propagating class instead of
    `std::unique_ptr`.
  - Added the script `SplitSource.php`, which can be used to split some source code files before building
    the library to reduce maximum RAM usage per file at the expense of increased build time.
* The update `updateOption` with the `version` option is now guaranteed to come before all other updates.
  It can now be used to dynamically discover available methods.
* Added the ability to delete incoming messages in private chats and revoke messages without a time limit:
  - Added the parameter `revoke` to the method `deleteChatHistory`; use it to delete chat history for all chat members.
  - Added the fields `can_be_deleted_only_for_self` and `can_be_deleted_for_all_users` to the class `chat`
    which can be used to determine for whom the chat can be deleted through the `deleteChatHistory` method.
  - The fields `Message.can_be_deleted_only_for_self` and `Message.can_be_deleted_for_all_users` can still be used
    to determine for whom the message can be deleted through the `deleteMessages` method.
* Added support for server-generated notifications about newly registered contacts:
  - Setting the option `disable_contact_registered_notifications` now affects all user sessions.
    When the option is enabled, the client will still receive `messageContactRegistered` message in the private chat,
    but there will be no notification about the message.
  - `getOption("disable_contact_registered_notifications")` can be used to fetch the actual value of the option,
    the option will not be updated automatically after a change from another device.
* Decreased the maximum allowed first name and last name length to 64, chat title length to 128,
  matching the new server-side limits.
* Decreased the maximum allowed value of the `forward_limit` parameter of the `addChatMember` method from 300 to 100,
  matching the new server-side limit.
* Added protection from opening two TDLib instances with the same database directory from one process.
* Added copying of notification settings of new secret chats from notification settings of
  the corresponding private chat.
* Excluded the sponsored chat (when using sponsored proxies) from unread counters.
* Allowed to pass decreased local_size in `setFileGenerationProgress` to restart the generation from the beginning.
* Added a check for modification time of original file in `inputFileGenerated` whenever possible.
  If the original file was changed, then TDLib will restart the generation.
* Added the destruction of MTProto keys on the server during log out.
* Added support for hexadecimal-encoded and decimal-encoded IPv4 proxy server addresses.
* Improved the behavior of `changeImportedContacts` which now also deletes contacts of users without Telegram accounts
  from the server.
* Added the ability to call `getStorageStatistics` before authorization.
* Allowed to pass `limit` = -`offset` for negative offset in the `getChatHistory` method.
* Changed the recommended `inputThumbnail` size to be at most 320x320 instead of the previous 90x90.
* Disabled building by default of the native C interface. Use `cmake --build . --target tdc` to build it.
* Numerous optimizations and bug fixes:
  - Network implementation for Windows was completely rewritten to allow a literally unlimited number of
    simultaneously used TDLib instances.
  - TDLib instances can now share working threads with each other. Only a limited number of threads will be created
    even if there are thousands of TDLib instances in a single process.
  - Removed the restriction on the size of update or response result in JSON interface.
  - Fixed pinning of the 5th chat when there is a sponsored chat.
  - Fixed IPv6 on Windows.
  - Improved network connections balancing, aliveness checks and overall stability.
  - Various autogenerated documentation fixes and improvements.

-----------------------------------------------------------------------------------------------------------------------

Changes in 1.3.0 (5 Sep 2018):

* Added a review of existing TDLib based [frameworks](https://github.com/tdlib/td/blob/master/example/README.md)
  in different programming languages.
* Added a [Getting started](https://core.telegram.org/tdlib/getting-started) guide describing the main TDLib concepts
  and basic principles required for library usage.
* When a chat is opened, only those messages that have been viewed are marked as read.
* Improved the proxy settings API:
  - A list of proxies is stored instead of just one proxy.
  - New methods `addProxy`, `editProxy`, `enableProxy`, `disableProxy`, `removeProxy` and `getProxies` were added
    instead of `setProxy` and `getProxy`.
  - Added the method `pingProxy` which can be used to compute time needed to receive a response from a Telegram server
    through a proxy or directly.
  - Added support for MTProto proxy via class `proxyTypeMtproto`.
  - Added support for HTTP proxy via class `proxyTypeHttp`.
  - For each proxy last time it was used is remembered.
  - Added the method `getProxyLink` which returns an HTTPS link that can be used to share a proxy with others.
* Improved the notification settings API. Scope notification settings are now properly synchronized between all devices
  and chat notification settings can be reset to their default values:
  - The `notificationSettings` class was split into `chatNotificationSettings` and `scopeNotificationSettings`.
  - Only two notification settings scopes are left: `notificationSettingsScopePrivateChats` which is responsible for
    default notification settings for private and secret chats and `notificationSettingsScopeGroupChats` for all other
    chats.
  - `updateNotificationSettings` was split into `updateChatNotificationSettings` and `updateScopeNotificationSettings`.
  - `setNotificationSettings` was split into `setChatNotificationSettings` and `setScopeNotificationSettings`.
  - `getNotificationSettings` was replaced with `getScopeNotificationSettings`.
* Added the field `filter` to the `searchChatMembers` method to support searching among administrators, bots,
  restricted and banned members.
* Added the ability to use synchronous requests and `setAlarm` before the library is initialized.
* Added the ability to send requests that don't need authentication before the library is initialized. These requests
  will be postponed and executed at the earliest opportunity. For example, `setNetworkType` can be used to disable the
  network for TDLib before the library tries to use it; `addProxy` can be used to add a proxy before any network
  activity; or `setOption("use_pfs")` can be used to guarantee that PFS is used for all requests.
* Added support for tg:// links in `inlineKeyboardButtonTypeUrl` and `textEntityTypeTextUrl`.
* Added the ability to call `deleteAccount` in the `authorizationStateWaitPassword` authorization state.
* Added the ability to call `checkAuthenticationCode` with an empty `first_name` for unregistered users to check the
  code validity.
* Added the methods `editMessageMedia` and `editInlineMessageMedia` for editing media messages content.
* Renamed the class `shippingAddress` to `address`.
* Changed the return value of the `requestPasswordRecovery` method from `passwordRecoveryInfo` to
  `emailAddressAuthenticationCodeInfo`.
* Added support for sponsored channels promoted by MTProto-proxies:
  - Added the field `is_sponsored` to the `chat` class.
  - Added `updateChatIsSponsored`, sent when this field changes.
* Added support for marking chats as unread:
  - Added the field `is_marked_as_unread` to `chat`.
  - Added the update `updateChatIsMarkedAsUnread`.
  - Added the method `toggleChatIsMarkedAsUnread`.
* Added support for a default value of `disable_notification`, used when a message is sent to the chat:
  - Added the field `default_disable_notification` to `chat` class.
  - Added the update `updateChatDefaultDisableNotification`.
  - Added the method `toggleChatDefaultDisableNotification`.
* Added the field `vcard` to the `contact` class.
* Added the field `type` to `venue`, which contains a provider-specific type of the venue,
* Added the update `updateUnreadChatCount`, enabled when the message database is used and sent when
  the number of unread chats has changed.
* Added the method `addLocalMessage` for adding a local message to a chat.
* Added the method `getDeepLinkInfo`, which can return information about `tg://` links that are not supported by
  the client.
* Added support for language packs:
  - Added the writable option `language_pack_database_path` which can be used to specify a path to a database
    for storing language pack strings, so that this database can be shared between different accounts.
    If not specified, language pack strings will be stored only in memory.
    Changes to the option are applied only on the next TDLib launch.
  - Added the writable option `localization_target` for setting up a name for the current localization target
    (currently supported: "android", "android_x", "ios", "macos" and "tdesktop").
  - Added the writable option `language_pack_id` for setting up an identifier of the currently used language pack from
    the current localization target (a "language pack" represents the collection of strings that can be used to display
    the interface of a particular application in a particular language).
  - Added the class `LanguagePackStringValue` describing the possible values of a string from a language pack.
  - Added the class `languagePackString` describing a string from a language pack.
  - Added the class `languagePackStrings` containing a list of language pack strings.
  - Added the class `languagePackInfo` containing information about a language pack from a localization target.
  - Added the class `localizationTargetInfo` containing information about a localization target.
  - Added the update `updateLanguagePackStrings` which is sent when some strings in a language pack have changed.
  - Added the synchronous method `getLanguagePackString` which can be used to get a language pack string from
    the local database.
  - Added the method `getLocalizationTargetInfo` which returns information about the current localization target.
  - Added the method `getLanguagePackStrings` which returns some or all strings from a language pack, possibly fetching
    them from the server.
  - Added the method `setCustomLanguagePack` for adding or editing a custom language pack.
  - Added the method `editCustomLanguagePackInfo` for editing information about a custom language pack.
  - Added the method `setCustomLanguagePackString` for adding, editing or deleting a string in a custom language pack.
  - Added the method `deleteLanguagePack` for deleting a language pack from the database.
  - Added the read-only option `suggested_language_pack_id` containing the identifier of the language pack,
    suggested for the user by the server.
* Added support for Telegram Passport:
  - Added two new message contents `messagePassportDataSent` for ordinary users and `messagePassportDataReceived`
    for bots containing information about Telegram Passport data shared with a bot.
  - Added the new file type `fileTypeSecure`.
  - Added the class `datedFile` containing information about a file along with the date it was uploaded.
  - Added the helper classes `date`, `personalDetails`, `identityDocument`, `inputIdentityDocument`,
    `personalDocument`, `inputPersonalDocument`, `passportElements`.
  - Added the class `PassportElementType` describing all supported types of Telegram Passport elements.
  - Added the class `PassportElement` containing information about a Telegram Passport element.
  - Added the class `InputPassportElement` containing information about a Telegram Passport element to save.
  - Added the classes `passportElementError` and `PassportElementErrorSource` describing an error in
    a Telegram Passport element.
  - Added the field `has_passport_data` to the `passwordState` class.
  - Added the methods `getPassportElement`, `getAllPassportElements`, `setPassportElement`, `deletePassportElement`
    for managing Telegram Passport elements.
  - Added the methods `getPassportAuthorizationForm` and `sendPassportAuthorizationForm` used for sharing
    Telegram Passport data with a service via a bot.
  - Added the methods `sendPhoneNumberVerificationCode`, `resendPhoneNumberVerificationCode` and
    `checkPhoneNumberVerificationCode` for verification of a phone number used for Telegram Passport.
  - Added the methods `sendEmailAddressVerificationCode`, `resendEmailAddressVerificationCode` and
    `checkEmailAddressVerificationCode` for verification of an email address used for Telegram Passport.
  - Added the method `getPreferredCountryLanguage` returning a most popular language in a country.
  - Added the classes `inputPassportElementError` and `InputPassportElementErrorSource` for bots describing an error in
    a Telegram Passport element.
  - Added the method `setPassportElementErrors` for bots.
  - Added the class `encryptedPassportElement` and `encryptedCredentials` for bots describing
    an encrypted Telegram Passport element.
* Improved support for Telegram terms of service:
  - Added the class `termsOfService`, containing information about the Telegram terms of service.
  - Added the field `terms_of_service` to `authorizationStateWaitCode`.
  - Added the update `updateTermsOfService` coming when new terms of service need to be accepted by the user.
  - Added the method `acceptTermsOfService` for accepting terms of service.
  - Removed the method `getTermsOfService`.
* Added the method `getMapThumbnailFile` which can be used to register and download a map thumbnail file.
* Added the methods `sendPhoneNumberConfirmationCode`, `resendPhoneNumberConfirmationCode` and
  `checkPhoneNumberConfirmationCode` which can be used to prevent an account from being deleted.
* Added the convenience methods `joinChat` and `leaveChat` which can be used instead of `setChatMemberStatus` to manage
  the current user's membership in a chat.
* Added the convenience method `getContacts` which can be used instead of `searchContacts` to get all contacts.
* Added the synchronous method `cleanFileName` which removes potentially dangerous characters from a file name.
* Added the method `getChatMessageCount` which can be used to get the number of shared media.
* Added the writable option `ignore_inline_thumbnails` which can be used to prevent file thumbnails sent
  by the server along with messages from being saved on the disk.
* Added the writable option `prefer_ipv6` which can be used to prefer IPv6 connections over IPv4.
* Added the writable option `disable_top_chats` which can be used to disable support for top chats.
* Added the class `chatReportReasonCopyright` for reporting chats containing infringing content.
* Added the method `clearAllDraftMessages` which can be used to delete all cloud drafts.
* Added the read-only options `message_text_length_max` and `message_caption_length_max`.
* Added the read-only options `animation_search_bot_username`, `photo_search_bot_username` and
  `venue_search_bot_username` containing usernames of bots which can be used in inline mode for animations, photos and
  venues search respectively.
* Numerous optimizations and bug fixes:
  - Fixed string encoding for .NET binding.
  - Fixed building TDLib SDK for Universal Windows Platform for ARM with MSVC 2017.
  - Fixed the Swift example project.
  - Fixed the syntax error in the Python example.
  - Sticker thumbnails can now have `webp` extensions if they are more likely to be in WEBP format.

-----------------------------------------------------------------------------------------------------------------------

Changes in 1.2.0 (20 Mar 2018):

* Added support for native .NET bindings through `C++/CLI` and `C++/CX`.
  See [using in .NET projects](README.md#using-dotnet) for more details.
* Added a C# example. See [README](example/csharp/README.md) for build and usage instructions.
* Added a build and usage example of TDLib SDK for Universal Windows Platform. See [README](example/uwp/README.md)
  for detailed build and usage instructions. Also see [Unigram](https://github.com/UnigramDev/Unigram), which is
  a full-featured client rewritten from scratch using TDLib SDK for Universal Windows Platform in less than 2 months.
* Added a Swift example. See [README](example/swift/README.md) for build and usage instructions.
* Added an example of building TDLib for iOS, watchOS, tvOS, and also macOS. See [README](example/ios/README.md) for
  detailed build instructions.
* Added README to [C++](example/cpp/README.md) and [python](example/python/README.md) examples.
* Link Time Optimization is disabled by default. Use `-DTD_ENABLE_LTO=ON` CMake option and CMake >= 3.9 to enable it.
* `updateNotificationSettings` is now automatically sent when the mute time expires for a chat.
* Added automatic sending of a corresponding `chatAction` when a file is being uploaded.
* `updateUserChatAction` with `chatActionCancel` is now automatically sent when the timeout expires for an action.
* Authorization states `authorizationStateWaitCode` and `authorizationStateWaitPassword` are now saved between
  library restarts for 5 minutes.
* Added new message content type `messageWebsiteConnected`.
* Added new text entity types `textEntityTypeCashtag` and `textEntityTypePhoneNumber`.
* Added new update `updateUnreadMessageCount`, enabled when message database is used.
* Method `joinChatByInviteLink` now returns the joined `chat`.
* Method `getWebPagePreview` now accepts `formattedText` instead of plain `string`.
* Added field `phone_number` to `authenticationCodeInfo`, which contains a phone number that is being authenticated.
* Added field `is_secret` to `messageAnimation`, `messagePhoto`, `messageVideo` and `messageVideoNote` classes,
  which denotes whether the thumbnail for the content must be blurred and the content must be shown only while tapped.
* Added field `expires_in` to `messageLocation` for live locations.
* Added flag `can_be_reported` to `chat` class.
* Added flag `supports_streaming` to classes `video` and `inputMessageVideo`.
* Added parameter `message_ids` to `reportChat`, which can be used to report specific messages.
* Added method `checkChatUsername` for checking whether a username can be set for a chat.
* Added method `getRepliedMessage`, which returns a message that is replied by a given message.
* Added method `getChatPinnedMessage`, which returns the pinned message from a chat.
* Added method `searchStickers` to search by emoji for popular stickers suggested by the server.
* Added method `searchStickerSets` to search by title and name for popular sticker sets suggested by the server.
* Added method `searchInstalledStickerSets` to search by title and name for installed sticker sets.
* Added methods for handling connected websites: `getConnectedWebsites`, `disconnectWebsite` and
  `disconnectAllWebsites`.
* Added method `getCountryCode`, which uses current user IP address to identify their country.
* Added option `t_me_url`.
* Fixed `BlackBerry` spelling in `deviceTokenBlackBerryPush`.
* Fixed return type of `getChatMessageByDate` method, which is `Message` and not `Messages`.
* Ensured that updateOption("my_id") comes before `updateAuthorizationState` with `authorizationStateReady`.
* Numerous optimizations and bug fixes.

-----------------------------------------------------------------------------------------------------------------------

Changes in 1.1.1 (4 Feb 2018):
* Fixed C JSON bindings compilation error.
* Fixed locale-dependent JSON generation.

-----------------------------------------------------------------------------------------------------------------------

Changes in 1.1.0 (31 Jan 2018):

* Methods `td::Log::set_file_path` and `td_set_log_file_path` now return whether they succeeded.
* Added methods `td::Log::set_max_file_size` and `td_set_log_max_file_size` for restricting maximum TDLib log size.
* Added methods `td::Log::set_fatal_error_callback` and `td_set_log_fatal_error_callback` for providing callbacks
  on fatal errors.
* JNI-bindings are now package-agnostic. Use CMake option `TD_ENABLE_JNI` to enable JNI-bindings.
* Added a Java example. See [README](example/java/README.md) for build and usage instructions.
* Added support for text entities in media captions.
  - Added new type `formattedText` containing a text with entities.
  - Replaced all string fields `caption` with fields of type `formattedText`.
  - Replaced fields `text` and `entities` with the field `text` of type `formattedText` in class `messageText`.
  - Replaced fields `text` and `entities` with the field `text` of type `formattedText` in class `inputMessageText`.
  - Replaced fields `text` and `text_entities` with the field `text` of type `formattedText` in class `game`.
  - Removed field `parse_mode` from class `inputMessageText`.
  - Added synchronous method `parseTextEntities`.
* updateNewMessage is now sent for all sent messages.
* updateChatLastMessage is now sent when any field of the last message in a chat changes.
* Reworked the `registerDevice` method:
  - Added parameter `other_user_ids` to method `registerDevice` to support multiple accounts.
  - It is now possible to specify tokens for VoIP pushes, WNS, web Push API, Tizen Push Service as `DeviceToken`.
  - Added support for Apple Push Notification Service inside App Sandbox.
* Added method `searchChatsOnServer` analogous to `searchChats`, but using server search.
* Results from the `searchChatsOnServer` method are now excluded from `searchPublicChats` results,
  so `searchChatsOnServer` (along with `searchContacts`) should be called whenever `searchPublicChats` is called
  to ensure that no results were omitted.
* Added parameter `as_album` to method `getPublicMessageLink` to enable getting public links for media albums.
* Added field `html` to class `publicMessageLink`, containing HTML-code for message/message album embedding.
* Added parameter `only_if_pending` to method `cancelDownloadFile` to allow keeping already started downloads.
* Methods `createPrivateChat`, `createBasicGroupChat`, `createSupergroupChat` and `createSecretChat`
  can now be called without a prior call to `getUser`/`getBasicGroup`/`getSupergroup`/`getSecretChat`.
* Added parameter `force` to methods `createPrivateChat`, `createBasicGroupChat` and `createSupergroupChat` to allow
  creating a chat without network requests.
* Numerous optimizations and bug fixes.

-----------------------------------------------------------------------------------------------------------------------
