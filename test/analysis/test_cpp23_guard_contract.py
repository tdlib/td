# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT

import pathlib
import sys
import tempfile
import textwrap
import unittest


THIS_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = THIS_DIR.parent.parent
TOOLS_CI_DIR = REPO_ROOT / "tools" / "ci"
if str(TOOLS_CI_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_CI_DIR))

import check_cpp23_compat as guard


class Cpp23GuardContractTest(unittest.TestCase):
    def test_collect_source_files_skips_ignored_trees(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = pathlib.Path(temp_dir)
            (repo_root / "td").mkdir()
            (repo_root / "td" / "Kept.cpp").write_text("int kept();\n", encoding="utf-8")
            (repo_root / "test").mkdir()
            (repo_root / "test" / "Ignored.cpp").write_text("int ignored();\n", encoding="utf-8")
            (repo_root / "sqlite").mkdir()
            (repo_root / "sqlite" / "Ignored.cpp").write_text("int ignored();\n", encoding="utf-8")
            (repo_root / "build").mkdir()
            (repo_root / "build" / "Ignored.cpp").write_text("int ignored();\n", encoding="utf-8")

            sources = guard.collect_source_files(repo_root)

        self.assertEqual([repo_root / "td" / "Kept.cpp"], sources)

    def test_find_u8_literal_findings_reports_line_and_column(self) -> None:
        probe = textwrap.dedent(
            """\
            #include <string>

            std::string render() {
              return u8"bad";
            }
            """
        )

        findings = guard.find_u8_literal_findings(probe)

        self.assertEqual(1, len(findings))
        finding = findings[0]
        self.assertEqual(4, finding.line)
        self.assertEqual("u8\"", finding.token)

    def test_find_u8_literal_findings_ignores_comments(self) -> None:
        probe = textwrap.dedent(
            """
            // return u8"comment only";
            /* auto ignored = u8R"(comment only)"; */
            const char *value = "safe";
            """
        )

        findings = guard.find_u8_literal_findings(probe)

        self.assertEqual([], findings)


if __name__ == "__main__":
    unittest.main()