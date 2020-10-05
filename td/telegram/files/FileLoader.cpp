//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/files/FileLoader.h"

#include "td/telegram/files/FileLoaderUtils.h"
#include "td/telegram/files/ResourceManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/net/NetQueryDispatcher.h"
#include "td/telegram/UniqueId.h"

#include "td/utils/common.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/ScopeGuard.h"

#include <tuple>

namespace td {

void FileLoader::set_resource_manager(ActorShared<ResourceManager> resource_manager) {
  resource_manager_ = std::move(resource_manager);
  send_closure(resource_manager_, &ResourceManager::update_resources, resource_state_);
}
void FileLoader::update_priority(int8 priority) {
  send_closure(resource_manager_, &ResourceManager::update_priority, priority);
}
void FileLoader::update_resources(const ResourceState &other) {
  resource_state_.update_slave(other);
  VLOG(file_loader) << "Update resources " << resource_state_;
  loop();
}
void FileLoader::set_ordered_flag(bool flag) {
  ordered_flag_ = flag;
}
size_t FileLoader::get_part_size() const {
  return parts_manager_.get_part_size();
}

void FileLoader::hangup() {
  if (delay_dispatcher_.empty()) {
    stop();
  } else {
    delay_dispatcher_.reset();
  }
}

void FileLoader::hangup_shared() {
  if (get_link_token() == 1) {
    stop();
  }
}

void FileLoader::update_local_file_location(const LocalFileLocation &local) {
  auto r_prefix_info = on_update_local_location(local, parts_manager_.get_size_or_zero());
  if (r_prefix_info.is_error()) {
    on_error(r_prefix_info.move_as_error());
    stop_flag_ = true;
    return;
  }
  auto prefix_info = r_prefix_info.move_as_ok();
  auto status = parts_manager_.set_known_prefix(narrow_cast<size_t>(prefix_info.size), prefix_info.is_ready);
  if (status.is_error()) {
    on_error(std::move(status));
    stop_flag_ = true;
    return;
  }
  loop();
}

void FileLoader::update_downloaded_part(int64 offset, int64 limit) {
  if (parts_manager_.get_streaming_offset() != offset) {
    auto begin_part_id = parts_manager_.set_streaming_offset(offset, limit);
    auto new_end_part_id = limit <= 0 ? parts_manager_.get_part_count()
                                      : static_cast<int32>((offset + limit - 1) / parts_manager_.get_part_size()) + 1;
    auto max_parts = static_cast<int32>(ResourceManager::MAX_RESOURCE_LIMIT / parts_manager_.get_part_size());
    auto end_part_id = begin_part_id + td::min(max_parts, new_end_part_id - begin_part_id);
    VLOG(file_loader) << "Protect parts " << begin_part_id << " ... " << end_part_id - 1;
    for (auto &it : part_map_) {
      if (!it.second.second.empty() && !(begin_part_id <= it.second.first.id && it.second.first.id < end_part_id)) {
        VLOG(file_loader) << "Cancel part " << it.second.first.id;
        it.second.second.reset();  // cancel_query(it.second.second);
      }
    }
  } else {
    parts_manager_.set_streaming_limit(limit);
  }
  update_estimated_limit();
  loop();
}

void FileLoader::start_up() {
  auto r_file_info = init();
  if (r_file_info.is_error()) {
    on_error(r_file_info.move_as_error());
    stop_flag_ = true;
    return;
  }
  auto file_info = r_file_info.ok();
  auto size = file_info.size;
  auto expected_size = max(size, file_info.expected_size);
  bool is_size_final = file_info.is_size_final;
  auto part_size = file_info.part_size;
  auto &ready_parts = file_info.ready_parts;
  auto use_part_count_limit = file_info.use_part_count_limit;
  bool is_upload = file_info.is_upload;

  // Two cases when FILE_UPLOAD_RESTART will happen
  // 1. File is ready, size is final. But there are more uploaded parts, than size of a file
  //pm.init(1, 100000, true, 10, {0, 1, 2}, false, true).ensure_error();
  // This error is definitely ok, because we are using actual size of file on disk (mtime is checked by somebody
  // else). And actual size could change arbitrarily.
  //
  // 2. size is unknown/zero, size is not final, some parts of file are already uploaded
  // pm.init(0, 100000, false, 10, {0, 1, 2}, false, true).ensure_error();
  // This case is more complicated
  // It means that at some point we got inconsistent state. Like deleted local location, but left partial remote
  // locaiton untouched. This is completely possible at this point, but probably should be fixed.
  auto status =
      parts_manager_.init(size, expected_size, is_size_final, part_size, ready_parts, use_part_count_limit, is_upload);
  if (status.is_error()) {
    on_error(std::move(status));
    stop_flag_ = true;
    return;
  }
  if (file_info.only_check) {
    parts_manager_.set_checked_prefix_size(0);
  }
  parts_manager_.set_streaming_offset(file_info.offset, file_info.limit);
  if (ordered_flag_) {
    ordered_parts_ = OrderedEventsProcessor<std::pair<Part, NetQueryPtr>>(parts_manager_.get_ready_prefix_count());
  }
  if (file_info.need_delay) {
    delay_dispatcher_ = create_actor<DelayDispatcher>("DelayDispatcher", 0.003, actor_shared(this, 1));
    next_delay_ = 0.05;
  }
  resource_state_.set_unit_size(parts_manager_.get_part_size());
  update_estimated_limit();
  on_progress_impl();
  yield();
}

void FileLoader::loop() {
  if (stop_flag_) {
    return;
  }
  auto status = do_loop();
  if (status.is_error()) {
    if (status.code() == 1) {
      return;
    }
    on_error(std::move(status));
    stop_flag_ = true;
    return;
  }
}

Status FileLoader::do_loop() {
  TRY_RESULT(check_info,
             check_loop(parts_manager_.get_checked_prefix_size(), parts_manager_.get_unchecked_ready_prefix_size(),
                        parts_manager_.unchecked_ready()));
  if (check_info.changed) {
    on_progress_impl();
  }
  for (auto &query : check_info.queries) {
    G()->net_query_dispatcher().dispatch_with_callback(
        std::move(query), actor_shared(this, UniqueId::next(UniqueId::Type::Default, COMMON_QUERY_KEY)));
  }
  if (check_info.need_check) {
    parts_manager_.set_need_check();
    parts_manager_.set_checked_prefix_size(check_info.checked_prefix_size);
  }

  if (parts_manager_.may_finish()) {
    TRY_STATUS(parts_manager_.finish());
    TRY_STATUS(on_ok(parts_manager_.get_size()));
    LOG(INFO) << "Bad download order rate: "
              << (debug_total_parts_ == 0 ? 0.0 : 100.0 * debug_bad_part_order_ / debug_total_parts_) << "% "
              << debug_bad_part_order_ << "/" << debug_total_parts_ << " " << format::as_array(debug_bad_parts_);
    stop_flag_ = true;
    return Status::OK();
  }

  TRY_STATUS(before_start_parts());
  SCOPE_EXIT {
    after_start_parts();
  };
  while (true) {
    if (blocking_id_ != 0) {
      break;
    }
    if (resource_state_.unused() < static_cast<int64>(parts_manager_.get_part_size())) {
      VLOG(file_loader) << "Got only " << resource_state_.unused() << " resource";
      break;
    }
    TRY_RESULT(part, parts_manager_.start_part());
    if (part.size == 0) {
      break;
    }
    VLOG(file_loader) << "Start part " << tag("id", part.id) << tag("size", part.size);
    resource_state_.start_use(static_cast<int64>(part.size));

    TRY_RESULT(query_flag, start_part(part, parts_manager_.get_part_count(), parts_manager_.get_streaming_offset()));
    NetQueryPtr query;
    bool is_blocking;
    std::tie(query, is_blocking) = std::move(query_flag);
    uint64 id = UniqueId::next();
    if (is_blocking) {
      CHECK(blocking_id_ == 0);
      blocking_id_ = id;
    }
    part_map_[id] = std::make_pair(part, query->cancel_slot_.get_signal_new());
    // part_map_[id] = std::make_pair(part, query.get_weak());

    auto callback = actor_shared(this, id);
    if (delay_dispatcher_.empty()) {
      G()->net_query_dispatcher().dispatch_with_callback(std::move(query), std::move(callback));
    } else {
      query->debug("sent to DelayDispatcher");
      send_closure(delay_dispatcher_, &DelayDispatcher::send_with_callback_and_delay, std::move(query),
                   std::move(callback), next_delay_);
      next_delay_ = max(next_delay_ * 0.8, 0.003);
    }
  }
  return Status::OK();
}

void FileLoader::tear_down() {
  for (auto &it : part_map_) {
    it.second.second.reset();  // cancel_query(it.second.second);
  }
  ordered_parts_.clear([](auto &&part) { part.second->clear(); });
  if (!delay_dispatcher_.empty()) {
    send_closure(std::move(delay_dispatcher_), &DelayDispatcher::close_silent);
  }
}

void FileLoader::update_estimated_limit() {
  if (stop_flag_) {
    return;
  }
  auto estimated_extra = parts_manager_.get_estimated_extra();
  resource_state_.update_estimated_limit(estimated_extra);
  VLOG(file_loader) << "Update estimated limit " << estimated_extra;
  if (!resource_manager_.empty()) {
    keep_fd_flag(narrow_cast<uint64>(resource_state_.active_limit()) >= parts_manager_.get_part_size());
    send_closure(resource_manager_, &ResourceManager::update_resources, resource_state_);
  }
}

void FileLoader::on_result(NetQueryPtr query) {
  if (stop_flag_) {
    return;
  }
  auto id = get_link_token();
  if (id == blocking_id_) {
    blocking_id_ = 0;
  }
  if (UniqueId::extract_key(id) == COMMON_QUERY_KEY) {
    on_common_query(std::move(query));
    return loop();
  }
  auto it = part_map_.find(id);
  if (it == part_map_.end()) {
    LOG(WARNING) << "Got result for unknown part";
    return;
  }

  Part part = it->second.first;
  it->second.second.release();
  CHECK(query->is_ready());
  part_map_.erase(it);

  bool next = false;
  auto status = [&] {
    TRY_RESULT(should_restart, should_restart_part(part, query));
    if (query->is_error() && query->error().code() == NetQuery::Error::Cancelled) {
      should_restart = true;
    }
    if (should_restart) {
      VLOG(file_loader) << "Restart part " << tag("id", part.id) << tag("size", part.size);
      resource_state_.stop_use(static_cast<int64>(part.size));
      parts_manager_.on_part_failed(part.id);
    } else {
      next = true;
    }
    return Status::OK();
  }();
  if (status.is_error()) {
    on_error(std::move(status));
    stop_flag_ = true;
    return;
  }

  if (next) {
    if (ordered_flag_) {
      auto seq_no = part.id;
      ordered_parts_.add(seq_no, std::make_pair(part, std::move(query)),
                         [this](auto seq_no, auto &&p) { this->on_part_query(p.first, std::move(p.second)); });
    } else {
      on_part_query(part, std::move(query));
    }
  }
  update_estimated_limit();
  loop();
}

void FileLoader::on_part_query(Part part, NetQueryPtr query) {
  if (stop_flag_) {
    // important for secret files
    return;
  }
  auto status = try_on_part_query(part, std::move(query));
  if (status.is_error()) {
    on_error(std::move(status));
    stop_flag_ = true;
  }
}

void FileLoader::on_common_query(NetQueryPtr query) {
  auto status = process_check_query(std::move(query));
  if (status.is_error()) {
    on_error(std::move(status));
    stop_flag_ = true;
  }
}

Status FileLoader::try_on_part_query(Part part, NetQueryPtr query) {
  TRY_RESULT(size, process_part(part, std::move(query)));
  VLOG(file_loader) << "Ok part " << tag("id", part.id) << tag("size", part.size);
  resource_state_.stop_use(static_cast<int64>(part.size));
  auto old_ready_prefix_count = parts_manager_.get_unchecked_ready_prefix_count();
  TRY_STATUS(parts_manager_.on_part_ok(part.id, part.size, size));
  auto new_ready_prefix_count = parts_manager_.get_unchecked_ready_prefix_count();
  debug_total_parts_++;
  if (old_ready_prefix_count == new_ready_prefix_count) {
    debug_bad_parts_.push_back(part.id);
    debug_bad_part_order_++;
  }
  on_progress_impl();
  return Status::OK();
}

void FileLoader::on_progress_impl() {
  Progress progress;
  progress.part_count = parts_manager_.get_part_count();
  progress.part_size = static_cast<int32>(parts_manager_.get_part_size());
  progress.ready_part_count = parts_manager_.get_ready_prefix_count();
  progress.ready_bitmask = parts_manager_.get_bitmask();
  progress.is_ready = parts_manager_.ready();
  progress.ready_size = parts_manager_.get_ready_size();
  progress.size = parts_manager_.get_size_or_zero();
  on_progress(std::move(progress));
}

}  // namespace td
