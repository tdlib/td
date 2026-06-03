# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import json
import pathlib
import re
import sys
import tempfile
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
TOOLS_CI = REPO_ROOT / "tools" / "ci"
if str(TOOLS_CI) not in sys.path:
    sys.path.insert(0, str(TOOLS_CI))

import run_sanitizer_matrix  # noqa: E402


class RunSanitizerMatrixMsanContractTest(unittest.TestCase):
    def test_runner_exposes_msan_lane_and_presets(self) -> None:
        lane_by_name = {lane.name: lane for lane in run_sanitizer_matrix.LANES}
        self.assertIn("msan", lane_by_name)
        self.assertEqual("sanitizer-msan", lane_by_name["msan"].configure_preset)
        self.assertEqual("sanitizer-msan-tests", lane_by_name["msan"].build_preset)
        self.assertEqual("build-msan", lane_by_name["msan"].build_dir)

    def test_runner_tracks_memory_sanitizer_findings(self) -> None:
        categories = {
            pattern["category"]
            for pattern in run_sanitizer_matrix.SANITIZER_FINDING_PATTERNS
        }
        self.assertIn("msan_uninitialized_use", categories)

    def test_runner_sets_symbolizer_for_asan_and_msan(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            wrapper = pathlib.Path(temp_dir) / "llvm-symbolizer"
            wrapper.write_text("#!/bin/sh\n", encoding="utf-8")
            env = run_sanitizer_matrix.build_lane_env({}, wrapper)

            self.assertEqual(str(wrapper), env["ASAN_SYMBOLIZER_PATH"])
            self.assertEqual(str(wrapper), env["MSAN_SYMBOLIZER_PATH"])

    def test_cmake_presets_define_msan_configure_and_build(self) -> None:
        presets_path = REPO_ROOT / "CMakePresets.json"
        payload = json.loads(presets_path.read_text(encoding="utf-8"))

        configure_presets = {
            preset["name"]: preset for preset in payload.get("configurePresets", [])
        }
        build_presets = {
            preset["name"]: preset for preset in payload.get("buildPresets", [])
        }

        self.assertIn("sanitizer-msan", configure_presets)
        self.assertEqual(
            "${sourceDir}/build-msan",
            configure_presets["sanitizer-msan"]["binaryDir"],
        )
        self.assertIn("sanitizer-msan-tests", build_presets)
        self.assertEqual(
            "sanitizer-msan",
            build_presets["sanitizer-msan-tests"]["configurePreset"],
        )

    def test_cmake_presets_wire_vendor_sqlcipher_ignorelist(self) -> None:
        presets_path = REPO_ROOT / "CMakePresets.json"
        payload = json.loads(presets_path.read_text(encoding="utf-8"))

        configure_presets = {
            preset["name"]: preset for preset in payload.get("configurePresets", [])
        }
        msan_preset = configure_presets["sanitizer-msan"]
        msan_flags = msan_preset["cacheVariables"]
        ignorelist_flag = "-fsanitize-ignorelist=${sourceDir}/tools/ci/msan.ignorelist"

        self.assertIn(ignorelist_flag, msan_flags["CMAKE_C_FLAGS"])
        self.assertIn(ignorelist_flag, msan_flags["CMAKE_CXX_FLAGS"])

        ignorelist_path = REPO_ROOT / "tools" / "ci" / "msan.ignorelist"
        ignorelist_body = ignorelist_path.read_text(encoding="utf-8")

        self.assertIn("src:*/sqlite/tdsqlite_amalgamation.c", ignorelist_body)
        self.assertIn("fun:sqlcipher_openssl_cipher", ignorelist_body)
        self.assertIn("fun:sqlcipher_openssl_hmac", ignorelist_body)

    def test_sqlite_target_disables_msan_param_retval_only_for_msan(self) -> None:
        sqlite_cmake = (REPO_ROOT / "sqlite" / "CMakeLists.txt").read_text(
            encoding="utf-8"
        )

        self.assertRegex(
            sqlite_cmake,
            re.compile(
                r"if\s*\(\s*CMAKE_C_FLAGS\s+MATCHES\s+\"\(\^\| \)-fsanitize=memory\( \|\$\)\"\s*\)",
                re.MULTILINE,
            ),
        )
        self.assertIn(
            "$<$<COMPILE_LANGUAGE:C>:-fno-sanitize-memory-param-retval>",
            sqlite_cmake,
        )


if __name__ == "__main__":
    unittest.main()
