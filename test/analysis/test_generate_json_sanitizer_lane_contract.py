# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import pathlib
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
TD_GENERATE_CMAKE = REPO_ROOT / "td" / "generate" / "CMakeLists.txt"


class GenerateJsonSanitizerLaneContractTest(unittest.TestCase):
    def test_generator_disable_helper_stays_off_for_sanitizer_builds(self) -> None:
        cmake_text = TD_GENERATE_CMAKE.read_text(encoding="utf-8")

        self.assertIn(
            "if(NOT TD_SANITIZER_BUILD)",
            cmake_text,
            msg=(
                "Generator sanitizer disabling must remain disabled in sanitizer lanes "
                "to avoid turning real sanitizer defects into non-blocking behavior"
            ),
        )
        self.assertNotIn("MSAN_OPTIONS=halt_on_error=0:exit_code=0", cmake_text)
        self.assertNotIn("LSAN_OPTIONS=detect_leaks=0:exitcode=0", cmake_text)

    def test_generate_json_keeps_standard_sanitizer_command_shape(self) -> None:
        cmake_text = TD_GENERATE_CMAKE.read_text(encoding="utf-8")

        self.assertIn(
            "set(TL_GENERATE_JSON_COMMAND\n        ${CMAKE_COMMAND} -E env ASAN_OPTIONS=detect_leaks=0",
            cmake_text,
        )
        self.assertEqual(
            1,
            cmake_text.count("td_disable_sanitizers_for_generator(generate_json)"),
            msg=(
                "generate_json must continue using the shared generator helper callsite; "
                "helper internals decide when disabling is allowed"
            ),
        )


if __name__ == "__main__":
    unittest.main()
