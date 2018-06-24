//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/files/PartsManager.h"

#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"

#include <limits>
#include <numeric>

namespace td {
/*** PartsManager ***/

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
  part_count_ = 0;
  if (known_prefix_flag_) {
    part_count_ = static_cast<int>(known_prefix_size_ / part_size_);
  }
  part_count_ = max(part_count_, std::accumulate(ready_parts.begin(), ready_parts.end(), 0,
                                                 [](auto a, auto b) { return max(a, b + 1); }));

  init_common(ready_parts);
  return Status::OK();
}

Status PartsManager::init(int64 size, int64 expected_size, bool is_size_final, size_t part_size,
                          const std::vector<int> &ready_parts, bool use_part_count_limit) {
  CHECK(expected_size >= size);
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
  CHECK(size > 0) << tag("size", size);
  unknown_size_flag_ = false;
  size_ = size;

  if (part_size != 0) {
    part_size_ = part_size;
    if (use_part_count_limit_ && calc_part_count(expected_size_, part_size_) > MAX_PART_COUNT) {
      return Status::Error("FILE_UPLOAD_RESTART");
    }
  } else {
    // TODO choose part_size_ depending on size
    part_size_ = 64 * (1 << 10);
    while (use_part_count_limit && calc_part_count(expected_size_, part_size_) > MAX_PART_COUNT) {
      part_size_ *= 2;
      CHECK(part_size_ <= MAX_PART_SIZE);
    }
  }
  CHECK(1 <= size_) << tag("size_", size_);
  CHECK(!use_part_count_limit || calc_part_count(expected_size_, part_size_) <= MAX_PART_COUNT)
      << tag("size_", size_) << tag("expected_size", size_) << tag("is_size_final", is_size_final)
      << tag("part_size_", part_size_) << tag("ready_parts", ready_parts.size());
  part_count_ = static_cast<int>(calc_part_count(size_, part_size_));

  init_common(ready_parts);
  return Status::OK();
}

bool PartsManager::unchecked_ready() {
  VLOG(files) << "Check readiness. Ready size is " << ready_size_ << ", total size is " << size_
              << ", unknown_size_flag = " << unknown_size_flag_ << ", need_check = " << need_check_
              << ", checked_prefix_size = " << checked_prefix_size_;
  return !unknown_size_flag_ && ready_size_ == size_;
}
bool PartsManager::ready() {
  return unchecked_ready() && (!need_check_ || checked_prefix_size_ == size_);
}

Status PartsManager::finish() {
  if (!ready()) {
    return Status::Error("File transferring not finished");
  }
  return Status::OK();
}

void PartsManager::update_first_empty_part() {
  while (first_empty_part_ < part_count_ && part_status_[first_empty_part_] != PartStatus::Empty) {
    first_empty_part_++;
  }
}

void PartsManager::update_first_not_ready_part() {
  while (first_not_ready_part_ < part_count_ && part_status_[first_not_ready_part_] == PartStatus::Ready) {
    first_not_ready_part_++;
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

Result<Part> PartsManager::start_part() {
  update_first_empty_part();
  if (first_empty_part_ == part_count_) {
    if (unknown_size_flag_) {
      if (known_prefix_flag_ == false) {
        part_count_++;
        if (part_count_ > MAX_PART_COUNT) {
          return Status::Error("Too big file with unknown size");
        }
        part_status_.push_back(PartStatus::Empty);
      } else {
        return Status::Error(1, "Wait for prefix to be known");
      }
    } else {
      return get_empty_part();
    }
  }
  CHECK(part_status_[first_empty_part_] == PartStatus::Empty);
  int id = first_empty_part_;
  on_part_start(id);
  return get_part(id);
}

Status PartsManager::set_known_prefix(size_t size, bool is_ready) {
  CHECK(known_prefix_flag_) << unknown_size_flag_ << " " << size << " " << is_ready << " " << known_prefix_size_ << " "
                            << expected_size_ << " " << part_count_ << " " << part_status_.size();
  CHECK(size >= static_cast<size_t>(known_prefix_size_))
      << unknown_size_flag_ << " " << size << " " << is_ready << " " << known_prefix_size_ << " " << expected_size_
      << " " << part_count_ << " " << part_status_.size();
  known_prefix_size_ = narrow_cast<int64>(size);
  expected_size_ = max(known_prefix_size_, expected_size_);

  CHECK(static_cast<size_t>(part_count_) == part_status_.size());
  if (is_ready) {
    part_count_ = static_cast<int>(calc_part_count(size, part_size_));

    size_ = narrow_cast<int64>(size);
    unknown_size_flag_ = false;
  } else {
    part_count_ = static_cast<int>(size / part_size_);
  }
  CHECK(static_cast<size_t>(part_count_) >= part_status_.size())
      << size << " " << is_ready << " " << part_count_ << " " << part_size_ << " " << part_status_.size();
  part_status_.resize(part_count_);
  if (use_part_count_limit_ && calc_part_count(expected_size_, part_size_) > MAX_PART_COUNT) {
    return Status::Error("FILE_UPLOAD_RESTART");
  }
  return Status::OK();
}

Status PartsManager::on_part_ok(int32 id, size_t part_size, size_t actual_size) {
  CHECK(part_status_[id] == PartStatus::Pending);
  pending_count_--;

  part_status_[id] = PartStatus::Ready;
  ready_size_ += narrow_cast<int64>(actual_size);

  VLOG(files) << "Transferred part " << id << " of size " << part_size << ", total ready size = " << ready_size_;

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
}

int64 PartsManager::get_size() const {
  CHECK(!unknown_size_flag_);
  return size_;
}
int64 PartsManager::get_size_or_zero() const {
  return size_;
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

void PartsManager::init_common(const std::vector<int> &ready_parts) {
  ready_size_ = 0;
  pending_count_ = 0;
  first_empty_part_ = 0;
  first_not_ready_part_ = 0;
  part_status_ = vector<PartStatus>(part_count_);

  for (auto i : ready_parts) {
    CHECK(0 <= i && i < part_count_) << tag("i", i) << tag("part_count", part_count_);
    part_status_[i] = PartStatus::Ready;
    auto part = get_part(i);
    ready_size_ += narrow_cast<int64>(part.size);
  }

  checked_prefix_size_ = get_ready_prefix_count() * narrow_cast<int64>(part_size_);
}

void PartsManager::set_need_check() {
  need_check_ = true;
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

Part PartsManager::get_part(int id) {
  int64 offset = narrow_cast<int64>(part_size_) * id;
  int64 size = narrow_cast<int64>(part_size_);
  if (!unknown_size_flag_) {
    auto total_size = get_size();
    if (total_size < offset) {
      size = 0;
    } else {
      size = min(size, total_size - offset);
    }
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
