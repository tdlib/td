cmake_minimum_required(VERSION 3.0.2 FATAL_ERROR)

project(TDLib VERSION 1.7.2 LANGUAGES CXX C)

if (NOT DEFINED CMAKE_MODULE_PATH)
  set(CMAKE_MODULE_PATH "")
endif()
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake" "${CMAKE_MODULE_PATH}")

if (NOT DEFINED CMAKE_INSTALL_LIBDIR)
  set(CMAKE_INSTALL_LIBDIR "lib")
endif()
if (NOT DEFINED CMAKE_INSTALL_BINDIR)
  set(CMAKE_INSTALL_BINDIR "bin")
endif()
if (NOT DEFINED CMAKE_INSTALL_INCLUDEDIR)
  set(CMAKE_INSTALL_INCLUDEDIR "include")
endif()

if (POLICY CMP0054)
  # do not expand quoted arguments
  cmake_policy(SET CMP0054 NEW)
endif()
if (POLICY CMP0060)
  # link libraries by full path
  cmake_policy(SET CMP0060 NEW)
endif()

include(PreventInSourceBuild)
prevent_in_source_build()

option(TD_ENABLE_JNI "Use \"ON\" to enable JNI-compatible TDLib API.")
option(TD_ENABLE_DOTNET "Use \"ON\" to enable generation of C++/CLI or C++/CX TDLib API bindings.")

if (TD_ENABLE_DOTNET AND (CMAKE_VERSION VERSION_LESS "3.1.0"))
  message(FATAL_ERROR "CMake 3.1.0 or higher is required. You are running version ${CMAKE_VERSION}.")
endif()

enable_testing()

if (POLICY CMP0069)
  option(TD_ENABLE_LTO "Use \"ON\" to enable Link Time Optimization.")

  if (TD_ENABLE_LTO)
    cmake_policy(SET CMP0069 NEW)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT IPO_SUPPORTED)
    if (IPO_SUPPORTED)
      # set_property(DIRECTORY PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE) do not work?
      string(REPLACE ";" " " CXX_FLAGS_IPO "${CMAKE_CXX_COMPILE_OPTIONS_IPO}")
      message(STATUS "Use link time optimization CXX options: ${CXX_FLAGS_IPO}")
      set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${CXX_FLAGS_IPO}")

      string(REPLACE ";" " " C_FLAGS_IPO "${CMAKE_C_COMPILE_OPTIONS_IPO}")
      message(STATUS "Use link time optimization C options: ${C_FLAGS_IPO}")
      set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} ${C_FLAGS_IPO}")

      string(REPLACE ";" " " LINK_FLAGS_IPO "${CMAKE_CXX_LINK_OPTIONS_IPO}")
      message(STATUS "Use link time optimization linker options: ${LINK_FLAGS_IPO}")
      set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} ${LINK_FLAGS_IPO}")
    endif()
  endif()
endif()

# Configure CCache if available
find_program(CCACHE_FOUND ccache)
#set(CCACHE_FOUND 0)
if (CCACHE_FOUND)
  message(STATUS "Found ccache")
  set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
  set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK ccache)
else()
  message(STATUS "Could NOT find ccache (this is NOT an error)")
endif()

set(MEMPROF "" CACHE STRING "Use one of \"ON\", \"FAST\" or \"SAFE\" to enable memory profiling. \
Works under macOS and Linux when compiled using glibc. \
In FAST mode stack is unwinded only using frame pointers, which may fail. \
In SAFE mode stack is unwinded using backtrace function from execinfo.h, which may be very slow. \
By default both methods are used to achieve the maximum speed and accuracy")

if (EMSCRIPTEN)
  # use prebuilt zlib
  set(ZLIB_FOUND 1)
  set(ZLIB_LIBRARIES)
  set(ZLIB_INCLUDE_DIR)

  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s ALLOW_MEMORY_GROWTH=1 -s MEMFS_APPEND_TO_TYPED_ARRAYS=1 -s USE_ZLIB=1 -s MODULARIZE=1 \
    -s EXPORT_NAME=\"'createTdwebModule'\" -s WEBSOCKET_URL=\"'wss:#'\" -s EXTRA_EXPORTED_RUNTIME_METHODS=\"['FS','cwrap']\" -lidbfs.js -lworkerfs.js")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -s ALLOW_MEMORY_GROWTH=1 -s MEMFS_APPEND_TO_TYPED_ARRAYS=1 -s USE_ZLIB=1 -s MODULARIZE=1 \
    -s EXPORT_NAME=\"'createTdwebModule'\" -s WEBSOCKET_URL=\"'wss:#'\" -s EXTRA_EXPORTED_RUNTIME_METHODS=\"['FS','cwrap']\" -lidbfs.js -lworkerfs.js")
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -s DEMANGLE_SUPPORT=1 -s ASSERTIONS=1")
  set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -s DEMANGLE_SUPPORT=1 -s ASSERTIONS=1")

  if (ASMJS)
    set(TD_EMSCRIPTEN td_asmjs)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s WASM=0 -Wno-almost-asm")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -s WASM=0 -Wno-almost-asm")
  else()
    set(TD_EMSCRIPTEN td_wasm)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s WASM=1")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -s WASM=1")
  endif()
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --post-js ${CMAKE_CURRENT_SOURCE_DIR}/post.js")
endif()

if (NOT OPENSSL_FOUND)
  find_package(OpenSSL)
endif()
if (OPENSSL_FOUND)
  message(STATUS "Found OpenSSL: ${OPENSSL_INCLUDE_DIR} ${OPENSSL_LIBRARIES}")
endif()

set(CMAKE_THREAD_PREFER_PTHREAD ON)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

if (THREADS_HAVE_PTHREAD_ARG)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
endif()

include(TdSetUpCompiler)
td_set_up_compiler()

if (MSVC)
  option(TD_ENABLE_MULTI_PROCESSOR_COMPILATION "Use \"ON\" to enable multi-processor compilation.")

  if (TD_ENABLE_MULTI_PROCESSOR_COMPILATION)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")
  endif()
endif()

if (CLANG OR GCC)
  if (MEMPROF)
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag(-no-pie CXX_NO_PIE_FLAG)
    if (CXX_NO_PIE_FLAG)
      set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -no-pie")
    elseif (APPLE)
      set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-no_pie")
    endif()
  endif()
endif()

add_subdirectory(tdtl)

add_subdirectory(tdutils)

add_subdirectory(td/generate)

if (NOT CMAKE_CROSSCOMPILING)
  add_custom_target(prepare_cross_compiling DEPENDS tl_generate_common tdmime_auto tl_generate_json)
  if (TD_ENABLE_DOTNET)
    add_custom_target(remove_cpp_documentation
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      COMMAND remove_documentation ${TL_TD_API_AUTO_SOURCE} td/telegram/Client.h td/telegram/Log.h td/tl/TlObject.h
      COMMENT "Remove C++ documentation from sources"
      DEPENDS remove_documentation tl_generate_common generate_dotnet_api ${TL_TD_API_AUTO_SOURCE} td/telegram/Client.h td/telegram/Log.h td/tl/TlObject.h
    )

    add_dependencies(prepare_cross_compiling generate_dotnet_api remove_cpp_documentation)
  endif()
endif()

if (NOT OPENSSL_FOUND)
  message(WARNING "Can't find OpenSSL: stop TDLib building")
  return()
endif()

if (NOT ZLIB_FOUND)
  find_package(ZLIB)
endif()
if (NOT ZLIB_FOUND)
  message(WARNING "Can't find zlib: stop TDLib building")
  return()
endif()

if (NOT TDUTILS_MIME_TYPE)
  message(WARNING "Option TDUTILS_MIME_TYPE must not be disabled: stop TDLib building")
  return()
endif()

add_subdirectory(tdactor)

add_subdirectory(tdnet)

add_subdirectory(sqlite)

add_subdirectory(tddb)

add_subdirectory(test)

if (NOT CMAKE_CROSSCOMPILING)
  add_subdirectory(benchmark)
endif()


get_directory_property(HAS_PARENT PARENT_DIRECTORY)
if (HAS_PARENT)
  set(TL_TD_JSON_AUTO ${TL_TD_JSON_AUTO_SOURCE} PARENT_SCOPE) # used in tdbot
  set(TD_TEST_SOURCE ${TD_TEST_SOURCE} PARENT_SCOPE) # used to build tests
endif()


#SOURCE SETS

set_source_files_properties(${TL_TD_API_AUTO_SOURCE} PROPERTIES GENERATED TRUE)
if (TD_ENABLE_JNI OR ANDROID)
  set(TL_JNI_OBJECT_SOURCE
    td/tl/tl_jni_object.cpp
    td/tl/tl_jni_object.h
  )
else()
  set(TL_JNI_OBJECT_SOURCE)
endif()

set(TL_TD_API_SOURCE
  ${TL_TD_API_AUTO_SOURCE}
  ${TL_JNI_OBJECT_SOURCE}
  td/tl/TlObject.h
)

set_source_files_properties(${TL_TD_AUTO_SOURCE} PROPERTIES GENERATED TRUE)
set(TL_TD_SCHEME_SOURCE
  ${TL_TD_AUTO_SOURCE}
  td/tl/TlObject.h
  td/tl/tl_object_parse.h
  td/tl/tl_object_store.h
)

set_source_files_properties(${TL_TD_JSON_AUTO_SOURCE} PROPERTIES GENERATED TRUE)
set(TL_TD_JSON_SOURCE
  ${TL_TD_JSON_AUTO_SOURCE}
  td/tl/tl_json.h
)

set_source_files_properties(${TL_C_AUTO_SOURCE} PROPERTIES GENERATED TRUE)
set(TL_C_SCHEME_SOURCE
  ${TL_C_AUTO_SOURCE}
)

set_source_files_properties(${TL_DOTNET_AUTO_SOURCE} PROPERTIES GENERATED TRUE)
set(TL_DOTNET_SCHEME_SOURCE
  ${TL_DOTNET_AUTO_SOURCE}
  td/tl/tl_dotnet_object.h
)

set(TDLIB_SOURCE
  td/mtproto/AuthData.cpp
  td/mtproto/DhHandshake.cpp
  td/mtproto/Handshake.cpp
  td/mtproto/HandshakeActor.cpp
  td/mtproto/HttpTransport.cpp
  td/mtproto/IStreamTransport.cpp
  td/mtproto/KDF.cpp
  td/mtproto/Ping.cpp
  td/mtproto/PingConnection.cpp
  td/mtproto/ProxySecret.cpp
  td/mtproto/RawConnection.cpp
  td/mtproto/RSA.cpp
  td/mtproto/SessionConnection.cpp
  td/mtproto/TcpTransport.cpp
  td/mtproto/TlsInit.cpp
  td/mtproto/TlsReaderByteFlow.cpp
  td/mtproto/Transport.cpp
  td/mtproto/utils.cpp

  td/telegram/AnimationsManager.cpp
  td/telegram/AudiosManager.cpp
  td/telegram/AuthManager.cpp
  td/telegram/AutoDownloadSettings.cpp
  td/telegram/BackgroundManager.cpp
  td/telegram/BackgroundType.cpp
  td/telegram/CallActor.cpp
  td/telegram/CallDiscardReason.cpp
  td/telegram/CallManager.cpp
  td/telegram/CallbackQueriesManager.cpp
  td/telegram/ClientActor.cpp
  td/telegram/ConfigManager.cpp
  td/telegram/ConfigShared.cpp
  td/telegram/Contact.cpp
  td/telegram/ContactsManager.cpp
  td/telegram/CountryInfoManager.cpp
  td/telegram/DelayDispatcher.cpp
  td/telegram/Dependencies.cpp
  td/telegram/DeviceTokenManager.cpp
  td/telegram/DhCache.cpp
  td/telegram/DialogAction.cpp
  td/telegram/DialogAdministrator.cpp
  td/telegram/DialogDb.cpp
  td/telegram/DialogFilter.cpp
  td/telegram/DialogId.cpp
  td/telegram/DialogInviteLink.cpp
  td/telegram/DialogLocation.cpp
  td/telegram/DialogParticipant.cpp
  td/telegram/DialogSource.cpp
  td/telegram/Document.cpp
  td/telegram/DocumentsManager.cpp
  td/telegram/DraftMessage.cpp
  td/telegram/FileReferenceManager.cpp
  td/telegram/files/FileBitmask.cpp
  td/telegram/files/FileDb.cpp
  td/telegram/files/FileDownloader.cpp
  td/telegram/files/FileEncryptionKey.cpp
  td/telegram/files/FileFromBytes.cpp
  td/telegram/files/FileGcParameters.cpp
  td/telegram/files/FileGcWorker.cpp
  td/telegram/files/FileGenerateManager.cpp
  td/telegram/files/FileHashUploader.cpp
  td/telegram/files/FileLoader.cpp
  td/telegram/files/FileLoaderUtils.cpp
  td/telegram/files/FileLoadManager.cpp
  td/telegram/files/FileManager.cpp
  td/telegram/files/FileStats.cpp
  td/telegram/files/FileStatsWorker.cpp
  td/telegram/files/FileType.cpp
  td/telegram/files/FileUploader.cpp
  td/telegram/files/PartsManager.cpp
  td/telegram/files/ResourceManager.cpp
  td/telegram/Game.cpp
  td/telegram/Global.cpp
  td/telegram/GroupCallManager.cpp
  td/telegram/GroupCallParticipant.cpp
  td/telegram/HashtagHints.cpp
  td/telegram/InlineQueriesManager.cpp
  td/telegram/InputDialogId.cpp
  td/telegram/InputGroupCallId.cpp
  td/telegram/InputMessageText.cpp
  td/telegram/JsonValue.cpp
  td/telegram/LanguagePackManager.cpp
  td/telegram/Location.cpp
  td/telegram/logevent/LogEventHelper.cpp
  td/telegram/Logging.cpp
  td/telegram/MessageContent.cpp
  td/telegram/MessageContentType.cpp
  td/telegram/MessageEntity.cpp
  td/telegram/MessageId.cpp
  td/telegram/MessageReplyInfo.cpp
  td/telegram/MessagesDb.cpp
  td/telegram/MessageSearchFilter.cpp
  td/telegram/MessagesManager.cpp
  td/telegram/MessageTtlSetting.cpp
  td/telegram/misc.cpp
  td/telegram/net/AuthDataShared.cpp
  td/telegram/net/ConnectionCreator.cpp
  td/telegram/net/DcAuthManager.cpp
  td/telegram/net/DcOptionsSet.cpp
  td/telegram/net/MtprotoHeader.cpp
  td/telegram/net/NetActor.cpp
  td/telegram/net/NetQuery.cpp
  td/telegram/net/NetQueryCreator.cpp
  td/telegram/net/NetQueryDelayer.cpp
  td/telegram/net/NetQueryDispatcher.cpp
  td/telegram/net/NetQueryStats.cpp
  td/telegram/net/NetStatsManager.cpp
  td/telegram/net/Proxy.cpp
  td/telegram/net/PublicRsaKeyShared.cpp
  td/telegram/net/PublicRsaKeyWatchdog.cpp
  td/telegram/net/Session.cpp
  td/telegram/net/SessionProxy.cpp
  td/telegram/net/SessionMultiProxy.cpp
  td/telegram/NotificationManager.cpp
  td/telegram/NotificationSettings.cpp
  td/telegram/NotificationType.cpp
  td/telegram/Payments.cpp
  td/telegram/PasswordManager.cpp
  td/telegram/PhoneNumberManager.cpp
  td/telegram/PrivacyManager.cpp
  td/telegram/Photo.cpp
  td/telegram/PhotoSizeSource.cpp
  td/telegram/PollManager.cpp
  td/telegram/QueryCombiner.cpp
  td/telegram/ReplyMarkup.cpp
  td/telegram/ReportReason.cpp
  td/telegram/RestrictionReason.cpp
  td/telegram/SecretChatActor.cpp
  td/telegram/SecretChatDb.cpp
  td/telegram/SecretChatsManager.cpp
  td/telegram/SecureManager.cpp
  td/telegram/SecureStorage.cpp
  td/telegram/SecureValue.cpp
  td/telegram/SendCodeHelper.cpp
  td/telegram/SequenceDispatcher.cpp
  td/telegram/SpecialStickerSetType.cpp
  td/telegram/StateManager.cpp
  td/telegram/StickersManager.cpp
  td/telegram/StorageManager.cpp
  td/telegram/SuggestedAction.cpp
  td/telegram/Td.cpp
  td/telegram/TdDb.cpp
  td/telegram/TermsOfService.cpp
  td/telegram/TopDialogManager.cpp
  td/telegram/UpdatesManager.cpp
  td/telegram/Venue.cpp
  td/telegram/VideoNotesManager.cpp
  td/telegram/VideosManager.cpp
  td/telegram/VoiceNotesManager.cpp
  td/telegram/WebPageBlock.cpp
  td/telegram/WebPagesManager.cpp

  td/mtproto/AuthData.h
  td/mtproto/AuthKey.h
  td/mtproto/CryptoStorer.h
  td/mtproto/DhHandshake.h
  td/mtproto/Handshake.h
  td/mtproto/HandshakeActor.h
  td/mtproto/HandshakeConnection.h
  td/mtproto/HttpTransport.h
  td/mtproto/IStreamTransport.h
  td/mtproto/KDF.h
  td/mtproto/NoCryptoStorer.h
  td/mtproto/PacketInfo.h
  td/mtproto/PacketStorer.h
  td/mtproto/Ping.h
  td/mtproto/PingConnection.h
  td/mtproto/ProxySecret.h
  td/mtproto/Query.h
  td/mtproto/RawConnection.h
  td/mtproto/RSA.h
  td/mtproto/SessionConnection.h
  td/mtproto/TcpTransport.h
  td/mtproto/TlsInit.h
  td/mtproto/TlsReaderByteFlow.h
  td/mtproto/Transport.h
  td/mtproto/TransportType.h
  td/mtproto/utils.h

  td/telegram/AccessRights.h
  td/telegram/AnimationsManager.h
  td/telegram/AudiosManager.h
  td/telegram/AuthManager.h
  td/telegram/AutoDownloadSettings.h
  td/telegram/BackgroundId.h
  td/telegram/BackgroundManager.h
  td/telegram/BackgroundType.h
  td/telegram/CallActor.h
  td/telegram/CallDiscardReason.h
  td/telegram/CallId.h
  td/telegram/CallManager.h
  td/telegram/CallbackQueriesManager.h
  td/telegram/ChannelId.h
  td/telegram/ChatId.h
  td/telegram/ClientActor.h
  td/telegram/ConfigManager.h
  td/telegram/ConfigShared.h
  td/telegram/Contact.h
  td/telegram/ContactsManager.h
  td/telegram/CountryInfoManager.h
  td/telegram/DelayDispatcher.h
  td/telegram/Dependencies.h
  td/telegram/DeviceTokenManager.h
  td/telegram/DhCache.h
  td/telegram/DhConfig.h
  td/telegram/DialogAction.h
  td/telegram/DialogAdministrator.h
  td/telegram/DialogDate.h
  td/telegram/DialogDb.h
  td/telegram/DialogFilter.h
  td/telegram/DialogFilterId.h
  td/telegram/DialogId.h
  td/telegram/DialogInviteLink.h
  td/telegram/DialogListId.h
  td/telegram/DialogLocation.h
  td/telegram/DialogParticipant.h
  td/telegram/DialogSource.h
  td/telegram/Document.h
  td/telegram/DocumentsManager.h
  td/telegram/DraftMessage.h
  td/telegram/FileReferenceManager.h
  td/telegram/files/FileBitmask.h
  td/telegram/files/FileData.h
  td/telegram/files/FileDb.h
  td/telegram/files/FileDbId.h
  td/telegram/files/FileDownloader.h
  td/telegram/files/FileEncryptionKey.h
  td/telegram/files/FileFromBytes.h
  td/telegram/files/FileGcParameters.h
  td/telegram/files/FileGcWorker.h
  td/telegram/files/FileGenerateManager.h
  td/telegram/files/FileHashUploader.h
  td/telegram/files/FileId.h
  td/telegram/files/FileLoaderActor.h
  td/telegram/files/FileLoader.h
  td/telegram/files/FileLoaderUtils.h
  td/telegram/files/FileLoadManager.h
  td/telegram/files/FileLocation.h
  td/telegram/files/FileManager.h
  td/telegram/files/FileSourceId.h
  td/telegram/files/FileStats.h
  td/telegram/files/FileStatsWorker.h
  td/telegram/files/FileType.h
  td/telegram/files/FileUploader.h
  td/telegram/files/PartsManager.h
  td/telegram/files/ResourceManager.h
  td/telegram/files/ResourceState.h
  td/telegram/FolderId.h
  td/telegram/FullMessageId.h
  td/telegram/Game.h
  td/telegram/Global.h
  td/telegram/GroupCallId.h
  td/telegram/GroupCallManager.h
  td/telegram/GroupCallParticipant.h
  td/telegram/HashtagHints.h
  td/telegram/InlineQueriesManager.h
  td/telegram/InputDialogId.h
  td/telegram/InputGroupCallId.h
  td/telegram/InputMessageText.h
  td/telegram/JsonValue.h
  td/telegram/LanguagePackManager.h
  td/telegram/Location.h
  td/telegram/logevent/LogEvent.h
  td/telegram/logevent/LogEventHelper.h
  td/telegram/logevent/SecretChatEvent.h
  td/telegram/Logging.h
  td/telegram/MessageContent.h
  td/telegram/MessageContentType.h
  td/telegram/MessageCopyOptions.h
  td/telegram/MessageEntity.h
  td/telegram/MessageId.h
  td/telegram/MessageReplyInfo.h
  td/telegram/MessagesDb.h
  td/telegram/MessageSearchFilter.h
  td/telegram/MessagesManager.h
  td/telegram/MessageTtlSetting.h
  td/telegram/misc.h
  td/telegram/net/AuthDataShared.h
  td/telegram/net/ConnectionCreator.h
  td/telegram/net/DcAuthManager.h
  td/telegram/net/DcId.h
  td/telegram/net/DcOptions.h
  td/telegram/net/DcOptionsSet.h
  td/telegram/net/MtprotoHeader.h
  td/telegram/net/NetActor.h
  td/telegram/net/NetQuery.h
  td/telegram/net/NetQueryCounter.h
  td/telegram/net/NetQueryCreator.h
  td/telegram/net/NetQueryDelayer.h
  td/telegram/net/NetQueryDispatcher.h
  td/telegram/net/NetQueryStats.h
  td/telegram/net/NetStatsManager.h
  td/telegram/net/NetType.h
  td/telegram/net/Proxy.h
  td/telegram/net/PublicRsaKeyShared.h
  td/telegram/net/PublicRsaKeyWatchdog.h
  td/telegram/net/Session.h
  td/telegram/net/SessionProxy.h
  td/telegram/net/SessionMultiProxy.h
  td/telegram/net/TempAuthKeyWatchdog.h
  td/telegram/Notification.h
  td/telegram/NotificationGroupId.h
  td/telegram/NotificationGroupKey.h
  td/telegram/NotificationGroupType.h
  td/telegram/NotificationId.h
  td/telegram/NotificationManager.h
  td/telegram/NotificationSettings.h
  td/telegram/NotificationType.h
  td/telegram/PasswordManager.h
  td/telegram/Payments.h
  td/telegram/PhoneNumberManager.h
  td/telegram/Photo.h
  td/telegram/PhotoSizeSource.h
  td/telegram/PollId.h
  td/telegram/PollManager.h
  td/telegram/PrivacyManager.h
  td/telegram/PtsManager.h
  td/telegram/PublicDialogType.h
  td/telegram/QueryCombiner.h
  td/telegram/ReplyMarkup.h
  td/telegram/ReportReason.h
  td/telegram/RequestActor.h
  td/telegram/RestrictionReason.h
  td/telegram/ScheduledServerMessageId.h
  td/telegram/SecretChatActor.h
  td/telegram/SecretChatId.h
  td/telegram/SecretChatDb.h
  td/telegram/SecretChatsManager.h
  td/telegram/SecretInputMedia.h
  td/telegram/SecureManager.h
  td/telegram/SecureStorage.h
  td/telegram/SecureValue.h
  td/telegram/SendCodeHelper.h
  td/telegram/SequenceDispatcher.h
  td/telegram/ServerMessageId.h
  td/telegram/SetWithPosition.h
  td/telegram/SpecialStickerSetType.h
  td/telegram/StateManager.h
  td/telegram/StickerSetId.h
  td/telegram/StickersManager.h
  td/telegram/StorageManager.h
  td/telegram/SuggestedAction.h
  td/telegram/Td.h
  td/telegram/TdCallback.h
  td/telegram/TdDb.h
  td/telegram/TdParameters.h
  td/telegram/TermsOfService.h
  td/telegram/TopDialogCategory.h
  td/telegram/TopDialogManager.h
  td/telegram/UniqueId.h
  td/telegram/UpdatesManager.h
  td/telegram/UserId.h
  td/telegram/Venue.h
  td/telegram/Version.h
  td/telegram/VideoNotesManager.h
  td/telegram/VideosManager.h
  td/telegram/VoiceNotesManager.h
  td/telegram/WebPageBlock.h
  td/telegram/WebPageId.h
  td/telegram/WebPagesManager.h

  td/telegram/AnimationsManager.hpp
  td/telegram/AudiosManager.hpp
  td/telegram/AuthManager.hpp
  td/telegram/BackgroundType.hpp
  td/telegram/DialogFilter.hpp
  td/telegram/Document.hpp
  td/telegram/DocumentsManager.hpp
  td/telegram/DraftMessage.hpp
  td/telegram/FileReferenceManager.hpp
  td/telegram/files/FileData.hpp
  td/telegram/files/FileId.hpp
  td/telegram/files/FileLocation.hpp
  td/telegram/files/FileManager.hpp
  td/telegram/Game.hpp
  td/telegram/InputMessageText.hpp
  td/telegram/MessageEntity.hpp
  td/telegram/NotificationSettings.hpp
  td/telegram/Payments.hpp
  td/telegram/Photo.hpp
  td/telegram/PhotoSizeSource.hpp
  td/telegram/PollId.hpp
  td/telegram/PollManager.hpp
  td/telegram/ReplyMarkup.hpp
  td/telegram/SecureValue.hpp
  td/telegram/SendCodeHelper.hpp
  td/telegram/StickerSetId.hpp
  td/telegram/StickersManager.hpp
  td/telegram/VideoNotesManager.hpp
  td/telegram/VideosManager.hpp
  td/telegram/VoiceNotesManager.hpp

  ${TL_TD_SCHEME_SOURCE}
)

set(MEMPROF_SOURCE
  memprof/memprof.cpp
  memprof/memprof.h
)

#RULES

file(MAKE_DIRECTORY auto)

if (CMAKE_HOST_WIN32)
  set(GIT_COMMIT_CMD powershell -ExecutionPolicy ByPass ./gen_git_commit_h.ps1)
else()
  set(GIT_COMMIT_CMD ./gen_git_commit_h.sh)
endif()

add_custom_target(git_commit ALL
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMAND ${GIT_COMMIT_CMD}
  COMMENT "Generate git_commit.h"
)

#LIBRARIES

# memprof - simple library for memory usage profiling
add_library(memprof STATIC ${MEMPROF_SOURCE})
target_include_directories(memprof PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_link_libraries(memprof PRIVATE tdutils)
if (MEMPROF)
  target_compile_definitions(memprof PRIVATE -DUSE_MEMPROF=1)
  if (MEMPROF STREQUAL "SAFE")
    target_compile_definitions(memprof PRIVATE -DUSE_MEMPROF_SAFE=1)
  elseif (MEMPROF STREQUAL "FAST")
    target_compile_definitions(memprof PRIVATE -DUSE_MEMPROF_FAST=1)
  elseif (NOT MEMPROF)
    message(FATAL_ERROR "Unsupported MEMPROF value \"${MEMPROF}\"")
  endif()
endif()


add_library(tdapi STATIC ${TL_TD_API_SOURCE})
target_include_directories(tdapi PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}> INTERFACE $<BUILD_INTERFACE:${TL_TD_AUTO_INCLUDE_DIR}>)
target_link_libraries(tdapi PRIVATE tdutils)

if (TD_ENABLE_JNI AND NOT ANDROID) # jni is available by default on Android
  if (NOT JNI_FOUND)
    find_package(JNI REQUIRED)
  endif()
  message(STATUS "Found JNI: ${JNI_INCLUDE_DIRS} ${JNI_LIBRARIES}")
  target_include_directories(tdapi PUBLIC ${JAVA_INCLUDE_PATH} ${JAVA_INCLUDE_PATH2})
  target_link_libraries(tdapi PUBLIC ${JAVA_JVM_LIBRARY})
endif()

if (NOT CMAKE_CROSSCOMPILING)
  add_dependencies(tdapi tl_generate_common)
endif()

# tdcore - mostly internal TDLib interface. One should use tdactor for interactions with it.
add_library(tdcore STATIC ${TDLIB_SOURCE})
target_include_directories(tdcore PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}> $<BUILD_INTERFACE:${TL_TD_AUTO_INCLUDE_DIR}>)
target_include_directories(tdcore SYSTEM PRIVATE ${OPENSSL_INCLUDE_DIR})
target_link_libraries(tdcore PUBLIC tdapi tdactor tdutils tdnet tddb PRIVATE ${OPENSSL_CRYPTO_LIBRARY} ${CMAKE_DL_LIBS} ${ZLIB_LIBRARIES})
if (WIN32)
  if (MINGW)
    target_link_libraries(tdcore PRIVATE ws2_32 mswsock crypt32)
  else()
    target_link_libraries(tdcore PRIVATE ws2_32 Mswsock Crypt32)
  endif()
endif()

if (NOT CMAKE_CROSSCOMPILING)
  add_dependencies(tdcore tl_generate_common)
  if (TD_ENABLE_JNI)
    add_dependencies(tdcore td_generate_java_api)
  endif()
  if (TD_ENABLE_DOTNET)
    add_dependencies(tdcore remove_cpp_documentation)
  endif()
endif()

add_library(tdclient td/telegram/Client.cpp td/telegram/Client.h td/telegram/Log.cpp td/telegram/Log.h)
target_include_directories(tdclient PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_link_libraries(tdclient PUBLIC tdapi PRIVATE tdcore)

if (TD_ENABLE_DOTNET)
  add_library(tddotnet SHARED
    td/telegram/ClientDotNet.cpp
    td/telegram/LogDotNet.cpp
    ${TL_DOTNET_SCHEME_SOURCE}
  )
  set_target_properties(tddotnet PROPERTIES OUTPUT_NAME Telegram.Td)
  target_link_libraries(tddotnet PRIVATE tdclient tdutils)
  target_include_directories(tddotnet PUBLIC
    $<BUILD_INTERFACE:${TL_TD_AUTO_INCLUDE_DIR}>
  )
  if (NOT CMAKE_CROSSCOMPILING)
    add_dependencies(tddotnet generate_dotnet_api)
  endif()

  target_compile_options(tddotnet PRIVATE "/doc")
  if (CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")
    set_target_properties(tddotnet PROPERTIES VS_WINRT_COMPONENT "true")
    target_compile_options(tddotnet PUBLIC "/ZW")
  else()
    set_target_properties(tddotnet PROPERTIES COMPILE_FLAGS "/GR /clr")
    target_compile_options(tddotnet PUBLIC "/EHa")
  endif()
endif()

# tdc - TDLib interface in pure c.
add_library(tdc STATIC EXCLUDE_FROM_ALL ${TL_C_SCHEME_SOURCE} td/telegram/td_c_client.cpp td/telegram/td_c_client.h)
target_include_directories(tdc PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<BUILD_INTERFACE:${TL_TD_AUTO_INCLUDE_DIR}>)
target_link_libraries(tdc PRIVATE tdclient tdutils)
if (NOT CMAKE_CROSSCOMPILING)
  add_dependencies(tdc tl_generate_c)
endif()

add_library(tdjson_private STATIC ${TL_TD_JSON_SOURCE} td/telegram/ClientJson.cpp td/telegram/ClientJson.h)
target_include_directories(tdjson_private PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<BUILD_INTERFACE:${TL_TD_AUTO_INCLUDE_DIR}>)
target_link_libraries(tdjson_private PUBLIC tdclient tdutils)
if (NOT CMAKE_CROSSCOMPILING)
  add_dependencies(tdjson_private tl_generate_common tl_generate_json)
  if (TD_ENABLE_DOTNET)
    add_dependencies(tdjson_private remove_cpp_documentation)
  endif()
endif()

set(TD_JSON_HEADERS td/telegram/td_json_client.h td/telegram/td_log.h)
set(TD_JSON_SOURCE td/telegram/td_json_client.cpp td/telegram/td_log.cpp)

include(GenerateExportHeader)

add_library(tdjson SHARED ${TD_JSON_SOURCE} ${TD_JSON_HEADERS})
target_link_libraries(tdjson PRIVATE tdjson_private)
generate_export_header(tdjson EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/td/telegram/tdjson_export.h)
target_include_directories(tdjson PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
if (APPLE)
  set_target_properties(tdjson PROPERTIES LINK_FLAGS "-Wl,-exported_symbols_list,${CMAKE_CURRENT_SOURCE_DIR}/tdclientjson_export_list")
endif()

add_library(tdjson_static STATIC ${TD_JSON_SOURCE} ${TD_JSON_HEADERS})
target_link_libraries(tdjson_static PRIVATE tdjson_private)
target_compile_definitions(tdjson_static PUBLIC TDJSON_STATIC_DEFINE)
target_include_directories(tdjson_static PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)

if (EMSCRIPTEN)
  set(TD_EMSCRIPTEN_SRC td/telegram/td_emscripten.cpp)
  add_executable(${TD_EMSCRIPTEN} ${TD_EMSCRIPTEN_SRC})
  target_include_directories(${TD_EMSCRIPTEN} PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
  target_link_libraries(${TD_EMSCRIPTEN} PRIVATE tdjson_static tdactor)
endif()

#EXECUTABLES
if (NOT CMAKE_CROSSCOMPILING)
  add_executable(tg_cli td/telegram/cli.cpp ${TL_TD_JSON_SOURCE})

  if (NOT READLINE_FOUND)
    find_package(Readline)
  endif()
  if (NOT READLINE_FOUND)
    message(STATUS "Could NOT find Readline (this is NOT an error)")
  else()
    message(STATUS "Found Readline: ${READLINE_INCLUDE_DIR} ${READLINE_LIBRARY}")
    if (NOT USABLE_READLINE_FOUND)
      set(CMAKE_REQUIRED_INCLUDES "${READLINE_INCLUDE_DIR}")
      set(CMAKE_REQUIRED_LIBRARIES "${READLINE_LIBRARY}")
      include(CheckCXXSourceCompiles)
      unset(USABLE_READLINE_FOUND CACHE)
      check_cxx_source_compiles("#include <stdio.h>\n#include <readline/readline.h>\nint main() { rl_free(0); }" USABLE_READLINE_FOUND)
      if (NOT USABLE_READLINE_FOUND)
        message(STATUS "Found Readline is too old, ignore it (this is NOT an error)")
        unset(READLINE_INCLUDE_DIR CACHE)
        unset(READLINE_LIBRARY CACHE)
      endif()
    endif()
    if (USABLE_READLINE_FOUND)
      target_link_libraries(tg_cli PRIVATE ${READLINE_LIBRARY})
      target_include_directories(tg_cli SYSTEM PRIVATE ${READLINE_INCLUDE_DIR})
      target_compile_definitions(tg_cli PRIVATE -DUSE_READLINE=1)
    endif()
  endif()
  target_link_libraries(tg_cli PRIVATE memprof tdclient tdcore)
  add_dependencies(tg_cli tl_generate_json)
endif()

#Exported libraries
add_library(TdStatic INTERFACE)
target_link_libraries(TdStatic INTERFACE tdclient)

add_library(TdJson INTERFACE)
target_link_libraries(TdJson INTERFACE tdjson)

add_library(TdJsonStatic INTERFACE)
target_link_libraries(TdJsonStatic INTERFACE tdjson_static)

add_library(Td::TdStatic ALIAS TdStatic)
add_library(Td::TdJson ALIAS TdJson)
add_library(Td::TdJsonStatic ALIAS TdJsonStatic)

install(TARGETS tdjson TdJson tdjson_static TdJsonStatic tdjson_private tdclient tdcore tdapi TdStatic EXPORT TdTargets
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

# generate pkg-config files
include(GeneratePkgConfig)

generate_pkgconfig(tdutils "Telegram Library - Utils")
generate_pkgconfig(tdactor "Telegram Library - Actor")
generate_pkgconfig(tdnet "Telegram Library - Net")
generate_pkgconfig(tdsqlite "Telegram Library - SQLite")
generate_pkgconfig(tddb "Telegram Library - Database")
if (MEMPROF)
  # generate_pkgconfig(memprof "memprof - simple library for memory usage profiling")
endif()
generate_pkgconfig(tdcore "Telegram Library - Core")
generate_pkgconfig(tdclient "Telegram Library - C++ Interface")
if (TD_ENABLE_DOTNET)
  # generate_pkgconfig(tddotnet "Telegram Library - C# Interface")
endif()
# generate_pkgconfig(tdc "Telegram Library - C interface")
generate_pkgconfig(tdapi "Telegram Library - API")
generate_pkgconfig(tdjson_private "Telegram Library - JSON interface (private)")
generate_pkgconfig(tdjson "Telegram Library - JSON interface (shared)")
generate_pkgconfig(tdjson_static "Telegram Library - JSON interface (static)")

install(EXPORT TdTargets
  FILE TdTargets.cmake
  NAMESPACE Td::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Td"
)

# Install tdjson/tdjson_static:
install(FILES ${TD_JSON_HEADERS} "${CMAKE_CURRENT_BINARY_DIR}/td/telegram/tdjson_export.h" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/td/telegram")
# Install tdclient:
install(FILES td/telegram/Client.h td/telegram/Log.h DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/td/telegram")
# Install tdapi:
install(FILES td/tl/TlObject.h DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/td/tl")
install(FILES "${TL_TD_AUTO_INCLUDE_DIR}/td/telegram/td_api.h" "${TL_TD_AUTO_INCLUDE_DIR}/td/telegram/td_api.hpp" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/td/telegram")
if (TD_ENABLE_JNI)
  install(FILES td/tl/tl_jni_object.h DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/td/tl")
endif()
if (MSVC AND VCPKG_TOOLCHAIN)
  install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/" DESTINATION "${CMAKE_INSTALL_BINDIR}" FILES_MATCHING PATTERN "*.dll")
endif()

include(CMakePackageConfigHelpers)
write_basic_package_version_file("TdConfigVersion.cmake"
  VERSION "${TDLib_VERSION}"
  COMPATIBILITY ExactVersion
)
install(FILES "TdConfig.cmake" "${CMAKE_CURRENT_BINARY_DIR}/TdConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Td"
)

# Add SOVERSION to shared libraries
set_property(TARGET tdclient PROPERTY SOVERSION "${TDLib_VERSION}")
set_property(TARGET tdjson PROPERTY SOVERSION "${TDLib_VERSION}")
