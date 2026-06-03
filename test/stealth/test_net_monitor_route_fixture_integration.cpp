// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/telegram/net/NetReliabilityMonitor.h"

#include "test/stealth/SourceContractFileReader.h"

#include "td/utils/tests.h"

#include <cstdlib>

namespace {

td::vector<double> extract_frame_time_epochs(td::Slice path, size_t limit) {
  auto text = td::mtproto::test::read_repo_text_file(path);
  td::vector<double> out;
  td::Slice needle("\"frame_time_epoch\": \"");
  size_t pos = 0;
  while (out.size() < limit) {
    pos = text.find(needle.str(), pos);
    if (pos == td::string::npos) {
      break;
    }
    pos += needle.size();
    auto end = text.find('"', pos);
    CHECK(end != td::string::npos);
    auto value = text.substr(pos, end - pos);
    char *parse_end = nullptr;
    auto parsed = std::strtod(value.c_str(), &parse_end);
    CHECK(parse_end != nullptr && *parse_end == '\0');
    out.push_back(parsed);
    pos = end + 1;
  }
  return out;
}

TEST(NetMonitorRouteFixtureIntegration, RealFixtureBurstStillLeavesDelayedDestroySuspicious) {
  td::net_health::reset_net_monitor_for_tests();

  auto frame_times = extract_frame_time_epochs(
      "test/analysis/fixtures/clienthello/android/vivaldi7_9_3980_88_android16_10ab0dc7.clienthello.json", 3);
  ASSERT_EQ(3u, frame_times.size());

  const double first_gap = frame_times[1] - frame_times[0];
  const double second_gap = frame_times[2] - frame_times[1];
  ASSERT_TRUE(first_gap > 0.0);
  ASSERT_TRUE(second_gap > 0.0);
  ASSERT_TRUE(first_gap < 1.0);
  ASSERT_TRUE(second_gap < 1.0);

  constexpr td::int32 dc_id = 2;
  constexpr double route_change_at = 200000.0;
  constexpr double delayed_destroy_at = route_change_at + 6.0 * 60.0 * 60.0;

  td::net_health::set_lane_probe_now_for_tests(route_change_at);
  td::net_health::note_route_push_nonbaseline_address();
  td::net_health::note_route_address_update(dc_id, route_change_at);
  td::net_health::note_route_address_update(dc_id, route_change_at + first_gap);
  td::net_health::note_route_address_update(dc_id, route_change_at + first_gap + second_gap);

  td::net_health::set_lane_probe_now_for_tests(delayed_destroy_at);
  td::net_health::note_auth_key_destroy(dc_id, td::net_health::AuthKeyDestroyReason::ProgrammaticApiCall,
                                        delayed_destroy_at);
  td::net_health::note_handshake_initiated(dc_id, delayed_destroy_at + second_gap);

  // Resolve the monitor state against the synthetic event timeline before clearing the injected clock.
  auto snapshot = td::net_health::get_net_monitor_snapshot();
  ASSERT_TRUE(snapshot.state == td::net_health::NetMonitorState::Suspicious);
  ASSERT_EQ(1u, snapshot.counters.route_push_nonbaseline_address_total);
  ASSERT_EQ(1u, snapshot.counters.auth_key_destroy_total);
  ASSERT_EQ(0u, snapshot.counters.flow_anchor_reset_sequence_total);
  td::net_health::clear_lane_probe_now_for_tests();
}

}  // namespace