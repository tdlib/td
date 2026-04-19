# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT

import pathlib
import sys
import textwrap
import unittest


THIS_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = THIS_DIR.parent.parent
TOOLS_CI_DIR = REPO_ROOT / "tools" / "ci"
if str(TOOLS_CI_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_CI_DIR))

import check_cpp23_compat as guard


class Cpp23GuardAdversarialTest(unittest.TestCase):
    def test_find_u8_literal_findings_flags_multiple_literal_forms(self) -> None:
        probe = textwrap.dedent(
            """
            auto one = u8"plain";
            auto two = u8R"(raw)";
            auto three = u8'x';
            """
        )

        findings = guard.find_u8_literal_findings(probe)

        self.assertEqual(["u8\"", "u8R\"", "u8'"], [finding.token for finding in findings])

    def test_find_u8_literal_findings_ignores_escaped_quote_sequences_inside_strings(self) -> None:
        probe = textwrap.dedent(
            r'''
            const char *value = "the token u8\" is data, not code";
            const char *other = R"(u8R\" is also data here)";
            '''
        )

        findings = guard.find_u8_literal_findings(probe)

        self.assertEqual([], findings)


if __name__ == "__main__":
    unittest.main()