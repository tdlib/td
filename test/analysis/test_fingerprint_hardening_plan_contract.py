#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import pathlib
import unittest

THIS_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = THIS_DIR.parents[1]
PLAN_PATH = (
    REPO_ROOT / "docs" / "Plans" / "FINGERPRINT_HARDENING_MASTER_PLAN_2026-05-24.md"
)
LEGACY_PLAN_PATH = (
    REPO_ROOT
    / "docs"
    / "Plans"
    / "FINGERPRINT_DOCUMENTATION_AND_HARDENING_PLAN_2026-04-25.md"
)
FINAL_AUDIT_PATH = (
    REPO_ROOT
    / "docs"
    / "Plans"
    / "FINGERPRINT_HARDENING_PLAN_FINAL_AUDIT_2026-04-25.md"
)
LEGACY_PIPELINE_PLAN_PATH = (
    REPO_ROOT
    / "docs"
    / "Plans"
    / "FINGERPRINT_TEST_AND_PIPELINE_HARDENING_PLAN_2026-05-09.md"
)
ARCHIVED_LEGACY_PLAN_PATH = (
    REPO_ROOT
    / "docs"
    / "Plans"
    / "Archived"
    / "FINGERPRINT_DOCUMENTATION_AND_HARDENING_PLAN_2026-04-25.md"
)
ARCHIVED_LEGACY_AUDIT_COMPLETE_PATH = (
    REPO_ROOT
    / "docs"
    / "Plans"
    / "Archived"
    / "FINGERPRINT_DOCUMENTATION_AND_HARDENING_PLAN_2026-04-25_AUDIT_COMPLETE.md"
)
ARCHIVED_LEGACY_FINAL_AUDIT_PATH = (
    REPO_ROOT
    / "docs"
    / "Plans"
    / "Archived"
    / "FINGERPRINT_HARDENING_PLAN_FINAL_AUDIT_2026-04-25.md"
)
OPS_GUIDE_PATH = (
    REPO_ROOT / "docs" / "Documentation" / "FINGERPRINT_OPERATIONS_GUIDE.md"
)
WORKFLOW_PATH = REPO_ROOT / ".github" / "workflows" / "fingerprint-policy-integrity.yml"
TRANSPORT_STATUS_PATH = (
    REPO_ROOT
    / "docs"
    / "Generated"
    / "FINGERPRINT_TRANSPORT_COHERENCE_STATUS.generated.json"
)


class FingerprintHardeningPlanContractTest(unittest.TestCase):
    def test_single_authoritative_plan_exists_and_legacy_docs_are_removed(self) -> None:
        self.assertTrue(PLAN_PATH.exists())
        self.assertFalse(LEGACY_PLAN_PATH.exists())
        self.assertFalse(FINAL_AUDIT_PATH.exists())
        self.assertFalse(LEGACY_PIPELINE_PLAN_PATH.exists())
        self.assertFalse(ARCHIVED_LEGACY_PLAN_PATH.exists())
        self.assertFalse(ARCHIVED_LEGACY_AUDIT_COMPLETE_PATH.exists())
        self.assertFalse(ARCHIVED_LEGACY_FINAL_AUDIT_PATH.exists())

    def test_master_plan_contains_required_sections_and_repository_reality(
        self,
    ) -> None:
        text = PLAN_PATH.read_text(encoding="utf-8")

        self.assertIn("# Fingerprint Hardening Master Plan", text)
        self.assertIn("## 1. Verified Repository Reality", text)
        self.assertIn("## 4. Statistical And Release Policy", text)
        self.assertIn("## 6. Agent Execution Phases", text)
        self.assertIn("## 7. Validation Matrix", text)
        self.assertIn("## 8. Release Blockers And Exit Criteria", text)
        self.assertIn("Real Fixture Anchor Selection Protocol", text)
        self.assertIn("Agent Red-Test Evidence Standard", text)
        self.assertIn("Cohort Identity And Release Predicate Scope", text)
        self.assertIn("Required Mutation Catalog Schema", text)
        self.assertIn("Contract -> Attack -> Red -> Green -> Survive -> Refactor", text)
        self.assertIn("SocratiCode", text)
        self.assertIn("safari26_3_1_ios26_3_1_a.clienthello.json", text)
        self.assertIn("chrome147_0_7727_47_ios26_4_a.clienthello.json", text)
        self.assertIn("docs/Samples/Traffic dumps/", text)
        self.assertIn("Artifact Dependency DAG", text)
        self.assertIn("Wilson 95% confidence intervals", text)
        self.assertIn("Missing metrics stay `unavailable`", text)
        self.assertIn(
            "top-level transport status remains `pending` while individual missing metrics remain `unavailable`",
            text,
        )
        self.assertIn("cluster-resampled bootstrap", text)
        self.assertIn("supported_versions", text)
        self.assertIn("Fixture Census And Provenance Audit", text)
        self.assertIn("artifact_path.stem", text)
        self.assertIn("source-kind vocabulary", text)
        self.assertIn(
            "Generated seeds are runtime stress inputs, not independent browser evidence.",
            text,
        )
        self.assertIn(
            "Changing `source_path` alone may not create a new independent source.",
            text,
        )
        self.assertIn(
            "Cluster-collapse repeated samples to the session or capture level before computing Wilson 95% confidence intervals.",
            text,
        )

    def test_master_plan_pins_real_fixture_census_and_modern_ios_scope(self) -> None:
        text = PLAN_PATH.read_text(encoding="utf-8")

        self.assertIn("134 raw dump files", text)
        self.assertIn("106 reviewed ClientHello artifacts", text)
        self.assertIn("Deduplicated authoritative capture identities", text)
        self.assertIn(
            "Independent reviewed sessions with non-empty `scenario_id`", text
        )
        self.assertIn("458 reviewed ClientHello samples with extension `0x002B`", text)
        self.assertIn("iOS 17.2 and iOS 18.7", text)
        self.assertIn("must not be pooled with modern iOS 26 Apple TLS", text)
        self.assertIn("`[0x0304, 0x0303]`", text)
        self.assertIn("`[0x0304, 0x0303, 0x0302, 0x0301]`", text)
        self.assertIn("generation-aware family classification", text)

    def test_master_plan_requires_agent_gate_artifacts_and_parser_ownership(
        self,
    ) -> None:
        text = PLAN_PATH.read_text(encoding="utf-8")

        self.assertIn("Agent Gate Artifact", text)
        self.assertIn(
            "docs/Plans/fingerprint-hardening-master-plan-2026-05-24/handoffs/", text
        )
        self.assertIn("canonical Python parser", text)
        self.assertIn("canonical C++ test helper", text)
        self.assertIn(
            "no agent may claim a parser fix by changing only one language", text
        )
        self.assertIn("Do not relax the first red test", text)

    def test_master_plan_requires_mutation_adequacy_and_raw_fixture_reproduction(
        self,
    ) -> None:
        text = PLAN_PATH.read_text(encoding="utf-8")

        self.assertIn("Fingerprint Mutation Adequacy", text)
        self.assertIn("controlled mutant", text)
        self.assertIn("mutation_id", text)
        self.assertIn("Raw-to-fixture differential reproduction", text)
        self.assertIn("byte-identical for release-critical extracted fields", text)
        self.assertIn("false-negative", text)
        self.assertIn("Unicode normalization", text)
        self.assertIn("mutation_class", text)
        self.assertIn("expected_diagnostic", text)
        self.assertIn("positive_control", text)
        self.assertIn(
            "Phase 0D. Mutant Sensitivity And Raw-Fixture Reproduction Gate", text
        )

    def test_master_plan_requires_cohort_scoped_release_evidence(self) -> None:
        text = PLAN_PATH.read_text(encoding="utf-8")

        self.assertIn(
            "Every release-facing predicate must carry an explicit `cohort_id`",
            text,
        )
        self.assertIn(
            "A release predicate is scoped to `(family_id, cohort_id, route_lane, evidence_lane, predicate_id)`",
            text,
        )
        self.assertIn(
            "Release predicates include `cohort_id`, mixed iOS generations are not pooled",
            text,
        )
        self.assertIn("RISK-FP-22", text)
        self.assertIn(
            "modern iOS 26 Apple TLS, legacy iOS 17.2 or iOS 18.7 Apple TLS, imported diagnostics, and runtime seed coverage cannot share one denominator",
            text,
        )

    def test_ops_guide_and_workflow_point_to_single_plan(self) -> None:
        ops_guide_text = OPS_GUIDE_PATH.read_text(encoding="utf-8")
        workflow_text = WORKFLOW_PATH.read_text(encoding="utf-8")

        self.assertIn(
            "docs/Plans/FINGERPRINT_HARDENING_MASTER_PLAN_2026-05-24.md",
            ops_guide_text,
        )
        self.assertNotIn(
            "docs/Plans/FINGERPRINT_DOCUMENTATION_AND_HARDENING_PLAN_2026-04-25.md",
            ops_guide_text,
        )
        self.assertIn(
            "docs/Plans/FINGERPRINT_HARDENING_MASTER_PLAN_2026-05-24.md",
            workflow_text,
        )
        self.assertNotIn(
            "docs/Plans/FINGERPRINT_DOCUMENTATION_AND_HARDENING_PLAN_2026-04-25.md",
            workflow_text,
        )
        self.assertNotIn(
            "docs/Plans/FINGERPRINT_HARDENING_PLAN_FINAL_AUDIT_2026-04-25.md",
            workflow_text,
        )
        self.assertNotIn(
            "docs/Plans/FINGERPRINT_TEST_AND_PIPELINE_HARDENING_PLAN_2026-05-09.md",
            workflow_text,
        )
        self.assertNotIn(
            "docs/Plans/FINGERPRINT_DOCUMENTATION_AND_HARDENING_PLAN_2026-04-25_AUDIT_COMPLETE.md",
            workflow_text,
        )

    def test_transport_status_contract_still_matches_fail_closed_language(self) -> None:
        transport_status_text = TRANSPORT_STATUS_PATH.read_text(encoding="utf-8")
        plan_text = PLAN_PATH.read_text(encoding="utf-8")

        self.assertIn('"ttl_bucket_match_rate": null', transport_status_text)
        self.assertIn(
            '"syn_option_order_class_match_rate": null', transport_status_text
        )
        self.assertIn('"availability": "unavailable"', transport_status_text)
        self.assertIn(
            '"first_flight_segmentation_signature_match_rate": 1.0',
            transport_status_text,
        )
        self.assertIn("Missing metrics stay `unavailable`", plan_text)
        self.assertIn(
            "The current transport thresholds remain `0.85` for Tier2 and `0.95` for Tier3",
            plan_text,
        )


if __name__ == "__main__":
    unittest.main()
