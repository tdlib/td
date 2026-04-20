// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/net/ActiveConnectionLifecycleStateMachine.h"

#include "td/utils/tests.h"

namespace {

using td::ActiveConnectionLifecycleInput;
using td::ActiveConnectionLifecyclePolicy;
using td::ActiveConnectionLifecycleRole;
using td::ActiveConnectionLifecycleState;
using td::ActiveConnectionLifecycleStateMachine;
using td::ActiveConnectionRotationExemptionReason;

ActiveConnectionLifecyclePolicy default_policy() {
  ActiveConnectionLifecyclePolicy policy;
  policy.enable_active_rotation = true;
  policy.hard_ceiling_ms = 5000;
  policy.overlap_max_ms = 400;
  policy.rotation_backoff_ms = 100;
  policy.max_overlap_connections_per_destination = 1;
  return policy;
}

void assert_state_eq(ActiveConnectionLifecycleState expected, ActiveConnectionLifecycleState actual) {
  ASSERT_TRUE(static_cast<int>(expected) == static_cast<int>(actual));
}

void assert_reason_eq(ActiveConnectionRotationExemptionReason expected,
                      ActiveConnectionRotationExemptionReason actual) {
  ASSERT_TRUE(static_cast<int>(expected) == static_cast<int>(actual));
}

TEST(ActiveConnectionLifecycleStateMachine, EligibleConnectionEntersRotationPendingAtSampledDeadline) {
  auto policy = default_policy();
  ActiveConnectionLifecycleStateMachine machine(ActiveConnectionLifecycleRole::Main, 1000, 2000);

  machine.mark_eligible();
  auto decision = machine.poll(policy, ActiveConnectionLifecycleInput{2000, false, false, false, false, true, true});

  ASSERT_TRUE(decision.prepare_successor);
  ASSERT_FALSE(decision.route_new_queries_to_successor);
  ASSERT_FALSE(decision.retire_current);
  ASSERT_FALSE(decision.over_age_degraded);
  assert_state_eq(ActiveConnectionLifecycleState::RotationPending, machine.state());
  ASSERT_EQ(1u, machine.rotation_attempts());
  ASSERT_TRUE(machine.has_successor());
  assert_reason_eq(ActiveConnectionRotationExemptionReason::None, machine.rotation_exemption_reason());
}

TEST(ActiveConnectionLifecycleStateMachine, RotationPendingTransitionsToDrainingWhenSuccessorReady) {
  auto policy = default_policy();
  ActiveConnectionLifecycleStateMachine machine(ActiveConnectionLifecycleRole::Main, 1000, 2000);

  machine.mark_eligible();
  ASSERT_TRUE(machine.poll(policy, ActiveConnectionLifecycleInput{2000, false, false, false, false, true, true})
                  .prepare_successor);

  machine.mark_successor_ready(2100);
  auto decision = machine.poll(policy, ActiveConnectionLifecycleInput{2100, true, false, false, false, true, true});

  ASSERT_FALSE(decision.prepare_successor);
  ASSERT_TRUE(decision.route_new_queries_to_successor);
  ASSERT_FALSE(decision.retire_current);
  assert_state_eq(ActiveConnectionLifecycleState::Draining, machine.state());
  ASSERT_EQ(2100u, machine.draining_started_at_ms());
}

TEST(ActiveConnectionLifecycleStateMachine, DrainingTransitionsToRetiredAfterOutstandingQueriesFinish) {
  auto policy = default_policy();
  ActiveConnectionLifecycleStateMachine machine(ActiveConnectionLifecycleRole::Main, 1000, 2000);

  machine.mark_eligible();
  ASSERT_TRUE(machine.poll(policy, ActiveConnectionLifecycleInput{2000, false, false, false, false, true, true})
                  .prepare_successor);
  machine.mark_successor_ready(2100);
  ASSERT_TRUE(machine.poll(policy, ActiveConnectionLifecycleInput{2100, true, false, false, false, true, true})
                  .route_new_queries_to_successor);

  auto decision = machine.poll(policy, ActiveConnectionLifecycleInput{2200, false, false, false, false, true, true});

  ASSERT_FALSE(decision.prepare_successor);
  ASSERT_FALSE(decision.route_new_queries_to_successor);
  ASSERT_TRUE(decision.retire_current);
  assert_state_eq(ActiveConnectionLifecycleState::Retired, machine.state());
}

TEST(ActiveConnectionLifecycleStateMachine, DestinationBudgetGateDefersOverlapWhenCapWouldBeExceeded) {
  auto policy = default_policy();
  ActiveConnectionLifecycleStateMachine machine(ActiveConnectionLifecycleRole::LongPoll, 1000, 2000);

  machine.mark_eligible();
  auto decision = machine.poll(policy, ActiveConnectionLifecycleInput{2000, false, false, false, false, false, true});

  ASSERT_FALSE(decision.prepare_successor);
  ASSERT_FALSE(decision.route_new_queries_to_successor);
  ASSERT_FALSE(decision.retire_current);
  ASSERT_FALSE(decision.over_age_degraded);
  assert_state_eq(ActiveConnectionLifecycleState::RotationPending, machine.state());
  ASSERT_FALSE(machine.has_successor());
  assert_reason_eq(ActiveConnectionRotationExemptionReason::DestinationBudget, machine.rotation_exemption_reason());
}

TEST(ActiveConnectionLifecycleStateMachine, AntiChurnGateDefersRotationUntilWindowReopens) {
  auto policy = default_policy();
  ActiveConnectionLifecycleStateMachine machine(ActiveConnectionLifecycleRole::Main, 1000, 2000);

  machine.mark_eligible();
  auto blocked = machine.poll(policy, ActiveConnectionLifecycleInput{2000, false, false, false, false, true, false});

  ASSERT_FALSE(blocked.prepare_successor);
  ASSERT_FALSE(blocked.route_new_queries_to_successor);
  ASSERT_FALSE(blocked.retire_current);
  ASSERT_FALSE(blocked.over_age_degraded);
  assert_state_eq(ActiveConnectionLifecycleState::RotationPending, machine.state());
  ASSERT_FALSE(machine.has_successor());
  assert_reason_eq(ActiveConnectionRotationExemptionReason::AntiChurn, machine.rotation_exemption_reason());

  auto resumed = machine.poll(policy, ActiveConnectionLifecycleInput{2101, false, false, false, false, true, true});
  ASSERT_TRUE(resumed.prepare_successor);
  ASSERT_TRUE(machine.has_successor());
  assert_reason_eq(ActiveConnectionRotationExemptionReason::None, machine.rotation_exemption_reason());
}

TEST(ActiveConnectionLifecycleStateMachine, AuthHandshakeGateDefersRotationUntilHandshakeCompletes) {
  auto policy = default_policy();
  ActiveConnectionLifecycleStateMachine machine(ActiveConnectionLifecycleRole::Main, 1000, 2000);

  machine.mark_eligible();
  auto blocked = machine.poll(policy, ActiveConnectionLifecycleInput{2000, false, true, false, false, true, true});

  ASSERT_FALSE(blocked.prepare_successor);
  ASSERT_FALSE(blocked.route_new_queries_to_successor);
  ASSERT_FALSE(blocked.retire_current);
  ASSERT_FALSE(blocked.over_age_degraded);
  assert_state_eq(ActiveConnectionLifecycleState::RotationPending, machine.state());
  ASSERT_FALSE(machine.has_successor());
  assert_reason_eq(ActiveConnectionRotationExemptionReason::AuthHandshake, machine.rotation_exemption_reason());

  auto resumed = machine.poll(policy, ActiveConnectionLifecycleInput{2100, false, false, false, false, true, true});
  ASSERT_TRUE(resumed.prepare_successor);
  ASSERT_TRUE(machine.has_successor());
  assert_reason_eq(ActiveConnectionRotationExemptionReason::None, machine.rotation_exemption_reason());
}

TEST(ActiveConnectionLifecycleStateMachine, UnsafeHandoverGateDefersRotationUntilSafePoint) {
  auto policy = default_policy();
  ActiveConnectionLifecycleStateMachine machine(ActiveConnectionLifecycleRole::Main, 1000, 2000);

  machine.mark_eligible();
  auto blocked = machine.poll(policy, ActiveConnectionLifecycleInput{2000, false, false, false, true, true, true});

  ASSERT_FALSE(blocked.prepare_successor);
  ASSERT_FALSE(blocked.route_new_queries_to_successor);
  ASSERT_FALSE(blocked.retire_current);
  ASSERT_FALSE(blocked.over_age_degraded);
  assert_state_eq(ActiveConnectionLifecycleState::RotationPending, machine.state());
  ASSERT_FALSE(machine.has_successor());
  assert_reason_eq(ActiveConnectionRotationExemptionReason::UnsafeHandoverPoint, machine.rotation_exemption_reason());

  auto resumed = machine.poll(policy, ActiveConnectionLifecycleInput{2100, false, false, false, false, true, true});
  ASSERT_TRUE(resumed.prepare_successor);
  ASSERT_TRUE(machine.has_successor());
  assert_reason_eq(ActiveConnectionRotationExemptionReason::None, machine.rotation_exemption_reason());
}

TEST(ActiveConnectionLifecycleStateMachine, HardCeilingWithoutSuccessorRaisesOverAgeSignal) {
  auto policy = default_policy();
  ActiveConnectionLifecycleStateMachine machine(ActiveConnectionLifecycleRole::Main, 1000, 2000);

  machine.mark_eligible();
  auto decision = machine.poll(policy, ActiveConnectionLifecycleInput{6100, true, false, false, false, false, false});

  ASSERT_FALSE(decision.prepare_successor);
  ASSERT_FALSE(decision.route_new_queries_to_successor);
  ASSERT_FALSE(decision.retire_current);
  ASSERT_TRUE(decision.over_age_degraded);
  assert_state_eq(ActiveConnectionLifecycleState::RotationPending, machine.state());
  ASSERT_TRUE(machine.is_over_age_degraded());
}

TEST(ActiveConnectionLifecycleStateMachine, HardCeilingStillSignalsOverAgeWhenAntiChurnBlocksRotation) {
  auto policy = default_policy();
  ActiveConnectionLifecycleStateMachine machine(ActiveConnectionLifecycleRole::Main, 1000, 2000);

  machine.mark_eligible();
  auto decision = machine.poll(policy, ActiveConnectionLifecycleInput{6100, true, false, false, false, true, false});

  ASSERT_FALSE(decision.prepare_successor);
  ASSERT_FALSE(decision.route_new_queries_to_successor);
  ASSERT_FALSE(decision.retire_current);
  ASSERT_TRUE(decision.over_age_degraded);
  assert_state_eq(ActiveConnectionLifecycleState::RotationPending, machine.state());
  assert_reason_eq(ActiveConnectionRotationExemptionReason::AntiChurn, machine.rotation_exemption_reason());
  ASSERT_TRUE(machine.is_over_age_degraded());
}

TEST(ActiveConnectionLifecycleStateMachine, SuppressedRotationDoesNotSignalOverAgeBeforeHardCeiling) {
  auto policy = default_policy();
  ActiveConnectionLifecycleStateMachine machine(ActiveConnectionLifecycleRole::Main, 1000, 2000);

  machine.mark_eligible();
  auto decision = machine.poll(policy, ActiveConnectionLifecycleInput{4500, true, false, false, false, true, false});

  ASSERT_FALSE(decision.prepare_successor);
  ASSERT_FALSE(decision.route_new_queries_to_successor);
  ASSERT_FALSE(decision.retire_current);
  ASSERT_FALSE(decision.over_age_degraded);
  ASSERT_FALSE(machine.is_over_age_degraded());
  assert_state_eq(ActiveConnectionLifecycleState::RotationPending, machine.state());
  assert_reason_eq(ActiveConnectionRotationExemptionReason::AntiChurn, machine.rotation_exemption_reason());
}

TEST(ActiveConnectionLifecycleStateMachine, SuccessorReadyClearsPreviousOverAgeDegradedState) {
  auto policy = default_policy();
  ActiveConnectionLifecycleStateMachine machine(ActiveConnectionLifecycleRole::Main, 1000, 2000);

  machine.mark_eligible();
  auto blocked = machine.poll(policy, ActiveConnectionLifecycleInput{6100, true, false, false, false, false, false});
  ASSERT_TRUE(blocked.over_age_degraded);
  ASSERT_TRUE(machine.is_over_age_degraded());

  auto resumed = machine.poll(policy, ActiveConnectionLifecycleInput{6200, false, false, false, false, true, true});
  ASSERT_TRUE(resumed.prepare_successor);
  ASSERT_TRUE(machine.has_successor());
  ASSERT_TRUE(machine.mark_successor_ready(6250));

  auto draining = machine.poll(policy, ActiveConnectionLifecycleInput{6250, true, false, false, false, true, true});
  ASSERT_TRUE(draining.route_new_queries_to_successor);
  ASSERT_FALSE(machine.is_over_age_degraded());
  assert_state_eq(ActiveConnectionLifecycleState::Draining, machine.state());
}

TEST(ActiveConnectionLifecycleStateMachine, SuccessorOverlapNeverCreatesMoreThanOneTemporaryExtraSocketPerRole) {
  auto policy = default_policy();
  ActiveConnectionLifecycleStateMachine machine(ActiveConnectionLifecycleRole::Main, 1000, 2000);

  machine.mark_eligible();
  ASSERT_TRUE(machine.poll(policy, ActiveConnectionLifecycleInput{2000, false, false, false, false, true, true})
                  .prepare_successor);

  auto decision = machine.poll(policy, ActiveConnectionLifecycleInput{2050, false, false, false, false, true, true});

  ASSERT_FALSE(decision.prepare_successor);
  ASSERT_EQ(1u, machine.rotation_attempts());
  ASSERT_TRUE(machine.has_successor());
  assert_state_eq(ActiveConnectionLifecycleState::RotationPending, machine.state());
}

TEST(ActiveConnectionLifecycleStateMachine, DrainingRetiresWhenOverlapBudgetExpires) {
  auto policy = default_policy();
  ActiveConnectionLifecycleStateMachine machine(ActiveConnectionLifecycleRole::Main, 1000, 2000);

  machine.mark_eligible();
  ASSERT_TRUE(machine.poll(policy, ActiveConnectionLifecycleInput{2000, false, false, false, false, true, true})
                  .prepare_successor);
  machine.mark_successor_ready(2100);
  ASSERT_TRUE(machine.poll(policy, ActiveConnectionLifecycleInput{2100, true, false, false, false, true, true})
                  .route_new_queries_to_successor);

  auto decision = machine.poll(policy, ActiveConnectionLifecycleInput{2501, true, false, false, false, true, true});

  ASSERT_TRUE(decision.retire_current);
  assert_state_eq(ActiveConnectionLifecycleState::Retired, machine.state());
}

TEST(ActiveConnectionLifecycleStateMachine, DrainingDoesNotRetireBeforeOverlapExpiryWhenQueriesInFlight) {
  auto policy = default_policy();
  ActiveConnectionLifecycleStateMachine machine(ActiveConnectionLifecycleRole::Main, 1000, 2000);

  machine.mark_eligible();
  ASSERT_TRUE(machine.poll(policy, ActiveConnectionLifecycleInput{2000, false, false, false, false, true, true})
                  .prepare_successor);
  machine.mark_successor_ready(2100);
  ASSERT_TRUE(machine.poll(policy, ActiveConnectionLifecycleInput{2100, true, false, false, false, true, true})
                  .route_new_queries_to_successor);

  auto decision = machine.poll(policy, ActiveConnectionLifecycleInput{2400, true, false, false, false, true, true});

  ASSERT_FALSE(decision.prepare_successor);
  ASSERT_FALSE(decision.route_new_queries_to_successor);
  ASSERT_FALSE(decision.retire_current);
  assert_state_eq(ActiveConnectionLifecycleState::Draining, machine.state());
}

TEST(ActiveConnectionLifecycleStateMachine, FailedSuccessorPreparationRespectsBackoffBeforeRetrying) {
  auto policy = default_policy();
  ActiveConnectionLifecycleStateMachine machine(ActiveConnectionLifecycleRole::Main, 1000, 2000);

  machine.mark_eligible();
  ASSERT_TRUE(machine.poll(policy, ActiveConnectionLifecycleInput{2000, false, false, false, false, true, true})
                  .prepare_successor);
  machine.mark_successor_failed(2050, policy.rotation_backoff_ms, ActiveConnectionRotationExemptionReason::None);

  ASSERT_FALSE(machine.poll(policy, ActiveConnectionLifecycleInput{2100, false, false, false, false, true, true})
                   .prepare_successor);
  ASSERT_TRUE(machine.poll(policy, ActiveConnectionLifecycleInput{2150, false, false, false, false, true, true})
                  .prepare_successor);
}

TEST(ActiveConnectionLifecycleStateMachine, ShutdownSuppressesSuccessorPreparationUntilSessionReopens) {
  auto policy = default_policy();
  ActiveConnectionLifecycleStateMachine machine(ActiveConnectionLifecycleRole::Main, 1000, 2000);

  machine.mark_eligible();
  auto blocked = machine.poll(policy, ActiveConnectionLifecycleInput{2000, false, false, true, false, true, true});

  ASSERT_FALSE(blocked.prepare_successor);
  ASSERT_FALSE(blocked.route_new_queries_to_successor);
  ASSERT_FALSE(blocked.retire_current);
  ASSERT_FALSE(blocked.over_age_degraded);
  assert_state_eq(ActiveConnectionLifecycleState::RotationPending, machine.state());
  assert_reason_eq(ActiveConnectionRotationExemptionReason::Shutdown, machine.rotation_exemption_reason());

  auto resumed = machine.poll(policy, ActiveConnectionLifecycleInput{2001, false, false, false, false, true, true});
  ASSERT_TRUE(resumed.prepare_successor);
}

}  // namespace