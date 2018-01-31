Changes in 1.1.0:

* Methods `td::Log::set_file_path` and `td_set_log_file_path` now return whether they succeeded.
* Added methods `td::Log::set_max_file_size` and `td_set_log_max_file_size` for restricting maximum TDLib log size.
* Added methods `td::Log::set_fatal_error_callback` and `td_set_log_fatal_error_callback` for providing callbacks
  on fatal errors.
* JNI-bindings are now package-agnostic. Use CMake option `TD_ENABLE_JNI` to enable JNI-bindings.
* Added a Java example. See [Readme](example/java/README.md) for build and usage instructions.
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
* Add method `searchChatsOnServer` analogous to `searchChats`, but using server search.
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
