#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import json
import pathlib
import re
import unittest

THIS_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = THIS_DIR.parents[1]
CATALOG_PATH = THIS_DIR / "fingerprint_mutation_catalog.json"

VALID_MUTATION_ID_RE = re.compile(r"^[a-z][a-z0-9]*(_[a-z0-9]+)*$")
VALID_FAILURE_LANE_RE = re.compile(
    r"^(contract|generator|matcher|nightly|renderer|route_policy|"
    r"reviewed_corpus_smoke|imported_corpus_smoke)(_[a-z0-9]+)*$"
)
VALID_MUTATION_CLASSES = frozenset(
    [
        "exact_field_downgrade",
        "grease_evasion",
        "grease_slot_drift",
        "length_mismatch",
        "order_drift",
        "capture_selection_drift",
        "plaintext_name_leak",
        "identity_alias",
        "missing_identity",
        "advisory_contamination",
        "route_policy_leak",
        "seed_inflation",
        "lineage_stale",
        "threshold_overfit",
    ]
)
REQUIRED_ENTRY_KEYS = frozenset(
    [
        "mutation_id",
        "risk_ids",
        "family_id",
        "cohort_id",
        "route_lane",
        "evidence_lane",
        "fixture_anchor",
        "raw_dump_anchor",
        "mutated_field",
        "mutation_class",
        "expected_failure_lane",
        "positive_control",
        "expected_diagnostic",
        "created_by_phase",
        "last_verified_utc",
    ]
)


def _load_catalog() -> dict:
    text = CATALOG_PATH.read_text(encoding="utf-8")
    return json.loads(text)


class FingerprintMutationAdequacyContractTest(unittest.TestCase):
    """Tests mutation catalog schema and mutation adequacy gates.

    Covers: RISK-FP-21 (test false negatives)
    """

    @classmethod
    def setUpClass(cls) -> None:
        cls.catalog = _load_catalog()
        cls.mutations = cls.catalog.get("mutations", [])

    # ------------------------------------------------------------------
    # 1. mutation_id format validation
    # ------------------------------------------------------------------
    def test_mutation_id_format_is_stable_snake_case(self) -> None:
        seen: set[str] = set()
        for entry in self.mutations:
            mid = entry["mutation_id"]
            self.assertRegex(
                mid,
                VALID_MUTATION_ID_RE,
                f"mutation_id '{mid}' is not stable snake_case",
            )
            self.assertNotIn(
                mid,
                seen,
                f"duplicate mutation_id '{mid}'",
            )
            seen.add(mid)

    # ------------------------------------------------------------------
    # 2. risk_ids non-empty
    # ------------------------------------------------------------------
    def test_risk_ids_non_empty_and_well_formed(self) -> None:
        risk_pattern = re.compile(r"^RISK-FP-\d+$")
        for entry in self.mutations:
            mid = entry["mutation_id"]
            risk_ids = entry["risk_ids"]
            self.assertIsInstance(risk_ids, list, f"{mid}: risk_ids must be a list")
            self.assertGreater(
                len(risk_ids), 0, f"{mid}: risk_ids must be non-empty"
            )
            for rid in risk_ids:
                self.assertRegex(
                    rid,
                    risk_pattern,
                    f"{mid}: malformed risk_id '{rid}'",
                )

    # ------------------------------------------------------------------
    # 3. fixture_anchor existence
    # ------------------------------------------------------------------
    def test_fixture_anchor_path_exists_in_repo(self) -> None:
        for entry in self.mutations:
            mid = entry["mutation_id"]
            anchor = entry["fixture_anchor"]
            self.assertIn("fixture_path", anchor, f"{mid}: missing fixture_path")
            fixture_path = REPO_ROOT / anchor["fixture_path"]
            self.assertTrue(
                fixture_path.exists(),
                f"{mid}: fixture_anchor path does not exist: {anchor['fixture_path']}",
            )
            self.assertIn("fixture_id", anchor, f"{mid}: missing fixture_id")
            self.assertIn("frame_number", anchor, f"{mid}: missing frame_number")
            self.assertIn("tcp_stream", anchor, f"{mid}: missing tcp_stream")

    # ------------------------------------------------------------------
    # 4. expected_failure_lane format
    # ------------------------------------------------------------------
    def test_expected_failure_lane_matches_known_lanes(self) -> None:
        for entry in self.mutations:
            mid = entry["mutation_id"]
            lane = entry["expected_failure_lane"]
            self.assertRegex(
                lane,
                VALID_FAILURE_LANE_RE,
                f"{mid}: expected_failure_lane '{lane}' does not match known lane pattern",
            )

    # ------------------------------------------------------------------
    # 5. positive control pass -- the positive_control fixture must exist
    # ------------------------------------------------------------------
    def test_positive_control_fixture_exists(self) -> None:
        for entry in self.mutations:
            mid = entry["mutation_id"]
            pc = entry["positive_control"]
            pc_path = REPO_ROOT / pc
            self.assertTrue(
                pc_path.exists(),
                f"{mid}: positive_control path does not exist: {pc}",
            )

    # ------------------------------------------------------------------
    # 6. controlled mutant fail -- mutation_class is from the documented
    #    vocabulary and expected_diagnostic is non-trivial
    # ------------------------------------------------------------------
    def test_controlled_mutant_has_valid_class_and_diagnostic(self) -> None:
        for entry in self.mutations:
            mid = entry["mutation_id"]
            mc = entry["mutation_class"]
            self.assertIn(
                mc,
                VALID_MUTATION_CLASSES,
                f"{mid}: mutation_class '{mc}' is not in the documented vocabulary",
            )
            diag = entry["expected_diagnostic"]
            self.assertIsInstance(diag, str, f"{mid}: expected_diagnostic must be str")
            self.assertGreater(
                len(diag),
                10,
                f"{mid}: expected_diagnostic is too short to be meaningful",
            )

    # ------------------------------------------------------------------
    # 7. no false-negatives -- every required entry key is present
    #    (schema completeness prevents silent omission of coverage fields)
    # ------------------------------------------------------------------
    def test_no_false_negatives_all_required_keys_present(self) -> None:
        for entry in self.mutations:
            mid = entry.get("mutation_id", "<unknown>")
            entry_keys = frozenset(entry.keys())
            missing = REQUIRED_ENTRY_KEYS - entry_keys
            self.assertFalse(
                missing,
                f"{mid}: missing required keys {sorted(missing)}",
            )

    # ------------------------------------------------------------------
    # 8. catalog sorted order -- mutations are sorted by mutation_id
    # ------------------------------------------------------------------
    def test_catalog_sorted_by_mutation_id(self) -> None:
        ids = [e["mutation_id"] for e in self.mutations]
        self.assertEqual(
            ids,
            sorted(ids),
            "mutations must be sorted by mutation_id in lexicographic order",
        )

    # ------------------------------------------------------------------
    # 9. catalog versioned -- schema_version and catalog_version present
    # ------------------------------------------------------------------
    def test_catalog_is_versioned(self) -> None:
        self.assertIn("schema_version", self.catalog)
        self.assertIsInstance(self.catalog["schema_version"], int)
        self.assertGreaterEqual(self.catalog["schema_version"], 1)

        self.assertIn("catalog_version", self.catalog)
        cv = self.catalog["catalog_version"]
        self.assertIsInstance(cv, str)
        self.assertGreater(len(cv), 0, "catalog_version must be non-empty")
        self.assertRegex(
            cv,
            r"^\d{4}-\d{2}-\d{2}",
            "catalog_version must start with a date prefix YYYY-MM-DD",
        )

    # ------------------------------------------------------------------
    # 10. catalog has at least one mutation entry
    # ------------------------------------------------------------------
    def test_catalog_has_minimum_mutation_count(self) -> None:
        self.assertGreaterEqual(
            len(self.mutations),
            3,
            "catalog must contain at least 3 mutation entries for meaningful adequacy",
        )

    # ------------------------------------------------------------------
    # 11. each mutation covers a distinct (family_id, mutated_field) pair
    #     to prevent redundant mutants hiding coverage gaps
    # ------------------------------------------------------------------
    def test_mutants_cover_distinct_family_field_pairs(self) -> None:
        seen: set[tuple[str, str]] = set()
        for entry in self.mutations:
            mid = entry["mutation_id"]
            pair = (entry["family_id"], entry["mutated_field"])
            self.assertNotIn(
                pair,
                seen,
                f"{mid}: duplicate (family_id, mutated_field) pair {pair}; "
                f"each controlled mutant should target a distinct combination",
            )
            seen.add(pair)


if __name__ == "__main__":
    unittest.main()
