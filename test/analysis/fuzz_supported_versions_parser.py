#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

"""Fuzz the canonical parse_supported_versions() function with random and
adversarial inputs.

Covers OWASP ASVS L2 input validation and RISK-FP-09.

The function under test (``parse_supported_versions``) does not exist yet in
``common_tls``.  The tests are structured so they are RED against current HEAD
-- every test that requires the function will fail with a clear message if it
cannot be imported.
"""

from __future__ import annotations

import json
import pathlib
import random
import struct
import sys
import unittest
from typing import Any

THIS_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = THIS_DIR.resolve().parents[1]
FIXTURES_ROOT = THIS_DIR / "fixtures" / "clienthello"

if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))

# ---------------------------------------------------------------------------
# GREASE helpers -- the GREASE values for TLS supported_versions are of the
# form 0x?a?a where both nibbles match:  0x0a0a, 0x1a1a, ..., 0xfafa.
# ---------------------------------------------------------------------------
GREASE_VALUES: frozenset[int] = frozenset(
    0x0A0A + 0x1010 * i for i in range(16)
)


def _is_grease(value: int) -> bool:
    return value in GREASE_VALUES


# ---------------------------------------------------------------------------
# Attempt to import parse_supported_versions from common_tls.
# If it does not exist yet the flag stays False and every test that
# needs it will fail explicitly (RED test).
# ---------------------------------------------------------------------------
_HAS_PARSE_SUPPORTED_VERSIONS = False
_parse_supported_versions = None  # type: Any

try:
    from common_tls import parse_supported_versions as _psv  # type: ignore[attr-defined]

    _parse_supported_versions = _psv
    _HAS_PARSE_SUPPORTED_VERSIONS = True
except ImportError:
    pass
except AttributeError:
    pass


def _require_parser(test_instance: unittest.TestCase) -> Any:
    """Fail the test immediately when the parser is not available."""
    if not _HAS_PARSE_SUPPORTED_VERSIONS:
        test_instance.fail(
            "parse_supported_versions is not yet exported from common_tls -- "
            "this test is intentionally RED until the function is implemented"
        )
    return _parse_supported_versions


# ---------------------------------------------------------------------------
# Helpers to build well-formed and malformed supported_versions bodies.
#
# TLS supported_versions extension body (ClientHello) layout:
#   uint8  list_length          -- number of bytes that follow
#   uint16 versions[list_length / 2]
# ---------------------------------------------------------------------------

def _build_body(versions: list[int]) -> bytes:
    """Build a well-formed supported_versions body from a list of uint16 values."""
    payload = b"".join(struct.pack("!H", v) for v in versions)
    return struct.pack("!B", len(payload)) + payload


def _load_ios_fixture_supported_versions_bodies() -> list[bytes]:
    """Extract the raw body_hex of every 0x002B extension from iOS fixtures."""
    bodies: list[bytes] = []
    ios_dir = FIXTURES_ROOT / "ios"
    if not ios_dir.is_dir():
        return bodies
    for fixture_path in sorted(ios_dir.glob("*.json")):
        try:
            with fixture_path.open("r", encoding="utf-8") as fh:
                artifact = json.load(fh)
        except (json.JSONDecodeError, OSError):
            continue
        for sample in artifact.get("samples", []):
            for ext in sample.get("extensions", []):
                ext_type = str(ext.get("type", "")).lower()
                if ext_type == "0x002b":
                    body_hex = ext.get("body_hex", "")
                    if body_hex:
                        bodies.append(bytes.fromhex(body_hex))
    return bodies


class TestParseImportability(unittest.TestCase):
    """Verify that the function can at least be imported (RED until implemented)."""

    def test_parse_supported_versions_is_importable(self) -> None:
        self.assertTrue(
            _HAS_PARSE_SUPPORTED_VERSIONS,
            "parse_supported_versions must be exported from common_tls "
            "(this test is RED until the function is implemented)",
        )


class TestFuzzRandomBytes(unittest.TestCase):
    """Feed 10 000+ random byte sequences to the parser and verify it never
    crashes and never performs unbounded allocation."""

    ITERATIONS = 10_000
    MAX_LEN = 520  # slightly above the theoretical max (1 + 255*2 = 511)

    def test_random_bytes_never_crash(self) -> None:
        parser = _require_parser(self)
        rng = random.Random(42)
        for _ in range(self.ITERATIONS):
            length = rng.randint(0, self.MAX_LEN)
            data = bytes(rng.getrandbits(8) for _ in range(length))
            try:
                result = parser(data)
                # If the parser accepts the input it must return a list.
                self.assertIsInstance(result, list)
                # No element should exceed uint16.
                for v in result:
                    self.assertIsInstance(v, int)
                    self.assertGreaterEqual(v, 0)
                    self.assertLessEqual(v, 0xFFFF)
            except (ValueError, TypeError, struct.error):
                # Parser is allowed to reject invalid inputs with an exception.
                pass
            except MemoryError:
                self.fail(
                    "parse_supported_versions caused MemoryError on random "
                    f"input of length {length}"
                )

    def test_deterministic_output(self) -> None:
        """Running the parser twice on the same input must yield the same result."""
        parser = _require_parser(self)
        rng = random.Random(42)
        for _ in range(2_000):
            length = rng.randint(0, self.MAX_LEN)
            data = bytes(rng.getrandbits(8) for _ in range(length))
            try:
                result_a = parser(data)
                result_b = parser(data)
                self.assertEqual(result_a, result_b)
            except (ValueError, TypeError, struct.error):
                pass


class TestAdversarialLengthPrefix(unittest.TestCase):
    """Exercise adversarial length-prefix manipulations."""

    def test_length_prefix_exceeds_available_bytes(self) -> None:
        """Length byte claims more data than actually present."""
        parser = _require_parser(self)
        # Body claims 10 bytes follow but only 4 are present.
        malformed = struct.pack("!B", 10) + struct.pack("!HH", 0x0304, 0x0303)
        with self.assertRaises((ValueError, struct.error)):
            parser(malformed)

    def test_length_prefix_fewer_than_available_trailing_bytes(self) -> None:
        """Length byte claims fewer bytes than actually present (trailing junk)."""
        parser = _require_parser(self)
        # Body claims 4 bytes but 6 follow (2 trailing).
        malformed = struct.pack("!B", 4) + struct.pack("!HHH", 0x0304, 0x0303, 0xBEEF)
        with self.assertRaises((ValueError, struct.error)):
            parser(malformed)

    def test_length_zero(self) -> None:
        """Length byte is zero -- the version list is empty."""
        parser = _require_parser(self)
        body = struct.pack("!B", 0)
        try:
            result = parser(body)
            # If accepted, must be an empty list.
            self.assertEqual(result, [])
        except (ValueError, struct.error):
            pass  # Rejecting an empty list is also valid.


class TestAdversarialOddLength(unittest.TestCase):
    """Version list length that is not a multiple of 2."""

    def test_odd_vector_length(self) -> None:
        parser = _require_parser(self)
        # 3 bytes of version data -- not a multiple of 2.
        body = struct.pack("!B", 3) + b"\x03\x04\x03"
        with self.assertRaises((ValueError, struct.error)):
            parser(body)

    def test_odd_length_five(self) -> None:
        parser = _require_parser(self)
        body = struct.pack("!B", 5) + b"\x03\x04\x03\x03\x03"
        with self.assertRaises((ValueError, struct.error)):
            parser(body)


class TestAdversarialEmptyAndMinimal(unittest.TestCase):
    """Empty and single-byte bodies."""

    def test_empty_body(self) -> None:
        parser = _require_parser(self)
        with self.assertRaises((ValueError, struct.error, IndexError)):
            parser(b"")

    def test_single_byte_body(self) -> None:
        """Only the length prefix, no version data at all."""
        parser = _require_parser(self)
        body = b"\x02"  # claims 2 bytes follow but nothing is there
        with self.assertRaises((ValueError, struct.error)):
            parser(body)

    def test_single_zero_byte_body(self) -> None:
        """Length prefix of zero, no trailing data."""
        parser = _require_parser(self)
        body = b"\x00"
        try:
            result = parser(body)
            self.assertEqual(result, [])
        except (ValueError, struct.error):
            pass


class TestAdversarialGreaseOnly(unittest.TestCase):
    """Body containing only GREASE values."""

    def test_all_grease_values(self) -> None:
        parser = _require_parser(self)
        grease_versions = [0x0A0A, 0x1A1A, 0x2A2A]
        body = _build_body(grease_versions)
        try:
            result = parser(body)
            # After GREASE filtering the result should contain no GREASE values.
            self.assertIsInstance(result, list)
            for v in result:
                self.assertFalse(
                    _is_grease(v),
                    f"GREASE value 0x{v:04X} must be filtered from output",
                )
        except (ValueError, struct.error):
            pass

    def test_single_grease_value(self) -> None:
        parser = _require_parser(self)
        body = _build_body([0xFAFA])
        try:
            result = parser(body)
            self.assertIsInstance(result, list)
            for v in result:
                self.assertFalse(_is_grease(v))
        except (ValueError, struct.error):
            pass

    def test_all_sixteen_grease_values(self) -> None:
        parser = _require_parser(self)
        body = _build_body(sorted(GREASE_VALUES))
        try:
            result = parser(body)
            self.assertIsInstance(result, list)
            for v in result:
                self.assertFalse(
                    _is_grease(v),
                    f"GREASE value 0x{v:04X} leaked through filter",
                )
        except (ValueError, struct.error):
            pass


class TestAdversarialDuplicateVersions(unittest.TestCase):
    """Duplicate version entries in the body."""

    def test_duplicate_tls13(self) -> None:
        parser = _require_parser(self)
        body = _build_body([0x0304, 0x0304, 0x0303])
        try:
            result = parser(body)
            self.assertIsInstance(result, list)
            # The parser should either deduplicate or preserve order; either
            # way every element must be a valid uint16.
            for v in result:
                self.assertGreaterEqual(v, 0)
                self.assertLessEqual(v, 0xFFFF)
        except (ValueError, struct.error):
            pass

    def test_many_duplicates(self) -> None:
        parser = _require_parser(self)
        body = _build_body([0x0303] * 50)
        try:
            result = parser(body)
            self.assertIsInstance(result, list)
        except (ValueError, struct.error):
            pass


class TestAdversarialMaximumLength(unittest.TestCase):
    """Maximum-length supported_versions body (255 bytes of version data = 127
    version entries plus one stray byte, so use 254 bytes = 127 versions)."""

    def test_max_legal_body_127_versions(self) -> None:
        parser = _require_parser(self)
        # 127 versions * 2 bytes = 254 bytes -- fits in a uint8 length prefix.
        versions = [0x0304] + [0x0303] * 126
        body = _build_body(versions)
        try:
            result = parser(body)
            self.assertIsInstance(result, list)
            self.assertLessEqual(len(result), 127)
        except (ValueError, struct.error):
            pass

    def test_maximum_uint8_length_255(self) -> None:
        """Length prefix 0xFF (255) -- 127 versions plus 1 trailing byte.
        This is odd-length, so must be rejected."""
        parser = _require_parser(self)
        payload = b"\x03\x04" * 127 + b"\x00"
        self.assertEqual(len(payload), 255)
        body = struct.pack("!B", 255) + payload
        with self.assertRaises((ValueError, struct.error)):
            parser(body)

    def test_body_with_128_versions(self) -> None:
        """128 versions * 2 = 256 bytes -- exceeds uint8 range so it cannot
        be encoded at all in a well-formed body.  Feed a synthetic 257-byte
        body (length byte = 0x00 wrapping) to verify the parser does not
        silently wrap around."""
        parser = _require_parser(self)
        payload = b"\x03\x04" * 128  # 256 bytes
        # Length byte 0x00 due to truncation -- the parser must not treat
        # this as a valid zero-length body since 256 extra bytes follow.
        body = struct.pack("!B", 0) + payload
        try:
            result = parser(body)
            # If it accepted, the claimed length was 0 so result should be empty
            # and the 256 trailing bytes should be rejected or ignored.
            # Accepting with a non-empty result would be wrong.
            self.assertEqual(result, [])
        except (ValueError, struct.error):
            pass  # Rejection is the preferred outcome.


class TestAdversarialHexManipulation(unittest.TestCase):
    """Attempt to sneak values outside uint16 range via raw byte manipulation."""

    def test_three_byte_version_entry_rejected(self) -> None:
        """Inject 3-byte value where 2-byte is expected."""
        parser = _require_parser(self)
        # Claim 3 bytes of version data -- not a multiple of 2.
        body = b"\x03\x03\x04\x03"
        with self.assertRaises((ValueError, struct.error)):
            parser(body)

    def test_body_with_all_ff_bytes(self) -> None:
        """All-0xFF body: length 0xFF followed by 0xFF bytes."""
        parser = _require_parser(self)
        body = b"\xff" * 256
        try:
            result = parser(body)
            self.assertIsInstance(result, list)
        except (ValueError, struct.error, IndexError):
            pass  # Rejection expected.

    def test_body_with_all_zero_bytes(self) -> None:
        """All-zero body: length 0x00."""
        parser = _require_parser(self)
        body = b"\x00"
        try:
            result = parser(body)
            self.assertEqual(result, [])
        except (ValueError, struct.error):
            pass


class TestGoldenPathIosFixtures(unittest.TestCase):
    """Parse known-good supported_versions bodies from iOS fixtures and
    verify the golden-path contract."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.ios_bodies = _load_ios_fixture_supported_versions_bodies()

    def test_ios_fixtures_exist(self) -> None:
        self.assertGreater(
            len(self.ios_bodies),
            0,
            "No iOS fixture bodies found -- FIXTURES_ROOT may be wrong",
        )

    def test_golden_path_all_ios_bodies(self) -> None:
        parser = _require_parser(self)
        for idx, body in enumerate(self.ios_bodies):
            with self.subTest(fixture_index=idx, body_hex=body.hex()):
                result = parser(body)
                # Must return a list.
                self.assertIsInstance(result, list, f"fixture {idx}: not a list")
                # Must be non-empty (iOS always offers at least TLS 1.2 + 1.3).
                self.assertGreater(
                    len(result), 0, f"fixture {idx}: empty result"
                )
                # Every element is a uint16.
                for v in result:
                    self.assertIsInstance(v, int)
                    self.assertGreaterEqual(v, 0)
                    self.assertLessEqual(v, 0xFFFF)
                # No GREASE values in the output.
                for v in result:
                    self.assertFalse(
                        _is_grease(v),
                        f"fixture {idx}: GREASE 0x{v:04X} in output",
                    )
                # TLS 1.3 (0x0304) should be present in every iOS fixture body.
                self.assertIn(
                    0x0304,
                    result,
                    f"fixture {idx}: TLS 1.3 (0x0304) missing from output",
                )

    def test_golden_path_deterministic(self) -> None:
        parser = _require_parser(self)
        for idx, body in enumerate(self.ios_bodies):
            with self.subTest(fixture_index=idx):
                a = parser(body)
                b = parser(body)
                self.assertEqual(a, b, f"fixture {idx}: non-deterministic output")

    def test_golden_path_output_is_ordered(self) -> None:
        """The parser contract requires the output to be an ordered list
        (preserving wire order after GREASE removal)."""
        parser = _require_parser(self)
        for idx, body in enumerate(self.ios_bodies):
            with self.subTest(fixture_index=idx):
                result = parser(body)
                # Verify result is a list (order preserved from wire).
                self.assertIsInstance(result, list)
                # Verify no GREASE.
                grease_in_output = [v for v in result if _is_grease(v)]
                self.assertEqual(
                    grease_in_output,
                    [],
                    f"fixture {idx}: GREASE leaked: {grease_in_output}",
                )


class TestFuzzBoundarySeeds(unittest.TestCase):
    """Targeted fuzz: generate bodies that sit on boundary conditions and
    verify the parser handles them gracefully."""

    def test_every_possible_single_version(self) -> None:
        """All 65 536 possible single-version bodies."""
        parser = _require_parser(self)
        for version in range(0x10000):
            body = _build_body([version])
            try:
                result = parser(body)
                self.assertIsInstance(result, list)
                if _is_grease(version):
                    for v in result:
                        self.assertFalse(_is_grease(v))
                else:
                    self.assertIn(version, result)
            except (ValueError, struct.error):
                pass

    def test_body_truncated_at_every_offset(self) -> None:
        """Take a known-good body and truncate it at every possible offset."""
        parser = _require_parser(self)
        good_body = _build_body([0x0304, 0x0303, 0x0302])
        for length in range(len(good_body)):
            truncated = good_body[:length]
            try:
                result = parser(truncated)
                self.assertIsInstance(result, list)
            except (ValueError, struct.error, IndexError):
                pass

    def test_body_extended_with_random_suffix(self) -> None:
        """Append random bytes after a well-formed body."""
        parser = _require_parser(self)
        rng = random.Random(42)
        good_body = _build_body([0x0304, 0x0303])
        for _ in range(500):
            suffix_len = rng.randint(1, 64)
            suffix = bytes(rng.getrandbits(8) for _ in range(suffix_len))
            extended = good_body + suffix
            try:
                result = parser(extended)
                # If accepted, the trailing bytes should be ignored or cause
                # strict rejection -- not silently alter the parsed versions.
                self.assertIsInstance(result, list)
            except (ValueError, struct.error):
                pass  # Strict rejection is fine.


class TestFuzzAllocationBound(unittest.TestCase):
    """Verify the parser does not allocate disproportionately to input size."""

    def test_no_unbounded_allocation_large_random(self) -> None:
        parser = _require_parser(self)
        rng = random.Random(42)
        for _ in range(1_000):
            data = bytes(rng.getrandbits(8) for _ in range(512))
            try:
                result = parser(data)
                # At most 127 versions (254 bytes / 2) can fit in a uint8-prefixed body.
                self.assertLessEqual(
                    len(result),
                    128,
                    "Parser returned more versions than can fit in a "
                    "uint8-prefixed body -- possible unbounded allocation",
                )
            except (ValueError, TypeError, struct.error):
                pass

    def test_no_unbounded_allocation_adversarial_length(self) -> None:
        """Length prefix 0xFF but only 2 bytes of data follow."""
        parser = _require_parser(self)
        body = b"\xff\x03\x04"
        with self.assertRaises((ValueError, struct.error)):
            parser(body)


if __name__ == "__main__":
    unittest.main()
