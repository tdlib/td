// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/utils/common.h"
#include "td/utils/JsonBuilder.h"

#include <mutex>
#include <vector>

namespace td {

class ConnectionLifecycleReportBuilder final {
 public:
  struct Record final {
    uint64 connection_id{0};
    string destination;
    int64 started_at_ms{0};
    int64 ended_at_ms{0};
    bool reused{false};
    uint64 bytes_sent{0};
    uint64 bytes_received{0};
    string role{"unknown"};
    string rotation_reason;
    int64 successor_opened_at_ms{0};
    uint64 overlap_ms{0};
    bool over_age_degraded{false};
    string over_age_exemption;
  };

  struct Snapshot final {
    string active_policy;
    bool quic_enabled{false};
    std::vector<Record> connections;
  };

  bool begin_connection(uint64 connection_id, string destination, int64 started_at_ms, bool reused);
  bool add_write(uint64 connection_id, uint64 bytes);
  bool add_read(uint64 connection_id, uint64 bytes);
  bool mark_reused(uint64 connection_id);
  bool end_connection(uint64 connection_id, int64 ended_at_ms);
  bool set_role(uint64 connection_id, Slice role);
  bool set_rotation_reason(uint64 connection_id, Slice reason);
  bool set_successor_opened_at(uint64 connection_id, int64 successor_opened_at_ms);
  bool set_overlap_ms(uint64 connection_id, uint64 overlap_ms);
  bool set_over_age_status(uint64 connection_id, bool over_age_degraded, Slice exemption);

  size_t active_connection_count() const;
  size_t completed_connection_count() const;
  std::vector<Record> completed_records() const;
  Snapshot snapshot(Slice active_policy, bool quic_enabled) const;
  string to_json(Slice active_policy, bool quic_enabled) const;

 private:
  struct ActiveRecord final {
    Record record;
  };

  static bool add_bytes(uint64 &target, uint64 bytes);
  template <class CallbackT>
  bool update_active_record(uint64 connection_id, CallbackT &&callback);
  std::vector<Record> sorted_completed_records() const;

  std::vector<ActiveRecord> active_records_;
  std::vector<Record> completed_records_;
  mutable std::mutex mutex_;
};

void to_json(JsonValueScope &jv, const ConnectionLifecycleReportBuilder::Record &record);
void to_json(JsonValueScope &jv, const ConnectionLifecycleReportBuilder::Snapshot &snapshot);

}  // namespace td