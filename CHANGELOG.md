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
