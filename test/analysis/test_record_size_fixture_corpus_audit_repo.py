# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

import pathlib
import sys
import unittest


THIS_DIR = pathlib.Path(__file__).resolve().parent
if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))

from audit_record_size_corpus import audit_record_size_corpus
from common_smoke import load_json_report


REPO_ROOT = THIS_DIR.parent.parent
FIXTURE_PATH = REPO_ROOT / "test" / "analysis" / "fixtures" / "record_sizes" / "capture_corpus.aggregate.record_sizes.json"


class RecordSizeFixtureCorpusAuditRepoTest(unittest.TestCase):
    def test_checked_in_aggregate_corpus_exposes_directional_small_record_anomalies(self) -> None:
        report = load_json_report(FIXTURE_PATH)
        metrics, findings = audit_record_size_corpus(report)

        self.assertGreater(metrics["overall_small_fraction"], 0.30)
        self.assertGreater(metrics["c2s_small_fraction"], 0.45)
        self.assertGreater(metrics["s2c_small_fraction"], 0.20)
        self.assertGreater(metrics["post_first_five_c2s_small_fraction"], 0.45)
        self.assertIn("overall-small-record-budget", findings)
        self.assertIn("c2s-small-record-budget", findings)
        self.assertIn("s2c-small-record-budget", findings)
        self.assertIn("post-greeting-c2s-small-record-budget", findings)


if __name__ == "__main__":
    unittest.main()