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

from check_server_hello_matrix import check_server_hello_matrix, compute_server_hello_key
from common_tls import load_server_hello_artifact


def make_registry() -> dict:
    return {
        "server_hello_matrix": {
            "chromium_44cd_mlkem_linux_desktop": {
                "parser_version": "tls-serverhello-parser-v1",
                "allowed_tuples": [
                    {
                        "selected_version": "0x0304",
                        "cipher_suite": "0x1301",
                        "extensions": ["0x002b", "0x0033"],
                    }
                ],
                "allowed_layout_signatures": [[22], [22, 20]],
            },
            "chromium_4469_mlkem_linux_desktop": {
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
        }
    }


def make_server_hello_artifact(
    *,
    layout_signature=None,
    parser_version="tls-serverhello-parser-v1",
    route_mode: str = "non_ru_egress",
    source_kind: str = "browser_capture",
    scenario_id: str = "chrome133_serverhello",
    source_path: str = "/captures/chrome133_serverhello.pcapng",
    source_sha256: str = "artifact-sha256",
    family: str = "chromium_44cd_mlkem_linux_desktop",
) -> dict:
    return {
        "route_mode": route_mode,
        "scenario_id": scenario_id,
        "source_path": source_path,
        "source_sha256": source_sha256,
        "parser_version": parser_version,
        "source_kind": source_kind,
        "samples": [
            {
                "fixture_id": "chrome133_serverhello:frame8",
                "family": family,
                "selected_version": "0x0304",
                "cipher_suite": "0x1301",
                "extensions": ["0x002b", "0x0033"],
                "record_layout_signature": [22, 20] if layout_signature is None else list(layout_signature),
            }
        ],
    }


class LoadServerHelloArtifactTest(unittest.TestCase):
    def test_preserves_family_parser_and_layout_metadata(self) -> None:
        artifact = make_server_hello_artifact(layout_signature=[22, 20, 23])

        with tempfile.TemporaryDirectory() as temp_dir:
            artifact_path = pathlib.Path(temp_dir) / "serverhello.json"
            artifact_path.write_text(json.dumps(artifact), encoding="utf-8")

            samples = load_server_hello_artifact(artifact_path)

        self.assertEqual(1, len(samples))
        self.assertEqual("chromium_44cd_mlkem_linux_desktop", samples[0].metadata.fixture_family_id)
        self.assertEqual("tls-serverhello-parser-v1", samples[0].metadata.parser_version)
        self.assertEqual([22, 20, 23], samples[0].record_layout_signature)


class CheckServerHelloMatrixTest(unittest.TestCase):
    def test_accepts_allowed_tuple_and_layout(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            artifact_path = pathlib.Path(temp_dir) / "serverhello.json"
            artifact_path.write_text(json.dumps(make_server_hello_artifact()), encoding="utf-8")
            samples = load_server_hello_artifact(artifact_path)

        ok, failures = check_server_hello_matrix(samples, make_registry())

        self.assertTrue(ok)
        self.assertEqual([], failures)
        self.assertEqual((0x0304, 0x1301, (0x002B, 0x0033)), compute_server_hello_key(samples[0]))

    def test_rejects_unknown_tuple(self) -> None:
        artifact = make_server_hello_artifact()
        artifact["samples"][0]["cipher_suite"] = "0x1302"

        with tempfile.TemporaryDirectory() as temp_dir:
            artifact_path = pathlib.Path(temp_dir) / "serverhello.json"
            artifact_path.write_text(json.dumps(artifact), encoding="utf-8")
            samples = load_server_hello_artifact(artifact_path)

        ok, failures = check_server_hello_matrix(samples, make_registry())

        self.assertFalse(ok)
        self.assertIn("sample[0]: ServerHello tuple not allowed for family chromium_44cd_mlkem_linux_desktop", failures)

    def test_rejects_parser_version_drift(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            artifact_path = pathlib.Path(temp_dir) / "serverhello.json"
            artifact_path.write_text(
                json.dumps(make_server_hello_artifact(parser_version="tls-serverhello-parser-v2")),
                encoding="utf-8",
            )
            samples = load_server_hello_artifact(artifact_path)

        ok, failures = check_server_hello_matrix(samples, make_registry())

        self.assertFalse(ok)
        self.assertIn("sample[0]: parser version mismatch for family chromium_44cd_mlkem_linux_desktop", failures)

    def test_rejects_duplicate_fixture_id_reuse_within_batch(self) -> None:
        artifact = make_server_hello_artifact()
        artifact["samples"] = [
            dict(artifact["samples"][0], fixture_id="chrome133_serverhello:frame8"),
            dict(artifact["samples"][0], fixture_id="chrome133_serverhello:frame8"),
        ]

        with tempfile.TemporaryDirectory() as temp_dir:
            artifact_path = pathlib.Path(temp_dir) / "serverhello-duplicate-fixture-id.json"
            artifact_path.write_text(json.dumps(artifact), encoding="utf-8")
            samples = load_server_hello_artifact(artifact_path)

        ok, failures = check_server_hello_matrix(samples, make_registry())

        self.assertFalse(ok)
        self.assertIn("sample[1]: duplicate fixture_id chrome133_serverhello:frame8", failures)

    def test_rejects_fixed_synthetic_layout_collapse(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            artifact_path = pathlib.Path(temp_dir) / "serverhello.json"
            artifact = make_server_hello_artifact(layout_signature=[22])
            artifact["samples"] = [dict(artifact["samples"][0], fixture_id=f"chrome133_serverhello:frame{index}") for index in range(60)]
            artifact_path.write_text(json.dumps(artifact), encoding="utf-8")
            samples = load_server_hello_artifact(artifact_path)

        ok, failures = check_server_hello_matrix(samples, make_registry())

        self.assertFalse(ok)
        self.assertIn("batch: synthetic ServerHello layout collapse", failures)

    def test_rejects_mixed_route_mode_batch(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            non_ru_path = pathlib.Path(temp_dir) / "serverhello-non-ru.json"
            ru_path = pathlib.Path(temp_dir) / "serverhello-ru.json"
            non_ru_path.write_text(json.dumps(make_server_hello_artifact(route_mode="non_ru_egress")), encoding="utf-8")
            ru_path.write_text(json.dumps(make_server_hello_artifact(route_mode="ru_egress")), encoding="utf-8")

            samples = load_server_hello_artifact(non_ru_path) + load_server_hello_artifact(ru_path)

        ok, failures = check_server_hello_matrix(samples, make_registry())

        self.assertFalse(ok)
        self.assertIn("batch: mixed route_mode values", failures)

    def test_rejects_non_network_server_hello_source_kind(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            artifact_path = pathlib.Path(temp_dir) / "serverhello-advisory.json"
            artifact_path.write_text(
                json.dumps(make_server_hello_artifact(source_kind="advisory_code_sample")),
                encoding="utf-8",
            )
            samples = load_server_hello_artifact(artifact_path)

        ok, failures = check_server_hello_matrix(samples, make_registry())

        self.assertFalse(ok)
        self.assertIn("sample[0]: non-authoritative ServerHello source_kind advisory_code_sample", failures)

    def test_rejects_mixed_scenario_id_batch(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            first_path = pathlib.Path(temp_dir) / "serverhello-scenario-a.json"
            second_path = pathlib.Path(temp_dir) / "serverhello-scenario-b.json"
            first_path.write_text(
                json.dumps(make_server_hello_artifact(scenario_id="chrome133_serverhello_a")),
                encoding="utf-8",
            )
            second_path.write_text(
                json.dumps(make_server_hello_artifact(scenario_id="chrome133_serverhello_b")),
                encoding="utf-8",
            )

            samples = load_server_hello_artifact(first_path) + load_server_hello_artifact(second_path)

        ok, failures = check_server_hello_matrix(samples, make_registry())

        self.assertFalse(ok)
        self.assertIn("batch: mixed scenario_id values", failures)

    def test_rejects_mixed_source_path_batch(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            first_path = pathlib.Path(temp_dir) / "serverhello-source-a.json"
            second_path = pathlib.Path(temp_dir) / "serverhello-source-b.json"
            first_path.write_text(
                json.dumps(make_server_hello_artifact(source_path="/captures/chrome133_a.pcapng")),
                encoding="utf-8",
            )
            second_path.write_text(
                json.dumps(make_server_hello_artifact(source_path="/captures/chrome133_b.pcapng")),
                encoding="utf-8",
            )

            samples = load_server_hello_artifact(first_path) + load_server_hello_artifact(second_path)

        ok, failures = check_server_hello_matrix(samples, make_registry())

        self.assertFalse(ok)
        self.assertIn("batch: mixed source_path values", failures)

    def test_rejects_mixed_source_sha256_batch(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            first_path = pathlib.Path(temp_dir) / "serverhello-sha-a.json"
            second_path = pathlib.Path(temp_dir) / "serverhello-sha-b.json"
            first_path.write_text(
                json.dumps(make_server_hello_artifact(source_sha256="artifact-sha256-a")),
                encoding="utf-8",
            )
            second_path.write_text(
                json.dumps(make_server_hello_artifact(source_sha256="artifact-sha256-b")),
                encoding="utf-8",
            )

            samples = load_server_hello_artifact(first_path) + load_server_hello_artifact(second_path)

        ok, failures = check_server_hello_matrix(samples, make_registry())

        self.assertFalse(ok)
        self.assertIn("batch: mixed source_sha256 values", failures)

    def test_rejects_missing_source_path(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            artifact_path = pathlib.Path(temp_dir) / "serverhello-missing-source-path.json"
            artifact_path.write_text(json.dumps(make_server_hello_artifact(source_path="")), encoding="utf-8")
            samples = load_server_hello_artifact(artifact_path)

        ok, failures = check_server_hello_matrix(samples, make_registry())

        self.assertFalse(ok)
        self.assertIn("sample[0]: missing source_path", failures)

    def test_rejects_missing_source_sha256(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            artifact_path = pathlib.Path(temp_dir) / "serverhello-missing-source-sha.json"
            artifact_path.write_text(json.dumps(make_server_hello_artifact(source_sha256="")), encoding="utf-8")
            samples = load_server_hello_artifact(artifact_path)

        ok, failures = check_server_hello_matrix(samples, make_registry())

        self.assertFalse(ok)
        self.assertIn("sample[0]: missing source_sha256", failures)

    def test_rejects_mixed_fixture_family_batch(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            first_path = pathlib.Path(temp_dir) / "serverhello-family-a.json"
            second_path = pathlib.Path(temp_dir) / "serverhello-family-b.json"
            first_path.write_text(
                json.dumps(make_server_hello_artifact(family="chromium_44cd_mlkem_linux_desktop")),
                encoding="utf-8",
            )
            second_path.write_text(
                json.dumps(make_server_hello_artifact(family="chromium_4469_mlkem_linux_desktop")),
                encoding="utf-8",
            )

            samples = load_server_hello_artifact(first_path) + load_server_hello_artifact(second_path)

        ok, failures = check_server_hello_matrix(samples, make_registry())

        self.assertFalse(ok)
        self.assertIn("batch: mixed fixture_family_id values", failures)


if __name__ == "__main__":
    unittest.main()