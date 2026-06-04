#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT

from __future__ import annotations

import json
import pathlib
import subprocess
import tempfile
import unittest

THIS_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = THIS_DIR.resolve().parents[1]


class TransportMetricsUnavailableNotZero(unittest.TestCase):
    """Covers: RISK-FP-06 (transport score abuse)"""

    def test_extractor_output_keeps_unavailable_syn_metrics_as_null(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            output_path = pathlib.Path(tmp) / "transport_observations.json"
            subprocess.check_call(
                [
                    "python3",
                    "test/analysis/extract_tcp_transport_signatures.py",
                    "--repo-root",
                    ".",
                    "--now-utc",
                    "2026-04-26T00:00:00Z",
                    "--output",
                    str(output_path),
                ],
                cwd=REPO_ROOT,
            )
            payload = json.loads(output_path.read_text(encoding="utf-8"))

        self.assertFalse(payload["evidence_scope"]["syn_phase_transport_available"])
        for metric_name in (
            "ttl_bucket_match_rate",
            "syn_option_order_class_match_rate",
            "mss_window_scale_bucket_match_rate",
        ):
            self.assertIsNone(payload["metrics"][metric_name])
            self.assertNotEqual(0.0, payload["metrics"][metric_name])


if __name__ == "__main__":
    unittest.main()
