//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/Global.h"
#include "td/telegram/Version.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/StorerBase.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/tl_helpers.h"
#include "td/utils/tl_parsers.h"
#include "td/utils/tl_storers.h"

namespace td {
namespace log_event {

template <class ParentT>
class WithVersion : public ParentT {
 public:
  using ParentT::ParentT;
  void set_version(int32 version) {
    version_ = version;
  }
  int32 version() const {
    return version_;
  }

 private:
  int32 version_{};
};

template <class ParentT, class ContextT>
class WithContext : public ParentT {
 public:
  using ParentT::ParentT;
  void set_context(ContextT context) {
    context_ = context;
  }
  ContextT context() const {
    return context_;
  }

 private:
  ContextT context_{};
};

class LogEvent {
 public:
  LogEvent() = default;
  LogEvent(const LogEvent &) = delete;
  LogEvent &operator=(const LogEvent &) = delete;
  virtual ~LogEvent() = default;
  enum HandlerType : uint32 {
    SecretChats = 1,
    Users = 2,
    Chats = 3,
    Channels = 4,
    SecretChatInfos = 5,
    WebPages = 0x10,
    SetPollAnswer = 0x20,
    StopPoll = 0x21,
    SendMessage = 0x100,
    DeleteMessage = 0x101,
    DeleteMessagesOnServer = 0x102,
    ReadHistoryOnServer = 0x103,
    ForwardMessages = 0x104,
    ReadMessageContentsOnServer = 0x105,
    SendBotStartMessage = 0x106,
    SendScreenshotTakenNotificationMessage = 0x107,
    SendInlineQueryResultMessage = 0x108,
    DeleteDialogHistoryOnServer = 0x109,
    ReadAllDialogMentionsOnServer = 0x10a,
    DeleteAllChannelMessagesFromSenderOnServer = 0x10b,
    ToggleDialogIsPinnedOnServer = 0x10c,
    ReorderPinnedDialogsOnServer = 0x10d,
    SaveDialogDraftMessageOnServer = 0x10e,
    UpdateDialogNotificationSettingsOnServer = 0x10f,
    UpdateScopeNotificationSettingsOnServer = 0x110,
    ResetAllNotificationSettingsOnServer = 0x111,
    ToggleDialogReportSpamStateOnServer = 0x112,
    RegetDialog = 0x113,
    ReadHistoryInSecretChat = 0x114,
    ToggleDialogIsMarkedAsUnreadOnServer = 0x115,
    SetDialogFolderIdOnServer = 0x116,
    DeleteScheduledMessagesOnServer = 0x117,
    ToggleDialogIsBlockedOnServer = 0x118,
    ReadMessageThreadHistoryOnServer = 0x119,
    BlockMessageSenderFromRepliesOnServer = 0x120,
    UnpinAllDialogMessagesOnServer = 0x121,
    DeleteAllCallMessagesOnServer = 0x122,
    DeleteDialogMessagesByDateOnServer = 0x123,
    ReadAllDialogReactionsOnServer = 0x124,
    GetChannelDifference = 0x140,
    AddMessagePushNotification = 0x200,
    EditMessagePushNotification = 0x201,
    ConfigPmcMagic = 0x1f18,
    BinlogPmcMagic = 0x4327
  };

  using Id = uint64;

  Id log_event_id() const {
    return log_event_id_;
  }
  void set_log_event_id(Id log_event_id) {
    log_event_id_ = log_event_id;
  }

  virtual StringBuilder &print(StringBuilder &sb) const {
    return sb << "[Logevent " << tag("id", log_event_id()) << "]";
  }

 private:
  Id log_event_id_{};
};

inline StringBuilder &operator<<(StringBuilder &sb, const LogEvent &log_event) {
  return log_event.print(sb);
}

class LogEventParser final : public WithVersion<WithContext<TlParser, Global *>> {
 public:
  explicit LogEventParser(Slice data) : WithVersion<WithContext<TlParser, Global *>>(data) {
    set_version(fetch_int());
    LOG_CHECK(version() < static_cast<int32>(Version::Next)) << "Wrong version " << version();
    set_context(G());
  }
};

class LogEventStorerCalcLength final : public WithContext<TlStorerCalcLength, Global *> {
 public:
  LogEventStorerCalcLength() : WithContext<TlStorerCalcLength, Global *>() {
    store_int(static_cast<int32>(Version::Next) - 1);
    set_context(G());
  }
};

class LogEventStorerUnsafe final : public WithContext<TlStorerUnsafe, Global *> {
 public:
  explicit LogEventStorerUnsafe(unsigned char *buf) : WithContext<TlStorerUnsafe, Global *>(buf) {
    store_int(static_cast<int32>(Version::Next) - 1);
    set_context(G());
  }
};

template <class T>
class LogEventStorerImpl final : public Storer {
 public:
  explicit LogEventStorerImpl(const T &event) : event_(event) {
  }

  size_t size() const final {
    LogEventStorerCalcLength storer;
    td::store(event_, storer);
    return storer.get_length();
  }
  size_t store(uint8 *ptr) const final {
    LogEventStorerUnsafe storer(ptr);
    td::store(event_, storer);
#ifdef TD_DEBUG
    T check_result;
    log_event_parse(check_result, Slice(ptr, storer.get_buf())).ensure();
#endif
    return static_cast<size_t>(storer.get_buf() - ptr);
  }

 private:
  const T &event_;
};

}  // namespace log_event

using LogEvent = log_event::LogEvent;
using LogEventParser = log_event::LogEventParser;
using LogEventStorerCalcLength = log_event::LogEventStorerCalcLength;
using LogEventStorerUnsafe = log_event::LogEventStorerUnsafe;

template <class T>
Status log_event_parse(T &data, Slice slice) TD_WARN_UNUSED_RESULT;

template <class T>
Status log_event_parse(T &data, Slice slice) {
  LogEventParser parser(slice);
  parse(data, parser);
  parser.fetch_end();
  return parser.get_status();
}

template <class T>
BufferSlice log_event_store(const T &data) {
  LogEventStorerCalcLength storer_calc_length;
  store(data, storer_calc_length);

  BufferSlice value_buffer{storer_calc_length.get_length()};
  auto ptr = value_buffer.as_slice().ubegin();
  LOG_CHECK(is_aligned_pointer<4>(ptr)) << ptr;

  LogEventStorerUnsafe storer_unsafe(ptr);
  store(data, storer_unsafe);

#ifdef TD_DEBUG
  T check_result;
  log_event_parse(check_result, value_buffer.as_slice()).ensure();
#endif
  return value_buffer;
}

template <class T>
log_event::LogEventStorerImpl<T> get_log_event_storer(const T &event) {
  return log_event::LogEventStorerImpl<T>(event);
}

}  // namespace td
