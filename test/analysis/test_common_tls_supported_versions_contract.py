#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

"""Contract tests for parse_supported_versions() and the
non_grease_supported_versions field on ClientHello.

These are RED tests -- they define the contract for a canonical
``parse_supported_versions()`` function and the corresponding
``non_grease_supported_versions`` dataclass field that do **not** exist
yet in ``common_tls.py``.  Every test in this module MUST fail against
current HEAD; that is the proof that the feature gap is real.

RISKS COVERED: RISK-FP-01, RISK-FP-02, RISK-FP-09, RISK-FP-19
"""

from __future__ import annotations

import json
import pathlib
import sys
import unittest

THIS_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = THIS_DIR.resolve().parents[1]
FIXTURES_ROOT = THIS_DIR / "fixtures" / "clienthello"

if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))

# ---------------------------------------------------------------------------
# GREASE constants (RFC 8701)
# Values of the form 0x?A?A where the two nibbles match:
#   0x0A0A, 0x1A1A, 0x2A2A, ..., 0xFAFA
# ---------------------------------------------------------------------------
GREASE_VALUES: frozenset[int] = frozenset(
    (val << 8) | val for val in range(0x0A, 0xFB, 0x10)
)


def _is_grease(value: int) -> bool:
    """Return True if *value* is a TLS GREASE sentinel (RFC 8701)."""
    return value in GREASE_VALUES


# ---------------------------------------------------------------------------
# Reference parser -- standalone implementation used to validate the
# expected values are correct.  This is what common_tls.parse_supported_versions
# SHOULD do once implemented.
# ---------------------------------------------------------------------------
def _reference_parse_supported_versions(body_hex: str) -> list[int]:
    """Parse the supported_versions extension body (0x002B) and return the
    list of non-GREASE protocol version u16 values, preserving order.

    The body format (client-side) is:
        1 byte   -- length of the version list in bytes
        N bytes  -- concatenated 2-byte version identifiers

    Raises ``ValueError`` on any malformed input.
    """
    if not body_hex:
        raise ValueError("empty body_hex")
    if len(body_hex) % 2 != 0:
        raise ValueError("odd-length hex string")
    try:
        raw = bytes.fromhex(body_hex)
    except ValueError as exc:
        raise ValueError(f"non-hex characters in body_hex: {exc}") from exc
    if len(raw) < 1:
        raise ValueError("body too short for length prefix")
    declared_length = raw[0]
    payload = raw[1:]
    if declared_length != len(payload):
        raise ValueError(
            f"length prefix mismatch: declared {declared_length}, "
            f"actual {len(payload)}"
        )
    if declared_length % 2 != 0:
        raise ValueError("version list length is not a multiple of 2")
    versions: list[int] = []
    for offset in range(0, len(payload), 2):
        version = (payload[offset] << 8) | payload[offset + 1]
        if not _is_grease(version):
            versions.append(version)
    return versions


# ---------------------------------------------------------------------------
# Fixture anchor expectations
# ---------------------------------------------------------------------------

# Maps fixture filename -> (0x002B body_hex, expected non-GREASE versions)
IOS_ANCHOR_FIXTURES: dict[str, tuple[str, list[int]]] = {
    "safari26_3_1_ios26_3_1_a.clienthello.json": (
        "063a3a03040303",
        [0x0304, 0x0303],
    ),
    "chrome147_0_7727_47_ios26_4_a.clienthello.json": (
        "062a2a03040303",
        [0x0304, 0x0303],
    ),
    "safari17_2_ios17_2_1_087f3601.clienthello.json": (
        "0a3a3a0304030303020301",
        [0x0304, 0x0303, 0x0302, 0x0301],
    ),
    "safari18_7_6_ios18_7_6.clienthello.json": (
        "0afafa0304030303020301",
        [0x0304, 0x0303, 0x0302, 0x0301],
    ),
}


# ===================================================================
# Test cases
# ===================================================================


class TestReferenceParserSanity(unittest.TestCase):
    """Validate our standalone reference parser against known fixture data."""

    def test_reference_parser_safari26_3_1(self) -> None:
        result = _reference_parse_supported_versions("063a3a03040303")
        self.assertEqual(result, [0x0304, 0x0303])

    def test_reference_parser_chrome147(self) -> None:
        result = _reference_parse_supported_versions("062a2a03040303")
        self.assertEqual(result, [0x0304, 0x0303])

    def test_reference_parser_safari17_2(self) -> None:
        result = _reference_parse_supported_versions("0a3a3a0304030303020301")
        self.assertEqual(result, [0x0304, 0x0303, 0x0302, 0x0301])

    def test_reference_parser_safari18_7_6(self) -> None:
        result = _reference_parse_supported_versions("0afafa0304030303020301")
        self.assertEqual(result, [0x0304, 0x0303, 0x0302, 0x0301])

    def test_reference_parser_rejects_empty_body(self) -> None:
        with self.assertRaises(ValueError):
            _reference_parse_supported_versions("")

    def test_reference_parser_rejects_odd_length_hex(self) -> None:
        with self.assertRaises(ValueError):
            _reference_parse_supported_versions("063a3a030403030")

    def test_reference_parser_rejects_length_mismatch(self) -> None:
        # Declared length 0x08 but only 6 payload bytes follow
        with self.assertRaises(ValueError):
            _reference_parse_supported_versions("083a3a03040303")

    def test_reference_parser_rejects_truncated_body(self) -> None:
        # Declared length 0x0a but only 4 payload bytes follow
        with self.assertRaises(ValueError):
            _reference_parse_supported_versions("0a3a3a0304")

    def test_reference_parser_rejects_non_hex(self) -> None:
        with self.assertRaises(ValueError):
            _reference_parse_supported_versions("06ZZZZ03040303")

    def test_reference_parser_all_grease_returns_empty(self) -> None:
        # Body with only GREASE versions: length=4, two GREASE values
        result = _reference_parse_supported_versions("040a0a1a1a")
        self.assertEqual(result, [])


class TestParseSupportedVersionsExistence(unittest.TestCase):
    """RED: parse_supported_versions must be importable from common_tls.

    This test MUST fail at current HEAD because the function does not
    exist yet.
    """

    def test_parse_supported_versions_importable(self) -> None:
        """common_tls must export parse_supported_versions."""
        import common_tls

        self.assertTrue(
            hasattr(common_tls, "parse_supported_versions"),
            "common_tls.py must export parse_supported_versions(); "
            "this function does not exist yet (RED test)",
        )

    def test_parse_supported_versions_is_callable(self) -> None:
        """parse_supported_versions must be callable once it exists."""
        import common_tls

        fn = getattr(common_tls, "parse_supported_versions", None)
        self.assertIsNotNone(
            fn,
            "parse_supported_versions not found in common_tls (RED test)",
        )
        self.assertTrue(
            callable(fn),
            "parse_supported_versions must be callable",
        )


class TestClientHelloNonGreaseSupportedVersionsField(unittest.TestCase):
    """RED: ClientHello dataclass must have a non_grease_supported_versions
    field populated by the loader.

    This test MUST fail at current HEAD because the field does not exist
    on the ClientHello dataclass (lines 54-64 of common_tls.py).
    """

    def test_field_exists_on_dataclass(self) -> None:
        """The ClientHello dataclass must declare non_grease_supported_versions."""
        from common_tls import ClientHello

        import dataclasses

        field_names = {f.name for f in dataclasses.fields(ClientHello)}
        self.assertIn(
            "non_grease_supported_versions",
            field_names,
            "ClientHello must have a non_grease_supported_versions field; "
            "it is currently missing (RED test)",
        )

    def test_field_populated_on_loaded_fixture(self) -> None:
        """Loaded ClientHello objects must carry non_grease_supported_versions."""
        from common_tls import load_clienthello_artifact

        fixture_path = (
            FIXTURES_ROOT
            / "ios"
            / "safari26_3_1_ios26_3_1_a.clienthello.json"
        )
        if not fixture_path.exists():
            self.skipTest(f"fixture not found: {fixture_path}")

        samples = load_clienthello_artifact(fixture_path)
        self.assertTrue(len(samples) > 0, "fixture must have at least one sample")
        sample = samples[0]
        self.assertTrue(
            hasattr(sample, "non_grease_supported_versions"),
            "loaded ClientHello must have non_grease_supported_versions attribute "
            "(RED test -- field not yet added)",
        )

    def test_field_value_matches_contract_safari26(self) -> None:
        """non_grease_supported_versions for Safari 26.3.1 must be [0x0304, 0x0303]."""
        from common_tls import load_clienthello_artifact

        fixture_path = (
            FIXTURES_ROOT
            / "ios"
            / "safari26_3_1_ios26_3_1_a.clienthello.json"
        )
        if not fixture_path.exists():
            self.skipTest(f"fixture not found: {fixture_path}")

        samples = load_clienthello_artifact(fixture_path)
        sample = samples[0]
        versions = getattr(sample, "non_grease_supported_versions", None)
        self.assertIsNotNone(
            versions,
            "non_grease_supported_versions must not be None (RED test)",
        )
        self.assertEqual(
            versions,
            [0x0304, 0x0303],
            "Safari 26.3.1 non_grease_supported_versions must be [0x0304, 0x0303]",
        )

    def test_field_value_matches_contract_chrome147(self) -> None:
        """non_grease_supported_versions for Chrome 147 must be [0x0304, 0x0303]."""
        from common_tls import load_clienthello_artifact

        fixture_path = (
            FIXTURES_ROOT
            / "ios"
            / "chrome147_0_7727_47_ios26_4_a.clienthello.json"
        )
        if not fixture_path.exists():
            self.skipTest(f"fixture not found: {fixture_path}")

        samples = load_clienthello_artifact(fixture_path)
        sample = samples[0]
        versions = getattr(sample, "non_grease_supported_versions", None)
        self.assertIsNotNone(
            versions,
            "non_grease_supported_versions must not be None (RED test)",
        )
        self.assertEqual(
            versions,
            [0x0304, 0x0303],
            "Chrome 147 non_grease_supported_versions must be [0x0304, 0x0303]",
        )

    def test_field_value_matches_contract_safari17_2(self) -> None:
        """non_grease_supported_versions for Safari 17.2 must be
        [0x0304, 0x0303, 0x0302, 0x0301]."""
        from common_tls import load_clienthello_artifact

        fixture_path = (
            FIXTURES_ROOT
            / "ios"
            / "safari17_2_ios17_2_1_087f3601.clienthello.json"
        )
        if not fixture_path.exists():
            self.skipTest(f"fixture not found: {fixture_path}")

        samples = load_clienthello_artifact(fixture_path)
        sample = samples[0]
        versions = getattr(sample, "non_grease_supported_versions", None)
        self.assertIsNotNone(
            versions,
            "non_grease_supported_versions must not be None (RED test)",
        )
        self.assertEqual(
            versions,
            [0x0304, 0x0303, 0x0302, 0x0301],
            "Safari 17.2 non_grease_supported_versions must be "
            "[0x0304, 0x0303, 0x0302, 0x0301]",
        )

    def test_field_value_matches_contract_safari18_7_6(self) -> None:
        """non_grease_supported_versions for Safari 18.7.6 must be
        [0x0304, 0x0303, 0x0302, 0x0301]."""
        from common_tls import load_clienthello_artifact

        fixture_path = (
            FIXTURES_ROOT
            / "ios"
            / "safari18_7_6_ios18_7_6.clienthello.json"
        )
        if not fixture_path.exists():
            self.skipTest(f"fixture not found: {fixture_path}")

        samples = load_clienthello_artifact(fixture_path)
        sample = samples[0]
        versions = getattr(sample, "non_grease_supported_versions", None)
        self.assertIsNotNone(
            versions,
            "non_grease_supported_versions must not be None (RED test)",
        )
        self.assertEqual(
            versions,
            [0x0304, 0x0303, 0x0302, 0x0301],
            "Safari 18.7.6 non_grease_supported_versions must be "
            "[0x0304, 0x0303, 0x0302, 0x0301]",
        )


class TestParseSupportedVersionsParserContract(unittest.TestCase):
    """RED: parse_supported_versions() must match the reference parser on all
    four iOS anchor fixtures.

    These tests will fail because the function does not exist yet.
    """

    def _get_parser(self):
        """Return parse_supported_versions from common_tls or skip."""
        import common_tls

        fn = getattr(common_tls, "parse_supported_versions", None)
        if fn is None:
            self.fail(
                "parse_supported_versions does not exist in common_tls "
                "(RED test -- function not yet implemented)"
            )
        return fn

    def test_safari26_3_1_body(self) -> None:
        parse = self._get_parser()
        result = parse("063a3a03040303")
        self.assertEqual(result, [0x0304, 0x0303])

    def test_chrome147_body(self) -> None:
        parse = self._get_parser()
        result = parse("062a2a03040303")
        self.assertEqual(result, [0x0304, 0x0303])

    def test_safari17_2_body(self) -> None:
        parse = self._get_parser()
        result = parse("0a3a3a0304030303020301")
        self.assertEqual(result, [0x0304, 0x0303, 0x0302, 0x0301])

    def test_safari18_7_6_body(self) -> None:
        parse = self._get_parser()
        result = parse("0afafa0304030303020301")
        self.assertEqual(result, [0x0304, 0x0303, 0x0302, 0x0301])


class TestParseSupportedVersionsGreaseRemoval(unittest.TestCase):
    """RED: parse_supported_versions() must strip all GREASE values while
    preserving the order of non-GREASE entries.
    """

    def _get_parser(self):
        import common_tls

        fn = getattr(common_tls, "parse_supported_versions", None)
        if fn is None:
            self.fail(
                "parse_supported_versions does not exist in common_tls "
                "(RED test -- function not yet implemented)"
            )
        return fn

    def test_grease_0x3a3a_filtered(self) -> None:
        """0x3A3A is GREASE and must not appear in result."""
        parse = self._get_parser()
        result = parse("063a3a03040303")
        for version in result:
            self.assertFalse(
                _is_grease(version),
                f"GREASE value 0x{version:04X} must not appear in parsed result",
            )

    def test_grease_0x2a2a_filtered(self) -> None:
        """0x2A2A is GREASE and must not appear in result."""
        parse = self._get_parser()
        result = parse("062a2a03040303")
        for version in result:
            self.assertFalse(
                _is_grease(version),
                f"GREASE value 0x{version:04X} must not appear in parsed result",
            )

    def test_grease_0xfafa_filtered(self) -> None:
        """0xFAFA is GREASE and must not appear in result."""
        parse = self._get_parser()
        result = parse("0afafa0304030303020301")
        for version in result:
            self.assertFalse(
                _is_grease(version),
                f"GREASE value 0x{version:04X} must not appear in parsed result",
            )

    def test_all_16_grease_values_filtered(self) -> None:
        """Every RFC 8701 GREASE value must be recognized and stripped."""
        parse = self._get_parser()
        # Construct a body with all 16 GREASE values plus TLS 1.3
        grease_sorted = sorted(GREASE_VALUES)
        payload_versions = grease_sorted + [0x0304]
        payload_bytes = b""
        for v in payload_versions:
            payload_bytes += v.to_bytes(2, "big")
        length_byte = len(payload_bytes).to_bytes(1, "big")
        body_hex = (length_byte + payload_bytes).hex()
        result = parse(body_hex)
        self.assertEqual(
            result,
            [0x0304],
            "Only TLS 1.3 (0x0304) should remain after stripping all GREASE values",
        )

    def test_order_preserved_after_grease_removal(self) -> None:
        """Non-GREASE versions must retain their original order."""
        parse = self._get_parser()
        result = parse("0afafa0304030303020301")
        self.assertEqual(
            result,
            [0x0304, 0x0303, 0x0302, 0x0301],
            "Order must be preserved: TLS 1.3, 1.2, 1.1, 1.0",
        )


class TestParseSupportedVersionsNegativeCases(unittest.TestCase):
    """RED: parse_supported_versions() must fail closed on malformed inputs.

    All malformed 0x002B bodies must raise ValueError (or a subclass).
    """

    def _get_parser(self):
        import common_tls

        fn = getattr(common_tls, "parse_supported_versions", None)
        if fn is None:
            self.fail(
                "parse_supported_versions does not exist in common_tls "
                "(RED test -- function not yet implemented)"
            )
        return fn

    def test_empty_body_raises(self) -> None:
        parse = self._get_parser()
        with self.assertRaises(ValueError):
            parse("")

    def test_odd_length_hex_raises(self) -> None:
        parse = self._get_parser()
        with self.assertRaises(ValueError):
            parse("063a3a0304030")

    def test_length_prefix_mismatch_raises(self) -> None:
        """Declared length does not match actual payload."""
        parse = self._get_parser()
        # Declared 0x08 (8 bytes) but only 6 bytes follow
        with self.assertRaises(ValueError):
            parse("083a3a03040303")

    def test_truncated_body_raises(self) -> None:
        """Declared length exceeds actual payload."""
        parse = self._get_parser()
        # Declared 0x0a (10 bytes) but only 4 bytes follow
        with self.assertRaises(ValueError):
            parse("0a3a3a0304")

    def test_non_hex_characters_raises(self) -> None:
        parse = self._get_parser()
        with self.assertRaises(ValueError):
            parse("06ZZZZ03040303")

    def test_only_length_byte_no_payload_raises(self) -> None:
        """A body with just the length byte (0x02) but no versions is invalid
        because declared length (2) != actual payload length (0)."""
        parse = self._get_parser()
        with self.assertRaises(ValueError):
            parse("02")

    def test_odd_payload_length_raises(self) -> None:
        """Declared payload length that is not a multiple of 2 is invalid."""
        parse = self._get_parser()
        # Length prefix = 3 (odd), followed by 3 bytes
        with self.assertRaises(ValueError):
            parse("03030403")


class TestInferTlsGenReplacedByProperParsing(unittest.TestCase):
    """RED: The substring-match hack in _infer_tls_gen() at common_tls.py
    line 178 (``"0304" in body_hex``) must be replaced by proper parsing
    via parse_supported_versions().

    This test verifies that _infer_tls_gen no longer uses a naive substring
    check and instead delegates to the canonical parser.
    """

    def test_infer_tls_gen_does_not_use_substring_match(self) -> None:
        """_infer_tls_gen must NOT rely on substring matching for the
        supported_versions extension body.

        We construct a synthetic sample where "0304" appears in a GREASE
        context but is NOT actually a TLS 1.3 version.  The substring hack
        would incorrectly classify this as tls13.

        Specifically: body_hex "020304" has length prefix 0x02 (2 bytes),
        followed by the single version 0x0304.  That IS TLS 1.3 -- so we
        need a subtler test.

        Instead, use body_hex where "0304" appears as part of a GREASE value
        or as a fragment that does not represent version 0x0304.  The body
        "040a040304" has length 4, versions [0x0A04, 0x0304].  0x0A04 is NOT
        GREASE (GREASE is 0x0A0A).  After proper parsing, 0x0304 IS present
        so tls13 is correct.

        The definitive test: a body where 0x0304 appears in the hex string
        but NOT as a parsed version.  Body "0203040303" -- wait, that is
        ambiguous.

        Best approach: verify that common_tls no longer has the substring
        check by inspecting the source.
        """
        import inspect
        import common_tls

        source = inspect.getsource(common_tls._infer_tls_gen)
        # The current hack is: if "0304" in body_hex.lower():
        # After the fix, this substring check must be gone and replaced
        # by a call to parse_supported_versions.
        self.assertNotIn(
            '"0304" in body_hex',
            source,
            '_infer_tls_gen still uses the substring hack '
            '``"0304" in body_hex`` -- it must delegate to '
            "parse_supported_versions() instead (RED test)",
        )

    def test_infer_tls_gen_uses_parse_supported_versions(self) -> None:
        """_infer_tls_gen source must reference parse_supported_versions."""
        import inspect
        import common_tls

        source = inspect.getsource(common_tls._infer_tls_gen)
        self.assertIn(
            "parse_supported_versions",
            source,
            "_infer_tls_gen must call parse_supported_versions() "
            "instead of doing substring matching (RED test)",
        )


class TestFixtureBodyHexMatchesExpectations(unittest.TestCase):
    """Verify that the raw 0x002B body_hex in each iOS anchor fixture
    matches the values encoded in our test expectations.

    These tests pass at current HEAD (they read the JSON fixtures directly).
    They serve as the ground truth anchor so that the parser contract tests
    above are testing against verified data.
    """

    def _get_supported_versions_body_hex(self, fixture_path: pathlib.Path) -> list[str]:
        """Load a fixture and return body_hex values for all 0x002B extensions
        across all samples."""
        with fixture_path.open("r", encoding="utf-8") as fh:
            data = json.load(fh)
        bodies: list[str] = []
        for sample in data.get("samples", []):
            for ext in sample.get("extensions", []):
                if str(ext.get("type", "")).lower() == "0x002b":
                    bodies.append(ext.get("body_hex", ""))
        return bodies

    def test_safari26_3_1_body_hex(self) -> None:
        path = FIXTURES_ROOT / "ios" / "safari26_3_1_ios26_3_1_a.clienthello.json"
        if not path.exists():
            self.skipTest(f"fixture not found: {path}")
        bodies = self._get_supported_versions_body_hex(path)
        self.assertTrue(len(bodies) > 0, "fixture must have 0x002B extension")
        self.assertEqual(bodies[0], "063a3a03040303")

    def test_chrome147_body_hex(self) -> None:
        path = FIXTURES_ROOT / "ios" / "chrome147_0_7727_47_ios26_4_a.clienthello.json"
        if not path.exists():
            self.skipTest(f"fixture not found: {path}")
        bodies = self._get_supported_versions_body_hex(path)
        self.assertTrue(len(bodies) > 0, "fixture must have 0x002B extension")
        self.assertEqual(bodies[0], "062a2a03040303")

    def test_safari17_2_body_hex(self) -> None:
        path = FIXTURES_ROOT / "ios" / "safari17_2_ios17_2_1_087f3601.clienthello.json"
        if not path.exists():
            self.skipTest(f"fixture not found: {path}")
        bodies = self._get_supported_versions_body_hex(path)
        self.assertTrue(len(bodies) > 0, "fixture must have 0x002B extension")
        self.assertEqual(bodies[0], "0a3a3a0304030303020301")

    def test_safari18_7_6_body_hex(self) -> None:
        path = FIXTURES_ROOT / "ios" / "safari18_7_6_ios18_7_6.clienthello.json"
        if not path.exists():
            self.skipTest(f"fixture not found: {path}")
        bodies = self._get_supported_versions_body_hex(path)
        self.assertTrue(len(bodies) > 0, "fixture must have 0x002B extension")
        self.assertEqual(bodies[0], "0afafa0304030303020301")


if __name__ == "__main__":
    unittest.main()
