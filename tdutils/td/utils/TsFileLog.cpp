//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/TsFileLog.h"

#include "td/utils/common.h"
#include "td/utils/FileLog.h"
#include "td/utils/logging.h"
#include "td/utils/port/thread_local.h"
#include "td/utils/Slice.h"

#include <array>
#include <limits>

namespace td {

namespace detail {

class TsFileLogImpl : public LogInterface {
 public:
  Status init(string path) {
    path_ = std::move(path);
    for (int32 i = 0; i < static_cast<int32>(logs_.size()); i++) {
      logs_[i].id = i;
    }
    return init_info(&logs_[0]);
  }

  vector<string> get_file_paths() override {
    vector<string> res;
    for (auto &log : logs_) {
      res.push_back(get_path(&log));
    }
    return res;
  }

  void append(CSlice cslice) override {
    return append(cslice, -1);
  }
  void append(CSlice cslice, int log_level) override {
    get_current_logger()->append(cslice, log_level);
  }

 private:
  struct Info {
    FileLog log;
    bool is_inited = false;
    int32 id;
  };
  static constexpr int32 MAX_THREAD_ID = 128;
  std::string path_;
  std::array<Info, MAX_THREAD_ID> logs_;

  LogInterface *get_current_logger() {
    auto *info = get_current_info();
    if (!info->is_inited) {
      init_info(info).ensure();
    }
    return &info->log;
  }

  Info *get_current_info() {
    return &logs_[get_thread_id()];
  }

  Status init_info(Info *info) {
    TRY_STATUS(info->log.init(get_path(info), std::numeric_limits<int64>::max(), info->id == 0));
    info->is_inited = true;
    return Status::OK();
  }

  string get_path(const Info *info) {
    if (info->id == 0) {
      return path_;
    }
    return PSTRING() << path_ << "." << info->id;
  }
};

}  // namespace detail

Result<unique_ptr<LogInterface>> TsFileLog::create(string path) {
  auto res = make_unique<detail::TsFileLogImpl>();
  TRY_STATUS(res->init(path));
  return std::move(res);
}

}  // namespace td
