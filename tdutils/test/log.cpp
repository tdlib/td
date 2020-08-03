//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/benchmark.h"
#include "td/utils/FileLog.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/MemoryLog.h"
#include "td/utils/port/path.h"
#include "td/utils/port/thread.h"
#include "td/utils/Slice.h"
#include "td/utils/tests.h"
#include "td/utils/TsFileLog.h"

#include <functional>
#include <limits>

char disable_linker_warning_about_empty_file_tdutils_test_log_cpp TD_UNUSED;

#if !TD_THREAD_UNSUPPORTED
template <class Log>
class LogBenchmark : public td::Benchmark {
 public:
  LogBenchmark(std::string name, int threads_n, bool test_full_logging, std::function<td::unique_ptr<Log>()> creator)
      : name_(std::move(name))
      , threads_n_(threads_n)
      , test_full_logging_(test_full_logging)
      , creator_(std::move(creator)) {
  }
  std::string get_description() const override {
    return PSTRING() << name_ << " " << (test_full_logging_ ? "ERROR" : "PLAIN") << " "
                     << td::tag("threads_n", threads_n_);
  }
  void start_up() override {
    log_ = creator_();
    threads_.resize(threads_n_);
  }
  void tear_down() override {
    for (auto path : log_->get_file_paths()) {
      td::unlink(path).ignore();
    }
    log_.reset();
  }
  void run(int n) override {
    auto old_log_interface = td::log_interface;
    td::log_interface = log_.get();

    for (auto &thread : threads_) {
      thread = td::thread([this, n] { this->run_thread(n); });
    }
    for (auto &thread : threads_) {
      thread.join();
    }

    td::log_interface = old_log_interface;
  }

  void run_thread(int n) {
    auto str = PSTRING() << "#" << n << " : fsjklfdjsklfjdsklfjdksl\n";
    for (int i = 0; i < n; i++) {
      if (i % 10000 == 0) {
        log_->rotate();
      }
      if (test_full_logging_) {
        LOG(ERROR) << str;
      } else {
        LOG(PLAIN) << str;
      }
    }
  }

 private:
  std::string name_;
  td::unique_ptr<td::LogInterface> log_;
  int threads_n_{0};
  bool test_full_logging_{false};
  std::function<td::unique_ptr<Log>()> creator_;
  std::vector<td::thread> threads_;
};

template <class F>
static void bench_log(std::string name, F &&f) {
  for (auto test_full_logging : {false, true}) {
    for (auto threads_n : {1, 4, 8}) {
      bench(LogBenchmark<typename decltype(f())::element_type>(name, threads_n, test_full_logging, f));
    }
  }
};

TEST(Log, Bench) {
  bench_log("NullLog", [] { return td::make_unique<td::NullLog>(); });

  bench_log("MemoryLog", [] { return td::make_unique<td::MemoryLog<1 << 20>>(); });

  bench_log("TsFileLog",
            [] { return td::TsFileLog::create("tmplog", std::numeric_limits<td::int64>::max(), false).move_as_ok(); });

  bench_log("FileLog + TsLog", [] {
    class FileLog : public td::LogInterface {
     public:
      FileLog() {
        file_log_.init("tmplog", std::numeric_limits<td::int64>::max(), false).ensure();
        ts_log_.init(&file_log_);
      }
      ~FileLog() {
      }
      void append(td::CSlice slice, int log_level) override {
        ts_log_.append(slice, log_level);
      }
      std::vector<std::string> get_file_paths() override {
        return file_log_.get_file_paths();
      }

     private:
      td::FileLog file_log_;
      td::TsLog ts_log_{nullptr};
    };
    return td::make_unique<FileLog>();
  });

  bench_log("FileLog", [] {
    class FileLog : public td::LogInterface {
     public:
      FileLog() {
        file_log_.init("tmplog", std::numeric_limits<td::int64>::max(), false).ensure();
      }
      ~FileLog() {
      }
      void append(td::CSlice slice, int log_level) override {
        file_log_.append(slice, log_level);
      }
      std::vector<std::string> get_file_paths() override {
        return file_log_.get_file_paths();
      }

     private:
      td::FileLog file_log_;
    };
    return td::make_unique<FileLog>();
  });
}
#endif
