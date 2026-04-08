#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

import argparse
import json
import pathlib
import sys
from typing import Any


def load_artifact(path: pathlib.Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as infile:
        return json.load(infile)


def cpp_u16_list(name: str, values: list[str]) -> str:
    body = ", ".join(values)
    return f"static const std::vector<uint16> {name} = {{{body}}};"


def cpp_key_share_entries(name: str, entries: list[dict[str, Any]]) -> str:
    lines = [f"// {name}"]
    for entry in entries:
        lines.append(
            f"//   group={entry['group']} key_exchange_length={entry['key_exchange_length']} grease={str(entry['is_grease_group']).lower()}"
        )
    return "\n".join(lines)


def render_sample_cpp(sample: dict[str, Any]) -> str:
    fixture_id = sample["fixture_id"].replace(":", "_").replace("-", "_").replace(".", "_")
    lines = [f"// fixture_id={sample['fixture_id']}"]
    lines.append(cpp_u16_list(f"k_{fixture_id}_cipher_suites", sample["cipher_suites"]))
    lines.append(cpp_u16_list(f"k_{fixture_id}_non_grease_cipher_suites", sample["non_grease_cipher_suites"]))
    lines.append(cpp_u16_list(f"k_{fixture_id}_supported_groups", sample["supported_groups"]))
    lines.append(cpp_u16_list(f"k_{fixture_id}_non_grease_supported_groups", sample["non_grease_supported_groups"]))
    lines.append(cpp_u16_list(f"k_{fixture_id}_extension_order", sample["extension_types"]))
    lines.append(cpp_u16_list(f"k_{fixture_id}_extension_order_no_padding", sample["non_grease_extensions_without_padding"]))
    lines.append(cpp_key_share_entries(f"k_{fixture_id}_key_share_entries", sample["key_share_entries"]))
    if sample.get("ech"):
        ech = sample["ech"]
        lines.append(
            f"//   ech: type=0xFE0D enc_length={ech['enc_length']} payload_length={ech['payload_length']} "
            f"outer={ech['outer_type']} kdf={ech['kdf_id']} aead={ech['aead_id']}"
        )
    if sample.get("alpn_protocols"):
        lines.append("//   alpn: " + ", ".join(sample["alpn_protocols"]))
    if sample.get("compress_certificate_algorithms"):
        lines.append(
            "//   compress_certificate algorithms: " + ", ".join(sample["compress_certificate_algorithms"])
        )
    return "\n".join(lines)


def render_sample_markdown(sample: dict[str, Any]) -> str:
    lines = [f"fixture_id: {sample['fixture_id']}"]
    lines.append(f"frame_number: {sample['frame_number']}")
    lines.append(f"tcp_stream: {sample['tcp_stream']}")
    lines.append(f"sni: {sample['sni']}")
    lines.append("cipher_suites: " + ", ".join(sample["cipher_suites"]))
    lines.append("non_grease_cipher_suites: " + ", ".join(sample["non_grease_cipher_suites"]))
    lines.append("supported_groups: " + ", ".join(sample["supported_groups"]))
    lines.append("extension_order: " + ", ".join(sample["extension_types"]))
    if sample.get("alpn_protocols"):
        lines.append("alpn_protocols: " + ", ".join(sample["alpn_protocols"]))
    if sample.get("compress_certificate_algorithms"):
        lines.append(
            "compress_certificate_algorithms: " + ", ".join(sample["compress_certificate_algorithms"])
        )
    if sample.get("ech"):
        ech = sample["ech"]
        lines.append(
            f"ech: enc_length={ech['enc_length']} payload_length={ech['payload_length']} outer={ech['outer_type']} "
            f"kdf={ech['kdf_id']} aead={ech['aead_id']}"
        )
    for entry in sample["key_share_entries"]:
        lines.append(
            f"key_share: group={entry['group']} key_exchange_length={entry['key_exchange_length']} grease={entry['is_grease_group']}"
        )
    return "\n".join(lines)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Render extracted TLS ClientHello fixture artifacts into stable refresh summaries or C++ literals."
    )
    parser.add_argument("--artifact", required=True, help="Path to the JSON artifact produced by extract_client_hello_fixtures.py")
    parser.add_argument("--format", default="cpp", choices=["cpp", "markdown", "json"],
                        help="Output format")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    artifact = load_artifact(pathlib.Path(args.artifact).resolve())
    samples = artifact.get("samples", [])
    if not samples:
        raise SystemExit("artifact has no samples")

    if args.format == "json":
        json.dump(artifact, sys.stdout, indent=2, sort_keys=True)
        sys.stdout.write("\n")
        return 0

    rendered: list[str] = []
    for sample in samples:
        if args.format == "cpp":
            rendered.append(render_sample_cpp(sample))
        else:
            rendered.append(render_sample_markdown(sample))
    sys.stdout.write("\n\n".join(rendered))
    if not rendered[-1].endswith("\n"):
        sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())