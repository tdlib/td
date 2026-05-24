# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import pathlib
import re
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
README_PATH = REPO_ROOT / "example" / "README.md"
JAVASCRIPT_SECTION_START = '<a name="javascript"></a>'
JAVASCRIPT_SECTION_END = '<a name="go"></a>'
REACT_NATIVE_SCOPE_SENTENCE = (
    "TDLib can also be used from React Native on iOS and Android."
)

TARGET_WRAPPERS = (
    "tdl-coroutines",
    "react-native-tdlib",
)

TARGET_LINK_PATTERN = re.compile(r"\[(?P<label>[^\]]+)\]\((?P<link>[^)]+)\)")


class ToolingDocsBackportPlanAdversarialTest(unittest.TestCase):
    def _get_javascript_section(self) -> str:
        readme = README_PATH.read_text(encoding="utf-8")

        start = readme.find(JAVASCRIPT_SECTION_START)
        end = readme.find(JAVASCRIPT_SECTION_END)
        self.assertNotEqual(-1, start)
        self.assertNotEqual(-1, end)
        self.assertLess(start, end)
        return readme[start:end]

    def _extract_target_wrapper_links(self) -> dict[str, list[str]]:
        readme = README_PATH.read_text(encoding="utf-8")

        links_by_label: dict[str, list[str]] = {}
        for match in TARGET_LINK_PATTERN.finditer(readme):
            label = match.group("label")
            link = match.group("link")
            if label not in TARGET_WRAPPERS:
                continue
            links_by_label.setdefault(label, []).append(link)
        return links_by_label

    def test_target_wrapper_links_must_be_https_github_urls(self) -> None:
        links_by_label = self._extract_target_wrapper_links()

        for wrapper in TARGET_WRAPPERS:
            self.assertIn(wrapper, links_by_label)
            for link in links_by_label[wrapper]:
                self.assertTrue(
                    link.startswith("https://github.com/"),
                    msg=f"wrapper {wrapper} must use an https GitHub URL",
                )
                self.assertNotIn("javascript:", link.lower())
                self.assertNotIn("http://", link.lower())

    def test_target_wrapper_links_must_not_be_duplicated(self) -> None:
        links_by_label = self._extract_target_wrapper_links()

        for wrapper in TARGET_WRAPPERS:
            self.assertIn(wrapper, links_by_label)
            self.assertEqual(
                1,
                len(links_by_label[wrapper]),
                msg=f"wrapper {wrapper} must be listed exactly once",
            )

    def test_react_native_scope_sentence_must_be_in_javascript_section(self) -> None:
        javascript_section = self._get_javascript_section()

        self.assertIn(REACT_NATIVE_SCOPE_SENTENCE, javascript_section)
        self.assertIn(
            "[react-native-tdlib](https://github.com/vladlenskiy/react-native-tdlib)",
            javascript_section,
        )

    def test_react_native_scope_sentence_must_be_unique(self) -> None:
        readme = README_PATH.read_text(encoding="utf-8")

        self.assertEqual(1, readme.count(REACT_NATIVE_SCOPE_SENTENCE))


if __name__ == "__main__":
    unittest.main()
