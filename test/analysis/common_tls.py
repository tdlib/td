#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
import pathlib
from typing import Any


CANONICAL_ROUTE_MODES = {"unknown", "ru_egress", "non_ru_egress"}
CANONICAL_DEVICE_CLASSES = {"desktop", "mobile", "tablet", "unknown"}
CANONICAL_OS_FAMILIES = {"android", "ios", "linux", "macos", "windows", "unknown"}
LEGACY_ROUTE_MODE_ALIASES = {
    "non_ru": "non_ru_egress",
    "ru": "ru_egress",
}


@dataclass(frozen=True)
class ParsedExtension:
    type: int
    body: bytes


@dataclass(frozen=True)
class SampleMeta:
    route_mode: str
    device_class: str
    os_family: str
    transport: str
    source_kind: str
    tls_gen: str
    fixture_family_id: str
    source_path: str
    source_sha256: str
    scenario_id: str
    ts_us: int
    fixture_id: str = ""


@dataclass(frozen=True)
class ClientHello:
    raw: bytes
    profile: str
    extensions: list[ParsedExtension]
    cipher_suites: list[int]
    supported_groups: list[int]
    key_share_groups: list[int]
    non_grease_extensions_without_padding: list[int]
    alpn_protocols: list[str]
    metadata: SampleMeta
    ech_payload_length: int | None = None


@dataclass(frozen=True)
class ServerHelloMeta:
    route_mode: str
    fixture_id: str
    fixture_family_id: str
    source_path: str
    source_sha256: str
    scenario_id: str
    parser_version: str
    transport: str
    source_kind: str


@dataclass(frozen=True)
class ServerHello:
    selected_version: int
    cipher_suite: int
    extensions: list[int]
    record_layout_signature: list[int]
    metadata: ServerHelloMeta


def normalize_route_mode(route_mode: str) -> str:
    canonical = LEGACY_ROUTE_MODE_ALIASES.get(route_mode, route_mode)
    if canonical not in CANONICAL_ROUTE_MODES:
        raise ValueError(f"unknown route_mode: {route_mode}")
    return canonical


def normalize_device_class(device_class: str) -> str:
    canonical = device_class.strip().lower().replace("-", "_")
    if canonical not in CANONICAL_DEVICE_CLASSES:
        raise ValueError(f"unknown device_class: {device_class}")
    return canonical


def normalize_os_family(os_family: str) -> str:
    canonical = os_family.strip().lower().replace("-", "_")
    if canonical not in CANONICAL_OS_FAMILIES:
        raise ValueError(f"unknown os_family: {os_family}")
    return canonical


def load_profile_registry(path: str | pathlib.Path) -> dict[str, Any]:
    registry_path = pathlib.Path(path)
    with registry_path.open("r", encoding="utf-8") as infile:
        registry = json.load(infile)
    if not isinstance(registry, dict) or not isinstance(registry.get("profiles"), dict):
        raise ValueError("profile registry must contain a top-level profiles object")
    return registry


def read_sha256(path: str | pathlib.Path) -> str:
    digest = hashlib.sha256()
    file_path = pathlib.Path(path)
    with file_path.open("rb") as infile:
        while True:
            chunk = infile.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def _parse_hex_list(values: list[str]) -> list[int]:
    return [int(value, 16) for value in values]


def _parse_extensions(sample: dict[str, Any]) -> list[ParsedExtension]:
    extensions: list[ParsedExtension] = []
    for entry in sample.get("extensions", []):
        body_hex = entry.get("body_hex", "")
        extensions.append(ParsedExtension(type=int(entry["type"], 16), body=bytes.fromhex(body_hex)))
    return extensions


def _parse_hex_extension_order(values: list[str]) -> list[int]:
    return [int(value, 16) for value in values]


def _parse_u16_sequence(values: list[Any]) -> list[int]:
    parsed: list[int] = []
    for value in values:
        if isinstance(value, str):
            parsed.append(int(value, 16))
        else:
            parsed.append(int(value))
    return parsed


def _infer_tls_gen(sample: dict[str, Any]) -> str:
    explicit_tls_gen = sample.get("tls_gen")
    if isinstance(explicit_tls_gen, str) and explicit_tls_gen:
        return explicit_tls_gen

    for entry in sample.get("extensions", []):
        if not isinstance(entry, dict):
            continue
        if str(entry.get("type", "")).lower() != "0x002b":
            continue
        body_hex = str(entry.get("body_hex", ""))
        if "0304" in body_hex.lower():
            return "tls13"

    legacy_version = str(sample.get("legacy_version", "")).lower()
    if legacy_version == "0x0303":
        return "tls12"
    return "unknown"


def load_clienthello_artifact(path: str | pathlib.Path) -> list[ClientHello]:
    artifact_path = pathlib.Path(path)
    with artifact_path.open("r", encoding="utf-8") as infile:
        artifact = json.load(infile)

    route_mode = normalize_route_mode(str(artifact.get("route_mode", "")))
    profile = str(artifact.get("profile_id", ""))
    scenario_id = str(artifact.get("scenario_id", artifact_path.stem))
    device_class = normalize_device_class(str(artifact.get("device_class", "desktop")))
    os_family = normalize_os_family(str(artifact.get("os_family", "unknown")))
    transport = str(artifact.get("transport", "tcp"))
    source_kind = str(artifact.get("source_kind", "unknown"))
    fixture_family_id = str(artifact.get("fixture_family_id", artifact.get("family", "")))
    source_path = str(artifact.get("source_path", ""))
    source_sha256 = str(artifact.get("source_sha256", ""))
    if not profile:
        raise ValueError("artifact must contain profile_id")

    raw_samples = artifact.get("samples")
    if not isinstance(raw_samples, list):
        raise ValueError("artifact must contain a samples list")
    if not raw_samples:
        raise ValueError("artifact must contain at least one sample")

    samples: list[ClientHello] = []
    for sample in raw_samples:
        ech = sample.get("ech") or {}
        fixture_id = str(sample.get("fixture_id", ""))
        key_share_groups = [
            int(entry["group"], 16) for entry in sample.get("key_share_entries", []) if isinstance(entry, dict)
        ]
        meta = SampleMeta(
            route_mode=route_mode,
            device_class=device_class,
            os_family=os_family,
            transport=transport,
            source_kind=source_kind,
            tls_gen=_infer_tls_gen(sample) if not artifact.get("tls_gen") else str(artifact.get("tls_gen")),
            fixture_family_id=str(sample.get("fixture_family_id", sample.get("family", fixture_family_id))),
            source_path=source_path,
            source_sha256=source_sha256,
            scenario_id=scenario_id,
            ts_us=0,
            fixture_id=fixture_id,
        )
        samples.append(
            ClientHello(
                raw=b"",
                profile=profile,
                extensions=_parse_extensions(sample),
                cipher_suites=_parse_hex_list(sample.get("cipher_suites", [])),
                supported_groups=_parse_hex_list(sample.get("supported_groups", [])),
                key_share_groups=key_share_groups,
                non_grease_extensions_without_padding=_parse_hex_extension_order(
                    sample.get("non_grease_extensions_without_padding", [])
                ),
                alpn_protocols=list(sample.get("alpn_protocols", [])),
                metadata=meta,
                ech_payload_length=ech.get("payload_length"),
            )
        )
    return samples


def load_server_hello_artifact(path: str | pathlib.Path) -> list[ServerHello]:
    artifact_path = pathlib.Path(path)
    with artifact_path.open("r", encoding="utf-8") as infile:
        artifact = json.load(infile)

    route_mode = normalize_route_mode(str(artifact.get("route_mode", "")))
    scenario_id = str(artifact.get("scenario_id", artifact_path.stem))
    source_path = str(artifact.get("source_path", ""))
    source_sha256 = str(artifact.get("source_sha256", ""))
    parser_version = str(artifact.get("parser_version", ""))
    transport = str(artifact.get("transport", "tcp"))
    source_kind = str(artifact.get("source_kind", "browser_capture"))
    if not parser_version:
        raise ValueError("artifact must contain parser_version")

    raw_samples = artifact.get("samples")
    if not isinstance(raw_samples, list):
        raise ValueError("artifact must contain a samples list")
    if not raw_samples:
        raise ValueError("artifact must contain at least one sample")

    samples: list[ServerHello] = []
    for sample in raw_samples:
        fixture_id = str(sample.get("fixture_id", ""))
        fixture_family_id = str(sample.get("fixture_family_id", sample.get("family", artifact.get("family", ""))))
        if not fixture_id:
            raise ValueError("server hello sample must contain fixture_id")
        if not fixture_family_id:
            raise ValueError("server hello sample must contain fixture family")
        samples.append(
            ServerHello(
                selected_version=int(sample["selected_version"], 16)
                if isinstance(sample.get("selected_version"), str)
                else int(sample["selected_version"]),
                cipher_suite=int(sample["cipher_suite"], 16)
                if isinstance(sample.get("cipher_suite"), str)
                else int(sample["cipher_suite"]),
                extensions=_parse_u16_sequence(sample.get("extensions", [])),
                record_layout_signature=[int(value) for value in sample.get("record_layout_signature", [])],
                metadata=ServerHelloMeta(
                    route_mode=route_mode,
                    fixture_id=fixture_id,
                    fixture_family_id=fixture_family_id,
                    source_path=source_path,
                    source_sha256=source_sha256,
                    scenario_id=scenario_id,
                    parser_version=parser_version,
                    transport=transport,
                    source_kind=source_kind,
                ),
            )
        )
    return samples


def has_extension(sample: ClientHello, ext_type: int) -> bool:
    return any(extension.type == ext_type for extension in sample.extensions)


def profile_requires_ech(profile: str, registry: dict[str, Any]) -> bool:
    profiles = registry.get("profiles")
    if not isinstance(profiles, dict) or profile not in profiles:
        raise ValueError(f"unknown profile in registry: {profile}")
    ech_type = profiles[profile].get("ech_type")
    return ech_type not in (None, "", False)