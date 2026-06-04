#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

"""Contract tests for small-n statistical wording.

When n_cluster < 3, outputs must use count-first phrasing (e.g.
"1/1 reviewed capture matched") rather than high-confidence percentage
claims.  Percentages, when present, must include the Wilson lower bound
and the denominator so that readers can judge statistical power.  Phrases
such as "100% fidelity" are rejected outright for small n.
"""

from __future__ import annotations

import math
import pathlib
import re
import sys
import unittest

THIS_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = THIS_DIR.parents[1]
if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))


# ---------------------------------------------------------------------------
# Helpers -- mirror the Wilson lower-bound computation used elsewhere in the
# test-analysis suite so that wording validators can cross-check bounds.
# ---------------------------------------------------------------------------

def _wilson_lower_bound(x_pass: int, n: int, z: float = 1.96) -> float:
    """Wilson score interval lower bound (95 % by default).

    Returns 0.0 when *n* is zero.
    """
    if n == 0:
        return 0.0
    p_hat = x_pass / n
    denominator = 1.0 + z * z / n
    centre = p_hat + z * z / (2.0 * n)
    spread = z * math.sqrt((p_hat * (1.0 - p_hat) + z * z / (4.0 * n)) / n)
    return (centre - spread) / denominator


def _format_small_n_wording(x_pass: int, n_cluster: int) -> str:
    """Produce the canonical wording for a small-n result.

    Rules
    -----
    * n_cluster == 0  -> "reviewed capture data unavailable"
    * n_cluster == 1  -> "1/1 reviewed capture matched"  (or "0/1 ...")
    * n_cluster == 2  -> "N/2 reviewed captures matched"
    * n_cluster >= 3  -> percentage form (not tested here)
    """
    if n_cluster == 0:
        return "reviewed capture data unavailable"
    noun = "capture" if n_cluster == 1 else "captures"
    return f"{x_pass}/{n_cluster} reviewed {noun} matched"


def _format_percentage_with_ci(
    x_pass: int, n_cluster: int, z: float = 1.96,
) -> str:
    """Produce a percentage string that includes Wilson lower bound and N.

    Example output: "90.0 % (Wilson lower 82.6 %, n=100)"
    """
    if n_cluster == 0:
        return "reviewed capture data unavailable"
    pct = 100.0 * x_pass / n_cluster
    lb = _wilson_lower_bound(x_pass, n_cluster, z=z)
    return f"{pct:.1f}% (Wilson lower {100.0 * lb:.1f}%, n={n_cluster})"


# Regex that matches bare high-confidence percentage claims with no
# denominator or CI qualifier.
_HIGH_CONFIDENCE_RE = re.compile(
    r"\b100(?:\.0+)?\s*%\s*(?:fidelity|match|accuracy|pass)",
    re.IGNORECASE,
)

# Regex that matches a well-formed percentage *with* Wilson lower bound
# and denominator -- the only acceptable percentage form.
_QUALIFIED_PCT_RE = re.compile(
    r"\d+\.\d+%\s*\(Wilson\s+lower\s+\d+\.\d+%,\s*n=\d+\)",
)


class SmallNStatisticalWordingContractTest(unittest.TestCase):
    """Contract: n_cluster < 3 must use count-first, not percentage wording.

    Covers: RISK-FP-25 (small-n overstatement)
    """

    # ------------------------------------------------------------------
    # 1. n == 1: count-first wording
    # ------------------------------------------------------------------

    def test_n1_pass_uses_count_first_wording(self) -> None:
        """n_cluster=1 with a passing capture must say '1/1 reviewed capture matched'."""
        wording = _format_small_n_wording(x_pass=1, n_cluster=1)
        self.assertEqual(
            "1/1 reviewed capture matched",
            wording,
            "n=1 must use exact count-first phrasing, not a percentage",
        )

    def test_n1_fail_uses_count_first_wording(self) -> None:
        """n_cluster=1 with a failing capture must say '0/1 reviewed capture matched'."""
        wording = _format_small_n_wording(x_pass=0, n_cluster=1)
        self.assertEqual(
            "0/1 reviewed capture matched",
            wording,
            "n=1 failure must still use count-first phrasing",
        )

    # ------------------------------------------------------------------
    # 2. n == 2: count-first wording (plural)
    # ------------------------------------------------------------------

    def test_n2_all_pass_uses_count_first_wording(self) -> None:
        """n_cluster=2 must say '2/2 reviewed captures matched'."""
        wording = _format_small_n_wording(x_pass=2, n_cluster=2)
        self.assertEqual(
            "2/2 reviewed captures matched",
            wording,
            "n=2 must use count-first phrasing with plural 'captures'",
        )

    def test_n2_partial_pass_uses_count_first_wording(self) -> None:
        """n_cluster=2 with one failure must say '1/2 reviewed captures matched'."""
        wording = _format_small_n_wording(x_pass=1, n_cluster=2)
        self.assertEqual(
            "1/2 reviewed captures matched",
            wording,
        )

    # ------------------------------------------------------------------
    # 3. n == 0: unavailable
    # ------------------------------------------------------------------

    def test_n0_reports_unavailable(self) -> None:
        """n_cluster=0 must produce 'reviewed capture data unavailable'."""
        wording = _format_small_n_wording(x_pass=0, n_cluster=0)
        self.assertIn(
            "unavailable",
            wording,
            "n=0 must explicitly state data is unavailable",
        )
        self.assertEqual("reviewed capture data unavailable", wording)

    # ------------------------------------------------------------------
    # 4. Percentages must include Wilson lower bound and denominator
    # ------------------------------------------------------------------

    def test_percentage_includes_wilson_lower_and_denominator(self) -> None:
        """Any percentage output must carry the Wilson lower bound and n=<N>."""
        text = _format_percentage_with_ci(x_pass=9, n_cluster=10)
        self.assertRegex(
            text,
            _QUALIFIED_PCT_RE,
            "Percentage wording must include '(Wilson lower ...%, n=<N>)'",
        )
        self.assertIn("n=10", text)

    def test_percentage_for_large_n_includes_ci(self) -> None:
        """Even for larger n the Wilson lower bound must be present."""
        text = _format_percentage_with_ci(x_pass=95, n_cluster=100)
        self.assertRegex(text, _QUALIFIED_PCT_RE)
        self.assertIn("n=100", text)
        # Sanity: the lower bound should be meaningfully below 95 %
        lb = _wilson_lower_bound(95, 100)
        self.assertLess(lb, 0.95)
        self.assertGreater(lb, 0.85)

    # ------------------------------------------------------------------
    # 5. "100% fidelity" must be rejected for small n
    # ------------------------------------------------------------------

    def test_100_percent_fidelity_rejected_for_n1(self) -> None:
        """The phrase '100% fidelity' must never appear when n_cluster < 3."""
        wording = _format_small_n_wording(x_pass=1, n_cluster=1)
        self.assertIsNone(
            _HIGH_CONFIDENCE_RE.search(wording),
            "Small-n output must not contain '100% fidelity' or similar "
            "high-confidence percentage claims",
        )

    # ------------------------------------------------------------------
    # 6. Generic high-confidence percentage wording rejected for small n
    # ------------------------------------------------------------------

    def test_high_confidence_percentage_rejected_for_small_n(self) -> None:
        """No variant of '100% match/accuracy/pass' may appear for n < 3."""
        for n in (0, 1, 2):
            wording = _format_small_n_wording(x_pass=n, n_cluster=n)
            self.assertIsNone(
                _HIGH_CONFIDENCE_RE.search(wording),
                f"High-confidence percentage claim found in small-n (n={n}) "
                f"output: {wording!r}",
            )


if __name__ == "__main__":
    unittest.main()
