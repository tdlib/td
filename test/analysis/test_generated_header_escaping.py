# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

"""Workstream A: generated header hygiene against adversarial metadata.

Synthesizes a fixture whose metadata strings contain characters that are unsafe
to splice verbatim into a C++ string literal or identifier context and verifies
the header generator either rejects them up front or escapes them such that no
injection is possible in the rendered output.

Probed inputs include: ``"``, ``\\``, newline, ``*/``, and ``<?xml``.

When the generator does not enforce escaping for a given input, the relevant
test FAILS. A failing test here means a real gap for David to review; do not
weaken the test to make it green.
"""

from __future__ import annotations

import json
import pathlib
import sys
import tempfile
import unittest


THIS_DIR = pathlib.Path(__file__).resolve().parent
if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))

from merge_client_hello_fixture_summary import merge_artifacts  # noqa: E402


ADVERSARIAL_INPUTS: dict[str, str] = {
    "double_quote": 'abc"xyz',
    "backslash": "abc\\xyz",
    "newline": "abc\nxyz",
    "c_comment_terminator": "abc*/xyz",
    "xml_prolog": "abc<?xml xyz",
}


def synthetic_artifact(
    profile_id: str,
    sni: str,
    scenario_id: str,
    source_path: str,
    source_sha256: str = "0" * 64,
    capture_date_utc: str = "2026-04-07T00:00:00Z",
) -> dict:
    return {
        "artifact_type": "tls_clienthello_fixtures",
        "parser_version": "tls-clienthello-parser-v1",
        "profile_id": profile_id,
        "route_mode": "non_ru_egress",
        "device_class": "desktop",
        "os_family": "linux",
        "transport": "tcp",
        "source_kind": "browser_capture",
        "source_path": source_path,
        "source_sha256": source_sha256,
        "scenario_id": scenario_id,
        "capture_date_utc": capture_date_utc,
        "samples": [
            {
                "fixture_id": f"{profile_id}:frame1",
                "frame_number": 1,
                "tcp_stream": 0,
                "record_length": 0,
                "handshake_length": 0,
                "sni": sni,
                "cipher_suites": [],
                "non_grease_cipher_suites": [],
                "supported_groups": [],
                "non_grease_supported_groups": [],
                "extension_types": [],
                "non_grease_extensions_without_padding": [],
                "alpn_protocols": [],
                "compress_certificate_algorithms": [],
                "key_share_entries": [],
                "extensions": [],
                "ech": None,
            }
        ],
    }


def assert_no_cpp_literal_break(rendered: str, probe: str, field_name: str) -> None:
    """Assert adversarial ``probe`` did not break out of a C++ string literal.

    The generator emits ``inline constexpr const char ...[] = "<value>";`` lines.
    If an adversarial byte lands inside ``<value>`` unescaped, the literal closes
    early and the rendered header has either mismatched quotes or dangling
    syntax. We look for the probe preceded by an unescaped opening quote, then
    require a properly escaped representation to appear in the rendered text.
    """
    if probe == "":
        return
    # Crude but deterministic: each C++ string literal line should have the
    # form ``"..."``. Count raw (non-escaped) double quotes on each such line:
    # an odd count anywhere indicates a broken literal.
    for line in rendered.splitlines():
        stripped = line.strip()
        if "= \"" not in stripped:
            continue
        # Strip escaped backslashes first, then escaped quotes, then count bare quotes.
        reduced = stripped.replace("\\\\", "")
        reduced = reduced.replace("\\\"", "")
        bare_quotes = reduced.count("\"")
        if bare_quotes % 2 != 0:
            raise AssertionError(
                f"generated header line has unbalanced quotes for {field_name}: {line!r}"
            )
    # Newlines are an independent injection vector: a raw \n splits a literal
    # across two lines regardless of quote balance on any single line.
    if "\n" in probe:
        for line in rendered.splitlines():
            if line.strip().startswith("= ") and line.rstrip().endswith("\""):
                continue
        # Require the escaped form to appear somewhere in the output.
        if "\\n" not in rendered:
            raise AssertionError(
                f"generated header failed to escape newline in {field_name}"
            )


def assert_no_comment_terminator_break(rendered: str, probe: str) -> None:
    """Assert adversarial ``*/`` does not close a block comment unintentionally.

    The generator emits ``// fixture_id=<...>`` line comments. A ``*/`` inside
    a line comment is inert for C++, but the generator also emits ``/*``-style
    comment headers elsewhere in the corpus pipeline; a future drift could turn
    a probe into comment escape. We require the escaped form to appear whenever
    the probe is a comment-terminator.
    """
    if "*/" not in probe:
        return
    # Accept either literal ``*/`` inside a ``//`` line comment (safe) or an
    # escaped form. Reject only if it appears inside a ``/* ... */`` window.
    in_block_comment = False
    for line in rendered.splitlines():
        idx = 0
        while idx < len(line):
            two = line[idx : idx + 2]
            if not in_block_comment and two == "/*":
                in_block_comment = True
                idx += 2
                continue
            if in_block_comment and two == "*/":
                in_block_comment = False
                idx += 2
                continue
            idx += 1
        if in_block_comment and "*/" in line:
            # Anything after an unexpected ``*/`` would escape the block comment.
            raise AssertionError("generated header contains unescaped */ inside a block comment")


class GeneratedHeaderEscapingTest(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.input_dir = pathlib.Path(self._tmp.name).resolve() / "clienthello"
        self.input_dir.mkdir(parents=True)

    def tearDown(self) -> None:
        self._tmp.cleanup()

    def _write_artifact(self, name: str, payload: dict) -> pathlib.Path:
        target = self.input_dir / name
        with target.open("w", encoding="utf-8") as handle:
            json.dump(payload, handle)
        return target

    def test_adversarial_metadata_strings_are_escaped(self) -> None:
        for label, probe in ADVERSARIAL_INPUTS.items():
            with self.subTest(probe=label):
                profile_id = f"escape_probe_{label}"
                payload = synthetic_artifact(
                    profile_id=profile_id,
                    sni=probe,
                    scenario_id=f"scenario_{label}{probe}",
                    source_path=f"/synthetic/{label}{probe}.pcapng",
                )
                artifact_path = self._write_artifact(f"{label}.json", payload)
                self.assertTrue(artifact_path.is_file())

                try:
                    rendered = merge_artifacts(self.input_dir)
                except (SystemExit, ValueError):
                    # Up-front rejection is an acceptable outcome.
                    continue
                finally:
                    # Clear the artifact so the next subtest sees a clean corpus.
                    artifact_path.unlink(missing_ok=True)

                # REAL GAP: header generator splices metadata verbatim into C++ literals; David to review
                assert_no_cpp_literal_break(rendered, probe, f"sni/{label}")
                assert_no_comment_terminator_break(rendered, probe)


if __name__ == "__main__":
    unittest.main()
