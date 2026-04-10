// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/net/ConnectionLifecycleReport.h"

#include "td/utils/tests.h"

namespace {

using td::ConnectionLifecycleReportBuilder;

TEST(ConnectionLifecycleReportRotation, SerializesRotationMetadataForCompletedConnections) {
  ConnectionLifecycleReportBuilder report;
  ASSERT_TRUE(report.begin_connection(11, "1.1.1.1:443|a.example", 100, false));
  ASSERT_TRUE(report.set_role(11, "main"));
  ASSERT_TRUE(report.set_rotation_reason(11, "age_threshold"));
  ASSERT_TRUE(report.set_successor_opened_at(11, 150));
  ASSERT_TRUE(report.set_overlap_ms(11, 25));
  ASSERT_TRUE(report.set_over_age_status(11, true, "destination_budget"));
  ASSERT_TRUE(report.end_connection(11, 200));

  auto json = report.to_json("non_ru_egress", false);

  ASSERT_EQ(
      "{\"active_policy\":\"non_ru_egress\",\"quic_enabled\":false,\"connections\":[{\"destination\":\"1.1.1.1:443|a."
      "example\",\"started_at_ms\":100,\"ended_at_ms\":200,\"reused\":false,\"bytes_sent\":0,\"bytes_received\":0,"
      "\"role\":\"main\",\"rotation_reason\":\"age_threshold\",\"successor_opened_at_ms\":150,\"overlap_ms\":25,\"over_"
      "age_degraded\":true,\"over_age_exemption\":\"destination_budget\"}]}",
      json);
}

TEST(ConnectionLifecycleReportRotation, RejectsMetadataUpdatesForUnknownConnections) {
  ConnectionLifecycleReportBuilder report;

  ASSERT_FALSE(report.set_role(77, "main"));
  ASSERT_FALSE(report.set_rotation_reason(77, "age_threshold"));
  ASSERT_FALSE(report.set_successor_opened_at(77, 150));
  ASSERT_FALSE(report.set_overlap_ms(77, 25));
  ASSERT_FALSE(report.set_over_age_status(77, true, "destination_budget"));
}

TEST(ConnectionLifecycleReportRotation, RejectsNegativeSuccessorTimestamp) {
  ConnectionLifecycleReportBuilder report;
  ASSERT_TRUE(report.begin_connection(12, "2.2.2.2:443|b.example", 100, false));

  ASSERT_FALSE(report.set_successor_opened_at(12, -1));
}

}  // namespace