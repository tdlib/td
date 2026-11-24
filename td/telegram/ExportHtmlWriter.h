//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/ExportSettings.h"
#include "td/telegram/ExportWriter.h"

#include "td/utils/common.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"

namespace td {

class Td;

class ExportHtmlPage;

struct ExportHtmlMessageInfo;

class ExportHtmlWriter final : public ExportWriter {
 public:
  explicit ExportHtmlWriter(Td *td);
  ExportHtmlWriter(const ExportHtmlWriter &) = delete;
  ExportHtmlWriter &operator=(const ExportHtmlWriter &) = delete;
  ExportHtmlWriter(ExportHtmlWriter &&) = delete;
  ExportHtmlWriter &operator=(ExportHtmlWriter &&) = delete;
  ~ExportHtmlWriter() final;

  Status start(const ExportSettings &settings, ExportStats *stats) final;

  Status write_dialogs_start(int32 dialog_count) final;

  Status write_dialog_start(const ExportDialogInfo &dialog) final;

  Status write_dialog_messages(const vector<ExportMessage> &messages, bool is_migrated,
                               const ExportCustomEmojiFiles &custom_emoji_files) final;

  Status write_dialog_end(bool only_my_messages) final;

  Status write_dialogs_end() final;

  Status finish() final;

 private:
  enum class DialogsMode : int32 { None, Chats, Left };

  struct Section {
    int32 priority = 0;
    string label;
    string type;
    int32 count = 0;
    string path;
  };

  Td *td_;
  ExportSettings settings_;
  ExportStats *stats_ = nullptr;

  // the page with the sections of the exported data
  unique_ptr<ExportHtmlPage> summary_;
  bool summary_needs_divider_ = false;
  bool has_sections_ = false;
  // the sections, which aren't written yet
  vector<Section> sections_;

  unique_ptr<ExportHtmlPage> dialogs_page_;
  DialogsMode dialogs_mode_ = DialogsMode::None;

  ExportDialogInfo dialog_;
  string dialog_first_name_;
  string dialog_last_name_;

  unique_ptr<ExportHtmlPage> page_;
  bool is_page_empty_ = false;
  int32 message_count_ = 0;
  // service messages with dates have negative identifiers
  int32 date_message_id_ = 0;
  unique_ptr<ExportHtmlMessageInfo> last_message_info_;
  // the identifier of the last message on each page except the current one
  vector<int32> last_message_ids_per_page_;

  unique_ptr<ExportHtmlPage> create_page(Slice relative_path) const;

  Status write_file(Slice relative_path, Slice data) TD_WARN_UNUSED_RESULT;

  Status write_page_opening(int32 index);

  Status switch_to_next_page(int32 index);

  void add_section(int32 priority, Slice label, Slice type, int32 count, Slice path);

  Status write_sections();

  Status write_dialog_list_start(bool is_left_channel);

  Status write_dialog_list_entry();
};

}  // namespace td
