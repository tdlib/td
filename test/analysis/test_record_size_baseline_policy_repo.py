# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

import json
import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
REGISTRY_PATH = REPO_ROOT / "test" / "analysis" / "profiles_validation.json"


class RecordSizeBaselinePolicyRepoTest(unittest.TestCase):
    def test_checked_in_registry_contains_record_size_policy(self) -> None:
        registry = json.loads(REGISTRY_PATH.read_text(encoding="utf-8"))
        policy = registry.get("record_size_baseline_policy")

        self.assertIsInstance(policy, dict)
        self.assertIn("reference_sizes", policy)
        self.assertIn("greeting_ranges", policy)
        self.assertIn("small_record_threshold", policy)
        self.assertIn("small_record_max_fraction", policy)
        self.assertIn("bulk_record_threshold", policy)
        self.assertIn("bulk_record_min_fraction", policy)
        self.assertIn("bucket_boundaries", policy)
        self.assertIn("bucket_overhead", policy)
        self.assertIn("bucket_tolerance", policy)
        self.assertIn("bucket_excess_ratio_threshold", policy)
        self.assertIn("max_lag1_autocorrelation_abs", policy)
        self.assertIn("max_adjacent_size_ratio", policy)
        self.assertIn("ks_min_pvalue", policy)
        self.assertIn("chi_squared_min_pvalue", policy)
        self.assertIn("bin_width", policy)

    def test_checked_in_record_size_policy_values_are_sane(self) -> None:
        registry = json.loads(REGISTRY_PATH.read_text(encoding="utf-8"))
        policy = registry["record_size_baseline_policy"]

        reference_sizes = policy["reference_sizes"]
        greeting_ranges = policy["greeting_ranges"]

        self.assertGreaterEqual(len(reference_sizes), 64)
        self.assertTrue(all(isinstance(size, int) and 0 < size <= 16640 for size in reference_sizes))
        self.assertGreaterEqual(len(greeting_ranges), 3)
        self.assertTrue(all(isinstance(entry, list) and len(entry) == 2 for entry in greeting_ranges))
        self.assertGreaterEqual(policy["small_record_threshold"], 200)
        self.assertLessEqual(policy["small_record_max_fraction"], 0.50)
        self.assertGreaterEqual(policy["bulk_record_threshold"], 8192)
        self.assertGreaterEqual(policy["bulk_record_min_fraction"], 0.0)
        self.assertGreater(policy["ks_min_pvalue"], 0.0)
        self.assertGreater(policy["chi_squared_min_pvalue"], 0.0)
        self.assertGreater(policy["bin_width"], 0)


if __name__ == "__main__":
    unittest.main()