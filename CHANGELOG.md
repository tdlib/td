Changes in 1.3.0:

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
  - Fixed string encoding for C# binding.
  - Fixed building TDLib SDK for Universal Windows Platform for ARM with MSVC 2017.
  - Fixed the Swift example project.
  - Fixed the syntax error in the Python example.
  - Sticker thumbnails can now have `webp` extensions if they are more likely to be in WEBP format.

-----------------------------------------------------------------------------------------------------------------------

Changes in 1.2.0:

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
* Method `joinChatByInviteLink` now returns the joined `Chat`.
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
* Added method `getCountryCode`, which uses current user IP to identify their country.
* Added option `t_me_url`.
* Fixed `BlackBerry` spelling in `deviceTokenBlackBerryPush`.
* Fixed return type of `getChatMessageByDate` method, which is `Message` and not `Messages`.
* Ensured that updateOption("my_id") comes before `updateAuthorizationState` with `authorizationStateReady`.
* Numerous optimizations and bug fixes.

-----------------------------------------------------------------------------------------------------------------------

Changes in 1.1.1:
* Fixed C JSON bindings compilation error.
* Fixed locale-dependent JSON generation.

-----------------------------------------------------------------------------------------------------------------------

Changes in 1.1.0:

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
* Methods `createPrivateChat`, `createBasciGroupChat`, `createSupergroupChat` and `createSecretChat`
  can now be called without a prior call to `getUser`/`getBasicGroup`/`getSupergorup`/`getSecretChat`.
* Added parameter `force` to methods `createPrivateChat`, `createBasciGroupChat` and `createSupergroupChat` to allow
  creating a chat without network requests.
* Numerous optimizations and bug fixes.

-----------------------------------------------------------------------------------------------------------------------
