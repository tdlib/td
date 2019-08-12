//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "TsFileLog.h"

namespace td {
namespace detail {
class TsFileLog : public LogInterface {
 public:
  Status init(string path) {
    path_ = std::move(path);
    for (int i = 0; i < (int)logs_.size(); i++) {
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
    bool is_inited;
    int id;
  };
  static constexpr int MAX_THREAD_ID = 128;
  std::string path_;
  std::array<Info, MAX_THREAD_ID> logs_;

  LogInterface *get_current_logger() {
    auto *info = get_current_info();
    if (!info->is_inited) {
      CHECK(init_info(info).is_ok());
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

  string get_path(Info *info) {
    if (info->id == 0) {
      return path_;
    }
    return PSTRING() << path_ << "." << info->id;
  }
};
}  // namespace detail

Result<td::unique_ptr<LogInterface>> TsFileLog::create(string path) {
  auto res = td::make_unique<detail::TsFileLog>();
  TRY_STATUS(res->init(path));
  return std::move(res);
}
}  // namespace td
