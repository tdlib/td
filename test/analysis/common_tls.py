#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import hashlib
import json
import pathlib
import re
from dataclasses import dataclass, field
from typing import Any

CANONICAL_ROUTE_MODES = {"unknown", "ru_egress", "non_ru_egress"}
CANONICAL_DEVICE_CLASSES = {"desktop", "mobile", "tablet", "unknown"}
CANONICAL_OS_FAMILIES = {"android", "ios", "linux", "macos", "windows", "unknown"}
AUTHORITATIVE_SOURCE_KINDS = frozenset({"browser_capture", "curl_cffi_capture", "pcap"})
ADVISORY_SOURCE_KINDS = frozenset({"utls_snapshot", "advisory_code_sample"})
CANONICAL_SOURCE_KINDS = AUTHORITATIVE_SOURCE_KINDS | ADVISORY_SOURCE_KINDS


def validate_source_kind(source_kind: str) -> str:
    if source_kind not in CANONICAL_SOURCE_KINDS:
        raise ValueError(f"unknown source_kind: {source_kind!r}; must be one of {sorted(CANONICAL_SOURCE_KINDS)}")
    return source_kind


def is_authoritative_source_kind(source_kind: str) -> bool:
    return source_kind in AUTHORITATIVE_SOURCE_KINDS


LEGACY_ROUTE_MODE_ALIASES = {
    "non_ru": "non_ru_egress",
    "ru": "ru_egress",
}
CLIENTHELLO_ARTIFACT_TYPE = "tls_clienthello_fixtures"
CLIENTHELLO_PARSER_VERSION = "tls-clienthello-parser-v1"
SERVERHELLO_ARTIFACT_TYPE = "tls_serverhello_fixtures"
SERVERHELLO_PARSER_VERSION = "tls-serverhello-parser-v1"

_SHA256_HEX_RE = re.compile(r"^[0-9a-f]{64}$")


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
    non_grease_supported_versions: list[int] = field(default_factory=list)
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
    client_profile_id: str = ""


@dataclass(frozen=True)
class ServerEndpoint:
    ip: str
    port: int


@dataclass(frozen=True)
class ServerHello:
    selected_version: int
    cipher_suite: int
    extensions: list[int]
    record_layout_signature: list[int]
    metadata: ServerHelloMeta
    server_endpoint: ServerEndpoint | None = None


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


REPO_TRAFFIC_DUMPS_PREFIX = "docs/Samples/Traffic dumps/"
_KNOWN_WORKSPACE_PREFIXES = [
    "/home/david_osipov/tdlib-obf/",
]


def validate_source_path_portable(source_path: str, repo_root: pathlib.Path | None = None) -> str:
    """Validate and convert a source_path to repository-relative form.
    Raises ValueError on traversal, symlink escape, or non-repo paths.
    """
    if not source_path:
        raise ValueError("source_path must be non-empty")
    # Strip known workspace prefixes
    rel_path = source_path
    for prefix in _KNOWN_WORKSPACE_PREFIXES:
        if source_path.startswith(prefix):
            rel_path = source_path[len(prefix):]
            break
    # Reject traversal
    if ".." in rel_path.split("/"):
        raise ValueError(f"source_path contains traversal: {source_path}")
    # Must be under Traffic dumps
    if not rel_path.startswith(REPO_TRAFFIC_DUMPS_PREFIX):
        raise ValueError(f"source_path not under {REPO_TRAFFIC_DUMPS_PREFIX}: {rel_path}")
    # Verify file exists if repo_root provided
    if repo_root is not None:
        full_path = repo_root / rel_path
        if not full_path.exists():
            raise ValueError(f"source_path does not exist: {full_path}")
        # Check no symlink escape
        try:
            resolved = full_path.resolve()
            if not str(resolved).startswith(str(repo_root.resolve())):
                raise ValueError(f"source_path escapes repo root via symlink: {source_path}")
        except OSError as e:
            raise ValueError(f"source_path resolution failed: {e}") from e
    return rel_path


def normalize_source_path(source_path: str) -> str:
    """Strip known workspace prefix, preserve exact byte spelling."""
    for prefix in _KNOWN_WORKSPACE_PREFIXES:
        if source_path.startswith(prefix):
            return source_path[len(prefix):]
    return source_path


def source_paths_identical(a: str, b: str) -> bool:
    """Two source paths are identical only if their normalized forms match byte-for-byte."""
    return normalize_source_path(a) == normalize_source_path(b)


def deduplicate_source_paths(paths: list[str]) -> list[str]:
    """Deduplicate source paths by normalized form, preserving first occurrence order."""
    seen: set[str] = set()
    result: list[str] = []
    for p in paths:
        norm = normalize_source_path(p)
        if norm not in seen:
            seen.add(norm)
            result.append(p)
    return result


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
        extensions.append(
            ParsedExtension(type=int(entry["type"], 16), body=bytes.fromhex(body_hex))
        )
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


def is_grease_version(v: int) -> bool:
    return (v & 0x0F0F) == 0x0A0A


def parse_supported_versions(body_hex: str | bytes) -> list[int]:
    """Parse a supported_versions extension body (0x002B) from its hex encoding
    or raw bytes.

    Input: hex string of the extension body, e.g. "063a3a03040303",
           or raw bytes of the extension body.
    Output: ordered list of uint16 version values with GREASE removed.

    The first byte is the vector length (number of remaining bytes).
    Each version is 2 bytes big-endian.
    Raises ValueError on malformed input.
    """
    if isinstance(body_hex, (bytes, bytearray)):
        body_hex = body_hex.hex()
    if not body_hex:
        raise ValueError("supported_versions body is empty")
    if len(body_hex) % 2 != 0:
        raise ValueError("supported_versions body has odd hex length")
    try:
        raw_bytes = bytes.fromhex(body_hex)
    except ValueError as exc:
        raise ValueError(f"supported_versions body contains non-hex characters: {exc}") from exc
    if len(raw_bytes) < 1:
        raise ValueError("supported_versions body is empty")
    vec_len = raw_bytes[0]
    payload = raw_bytes[1:]
    if vec_len != len(payload):
        raise ValueError(
            f"supported_versions length prefix mismatch: declared {vec_len}, "
            f"actual {len(payload)}"
        )
    if vec_len == 0:
        raise ValueError("supported_versions vector is empty")
    if vec_len % 2 != 0:
        raise ValueError(
            f"supported_versions vector length {vec_len} is odd (must be even for uint16 pairs)"
        )
    versions: list[int] = []
    for i in range(0, vec_len, 2):
        v = (payload[i] << 8) | payload[i + 1]
        if not is_grease_version(v):
            versions.append(v)
    return versions


def _infer_tls_gen(sample: dict[str, Any]) -> str:
    explicit = sample.get("tls_gen")
    if isinstance(explicit, str) and explicit:
        return explicit
    for entry in sample.get("extensions", []):
        if not isinstance(entry, dict):
            continue
        if str(entry.get("type", "")).lower() != "0x002b":
            continue
        body_hex = str(entry.get("body_hex", ""))
        try:
            versions = parse_supported_versions(body_hex)
        except ValueError:
            continue
        if 0x0304 in versions:
            return "tls13"
        if 0x0303 in versions:
            return "tls12"
    legacy_version = str(sample.get("legacy_version", "")).lower()
    if legacy_version == "0x0303":
        return "tls12"
    return "unknown"


def _validate_sha256_hex(field_name: str, value: str) -> None:
    normalized = value.strip().lower()
    if not _SHA256_HEX_RE.match(normalized):
        raise ValueError(f"{field_name} must be a 64-character lowercase hex digest")


def _parse_bounded_positive_int(
    field_name: str, value: Any, *, max_value: int | None = None
) -> int:
    try:
        parsed = int(value)
    except (TypeError, ValueError) as exc:
        raise ValueError(f"{field_name} must be int") from exc
    if parsed <= 0:
        raise ValueError(f"{field_name} must be positive")
    if max_value is not None and parsed > max_value:
        raise ValueError(f"{field_name} must be <= {max_value}")
    return parsed


def _validate_clienthello_record_metadata(
    sample: dict[str, Any], *, sample_index: int
) -> None:
    raw_record_lengths = sample.get("record_lengths")
    raw_record_count = sample.get("record_count")
    raw_record_length = sample.get("record_length")
    # Strict multi-record cross-validation is triggered only when the explicit
    # multi-record metadata fields ("record_lengths" or "record_count") are present.
    # "record_length" alone is a basic TLS record-header scalar that older fixtures
    # carry without the accompanying normalised list; treating it as a trigger for
    # the full multi-record schema requirement breaks backward compatibility with
    # every pre-multi-record fixture.
    has_record_metadata = "record_lengths" in sample or "record_count" in sample

    if not has_record_metadata:
        # Old-schema: record_length scalar is the only record field.
        # Validate it in isolation when present (positive int ≤ 65535 per TLS spec).
        if raw_record_length is not None:
            _parse_bounded_positive_int(
                f"sample[{sample_index}].record_length",
                raw_record_length,
                max_value=65535,
            )
        return

    if raw_record_lengths is None:
        raise ValueError(
            f"sample[{sample_index}].record_lengths must be present when record metadata is provided"
        )
    if not isinstance(raw_record_lengths, list) or not raw_record_lengths:
        raise ValueError(
            f"sample[{sample_index}].record_lengths must be a non-empty list"
        )

    record_lengths = [
        _parse_bounded_positive_int(
            f"sample[{sample_index}].record_lengths[{index}]",
            value,
            max_value=65535,
        )
        for index, value in enumerate(raw_record_lengths)
    ]

    if raw_record_count is not None:
        record_count = _parse_bounded_positive_int(
            f"sample[{sample_index}].record_count", raw_record_count
        )
        if record_count != len(record_lengths):
            raise ValueError(
                f"sample[{sample_index}].record_count must match record_lengths length"
            )

    if raw_record_length is not None:
        record_length = _parse_bounded_positive_int(
            f"sample[{sample_index}].record_length",
            raw_record_length,
            max_value=65535 * len(record_lengths),
        )
        if record_length != sum(record_lengths):
            raise ValueError(
                f"sample[{sample_index}].record_length must equal the sum of record_lengths"
            )


def _parse_server_endpoint(field_name: str, value: Any) -> ServerEndpoint:
    if not isinstance(value, dict):
        raise ValueError(f"{field_name} must be an object")
    ip = str(value.get("ip", "")).strip()
    if not ip:
        raise ValueError(f"{field_name}.ip must be non-empty")
    port = _parse_bounded_positive_int(
        f"{field_name}.port", value.get("port", 0), max_value=65535
    )
    return ServerEndpoint(ip=ip, port=port)


def _parse_observed_server_endpoints(
    artifact: dict[str, Any],
) -> set[tuple[str, int]] | None:
    if "observed_server_endpoints" not in artifact:
        return None

    raw_endpoints = artifact.get("observed_server_endpoints")
    if not isinstance(raw_endpoints, list) or not raw_endpoints:
        raise ValueError(
            "observed_server_endpoints must be a non-empty list when present"
        )

    parsed_endpoints: set[tuple[str, int]] = set()
    for index, raw_endpoint in enumerate(raw_endpoints):
        endpoint = _parse_server_endpoint(
            f"observed_server_endpoints[{index}]", raw_endpoint
        )
        key = (endpoint.ip, endpoint.port)
        if key in parsed_endpoints:
            raise ValueError("observed_server_endpoints must not contain duplicates")
        parsed_endpoints.add(key)
    return parsed_endpoints


def _require_string_field(artifact: dict[str, Any], field_name: str) -> str:
    value = str(artifact.get(field_name, "")).strip()
    if not value:
        raise ValueError(f"artifact must contain {field_name}")
    return value


def _validate_clienthello_artifact_header(artifact: dict[str, Any]) -> None:
    artifact_type = str(artifact.get("artifact_type", "")).strip()
    parser_version = str(artifact.get("parser_version", "")).strip()
    if artifact_type != CLIENTHELLO_ARTIFACT_TYPE:
        raise ValueError(f"artifact_type must be {CLIENTHELLO_ARTIFACT_TYPE}")
    if parser_version != CLIENTHELLO_PARSER_VERSION:
        raise ValueError(f"parser_version must be {CLIENTHELLO_PARSER_VERSION}")


def _validate_serverhello_artifact_header(artifact: dict[str, Any]) -> None:
    artifact_type = str(artifact.get("artifact_type", "")).strip()
    parser_version = str(artifact.get("parser_version", "")).strip()
    if artifact_type != SERVERHELLO_ARTIFACT_TYPE:
        raise ValueError(f"artifact_type must be {SERVERHELLO_ARTIFACT_TYPE}")
    if parser_version != SERVERHELLO_PARSER_VERSION:
        raise ValueError(f"parser_version must be {SERVERHELLO_PARSER_VERSION}")


def load_clienthello_artifact(path: str | pathlib.Path) -> list[ClientHello]:
    artifact_path = pathlib.Path(path)
    with artifact_path.open("r", encoding="utf-8") as infile:
        artifact = json.load(infile)

    _validate_clienthello_artifact_header(artifact)

    route_mode = normalize_route_mode(str(artifact.get("route_mode", "")))
    profile = _require_string_field(artifact, "profile_id")
    scenario_id = str(artifact.get("scenario_id", "")).strip()
    if not scenario_id:
        raise ValueError("reviewed artifact must contain non-empty scenario_id")
    device_class = normalize_device_class(str(artifact.get("device_class", "desktop")))
    os_family = normalize_os_family(str(artifact.get("os_family", "unknown")))
    transport = _require_string_field(artifact, "transport")
    source_kind = _require_string_field(artifact, "source_kind")
    fixture_family_id = str(
        artifact.get("fixture_family_id", artifact.get("family", ""))
    )
    source_path = _require_string_field(artifact, "source_path")
    source_sha256 = _require_string_field(artifact, "source_sha256").lower()
    _validate_sha256_hex("source_sha256", source_sha256)

    raw_samples = artifact.get("samples")
    if not isinstance(raw_samples, list):
        raise ValueError("artifact must contain a samples list")
    if not raw_samples:
        raise ValueError("artifact must contain at least one sample")

    samples: list[ClientHello] = []
    seen_fixture_ids: set[str] = set()
    for sample_index, sample in enumerate(raw_samples):
        if not isinstance(sample, dict):
            raise ValueError("sample entry must be an object")
        _validate_clienthello_record_metadata(sample, sample_index=sample_index)
        ech = sample.get("ech") or {}
        fixture_id = str(sample.get("fixture_id", "")).strip()
        if not fixture_id:
            raise ValueError("clienthello sample must contain fixture_id")
        if fixture_id in seen_fixture_ids:
            raise ValueError(f"duplicate fixture_id in artifact: {fixture_id}")
        seen_fixture_ids.add(fixture_id)
        key_share_groups = [
            int(entry["group"], 16)
            for entry in sample.get("key_share_entries", [])
            if isinstance(entry, dict)
        ]
        meta = SampleMeta(
            route_mode=route_mode,
            device_class=device_class,
            os_family=os_family,
            transport=transport,
            source_kind=source_kind,
            tls_gen=(
                _infer_tls_gen(sample)
                if not artifact.get("tls_gen")
                else str(artifact.get("tls_gen"))
            ),
            fixture_family_id=str(
                sample.get("fixture_family_id", sample.get("family", fixture_family_id))
            ),
            source_path=source_path,
            source_sha256=source_sha256,
            scenario_id=scenario_id,
            ts_us=0,
            fixture_id=fixture_id,
        )
        # Find 0x002B extension body
        supported_versions_body = ""
        for ext in sample.get("extensions", []):
            if isinstance(ext, dict) and str(ext.get("type", "")).lower() == "0x002b":
                supported_versions_body = str(ext.get("body_hex", ""))
                break
        non_grease_sv = parse_supported_versions(supported_versions_body) if supported_versions_body else []
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
                non_grease_supported_versions=non_grease_sv,
                ech_payload_length=ech.get("payload_length"),
            )
        )
    return samples


def _load_serverhello_common_metadata(
    artifact: dict[str, Any], artifact_path: pathlib.Path
) -> tuple[str, str, str, str, str, str, str, str, set[tuple[str, int]] | None]:
    route_mode = normalize_route_mode(str(artifact.get("route_mode", "")))
    scenario_id = str(artifact.get("scenario_id", "")).strip()
    if not scenario_id:
        raise ValueError("reviewed artifact must contain non-empty scenario_id")
    source_path = _require_string_field(artifact, "source_path")
    source_sha256 = _require_string_field(artifact, "source_sha256").lower()
    _validate_sha256_hex("source_sha256", source_sha256)
    parser_version = _require_string_field(artifact, "parser_version")
    transport = _require_string_field(artifact, "transport")
    source_kind = _require_string_field(artifact, "source_kind")
    capture_provenance = artifact.get("capture_provenance")
    if not isinstance(capture_provenance, dict):
        raise ValueError("artifact must contain capture_provenance")
    client_profile_id = str(capture_provenance.get("client_profile_id", "")).strip()
    if not client_profile_id:
        raise ValueError("capture_provenance.client_profile_id must be non-empty")
    observed_server_endpoints = _parse_observed_server_endpoints(artifact)
    return (
        route_mode,
        scenario_id,
        source_path,
        source_sha256,
        parser_version,
        transport,
        source_kind,
        client_profile_id,
        observed_server_endpoints,
    )


def _extract_serverhello_raw_samples(artifact: dict[str, Any]) -> list[dict[str, Any]]:
    raw_samples = artifact.get("samples")
    if not isinstance(raw_samples, list):
        raise ValueError("artifact must contain a samples list")
    if not raw_samples:
        raise ValueError("artifact must contain at least one sample")
    for sample in raw_samples:
        if not isinstance(sample, dict):
            raise ValueError("sample entry must be an object")
    return raw_samples


def _parse_serverhello_sample(
    *,
    sample: dict[str, Any],
    sample_index: int,
    artifact: dict[str, Any],
    route_mode: str,
    source_path: str,
    source_sha256: str,
    scenario_id: str,
    parser_version: str,
    transport: str,
    source_kind: str,
    client_profile_id: str,
) -> tuple[str, tuple[str, int] | None, ServerHello]:
    fixture_id = str(sample.get("fixture_id", "")).strip()
    if not fixture_id:
        raise ValueError("server hello sample must contain fixture_id")

    fixture_family_id = str(
        sample.get(
            "fixture_family_id", sample.get("family", artifact.get("family", ""))
        )
    )
    if not fixture_family_id:
        raise ValueError("server hello sample must contain fixture family")

    raw_endpoint = sample.get("server_endpoint")
    server_endpoint: ServerEndpoint | None = None
    endpoint_key: tuple[str, int] | None = None
    if raw_endpoint is not None:
        server_endpoint = _parse_server_endpoint(
            f"samples[{sample_index}].server_endpoint", raw_endpoint
        )
        endpoint_key = (server_endpoint.ip, server_endpoint.port)

    server_hello = ServerHello(
        selected_version=(
            int(sample["selected_version"], 16)
            if isinstance(sample.get("selected_version"), str)
            else int(sample["selected_version"])
        ),
        cipher_suite=(
            int(sample["cipher_suite"], 16)
            if isinstance(sample.get("cipher_suite"), str)
            else int(sample["cipher_suite"])
        ),
        extensions=_parse_u16_sequence(sample.get("extensions", [])),
        record_layout_signature=[
            int(value) for value in sample.get("record_layout_signature", [])
        ],
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
            client_profile_id=client_profile_id,
        ),
        server_endpoint=server_endpoint,
    )
    return fixture_id, endpoint_key, server_hello


def load_server_hello_artifact(path: str | pathlib.Path) -> list[ServerHello]:
    artifact_path = pathlib.Path(path)
    with artifact_path.open("r", encoding="utf-8") as infile:
        artifact = json.load(infile)

    _validate_serverhello_artifact_header(artifact)

    (
        route_mode,
        scenario_id,
        source_path,
        source_sha256,
        parser_version,
        transport,
        source_kind,
        client_profile_id,
        observed_server_endpoints,
    ) = _load_serverhello_common_metadata(artifact, artifact_path)
    raw_samples = _extract_serverhello_raw_samples(artifact)

    samples: list[ServerHello] = []
    seen_fixture_ids: set[str] = set()
    seen_sample_endpoints: set[tuple[str, int]] = set()
    for sample_index, sample in enumerate(raw_samples):
        fixture_id, endpoint_key, parsed_sample = _parse_serverhello_sample(
            sample=sample,
            sample_index=sample_index,
            artifact=artifact,
            route_mode=route_mode,
            source_path=source_path,
            source_sha256=source_sha256,
            scenario_id=scenario_id,
            parser_version=parser_version,
            transport=transport,
            source_kind=source_kind,
            client_profile_id=client_profile_id,
        )
        if endpoint_key is not None:
            seen_sample_endpoints.add(endpoint_key)
        if fixture_id in seen_fixture_ids:
            raise ValueError(f"duplicate fixture_id in artifact: {fixture_id}")
        seen_fixture_ids.add(fixture_id)
        samples.append(parsed_sample)
    if (
        observed_server_endpoints is not None
        and seen_sample_endpoints != observed_server_endpoints
    ):
        raise ValueError(
            "observed_server_endpoints must exactly match the set of sample.server_endpoint values"
        )
    return samples


def has_extension(sample: ClientHello, ext_type: int) -> bool:
    return any(extension.type == ext_type for extension in sample.extensions)


def profile_requires_ech(profile: str, registry: dict[str, Any]) -> bool:
    profiles = registry.get("profiles")
    if not isinstance(profiles, dict) or profile not in profiles:
        raise ValueError(f"unknown profile in registry: {profile}")
    ech_type = profiles[profile].get("ech_type")
    if isinstance(ech_type, dict):
        allow_present = bool(ech_type.get("allow_present", True))
        allow_absent = bool(ech_type.get("allow_absent", False))
        return allow_present and not allow_absent
    return ech_type not in (None, "", False)
