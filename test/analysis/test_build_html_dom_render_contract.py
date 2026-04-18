# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT

import pathlib
import re
import unittest


THIS_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = THIS_DIR.parent.parent
BUILD_HTML_PATH = REPO_ROOT / "build.html"


class BuildHtmlDomRenderContractTest(unittest.TestCase):
    def test_on_os_changed_uses_text_content_for_build_text(self) -> None:
        content = BUILD_HTML_PATH.read_text(encoding="utf-8")
        on_os_changed_match = re.search(
            r"function onOsChanged\(\) \{(?P<body>.*?)\n\}\n\nfunction onOptionsChanged\(\)",
            content,
            flags=re.DOTALL,
        )
        self.assertIsNotNone(on_os_changed_match, msg="onOsChanged function must exist")
        body = on_os_changed_match.group("body")

        self.assertIn(
            "document.getElementById('buildText').textContent = text;",
            body,
            msg="buildText content must be written with textContent to avoid HTML reinterpretation",
        )
        self.assertNotIn(
            "document.getElementById('buildText').innerHTML = text;",
            body,
            msg="buildText must not be written with innerHTML",
        )

    def test_build_text_urls_are_rendered_as_literal_text(self) -> None:
        content = BUILD_HTML_PATH.read_text(encoding="utf-8")
        self.assertIn(
            "https://www.npmjs.com/package/tdweb",
            content,
            msg="tdweb package URL should remain visible in generated build text",
        )
        self.assertIn(
            "https://github.com/tdlib/td/tree/master/example/web",
            content,
            msg="web example URL should remain visible in generated build text",
        )


if __name__ == "__main__":
    unittest.main()
