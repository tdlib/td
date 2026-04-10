# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

import json
import pathlib
import sys
import unittest


THIS_DIR = pathlib.Path(__file__).resolve().parent
if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))

from check_record_size_distribution import check_record_size_distribution
from common_smoke import load_json_report


REPO_ROOT = THIS_DIR.parent.parent
REGISTRY_PATH = REPO_ROOT / "test" / "analysis" / "profiles_validation.json"
FIXTURES_ROOT = REPO_ROOT / "test" / "analysis" / "fixtures" / "record_sizes"


class RecordSizeFixtureCorpusRepoTest(unittest.TestCase):
    def test_checked_in_record_size_fixture_corpus_exists(self) -> None:
        artifacts = sorted(FIXTURES_ROOT.rglob("*.json"))

        self.assertTrue(artifacts)

    def test_checked_in_record_size_fixture_corpus_passes_policy(self) -> None:
        registry = json.loads(REGISTRY_PATH.read_text(encoding="utf-8"))
        policy = registry["record_size_baseline_policy"]
        artifacts = sorted(FIXTURES_ROOT.rglob("*.json"))

        self.assertTrue(artifacts)
        sample_count = 0
        for artifact_path in artifacts:
            artifact = load_json_report(artifact_path)
            failures = check_record_size_distribution(artifact, policy)
            sample_count += len(artifact.get("record_payload_sizes", [])) if isinstance(artifact.get("record_payload_sizes"), list) else 0
            if isinstance(artifact.get("connections"), list) and not isinstance(artifact.get("record_payload_sizes"), list):
                sample_count += sum(
                    len(connection.get("records", []))
                    for connection in artifact["connections"]
                    if isinstance(connection, dict) and isinstance(connection.get("records"), list)
                )
            self.assertEqual([], failures, msg=f"{artifact_path}: {failures}")
        self.assertGreaterEqual(sample_count, 256)


if __name__ == "__main__":
    unittest.main()