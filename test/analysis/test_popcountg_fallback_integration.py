# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import pathlib
import shutil
import subprocess
import tempfile
import textwrap
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
PLATFORM_HEADER = REPO_ROOT / "tdutils" / "td" / "utils" / "port" / "platform.h"
COMMON_HEADER = REPO_ROOT / "tdutils" / "td" / "utils" / "common.h"
SESSION_CONNECTION_HEADER = REPO_ROOT / "td" / "mtproto" / "SessionConnection.h"


class PopcountgFallbackIntegrationTest(unittest.TestCase):
    def test_common_header_keeps_platform_as_early_dependency(self) -> None:
        source = COMMON_HEADER.read_text(encoding="utf-8")

        self.assertIn('#include "td/utils/port/platform.h"', source)

    def test_mtproto_session_connection_transitively_includes_common_header(
        self,
    ) -> None:
        source = SESSION_CONNECTION_HEADER.read_text(encoding="utf-8")

        self.assertIn('#include "td/utils/common.h"', source)

    def test_clang_smoke_compile_with_bit_header(self) -> None:
        clangxx = shutil.which("clang++-22") or shutil.which("clang++")
        if clangxx is None:
            self.skipTest("clang++ toolchain is unavailable")

        source = textwrap.dedent("""
            #include \"td/utils/port/platform.h\"
            #include <bit>
            #include <cstdint>

            int main() {
              auto value = std::popcount(static_cast<std::uint64_t>(0xF0F0ULL));
              return value == 8 ? 0 : 1;
            }
            """)

        with tempfile.TemporaryDirectory(prefix="popcountg-integration-") as temp_dir:
            temp_path = pathlib.Path(temp_dir)
            source_path = temp_path / "probe.cpp"
            binary_path = temp_path / "probe"
            source_path.write_text(source, encoding="utf-8")

            command = [
                clangxx,
                "-std=gnu++23",
                "-I",
                str(REPO_ROOT / "tdutils"),
                str(source_path),
                "-o",
                str(binary_path),
            ]
            result = subprocess.run(
                command, check=False, capture_output=True, text=True
            )

            self.assertEqual(
                0,
                result.returncode,
                msg=(
                    "clang smoke compile must succeed with platform header and <bit>.\n"
                    f"stdout:\n{result.stdout}\n"
                    f"stderr:\n{result.stderr}"
                ),
            )


if __name__ == "__main__":
    unittest.main()
