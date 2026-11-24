//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/ExportSettings.h"
#include "td/telegram/td_api.h"

#include "td/utils/common.h"
#include "td/utils/port/FileFd.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"

namespace td {

class Td;

struct ExportStats {
  int32 file_count = 0;
  int64 total_size = 0;
};

struct ExportDialogInfo {
  enum class Type : int32 {
    Unknown,
    Self,
    Replies,
    VerifyCodes,
    Personal,
    Bot,
    PrivateGroup,
    PrivateSupergroup,
    PublicSupergroup,
    PrivateChannel,
    PublicChannel
  };
  DialogId dialog_id;
  Type type = Type::Unknown;
  string name;
  bool is_left_channel = false;
};

// returns the UTC offset of the date: utc_time_offset, moved by the change of the system time zone's offset between
// now and the date, so that each date gets its own daylight saving time
int32 get_export_utc_time_offset(int64 date, int32 utc_time_offset);

int64 get_export_local_time(int64 date, int32 utc_time_offset);

// returns the date in the "YYYY-MM-DDTHH:MM:SS" format
string get_export_date_time_string(int64 local_time);

// returns the date in the "DD-MM-YYYY_HH-MM-SS" format, used in names of exported files
string get_export_file_date_time_string(int64 local_time);

int64 get_export_file_size(const td_api::file *file);

const td_api::photoSize *get_export_photo_size(const vector<td_api::object_ptr<td_api::photoSize>> &sizes);

struct ExportFile {
  enum class SkipReason : int32 { None, Unavailable, FileType, FileSize };

  // the path relative to the export directory with '/' as the separator; empty if the file is skipped
  string relative_path;
  SkipReason skip_reason = SkipReason::FileType;
};

struct ExportMessage {
  td_api::object_ptr<td_api::message> message;
  // td_api has no content for cleared history, so such a message has messageUnsupported content
  bool is_history_cleared = false;
  // the photo, the document or the contact's vCard
  ExportFile file;
  ExportFile thumbnail;
};

class ExportOutputFile {
 public:
  ExportOutputFile(string path, ExportStats *stats);

  // an empty write creates an empty file
  Status write(Slice data) TD_WARN_UNUSED_RESULT;

 private:
  string path_;
  FileFd fd_;
  ExportStats *stats_;
};

class ExportWriter {
 public:
  ExportWriter() = default;
  ExportWriter(const ExportWriter &) = delete;
  ExportWriter &operator=(const ExportWriter &) = delete;
  ExportWriter(ExportWriter &&) = delete;
  ExportWriter &operator=(ExportWriter &&) = delete;
  virtual ~ExportWriter() = default;

  virtual Status start(const ExportSettings &settings, ExportStats *stats) TD_WARN_UNUSED_RESULT = 0;

  virtual Status write_dialog_start(const ExportDialogInfo &dialog) TD_WARN_UNUSED_RESULT = 0;

  // messages of the basic group the supergroup was migrated from have is_migrated set and are written first
  virtual Status write_dialog_messages(const vector<ExportMessage> &messages,
                                       bool is_migrated) TD_WARN_UNUSED_RESULT = 0;

  virtual Status write_dialog_end() TD_WARN_UNUSED_RESULT = 0;

  virtual Status finish() TD_WARN_UNUSED_RESULT = 0;
};

unique_ptr<ExportWriter> create_export_writer(Td *td, ExportFormat format);

}  // namespace td
