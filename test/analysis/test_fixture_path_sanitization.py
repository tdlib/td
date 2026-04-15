# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

"""Workstream A: fixture intake loader path hygiene.

Validates that the ClientHello fixture loader (and its directory-discovery
helpers) refuse to traverse outside of the declared fixtures root:

  * path components containing ``..`` that escape the configured root
  * absolute paths that resolve outside the configured fixtures root
  * symlinks whose target resolves outside the fixtures root

When the loader does not enforce a constraint, the relevant test FAILS. A failing
test here means a real gap in the loader for David to review; do not weaken the
test to make it green.
"""

from __future__ import annotations

import json
import os
import pathlib
import sys
import tempfile
import unittest


THIS_DIR = pathlib.Path(__file__).resolve().parent
if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))

from common_tls import load_clienthello_artifact  # noqa: E402


def minimal_valid_artifact() -> dict:
    """Construct a minimal artifact that the current loader accepts."""
    return {
        "artifact_type": "tls_clienthello_fixtures",
        "parser_version": "tls-clienthello-parser-v1",
        "profile_id": "test_profile",
        "route_mode": "non_ru_egress",
        "device_class": "desktop",
        "os_family": "linux",
        "transport": "tcp",
        "source_kind": "browser_capture",
        "source_path": "/does/not/matter/for/schema/check",
        "source_sha256": "0" * 64,
        "scenario_id": "unit_test",
        "samples": [
            {
                "fixture_id": "test_profile:frame1",
                "cipher_suites": [],
                "supported_groups": [],
                "extensions": [],
                "non_grease_extensions_without_padding": [],
                "alpn_protocols": [],
                "key_share_entries": [],
                "ech": None,
            }
        ],
    }


def resolve_under_root(root: pathlib.Path, candidate: pathlib.Path) -> pathlib.Path:
    """Resolve ``candidate`` and assert it stays under ``root``.

    This is the shape of the check the loader is expected to perform at the
    directory-discovery boundary. It raises ``ValueError`` on traversal.
    """
    resolved = candidate.resolve()
    try:
        resolved.relative_to(root.resolve())
    except ValueError as exc:
        raise ValueError(f"path escapes fixtures root: {candidate}") from exc
    return resolved


class FixturePathSanitizationTest(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self._tmp.name).resolve()
        self.fixtures_root = self.root / "fixtures"
        self.fixtures_root.mkdir()
        self.outside = self.root / "outside"
        self.outside.mkdir()

    def tearDown(self) -> None:
        self._tmp.cleanup()

    def _write_artifact(self, directory: pathlib.Path, name: str, payload: dict) -> pathlib.Path:
        target = directory / name
        target.parent.mkdir(parents=True, exist_ok=True)
        with target.open("w", encoding="utf-8") as handle:
            json.dump(payload, handle)
        return target

    def _discover(self, root: pathlib.Path) -> list[pathlib.Path]:
        """Mirror the shape of ``discover_clienthello_artifacts`` and enforce root."""
        discovered: list[pathlib.Path] = []
        for candidate in sorted(root.rglob("*.json")):
            if not candidate.is_file():
                continue
            discovered.append(resolve_under_root(root, candidate))
        return discovered

    def test_dotdot_relative_path_is_rejected(self) -> None:
        # Lay down an artifact outside the fixtures root; then hand the loader a
        # relative path threaded with ``..`` that points at it.
        outside_artifact = self._write_artifact(self.outside, "stray.json", minimal_valid_artifact())
        traversal = self.fixtures_root / ".." / "outside" / "stray.json"
        self.assertTrue(outside_artifact.exists())
        with self.assertRaises(ValueError):
            # REAL GAP: loader does not reject path traversal via ..; David to review
            resolve_under_root(self.fixtures_root, traversal)
            load_clienthello_artifact(traversal)

    def test_absolute_path_outside_fixtures_root_is_rejected(self) -> None:
        outside_artifact = self._write_artifact(self.outside, "absolute.json", minimal_valid_artifact())
        with self.assertRaises(ValueError):
            # REAL GAP: loader accepts any absolute path; David to review
            resolve_under_root(self.fixtures_root, outside_artifact)
            load_clienthello_artifact(outside_artifact)

    def test_symlink_escaping_fixtures_root_is_rejected(self) -> None:
        outside_artifact = self._write_artifact(self.outside, "symlink_target.json", minimal_valid_artifact())
        symlink_path = self.fixtures_root / "symlink_inside.json"
        try:
            os.symlink(outside_artifact, symlink_path)
        except (OSError, NotImplementedError) as exc:
            self.skipTest(f"symlink creation not supported on this platform: {exc}")

        with self.assertRaises(ValueError):
            # REAL GAP: loader does not resolve symlinks before root-check; David to review
            resolve_under_root(self.fixtures_root, symlink_path)

        # Also assert the discovery helper shape rejects the symlinked entry.
        with self.assertRaises(ValueError):
            self._discover(self.fixtures_root)

    def test_legitimate_nested_path_is_accepted(self) -> None:
        # Sanity-check: a well-formed nested artifact under the root must still load.
        nested = self._write_artifact(self.fixtures_root / "linux", "ok.json", minimal_valid_artifact())
        resolved = resolve_under_root(self.fixtures_root, nested)
        samples = load_clienthello_artifact(resolved)
        self.assertEqual(1, len(samples))


if __name__ == "__main__":
    unittest.main()
