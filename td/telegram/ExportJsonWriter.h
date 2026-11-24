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
#include "td/utils/Status.h"

namespace td {

class Td;

class ExportJsonWriter final : public ExportWriter {
 public:
  explicit ExportJsonWriter(Td *td);

  Status start(const ExportSettings &settings, ExportStats *stats) final;

  Status write_dialog_start(const ExportDialogInfo &dialog) final;

  Status write_dialog_messages(const vector<ExportMessage> &messages, bool is_migrated) final;

  Status write_dialog_end() final;

  Status finish() final;

 private:
  Td *td_;
  ExportSettings settings_;
  unique_ptr<ExportOutputFile> output_;
  bool has_messages_ = false;
};

}  // namespace td
