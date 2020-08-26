//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/actor/actor.h"

#include "td/telegram/DelayDispatcher.h"
#include "td/telegram/files/FileLoaderActor.h"
#include "td/telegram/files/FileLocation.h"
#include "td/telegram/files/PartsManager.h"
#include "td/telegram/files/ResourceManager.h"
#include "td/telegram/files/ResourceState.h"
#include "td/telegram/net/NetQuery.h"

#include "td/utils/OrderedEventsProcessor.h"
#include "td/utils/Status.h"

#include <map>
#include <utility>

namespace td {

class FileLoader : public FileLoaderActor {
 public:
  class Callback {
   public:
    Callback() = default;
    Callback(const Callback &) = delete;
    Callback &operator=(const Callback &) = delete;
    virtual ~Callback() = default;
  };
  void set_resource_manager(ActorShared<ResourceManager> resource_manager) override;
  void update_priority(int8 priority) override;
  void update_resources(const ResourceState &other) override;

  void update_local_file_location(const LocalFileLocation &local) override;
  void update_downloaded_part(int64 offset, int64 limit) override;

 protected:
  void set_ordered_flag(bool flag);
  size_t get_part_size() const;

  struct PrefixInfo {
    int64 size = -1;
    bool is_ready = false;
  };
  struct FileInfo {
    int64 size;
    int64 expected_size{0};
    bool is_size_final;
    int32 part_size;
    std::vector<int> ready_parts;
    bool use_part_count_limit = true;
    bool only_check = false;
    bool need_delay = false;
    int64 offset{0};
    int64 limit{0};
    bool is_upload{false};
  };
  virtual Result<FileInfo> init() TD_WARN_UNUSED_RESULT = 0;
  virtual Status on_ok(int64 size) TD_WARN_UNUSED_RESULT = 0;
  virtual void on_error(Status status) = 0;
  virtual Status before_start_parts() {
    return Status::OK();
  }
  virtual Result<std::pair<NetQueryPtr, bool>> start_part(Part part, int part_count,
                                                          int64 streaming_offset) TD_WARN_UNUSED_RESULT = 0;
  virtual void after_start_parts() {
  }
  virtual Result<size_t> process_part(Part part, NetQueryPtr net_query) TD_WARN_UNUSED_RESULT = 0;
  struct Progress {
    int32 part_count{0};
    int32 part_size{0};
    int32 ready_part_count{0};
    string ready_bitmask;
    bool is_ready{false};
    int64 ready_size{0};
    int64 size{0};
  };
  virtual void on_progress(Progress progress) = 0;
  virtual Callback *get_callback() = 0;
  virtual Result<PrefixInfo> on_update_local_location(const LocalFileLocation &location,
                                                      int64 file_size) TD_WARN_UNUSED_RESULT {
    return Status::Error("Unsupported");
  }
  virtual Result<bool> should_restart_part(Part part, NetQueryPtr &net_query) TD_WARN_UNUSED_RESULT {
    return false;
  }

  virtual Status process_check_query(NetQueryPtr net_query) {
    return Status::Error("Unsupported");
  }
  struct CheckInfo {
    bool need_check{false};
    bool changed{false};
    int64 checked_prefix_size{0};
    std::vector<NetQueryPtr> queries;
  };
  virtual Result<CheckInfo> check_loop(int64 checked_prefix_size, int64 ready_prefix_size, bool is_ready) {
    return CheckInfo{};
  }

  virtual void keep_fd_flag(bool keep_fd) {
  }

 private:
  static constexpr uint8 COMMON_QUERY_KEY = 2;
  bool stop_flag_ = false;
  ActorShared<ResourceManager> resource_manager_;
  ResourceState resource_state_;
  PartsManager parts_manager_;
  uint64 blocking_id_{0};
  std::map<uint64, std::pair<Part, ActorShared<>>> part_map_;
  bool ordered_flag_ = false;
  OrderedEventsProcessor<std::pair<Part, NetQueryPtr>> ordered_parts_;
  ActorOwn<DelayDispatcher> delay_dispatcher_;
  double next_delay_ = 0;

  uint32 debug_total_parts_ = 0;
  uint32 debug_bad_part_order_ = 0;
  std::vector<int32> debug_bad_parts_;

  void start_up() override;
  void loop() override;
  Status do_loop();
  void hangup() override;
  void hangup_shared() override;
  void tear_down() override;

  void update_estimated_limit();
  void on_progress_impl();

  void on_result(NetQueryPtr query) override;
  void on_part_query(Part part, NetQueryPtr query);
  void on_common_query(NetQueryPtr query);
  Status try_on_part_query(Part part, NetQueryPtr query);
};

}  // namespace td
