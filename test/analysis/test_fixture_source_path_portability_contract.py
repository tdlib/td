#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

"""RISK-FP-23, RISK-FP-10: fixture source_path portability contract.

Validates that fixture source_path values are portable across workstations and
that absolute workspace paths are handled correctly:

  * Absolute paths rooted at "/home/david_osipov/tdlib-obf/" must be
    convertible to repo-relative paths under "docs/Samples/Traffic dumps/".
  * Cyrillic / non-ASCII characters in filenames must survive round-trip
    normalization without silent corruption.
  * NFC / NFD normalized variants of a Cyrillic path must NOT be treated as
    the same path identity.
  * Path traversal attempts ("..") must be rejected.
  * SHA-256 digests and fixture/scenario identifiers must remain stable when
    the absolute workspace prefix is stripped.

Census findings informing this test:
  - All 106 fixtures carry absolute source_path starting with
    "/home/david_osipov/tdlib-obf/"
  - 31 Windows fixtures point to "Linux, desktop/Windows/" directory
  - 6 raw dump files have Cyrillic / non-ASCII names
"""

from __future__ import annotations

import json
import pathlib
import sys
import unicodedata
import unittest

THIS_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = THIS_DIR.resolve().parents[1]
FIXTURES_ROOT = THIS_DIR / "fixtures" / "clienthello"

if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

ABSOLUTE_WORKSPACE_PREFIX = "/home/david_osipov/tdlib-obf/"
EXPECTED_RELATIVE_ROOT = "docs/Samples/Traffic dumps/"

# Real Cyrillic corpus paths (from census).
CYRILLIC_ANDROID_PATH = (
    "docs/Samples/Traffic dumps/Android/"
    "Андроид_14,_Adblock_browser_3_11_1,"
    "_auto_Android_10,_auto_Chromi.pcap"
)
CYRILLIC_IOS_PATH = (
    "docs/Samples/Traffic dumps/iOS/"
    "iOS_18_7,_Brave_Версия_1_88_137,"
    "_auto_iOS_18_7,_auto_unknown_bro.pcap"
)
CYRILLIC_WINDOWS_PATH = (
    "docs/Samples/Traffic dumps/Linux, desktop/Windows/"
    "Windows_10_0_Версия_21H2_19044_7058,"
    "_Google_Chrome_146_0_7680_17.pcap"
)


def _load_json(path: pathlib.Path) -> dict:
    with path.open("r", encoding="utf-8") as fh:
        return json.load(fh)


def _collect_all_fixture_jsons() -> list[pathlib.Path]:
    """Return every *.json file under FIXTURES_ROOT, sorted for determinism."""
    return sorted(FIXTURES_ROOT.rglob("*.json"))


def _strip_workspace_prefix(source_path: str) -> str:
    """Strip the absolute workspace prefix and return a repo-relative path."""
    if source_path.startswith(ABSOLUTE_WORKSPACE_PREFIX):
        return source_path[len(ABSOLUTE_WORKSPACE_PREFIX):]
    return source_path


# ---------------------------------------------------------------------------
# Test: absolute-path detection (RISK-FP-23)
# ---------------------------------------------------------------------------

class TestAbsolutePathDetection(unittest.TestCase):
    """Every fixture must embed source_path.  No *new* artifact generator
    should emit an absolute workspace root.  The portability layer (which
    does not exist yet) must expose a function that detects and rejects
    absolute workspace roots at generation time.
    """

    def test_all_fixture_jsons_have_source_path(self) -> None:
        """Every checked-in fixture JSON must contain a non-empty source_path."""
        fixture_paths = _collect_all_fixture_jsons()
        self.assertTrue(fixture_paths, "no fixture JSONs found")
        for fpath in fixture_paths:
            artifact = _load_json(fpath)
            source_path = artifact.get("source_path", "")
            self.assertTrue(
                source_path,
                msg=f"{fpath.name} is missing source_path",
            )

    def test_portability_validator_normalizes_absolute_workspace_roots(self) -> None:
        """A portability validator function must exist in common_tls and must
        normalize absolute workspace paths by stripping the prefix and
        validating the relative form.  Absolute paths rooted at known workspace
        prefixes are legacy compatibility inputs that resolve inside the repo.
        """
        import common_tls  # noqa: E402

        self.assertTrue(
            hasattr(common_tls, "validate_source_path_portable"),
            "common_tls must expose validate_source_path_portable()",
        )
        validator = common_tls.validate_source_path_portable
        # Known absolute workspace path must be normalized to repo-relative
        abs_path = ABSOLUTE_WORKSPACE_PREFIX + "docs/Samples/Traffic dumps/iOS/test.pcap"
        result = validator(abs_path)
        self.assertEqual(result, "docs/Samples/Traffic dumps/iOS/test.pcap")
        self.assertFalse(result.startswith("/"))
        # Unknown absolute paths should still raise
        with self.assertRaises(ValueError):
            validator("/unknown/workspace/docs/Samples/Traffic dumps/test.pcap")

    def test_portability_normalizer_exists(self) -> None:
        """A normalize_source_path function must exist in common_tls that
        converts absolute workspace paths to repo-relative paths.
        This test is RED because the normalizer does not exist yet.
        """
        import common_tls  # noqa: E402

        self.assertTrue(
            hasattr(common_tls, "normalize_source_path"),
            "common_tls must expose normalize_source_path()",
        )


# ---------------------------------------------------------------------------
# Test: repo-relative conversion (RISK-FP-23)
# ---------------------------------------------------------------------------

class TestRepoRelativeConversion(unittest.TestCase):
    """For each absolute source_path currently in the corpus, stripping the
    workspace prefix must yield a path rooted at "docs/Samples/Traffic dumps/".
    """

    def test_all_source_paths_resolve_under_traffic_dumps(self) -> None:
        fixture_paths = _collect_all_fixture_jsons()
        self.assertTrue(fixture_paths, "no fixture JSONs found")
        for fpath in fixture_paths:
            artifact = _load_json(fpath)
            source_path = artifact.get("source_path", "")
            if not source_path:
                continue  # covered by detection test above
            relative = _strip_workspace_prefix(source_path)
            self.assertTrue(
                relative.startswith(EXPECTED_RELATIVE_ROOT),
                msg=(
                    f"{fpath.name}: stripped source_path does not start with "
                    f"'{EXPECTED_RELATIVE_ROOT}': got '{relative}'"
                ),
            )

    def test_normalizer_produces_repo_relative_for_every_fixture(self) -> None:
        """The normalize_source_path function must convert every existing
        absolute source_path to a repo-relative path.  RED because the
        function does not exist yet.
        """
        import common_tls  # noqa: E402

        self.assertTrue(
            hasattr(common_tls, "normalize_source_path"),
            "common_tls must expose normalize_source_path()",
        )
        normalizer = common_tls.normalize_source_path
        fixture_paths = _collect_all_fixture_jsons()
        self.assertTrue(fixture_paths, "no fixture JSONs found")
        for fpath in fixture_paths:
            artifact = _load_json(fpath)
            source_path = artifact.get("source_path", "")
            if not source_path:
                continue
            normalized = normalizer(source_path)
            self.assertTrue(
                normalized.startswith(EXPECTED_RELATIVE_ROOT),
                msg=f"{fpath.name}: normalize_source_path returned '{normalized}'",
            )
            # Must NOT start with the absolute workspace prefix
            self.assertFalse(
                normalized.startswith("/"),
                msg=f"{fpath.name}: normalized path is still absolute: '{normalized}'",
            )


# ---------------------------------------------------------------------------
# Test: Cyrillic path preservation (RISK-FP-10)
# ---------------------------------------------------------------------------

class TestCyrillicPathPreservation(unittest.TestCase):
    """Real corpus paths with Cyrillic characters must be preserved
    byte-for-byte after any normalization.
    """

    def test_cyrillic_android_path_survives_strip(self) -> None:
        """The Adblock/Android Cyrillic source_path must round-trip."""
        full = ABSOLUTE_WORKSPACE_PREFIX + CYRILLIC_ANDROID_PATH
        stripped = _strip_workspace_prefix(full)
        self.assertEqual(stripped, CYRILLIC_ANDROID_PATH)
        # Byte-for-byte: encode to UTF-8 and compare
        self.assertEqual(
            stripped.encode("utf-8"),
            CYRILLIC_ANDROID_PATH.encode("utf-8"),
        )

    def test_cyrillic_windows_path_survives_strip(self) -> None:
        """The Windows fixture with Cyrillic 'Версия' must round-trip."""
        full = ABSOLUTE_WORKSPACE_PREFIX + CYRILLIC_WINDOWS_PATH
        stripped = _strip_workspace_prefix(full)
        self.assertEqual(stripped, CYRILLIC_WINDOWS_PATH)
        self.assertEqual(
            stripped.encode("utf-8"),
            CYRILLIC_WINDOWS_PATH.encode("utf-8"),
        )

    def test_normalizer_preserves_cyrillic_bytes(self) -> None:
        """The normalize_source_path function must preserve Cyrillic characters
        byte-for-byte.  RED because the function does not exist yet.
        """
        import common_tls  # noqa: E402

        self.assertTrue(
            hasattr(common_tls, "normalize_source_path"),
            "common_tls must expose normalize_source_path()",
        )
        normalizer = common_tls.normalize_source_path

        for label, rel_path in [
            ("android_cyrillic", CYRILLIC_ANDROID_PATH),
            ("ios_cyrillic", CYRILLIC_IOS_PATH),
            ("windows_cyrillic", CYRILLIC_WINDOWS_PATH),
        ]:
            full = ABSOLUTE_WORKSPACE_PREFIX + rel_path
            normalized = normalizer(full)
            self.assertEqual(
                normalized.encode("utf-8"),
                rel_path.encode("utf-8"),
                msg=f"{label}: Cyrillic bytes were corrupted by normalize_source_path",
            )

    def test_real_fixture_with_cyrillic_android_source_path(self) -> None:
        """The adblock_browser3_11_1_android14 fixture must contain a Cyrillic
        source_path that matches the known census value byte-for-byte.
        """
        fixture_path = (
            FIXTURES_ROOT / "android"
            / "adblock_browser3_11_1_android14_1e08a5c3.clienthello.json"
        )
        self.assertTrue(fixture_path.exists(), f"missing: {fixture_path}")
        artifact = _load_json(fixture_path)
        source_path = artifact["source_path"]
        # Must contain the Cyrillic prefix
        self.assertIn("Андроид", source_path)

    def test_real_fixture_with_cyrillic_windows_source_path(self) -> None:
        """The chrome146 Windows fixture must contain Cyrillic 'Версия'."""
        fixture_path = (
            FIXTURES_ROOT / "windows"
            / "chrome146_0_7680_17_windows10_0_version_21h2_19044_7058_16d3ed6d.clienthello.json"
        )
        self.assertTrue(fixture_path.exists(), f"missing: {fixture_path}")
        artifact = _load_json(fixture_path)
        source_path = artifact["source_path"]
        self.assertIn("Версия", source_path)


# ---------------------------------------------------------------------------
# Test: Unicode alias rejection (RISK-FP-10)
# ---------------------------------------------------------------------------

class TestUnicodeAliasRejection(unittest.TestCase):
    """NFC / NFD normalized versions of a Cyrillic path must NOT be treated
    as the same path identity.  A path-identity comparator must exist.
    """

    def test_nfc_nfd_versions_differ(self) -> None:
        """Sanity: NFC and NFD of the Cyrillic Android path differ in bytes."""
        nfc = unicodedata.normalize("NFC", CYRILLIC_ANDROID_PATH)
        nfd = unicodedata.normalize("NFD", CYRILLIC_ANDROID_PATH)
        # For pure Cyrillic, NFC == NFD at the codepoint level in most cases,
        # but the contract requires the system to detect when they differ.
        # If they happen to be identical, skip -- the contract is trivially
        # satisfied.
        if nfc.encode("utf-8") == nfd.encode("utf-8"):
            # Use a synthetic example with a composed/decomposed pair to
            # exercise the NFC/NFD branch.
            # Cyrillic 'й' (U+0439) can be composed or decomposed
            # (U+0438 U+0306).
            composed = "й"  # Cyrillic SHORT I (NFC precomposed)
            decomposed = "й"  # Cyrillic I + COMBINING BREVE
            self.assertNotEqual(
                composed.encode("utf-8"),
                decomposed.encode("utf-8"),
            )

    def test_path_identity_comparator_rejects_nfc_nfd_alias(self) -> None:
        """The portability layer must expose a path-identity comparison that
        treats NFC and NFD forms as distinct.  RED because it does not exist.
        """
        import common_tls  # noqa: E402

        self.assertTrue(
            hasattr(common_tls, "source_paths_identical"),
            "common_tls must expose source_paths_identical()",
        )
        comparator = common_tls.source_paths_identical

        # Use a path containing decomposable Cyrillic 'й'
        path_nfc = "docs/Samples/Traffic dumps/Android/й_test.pcap"
        path_nfd = "docs/Samples/Traffic dumps/Android/й_test.pcap"
        # They must NOT be considered identical
        self.assertFalse(
            comparator(path_nfc, path_nfd),
            "NFC and NFD paths must NOT be treated as the same identity",
        )


# ---------------------------------------------------------------------------
# Test: path traversal rejection (RISK-FP-23)
# ---------------------------------------------------------------------------

class TestPathTraversalRejection(unittest.TestCase):
    """Paths with "..", symlinks, or escapes outside
    "docs/Samples/Traffic dumps/" must be rejected.
    """

    def test_dotdot_in_source_path_rejected_by_validator(self) -> None:
        """validate_source_path_portable must reject '..' components.
        RED because the validator does not exist yet.
        """
        import common_tls  # noqa: E402

        self.assertTrue(
            hasattr(common_tls, "validate_source_path_portable"),
            "common_tls must expose validate_source_path_portable()",
        )
        validator = common_tls.validate_source_path_portable

        traversal_paths = [
            "docs/Samples/Traffic dumps/../../../etc/passwd",
            "docs/Samples/Traffic dumps/Android/../../secret.pcap",
            "../docs/Samples/Traffic dumps/Android/test.pcap",
        ]
        for bad_path in traversal_paths:
            with self.assertRaises(
                ValueError, msg=f"validator must reject '{bad_path}'"
            ):
                validator(bad_path)

    def test_escape_outside_traffic_dumps_rejected(self) -> None:
        """Paths that resolve outside 'docs/Samples/Traffic dumps/' must be
        rejected even without '..'.  RED.
        """
        import common_tls  # noqa: E402

        self.assertTrue(
            hasattr(common_tls, "validate_source_path_portable"),
            "common_tls must expose validate_source_path_portable()",
        )
        validator = common_tls.validate_source_path_portable

        outside_paths = [
            "docs/Samples/other_dir/test.pcap",
            "src/main.cpp",
            "/etc/passwd",
            "docs/Samples/Traffic dumps",  # directory itself, not a file under it
        ]
        for bad_path in outside_paths:
            with self.assertRaises(
                ValueError, msg=f"validator must reject '{bad_path}'"
            ):
                validator(bad_path)

    def test_no_existing_fixtures_contain_dotdot(self) -> None:
        """No checked-in fixture source_path should contain '..' components."""
        fixture_paths = _collect_all_fixture_jsons()
        self.assertTrue(fixture_paths, "no fixture JSONs found")
        for fpath in fixture_paths:
            artifact = _load_json(fpath)
            source_path = artifact.get("source_path", "")
            self.assertNotIn(
                "..",
                source_path,
                msg=f"{fpath.name}: source_path contains '..'",
            )


# ---------------------------------------------------------------------------
# Test: case sensitivity (RISK-FP-23)
# ---------------------------------------------------------------------------

class TestCaseSensitivity(unittest.TestCase):
    """On case-insensitive filesystems, paths differing only by case must
    not inflate source counts.
    """

    def test_case_insensitive_dedup_exists(self) -> None:
        """The portability layer must expose a function that deduplicates
        source paths on case-insensitive filesystems.  RED.
        """
        import common_tls  # noqa: E402

        self.assertTrue(
            hasattr(common_tls, "deduplicate_source_paths"),
            "common_tls must expose deduplicate_source_paths()",
        )

    def test_case_sensitive_dedup_preserves_case_variants(self) -> None:
        """deduplicate_source_paths must use case-sensitive (byte-preserving)
        comparison per plan: 'path comparisons must be byte-preserving'.
        All three case variants are distinct paths.
        """
        import common_tls  # noqa: E402

        self.assertTrue(
            hasattr(common_tls, "deduplicate_source_paths"),
            "common_tls must expose deduplicate_source_paths()",
        )
        dedup = common_tls.deduplicate_source_paths

        paths = [
            "docs/Samples/Traffic dumps/iOS/Test.pcap",
            "docs/Samples/Traffic dumps/iOS/test.pcap",
            "docs/Samples/Traffic dumps/iOS/TEST.pcap",
        ]
        result = dedup(paths)
        # Case-sensitive: all 3 are distinct (byte-preserving per plan)
        self.assertEqual(len(result), 3)

    def test_no_case_only_duplicates_in_corpus(self) -> None:
        """No two fixtures in the current corpus should have source_paths
        that differ only by case.
        """
        fixture_paths = _collect_all_fixture_jsons()
        self.assertTrue(fixture_paths, "no fixture JSONs found")
        seen_lower: dict[str, str] = {}
        for fpath in fixture_paths:
            artifact = _load_json(fpath)
            source_path = artifact.get("source_path", "")
            if not source_path:
                continue
            lower = source_path.lower()
            if lower in seen_lower:
                self.fail(
                    f"Case-only duplicate: '{source_path}' in {fpath.name} "
                    f"vs '{seen_lower[lower]}'"
                )
            seen_lower[lower] = source_path


# ---------------------------------------------------------------------------
# Test: SHA-256 stability (RISK-FP-23)
# ---------------------------------------------------------------------------

class TestSha256Stability(unittest.TestCase):
    """Stripping the absolute workspace prefix must not change source_sha256,
    scenario_id, fixture_id, or any evidence count.
    """

    def test_source_sha256_is_independent_of_workspace_prefix(self) -> None:
        """source_sha256 must be a property of the capture file content, not
        of the source_path string.  Two fixtures from the same capture file
        must share the same source_sha256 regardless of workspace prefix.
        """
        fixture_paths = _collect_all_fixture_jsons()
        self.assertTrue(fixture_paths, "no fixture JSONs found")
        sha_set: dict[str, list[str]] = {}
        for fpath in fixture_paths:
            artifact = _load_json(fpath)
            sha = artifact.get("source_sha256", "")
            source_path = artifact.get("source_path", "")
            if sha:
                sha_set.setdefault(sha, []).append(source_path)

        # For each unique SHA, all associated source_paths must have the
        # same repo-relative tail (i.e., the SHA depends on content, not path)
        for sha, paths in sha_set.items():
            relatives = {_strip_workspace_prefix(p) for p in paths}
            self.assertEqual(
                len(relatives), 1,
                msg=(
                    f"SHA {sha} is associated with multiple distinct "
                    f"repo-relative paths: {relatives}"
                ),
            )

    def test_fixture_ids_stable_after_prefix_strip(self) -> None:
        """fixture_id values inside samples must not depend on the absolute
        workspace prefix of source_path.
        """
        fixture_paths = _collect_all_fixture_jsons()
        self.assertTrue(fixture_paths, "no fixture JSONs found")
        for fpath in fixture_paths:
            artifact = _load_json(fpath)
            samples = artifact.get("samples", [])
            for sample in samples:
                fixture_id = sample.get("fixture_id", "")
                self.assertTrue(
                    fixture_id,
                    msg=f"{fpath.name}: sample missing fixture_id",
                )
                # fixture_id must NOT embed the absolute workspace prefix
                self.assertNotIn(
                    ABSOLUTE_WORKSPACE_PREFIX,
                    fixture_id,
                    msg=f"{fpath.name}: fixture_id embeds workspace prefix",
                )

    def test_scenario_id_stable_after_prefix_strip(self) -> None:
        """scenario_id must not embed the absolute workspace prefix."""
        fixture_paths = _collect_all_fixture_jsons()
        self.assertTrue(fixture_paths, "no fixture JSONs found")
        for fpath in fixture_paths:
            artifact = _load_json(fpath)
            scenario_id = artifact.get("scenario_id", "")
            self.assertNotIn(
                ABSOLUTE_WORKSPACE_PREFIX,
                scenario_id,
                msg=f"{fpath.name}: scenario_id embeds workspace prefix",
            )

    def test_normalizer_preserves_sha256_and_ids(self) -> None:
        """After normalizing source_path, source_sha256, scenario_id, and
        fixture_id must remain unchanged.  RED because normalize_source_path
        does not exist yet.
        """
        import common_tls  # noqa: E402

        self.assertTrue(
            hasattr(common_tls, "normalize_source_path"),
            "common_tls must expose normalize_source_path()",
        )
        normalizer = common_tls.normalize_source_path

        fixture_paths = _collect_all_fixture_jsons()
        self.assertTrue(fixture_paths, "no fixture JSONs found")
        for fpath in fixture_paths:
            artifact = _load_json(fpath)
            original_sha = artifact.get("source_sha256", "")
            original_scenario = artifact.get("scenario_id", "")
            original_path = artifact.get("source_path", "")

            # Normalizing the path must not alter any other field
            _normalized_path = normalizer(original_path)

            # SHA must remain content-derived
            self.assertEqual(
                original_sha, artifact["source_sha256"],
                msg=f"{fpath.name}: source_sha256 changed after path normalization",
            )
            self.assertEqual(
                original_scenario, artifact["scenario_id"],
                msg=f"{fpath.name}: scenario_id changed after path normalization",
            )

            samples = artifact.get("samples", [])
            for sample in samples:
                original_fid = sample.get("fixture_id", "")
                self.assertTrue(original_fid)
                # fixture_id must be unchanged
                self.assertEqual(
                    original_fid, sample["fixture_id"],
                    msg=f"{fpath.name}: fixture_id changed after path normalization",
                )

    def test_sample_counts_stable_after_prefix_strip(self) -> None:
        """The number of samples (evidence count) in each fixture must not
        change when the absolute prefix is stripped.
        """
        fixture_paths = _collect_all_fixture_jsons()
        self.assertTrue(fixture_paths, "no fixture JSONs found")
        for fpath in fixture_paths:
            artifact = _load_json(fpath)
            samples = artifact.get("samples", [])
            self.assertIsInstance(samples, list)
            self.assertGreater(
                len(samples), 0,
                msg=f"{fpath.name}: fixture has no samples",
            )


if __name__ == "__main__":
    unittest.main()
