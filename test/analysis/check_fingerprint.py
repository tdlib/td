#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
import pathlib
import sys
from typing import Any

from common_tls import ClientHello, has_extension, load_clienthello_artifact, load_profile_registry, profile_requires_ech


ALLOWED_ECH_PAYLOAD_LENGTHS = {144, 176, 208, 240}
ALPS_EXTENSION_TYPES = {0x4469, 0x44CD}
KNOWN_TELEGRAM_JA3_HASHES = {"e0e58235789a753608b12649376e91ec"}
EXACT_JA3_PIN_FIELDS = ("exact_ja3", "exact_ja3_hash", "exact_ja3_hashes")
EXACT_JA4_PIN_FIELDS = ("exact_ja4", "exact_ja4_signature", "exact_ja4_signatures", "exact_ja4_token", "exact_ja4_tokens")


def is_grease(value: int) -> bool:
    low = value & 0xFF
    high = (value >> 8) & 0xFF
    return low == high and (low & 0x0F) == 0x0A


def get_profile_config(sample: ClientHello, registry: dict[str, Any]) -> dict[str, Any]:
    profiles = registry.get("profiles")
    if not isinstance(profiles, dict) or sample.profile not in profiles:
        raise ValueError(f"unknown profile in registry: {sample.profile}")
    profile = profiles[sample.profile]
    if not isinstance(profile, dict):
        raise ValueError(f"profile config must be an object: {sample.profile}")
    return profile


def parse_ec_point_formats(sample: ClientHello) -> list[int]:
    for extension in sample.extensions:
        if extension.type != 0x000B or not extension.body:
            continue
        total_length = extension.body[0]
        formats = extension.body[1:]
        if len(formats) != total_length:
            return []
        return list(formats)
    return []


def compute_ja3_hash(sample: ClientHello) -> str:
    cipher_suites = "-".join(str(cipher_suite) for cipher_suite in sample.cipher_suites if not is_grease(cipher_suite))
    extension_types = "-".join(
        str(extension.type)
        for extension in sample.extensions
        if extension.type != 0x0015 and not is_grease(extension.type)
    )
    supported_groups = "-".join(str(group) for group in sample.supported_groups if not is_grease(group))
    ec_point_formats = "-".join(str(point_format) for point_format in parse_ec_point_formats(sample))
    canonical = f"771,{cipher_suites},{extension_types},{supported_groups},{ec_point_formats}"
    return hashlib.md5(canonical.encode("ascii"), usedforsecurity=False).hexdigest()


def _ja4_hash12(value: str) -> str:
    if not value:
        return "000000000000"
    return hashlib.sha256(value.encode("ascii")).hexdigest()[:12]


def _ja4_first_last(value: str) -> tuple[str, str]:
    sanitized = [character if character.isascii() else "9" for character in value]
    if not sanitized:
        return "0", "0"
    if len(sanitized) == 1:
        return sanitized[0], "0"
    return sanitized[0], sanitized[-1]


def _ja4_tls_version(sample: ClientHello) -> str:
    if sample.metadata.tls_gen == "tls13":
        return "13"
    if sample.metadata.tls_gen == "tls12":
        return "12"
    return "00"


def parse_signature_algorithms(sample: ClientHello) -> list[str]:
    for extension in sample.extensions:
        if extension.type != 0x000D:
            continue
        if len(extension.body) < 2:
            return []
        total_length = int.from_bytes(extension.body[:2], "big")
        algorithms = extension.body[2:]
        if total_length != len(algorithms) or total_length % 2 != 0:
            return []
        return [algorithms[index : index + 2].hex() for index in range(0, len(algorithms), 2)]
    return []


def compute_ja4_signature(sample: ClientHello) -> str:
    transport_marker = "q" if "quic" in sample.metadata.transport.lower() else "t"
    non_grease_ciphers = [cipher_suite for cipher_suite in sample.cipher_suites if not is_grease(cipher_suite)]
    non_grease_extensions = [extension.type for extension in sample.extensions if not is_grease(extension.type)]
    sni_marker = "d" if 0x0000 in non_grease_extensions else "i"
    alpn_value = sample.alpn_protocols[0] if sample.alpn_protocols else ""
    alpn_first, alpn_last = _ja4_first_last(alpn_value)
    first_chunk = (
        f"{transport_marker}{_ja4_tls_version(sample)}{sni_marker}"
        f"{min(99, len(non_grease_ciphers)):02}{min(99, len(non_grease_extensions)):02}{alpn_first}{alpn_last}"
    )

    cipher_chunk = ",".join(sorted(f"{cipher_suite:04x}" for cipher_suite in non_grease_ciphers))
    extension_chunk = ",".join(
        sorted(f"{extension_type:04x}" for extension_type in non_grease_extensions if extension_type not in {0x0000, 0x0010})
    )
    signature_algorithms = parse_signature_algorithms(sample)
    extension_signature_chunk = extension_chunk
    if signature_algorithms:
        extension_signature_chunk = f"{extension_chunk}_{','.join(signature_algorithms)}"

    return f"{first_chunk}_{_ja4_hash12(cipher_chunk)}_{_ja4_hash12(extension_signature_chunk)}"


def check_profile_platform_policy(sample: ClientHello, registry: dict[str, Any]) -> bool:
    profile = get_profile_config(sample, registry)
    allowed_tags = profile.get("allowed_tags")
    if not isinstance(allowed_tags, dict):
        return True
    platform_classes = allowed_tags.get("platform_class")
    if isinstance(platform_classes, list) and platform_classes and sample.metadata.device_class not in platform_classes:
        return False
    os_families = allowed_tags.get("os_family")
    if isinstance(os_families, list) and os_families and sample.metadata.os_family not in os_families:
        return False
    return True


def get_matching_fixture_metadata(sample: ClientHello, registry: dict[str, Any]) -> dict[str, Any] | None:
    profile = get_profile_config(sample, registry)
    include_fixture_ids = profile.get("include_fixture_ids")
    fixtures = registry.get("fixtures")
    if not isinstance(include_fixture_ids, list) or not isinstance(fixtures, dict):
        return None
    if not sample.metadata.source_sha256:
        return None

    for fixture_id in include_fixture_ids:
        fixture = fixtures.get(fixture_id)
        if not isinstance(fixture, dict):
            continue
        if fixture.get("source_path") != sample.metadata.source_path:
            continue
        fixture_sha256 = fixture.get("source_sha256")
        if not fixture_sha256 or fixture_sha256 != sample.metadata.source_sha256:
            continue
        return fixture
    return None


def check_profile_fixture_provenance_policy(sample: ClientHello, registry: dict[str, Any]) -> bool:
    profile = get_profile_config(sample, registry)
    allowed_tags = profile.get("allowed_tags")
    if not isinstance(allowed_tags, dict):
        return True

    fixture = get_matching_fixture_metadata(sample, registry) or {}

    effective_family = sample.metadata.fixture_family_id or str(fixture.get("family", ""))
    effective_source_kind = sample.metadata.source_kind
    if effective_source_kind in {"", "unknown"}:
        effective_source_kind = str(fixture.get("source_kind", "unknown"))
    effective_tls_gen = sample.metadata.tls_gen
    if effective_tls_gen in {"", "unknown"}:
        effective_tls_gen = str(fixture.get("tls_gen", "unknown"))
    effective_transport = sample.metadata.transport
    if effective_transport in {"", "unknown"}:
        effective_transport = str(fixture.get("transport", "unknown"))

    fixture_families = allowed_tags.get("family")
    if isinstance(fixture_families, list) and fixture_families and effective_family not in fixture_families:
        return False

    source_kinds = allowed_tags.get("source_kind")
    if isinstance(source_kinds, list) and source_kinds and effective_source_kind not in source_kinds:
        return False

    tls_gens = allowed_tags.get("tls_gen")
    if isinstance(tls_gens, list) and tls_gens and effective_tls_gen not in tls_gens:
        return False

    transports = allowed_tags.get("transport")
    if isinstance(transports, list) and transports and effective_transport not in transports:
        return False

    return True


def _parse_expected_extension_order(values: Any) -> list[int] | None:
    if not isinstance(values, list):
        return None
    parsed: list[int] = []
    for value in values:
        if isinstance(value, str):
            parsed.append(int(value, 16))
        else:
            parsed.append(int(value))
    return parsed


def check_extension_order_policy(sample: ClientHello, registry: dict[str, Any]) -> bool:
    profile = get_profile_config(sample, registry)
    policy = profile.get("extension_order_policy")
    if policy in (None, "", False):
        return True

    fixture = get_matching_fixture_metadata(sample, registry)
    if not isinstance(fixture, dict):
        return False

    expected_order = _parse_expected_extension_order(fixture.get("non_grease_extensions_without_padding"))
    if expected_order is None:
        return False

    observed_order = sample.non_grease_extensions_without_padding
    if policy == "FixedFromFixture":
        return observed_order == expected_order
    if policy == "ChromeShuffleAnchored":
        return Counter(observed_order) == Counter(expected_order)
    return False


def check_anti_telegram_ja3_policy(sample: ClientHello, registry: dict[str, Any]) -> bool:
    profile = get_profile_config(sample, registry)
    fingerprint_policy = profile.get("fingerprint_policy")
    if not isinstance(fingerprint_policy, dict) or not fingerprint_policy.get("require_anti_telegram_ja3", False):
        return True

    denylist = fingerprint_policy.get("telegram_ja3_hashes")
    effective_denylist = KNOWN_TELEGRAM_JA3_HASHES if not isinstance(denylist, list) else set(denylist) | KNOWN_TELEGRAM_JA3_HASHES
    return compute_ja3_hash(sample) not in effective_denylist


def check_pq_group_policy(sample: ClientHello, registry: dict[str, Any]) -> bool:
    profile = get_profile_config(sample, registry)
    expected = profile.get("pq_group")
    if expected in (None, "", False):
        return True
    expected_group = int(expected, 16) if isinstance(expected, str) else int(expected)
    return expected_group in sample.supported_groups and expected_group in sample.key_share_groups


def check_alps_policy(sample: ClientHello, registry: dict[str, Any]) -> bool:
    profile = get_profile_config(sample, registry)
    expected = profile.get("alps_type")
    present_alps = {extension.type for extension in sample.extensions if extension.type in ALPS_EXTENSION_TYPES}
    if expected in (None, "", False):
        return not present_alps
    expected_type = int(expected, 16) if isinstance(expected, str) else int(expected)
    return present_alps == {expected_type}


def _has_exact_pin(fingerprint_policy: dict[str, Any], field_names: tuple[str, ...]) -> bool:
    for field_name in field_names:
        value = fingerprint_policy.get(field_name)
        if value in (None, "", False):
            continue
        if isinstance(value, list) and not value:
            continue
        return True
    return False


def validate_fingerprint_policy_config(registry: dict[str, Any]) -> list[str]:
    profiles = registry.get("profiles")
    if not isinstance(profiles, dict):
        raise ValueError("profile registry must contain a top-level profiles object")

    failures: list[str] = []
    for profile_name, profile in profiles.items():
        if not isinstance(profile, dict):
            raise ValueError(f"profile config must be an object: {profile_name}")
        fingerprint_policy = profile.get("fingerprint_policy")
        if not isinstance(fingerprint_policy, dict):
            continue
        if not fingerprint_policy.get("allow_exact_ja3_pin", False) and _has_exact_pin(
            fingerprint_policy, EXACT_JA3_PIN_FIELDS
        ):
            failures.append(f"profile[{profile_name}]: Exact JA3 pin disabled")
        if not fingerprint_policy.get("allow_exact_ja4_pin", False) and _has_exact_pin(
            fingerprint_policy, EXACT_JA4_PIN_FIELDS
        ):
            failures.append(f"profile[{profile_name}]: Exact JA4 pin disabled")
    return failures


def collect_fingerprint_telemetry(samples: list[ClientHello]) -> dict[str, Any]:
    profiles: dict[str, dict[str, Any]] = {}
    for sample in samples:
        profile = profiles.setdefault(
            sample.profile,
            {
                "sample_count": 0,
                "ja3_hash_counts": {},
                "ja4_signature_counts": {},
            },
        )
        profile["sample_count"] += 1

        ja3_hash = compute_ja3_hash(sample)
        ja4_signature = compute_ja4_signature(sample)
        profile["ja3_hash_counts"][ja3_hash] = profile["ja3_hash_counts"].get(ja3_hash, 0) + 1
        profile["ja4_signature_counts"][ja4_signature] = profile["ja4_signature_counts"].get(ja4_signature, 0) + 1

    return {
        "profiles": profiles,
        "unique_ja3_hash_count": len({compute_ja3_hash(sample) for sample in samples}),
        "unique_ja4_signature_count": len({compute_ja4_signature(sample) for sample in samples}),
    }


def check_sample_policies(sample: ClientHello, registry: dict[str, Any]) -> list[str]:
    failures: list[str] = []
    has_legacy_ech = has_extension(sample, 0xFE02)
    has_new_ech = has_extension(sample, 0xFE0D)
    route_mode = sample.metadata.route_mode

    if has_legacy_ech:
        failures.append("No legacy ECH 0xFE02")

    if route_mode in {"unknown", "ru_egress"}:
        if has_new_ech or has_legacy_ech:
            failures.append("ECH route policy")
        return failures

    if route_mode != "non_ru_egress":
        failures.append("Route mode known")
        return failures

    if profile_requires_ech(sample.profile, registry) != has_new_ech:
        failures.append("ECH route policy")
    if not check_profile_platform_policy(sample, registry):
        failures.append("Profile matches platform hints")
    if not check_profile_fixture_provenance_policy(sample, registry):
        failures.append("Profile matches fixture provenance tags")
    if not check_extension_order_policy(sample, registry):
        failures.append("Extension order policy")
    if not check_anti_telegram_ja3_policy(sample, registry):
        failures.append("Anti-Telegram JA3")
    if not check_pq_group_policy(sample, registry):
        failures.append("PQ group policy")
    if not check_alps_policy(sample, registry):
        failures.append("ALPS policy")
    return failures


def check_ech_payload_variance(samples: list[ClientHello], registry: dict[str, Any]) -> bool:
    scoped_by_profile: dict[str, list[ClientHello]] = {}
    for sample in samples:
        profile = get_profile_config(sample, registry)
        fingerprint_policy = profile.get("fingerprint_policy")
        require_dispersion = isinstance(fingerprint_policy, dict) and fingerprint_policy.get(
            "require_noncollapsed_randomized_hashes", False
        )
        if not require_dispersion:
            continue
        if (
            sample.metadata.route_mode == "non_ru_egress"
            and profile_requires_ech(sample.profile, registry)
            and has_extension(sample, 0xFE0D)
        ):
            scoped_by_profile.setdefault(sample.profile, []).append(sample)

    if not scoped_by_profile:
        return True

    for scoped in scoped_by_profile.values():
        profile = get_profile_config(scoped[0], registry)
        fingerprint_policy = profile.get("fingerprint_policy")
        if len(scoped) < 64:
            return False
        payload_lengths = [sample.ech_payload_length for sample in scoped]
        if any(length not in ALLOWED_ECH_PAYLOAD_LENGTHS for length in payload_lengths):
            return False
        if len(set(payload_lengths)) < 2:
            return False
        if isinstance(fingerprint_policy, dict):
            min_unique_ja3 = fingerprint_policy.get("min_unique_ja3_per_64")
            if min_unique_ja3 not in (None, "", False):
                if len({compute_ja3_hash(sample) for sample in scoped}) < int(min_unique_ja3):
                    return False
            min_unique_ja4 = fingerprint_policy.get("min_unique_ja4_per_64")
            if min_unique_ja4 not in (None, "", False):
                if len({compute_ja4_signature(sample) for sample in scoped}) < int(min_unique_ja4):
                    return False
    return True


def run_all_checks(samples: list[ClientHello], registry: dict[str, Any]) -> tuple[bool, list[str]]:
    failures = validate_fingerprint_policy_config(registry)
    for index, sample in enumerate(samples):
        for failure in check_sample_policies(sample, registry):
            failures.append(f"sample[{index}]: {failure}")
    if not check_ech_payload_variance(samples, registry):
        failures.append("batch: ECH payload variance")
    return len(failures) == 0, failures


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run initial PR-9 fingerprint smoke checks against extracted ClientHello artifacts."
    )
    parser.add_argument(
        "--artifact",
        action="append",
        required=True,
        help="Path to an extracted ClientHello artifact JSON file. May be repeated.",
    )
    parser.add_argument("--registry", required=True, help="Path to profiles_validation.json or equivalent registry JSON")
    parser.add_argument("--report-out", help="Optional path to write a JSON report")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    registry = load_profile_registry(args.registry)
    samples: list[ClientHello] = []
    for artifact_path in args.artifact:
        samples.extend(load_clienthello_artifact(artifact_path))
    ok, failures = run_all_checks(samples, registry)

    report = {
        "ok": ok,
        "sample_count": len(samples),
        "failures": failures,
        "telemetry": collect_fingerprint_telemetry(samples),
    }
    if args.report_out:
        report_path = pathlib.Path(args.report_out)
        report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    else:
        sys.stdout.write(json.dumps(report, indent=2, sort_keys=True))
        sys.stdout.write("\n")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())