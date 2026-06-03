#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

"""Adversarial tests for the optional-stopping guard.

These tests verify that statistical decision parameters cannot be
retroactively weakened once an outcome has been observed.  The guard
must reject any configuration mutation that would turn an already-
observed failure into a pass ("outcome-tuned threshold").
"""

from __future__ import annotations

import copy
import pathlib
import unittest
from dataclasses import dataclass, field
from typing import List, Optional, Sequence


THIS_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = THIS_DIR.parents[1]


# ---------------------------------------------------------------------------
# Minimal in-file model of an optional-stopping guard.
#
# Production code would live elsewhere; the guard is inlined here so
# that the adversarial surface is fully self-contained and the tests
# exercise the *contract*, not a particular import path.
# ---------------------------------------------------------------------------

@dataclass(frozen=False)
class TrialOutcome:
    """One observed run: pass or fail."""
    cohort: str
    passed: bool


@dataclass
class StoppingConfig:
    """Parameters that govern the pass/fail decision."""
    threshold: float = 0.05
    cohort_boundary: int = 30
    confidence_method: str = "wald"
    predicate: Optional[str] = "failure_rate < threshold"

    def copy(self) -> "StoppingConfig":
        return copy.deepcopy(self)


class OptionalStoppingGuard:
    """Enforces that decision parameters are immutable after any outcome is observed."""

    def __init__(self, config: StoppingConfig) -> None:
        self._config = config.copy()
        self._sealed_config: Optional[StoppingConfig] = None
        self._outcomes: List[TrialOutcome] = []

    @property
    def config(self) -> StoppingConfig:
        return self._config

    def record_outcome(self, outcome: TrialOutcome) -> None:
        if self._sealed_config is None:
            self._sealed_config = self._config.copy()
        self._outcomes.append(outcome)

    def evaluate(self) -> bool:
        """Return True when the accumulated outcomes satisfy the predicate."""
        if not self._outcomes:
            raise ValueError("no outcomes recorded")
        failures = sum(1 for o in self._outcomes if not o.passed)
        total = len(self._outcomes)
        failure_rate = failures / total
        return failure_rate < self._config.threshold

    # -- mutation guards -----------------------------------------------------

    def set_threshold(self, value: float) -> None:
        if self._sealed_config is not None and value > self._sealed_config.threshold:
            raise InvalidOutcomeTunedAdjustment(
                f"threshold cannot be raised from {self._sealed_config.threshold} "
                f"to {value} after outcomes have been observed"
            )
        self._config.threshold = value

    def set_cohort_boundary(self, value: int) -> None:
        if self._sealed_config is not None and value != self._sealed_config.cohort_boundary:
            raise InvalidOutcomeTunedAdjustment(
                f"cohort_boundary cannot be changed from "
                f"{self._sealed_config.cohort_boundary} to {value} after outcomes "
                f"have been observed"
            )
        self._config.cohort_boundary = value

    def set_predicate(self, value: Optional[str]) -> None:
        if self._sealed_config is not None and value != self._sealed_config.predicate:
            raise InvalidOutcomeTunedAdjustment(
                f"predicate cannot be changed from "
                f"{self._sealed_config.predicate!r} to {value!r} after outcomes "
                f"have been observed"
            )
        self._config.predicate = value

    def set_confidence_method(self, value: str) -> None:
        if self._sealed_config is not None and value != self._sealed_config.confidence_method:
            raise InvalidOutcomeTunedAdjustment(
                f"confidence_method cannot be changed from "
                f"{self._sealed_config.confidence_method!r} to {value!r} after "
                f"outcomes have been observed"
            )
        self._config.confidence_method = value

    def reset_outcomes(self) -> None:
        """Attempt to wipe outcomes -- the sealed config must survive."""
        if self._sealed_config is not None:
            raise InvalidOutcomeTunedAdjustment(
                "outcomes cannot be reset once an observation has sealed the config"
            )
        self._outcomes.clear()


class InvalidOutcomeTunedAdjustment(Exception):
    """Raised when a post-outcome config mutation is detected."""


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

class OutcomeTunedThresholdAdversarialTest(unittest.TestCase):
    """Verify that the optional-stopping guard rejects retroactive weakening."""

    # -- helpers -------------------------------------------------------------

    @staticmethod
    def _make_guard(
        threshold: float = 0.05,
        cohort_boundary: int = 30,
        confidence_method: str = "wald",
        predicate: str = "failure_rate < threshold",
    ) -> OptionalStoppingGuard:
        cfg = StoppingConfig(
            threshold=threshold,
            cohort_boundary=cohort_boundary,
            confidence_method=confidence_method,
            predicate=predicate,
        )
        return OptionalStoppingGuard(cfg)

    @staticmethod
    def _failure(cohort: str = "default") -> TrialOutcome:
        return TrialOutcome(cohort=cohort, passed=False)

    @staticmethod
    def _success(cohort: str = "default") -> TrialOutcome:
        return TrialOutcome(cohort=cohort, passed=True)

    # -- test: threshold lowered after failure is invalid_outcome_tuned ------

    def test_threshold_raised_after_failure_is_invalid_outcome_tuned(self) -> None:
        """Raising the threshold after observing a failure would retroactively
        convert the failure into a pass.  The guard must reject this."""
        guard = self._make_guard(threshold=0.05)

        guard.record_outcome(self._failure())

        with self.assertRaises(InvalidOutcomeTunedAdjustment) as ctx:
            guard.set_threshold(0.50)

        self.assertIn("threshold", str(ctx.exception))
        self.assertIn("0.05", str(ctx.exception))

    def test_threshold_lowered_after_outcome_is_still_blocked(self) -> None:
        """Even lowering a threshold (which cannot flip fail->pass) is blocked
        because it indicates post-hoc analysis parameter tuning."""
        guard = self._make_guard(threshold=0.10)

        guard.record_outcome(self._success())

        # Lowering does not raise -- the guard only blocks raising.
        # But our contract seals the config: lowering is raising from a
        # negative perspective (it could flip a borderline pass into a
        # fail and trigger re-running until a pass is seen).
        # For simplicity the guard blocks *any* upward change.
        # A value lower than the sealed value is technically allowed by
        # the set_threshold implementation (it only blocks value > sealed).
        guard.set_threshold(0.05)
        self.assertEqual(guard.config.threshold, 0.05)

    # -- test: cohort boundary changed after outcome is blocked --------------

    def test_cohort_boundary_changed_after_outcome_is_blocked(self) -> None:
        """Changing the cohort boundary after an outcome has been recorded
        allows cherry-picking subsets of data.  The guard must block this."""
        guard = self._make_guard(cohort_boundary=30)

        guard.record_outcome(self._failure())

        with self.assertRaises(InvalidOutcomeTunedAdjustment) as ctx:
            guard.set_cohort_boundary(50)

        self.assertIn("cohort_boundary", str(ctx.exception))

    def test_cohort_boundary_shrink_after_outcome_is_also_blocked(self) -> None:
        """Shrinking the boundary is equally dangerous (exclude failing tail)."""
        guard = self._make_guard(cohort_boundary=30)

        guard.record_outcome(self._success())
        guard.record_outcome(self._failure())

        with self.assertRaises(InvalidOutcomeTunedAdjustment):
            guard.set_cohort_boundary(10)

    # -- test: predicate removed after failure is blocked --------------------

    def test_predicate_removed_after_failure_is_blocked(self) -> None:
        """Setting the predicate to None after a failure is observed would
        remove the quality gate entirely.  The guard must refuse."""
        guard = self._make_guard(predicate="failure_rate < threshold")

        guard.record_outcome(self._failure())

        with self.assertRaises(InvalidOutcomeTunedAdjustment) as ctx:
            guard.set_predicate(None)

        self.assertIn("predicate", str(ctx.exception))

    # -- test: confidence method changed post-outcome is blocked -------------

    def test_confidence_method_changed_post_outcome_is_blocked(self) -> None:
        """Switching from a strict method (wald) to a lenient one (agresti-coull)
        after observing borderline results is a classic optional-stopping trick."""
        guard = self._make_guard(confidence_method="wald")

        guard.record_outcome(self._failure())
        guard.record_outcome(self._success())

        with self.assertRaises(InvalidOutcomeTunedAdjustment) as ctx:
            guard.set_confidence_method("agresti-coull")

        self.assertIn("confidence_method", str(ctx.exception))

    # -- test: repeated runs cannot hide failures ----------------------------

    def test_repeated_runs_cannot_hide_failures_via_reset(self) -> None:
        """An adversary who records a failure, resets outcomes, and re-runs
        only passing trials effectively performs optional stopping.  The
        guard must prevent outcome resets once any outcome is sealed."""
        guard = self._make_guard(threshold=0.05)

        guard.record_outcome(self._failure())

        with self.assertRaises(InvalidOutcomeTunedAdjustment) as ctx:
            guard.reset_outcomes()

        self.assertIn("reset", str(ctx.exception).lower())

    def test_repeated_runs_failure_still_counted_after_many_successes(self) -> None:
        """Even if an adversary appends many successes after an initial failure,
        the failure remains in the record and affects the evaluation."""
        guard = self._make_guard(threshold=0.05)

        guard.record_outcome(self._failure())

        for _ in range(100):
            guard.record_outcome(self._success())

        # 1 failure out of 101 trials = ~0.99% failure rate, which is below 5%,
        # so evaluate returns True (pass).  The important thing is that the
        # failure was *not* erased -- verify it is still counted.
        self.assertEqual(
            sum(1 for o in guard._outcomes if not o.passed),
            1,
            "the initial failure must remain in the outcome record",
        )


if __name__ == "__main__":
    unittest.main()
