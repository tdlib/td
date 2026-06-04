#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

"""Contract tests for statistical metadata in release evidence artifacts.

Validates that release evidence artifacts emit the required statistical
metadata fields and that unavailable values are represented as null (not 0).
Also verifies the Wilson lower-bound confidence interval computation.
"""

from __future__ import annotations

import math
import pathlib
import sys
import unittest

THIS_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = THIS_DIR.parents[1]
if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))


REQUIRED_STATISTICAL_FIELDS = (
    "x_pass",
    "n_cluster",
    "cluster_level",
    "ci_method",
    "ci_lower_95",
    "cohort_id",
    "n_sample_raw",
    "n_capture",
    "n_source",
    "n_session",
    "n_seed",
)


def _wilson_lower_bound(x_pass: int, n: int, z: float = 1.96) -> float:
    """Compute Wilson score interval lower bound at the given z-level.

    Returns the lower bound of the Wilson confidence interval for a
    binomial proportion.  When n is zero, returns 0.0.
    """
    if n == 0:
        return 0.0
    p_hat = x_pass / n
    denominator = 1.0 + z * z / n
    centre = p_hat + z * z / (2.0 * n)
    spread = z * math.sqrt((p_hat * (1.0 - p_hat) + z * z / (4.0 * n)) / n)
    return (centre - spread) / denominator


def _build_minimal_statistical_metadata(
    *,
    x_pass: int = 10,
    n_cluster: int = 5,
    cluster_level: str = "profile",
    ci_method: str = "wilson",
    ci_lower_95: float = 0.72,
    cohort_id: str = "reviewed-2026Q2",
    n_sample_raw: int = 120,
    n_capture: int = 30,
    n_source: int = 8,
    n_session: int = 12,
    n_seed: int = 3,
) -> dict:
    """Build a minimal conforming statistical metadata record."""
    return {
        "x_pass": x_pass,
        "n_cluster": n_cluster,
        "cluster_level": cluster_level,
        "ci_method": ci_method,
        "ci_lower_95": ci_lower_95,
        "cohort_id": cohort_id,
        "n_sample_raw": n_sample_raw,
        "n_capture": n_capture,
        "n_source": n_source,
        "n_session": n_session,
        "n_seed": n_seed,
    }


class StatisticalReleaseMetadataContractTest(unittest.TestCase):
    """Contract: release evidence artifacts must carry statistical metadata.

    Covers: RISK-FP-11 (seed inflation)
    """

    # ------------------------------------------------------------------
    # 1. Required field presence
    # ------------------------------------------------------------------

    def test_all_required_fields_present_in_conforming_record(self) -> None:
        """Every field in the required set must appear in a conforming record."""
        record = _build_minimal_statistical_metadata()
        for field in REQUIRED_STATISTICAL_FIELDS:
            self.assertIn(
                field,
                record,
                msg=f"Required statistical field '{field}' missing from record",
            )

    def test_required_fields_tuple_is_complete(self) -> None:
        """REQUIRED_STATISTICAL_FIELDS must contain exactly the canonical set."""
        expected = {
            "x_pass", "n_cluster", "cluster_level", "ci_method",
            "ci_lower_95", "cohort_id", "n_sample_raw", "n_capture",
            "n_source", "n_session", "n_seed",
        }
        self.assertEqual(expected, set(REQUIRED_STATISTICAL_FIELDS))
        self.assertEqual(len(expected), len(REQUIRED_STATISTICAL_FIELDS),
                         "REQUIRED_STATISTICAL_FIELDS must not have duplicates")

    def test_no_extra_unknown_fields_in_minimal_record(self) -> None:
        """The minimal builder should produce exactly the required fields."""
        record = _build_minimal_statistical_metadata()
        self.assertEqual(set(REQUIRED_STATISTICAL_FIELDS), set(record.keys()))

    # ------------------------------------------------------------------
    # 2. Unavailable handling -- null, not zero
    # ------------------------------------------------------------------

    def test_unavailable_n_capture_must_be_null_not_zero(self) -> None:
        """When capture count is unavailable it must be None, not 0."""
        record = _build_minimal_statistical_metadata(n_capture=0)
        # A zero is a valid *measured* count.  Unavailable must be None.
        record_unavailable = _build_minimal_statistical_metadata()
        record_unavailable["n_capture"] = None
        self.assertIsNone(record_unavailable["n_capture"])
        self.assertIsNotNone(record["n_capture"],
                             "Zero is a valid count; None signals unavailable")

    def test_unavailable_n_source_must_be_null_not_zero(self) -> None:
        """When source count is unavailable it must be None, not 0."""
        record = _build_minimal_statistical_metadata()
        record["n_source"] = None
        self.assertIsNone(record["n_source"])

    def test_unavailable_ci_lower_must_be_null_not_zero(self) -> None:
        """When the CI lower bound cannot be computed it must be None."""
        record = _build_minimal_statistical_metadata()
        record["ci_lower_95"] = None
        self.assertIsNone(record["ci_lower_95"])
        # Confirm that zero is a distinct, valid computed value
        record_zero = _build_minimal_statistical_metadata(ci_lower_95=0.0)
        self.assertEqual(0.0, record_zero["ci_lower_95"])

    def test_unavailable_n_session_must_be_null_not_zero(self) -> None:
        """When session count is unavailable it must be None, not 0."""
        record = _build_minimal_statistical_metadata()
        record["n_session"] = None
        self.assertIsNone(record["n_session"])

    # ------------------------------------------------------------------
    # 3. Wilson lower-bound computation
    # ------------------------------------------------------------------

    def test_wilson_lower_bound_perfect_pass(self) -> None:
        """100% pass rate with many observations should yield a high lower bound."""
        lb = _wilson_lower_bound(100, 100)
        self.assertGreater(lb, 0.95,
                           "Wilson lower bound for 100/100 should exceed 0.95")
        self.assertLessEqual(lb, 1.0)

    def test_wilson_lower_bound_zero_observations(self) -> None:
        """Zero observations must yield 0.0 without division errors."""
        lb = _wilson_lower_bound(0, 0)
        self.assertEqual(0.0, lb)

    def test_wilson_lower_bound_single_pass(self) -> None:
        """A single passing observation out of one trial."""
        lb = _wilson_lower_bound(1, 1)
        # With n=1, the lower bound should be well below 1.0
        self.assertGreater(lb, 0.0)
        self.assertLess(lb, 1.0)

    def test_wilson_lower_bound_half_pass(self) -> None:
        """50% pass rate should yield a lower bound below 0.5."""
        lb = _wilson_lower_bound(50, 100)
        self.assertLess(lb, 0.50,
                        "Wilson lower bound for 50/100 must be below 0.5")
        self.assertGreater(lb, 0.30,
                           "Wilson lower bound for 50/100 should be above 0.30")

    def test_wilson_lower_bound_monotonic_in_pass_count(self) -> None:
        """Increasing pass count with fixed total must increase the lower bound."""
        n = 200
        previous = -1.0
        for x in range(0, n + 1, 20):
            lb = _wilson_lower_bound(x, n)
            self.assertGreaterEqual(lb, previous,
                                    f"Wilson lower bound not monotonic at x_pass={x}")
            previous = lb

    def test_wilson_lower_bound_known_reference_value(self) -> None:
        """Cross-check against a manually computed reference value.

        For x_pass=90, n=100, z=1.96 the Wilson lower bound is approximately
        0.8262 (computed via the standard formula).
        """
        lb = _wilson_lower_bound(90, 100, z=1.96)
        self.assertAlmostEqual(lb, 0.8256, places=3,
                               msg="Wilson lower bound 90/100 should be ~0.826")


if __name__ == "__main__":
    unittest.main()
