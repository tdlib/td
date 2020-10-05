//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/files/PartsManager.h"

#include "td/telegram/files/FileLoaderUtils.h"

#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"

#include <limits>
#include <numeric>

namespace td {

namespace {
int64 calc_part_count(int64 size, int64 part_size) {
  CHECK(part_size != 0);
  return (size + part_size - 1) / part_size;
}
}  // namespace

Status PartsManager::init_known_prefix(int64 known_prefix, size_t part_size, const std::vector<int> &ready_parts) {
  known_prefix_flag_ = true;
  known_prefix_size_ = known_prefix;
  return init_no_size(part_size, ready_parts);
}

int32 PartsManager::set_streaming_offset(int64 offset, int64 limit) {
  auto finish = [&] {
    set_streaming_limit(limit);
    update_first_not_ready_part();
    return first_streaming_not_ready_part_;
  };

  if (offset < 0 || need_check_ || (!unknown_size_flag_ && get_size() < offset)) {
    streaming_offset_ = 0;
    LOG_IF(ERROR, offset != 0) << "Ignore streaming_offset " << offset << ", need_check_ = " << need_check_
                               << ", unknown_size_flag_ = " << unknown_size_flag_ << ", size = " << get_size();

    return finish();
  }

  auto part_i = offset / part_size_;
  if (use_part_count_limit_ && part_i >= MAX_PART_COUNT) {
    streaming_offset_ = 0;
    LOG(ERROR) << "Ignore streaming_offset " << offset << " in part " << part_i;

    return finish();
  }

  streaming_offset_ = offset;
  first_streaming_empty_part_ = narrow_cast<int>(part_i);
  first_streaming_not_ready_part_ = narrow_cast<int>(part_i);
  if (part_count_ < first_streaming_empty_part_) {
    part_count_ = first_streaming_empty_part_;
    part_status_.resize(part_count_, PartStatus::Empty);
  }

  return finish();
}

int32 PartsManager::get_pending_count() const {
  return pending_count_;
}

void PartsManager::set_streaming_limit(int64 limit) {
  streaming_limit_ = limit;
  streaming_ready_size_ = 0;
  if (streaming_limit_ == 0) {
    return;
  }
  for (int part_i = 0; part_i < part_count_; part_i++) {
    if (is_part_in_streaming_limit(part_i) && part_status_[part_i] == PartStatus::Ready) {
      streaming_ready_size_ += get_part(part_i).size;
    }
  }
}

Status PartsManager::init_no_size(size_t part_size, const std::vector<int> &ready_parts) {
  unknown_size_flag_ = true;
  size_ = 0;
  min_size_ = 0;
  max_size_ = std::numeric_limits<int64>::max();

  if (part_size != 0) {
    part_size_ = part_size;
  } else {
    part_size_ = 32 * (1 << 10);
    while (use_part_count_limit_ && calc_part_count(expected_size_, part_size_) > MAX_PART_COUNT) {
      part_size_ *= 2;
      CHECK(part_size_ <= MAX_PART_SIZE);
    }
    // just in case if expected_size_ is wrong
    if (part_size_ < MAX_PART_SIZE) {
      part_size_ *= 2;
    }
  }
  part_count_ =
      std::accumulate(ready_parts.begin(), ready_parts.end(), 0, [](auto a, auto b) { return max(a, b + 1); });

  return init_common(ready_parts);
}

Status PartsManager::init(int64 size, int64 expected_size, bool is_size_final, size_t part_size,
                          const std::vector<int> &ready_parts, bool use_part_count_limit, bool is_upload) {
  CHECK(expected_size >= size);
  is_upload_ = is_upload;
  use_part_count_limit_ = use_part_count_limit;
  expected_size_ = expected_size;
  if (expected_size_ > MAX_FILE_SIZE) {
    return Status::Error("Too big file");
  }
  if (!is_size_final) {
    return init_known_prefix(size, part_size, ready_parts);
  }
  if (size == 0) {
    return init_no_size(part_size, ready_parts);
  }
  LOG_CHECK(size > 0) << tag("size", size);
  unknown_size_flag_ = false;
  size_ = size;

  if (part_size != 0) {
    part_size_ = part_size;
    if (use_part_count_limit_ && calc_part_count(expected_size_, part_size_) > MAX_PART_COUNT) {
      return Status::Error("FILE_UPLOAD_RESTART");
    }
  } else {
    part_size_ = 64 * (1 << 10);
    while (use_part_count_limit && calc_part_count(expected_size_, part_size_) > MAX_PART_COUNT) {
      part_size_ *= 2;
      CHECK(part_size_ <= MAX_PART_SIZE);
    }
  }
  LOG_CHECK(1 <= size_) << tag("size_", size_);
  LOG_CHECK(!use_part_count_limit || calc_part_count(expected_size_, part_size_) <= MAX_PART_COUNT)
      << tag("size_", size_) << tag("expected_size", size_) << tag("is_size_final", is_size_final)
      << tag("part_size_", part_size_) << tag("ready_parts", ready_parts.size());
  part_count_ = static_cast<int>(calc_part_count(size_, part_size_));

  return init_common(ready_parts);
}

bool PartsManager::unchecked_ready() {
  VLOG(file_loader) << "Check readiness. Ready size is " << ready_size_ << ", total size is " << size_
                    << ", unknown_size_flag = " << unknown_size_flag_ << ", need_check = " << need_check_
                    << ", checked_prefix_size = " << checked_prefix_size_;
  return !unknown_size_flag_ && ready_size_ == size_;
}

bool PartsManager::may_finish() {
  if (is_streaming_limit_reached()) {
    return true;
  }
  return ready();
}

bool PartsManager::ready() {
  return unchecked_ready() && (!need_check_ || checked_prefix_size_ == size_);
}

Status PartsManager::finish() {
  if (ready()) {
    return Status::OK();
  }
  if (is_streaming_limit_reached()) {
    return Status::Error("FILE_DOWNLOAD_LIMIT");
  }
  return Status::Error("File transferring not finished");
}

void PartsManager::update_first_empty_part() {
  while (first_empty_part_ < part_count_ && part_status_[first_empty_part_] != PartStatus::Empty) {
    first_empty_part_++;
  }

  if (streaming_offset_ == 0) {
    first_streaming_empty_part_ = first_empty_part_;
    return;
  }
  while (first_streaming_empty_part_ < part_count_ && part_status_[first_streaming_empty_part_] != PartStatus::Empty) {
    first_streaming_empty_part_++;
  }
}

void PartsManager::update_first_not_ready_part() {
  while (first_not_ready_part_ < part_count_ && part_status_[first_not_ready_part_] == PartStatus::Ready) {
    first_not_ready_part_++;
  }
  if (streaming_offset_ == 0) {
    first_streaming_not_ready_part_ = first_not_ready_part_;
    return;
  }
  while (first_streaming_not_ready_part_ < part_count_ &&
         part_status_[first_streaming_not_ready_part_] == PartStatus::Ready) {
    first_streaming_not_ready_part_++;
  }
}

int32 PartsManager::get_unchecked_ready_prefix_count() {
  update_first_not_ready_part();
  return first_not_ready_part_;
}

int32 PartsManager::get_ready_prefix_count() {
  auto res = get_unchecked_ready_prefix_count();
  if (need_check_) {
    auto checked_parts = narrow_cast<int32>(checked_prefix_size_ / part_size_);
    if (checked_parts < res) {
      return checked_parts;
    }
  }
  return res;
}

int64 PartsManager::get_streaming_offset() const {
  return streaming_offset_;
}

string PartsManager::get_bitmask() {
  int32 prefix_count = -1;
  if (need_check_) {
    prefix_count = narrow_cast<int32>(checked_prefix_size_ / part_size_);
  }
  return bitmask_.encode(prefix_count);
}

bool PartsManager::is_part_in_streaming_limit(int part_i) const {
  CHECK(part_i < part_count_);
  auto offset_begin = static_cast<int64>(part_i) * static_cast<int64>(get_part_size());
  auto offset_end = offset_begin + static_cast<int64>(get_part(part_i).size);

  if (offset_begin >= get_expected_size()) {
    return false;
  }

  if (streaming_limit_ == 0) {
    return true;
  }

  auto is_intersect_with = [&](int64 begin, int64 end) {
    return max(begin, offset_begin) < min(end, offset_end);
  };

  auto streaming_begin = streaming_offset_;
  auto streaming_end = streaming_offset_ + streaming_limit_;
  if (is_intersect_with(streaming_begin, streaming_end)) {
    return true;
  }
  // wrap limit
  if (!unknown_size_flag_ && streaming_end > get_size() && is_intersect_with(0, streaming_end - get_size())) {
    return true;
  }
  return false;
}

bool PartsManager::is_streaming_limit_reached() {
  if (streaming_limit_ == 0) {
    return false;
  }
  update_first_not_ready_part();
  auto part_i = first_streaming_not_ready_part_;

  // wrap
  if (!unknown_size_flag_ && part_i == part_count_) {
    part_i = first_not_ready_part_;
  }
  if (part_i == part_count_) {
    return false;
  }
  return !is_part_in_streaming_limit(part_i);
}

Result<Part> PartsManager::start_part() {
  update_first_empty_part();
  auto part_i = first_streaming_empty_part_;
  if (known_prefix_flag_ && part_i >= static_cast<int>(known_prefix_size_ / part_size_)) {
    return Status::Error(1, "Wait for prefix to be known");
  }
  if (part_i == part_count_) {
    if (unknown_size_flag_) {
      part_count_++;
      if (part_count_ > MAX_PART_COUNT) {
        if (!is_upload_) {
          // Caller will try to increase part size if it is possible
          return Status::Error("FILE_DOWNLOAD_RESTART_INCREASE_PART_SIZE");
        }
        return Status::Error("Too big file with unknown size");
      }
      part_status_.push_back(PartStatus::Empty);
    } else {
      if (first_empty_part_ < part_count_) {
        part_i = first_empty_part_;
      } else {
        return get_empty_part();
      }
    }
  }

  if (!is_part_in_streaming_limit(part_i)) {
    return get_empty_part();
  }
  CHECK(part_status_[part_i] == PartStatus::Empty);
  on_part_start(part_i);
  return get_part(part_i);
}

Status PartsManager::set_known_prefix(size_t size, bool is_ready) {
  if (!known_prefix_flag_ || size < static_cast<size_t>(known_prefix_size_)) {
    CHECK(is_upload_);
    return Status::Error("FILE_UPLOAD_RESTART");
  }
  known_prefix_size_ = narrow_cast<int64>(size);
  expected_size_ = max(known_prefix_size_, expected_size_);

  CHECK(static_cast<size_t>(part_count_) == part_status_.size());
  if (is_ready) {
    part_count_ = static_cast<int>(calc_part_count(size, part_size_));

    size_ = narrow_cast<int64>(size);
    unknown_size_flag_ = false;
    known_prefix_flag_ = false;
  } else {
    part_count_ = static_cast<int>(size / part_size_);
  }

  LOG_CHECK(static_cast<size_t>(part_count_) >= part_status_.size())
      << size << " " << is_ready << " " << part_count_ << " " << part_size_ << " " << part_status_.size();
  part_status_.resize(part_count_);
  if (use_part_count_limit_ && calc_part_count(expected_size_, part_size_) > MAX_PART_COUNT) {
    CHECK(is_upload_);
    return Status::Error("FILE_UPLOAD_RESTART");
  }
  return Status::OK();
}

Status PartsManager::on_part_ok(int32 id, size_t part_size, size_t actual_size) {
  CHECK(part_status_[id] == PartStatus::Pending);
  pending_count_--;

  part_status_[id] = PartStatus::Ready;
  if (actual_size != 0) {
    bitmask_.set(id);
  }
  ready_size_ += narrow_cast<int64>(actual_size);
  if (streaming_limit_ > 0 && is_part_in_streaming_limit(id)) {
    streaming_ready_size_ += narrow_cast<int64>(actual_size);
  }

  VLOG(file_loader) << "Transferred part " << id << " of size " << part_size << ", total ready size = " << ready_size_;

  int64 offset = narrow_cast<int64>(part_size_) * id;
  int64 end_offset = offset + narrow_cast<int64>(actual_size);
  if (unknown_size_flag_) {
    CHECK(part_size == part_size_);
    if (actual_size < part_size_) {
      max_size_ = min(max_size_, end_offset);
    }
    if (actual_size) {
      min_size_ = max(min_size_, end_offset);
    }
    if (min_size_ > max_size_) {
      auto status = Status::Error(PSLICE() << "Failed to transfer file: " << tag("min_size", min_size_)
                                           << tag("max_size", max_size_));
      LOG(ERROR) << status;
      return status;
    } else if (min_size_ == max_size_) {
      unknown_size_flag_ = false;
      size_ = min_size_;
    }
  } else {
    if ((actual_size < part_size && offset < size_) || (offset >= size_ && actual_size > 0)) {
      auto status = Status::Error(PSLICE() << "Failed to transfer file: " << tag("size", size_) << tag("offset", offset)
                                           << tag("transferred size", actual_size) << tag("part size", part_size));
      LOG(ERROR) << status;
      return status;
    }
  }
  return Status::OK();
}

void PartsManager::on_part_failed(int32 id) {
  CHECK(part_status_[id] == PartStatus::Pending);
  pending_count_--;
  part_status_[id] = PartStatus::Empty;
  if (id < first_empty_part_) {
    first_empty_part_ = id;
  }
  if (streaming_offset_ == 0) {
    first_streaming_empty_part_ = id;
    return;
  }
  auto part_i = narrow_cast<int>(streaming_offset_ / part_size_);
  if (id >= part_i && id < first_streaming_empty_part_) {
    first_streaming_empty_part_ = id;
  }
}

int64 PartsManager::get_size() const {
  CHECK(!unknown_size_flag_);
  return size_;
}

int64 PartsManager::get_size_or_zero() const {
  return size_;
}

int64 PartsManager::get_estimated_extra() const {
  auto total_estimated_extra = get_expected_size() - get_ready_size();
  if (streaming_limit_ != 0) {
    int64 expected_size = get_expected_size();
    int64 streaming_begin = streaming_offset_ / get_part_size() * get_part_size();
    int64 streaming_end =
        (streaming_offset_ + streaming_limit_ + get_part_size() - 1) / get_part_size() * get_part_size();
    int64 streaming_size = streaming_end - streaming_begin;
    if (unknown_size_flag_) {
      if (streaming_begin < expected_size) {
        streaming_size = min(expected_size - streaming_begin, streaming_size);
      } else {
        streaming_size = 0;
      }
    } else {
      if (streaming_end > expected_size) {
        int64 total = streaming_limit_;
        int64 suffix = 0;
        if (streaming_offset_ < expected_size_) {
          suffix = expected_size_ - streaming_begin;
          total -= expected_size_ - streaming_offset_;
        }
        int64 prefix = (total + get_part_size() - 1) / get_part_size() * get_part_size();
        streaming_size = min(expected_size, prefix + suffix);
      }
    }
    int64 res = streaming_size;

    //TODO: delete this block if CHECK won't fail
    int64 sub = 0;
    for (int part_i = 0; part_i < part_count_; part_i++) {
      if (is_part_in_streaming_limit(part_i) && part_status_[part_i] == PartStatus::Ready) {
        sub += get_part(part_i).size;
      }
    }
    CHECK(sub == streaming_ready_size_);

    res -= streaming_ready_size_;
    CHECK(res >= 0);
    return res;
  }
  return total_estimated_extra;
}

int64 PartsManager::get_ready_size() const {
  return ready_size_;
}

int64 PartsManager::get_expected_size() const {
  if (unknown_size_flag_) {
    return max(static_cast<int64>(512 * (1 << 10)), get_ready_size() * 2);
  }
  return get_size();
}

size_t PartsManager::get_part_size() const {
  return part_size_;
}

int32 PartsManager::get_part_count() const {
  return part_count_;
}

Status PartsManager::init_common(const std::vector<int> &ready_parts) {
  ready_size_ = 0;
  streaming_ready_size_ = 0;
  pending_count_ = 0;
  first_empty_part_ = 0;
  first_not_ready_part_ = 0;
  part_status_ = vector<PartStatus>(part_count_);

  for (auto i : ready_parts) {
    if (known_prefix_flag_ && i >= static_cast<int>(known_prefix_size_ / part_size_)) {
      CHECK(is_upload_);
      return Status::Error("FILE_UPLOAD_RESTART");
    }
    if (is_upload_ && i >= part_count_) {
      return Status::Error("FILE_UPLOAD_RESTART");
    }
    LOG_CHECK(0 <= i && i < part_count_) << tag("i", i) << tag("part_count", part_count_) << tag("size", size_)
                                         << tag("part_size", part_size_) << tag("known_prefix_flag", known_prefix_flag_)
                                         << tag("known_prefix_size", known_prefix_size_)
                                         << tag("real part_count",
                                                std::accumulate(ready_parts.begin(), ready_parts.end(), 0,
                                                                [](auto a, auto b) { return max(a, b + 1); }));
    part_status_[i] = PartStatus::Ready;
    bitmask_.set(i);
    auto part = get_part(i);
    ready_size_ += narrow_cast<int64>(part.size);
  }

  checked_prefix_size_ = get_ready_prefix_count() * narrow_cast<int64>(part_size_);

  return Status::OK();
}

void PartsManager::set_need_check() {
  need_check_ = true;
  set_streaming_offset(0, 0);
}

void PartsManager::set_checked_prefix_size(int64 size) {
  checked_prefix_size_ = size;
}

int64 PartsManager::get_checked_prefix_size() const {
  return checked_prefix_size_;
}

int64 PartsManager::get_unchecked_ready_prefix_size() {
  update_first_not_ready_part();
  auto count = first_not_ready_part_;
  if (count == 0) {
    return 0;
  }
  auto part = get_part(count - 1);
  int64 res = part.offset;
  if (!unknown_size_flag_) {
    res += narrow_cast<int64>(part.size);
    res = min(res, get_size());
  }
  return res;
}

Part PartsManager::get_part(int id) const {
  int64 offset = narrow_cast<int64>(part_size_) * id;
  int64 size = narrow_cast<int64>(part_size_);
  auto total_size = unknown_size_flag_ ? max_size_ : get_size();
  if (total_size < offset) {
    size = 0;
  } else {
    size = min(size, total_size - offset);
  }
  return Part{id, offset, static_cast<size_t>(size)};
}

Part PartsManager::get_empty_part() {
  return Part{-1, 0, 0};
}

void PartsManager::on_part_start(int32 id) {
  CHECK(part_status_[id] == PartStatus::Empty);
  part_status_[id] = PartStatus::Pending;
  pending_count_++;
}

}  // namespace td
