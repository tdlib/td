// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/net/ConnectionLifecycleReport.h"

#include "td/utils/tests.h"

#include <limits>

namespace {

using td::ConnectionLifecycleReportBuilder;

TEST(ConnectionLifecycleReport, CompletedConnectionsAreSortedByStartTimeInJsonOutput) {
  ConnectionLifecycleReportBuilder report;
  ASSERT_TRUE(report.begin_connection(2, "2.2.2.2:443|b.example", 200, true));
  ASSERT_TRUE(report.add_write(2, 400));
  ASSERT_TRUE(report.end_connection(2, 500));
  ASSERT_TRUE(report.begin_connection(1, "1.1.1.1:443|a.example", 100, false));
  ASSERT_TRUE(report.add_write(1, 200));
  ASSERT_TRUE(report.add_read(1, 300));
  ASSERT_TRUE(report.end_connection(1, 250));

  auto json = report.to_json("non_ru_egress", false);

  ASSERT_EQ(
      "{\"active_policy\":\"non_ru_egress\",\"quic_enabled\":false,\"connections\":[{\"destination\":\"1.1.1.1:443|a."
      "example\",\"started_at_ms\":100,\"ended_at_ms\":250,\"reused\":false,\"bytes_sent\":200,\"bytes_received\":300,"
      "\"role\":\"unknown\",\"rotation_reason\":\"\",\"successor_opened_at_ms\":0,\"overlap_ms\":0,"
      "\"over_age_degraded\":false,\"over_age_exemption\":\"\"},{\"destination\":\"2.2.2.2:443|b.example\","
      "\"started_at_ms\":200,\"ended_at_ms\":500,\"reused\":true,\"bytes_sent\":400,\"bytes_received\":0,"
      "\"role\":\"unknown\",\"rotation_reason\":\"\",\"successor_opened_at_ms\":0,\"overlap_ms\":0,"
      "\"over_age_degraded\":false,\"over_age_exemption\":\"\"}]}",
      json);
}

TEST(ConnectionLifecycleReport, RejectsDuplicateActiveConnectionIdentifiers) {
  ConnectionLifecycleReportBuilder report;

  ASSERT_TRUE(report.begin_connection(7, "1.1.1.1:443|a.example", 100, false));
  ASSERT_FALSE(report.begin_connection(7, "1.1.1.1:443|a.example", 200, true));
}

TEST(ConnectionLifecycleReport, RejectsUnknownConnectionTrafficAndCloseEvents) {
  ConnectionLifecycleReportBuilder report;

  ASSERT_FALSE(report.add_write(99, 10));
  ASSERT_FALSE(report.add_read(99, 10));
  ASSERT_FALSE(report.end_connection(99, 100));
}

TEST(ConnectionLifecycleReport, RejectsConnectionEndBeforeStart) {
  ConnectionLifecycleReportBuilder report;
  ASSERT_TRUE(report.begin_connection(3, "1.1.1.1:443|a.example", 500, false));

  ASSERT_FALSE(report.end_connection(3, 499));
  ASSERT_EQ(1u, report.active_connection_count());
  ASSERT_EQ(0u, report.completed_connection_count());
}

TEST(ConnectionLifecycleReport, RejectsByteCounterOverflowWithoutMutatingRecord) {
  ConnectionLifecycleReportBuilder report;
  ASSERT_TRUE(report.begin_connection(4, "1.1.1.1:443|a.example", 0, false));
  ASSERT_TRUE(report.add_write(4, static_cast<td::uint64>(std::numeric_limits<td::int64>::max()) - 1));

  ASSERT_FALSE(report.add_write(4, 2));
  ASSERT_TRUE(report.end_connection(4, 1));

  auto completed = report.completed_records();
  ASSERT_EQ(1u, completed.size());
  ASSERT_EQ(static_cast<td::uint64>(std::numeric_limits<td::int64>::max()) - 1, completed[0].bytes_sent);
}

TEST(ConnectionLifecycleReport, CompletedRecordsExcludeStillActiveConnections) {
  ConnectionLifecycleReportBuilder report;
  ASSERT_TRUE(report.begin_connection(5, "1.1.1.1:443|a.example", 0, false));
  ASSERT_TRUE(report.begin_connection(6, "2.2.2.2:443|b.example", 10, true));
  ASSERT_TRUE(report.end_connection(5, 100));

  auto completed = report.completed_records();
  ASSERT_EQ(1u, completed.size());
  ASSERT_EQ("1.1.1.1:443|a.example", completed[0].destination);
  ASSERT_EQ(1u, report.active_connection_count());
}

TEST(ConnectionLifecycleReport, MarkReusedPromotesActiveConnectionBeforeClose) {
  ConnectionLifecycleReportBuilder report;
  ASSERT_TRUE(report.begin_connection(8, "1.1.1.1:443|a.example", 0, false));

  ASSERT_TRUE(report.mark_reused(8));
  ASSERT_TRUE(report.end_connection(8, 100));

  auto completed = report.completed_records();
  ASSERT_EQ(1u, completed.size());
  ASSERT_TRUE(completed[0].reused);
}

TEST(ConnectionLifecycleReport, MarkReusedRejectsUnknownConnection) {
  ConnectionLifecycleReportBuilder report;

  ASSERT_FALSE(report.mark_reused(123));
}

}  // namespace