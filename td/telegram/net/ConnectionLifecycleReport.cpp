// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/net/ConnectionLifecycleReport.h"

#include <algorithm>
#include <limits>

namespace td {
namespace {

template <class RecordT>
bool compare_records(const RecordT &left, const RecordT &right) {
  if (left.started_at_ms != right.started_at_ms) {
    return left.started_at_ms < right.started_at_ms;
  }
  if (left.destination != right.destination) {
    return left.destination < right.destination;
  }
  return left.connection_id < right.connection_id;
}

string encode_records_array(const std::vector<ConnectionLifecycleReportBuilder::Record> &records) {
  string result = "[";
  bool need_comma = false;
  for (const auto &record : records) {
    if (need_comma) {
      result += ",";
    }
    need_comma = true;
    result += json_encode<string>(ToJson(record));
  }
  result += "]";
  return result;
}

}  // namespace

bool ConnectionLifecycleReportBuilder::begin_connection(uint64 connection_id, string destination, int64 started_at_ms,
                                                        bool reused) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (connection_id == 0 || destination.empty() || started_at_ms < 0) {
    return false;
  }
  for (const auto &active_record : active_records_) {
    if (active_record.record.connection_id == connection_id) {
      return false;
    }
  }

  ActiveRecord active_record;
  active_record.record.connection_id = connection_id;
  active_record.record.destination = std::move(destination);
  active_record.record.started_at_ms = started_at_ms;
  active_record.record.reused = reused;
  active_records_.push_back(std::move(active_record));
  return true;
}

bool ConnectionLifecycleReportBuilder::add_write(uint64 connection_id, uint64 bytes) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto &active_record : active_records_) {
    if (active_record.record.connection_id == connection_id) {
      return add_bytes(active_record.record.bytes_sent, bytes);
    }
  }
  return false;
}

bool ConnectionLifecycleReportBuilder::add_read(uint64 connection_id, uint64 bytes) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto &active_record : active_records_) {
    if (active_record.record.connection_id == connection_id) {
      return add_bytes(active_record.record.bytes_received, bytes);
    }
  }
  return false;
}

bool ConnectionLifecycleReportBuilder::mark_reused(uint64 connection_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto &active_record : active_records_) {
    if (active_record.record.connection_id == connection_id) {
      active_record.record.reused = true;
      return true;
    }
  }
  return false;
}

bool ConnectionLifecycleReportBuilder::end_connection(uint64 connection_id, int64 ended_at_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (size_t i = 0; i < active_records_.size(); i++) {
    auto &record = active_records_[i].record;
    if (record.connection_id != connection_id) {
      continue;
    }
    if (ended_at_ms < record.started_at_ms) {
      return false;
    }
    record.ended_at_ms = ended_at_ms;
    completed_records_.push_back(record);
    active_records_.erase(active_records_.begin() + static_cast<td::int64>(i));
    return true;
  }
  return false;
}

bool ConnectionLifecycleReportBuilder::set_role(uint64 connection_id, Slice role) {
  if (role.empty()) {
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  return update_active_record(connection_id, [role](Record &record) { record.role = role.str(); });
}

bool ConnectionLifecycleReportBuilder::set_rotation_reason(uint64 connection_id, Slice reason) {
  if (reason.empty()) {
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  return update_active_record(connection_id, [reason](Record &record) { record.rotation_reason = reason.str(); });
}

bool ConnectionLifecycleReportBuilder::set_successor_opened_at(uint64 connection_id, int64 successor_opened_at_ms) {
  if (successor_opened_at_ms < 0) {
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  return update_active_record(connection_id, [successor_opened_at_ms](Record &record) {
    record.successor_opened_at_ms = successor_opened_at_ms;
  });
}

bool ConnectionLifecycleReportBuilder::set_overlap_ms(uint64 connection_id, uint64 overlap_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  return update_active_record(connection_id, [overlap_ms](Record &record) { record.overlap_ms = overlap_ms; });
}

bool ConnectionLifecycleReportBuilder::set_over_age_status(uint64 connection_id, bool over_age_degraded,
                                                           Slice exemption) {
  std::lock_guard<std::mutex> lock(mutex_);
  return update_active_record(connection_id, [over_age_degraded, exemption](Record &record) {
    record.over_age_degraded = over_age_degraded;
    record.over_age_exemption = exemption.str();
  });
}

size_t ConnectionLifecycleReportBuilder::active_connection_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return active_records_.size();
}

size_t ConnectionLifecycleReportBuilder::completed_connection_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return completed_records_.size();
}

std::vector<ConnectionLifecycleReportBuilder::Record> ConnectionLifecycleReportBuilder::completed_records() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return sorted_completed_records();
}

ConnectionLifecycleReportBuilder::Snapshot ConnectionLifecycleReportBuilder::snapshot(Slice active_policy,
                                                                                      bool quic_enabled) const {
  std::lock_guard<std::mutex> lock(mutex_);
  Snapshot result;
  result.active_policy = active_policy.str();
  result.quic_enabled = quic_enabled;
  result.connections = sorted_completed_records();
  return result;
}

string ConnectionLifecycleReportBuilder::to_json(Slice active_policy, bool quic_enabled) const {
  return json_encode<string>(ToJson(snapshot(active_policy, quic_enabled)));
}

bool ConnectionLifecycleReportBuilder::add_bytes(uint64 &target, uint64 bytes) {
  static constexpr uint64 kMaxSerializableCounter = static_cast<uint64>(std::numeric_limits<int64>::max());
  if (target > kMaxSerializableCounter || bytes > kMaxSerializableCounter - target) {
    return false;
  }
  target += bytes;
  return true;
}

template <class CallbackT>
bool ConnectionLifecycleReportBuilder::update_active_record(uint64 connection_id, CallbackT &&callback) {
  for (auto &active_record : active_records_) {
    if (active_record.record.connection_id == connection_id) {
      callback(active_record.record);
      return true;
    }
  }
  return false;
}

std::vector<ConnectionLifecycleReportBuilder::Record> ConnectionLifecycleReportBuilder::sorted_completed_records()
    const {
  auto result = completed_records_;
  std::sort(result.begin(), result.end(), compare_records<Record>);
  return result;
}

void to_json(JsonValueScope &jv, const ConnectionLifecycleReportBuilder::Record &record) {
  auto jo = jv.enter_object();
  jo("destination", record.destination);
  jo("started_at_ms", record.started_at_ms);
  jo("ended_at_ms", record.ended_at_ms);
  jo("reused", JsonBool(record.reused));
  jo("bytes_sent", static_cast<int64>(record.bytes_sent));
  jo("bytes_received", static_cast<int64>(record.bytes_received));
  jo("role", record.role);
  jo("rotation_reason", record.rotation_reason);
  jo("successor_opened_at_ms", record.successor_opened_at_ms);
  jo("overlap_ms", static_cast<int64>(record.overlap_ms));
  jo("over_age_degraded", JsonBool(record.over_age_degraded));
  jo("over_age_exemption", record.over_age_exemption);
}

void to_json(JsonValueScope &jv, const ConnectionLifecycleReportBuilder::Snapshot &snapshot) {
  auto jo = jv.enter_object();
  jo("active_policy", snapshot.active_policy);
  jo("quic_enabled", JsonBool(snapshot.quic_enabled));
  jo("connections", JsonRaw(encode_records_array(snapshot.connections)));
}

}  // namespace td