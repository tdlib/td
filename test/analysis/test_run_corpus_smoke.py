# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

import json
import pathlib
import sys
import tempfile
import unittest


THIS_DIR = pathlib.Path(__file__).resolve().parent
if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))

from common_tls import read_sha256
from run_corpus_smoke import run_corpus_smoke


def write_clienthello_artifact(
    artifact_path: pathlib.Path,
    *,
    profile_id: str,
    route_mode: str,
    source_path: pathlib.Path,
    source_sha256: str,
    extensions=None,
    cipher_suites=None,
    alpn_protocols=None,
):
    artifact = {
        "profile_id": profile_id,
        "route_mode": route_mode,
        "scenario_id": f"{profile_id}-scenario",
        "source_path": str(source_path),
        "source_sha256": source_sha256,
        "source_kind": "browser_capture",
        "fixture_family_id": f"{profile_id}_family",
        "tls_gen": "tls13",
        "device_class": "desktop",
        "os_family": "linux",
        "transport": "tcp",
        "samples": [
            {
                "cipher_suites": ["0x1301"] if cipher_suites is None else list(cipher_suites),
                "supported_groups": ["0x001d"],
                "key_share_entries": [{"group": "0x001d"}],
                "non_grease_extensions_without_padding": [],
                "alpn_protocols": ["h2"] if alpn_protocols is None else list(alpn_protocols),
                "extensions": [] if extensions is None else list(extensions),
            }
        ],
    }
    artifact_path.write_text(json.dumps(artifact), encoding="utf-8")


def write_serverhello_artifact(
    artifact_path: pathlib.Path,
    *,
    family: str,
    route_mode: str,
    cipher_suite: str = "0x1301",
    layout_signature=None,
    source_path: str = "/captures/serverhello.pcapng",
    source_sha256: str = "serverhello-sha256",
):
    artifact = {
        "route_mode": route_mode,
        "scenario_id": f"{family}-serverhello-scenario",
        "source_path": source_path,
        "source_sha256": source_sha256,
        "parser_version": "tls-serverhello-parser-v1",
        "samples": [
            {
                "fixture_id": f"{family}:frame8",
                "family": family,
                "selected_version": "0x0304",
                "cipher_suite": cipher_suite,
                "extensions": ["0x002b", "0x0033"],
                "record_layout_signature": [22, 20] if layout_signature is None else list(layout_signature),
            }
        ],
    }
    artifact_path.write_text(json.dumps(artifact), encoding="utf-8")


def write_ipt_artifact(artifact_path: pathlib.Path, *, active_policy: str = "non_ru_egress", baseline_distance: float = 0.05):
    artifact = {
        "active_policy": active_policy,
        "quic_enabled": False,
        "runs": [
            {
                "lognormal_fit_p_value": 0.25,
                "keepalive_bypass_p99_ms": 5.0,
                "baseline_distance": baseline_distance,
                "active_intervals_ms": [80.0, 95.0, 110.0],
            }
        ],
    }
    artifact_path.write_text(json.dumps(artifact), encoding="utf-8")


def write_drs_artifact(artifact_path: pathlib.Path, *, active_policy: str = "non_ru_egress", baseline_distance: float = 0.05):
    artifact = {
        "active_policy": active_policy,
        "quic_enabled": False,
        "record_payload_sizes": [1200, 1400, 1800, 2200],
        "long_flow_windows": [
            {
                "total_payload_bytes": 64000,
                "max_record_payload_size": 12000,
            }
        ],
        "baseline_distance": baseline_distance,
    }
    artifact_path.write_text(json.dumps(artifact), encoding="utf-8")


def write_flow_artifact(artifact_path: pathlib.Path, *, active_policy: str = "non_ru_egress", destination: str = "1.1.1.1:443|a.example"):
    artifact = {
        "active_policy": active_policy,
        "quic_enabled": False,
        "connections": [
            {"destination": destination, "started_at_ms": 0, "ended_at_ms": 5000, "reused": False, "bytes_sent": 4096},
            {"destination": destination, "started_at_ms": 5000, "ended_at_ms": 10000, "reused": True, "bytes_sent": 4096},
            {"destination": "2.2.2.2:443|b.example", "started_at_ms": 12000, "ended_at_ms": 18000, "reused": True, "bytes_sent": 4096},
        ],
    }
    artifact_path.write_text(json.dumps(artifact), encoding="utf-8")


def make_registry(capture_path: pathlib.Path) -> dict:
    return {
        "contamination_guard": {
            "fail_on_missing_required_tag": True,
            "allow_mixed_source_kind_per_profile": False,
            "allow_mixed_family_per_profile": False,
            "allow_advisory_code_sample_per_profile": False,
        },
        "server_hello_matrix": {
            "ChromeGood_family": {
                "parser_version": "tls-serverhello-parser-v1",
                "allowed_tuples": [
                    {
                        "selected_version": "0x0304",
                        "cipher_suite": "0x1301",
                        "extensions": ["0x002b", "0x0033"],
                    }
                ],
                "allowed_layout_signatures": [[22], [22, 20]],
            }
        },
        "fixtures": {
            "fx_chrome_good": {
                "source_kind": "browser_capture",
                "trust_tier": "verified",
                "family": "ChromeGood_family",
                "transport": "tcp",
                "platform_class": "desktop",
                "tls_gen": "tls13",
                "source_path": str(capture_path),
                "source_sha256": read_sha256(capture_path),
            },
            "fx_chrome_bad": {
                "source_kind": "browser_capture",
                "trust_tier": "verified",
                "family": "ChromeBad_family",
                "transport": "tcp",
                "platform_class": "desktop",
                "tls_gen": "tls13",
                "source_path": str(capture_path),
                "source_sha256": read_sha256(capture_path),
            },
        },
        "profiles": {
            "ChromeGood": {
                "release_gating": False,
                "include_fixture_ids": ["fx_chrome_good"],
                "ech_type": None,
            },
            "ChromeBad": {
                "release_gating": False,
                "include_fixture_ids": ["fx_chrome_bad"],
                "ech_type": "0xFE0D",
            },
        },
    }


class RunCorpusSmokeTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temp_dir = tempfile.TemporaryDirectory()
        self.base_dir = pathlib.Path(self.temp_dir.name)
        self.capture_path = self.base_dir / "capture.pcapng"
        self.capture_path.write_text("capture-bytes\n", encoding="utf-8")
        self.fixtures_root = self.base_dir / "fixtures" / "clienthello"
        (self.fixtures_root / "linux_desktop").mkdir(parents=True)
        self.serverhello_root = self.base_dir / "fixtures" / "serverhello"
        self.serverhello_root.mkdir(parents=True)
        self.ipt_root = self.base_dir / "fixtures" / "ipt"
        self.ipt_root.mkdir(parents=True)
        self.drs_root = self.base_dir / "fixtures" / "drs"
        self.drs_root.mkdir(parents=True)
        self.flow_root = self.base_dir / "fixtures" / "flow"
        self.flow_root.mkdir(parents=True)
        self.registry_path = self.base_dir / "profiles_validation.json"

    def tearDown(self) -> None:
        self.temp_dir.cleanup()

    def test_runs_registry_and_fingerprint_checks_over_fixture_tree(self) -> None:
        registry = make_registry(self.capture_path)
        self.registry_path.write_text(json.dumps(registry), encoding="utf-8")
        write_clienthello_artifact(
            self.fixtures_root / "linux_desktop" / "chrome_good.clienthello.json",
            profile_id="ChromeGood",
            route_mode="non_ru_egress",
            source_path=self.capture_path,
            source_sha256=read_sha256(self.capture_path),
        )

        report = run_corpus_smoke(self.registry_path, self.fixtures_root)

        self.assertTrue(report["ok"])
        self.assertEqual([], report["registry_failures"])
        self.assertEqual(1, report["artifact_count"])
        self.assertEqual(1, report["sample_count"])
        self.assertEqual(1, report["telemetry"]["profiles"]["ChromeGood"]["sample_count"])

    def test_reports_artifact_scoped_fingerprint_failures(self) -> None:
        registry = make_registry(self.capture_path)
        self.registry_path.write_text(json.dumps(registry), encoding="utf-8")
        artifact_path = self.fixtures_root / "linux_desktop" / "chrome_bad.clienthello.json"
        write_clienthello_artifact(
            artifact_path,
            profile_id="ChromeBad",
            route_mode="non_ru_egress",
            source_path=self.capture_path,
            source_sha256=read_sha256(self.capture_path),
        )

        report = run_corpus_smoke(self.registry_path, self.fixtures_root)

        self.assertFalse(report["ok"])
        self.assertEqual([], report["registry_failures"])
        self.assertEqual(["sample[0]: ECH route policy"], report["artifacts"][0]["failures"])
        self.assertEqual(str(artifact_path), report["artifacts"][0]["artifact_path"])

    def test_reports_registry_failures_alongside_artifact_summary(self) -> None:
        registry = make_registry(self.capture_path)
        del registry["fixtures"]["fx_chrome_good"]["transport"]
        self.registry_path.write_text(json.dumps(registry), encoding="utf-8")
        write_clienthello_artifact(
            self.fixtures_root / "linux_desktop" / "chrome_good.clienthello.json",
            profile_id="ChromeGood",
            route_mode="non_ru_egress",
            source_path=self.capture_path,
            source_sha256=read_sha256(self.capture_path),
        )

        report = run_corpus_smoke(self.registry_path, self.fixtures_root)

        self.assertFalse(report["ok"])
        self.assertIn("fixture fx_chrome_good is missing required tag transport", report["registry_failures"])
        self.assertEqual(1, report["artifact_count"])

    def test_fails_closed_when_fixture_tree_is_empty(self) -> None:
        registry = make_registry(self.capture_path)
        self.registry_path.write_text(json.dumps(registry), encoding="utf-8")

        report = run_corpus_smoke(self.registry_path, self.fixtures_root)

        self.assertFalse(report["ok"])
        self.assertEqual([], report["registry_failures"])
        self.assertEqual(0, report["artifact_count"])
        self.assertIn("artifacts: no clienthello artifacts found under", report["failures"][0])

    def test_fails_closed_when_artifact_has_no_samples(self) -> None:
        registry = make_registry(self.capture_path)
        self.registry_path.write_text(json.dumps(registry), encoding="utf-8")

        artifact_path = self.fixtures_root / "linux_desktop" / "chrome_good_empty.clienthello.json"
        artifact = {
            "profile_id": "ChromeGood",
            "route_mode": "non_ru_egress",
            "scenario_id": "ChromeGood-empty-scenario",
            "source_path": str(self.capture_path),
            "source_sha256": read_sha256(self.capture_path),
            "source_kind": "browser_capture",
            "fixture_family_id": "ChromeGood_family",
            "tls_gen": "tls13",
            "device_class": "desktop",
            "os_family": "linux",
            "transport": "tcp",
            "samples": [],
        }
        artifact_path.write_text(json.dumps(artifact), encoding="utf-8")

        report = run_corpus_smoke(self.registry_path, self.fixtures_root)

        self.assertFalse(report["ok"])
        self.assertEqual(1, report["artifact_count"])
        self.assertEqual(0, report["sample_count"])
        self.assertEqual(["artifact-load: artifact must contain at least one sample"], report["artifacts"][0]["failures"])
        self.assertIn(
            f"artifact[{artifact_path}]: artifact-load: artifact must contain at least one sample",
            report["failures"],
        )

    def test_keeps_valid_artifact_telemetry_when_sibling_artifact_is_malformed(self) -> None:
        registry = make_registry(self.capture_path)
        self.registry_path.write_text(json.dumps(registry), encoding="utf-8")

        good_artifact_path = self.fixtures_root / "linux_desktop" / "chrome_good.clienthello.json"
        write_clienthello_artifact(
            good_artifact_path,
            profile_id="ChromeGood",
            route_mode="non_ru_egress",
            source_path=self.capture_path,
            source_sha256=read_sha256(self.capture_path),
        )
        bad_artifact_path = self.fixtures_root / "linux_desktop" / "broken.clienthello.json"
        bad_artifact_path.write_text("{not-json}\n", encoding="utf-8")

        report = run_corpus_smoke(self.registry_path, self.fixtures_root)

        self.assertFalse(report["ok"])
        self.assertEqual(2, report["artifact_count"])
        self.assertEqual(1, report["sample_count"])
        self.assertEqual(1, report["telemetry"]["profiles"]["ChromeGood"]["sample_count"])
        artifact_reports = {artifact["artifact_path"]: artifact for artifact in report["artifacts"]}
        self.assertTrue(artifact_reports[str(good_artifact_path)]["ok"])
        self.assertEqual(artifact_reports[str(good_artifact_path)]["failures"], [])
        self.assertEqual(artifact_reports[str(bad_artifact_path)]["ok"], False)
        self.assertEqual(1, len(artifact_reports[str(bad_artifact_path)]["failures"]))
        self.assertTrue(artifact_reports[str(bad_artifact_path)]["failures"][0].startswith("artifact-load: "))

    def test_reports_server_hello_matrix_failures_when_response_corpus_is_provided(self) -> None:
        registry = make_registry(self.capture_path)
        self.registry_path.write_text(json.dumps(registry), encoding="utf-8")

        write_clienthello_artifact(
            self.fixtures_root / "linux_desktop" / "chrome_good.clienthello.json",
            profile_id="ChromeGood",
            route_mode="non_ru_egress",
            source_path=self.capture_path,
            source_sha256=read_sha256(self.capture_path),
        )
        serverhello_artifact_path = self.serverhello_root / "chrome_good.serverhello.json"
        write_serverhello_artifact(
            serverhello_artifact_path,
            family="ChromeGood_family",
            route_mode="non_ru_egress",
            cipher_suite="0x1302",
            source_path=str(self.capture_path),
            source_sha256=read_sha256(self.capture_path),
        )

        report = run_corpus_smoke(
            self.registry_path,
            self.fixtures_root,
            server_hello_fixtures_root=self.serverhello_root,
        )

        self.assertFalse(report["ok"])
        self.assertEqual(1, report["server_hello_artifact_count"])
        self.assertEqual(1, report["server_hello_sample_count"])
        self.assertEqual(
            ["sample[0]: ServerHello tuple not allowed for family ChromeGood_family"],
            report["server_hello_artifacts"][0]["failures"],
        )
        self.assertIn(
            f"serverhello[{serverhello_artifact_path}]: sample[0]: ServerHello tuple not allowed for family ChromeGood_family",
            report["failures"],
        )

    def test_reports_duplicate_server_hello_fixture_reuse_when_response_corpus_is_provided(self) -> None:
        registry = make_registry(self.capture_path)
        self.registry_path.write_text(json.dumps(registry), encoding="utf-8")

        write_clienthello_artifact(
            self.fixtures_root / "linux_desktop" / "chrome_good.clienthello.json",
            profile_id="ChromeGood",
            route_mode="non_ru_egress",
            source_path=self.capture_path,
            source_sha256=read_sha256(self.capture_path),
        )
        serverhello_artifact_path = self.serverhello_root / "chrome_good_duplicate.serverhello.json"
        duplicate_artifact = {
            "route_mode": "non_ru_egress",
            "scenario_id": "ChromeGood_family-serverhello-scenario",
            "source_path": str(self.capture_path),
            "source_sha256": read_sha256(self.capture_path),
            "parser_version": "tls-serverhello-parser-v1",
            "samples": [
                {
                    "fixture_id": "ChromeGood_family:frame8",
                    "family": "ChromeGood_family",
                    "selected_version": "0x0304",
                    "cipher_suite": "0x1301",
                    "extensions": ["0x002b", "0x0033"],
                    "record_layout_signature": [22, 20],
                },
                {
                    "fixture_id": "ChromeGood_family:frame8",
                    "family": "ChromeGood_family",
                    "selected_version": "0x0304",
                    "cipher_suite": "0x1301",
                    "extensions": ["0x002b", "0x0033"],
                    "record_layout_signature": [22, 20],
                },
            ],
        }
        serverhello_artifact_path.write_text(json.dumps(duplicate_artifact), encoding="utf-8")

        report = run_corpus_smoke(
            self.registry_path,
            self.fixtures_root,
            server_hello_fixtures_root=self.serverhello_root,
        )

        self.assertFalse(report["ok"])
        self.assertEqual(1, report["server_hello_artifact_count"])
        self.assertEqual(2, report["server_hello_sample_count"])
        self.assertEqual(
            ["sample[1]: duplicate fixture_id ChromeGood_family:frame8"],
            report["server_hello_artifacts"][0]["failures"],
        )
        self.assertIn(
            f"serverhello[{serverhello_artifact_path}]: sample[1]: duplicate fixture_id ChromeGood_family:frame8",
            report["failures"],
        )

    def test_reports_server_hello_family_mismatch_against_matching_clienthello_capture(self) -> None:
        registry = make_registry(self.capture_path)
        registry["server_hello_matrix"]["Other_family"] = {
            "parser_version": "tls-serverhello-parser-v1",
            "allowed_tuples": [
                {
                    "selected_version": "0x0304",
                    "cipher_suite": "0x1301",
                    "extensions": ["0x002b", "0x0033"],
                }
            ],
            "allowed_layout_signatures": [[22, 20]],
        }
        self.registry_path.write_text(json.dumps(registry), encoding="utf-8")

        write_clienthello_artifact(
            self.fixtures_root / "linux_desktop" / "chrome_good.clienthello.json",
            profile_id="ChromeGood",
            route_mode="non_ru_egress",
            source_path=self.capture_path,
            source_sha256=read_sha256(self.capture_path),
        )
        serverhello_artifact_path = self.serverhello_root / "chrome_wrong_family.serverhello.json"
        write_serverhello_artifact(
            serverhello_artifact_path,
            family="Other_family",
            route_mode="non_ru_egress",
            source_path=str(self.capture_path),
            source_sha256=read_sha256(self.capture_path),
        )

        report = run_corpus_smoke(
            self.registry_path,
            self.fixtures_root,
            server_hello_fixtures_root=self.serverhello_root,
        )

        self.assertFalse(report["ok"])
        self.assertEqual(
            [
                "batch: ServerHello family Other_family does not match ClientHello families [ChromeGood_family]"
            ],
            report["server_hello_artifacts"][0]["failures"],
        )
        self.assertIn(
            f"serverhello[{serverhello_artifact_path}]: batch: ServerHello family Other_family does not match ClientHello families [ChromeGood_family]",
            report["failures"],
        )

    def test_reports_server_hello_capture_without_matching_clienthello_metadata(self) -> None:
        registry = make_registry(self.capture_path)
        self.registry_path.write_text(json.dumps(registry), encoding="utf-8")

        write_clienthello_artifact(
            self.fixtures_root / "linux_desktop" / "chrome_good.clienthello.json",
            profile_id="ChromeGood",
            route_mode="non_ru_egress",
            source_path=self.capture_path,
            source_sha256=read_sha256(self.capture_path),
        )
        serverhello_artifact_path = self.serverhello_root / "chrome_missing_clienthello.serverhello.json"
        artifact = {
            "route_mode": "non_ru_egress",
            "scenario_id": "ChromeGood_family-serverhello-scenario",
            "source_path": "/captures/other-serverhello.pcapng",
            "source_sha256": "other-serverhello-sha256",
            "parser_version": "tls-serverhello-parser-v1",
            "samples": [
                {
                    "fixture_id": "ChromeGood_family:frame8",
                    "family": "ChromeGood_family",
                    "selected_version": "0x0304",
                    "cipher_suite": "0x1301",
                    "extensions": ["0x002b", "0x0033"],
                    "record_layout_signature": [22, 20],
                }
            ],
        }
        serverhello_artifact_path.write_text(json.dumps(artifact), encoding="utf-8")

        report = run_corpus_smoke(
            self.registry_path,
            self.fixtures_root,
            server_hello_fixtures_root=self.serverhello_root,
        )

        self.assertFalse(report["ok"])
        self.assertEqual(
            ["batch: no matching ClientHello capture metadata for ServerHello artifact"],
            report["server_hello_artifacts"][0]["failures"],
        )
        self.assertIn(
            f"serverhello[{serverhello_artifact_path}]: batch: no matching ClientHello capture metadata for ServerHello artifact",
            report["failures"],
        )

    def test_reports_server_hello_source_sha256_mismatch_for_matching_source_path(self) -> None:
        registry = make_registry(self.capture_path)
        self.registry_path.write_text(json.dumps(registry), encoding="utf-8")

        expected_sha256 = read_sha256(self.capture_path)
        write_clienthello_artifact(
            self.fixtures_root / "linux_desktop" / "chrome_good.clienthello.json",
            profile_id="ChromeGood",
            route_mode="non_ru_egress",
            source_path=self.capture_path,
            source_sha256=expected_sha256,
        )
        serverhello_artifact_path = self.serverhello_root / "chrome_wrong_sha.serverhello.json"
        write_serverhello_artifact(
            serverhello_artifact_path,
            family="ChromeGood_family",
            route_mode="non_ru_egress",
            source_path=str(self.capture_path),
            source_sha256="different-serverhello-sha256",
        )

        report = run_corpus_smoke(
            self.registry_path,
            self.fixtures_root,
            server_hello_fixtures_root=self.serverhello_root,
        )

        self.assertFalse(report["ok"])
        self.assertEqual(
            [
                f"batch: ServerHello source_sha256 different-serverhello-sha256 does not match ClientHello capture metadata for source_path {self.capture_path}; expected one of [{expected_sha256}]"
            ],
            report["server_hello_artifacts"][0]["failures"],
        )
        self.assertIn(
            f"serverhello[{serverhello_artifact_path}]: batch: ServerHello source_sha256 different-serverhello-sha256 does not match ClientHello capture metadata for source_path {self.capture_path}; expected one of [{expected_sha256}]",
            report["failures"],
        )

    def test_runs_ipt_drs_and_flow_checks_when_auxiliary_corpora_are_provided(self) -> None:
        registry = make_registry(self.capture_path)
        self.registry_path.write_text(json.dumps(registry), encoding="utf-8")

        write_clienthello_artifact(
            self.fixtures_root / "linux_desktop" / "chrome_good.clienthello.json",
            profile_id="ChromeGood",
            route_mode="non_ru_egress",
            source_path=self.capture_path,
            source_sha256=read_sha256(self.capture_path),
        )
        write_ipt_artifact(self.ipt_root / "chrome_good.ipt.json")
        write_drs_artifact(self.drs_root / "chrome_good.drs.json")
        write_flow_artifact(self.flow_root / "chrome_good.flow.json")

        report = run_corpus_smoke(
            self.registry_path,
            self.fixtures_root,
            ipt_fixtures_root=self.ipt_root,
            drs_fixtures_root=self.drs_root,
            flow_fixtures_root=self.flow_root,
        )

        self.assertTrue(report["ok"])
        self.assertEqual(1, report["ipt_artifact_count"])
        self.assertEqual(1, report["drs_artifact_count"])
        self.assertEqual(1, report["flow_artifact_count"])
        self.assertTrue(report["ipt_artifacts"][0]["ok"])
        self.assertTrue(report["drs_artifacts"][0]["ok"])
        self.assertTrue(report["flow_artifacts"][0]["ok"])

    def test_reports_auxiliary_smoke_failures_when_provided(self) -> None:
        registry = make_registry(self.capture_path)
        self.registry_path.write_text(json.dumps(registry), encoding="utf-8")

        write_clienthello_artifact(
            self.fixtures_root / "linux_desktop" / "chrome_good.clienthello.json",
            profile_id="ChromeGood",
            route_mode="non_ru_egress",
            source_path=self.capture_path,
            source_sha256=read_sha256(self.capture_path),
        )
        ipt_artifact_path = self.ipt_root / "chrome_bad.ipt.json"
        drs_artifact_path = self.drs_root / "chrome_bad.drs.json"
        flow_artifact_path = self.flow_root / "chrome_bad.flow.json"
        write_ipt_artifact(ipt_artifact_path, baseline_distance=0.2)
        write_drs_artifact(drs_artifact_path, baseline_distance=0.2)
        write_flow_artifact(flow_artifact_path, destination="1.1.1.1:443")

        report = run_corpus_smoke(
            self.registry_path,
            self.fixtures_root,
            ipt_fixtures_root=self.ipt_root,
            drs_fixtures_root=self.drs_root,
            flow_fixtures_root=self.flow_root,
        )

        self.assertFalse(report["ok"])
        self.assertEqual(["baseline-distance"], report["ipt_artifacts"][0]["failures"])
        self.assertEqual(["histogram-distance"], report["drs_artifacts"][0]["failures"])
        self.assertEqual(["connection-schema"], report["flow_artifacts"][0]["failures"])
        self.assertIn(f"ipt[{ipt_artifact_path}]: baseline-distance", report["failures"])
        self.assertIn(f"drs[{drs_artifact_path}]: histogram-distance", report["failures"])
        self.assertIn(f"flow[{flow_artifact_path}]: connection-schema", report["failures"])

    def test_reports_adversarial_auxiliary_smoke_failures_when_provided(self) -> None:
        registry = make_registry(self.capture_path)
        self.registry_path.write_text(json.dumps(registry), encoding="utf-8")

        write_clienthello_artifact(
            self.fixtures_root / "linux_desktop" / "chrome_good.clienthello.json",
            profile_id="ChromeGood",
            route_mode="non_ru_egress",
            source_path=self.capture_path,
            source_sha256=read_sha256(self.capture_path),
        )
        ipt_artifact_path = self.ipt_root / "chrome_replayed.ipt.json"
        drs_artifact_path = self.drs_root / "chrome_oscillating.drs.json"
        flow_artifact_path = self.flow_root / "chrome_rotating_domain.flow.json"
        ipt_artifact_path.write_text(
            json.dumps(
                {
                    "active_policy": "non_ru_egress",
                    "quic_enabled": False,
                    "runs": [
                        {
                            "lognormal_fit_p_value": 0.40,
                            "keepalive_bypass_p99_ms": 2.0,
                            "baseline_distance": 0.04,
                            "active_intervals_ms": [10, 20, 40],
                        },
                        {
                            "lognormal_fit_p_value": 0.45,
                            "keepalive_bypass_p99_ms": 2.5,
                            "baseline_distance": 0.05,
                            "active_intervals_ms": [10, 20, 40],
                        },
                        {
                            "lognormal_fit_p_value": 0.50,
                            "keepalive_bypass_p99_ms": 3.0,
                            "baseline_distance": 0.05,
                            "active_intervals_ms": [10, 20, 40],
                        },
                    ],
                }
            ),
            encoding="utf-8",
        )
        drs_artifact_path.write_text(
            json.dumps(
                {
                    "active_policy": "non_ru_egress",
                    "quic_enabled": False,
                    "record_payload_sizes": [1400, 4096] * 8,
                    "baseline_distance": 0.05,
                    "long_flow_windows": [],
                }
            ),
            encoding="utf-8",
        )
        flow_artifact_path.write_text(
            json.dumps(
                {
                    "active_policy": "non_ru_egress",
                    "quic_enabled": False,
                    "connections": [
                        {
                            "destination": f"1.1.1.1:443|domain{index}.example",
                            "started_at_ms": index * 1000,
                            "ended_at_ms": index * 1000 + 1600,
                            "reused": False,
                            "bytes_sent": 4096,
                        }
                        for index in range(7)
                    ],
                }
            ),
            encoding="utf-8",
        )

        report = run_corpus_smoke(
            self.registry_path,
            self.fixtures_root,
            ipt_fixtures_root=self.ipt_root,
            drs_fixtures_root=self.drs_root,
            flow_fixtures_root=self.flow_root,
        )

        self.assertFalse(report["ok"])
        self.assertEqual(["replayed-active-intervals"], report["ipt_artifacts"][0]["failures"])
        self.assertEqual(["two-size-oscillation"], report["drs_artifacts"][0]["failures"])
        self.assertIn("reconnect-storm", report["flow_artifacts"][0]["failures"])
        self.assertIn(f"ipt[{ipt_artifact_path}]: replayed-active-intervals", report["failures"])
        self.assertIn(f"drs[{drs_artifact_path}]: two-size-oscillation", report["failures"])
        self.assertIn(f"flow[{flow_artifact_path}]: reconnect-storm", report["failures"])


if __name__ == "__main__":
    unittest.main()