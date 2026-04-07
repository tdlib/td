#!/usr/bin/env python3

import argparse
import datetime as dt
import hashlib
import json
import pathlib
import subprocess
import sys
from typing import Any


PARSER_VERSION = "tls-clienthello-parser-v1"
DEFAULT_DISPLAY_FILTER = "tcp && tls.handshake.type == 1"


def run_command(argv: list[str]) -> str:
    result = subprocess.run(argv, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        raise RuntimeError(
            f"command failed ({result.returncode}): {' '.join(argv)}\n{result.stderr.strip()}"
        )
    return result.stdout


def read_sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as infile:
        while True:
            chunk = infile.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def tshark_version() -> str:
    output = run_command(["tshark", "-v"])
    first_line = output.splitlines()[0].strip()
    return first_line


def u16_hex(value: int) -> str:
    return f"0x{value:04X}"


def u8_hex(value: int) -> str:
    return f"0x{value:02X}"


def is_grease(value: int) -> bool:
    low = value & 0xFF
    high = (value >> 8) & 0xFF
    return low == high and (low & 0x0F) == 0x0A


class Reader:
    def __init__(self, data: bytes) -> None:
        self.data = data
        self.offset = 0

    def left(self) -> int:
        return len(self.data) - self.offset

    def read_u8(self) -> int:
        if self.left() < 1:
            raise ValueError("unexpected EOF while reading u8")
        value = self.data[self.offset]
        self.offset += 1
        return value

    def read_u16(self) -> int:
        if self.left() < 2:
            raise ValueError("unexpected EOF while reading u16")
        value = int.from_bytes(self.data[self.offset : self.offset + 2], "big")
        self.offset += 2
        return value

    def read_u24(self) -> int:
        if self.left() < 3:
            raise ValueError("unexpected EOF while reading u24")
        value = int.from_bytes(self.data[self.offset : self.offset + 3], "big")
        self.offset += 3
        return value

    def read_bytes(self, length: int) -> bytes:
        if length < 0 or self.left() < length:
            raise ValueError("unexpected EOF while reading bytes")
        value = self.data[self.offset : self.offset + length]
        self.offset += length
        return value


def parse_alpn(body: bytes) -> list[str]:
    reader = Reader(body)
    total_length = reader.read_u16()
    if reader.left() != total_length:
        raise ValueError("ALPN extension length mismatch")
    protocols: list[str] = []
    while reader.left() > 0:
        item_length = reader.read_u8()
        protocols.append(reader.read_bytes(item_length).decode("ascii", errors="strict"))
    return protocols


def parse_supported_groups(body: bytes) -> list[int]:
    reader = Reader(body)
    total_length = reader.read_u16()
    if total_length % 2 != 0 or reader.left() != total_length:
        raise ValueError("supported_groups extension length mismatch")
    groups: list[int] = []
    while reader.left() > 0:
        groups.append(reader.read_u16())
    return groups


def parse_key_share(body: bytes) -> list[dict[str, Any]]:
    reader = Reader(body)
    total_length = reader.read_u16()
    if reader.left() != total_length:
        raise ValueError("key_share extension length mismatch")
    entries: list[dict[str, Any]] = []
    while reader.left() > 0:
        group = reader.read_u16()
        key_exchange_length = reader.read_u16()
        key_exchange = reader.read_bytes(key_exchange_length)
        entries.append(
            {
                "group": u16_hex(group),
                "key_exchange_length": key_exchange_length,
                "key_exchange_sha256": hashlib.sha256(key_exchange).hexdigest(),
                "is_grease_group": is_grease(group),
            }
        )
    return entries


def parse_compress_certificate(body: bytes) -> list[str]:
    if not body:
        return []
    reader = Reader(body)
    total_length = reader.read_u8()
    if reader.left() != total_length or total_length % 2 != 0:
        raise ValueError("compress_certificate extension length mismatch")
    values: list[str] = []
    while reader.left() > 0:
        values.append(u16_hex(reader.read_u16()))
    return values


def parse_ech(body: bytes) -> dict[str, Any]:
    reader = Reader(body)
    outer_type = reader.read_u8()
    kdf_id = reader.read_u16()
    aead_id = reader.read_u16()
    config_id = reader.read_u8()
    enc_length = reader.read_u16()
    enc = reader.read_bytes(enc_length)
    payload_length = reader.read_u16()
    payload = reader.read_bytes(payload_length)
    if reader.left() != 0:
        raise ValueError("ECH extension contains trailing bytes")
    return {
        "outer_type": u8_hex(outer_type),
        "kdf_id": u16_hex(kdf_id),
        "aead_id": u16_hex(aead_id),
        "config_id": u8_hex(config_id),
        "enc_length": enc_length,
        "enc_sha256": hashlib.sha256(enc).hexdigest(),
        "payload_length": payload_length,
        "payload_sha256": hashlib.sha256(payload).hexdigest(),
    }


def parse_client_hello(record: bytes) -> dict[str, Any]:
    reader = Reader(record)
    record_type = reader.read_u8()
    record_version = reader.read_u16()
    record_length = reader.read_u16()
    if reader.left() != record_length:
        raise ValueError("TLS record length mismatch")

    handshake_type = reader.read_u8()
    handshake_length = reader.read_u24()
    handshake_start = reader.offset
    if reader.left() != handshake_length:
        raise ValueError("TLS handshake length mismatch")

    legacy_version = reader.read_u16()
    random_bytes = reader.read_bytes(32)
    session_id_length = reader.read_u8()
    session_id = reader.read_bytes(session_id_length)

    cipher_suites_length = reader.read_u16()
    if cipher_suites_length % 2 != 0:
        raise ValueError("cipher suite vector length must be even")
    cipher_suite_bytes = reader.read_bytes(cipher_suites_length)
    cipher_suites = [
        int.from_bytes(cipher_suite_bytes[index : index + 2], "big")
        for index in range(0, len(cipher_suite_bytes), 2)
    ]

    compression_length = reader.read_u8()
    compression_methods = list(reader.read_bytes(compression_length))

    extensions_length = reader.read_u16()
    extensions_bytes = reader.read_bytes(extensions_length)
    if reader.left() != 0:
        raise ValueError("ClientHello contains trailing bytes")

    extension_reader = Reader(extensions_bytes)
    extensions: list[dict[str, Any]] = []
    supported_groups: list[int] = []
    key_share_entries: list[dict[str, Any]] = []
    alpn_protocols: list[str] = []
    ech: dict[str, Any] | None = None
    compress_certificate_algorithms: list[str] = []
    sni = ""

    while extension_reader.left() > 0:
        ext_type = extension_reader.read_u16()
        ext_length = extension_reader.read_u16()
        ext_body = extension_reader.read_bytes(ext_length)
        extensions.append(
            {
                "type": u16_hex(ext_type),
                "length": ext_length,
                "is_grease": is_grease(ext_type),
                "body_hex": ext_body.hex(),
            }
        )
        if ext_type == 0x0000:
            server_name_reader = Reader(ext_body)
            list_length = server_name_reader.read_u16()
            if server_name_reader.left() != list_length:
                raise ValueError("server_name extension length mismatch")
            name_type = server_name_reader.read_u8()
            if name_type != 0:
                raise ValueError("unexpected server_name type")
            name_length = server_name_reader.read_u16()
            sni = server_name_reader.read_bytes(name_length).decode("utf-8", errors="replace")
            if server_name_reader.left() != 0:
                raise ValueError("server_name extension trailing bytes")
        elif ext_type == 0x000A:
            supported_groups = parse_supported_groups(ext_body)
        elif ext_type == 0x0010:
            alpn_protocols = parse_alpn(ext_body)
        elif ext_type == 0x001B:
            compress_certificate_algorithms = parse_compress_certificate(ext_body)
        elif ext_type == 0x0033:
            key_share_entries = parse_key_share(ext_body)
        elif ext_type == 0xFE0D:
            ech = parse_ech(ext_body)

    if extension_reader.left() != 0:
        raise ValueError("extension parser stopped early")

    non_grease_cipher_suites = [value for value in cipher_suites if not is_grease(value)]
    non_grease_supported_groups = [value for value in supported_groups if not is_grease(value)]
    extension_types = [entry["type"] for entry in extensions]
    non_grease_extensions = [entry["type"] for entry in extensions if not entry["is_grease"]]
    non_grease_extensions_without_padding = [
        entry["type"] for entry in extensions if not entry["is_grease"] and entry["type"] != u16_hex(0x0015)
    ]

    return {
        "record_type": u8_hex(record_type),
        "record_version": u16_hex(record_version),
        "record_length": record_length,
        "handshake_type": handshake_type,
        "handshake_length": handshake_length,
        "legacy_version": u16_hex(legacy_version),
        "session_id_length": session_id_length,
        "session_id_sha256": hashlib.sha256(session_id).hexdigest(),
        "random_sha256": hashlib.sha256(random_bytes).hexdigest(),
        "compression_methods": [u8_hex(value) for value in compression_methods],
        "cipher_suites": [u16_hex(value) for value in cipher_suites],
        "non_grease_cipher_suites": [u16_hex(value) for value in non_grease_cipher_suites],
        "has_grease_cipher_suite": any(is_grease(value) for value in cipher_suites),
        "supported_groups": [u16_hex(value) for value in supported_groups],
        "non_grease_supported_groups": [u16_hex(value) for value in non_grease_supported_groups],
        "has_grease_supported_group": any(is_grease(value) for value in supported_groups),
        "key_share_entries": key_share_entries,
        "extension_types": extension_types,
        "non_grease_extensions": non_grease_extensions,
        "non_grease_extensions_without_padding": non_grease_extensions_without_padding,
        "has_grease_extension": any(entry["is_grease"] for entry in extensions),
        "extensions": extensions,
        "sni": sni,
        "alpn_protocols": alpn_protocols,
        "compress_certificate_algorithms": compress_certificate_algorithms,
        "ech": ech,
        "client_hello_sha256": hashlib.sha256(record[handshake_start : handshake_start + handshake_length]).hexdigest(),
        "tls_record_sha256": hashlib.sha256(record).hexdigest(),
    }


def collect_frames(pcap_path: pathlib.Path, display_filter: str) -> list[dict[str, str]]:
    output = run_command(
        [
            "tshark",
            "-r",
            str(pcap_path),
            "-Y",
            display_filter,
            "-T",
            "fields",
            "-E",
            "separator=|",
            "-e",
            "frame.number",
            "-e",
            "frame.time_epoch",
            "-e",
            "tcp.stream",
            "-e",
            "tls.record.version",
            "-e",
            "tls.record.length",
            "-e",
            "tls.handshake.type",
            "-e",
            "tcp.reassembled.data",
        ]
    )
    frames: list[dict[str, str]] = []
    for line in output.splitlines():
        if not line.strip():
            continue
        parts = line.split("|")
        if len(parts) != 7:
            raise RuntimeError(f"unexpected tshark field row: {line}")
        frames.append(
            {
                "frame_number": parts[0],
                "frame_time_epoch": parts[1],
                "tcp_stream": parts[2],
                "tls_record_version": parts[3],
                "tls_record_length": parts[4],
                "tls_handshake_type": parts[5],
                "tcp_reassembled_data": parts[6],
            }
        )
    return frames


def frame_time_utc(epoch_string: str) -> str:
    epoch = float(epoch_string)
    return dt.datetime.fromtimestamp(epoch, tz=dt.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def build_fixture_id(profile_id: str | None, pcap_path: pathlib.Path, frame_number: str) -> str:
    stem = profile_id if profile_id else pcap_path.stem.replace(" ", "_")
    return f"{stem}:frame{frame_number}"


def validate_source_kind(args: argparse.Namespace) -> None:
    if args.source_kind != "curl_cffi_capture":
        return
    required = [
        ("capture_tool", args.capture_tool),
        ("capture_tool_version", args.capture_tool_version),
        ("curl_cffi_version", args.curl_cffi_version),
        ("browser_type", args.browser_type),
        ("target_host", args.target_host),
        ("intercept_method", args.intercept_method),
    ]
    missing = [name for name, value in required if not value]
    if missing:
        raise SystemExit(
            "curl_cffi_capture requires the following metadata fields: " + ", ".join(missing)
        )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Extract provenance-checked TLS ClientHello fixture summaries from pcap/pcapng captures."
    )
    parser.add_argument("--pcap", required=True, help="Input pcap/pcapng path")
    parser.add_argument("--out", required=True, help="Output JSON artifact path")
    parser.add_argument("--profile-id", help="Logical fixture family/profile id for generated fixture ids")
    parser.add_argument("--source-kind", default="browser_capture",
                        choices=["browser_capture", "curl_cffi_capture"],
                        help="Primary provenance source kind")
    parser.add_argument("--capture-date-utc", help="Capture date in UTC, for example 2026-04-07T11:33:00Z")
    parser.add_argument("--scenario-id", default="fixture_refresh", help="Scenario id stored in the artifact")
    parser.add_argument("--route-mode", default="unknown", help="Route mode stored in the artifact")
    parser.add_argument("--display-filter", default=DEFAULT_DISPLAY_FILTER,
                        help="tshark display filter used to select ClientHello frames")
    parser.add_argument("--frame-number", action="append",
                        help="Optional specific frame.number to extract; can be repeated")
    parser.add_argument("--frame-limit", type=int, default=0,
                        help="Optional max number of matching frames to emit after filtering")
    parser.add_argument("--capture-tool", help="Capture tool name for curl_cffi_capture provenance")
    parser.add_argument("--capture-tool-version", help="Capture tool version for curl_cffi_capture provenance")
    parser.add_argument("--curl-cffi-version", help="curl_cffi version for curl_cffi_capture provenance")
    parser.add_argument("--browser-type", help="curl_cffi BrowserType name for curl_cffi_capture provenance")
    parser.add_argument("--target-host", help="Target host used by curl_cffi_capture")
    parser.add_argument("--intercept-method", help="Intercept method used by curl_cffi_capture")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    validate_source_kind(args)

    pcap_path = pathlib.Path(args.pcap).resolve()
    output_path = pathlib.Path(args.out).resolve()
    if not pcap_path.exists():
        raise SystemExit(f"pcap path does not exist: {pcap_path}")

    frames = collect_frames(pcap_path, args.display_filter)
    if args.frame_number:
        allowed = set(args.frame_number)
        frames = [frame for frame in frames if frame["frame_number"] in allowed]
    if args.frame_limit > 0:
        frames = frames[: args.frame_limit]
    if not frames:
        raise SystemExit("no matching ClientHello frames found")

    samples: list[dict[str, Any]] = []
    for frame in frames:
        raw_hex = frame["tcp_reassembled_data"].strip()
        if not raw_hex:
            raise SystemExit(
                f"frame {frame['frame_number']} is missing tcp.reassembled.data; ensure TCP reassembly is available"
            )
        raw_record = bytes.fromhex(raw_hex)
        parsed = parse_client_hello(raw_record)
        parsed.update(
            {
                "fixture_id": build_fixture_id(args.profile_id, pcap_path, frame["frame_number"]),
                "frame_number": int(frame["frame_number"]),
                "frame_time_epoch": frame["frame_time_epoch"],
                "frame_time_utc": frame_time_utc(frame["frame_time_epoch"]),
                "tcp_stream": int(frame["tcp_stream"]),
                "source_tls_record_version": u16_hex(int(frame["tls_record_version"], 0))
                if frame["tls_record_version"]
                else parsed["record_version"],
                "source_tls_record_length": int(frame["tls_record_length"]) if frame["tls_record_length"] else parsed["record_length"],
                "source_tls_handshake_type": int(frame["tls_handshake_type"]) if frame["tls_handshake_type"] else parsed["handshake_type"],
            }
        )
        samples.append(parsed)

    capture_date_utc = args.capture_date_utc if args.capture_date_utc else samples[0]["frame_time_utc"]

    artifact: dict[str, Any] = {
        "artifact_type": "tls_clienthello_fixtures",
        "parser_version": PARSER_VERSION,
        "generated_at_utc": utc_now(),
        "source_path": str(pcap_path),
        "source_sha256": read_sha256(pcap_path),
        "source_kind": args.source_kind,
        "capture_date_utc": capture_date_utc,
        "scenario_id": args.scenario_id,
        "route_mode": args.route_mode,
        "display_filter": args.display_filter,
        "transport": "tcp",
        "tls_handshake_type": 1,
        "extractor": {
            "name": "extract_client_hello_fixtures.py",
            "tshark_version": tshark_version(),
        },
        "samples": samples,
    }
    if args.profile_id:
        artifact["profile_id"] = args.profile_id
    if args.source_kind == "curl_cffi_capture":
        artifact.update(
            {
                "capture_tool": args.capture_tool,
                "capture_tool_version": args.capture_tool_version,
                "curl_cffi_version": args.curl_cffi_version,
                "browser_type": args.browser_type,
                "target_host": args.target_host,
                "intercept_method": args.intercept_method,
            }
        )

    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("w", encoding="utf-8") as outfile:
        json.dump(artifact, outfile, indent=2, sort_keys=True)
        outfile.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())