//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/ExportSettings.h"
#include "td/telegram/ExportWriter.h"
#include "td/telegram/td_api.h"

#include "td/utils/common.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"

namespace td {

class Td;

class ExportJsonWriter final : public ExportWriter {
 public:
  explicit ExportJsonWriter(Td *td);

  Status start(const ExportSettings &settings, ExportStats *stats) final;

  Status write_personal_info(const ExportPersonalInfo &info) final;

  Status write_contacts(const ExportContactList &contacts) final;

  Status write_sessions(const ExportSessionList &sessions) final;

  Status write_other_data(const ExportFile &file) final;

  Status write_dialogs_start(int32 dialog_count) final;

  Status write_dialog_start(const ExportDialogInfo &dialog) final;

  Status write_dialog_messages(const vector<ExportMessage> &messages, bool is_migrated,
                               const ExportCustomEmojiFiles &custom_emoji_files) final;

  Status write_dialog_end(bool only_my_messages) final;

  Status write_dialogs_end() final;

  Status finish() final;

 private:
  enum class DialogsMode : int32 { None, Chats, Left };

  Td *td_;
  ExportSettings settings_;
  unique_ptr<ExportOutputFile> output_;
  // the nesting level of the objects of chats
  int32 dialog_offset_ = 0;
  DialogsMode dialogs_mode_ = DialogsMode::None;
  bool has_dialogs_ = false;
  bool has_messages_ = false;

  // writes an item of the root object after the about text
  template <class T>
  Status write_root_item(Slice key, const T &value);

  Status write_dialog_list_start(bool is_left_channel);

  Status write_dialog_list_end();
};

}  // namespace td
