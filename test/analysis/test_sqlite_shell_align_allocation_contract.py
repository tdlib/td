# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT

import pathlib
import re
import unittest


THIS_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = THIS_DIR.parent.parent
SHELL_C_CANDIDATES = (
    REPO_ROOT / "docs" / "Samples" / "sqlite3_uptodate" / "shell.c",
    REPO_ROOT / "sqlite" / "upstream" / "shell.c",
)


class SqliteShellAlignAllocationContractTest(unittest.TestCase):
    def test_align_option_uses_allocate_then_swap_pattern(self) -> None:
        shell_c_path = next((path for path in SHELL_C_CANDIDATES if path.exists()), None)
        if shell_c_path is None:
            raise unittest.SkipTest("sqlite shell.c sample is not present in this checkout")

        content = shell_c_path.read_text(encoding="utf-8")
        align_block_match = re.search(
            r"\}else if\( optionMatch\(z,\"align\"\) \)\{(?P<body>.*?)\n\s*\}else if\( pickStr\(z,0,\"-blob\",",
            content,
            flags=re.DOTALL,
        )
        self.assertIsNotNone(align_block_match, msg="align option block must exist")
        body = align_block_match.group("body")

        malloc_pos = body.find("aNewAlign = malloc(nAlign);")
        free_old_pos = body.find("free(p->mode.spec.aAlign);")
        assign_pos = body.find("p->mode.spec.aAlign = aNewAlign;")

        self.assertGreaterEqual(malloc_pos, 0, msg="align block must allocate a temporary buffer")
        self.assertGreaterEqual(free_old_pos, 0, msg="align block must free old buffer")
        self.assertGreaterEqual(assign_pos, 0, msg="align block must swap pointer to temporary buffer")

        self.assertLess(
            malloc_pos,
            free_old_pos,
            msg="new buffer allocation must happen before old buffer free",
        )
        self.assertLess(
            free_old_pos,
            assign_pos,
            msg="pointer swap must happen only after old buffer free",
        )

        self.assertIn(
            "aNewAlign[k] = c;",
            body,
            msg="alignment values must be written into temporary buffer",
        )
        self.assertNotIn(
            "p->mode.spec.aAlign[k] = c;",
            body,
            msg="alignment values must not be written directly to live pointer before swap",
        )


if __name__ == "__main__":
    unittest.main()
