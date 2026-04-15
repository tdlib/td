#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT

from __future__ import annotations

import argparse
import dataclasses
import hashlib
import hmac
import json
import pathlib
import re
from typing import Any, Mapping, Sequence

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]

ROLE_ORDER = ("primary", "secondary", "auxiliary")
ROLE_BYTES = {
    "primary": 0x01,
    "secondary": 0x02,
    "auxiliary": 0x03,
}
ROLE_SUFFIX = {
    "primary": "Primary",
    "secondary": "Secondary",
    "auxiliary": "Auxiliary",
}
LEGACY_PEM_SOURCES = {
    "primary": ("td/telegram/net/PublicRsaKeySharedMain.cpp", "CSlice retained_primary_block()"),
    "secondary": ("td/telegram/net/PublicRsaKeySharedMain.cpp", "CSlice retained_secondary_block()"),
    "auxiliary": ("td/telegram/ConfigManager.cpp", "CSlice retained_auxiliary_block()"),
}
SENTINEL_HEADERS = (
    ("tdutils/td/utils/HashIndexSeeds.h", "kHashIndexSeeds"),
    ("tdnet/td/net/SessionTicketSeeds.h", "kSessionTicketSeeds"),
    ("td/mtproto/PacketAlignmentSeeds.h", "kPacketAlignmentSeeds"),
    ("td/telegram/net/ConfigCacheSeeds.h", "kConfigCacheSeeds"),
)
SHARD_HEADERS = {
    "protocol_fingerprint_table": ("td/mtproto/ProtocolFingerprintTable.h", "kProtocolFingerprintTable"),
    "entropy_mix_table": ("tdutils/td/utils/EntropyMixTable.h", "kEntropyMixTable"),
}
SHARD_HEADER_NAMES = {
    "protocol_fingerprint_table": {
        role: f"kProtocolFingerprintTable{ROLE_SUFFIX[role]}" for role in ROLE_ORDER
    },
    "entropy_mix_table": {
        role: f"kEntropyMixTable{ROLE_SUFFIX[role]}" for role in ROLE_ORDER
    },
}
CURRENT_CROSS_CHECK_LABELS = {
    "catalog_weight_table": b"table_mix_v1_gamma",
    "route_window_table": b"table_mix_v1_delta",
    "session_blend_table": b"table_mix_v1_sigma",
    "config_window_table": b"table_mix_v1_theta",
}
CURRENT_CROSS_CHECK_HEADERS = {
    "catalog_weight_table": ("tdutils/td/utils/CatalogWeightTable.h", {
        "primary": "kCatalogWeightPrimary",
        "secondary": "kCatalogWeightSecondary",
    }),
    "route_window_table": ("tdnet/td/net/RouteWindowTable.h", {
        "primary": "kRouteWindowPrimary",
        "secondary": "kRouteWindowSecondary",
    }),
    "session_blend_table": ("td/telegram/SessionBlendTable.h", {
        "primary": "kSessionBlendPrimary",
        "secondary": "kSessionBlendSecondary",
    }),
    "config_window_table": ("td/mtproto/ConfigWindowTable.h", {
        "auxiliary": "kConfigWindowAuxiliary",
    }),
}

HEADER_PREFIX = """//\n// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026\n//\n// Distributed under the Boost Software License, Version 1.0. (See accompanying\n// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)\n//\n#pragma once\n\nnamespace td {\nnamespace vault_detail {\n\n"""
HEADER_SUFFIX = "\n}  // namespace vault_detail\n}  // namespace td\n"
ARRAY_VALUES_PER_LINE = 16


@dataclasses.dataclass(frozen=True)
class RepoSnapshot:
    public_keys: dict[str, str]
    sentinels: tuple[bytes, bytes, bytes, bytes]


@dataclasses.dataclass(frozen=True)
class StaticTableArtifacts:
    sentinels: tuple[bytes, bytes, bytes, bytes]
    shards: dict[str, dict[str, bytes]]
    cross_checks: dict[str, dict[str, int]]
    manifest: dict[str, Any]


def normalize_pem(pem: str) -> str:
    return pem.replace("\r\n", "\n").strip("\n") + "\n"


def _read_text(path: pathlib.Path) -> str:
    return path.read_text(encoding="utf-8")


def _extract_pem_literal(content: str, function_signature: str) -> str:
    signature_pos = content.find(function_signature)
    if signature_pos == -1:
        raise ValueError(f"missing function signature: {function_signature}")
    return_pos = content.find("return", signature_pos)
    if return_pos == -1:
        raise ValueError(f"missing return statement for {function_signature}")
    end_pos = content.find(';', return_pos)
    if end_pos == -1:
        raise ValueError(f"missing statement terminator for {function_signature}")

    parts: list[str] = []
    position = return_pos
    while True:
        quote_pos = content.find('"', position)
        if quote_pos == -1 or quote_pos >= end_pos:
            break
        chunk: list[str] = []
        index = quote_pos + 1
        while index < end_pos:
            char = content[index]
            if char == '\\':
                if index + 1 >= end_pos:
                    raise ValueError(f"unterminated escape in {function_signature}")
                escaped = content[index + 1]
                if escaped == 'n':
                    chunk.append('\n')
                elif escaped == 'r':
                    chunk.append('\r')
                elif escaped == 't':
                    chunk.append('\t')
                else:
                    chunk.append(escaped)
                index += 2
                continue
            if char == '"':
                position = index + 1
                break
            chunk.append(char)
            index += 1
        parts.append(''.join(chunk))

    if not parts:
        raise ValueError(f"empty PEM literal in {function_signature}")
    return normalize_pem(''.join(parts))


def _parse_byte_array(header_text: str, symbol_name: str) -> bytes:
    match = re.search(rf"{re.escape(symbol_name)}\[\]\s*=\s*\{{(.*?)\}};", header_text, re.S)
    if match is None:
        raise ValueError(f"missing byte array: {symbol_name}")
    values = re.findall(r"0x([0-9a-fA-F]{1,2})", match.group(1))
    if not values:
        raise ValueError(f"empty byte array: {symbol_name}")
    return bytes(int(value, 16) for value in values)


def _parse_uint64_constant(header_text: str, symbol_name: str) -> int:
    match = re.search(rf"{re.escape(symbol_name)}\s*=\s*(0x[0-9a-fA-F]+)ULL;", header_text)
    if match is None:
        raise ValueError(f"missing uint64 constant: {symbol_name}")
    return int(match.group(1), 16)


def _minimal_big_endian(value: int) -> bytes:
    return value.to_bytes(max(1, (value.bit_length() + 7) // 8), "big")


def _tl_string(data: bytes) -> bytes:
    if len(data) < 254:
        header = bytes((len(data),))
    elif len(data) < (1 << 24):
        header = bytes((254,)) + len(data).to_bytes(3, "little")
    else:
        raise ValueError("TL string too large")
    raw = header + data
    return raw + (b"\x00" * ((-len(raw)) % 4))


def _validate_required_roles(public_keys: Mapping[str, str]) -> None:
    missing = [role for role in ROLE_ORDER if role not in public_keys]
    if missing:
        raise ValueError(f"missing required roles: {', '.join(missing)}")
    unknown = sorted(set(public_keys) - set(ROLE_ORDER))
    if unknown:
        raise ValueError(f"unknown roles: {', '.join(unknown)}")


def _validate_sentinels(sentinels: Sequence[bytes]) -> tuple[bytes, bytes, bytes, bytes]:
    if len(sentinels) != 4:
        raise ValueError("expected exactly 4 sentinels")
    normalized = [bytes(sentinel) for sentinel in sentinels]
    if any(len(sentinel) != 32 for sentinel in normalized):
        raise ValueError("all sentinels must be 32-byte values")
    return tuple(normalized)  # type: ignore[return-value]


def _key_material(sentinels: Sequence[bytes]) -> bytes:
    return b"".join(_validate_sentinels(sentinels))


def _derive_runtime_keys(sentinels: Sequence[bytes]) -> tuple[bytes, bytes]:
    key_material = _key_material(sentinels)
    aes_key = hmac.new(key_material, b"rsa_vault_v1_key", hashlib.sha256).digest()
    mac_key = hmac.new(key_material, b"rsa_vault_v1_mac", hashlib.sha256).digest()
    return aes_key, mac_key


def _expand_stream(key: bytes, label: bytes, size: int) -> bytes:
    chunks: list[bytes] = []
    counter = 0
    while sum(len(chunk) for chunk in chunks) < size:
        chunks.append(hmac.new(key, label + counter.to_bytes(4, "little"), hashlib.sha256).digest())
        counter += 1
    return b"".join(chunks)[:size]


def _pkcs7_pad(plaintext: bytes) -> bytes:
    padding = 16 - (len(plaintext) % 16)
    return plaintext + bytes((padding,)) * padding


def _pkcs7_unpad(plaintext: bytes) -> bytes:
    if not plaintext or len(plaintext) % 16 != 0:
        raise ValueError("invalid plaintext block size")
    padding = plaintext[-1]
    if padding == 0 or padding > 16 or plaintext[-padding:] != bytes((padding,)) * padding:
        raise ValueError("invalid PKCS#7 padding")
    return plaintext[:-padding]


def _aes_cbc_encrypt(key: bytes, iv: bytes, plaintext: bytes) -> bytes:
    encryptor = Cipher(algorithms.AES(key), modes.CBC(iv)).encryptor()
    return encryptor.update(plaintext) + encryptor.finalize()


def _aes_cbc_decrypt(key: bytes, iv: bytes, ciphertext: bytes) -> bytes:
    decryptor = Cipher(algorithms.AES(key), modes.CBC(iv)).decryptor()
    return decryptor.update(ciphertext) + decryptor.finalize()


def compute_slot_fingerprint(pem: str) -> int:
    try:
        public_key = serialization.load_pem_public_key(normalize_pem(pem).encode("utf-8"))
    except (TypeError, ValueError) as exc:
        raise ValueError("invalid RSA public key PEM") from exc
    if not isinstance(public_key, rsa.RSAPublicKey):
        raise ValueError("invalid RSA public key PEM")
    numbers = public_key.public_numbers()
    serialized = _tl_string(_minimal_big_endian(numbers.n)) + _tl_string(_minimal_big_endian(numbers.e))
    return int.from_bytes(hashlib.sha1(serialized).digest()[12:20], "little", signed=False)


def compute_slot_fingerprints(public_keys: Mapping[str, str]) -> dict[str, int]:
    _validate_required_roles(public_keys)
    return {role: compute_slot_fingerprint(public_keys[role]) for role in ROLE_ORDER}


def build_check_tables(fingerprints: Mapping[str, int], sentinels: Sequence[bytes]) -> dict[str, dict[str, int]]:
    _validate_required_roles({role: "present" for role in fingerprints})
    key_material = _key_material(sentinels)
    checks: dict[str, dict[str, int]] = {}
    for check_name, label in CURRENT_CROSS_CHECK_LABELS.items():
        mask = hmac.new(label, key_material, hashlib.sha256).digest()
        if check_name == "config_window_table":
            checks[check_name] = {
                "auxiliary": fingerprints["auxiliary"] ^ int.from_bytes(mask[16:24], "little"),
            }
        else:
            checks[check_name] = {
                "primary": fingerprints["primary"] ^ int.from_bytes(mask[0:8], "little"),
                "secondary": fingerprints["secondary"] ^ int.from_bytes(mask[8:16], "little"),
            }
    return checks


def _encrypt_public_keys(public_keys: Mapping[str, str], sentinels: Sequence[bytes]) -> dict[str, tuple[bytes, bytes]]:
    _validate_required_roles(public_keys)
    aes_key, mac_key = _derive_runtime_keys(sentinels)
    key_material = _key_material(sentinels)
    encrypted: dict[str, tuple[bytes, bytes]] = {}
    for role in ROLE_ORDER:
        plaintext = normalize_pem(public_keys[role]).encode("utf-8")
        padded = _pkcs7_pad(plaintext)
        iv = _expand_stream(key_material, f"table_mix_v1_iv:{role}".encode("ascii"), 16)
        ciphertext = _aes_cbc_encrypt(aes_key, iv, padded)
        mac = hmac.new(mac_key, iv + ciphertext + bytes((ROLE_BYTES[role],)), hashlib.sha256).digest()
        blob = iv + ciphertext + mac
        right_shard = _expand_stream(key_material, f"table_mix_v1_split:{role}".encode("ascii"), len(blob))
        left_shard = bytes(left ^ right for left, right in zip(blob, right_shard))
        encrypted[role] = (left_shard, right_shard)
    return encrypted


def recover_slot_fingerprints(shards: Mapping[str, Mapping[str, bytes]], sentinels: Sequence[bytes]) -> dict[str, int]:
    aes_key, mac_key = _derive_runtime_keys(sentinels)
    left_shards = shards.get("protocol_fingerprint_table", {})
    right_shards = shards.get("entropy_mix_table", {})
    fingerprints: dict[str, int] = {}
    for role in ROLE_ORDER:
        left = left_shards.get(role)
        right = right_shards.get(role)
        if left is None or right is None:
            raise ValueError(f"missing shard data for {role}")
        if len(left) != len(right):
            raise ValueError(f"shard size mismatch for {role}")
        blob = bytes(left_byte ^ right_byte for left_byte, right_byte in zip(left, right))
        if len(blob) < 48 or (len(blob) - 48) % 16 != 0:
            raise ValueError(f"invalid encrypted key blob size for {role}")
        iv = blob[:16]
        ciphertext = blob[16:-32]
        expected_mac = blob[-32:]
        actual_mac = hmac.new(mac_key, iv + ciphertext + bytes((ROLE_BYTES[role],)), hashlib.sha256).digest()
        if actual_mac != expected_mac:
            raise ValueError(f"encrypted key blob MAC mismatch for {role}")
        plaintext = _pkcs7_unpad(_aes_cbc_decrypt(aes_key, iv, ciphertext))
        fingerprints[role] = compute_slot_fingerprint(plaintext.decode("utf-8"))
    return fingerprints


def build_static_table_artifacts(public_keys: Mapping[str, str], sentinels: Sequence[bytes]) -> StaticTableArtifacts:
    normalized_sentinels = _validate_sentinels(sentinels)
    fingerprints = compute_slot_fingerprints(public_keys)
    encrypted = _encrypt_public_keys(public_keys, normalized_sentinels)
    cross_checks = build_check_tables(fingerprints, normalized_sentinels)
    shards = {
        "protocol_fingerprint_table": {role: encrypted[role][0] for role in ROLE_ORDER},
        "entropy_mix_table": {role: encrypted[role][1] for role in ROLE_ORDER},
    }
    manifest = {
        "format_version": 1,
        "generator": {
            "path": "tools/catalog_sync/refresh_static_tables.py",
            "cross_check_labels": {name: label.decode("ascii") for name, label in CURRENT_CROSS_CHECK_LABELS.items()},
        },
        "roles": {
            role: {
                "fingerprint": f"0x{fingerprints[role]:016x}",
                "role_byte": ROLE_BYTES[role],
                "blob_size": len(encrypted[role][0]),
                "pem_sha256": hashlib.sha256(normalize_pem(public_keys[role]).encode("utf-8")).hexdigest(),
            }
            for role in ROLE_ORDER
        },
    }
    return StaticTableArtifacts(normalized_sentinels, shards, cross_checks, manifest)


def render_state_manifest_json(manifest: Mapping[str, Any]) -> str:
    return json.dumps(manifest, indent=2, sort_keys=True) + "\n"


def load_repo_material(repo_root: pathlib.Path) -> RepoSnapshot:
    repo_root = repo_root.resolve()
    public_keys = {
        role: _extract_pem_literal(_read_text(repo_root / relative_path), function_signature)
        for role, (relative_path, function_signature) in LEGACY_PEM_SOURCES.items()
    }
    generated = load_repo_table_data(repo_root)
    return RepoSnapshot(public_keys=public_keys, sentinels=generated.sentinels)


def load_repo_table_data(repo_root: pathlib.Path) -> StaticTableArtifacts:
    repo_root = repo_root.resolve()
    sentinels = [_parse_byte_array(_read_text(repo_root / relative_path), symbol_name) for relative_path, symbol_name in SENTINEL_HEADERS]
    shards = {
        group_name: {
            role: _parse_byte_array(_read_text(repo_root / SHARD_HEADERS[group_name][0]), SHARD_HEADER_NAMES[group_name][role])
            for role in ROLE_ORDER
        }
        for group_name in SHARD_HEADERS
    }
    cross_checks = {
        group_name: {
            role: _parse_uint64_constant(_read_text(repo_root / relative_path), symbol_name)
            for role, symbol_name in symbol_names.items()
        }
        for group_name, (relative_path, symbol_names) in CURRENT_CROSS_CHECK_HEADERS.items()
    }
    return StaticTableArtifacts(_validate_sentinels(sentinels), shards, cross_checks, {})


def _format_byte_rows(values: bytes) -> str:
    rows = []
    for start in range(0, len(values), ARRAY_VALUES_PER_LINE):
        rows.append("    " + ", ".join(f"0x{value:02x}" for value in values[start : start + ARRAY_VALUES_PER_LINE]))
    return ",\n".join(rows)


def _render_array_header(symbols: Mapping[str, bytes]) -> str:
    body: list[str] = []
    for symbol_name, values in symbols.items():
        body.append(f"inline constexpr unsigned char {symbol_name}[] = {{")
        body.append(_format_byte_rows(values))
        body.append("};\n")
    return HEADER_PREFIX + "\n".join(body).rstrip() + HEADER_SUFFIX


def _render_constant_header(symbols: Mapping[str, int]) -> str:
    body = [f"inline constexpr unsigned long long {symbol_name} = 0x{value:016x}ULL;" for symbol_name, value in symbols.items()]
    return HEADER_PREFIX + "\n".join(body) + "\n" + HEADER_SUFFIX


def render_header_files(artifacts: StaticTableArtifacts) -> dict[str, str]:
    return {
        SENTINEL_HEADERS[0][0]: _render_array_header({SENTINEL_HEADERS[0][1]: artifacts.sentinels[0]}),
        SENTINEL_HEADERS[1][0]: _render_array_header({SENTINEL_HEADERS[1][1]: artifacts.sentinels[1]}),
        SENTINEL_HEADERS[2][0]: _render_array_header({SENTINEL_HEADERS[2][1]: artifacts.sentinels[2]}),
        SENTINEL_HEADERS[3][0]: _render_array_header({SENTINEL_HEADERS[3][1]: artifacts.sentinels[3]}),
        SHARD_HEADERS["protocol_fingerprint_table"][0]: _render_array_header(
            {SHARD_HEADER_NAMES["protocol_fingerprint_table"][role]: artifacts.shards["protocol_fingerprint_table"][role] for role in ROLE_ORDER}
        ),
        SHARD_HEADERS["entropy_mix_table"][0]: _render_array_header(
            {SHARD_HEADER_NAMES["entropy_mix_table"][role]: artifacts.shards["entropy_mix_table"][role] for role in ROLE_ORDER}
        ),
        CURRENT_CROSS_CHECK_HEADERS["catalog_weight_table"][0]: _render_constant_header(
            {CURRENT_CROSS_CHECK_HEADERS["catalog_weight_table"][1][role]: artifacts.cross_checks["catalog_weight_table"][role] for role in ("primary", "secondary")}
        ),
        CURRENT_CROSS_CHECK_HEADERS["route_window_table"][0]: _render_constant_header(
            {CURRENT_CROSS_CHECK_HEADERS["route_window_table"][1][role]: artifacts.cross_checks["route_window_table"][role] for role in ("primary", "secondary")}
        ),
        CURRENT_CROSS_CHECK_HEADERS["session_blend_table"][0]: _render_constant_header(
            {CURRENT_CROSS_CHECK_HEADERS["session_blend_table"][1][role]: artifacts.cross_checks["session_blend_table"][role] for role in ("primary", "secondary")}
        ),
        CURRENT_CROSS_CHECK_HEADERS["config_window_table"][0]: _render_constant_header(
            {CURRENT_CROSS_CHECK_HEADERS["config_window_table"][1]["auxiliary"]: artifacts.cross_checks["config_window_table"]["auxiliary"]}
        ),
    }


def derive_seed_rows_from_seed(seed: bytes) -> tuple[bytes, bytes, bytes, bytes]:
    if not seed:
        raise ValueError("seed must not be empty")
    return tuple(_expand_stream(seed, f"table_mix_v1_seed:{index}".encode("ascii"), 32) for index in range(4))  # type: ignore[return-value]


def _parse_hex_sentinel(value: str) -> bytes:
    parsed = bytes.fromhex(value.strip().removeprefix("0x"))
    if len(parsed) != 32:
        raise ValueError("sentinel hex inputs must decode to 32 bytes")
    return parsed


def _resolve_public_keys(args: argparse.Namespace) -> dict[str, str]:
    snapshot = load_repo_material(pathlib.Path(args.repo_root))
    public_keys = dict(snapshot.public_keys)
    for role in ROLE_ORDER:
        path = getattr(args, f"{role}_pem", None)
        if path:
            public_keys[role] = normalize_pem(pathlib.Path(path).read_text(encoding="utf-8"))
    return public_keys


def _resolve_sentinels(args: argparse.Namespace) -> tuple[bytes, bytes, bytes, bytes]:
    if args.sentinel_hex:
        return _validate_sentinels([_parse_hex_sentinel(value) for value in args.sentinel_hex])
    if args.seed_hex:
        return derive_seed_rows_from_seed(bytes.fromhex(args.seed_hex.removeprefix("0x")))
    return load_repo_material(pathlib.Path(args.repo_root)).sentinels


def _write_output_files(output_root: pathlib.Path, headers: Mapping[str, str]) -> None:
    for relative_path, content in headers.items():
        destination = output_root / relative_path
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(content, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Refresh deterministic static table artifacts used by the transport runtime.")
    parser.add_argument("--repo-root", default=str(REPO_ROOT), help="Repository root used for loading legacy PEM literals and existing sentinels")
    parser.add_argument("--output-root", help="Directory where generated header files should be written")
    parser.add_argument("--manifest-path", help="Path for writing the generated JSON manifest")
    parser.add_argument("--seed-hex", help="Optional deterministic seed used to derive four 32-byte sentinels")
    parser.add_argument("--sentinel-hex", action="append", help="Explicit 32-byte sentinel in hex; pass four times to override existing sentinels")
    parser.add_argument("--primary-pem", dest="primary_pem", help="Path to a PEM file for the primary bundled block")
    parser.add_argument("--secondary-pem", dest="secondary_pem", help="Path to a PEM file for the secondary bundled block")
    parser.add_argument("--auxiliary-pem", dest="auxiliary_pem", help="Path to a PEM file for the auxiliary bundled block")
    args = parser.parse_args()

    artifacts = build_static_table_artifacts(_resolve_public_keys(args), _resolve_sentinels(args))
    if args.output_root:
        _write_output_files(pathlib.Path(args.output_root), render_header_files(artifacts))
    if args.manifest_path:
        manifest_path = pathlib.Path(args.manifest_path)
        manifest_path.parent.mkdir(parents=True, exist_ok=True)
        manifest_path.write_text(render_state_manifest_json(artifacts.manifest), encoding="utf-8")
    if not args.output_root and not args.manifest_path:
        print(render_state_manifest_json(artifacts.manifest), end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())