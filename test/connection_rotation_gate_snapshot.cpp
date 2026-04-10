// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/net/ConnectionRotationGateSnapshot.h"

#include "td/utils/tests.h"

namespace {

using td::ConnectionRotationGateSnapshot;
using td::ConnectionRotationGateSnapshotHandle;

TEST(ConnectionRotationGateSnapshot, DefaultsToAllowingRotationAndOverlap) {
  ConnectionRotationGateSnapshotHandle handle;

  auto snapshot = handle.get();

  ASSERT_TRUE(snapshot.anti_churn_allows_rotation);
  ASSERT_TRUE(snapshot.destination_budget_allows_overlap);
}

TEST(ConnectionRotationGateSnapshot, StoresPublishedGateDecisions) {
  ConnectionRotationGateSnapshotHandle handle;
  ConnectionRotationGateSnapshot snapshot;
  snapshot.anti_churn_allows_rotation = false;
  snapshot.destination_budget_allows_overlap = true;

  handle.set(snapshot);

  auto published = handle.get();
  ASSERT_FALSE(published.anti_churn_allows_rotation);
  ASSERT_TRUE(published.destination_budget_allows_overlap);
}

TEST(ConnectionRotationGateSnapshot, NewPublishReplacesPreviousState) {
  ConnectionRotationGateSnapshotHandle handle;
  handle.set(ConnectionRotationGateSnapshot{false, false});

  handle.set(ConnectionRotationGateSnapshot{true, false});

  auto published = handle.get();
  ASSERT_TRUE(published.anti_churn_allows_rotation);
  ASSERT_FALSE(published.destination_budget_allows_overlap);
}

}  // namespace