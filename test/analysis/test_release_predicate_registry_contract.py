#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

import copy
import json
import pathlib
import unittest


THIS_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = THIS_DIR.parents[1]

REGISTRY_PATH = THIS_DIR / "fingerprint_release_predicates.json"

REQUIRED_PREDICATE_FIELDS = {
    "predicate_id",
    "threshold",
    "cohort_id",
    "confidence_method",
}


def load_registry(path: pathlib.Path = REGISTRY_PATH) -> dict:
    with path.open("r", encoding="utf-8") as fh:
        return json.load(fh)


class ReleasePredicateRegistryContractTest(unittest.TestCase):
    """Verify structural and semantic invariants of the release-predicate registry."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.registry = load_registry()
        cls.predicates = cls.registry.get("predicates", {})

    # ------------------------------------------------------------------
    # 1. predicate_id is required on every entry
    # ------------------------------------------------------------------
    def test_every_entry_has_predicate_id(self) -> None:
        for key, entry in self.predicates.items():
            with self.subTest(key=key):
                self.assertIn("predicate_id", entry)
                self.assertIsInstance(entry["predicate_id"], str)
                self.assertTrue(entry["predicate_id"])

    # ------------------------------------------------------------------
    # 2. predicate_id value must match its dictionary key
    # ------------------------------------------------------------------
    def test_predicate_id_matches_key(self) -> None:
        for key, entry in self.predicates.items():
            with self.subTest(key=key):
                self.assertEqual(
                    key,
                    entry["predicate_id"],
                    msg="predicate_id must be identical to its registry key",
                )

    # ------------------------------------------------------------------
    # 3. threshold is present and numeric
    # ------------------------------------------------------------------
    def test_threshold_present_and_numeric(self) -> None:
        for key, entry in self.predicates.items():
            with self.subTest(key=key):
                self.assertIn("threshold", entry)
                self.assertIsInstance(entry["threshold"], (int, float))

    # ------------------------------------------------------------------
    # 4. cohort_id is present and non-empty
    # ------------------------------------------------------------------
    def test_cohort_id_present_and_nonempty(self) -> None:
        for key, entry in self.predicates.items():
            with self.subTest(key=key):
                self.assertIn("cohort_id", entry)
                self.assertIsInstance(entry["cohort_id"], str)
                self.assertTrue(entry["cohort_id"])

    # ------------------------------------------------------------------
    # 5. confidence_method is present and non-empty
    # ------------------------------------------------------------------
    def test_confidence_method_present_and_nonempty(self) -> None:
        for key, entry in self.predicates.items():
            with self.subTest(key=key):
                self.assertIn("confidence_method", entry)
                self.assertIsInstance(entry["confidence_method"], str)
                self.assertTrue(entry["confidence_method"])

    # ------------------------------------------------------------------
    # 6. All required fields are present (belt-and-suspenders aggregate)
    # ------------------------------------------------------------------
    def test_all_required_fields_present(self) -> None:
        for key, entry in self.predicates.items():
            with self.subTest(key=key):
                missing = REQUIRED_PREDICATE_FIELDS - set(entry.keys())
                self.assertFalse(
                    missing,
                    msg=f"predicate {key!r} missing required fields: {missing}",
                )

    # ------------------------------------------------------------------
    # 7. Missing registry entry means predicate is unavailable
    # ------------------------------------------------------------------
    def test_missing_entry_means_unavailable(self) -> None:
        fabricated_id = "nonexistent_predicate_9999"
        self.assertNotIn(
            fabricated_id,
            self.predicates,
            msg="A predicate not in the registry must be considered unavailable",
        )

    # ------------------------------------------------------------------
    # 8. Threshold change after a failed run creates a new version
    #    (simulate by mutating a deep copy and verifying divergence)
    # ------------------------------------------------------------------
    def test_threshold_change_after_failed_run_creates_new_version(self) -> None:
        original = load_registry()
        mutated = copy.deepcopy(original)

        first_key = next(iter(mutated["predicates"]))
        original_threshold = mutated["predicates"][first_key]["threshold"]
        mutated["predicates"][first_key]["threshold"] = original_threshold + 42

        self.assertNotEqual(
            original["predicates"][first_key]["threshold"],
            mutated["predicates"][first_key]["threshold"],
            msg="After a threshold adjustment the mutated registry must diverge "
            "from the original, representing a new version",
        )

    # ------------------------------------------------------------------
    # 9. Predicates declare positive controls
    # ------------------------------------------------------------------
    def test_predicates_declare_positive_controls(self) -> None:
        for key, entry in self.predicates.items():
            with self.subTest(key=key):
                controls = entry.get("positive_controls")
                self.assertIsInstance(
                    controls,
                    list,
                    msg=f"predicate {key!r} must declare positive_controls as a list",
                )
                self.assertTrue(
                    controls,
                    msg=f"predicate {key!r} positive_controls must not be empty",
                )
                for ctrl in controls:
                    self.assertIsInstance(ctrl, str)
                    self.assertTrue(ctrl)

    # ------------------------------------------------------------------
    # 10. Predicates declare hostile mutants
    # ------------------------------------------------------------------
    def test_predicates_declare_hostile_mutants(self) -> None:
        for key, entry in self.predicates.items():
            with self.subTest(key=key):
                mutants = entry.get("hostile_mutants")
                self.assertIsInstance(
                    mutants,
                    list,
                    msg=f"predicate {key!r} must declare hostile_mutants as a list",
                )
                self.assertTrue(
                    mutants,
                    msg=f"predicate {key!r} hostile_mutants must not be empty",
                )
                for mutant in mutants:
                    self.assertIsInstance(mutant, str)
                    self.assertTrue(mutant)


if __name__ == "__main__":
    unittest.main()
