# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

import argparse
import json
import pathlib
import sys
import unittest
from unittest import mock


THIS_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = THIS_DIR.parent.parent
TOOLS_SQLITE_DIR = REPO_ROOT / "tools" / "sqlite"
WORKFLOW_PATH = REPO_ROOT / ".github" / "workflows" / "sqlite-vendor-integrity.yml"
MAINTENANCE_DOC_PATH = REPO_ROOT / "docs" / "Plans" / "SQLITE_VENDOR_MAINTENANCE_2026-04-13.md"
VENDOR_MANIFEST_PATH = REPO_ROOT / "sqlite" / "VENDOR.json"
if str(TOOLS_SQLITE_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_SQLITE_DIR))

import update_vendor


class SqliteUpdateVendorContractTest(unittest.TestCase):
    def test_current_manifest_pins_source_tarball_sha256(self) -> None:
        manifest = json.loads(VENDOR_MANIFEST_PATH.read_text(encoding="utf-8"))

        self.assertRegex(
            manifest["vendor"].get("source_tarball_sha256", ""),
            r"^[0-9a-f]{64}$",
            msg="sqlite/VENDOR.json must pin a 64-hex source tarball digest",
        )

    def test_resolve_source_tarball_sha256_uses_current_manifest_pin(self) -> None:
        manifest = json.loads(VENDOR_MANIFEST_PATH.read_text(encoding="utf-8"))
        args = argparse.Namespace(
            source_tarball_sha256=None,
            source_tarball_url=manifest["vendor"]["source_tarball_url"],
            release_tag=manifest["vendor"]["release_tag"],
        )

        resolved = update_vendor.resolve_source_tarball_sha256(
            args,
            REPO_ROOT,
            manifest["vendor"]["source_tarball_url"],
        )

        self.assertEqual(manifest["vendor"]["source_tarball_sha256"], resolved)

    def test_acquire_source_tree_allows_explicitly_allowlisted_custom_https_host(self) -> None:
        args = argparse.Namespace(
            source_dir=None,
            source_tarball_url="https://mirror.example/sqlcipher-v4.14.0.tar.gz",
            source_tarball_sha256="7" * 64,
            release_tag="v4.14.0",
            allow_source_host=["mirror.example"],
        )

        with mock.patch.object(update_vendor, "download_tarball", return_value=pathlib.Path("/tmp/sqlcipher.tar.gz")) as download_tarball, mock.patch.object(
            update_vendor, "verify_downloaded_tarball_sha256"
        ) as verify_downloaded_tarball_sha256, mock.patch.object(
            update_vendor, "extract_tarball", return_value=pathlib.Path("/tmp/sqlcipher-src")
        ) as extract_tarball:
            source_dir, tarball_url, source_tarball_sha256 = update_vendor.acquire_source_tree(
                args, pathlib.Path("/tmp")
            )

        self.assertEqual(pathlib.Path("/tmp/sqlcipher-src"), source_dir)
        self.assertEqual("https://mirror.example/sqlcipher-v4.14.0.tar.gz", tarball_url)
        self.assertEqual("7" * 64, source_tarball_sha256)
        download_tarball.assert_called_once()
        verify_downloaded_tarball_sha256.assert_called_once_with(pathlib.Path("/tmp/sqlcipher.tar.gz"), "7" * 64)
        extract_tarball.assert_called_once()

    def test_small_verification_suite_covers_phase4_acceptance_gate(self) -> None:
        recorded_commands = []

        def fake_run_checked(command: list[str], cwd: pathlib.Path, env=None) -> None:
            recorded_commands.append((command, cwd, env))

        with mock.patch.object(update_vendor, "verify_vendor_integrity", return_value=[]), mock.patch.object(
            update_vendor, "run_checked", side_effect=fake_run_checked
        ):
            update_vendor.run_small_verification_suite(REPO_ROOT)

        self.assertGreaterEqual(len(recorded_commands), 1)
        command_tokens = [token for command, _, _ in recorded_commands for token in command]
        self.assertTrue(
            "test_sqlite*.py" in command_tokens
            or "test_sqlite_phase4_vendor_target_repo.py" in command_tokens,
            msg="phase 4 vendor refresh verification must execute the target-baseline acceptance test",
        )

    def test_phase5_ci_workflow_triggers_when_update_vendor_tests_change(self) -> None:
        workflow_text = WORKFLOW_PATH.read_text(encoding="utf-8")

        self.assertTrue(
            "test/analysis/test_sqlite*.py" in workflow_text
            or "test/analysis/test_sqlite_update_vendor*.py" in workflow_text,
            msg="phase 5 CI must trigger when updater guardrail tests change",
        )

    def test_phase5_ci_workflow_executes_update_vendor_tests(self) -> None:
        workflow_text = WORKFLOW_PATH.read_text(encoding="utf-8")

        self.assertTrue(
            "test_sqlite*.py" in workflow_text
            or "test_sqlite_update_vendor*.py" in workflow_text,
            msg="phase 5 CI must execute updater contract/adversarial tests, not only audit tests",
        )

    def test_phase5_ci_workflow_triggers_when_native_sqlite_smoke_contract_changes(self) -> None:
        workflow_text = WORKFLOW_PATH.read_text(encoding="utf-8")

        self.assertIn(
            "test/sqlite_vendor_contract.cpp",
            workflow_text,
            msg="phase 5 CI must trigger when the native SQLite smoke contract source changes",
        )
        self.assertIn(
            "test/CMakeLists.txt",
            workflow_text,
            msg="phase 5 CI must trigger when run_all_tests exact CTest discovery wiring changes",
        )

    def test_phase5_ci_workflow_triggers_when_cmake_tools_contract_inputs_change(self) -> None:
        workflow_text = WORKFLOW_PATH.read_text(encoding="utf-8")

        required_paths = (
            "CMakeLists.txt",
            "CMakePresets.json",
            ".vscode/settings.json",
            ".vscode/tasks.json",
        )

        for required_path in required_paths:
            with self.subTest(required_path=required_path):
                self.assertIn(
                    required_path,
                    workflow_text,
                    msg="phase 5 CI must trigger when checked-in CMake Tools contract inputs change",
                )

    def test_phase5_ci_workflow_runs_native_db_smoke_after_python_guardrails(self) -> None:
        workflow_text = WORKFLOW_PATH.read_text(encoding="utf-8")

        analysis_index = workflow_text.index("python3 -m unittest discover -s test/analysis -p 'test_sqlite*.py'")
        smoke_index = workflow_text.index(
            "ctest --test-dir build/test --output-on-failure -R '^Test_DB_sqlite_vendor_wrapper_surface_contract$'"
        )

        self.assertIn("cmake --build build --target run_all_tests", workflow_text)
        self.assertLess(
            analysis_index,
            smoke_index,
            msg="native DB smoke must run after the Python guardrails",
        )

    def test_phase5_ci_workflow_installs_gperf_before_native_configure(self) -> None:
        workflow_text = WORKFLOW_PATH.read_text(encoding="utf-8")

        install_index = workflow_text.index("apt-get install -y gperf")
        configure_index = workflow_text.index(
            "cmake -S . -B build -G Ninja -DBUILD_SHARED_LIBS=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DTD_ENABLE_BENCHMARKS=OFF -DTD_ENABLE_LLD=ON"
        )

        self.assertLess(
            install_index,
            configure_index,
            msg="phase 5 CI must install gperf before the first native configure because tdutils/generate hard-requires it",
        )

    def test_phase5_ci_workflow_reconfigures_after_build_before_exact_ctest_smoke(self) -> None:
        workflow_text = WORKFLOW_PATH.read_text(encoding="utf-8")

        configure_command = (
            "cmake -S . -B build -G Ninja -DBUILD_SHARED_LIBS=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DTD_ENABLE_BENCHMARKS=OFF -DTD_ENABLE_LLD=ON"
        )
        first_configure_index = workflow_text.index(configure_command)
        build_index = workflow_text.index("cmake --build build --target run_all_tests --parallel $(nproc)")
        second_configure_index = workflow_text.rindex(configure_command)
        smoke_index = workflow_text.index(
            "ctest --test-dir build/test --output-on-failure -R '^Test_DB_sqlite_vendor_wrapper_surface_contract$'"
        )

        self.assertGreater(
            second_configure_index,
            build_index,
            msg="clean-tree exact CTest smoke needs a post-build configure pass so run_all_tests --list can register exact test names",
        )
        self.assertLess(
            second_configure_index,
            smoke_index,
            msg="the post-build configure refresh must happen before exact CTest smoke execution",
        )
        self.assertLess(first_configure_index, build_index)

    def test_maintenance_doc_fast_guardrails_cover_update_vendor_tests(self) -> None:
        maintenance_doc = MAINTENANCE_DOC_PATH.read_text(encoding="utf-8")

        self.assertIn(
            "test_sqlite*.py",
            maintenance_doc,
            msg="phase 5 maintenance guidance must include updater analysis tests in the fast guardrail command",
        )

    def test_maintenance_doc_upgrade_sequence_requires_pinned_tarball_sha256(self) -> None:
        maintenance_doc = MAINTENANCE_DOC_PATH.read_text(encoding="utf-8")

        self.assertIn(
            "--source-tarball-sha256",
            maintenance_doc,
            msg="phase 5 maintenance guidance must document the pinned tarball digest requirement",
        )

    def test_maintenance_doc_prerequisites_include_gperf_for_native_smoke(self) -> None:
        maintenance_doc = MAINTENANCE_DOC_PATH.read_text(encoding="utf-8")

        self.assertIn(
            "gperf",
            maintenance_doc,
            msg="maintenance guidance must call out gperf because the native configure step fails without it",
        )

    def test_maintenance_doc_reconfigures_after_build_before_exact_ctest_smoke(self) -> None:
        maintenance_doc = MAINTENANCE_DOC_PATH.read_text(encoding="utf-8")

        configure_command = (
            "cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DTD_ENABLE_BENCHMARKS=OFF"
        )
        first_configure_index = maintenance_doc.index(configure_command)
        build_index = maintenance_doc.index("cmake --build build --target run_all_tests --parallel 2")
        second_configure_index = maintenance_doc.rindex(configure_command)
        smoke_index = maintenance_doc.index(
            "ctest --test-dir build/test --output-on-failure -R '^Test_DB_sqlite_vendor_wrapper_surface_contract$'"
        )

        self.assertGreater(
            second_configure_index,
            build_index,
            msg="maintenance guidance must include the post-build configure pass required for exact CTest discovery on clean trees",
        )
        self.assertLess(
            second_configure_index,
            smoke_index,
            msg="maintenance guidance must refresh exact CTest discovery before the targeted smoke run",
        )
        self.assertLess(first_configure_index, build_index)
