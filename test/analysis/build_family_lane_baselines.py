#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

"""Build a compile-time reference table of reviewed family/lane baselines.

Reads the frozen ClientHello capture artifacts under
test/analysis/fixtures/clienthello/**/*.clienthello.json, groups samples by
(family_id, route_lane), and emits a deterministic C++ header with:

  * ExactInvariants: non-GREASE cipher-suite order, non-GREASE extension set,
    non-GREASE supported-groups list, ALPN list, compress_cert algo list,
    ECH presence bit, TLS record version, legacy ClientHello version.
  * SetMembershipCatalog: observed extension-order templates, wire length
    bucket, ECH payload lengths, ALPS types.
  * TierLevel: Tier1 n==1, Tier2 n>=3, Tier3 n>=15, Tier4 n>=200.

The script is pure stdlib. Output is byte-deterministic so committing the
generated header and a regression test can both be green at the same time.
"""

from __future__ import annotations

import argparse
import difflib
import json
import os
import pathlib
import re
import sys
import tempfile
import unittest
from typing import Any

# ---------------------------------------------------------------------------
# Family classification. Folded here (not in a JSON) so the script is pure
# stdlib and reviewers can audit the full mapping in one file.
# ---------------------------------------------------------------------------

_FIREFOX_TOKENS = ("firefox", "librewolf", "ironfox", "firefoxzen")
_SAFARI_TOKENS = ("safari",)


def classify_family_id(profile_id: str, os_family: str) -> str:
    """Return the coarse family identifier used by the baseline table.

    The classification mirrors the groupings we have asserted on in the
    existing capture differential tests and the corpus invariance tests:
    Safari and iOS-native browsers share the Apple TLS stack, every
    Chromium-derived browser on desktop/Android shares the BoringSSL
    stack, iOS Chrome/Brave also sits on top of Apple TLS, Firefox
    variants on desktop/Android use Gecko/NSS, Firefox on iOS is an
    Apple-TLS skin.
    """

    normalized = profile_id.lower()

    is_firefox_gecko = any(token in normalized for token in _FIREFOX_TOKENS)
    is_safari = any(token in normalized for token in _SAFARI_TOKENS)

    if is_safari:
        if os_family == "ios":
            return "apple_ios_tls"
        if os_family == "macos":
            return "apple_macos_tls"
        return "apple_tls"

    if is_firefox_gecko:
        if os_family == "ios":
            # Gecko is not available on iOS; the app must be a skin
            # sitting on Apple TLS.
            return "apple_ios_tls"
        if os_family == "android":
            return "firefox_android"
        if os_family == "macos":
            return "firefox_macos"
        if os_family == "linux":
            return "firefox_linux_desktop"
        if os_family == "windows":
            return "firefox_windows"
        return "firefox_other"

    # Chromium-derived engines: chrome, chromium, edge, opera, vivaldi,
    # brave, cromite, yandex, samsung_internet, adblock_browser, tdesktop,
    # chromiummaxthon, ...
    if os_family == "ios":
        return "ios_chromium"
    if os_family == "android":
        return "android_chromium"
    if os_family == "macos":
        return "chromium_macos"
    if os_family == "linux":
        return "chromium_linux_desktop"
    if os_family == "windows":
        return "chromium_windows"
    return "chromium_other"


# ---------------------------------------------------------------------------
# Sample ingestion
# ---------------------------------------------------------------------------


def _parse_u16(text: str) -> int:
    return int(text, 16)


_GREASE_U16 = {
    0x0A0A, 0x1A1A, 0x2A2A, 0x3A3A, 0x4A4A, 0x5A5A, 0x6A6A, 0x7A7A,
    0x8A8A, 0x9A9A, 0xAAAA, 0xBABA, 0xCACA, 0xDADA, 0xEAEA, 0xFAFA,
}


def _is_grease(value: int) -> bool:
    return value in _GREASE_U16


# Padding extension (type 0x0015) is ignored alongside GREASE to keep parity
# with the naming used in the reviewed fixture helpers ("without_padding").
_PAD_EXT = 0x0015


def _strip_grease(values: list[int]) -> list[int]:
    return [v for v in values if not _is_grease(v)]


def _strip_grease_and_pad(values: list[int]) -> list[int]:
    return [v for v in values if not _is_grease(v) and v != _PAD_EXT]


def _sample_wire_length(sample: dict[str, Any]) -> int:
    # record_length is the full TLS record; handshake_length is the inner
    # ClientHello body length. We prefer record_length because the matchers
    # compare against post-builder wire bytes.
    if "record_length" in sample:
        return int(sample["record_length"])
    if "handshake_length" in sample:
        return int(sample["handshake_length"])
    return 0


def _sample_ech_present(sample: dict[str, Any]) -> bool:
    ech = sample.get("ech") or {}
    if ech.get("payload_length") is not None:
        return True
    return any(_parse_u16(str(e.get("type", "0x0000"))) == 0xFE0D for e in sample.get("extensions", []))


def _sample_ech_payload_length(sample: dict[str, Any]) -> int | None:
    ech = sample.get("ech") or {}
    payload = ech.get("payload_length")
    if isinstance(payload, int):
        return payload
    return None


def _sample_alps_types(sample: dict[str, Any]) -> list[int]:
    result: list[int] = []
    for ext in sample.get("extensions", []):
        t = _parse_u16(str(ext.get("type", "0x0000")))
        if t in (0x4469, 0x44CD):
            result.append(t)
    return result


def _sample_non_grease_extension_order(sample: dict[str, Any]) -> list[int]:
    if "non_grease_extensions_without_padding" in sample:
        return [_parse_u16(v) for v in sample["non_grease_extensions_without_padding"]]
    return _strip_grease_and_pad([_parse_u16(v) for v in sample.get("extension_types", [])])


def _sample_non_grease_cipher_suites(sample: dict[str, Any]) -> list[int]:
    if "non_grease_cipher_suites" in sample:
        return [_parse_u16(v) for v in sample["non_grease_cipher_suites"]]
    return _strip_grease([_parse_u16(v) for v in sample.get("cipher_suites", [])])


def _sample_non_grease_supported_groups(sample: dict[str, Any]) -> list[int]:
    if "non_grease_supported_groups" in sample:
        return [_parse_u16(v) for v in sample["non_grease_supported_groups"]]
    return _strip_grease([_parse_u16(v) for v in sample.get("supported_groups", [])])


def _sample_compress_cert_algos(sample: dict[str, Any]) -> list[int]:
    return [_parse_u16(v) for v in sample.get("compress_certificate_algorithms", [])]


def _sample_alpn(sample: dict[str, Any]) -> list[str]:
    return list(sample.get("alpn_protocols", []))


def _sample_record_version(sample: dict[str, Any]) -> int:
    text = sample.get("source_tls_record_version") or sample.get("record_version") or "0x0301"
    return _parse_u16(str(text))


def _sample_legacy_version(sample: dict[str, Any]) -> int:
    text = sample.get("legacy_version", "0x0303")
    return _parse_u16(str(text))


# ---------------------------------------------------------------------------
# Tier assignment
# ---------------------------------------------------------------------------


def _tier_for(sample_count: int) -> str:
    if sample_count >= 200:
        return "Tier4"
    if sample_count >= 15:
        return "Tier3"
    if sample_count >= 3:
        return "Tier2"
    return "Tier1"


# ---------------------------------------------------------------------------
# Header emission helpers
# ---------------------------------------------------------------------------


def _cpp_u16(value: int) -> str:
    return f"0x{value:04X}u"


def _cpp_u16_list(values: list[int]) -> str:
    return "{" + ", ".join(_cpp_u16(v) for v in values) + "}"


def _cpp_size_list(values: list[int]) -> str:
    return "{" + ", ".join(f"{v}u" for v in values) + "}"


def _cpp_string(value: str) -> str:
    escaped = value.replace("\\", "\\\\").replace("\"", "\\\"")
    return f"\"{escaped}\""


def _cpp_string_list(values: list[str]) -> str:
    return "{" + ", ".join(_cpp_string(v) for v in values) + "}"


def _sanitize_identifier(name: str) -> str:
    return re.sub(r"[^0-9A-Za-z]+", "_", name).strip("_")


HEADER_PROLOGUE = """// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

// This file is generated by test/analysis/build_family_lane_baselines.py.
// Do not edit by hand. Refresh the frozen capture corpus first, review the
// diff, then regenerate this header from the checked-in JSON artifacts.

#pragma once

#include "td/utils/common.h"
#include "td/utils/Slice.h"

#include <cstddef>

namespace td {
namespace mtproto {
namespace test {
namespace baselines {

enum class TierLevel : int { Tier1 = 1, Tier2 = 2, Tier3 = 3, Tier4 = 4 };

struct ExactInvariants final {
  Slice family_id;
  Slice route_lane;
  vector<uint16> non_grease_cipher_suites_ordered;
  vector<uint16> non_grease_extension_set;
  vector<uint16> non_grease_supported_groups;
  vector<string> alpn_protocols;
  vector<uint16> compress_cert_algorithms;
  bool ech_presence_required{false};
  uint16 tls_record_version{0};
  uint16 client_hello_legacy_version{0};
};

struct SetMembershipCatalog final {
  vector<vector<uint16>> observed_extension_order_templates;
  vector<size_t> observed_wire_lengths;
  vector<uint16> observed_ech_payload_lengths;
  vector<uint16> observed_alps_types;
};

struct FamilyLaneBaseline final {
  Slice family_id;
  Slice route_lane;
  TierLevel tier;
  size_t sample_count;
  size_t num_sources;
  ExactInvariants invariants;
  SetMembershipCatalog set_catalog;
};

const FamilyLaneBaseline *get_baseline(Slice family_id, Slice route_lane);
size_t get_baseline_count();
const FamilyLaneBaseline &get_baseline_by_index(size_t index);

"""

HEADER_EPILOGUE = """
}  // namespace baselines
}  // namespace test
}  // namespace mtproto
}  // namespace td
"""


# ---------------------------------------------------------------------------
# Core build
# ---------------------------------------------------------------------------


def iter_artifact_paths(input_dir: pathlib.Path) -> list[pathlib.Path]:
    return sorted(p for p in input_dir.rglob("*.clienthello.json") if p.is_file())


def load_samples(input_dir: pathlib.Path) -> list[dict[str, Any]]:
    samples: list[dict[str, Any]] = []
    for artifact_path in iter_artifact_paths(input_dir):
        with artifact_path.open("r", encoding="utf-8") as infile:
            artifact = json.load(infile)
        profile_id = str(artifact.get("profile_id", ""))
        route_lane = str(artifact.get("route_mode", "unknown"))
        os_family = str(artifact.get("os_family", "unknown"))
        family_id = classify_family_id(profile_id, os_family)
        for sample in artifact.get("samples", []):
            samples.append({
                "family_id": family_id,
                "route_lane": route_lane,
                "profile_id": profile_id,
                "sample": sample,
                "artifact_path": artifact_path,
            })
    return samples


def _merge_exact_invariants(group: list[dict[str, Any]]) -> dict[str, Any]:
    # Exact invariants are intersected across samples: a value is kept iff
    # every sample in the group agrees on it. For list-valued fields we keep
    # the first sample's list and only keep it if all samples share the same
    # list; otherwise we degrade the list to the empty list (callers treat
    # the empty list as "no exact invariant").
    ech_present_bits = {_sample_ech_present(e["sample"]) for e in group}
    record_versions = {_sample_record_version(e["sample"]) for e in group}
    legacy_versions = {_sample_legacy_version(e["sample"]) for e in group}

    def common_list(values: list[list]) -> list:
        if not values:
            return []
        first = values[0]
        for other in values[1:]:
            if other != first:
                return []
        return first

    cipher_suites = common_list([_sample_non_grease_cipher_suites(e["sample"]) for e in group])
    extension_set = common_list([_sample_non_grease_extension_order(e["sample"]) for e in group])
    supported_groups = common_list([_sample_non_grease_supported_groups(e["sample"]) for e in group])
    alpn_protocols = common_list([_sample_alpn(e["sample"]) for e in group])
    compress_algos = common_list([_sample_compress_cert_algos(e["sample"]) for e in group])

    return {
        "cipher_suites": cipher_suites,
        "extension_set": extension_set,
        "supported_groups": supported_groups,
        "alpn_protocols": alpn_protocols,
        "compress_algos": compress_algos,
        "ech_presence_required": len(ech_present_bits) == 1 and True in ech_present_bits,
        "record_version": record_versions.pop() if len(record_versions) == 1 else 0,
        "legacy_version": legacy_versions.pop() if len(legacy_versions) == 1 else 0,
    }


def _build_set_catalog(group: list[dict[str, Any]]) -> dict[str, Any]:
    order_templates: list[list[int]] = []
    seen_templates: set[tuple[int, ...]] = set()
    wire_lengths: list[int] = []
    seen_wire: set[int] = set()
    ech_lengths: list[int] = []
    seen_ech: set[int] = set()
    alps_types: list[int] = []
    seen_alps: set[int] = set()

    for entry in group:
        sample = entry["sample"]

        ext_order = _sample_non_grease_extension_order(sample)
        key = tuple(ext_order)
        if key not in seen_templates:
            seen_templates.add(key)
            order_templates.append(ext_order)

        wl = _sample_wire_length(sample)
        if wl and wl not in seen_wire:
            seen_wire.add(wl)
            wire_lengths.append(wl)

        ech_len = _sample_ech_payload_length(sample)
        if ech_len is not None and ech_len not in seen_ech:
            seen_ech.add(ech_len)
            ech_lengths.append(ech_len)

        for t in _sample_alps_types(sample):
            if t not in seen_alps:
                seen_alps.add(t)
                alps_types.append(t)

    # Stable ordering: templates sorted by their tuple, numbers ascending.
    order_templates.sort()
    wire_lengths.sort()
    ech_lengths.sort()
    alps_types.sort()

    return {
        "extension_order_templates": order_templates,
        "wire_lengths": wire_lengths,
        "ech_payload_lengths": ech_lengths,
        "alps_types": alps_types,
    }


def build_baselines(samples: list[dict[str, Any]]) -> list[dict[str, Any]]:
    grouped: dict[tuple[str, str], list[dict[str, Any]]] = {}
    for entry in samples:
        key = (entry["family_id"], entry["route_lane"])
        grouped.setdefault(key, []).append(entry)

    baselines: list[dict[str, Any]] = []
    for (family_id, route_lane), group in sorted(grouped.items()):
        invariants = _merge_exact_invariants(group)
        catalog = _build_set_catalog(group)
        sources = {entry["profile_id"] for entry in group}
        baselines.append({
            "family_id": family_id,
            "route_lane": route_lane,
            "tier": _tier_for(len(group)),
            "sample_count": len(group),
            "num_sources": len(sources),
            "invariants": invariants,
            "set_catalog": catalog,
        })
    return baselines


def render_header(baselines: list[dict[str, Any]]) -> str:
    lines: list[str] = [HEADER_PROLOGUE.rstrip(), ""]

    # Per-baseline inline globals so that the large vector<uint16> fields do
    # not have to live in a single gigantic array initialiser.
    var_names: list[str] = []
    for baseline in baselines:
        ident = _sanitize_identifier(f"{baseline['family_id']}__{baseline['route_lane']}")
        prefix = f"k{ident}"
        inv = baseline["invariants"]
        cat = baseline["set_catalog"]

        lines.append(f"// family_id={baseline['family_id']} route_lane={baseline['route_lane']}")
        lines.append(
            f"inline const vector<uint16> {prefix}CipherSuites = {_cpp_u16_list(inv['cipher_suites'])};"
        )
        lines.append(
            f"inline const vector<uint16> {prefix}ExtensionSet = {_cpp_u16_list(inv['extension_set'])};"
        )
        lines.append(
            f"inline const vector<uint16> {prefix}SupportedGroups = {_cpp_u16_list(inv['supported_groups'])};"
        )
        lines.append(
            f"inline const vector<string> {prefix}AlpnProtocols = {_cpp_string_list(inv['alpn_protocols'])};"
        )
        lines.append(
            f"inline const vector<uint16> {prefix}CompressCertAlgorithms = {_cpp_u16_list(inv['compress_algos'])};"
        )

        template_inits = ", ".join(
            _cpp_u16_list(t) for t in cat["extension_order_templates"]
        )
        lines.append(
            f"inline const vector<vector<uint16>> {prefix}ObservedExtensionOrderTemplates = {{{template_inits}}};"
        )
        lines.append(
            f"inline const vector<size_t> {prefix}ObservedWireLengths = {_cpp_size_list(cat['wire_lengths'])};"
        )
        lines.append(
            f"inline const vector<uint16> {prefix}ObservedEchPayloadLengths = {_cpp_u16_list(cat['ech_payload_lengths'])};"
        )
        lines.append(
            f"inline const vector<uint16> {prefix}ObservedAlpsTypes = {_cpp_u16_list(cat['alps_types'])};"
        )
        lines.append("")
        var_names.append(prefix)

    # The actual table of baselines.
    lines.append("inline const vector<FamilyLaneBaseline> &get_baselines_table() {")
    lines.append("  static const vector<FamilyLaneBaseline> kTable = [] {")
    lines.append("    vector<FamilyLaneBaseline> t;")
    lines.append(f"    t.reserve({len(baselines)});")
    for baseline, prefix in zip(baselines, var_names):
        inv = baseline["invariants"]
        lines.append("    {")
        lines.append("      FamilyLaneBaseline b;")
        lines.append(f"      b.family_id = Slice({_cpp_string(baseline['family_id'])});")
        lines.append(f"      b.route_lane = Slice({_cpp_string(baseline['route_lane'])});")
        lines.append(f"      b.tier = TierLevel::{baseline['tier']};")
        lines.append(f"      b.sample_count = {baseline['sample_count']}u;")
        lines.append(f"      b.num_sources = {baseline['num_sources']}u;")
        lines.append(f"      b.invariants.family_id = b.family_id;")
        lines.append(f"      b.invariants.route_lane = b.route_lane;")
        lines.append(f"      b.invariants.non_grease_cipher_suites_ordered = {prefix}CipherSuites;")
        lines.append(f"      b.invariants.non_grease_extension_set = {prefix}ExtensionSet;")
        lines.append(f"      b.invariants.non_grease_supported_groups = {prefix}SupportedGroups;")
        lines.append(f"      b.invariants.alpn_protocols = {prefix}AlpnProtocols;")
        lines.append(f"      b.invariants.compress_cert_algorithms = {prefix}CompressCertAlgorithms;")
        lines.append(
            f"      b.invariants.ech_presence_required = {'true' if inv['ech_presence_required'] else 'false'};"
        )
        lines.append(f"      b.invariants.tls_record_version = {_cpp_u16(inv['record_version'])};")
        lines.append(
            f"      b.invariants.client_hello_legacy_version = {_cpp_u16(inv['legacy_version'])};"
        )
        lines.append(
            f"      b.set_catalog.observed_extension_order_templates = {prefix}ObservedExtensionOrderTemplates;"
        )
        lines.append(
            f"      b.set_catalog.observed_wire_lengths = {prefix}ObservedWireLengths;"
        )
        lines.append(
            f"      b.set_catalog.observed_ech_payload_lengths = {prefix}ObservedEchPayloadLengths;"
        )
        lines.append(
            f"      b.set_catalog.observed_alps_types = {prefix}ObservedAlpsTypes;"
        )
        lines.append("      t.push_back(std::move(b));")
        lines.append("    }")
    lines.append("    return t;")
    lines.append("  }();")
    lines.append("  return kTable;")
    lines.append("}")
    lines.append("")

    lines.append("inline size_t get_baseline_count() {")
    lines.append("  return get_baselines_table().size();")
    lines.append("}")
    lines.append("")
    lines.append("inline const FamilyLaneBaseline &get_baseline_by_index(size_t index) {")
    lines.append("  return get_baselines_table().at(index);")
    lines.append("}")
    lines.append("")
    lines.append("inline const FamilyLaneBaseline *get_baseline(Slice family_id, Slice route_lane) {")
    lines.append("  for (const auto &b : get_baselines_table()) {")
    lines.append("    if (b.family_id == family_id && b.route_lane == route_lane) {")
    lines.append("      return &b;")
    lines.append("    }")
    lines.append("  }")
    lines.append("  return nullptr;")
    lines.append("}")

    lines.append(HEADER_EPILOGUE.rstrip("\n"))
    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------------------
# CLI + self-test
# ---------------------------------------------------------------------------


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=(
        "Emit the reviewed family/lane baseline header from frozen "
        "ClientHello capture artifacts."
    ))
    parser.add_argument(
        "--input-dir",
        default="test/analysis/fixtures/clienthello",
        help="Directory with frozen JSON capture artifacts.",
    )
    parser.add_argument(
        "--output",
        default="test/stealth/ReviewedFamilyLaneBaselines.h",
        help="Generated C++ header path.",
    )
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="Run the internal self-test harness and exit.",
    )
    return parser.parse_args(argv)


def generate_for(input_dir: pathlib.Path, output_path: pathlib.Path) -> str:
    samples = load_samples(input_dir)
    baselines = build_baselines(samples)
    rendered = render_header(baselines)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(rendered, encoding="utf-8", newline="\n")
    return rendered


class _DeterminismSelfTest(unittest.TestCase):
    def test_regeneration_matches_committed(self) -> None:
        repo_root = pathlib.Path(__file__).resolve().parents[2]
        input_dir = repo_root / "test" / "analysis" / "fixtures" / "clienthello"
        committed = repo_root / "test" / "stealth" / "ReviewedFamilyLaneBaselines.h"
        if not committed.exists():
            self.skipTest("committed header not present yet")
        with tempfile.TemporaryDirectory() as tmp:
            scratch = pathlib.Path(tmp) / "out.h"
            generated = generate_for(input_dir, scratch)
            committed_text = committed.read_text(encoding="utf-8")
            if generated != committed_text:
                diff = "\n".join(
                    difflib.unified_diff(
                        committed_text.splitlines(),
                        generated.splitlines(),
                        lineterm="",
                        fromfile="committed",
                        tofile="regenerated",
                    )
                )
                self.fail(
                    "committed ReviewedFamilyLaneBaselines.h is stale; "
                    "rerun build_family_lane_baselines.py. diff:\n" + diff
                )

    def test_regeneration_is_byte_deterministic(self) -> None:
        repo_root = pathlib.Path(__file__).resolve().parents[2]
        input_dir = repo_root / "test" / "analysis" / "fixtures" / "clienthello"
        with tempfile.TemporaryDirectory() as tmp:
            p1 = pathlib.Path(tmp) / "a.h"
            p2 = pathlib.Path(tmp) / "b.h"
            first = generate_for(input_dir, p1)
            second = generate_for(input_dir, p2)
            self.assertEqual(first, second)


def _run_self_test() -> int:
    loader = unittest.defaultTestLoader
    suite = loader.loadTestsFromTestCase(_DeterminismSelfTest)
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)
    return 0 if result.wasSuccessful() else 1


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if args.self_test:
        return _run_self_test()
    input_dir = pathlib.Path(args.input_dir).resolve()
    output_path = pathlib.Path(args.output).resolve()
    generate_for(input_dir, output_path)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
