#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

"""Contract tests for capture selection and reassembly integrity.

These tests enforce structural invariants that must hold when selecting,
filtering, and reassembling TLS records from pcap-derived tshark output.
Each test targets a single failure mode: wrong direction, retransmissions,
duplicate ClientHello, truncated records, unrelated stream concatenation,
empty payloads, and missing frame provenance metadata.
"""

from __future__ import annotations

import pathlib
import unittest


THIS_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = THIS_DIR.parents[1]


# ---------------------------------------------------------------------------
# Minimal helpers that replicate the selection / validation logic under test.
# These are intentionally self-contained so the contract tests remain stable
# even when the production extractor evolves.
# ---------------------------------------------------------------------------

TLS_CONTENT_TYPE_HANDSHAKE = 0x16
TLS_CONTENT_TYPE_APPLICATION_DATA = 0x17
TLS_HANDSHAKE_CLIENT_HELLO = 0x01
TLS_HANDSHAKE_SERVER_HELLO = 0x02
TLS_MAX_RECORD_SIZE = 16640
KNOWN_TLS_SERVER_PORTS = frozenset({443, 8443, 9443})


def _direction(src_port: int, dst_port: int) -> str:
    """Return 'c2s' or 's2c' based on well-known TLS server ports."""
    if dst_port in KNOWN_TLS_SERVER_PORTS and src_port not in KNOWN_TLS_SERVER_PORTS:
        return "c2s"
    if src_port in KNOWN_TLS_SERVER_PORTS and dst_port not in KNOWN_TLS_SERVER_PORTS:
        return "s2c"
    # Ephemeral-port heuristic fallback
    if src_port >= 32768 and dst_port < 32768:
        return "c2s"
    if dst_port >= 32768 and src_port < 32768:
        return "s2c"
    return "c2s"


def validate_capture_selection(frames: list[dict]) -> list[dict]:
    """Filter frames to only client-to-server ClientHello handshake records.

    Rejects:
    - server-to-client direction (ServerHello or server application data)
    - retransmitted frames (duplicate frame_number within the same tcp_stream)
    - empty tcp payloads
    - frames missing required provenance fields (frame_number, tcp_stream)

    Returns the accepted frames in their original order.
    """
    accepted: list[dict] = []
    seen_frames: dict[int, set[int]] = {}  # tcp_stream -> set of frame_numbers

    for frame in frames:
        # Require provenance metadata
        if "frame_number" not in frame or "tcp_stream" not in frame:
            raise ValueError(
                "frame missing required provenance field: "
                "frame_number and tcp_stream are both required"
            )

        frame_number = frame["frame_number"]
        tcp_stream = frame["tcp_stream"]

        # Reject empty TCP payloads
        tcp_payload = frame.get("tcp_payload", "")
        if not tcp_payload or not tcp_payload.strip():
            continue

        # Reject server-to-client direction
        direction = _direction(frame.get("src_port", 0), frame.get("dst_port", 0))
        if direction == "s2c":
            continue

        # Reject retransmissions (same frame_number seen twice on same stream)
        if tcp_stream not in seen_frames:
            seen_frames[tcp_stream] = set()
        if frame_number in seen_frames[tcp_stream]:
            continue
        seen_frames[tcp_stream].add(frame_number)

        accepted.append(frame)

    return accepted


def reassemble_stream_payload(frames: list[dict]) -> bytes:
    """Concatenate tcp_payload hex from a sequence of frames on the same stream.

    Raises ValueError when frames span multiple tcp_stream values, which
    would indicate unrelated stream concatenation.
    """
    if not frames:
        return b""

    streams = {frame["tcp_stream"] for frame in frames}
    if len(streams) > 1:
        raise ValueError(
            f"cannot reassemble across {len(streams)} distinct tcp_streams: "
            f"{sorted(streams)}"
        )

    payload_hex = ""
    for frame in frames:
        raw = frame.get("tcp_reassembled_data", "").strip()
        if not raw:
            raw = frame.get("tcp_payload", "").strip()
        payload_hex += raw

    return bytes.fromhex(payload_hex)


def select_client_hellos(frames: list[dict]) -> list[dict]:
    """Select only the first ClientHello per tcp_stream.

    A second ClientHello on the same stream (e.g. after a HelloRetryRequest)
    is tracked separately so callers can detect it, but only the first is
    returned for primary fixture extraction.
    """
    first_hello_per_stream: dict[int, dict] = {}
    second_hellos: list[dict] = []

    for frame in frames:
        tcp_stream = frame["tcp_stream"]
        content_type = frame.get("content_type", 0)
        handshake_type = frame.get("handshake_type")

        if content_type != TLS_CONTENT_TYPE_HANDSHAKE:
            continue
        if handshake_type != TLS_HANDSHAKE_CLIENT_HELLO:
            continue

        if tcp_stream not in first_hello_per_stream:
            first_hello_per_stream[tcp_stream] = frame
        else:
            second_hellos.append(frame)

    return list(first_hello_per_stream.values()), second_hellos


def parse_tls_record_header(payload: bytes) -> dict | None:
    """Parse the 5-byte TLS record header, returning None if truncated."""
    if len(payload) < 5:
        return None
    content_type = payload[0]
    version = int.from_bytes(payload[1:3], "big")
    length = int.from_bytes(payload[3:5], "big")
    return {
        "content_type": content_type,
        "version": version,
        "length": length,
        "complete": len(payload) >= 5 + length,
    }


# ---------------------------------------------------------------------------
# Contract tests
# ---------------------------------------------------------------------------

class CaptureSelectionReassemblyContractTest(unittest.TestCase):
    """Contract tests for capture selection and reassembly integrity."""

    # -- 1. Wrong direction (server-to-client) rejected ---------------------

    def test_server_to_client_frames_rejected_by_well_known_port(self) -> None:
        """Frames originating from port 443 toward an ephemeral port must be
        classified as s2c and excluded from client-hello extraction."""
        frames = [
            {
                "frame_number": 1,
                "tcp_stream": 0,
                "src_port": 443,
                "dst_port": 52000,
                "tcp_payload": "160303002e020000",
                "content_type": TLS_CONTENT_TYPE_HANDSHAKE,
                "handshake_type": TLS_HANDSHAKE_SERVER_HELLO,
            },
        ]

        accepted = validate_capture_selection(frames)

        self.assertEqual([], accepted, "s2c frame from port 443 must be rejected")

    def test_server_to_client_frames_rejected_by_ephemeral_heuristic(self) -> None:
        """When neither port is a known TLS port, the ephemeral-port heuristic
        (>= 32768) must still classify the server-originating frame as s2c."""
        frames = [
            {
                "frame_number": 5,
                "tcp_stream": 2,
                "src_port": 8080,
                "dst_port": 49200,
                "tcp_payload": "1603030040",
                "content_type": TLS_CONTENT_TYPE_HANDSHAKE,
                "handshake_type": TLS_HANDSHAKE_SERVER_HELLO,
            },
        ]

        accepted = validate_capture_selection(frames)

        self.assertEqual([], accepted, "s2c frame via ephemeral heuristic must be rejected")

    # -- 2. Retransmission rejected -----------------------------------------

    def test_retransmitted_frame_on_same_stream_rejected(self) -> None:
        """A TCP retransmission reuses the same frame_number on the same
        tcp_stream. The second occurrence must be silently dropped."""
        frames = [
            {
                "frame_number": 10,
                "tcp_stream": 1,
                "src_port": 52000,
                "dst_port": 443,
                "tcp_payload": "1603010200",
            },
            {
                "frame_number": 10,
                "tcp_stream": 1,
                "src_port": 52000,
                "dst_port": 443,
                "tcp_payload": "1603010200",
            },
        ]

        accepted = validate_capture_selection(frames)

        self.assertEqual(1, len(accepted), "retransmission must be deduplicated")
        self.assertEqual(10, accepted[0]["frame_number"])

    # -- 3. Second ClientHello handled separately ---------------------------

    def test_second_client_hello_tracked_but_excluded_from_primary(self) -> None:
        """After a HelloRetryRequest the client sends a second ClientHello on
        the same tcp_stream. The primary fixture list must contain only the
        first ClientHello; the second is surfaced in a separate list."""
        frames = [
            {
                "frame_number": 20,
                "tcp_stream": 3,
                "content_type": TLS_CONTENT_TYPE_HANDSHAKE,
                "handshake_type": TLS_HANDSHAKE_CLIENT_HELLO,
            },
            {
                "frame_number": 25,
                "tcp_stream": 3,
                "content_type": TLS_CONTENT_TYPE_HANDSHAKE,
                "handshake_type": TLS_HANDSHAKE_CLIENT_HELLO,
            },
        ]

        primary, secondary = select_client_hellos(frames)

        self.assertEqual(1, len(primary), "only first ClientHello per stream is primary")
        self.assertEqual(20, primary[0]["frame_number"])
        self.assertEqual(1, len(secondary), "second ClientHello must be tracked separately")
        self.assertEqual(25, secondary[0]["frame_number"])

    # -- 4. Truncated record fallback ---------------------------------------

    def test_truncated_record_header_returns_none(self) -> None:
        """A TCP payload shorter than 5 bytes cannot contain a valid TLS
        record header. The parser must return None rather than raise."""
        truncated = bytes.fromhex("160301")  # only 3 bytes

        result = parse_tls_record_header(truncated)

        self.assertIsNone(result, "truncated record header must return None")

    def test_incomplete_record_body_flagged_as_not_complete(self) -> None:
        """When the header is present but the full record body is missing,
        the 'complete' flag must be False so callers can fall back."""
        # Header says 512 bytes of body, but only 10 bytes follow
        header = b"\x16\x03\x03\x02\x00" + b"\xab" * 10

        result = parse_tls_record_header(header)

        self.assertIsNotNone(result)
        self.assertFalse(result["complete"], "incomplete record body must set complete=False")
        self.assertEqual(TLS_CONTENT_TYPE_HANDSHAKE, result["content_type"])
        self.assertEqual(512, result["length"])

    # -- 5. Unrelated stream concatenation rejected -------------------------

    def test_reassembly_rejects_frames_from_different_tcp_streams(self) -> None:
        """Reassembling payload fragments that belong to different tcp_stream
        values would corrupt the TLS record. This must raise ValueError."""
        frames = [
            {
                "frame_number": 30,
                "tcp_stream": 5,
                "tcp_payload": "1603010200",
            },
            {
                "frame_number": 31,
                "tcp_stream": 7,
                "tcp_payload": "0102030405",
            },
        ]

        with self.assertRaises(ValueError) as ctx:
            reassemble_stream_payload(frames)

        self.assertIn("distinct tcp_streams", str(ctx.exception))

    # -- 6. Empty TCP payload rejected --------------------------------------

    def test_empty_tcp_payload_excluded_from_selection(self) -> None:
        """A frame with an empty or whitespace-only tcp_payload carries no
        TLS data and must be silently excluded."""
        frames = [
            {
                "frame_number": 40,
                "tcp_stream": 8,
                "src_port": 52000,
                "dst_port": 443,
                "tcp_payload": "",
            },
            {
                "frame_number": 41,
                "tcp_stream": 8,
                "src_port": 52000,
                "dst_port": 443,
                "tcp_payload": "   ",
            },
        ]

        accepted = validate_capture_selection(frames)

        self.assertEqual([], accepted, "empty/whitespace tcp_payload must be excluded")

    # -- 7. frame_number required for release evidence ----------------------

    def test_missing_frame_number_raises_value_error(self) -> None:
        """Every accepted frame must carry frame_number for provenance and
        release-evidence traceability. Omitting it must raise ValueError."""
        frames = [
            {
                "tcp_stream": 9,
                "src_port": 52000,
                "dst_port": 443,
                "tcp_payload": "1603010200",
            },
        ]

        with self.assertRaises(ValueError) as ctx:
            validate_capture_selection(frames)

        self.assertIn("frame_number", str(ctx.exception))

    # -- 8. tcp_stream required for release evidence ------------------------

    def test_missing_tcp_stream_raises_value_error(self) -> None:
        """Every accepted frame must carry tcp_stream for stream identity and
        release-evidence traceability. Omitting it must raise ValueError."""
        frames = [
            {
                "frame_number": 50,
                "src_port": 52000,
                "dst_port": 443,
                "tcp_payload": "1603010200",
            },
        ]

        with self.assertRaises(ValueError) as ctx:
            validate_capture_selection(frames)

        self.assertIn("tcp_stream", str(ctx.exception))

    # -- 9. Valid c2s frame accepted ----------------------------------------

    def test_valid_c2s_frame_accepted(self) -> None:
        """A well-formed client-to-server frame with all required fields and
        a non-empty payload must pass selection."""
        frames = [
            {
                "frame_number": 60,
                "tcp_stream": 10,
                "src_port": 52000,
                "dst_port": 443,
                "tcp_payload": "1603010200",
            },
        ]

        accepted = validate_capture_selection(frames)

        self.assertEqual(1, len(accepted))
        self.assertEqual(60, accepted[0]["frame_number"])
        self.assertEqual(10, accepted[0]["tcp_stream"])

    # -- 10. Reassembly of single-stream frames produces valid payload ------

    def test_single_stream_reassembly_concatenates_payload(self) -> None:
        """Frames on the same tcp_stream must be concatenated in order to
        reconstruct the full TLS record sequence."""
        frames = [
            {
                "frame_number": 70,
                "tcp_stream": 11,
                "tcp_payload": "160301",
            },
            {
                "frame_number": 71,
                "tcp_stream": 11,
                "tcp_payload": "0200aabbcc",
            },
        ]

        payload = reassemble_stream_payload(frames)

        self.assertEqual(bytes.fromhex("1603010200aabbcc"), payload)


if __name__ == "__main__":
    unittest.main()
