# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

import pathlib
import sys
import tempfile
import unittest

import json


THIS_DIR = pathlib.Path(__file__).resolve().parent
if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))

from check_fingerprint import (
    check_ech_payload_variance,
    check_sample_policies,
    collect_fingerprint_telemetry,
    compute_ja3_hash,
    compute_ja4_signature,
    validate_fingerprint_policy_config,
)
from common_tls import ClientHello, ParsedExtension, SampleMeta, load_clienthello_artifact, normalize_route_mode


def make_sample(
    route_mode: str,
    *,
    profile: str = "Chrome133",
    payload_length=None,
    extensions=None,
    cipher_suites=None,
    supported_groups=None,
    key_share_groups=None,
    alpn_protocols=None,
    device_class: str = "desktop",
    os_family: str = "linux",
    source_kind: str = "browser_capture",
    fixture_family_id: str = "chrome133_like",
    tls_gen: str = "tls13",
    transport: str = "tcp",
    source_path: str = "/tmp/unit-artifact.pcapng",
    source_sha256: str = "unit-sha256",
    non_grease_extensions_without_padding=None,
) -> ClientHello:
    parsed_extensions = [] if extensions is None else list(extensions)
    return ClientHello(
        raw=b"",
        profile=profile,
        extensions=parsed_extensions,
        cipher_suites=[] if cipher_suites is None else list(cipher_suites),
        supported_groups=[] if supported_groups is None else list(supported_groups),
        key_share_groups=[] if key_share_groups is None else list(key_share_groups),
        non_grease_extensions_without_padding=[]
        if non_grease_extensions_without_padding is None
        else list(non_grease_extensions_without_padding),
        alpn_protocols=[] if alpn_protocols is None else list(alpn_protocols),
        metadata=SampleMeta(
            route_mode=route_mode,
            device_class=device_class,
            os_family=os_family,
            transport=transport,
            source_kind=source_kind,
            tls_gen=tls_gen,
            fixture_family_id=fixture_family_id,
            source_path=source_path,
            source_sha256=source_sha256,
            scenario_id="unit-scenario",
            ts_us=1,
        ),
        ech_payload_length=payload_length,
    )


def make_extension(ext_type: int) -> ParsedExtension:
    return ParsedExtension(type=ext_type, body=b"")


def make_extension_with_body(ext_type: int, body_hex: str) -> ParsedExtension:
    return ParsedExtension(type=ext_type, body=bytes.fromhex(body_hex))


class NormalizeRouteModeTest(unittest.TestCase):
    def test_normalizes_legacy_non_ru_alias(self) -> None:
        self.assertEqual(normalize_route_mode("non_ru"), "non_ru_egress")

    def test_rejects_unknown_route_mode(self) -> None:
        with self.assertRaises(ValueError):
            normalize_route_mode("internet")


class LoadClientHelloArtifactTest(unittest.TestCase):
    def test_rejects_missing_samples_list(self) -> None:
        artifact = {
            "profile_id": "Chrome133",
            "route_mode": "non_ru_egress",
            "scenario_id": "chrome133_missing_samples",
            "source_path": "/captures/chrome133.pcapng",
            "source_sha256": "artifact-sha256",
        }

        with tempfile.TemporaryDirectory() as temp_dir:
            artifact_path = pathlib.Path(temp_dir) / "chrome133.clienthello.json"
            artifact_path.write_text(json.dumps(artifact), encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "artifact must contain a samples list"):
                load_clienthello_artifact(artifact_path)

    def test_rejects_empty_samples_list(self) -> None:
        artifact = {
            "profile_id": "Chrome133",
            "route_mode": "non_ru_egress",
            "scenario_id": "chrome133_empty_samples",
            "source_path": "/captures/chrome133.pcapng",
            "source_sha256": "artifact-sha256",
            "samples": [],
        }

        with tempfile.TemporaryDirectory() as temp_dir:
            artifact_path = pathlib.Path(temp_dir) / "chrome133.clienthello.json"
            artifact_path.write_text(json.dumps(artifact), encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "artifact must contain at least one sample"):
                load_clienthello_artifact(artifact_path)

    def test_preserves_platform_metadata_from_artifact(self) -> None:
        artifact = {
            "profile_id": "AndroidChrome146",
            "route_mode": "non_ru_egress",
            "scenario_id": "android_pixel9_clienthello",
            "source_path": "/captures/android_pixel9.pcapng",
            "source_sha256": "artifact-sha256",
            "source_kind": "browser_capture",
            "fixture_family_id": "android_okhttp_like",
            "tls_gen": "tls13",
            "device_class": "mobile",
            "os_family": "android",
            "transport": "tcp",
            "samples": [
                {
                    "cipher_suites": ["0x1301"],
                    "supported_groups": ["0x001D"],
                    "key_share_entries": [{"group": "0x001D"}],
                    "non_grease_extensions_without_padding": ["0x000A", "0xFE0D"],
                    "alpn_protocols": ["h2", "http/1.1"],
                    "extensions": [{"type": "0xFE0D", "body_hex": ""}],
                    "ech": {"payload_length": 208},
                }
            ],
        }

        with tempfile.TemporaryDirectory() as temp_dir:
            artifact_path = pathlib.Path(temp_dir) / "android.clienthello.json"
            artifact_path.write_text(json.dumps(artifact), encoding="utf-8")

            samples = load_clienthello_artifact(artifact_path)

        self.assertEqual(1, len(samples))
        self.assertEqual("mobile", samples[0].metadata.device_class)
        self.assertEqual("android", samples[0].metadata.os_family)
        self.assertEqual("tcp", samples[0].metadata.transport)
        self.assertEqual("browser_capture", samples[0].metadata.source_kind)
        self.assertEqual("android_okhttp_like", samples[0].metadata.fixture_family_id)
        self.assertEqual("tls13", samples[0].metadata.tls_gen)
        self.assertEqual("/captures/android_pixel9.pcapng", samples[0].metadata.source_path)
        self.assertEqual("artifact-sha256", samples[0].metadata.source_sha256)
        self.assertEqual([0x000A, 0xFE0D], samples[0].non_grease_extensions_without_padding)
        self.assertEqual(["h2", "http/1.1"], samples[0].alpn_protocols)
        self.assertEqual("android_pixel9_clienthello", samples[0].metadata.scenario_id)


class CheckSamplePoliciesTest(unittest.TestCase):
    def setUp(self) -> None:
        self.registry = {
            "profiles": {
                "Chrome133": {
                    "ech_type": "0xFE0D",
                    "pq_group": "0x11EC",
                    "alps_type": "0x44CD",
                    "extension_order_policy": "ChromeShuffleAnchored",
                    "fingerprint_policy": {
                        "require_anti_telegram_ja3": True,
                    },
                    "allowed_tags": {
                        "source_kind": ["browser_capture"],
                        "family": ["chrome133_like"],
                        "platform_class": ["desktop"],
                        "os_family": ["linux"],
                        "tls_gen": ["tls13"],
                        "transport": ["tcp"],
                    },
                },
                "IOS14": {
                    "ech_type": None,
                    "pq_group": None,
                    "alps_type": None,
                    "extension_order_policy": "FixedFromFixture",
                    "allowed_tags": {
                        "platform_class": ["mobile"],
                        "os_family": ["ios"],
                    },
                }
            }
        }

    def test_unknown_route_rejects_new_ech(self) -> None:
        sample = make_sample("unknown", extensions=[make_extension(0xFE0D)])

        failures = check_sample_policies(sample, self.registry)

        self.assertIn("ECH route policy", failures)

    def test_non_ru_route_rejects_legacy_ech(self) -> None:
        sample = make_sample("non_ru_egress", extensions=[make_extension(0xFE02), make_extension(0xFE0D)])

        failures = check_sample_policies(sample, self.registry)

        self.assertIn("No legacy ECH 0xFE02", failures)

    def test_non_ru_route_requires_ech_for_ech_profile(self) -> None:
        sample = make_sample("non_ru_egress")

        failures = check_sample_policies(sample, self.registry)

        self.assertIn("ECH route policy", failures)

    def test_pq_group_must_match_supported_groups_and_key_share(self) -> None:
        sample = make_sample(
            "non_ru_egress",
            extensions=[make_extension(0xFE0D), make_extension(0x44CD)],
            supported_groups=[0x11EC],
            key_share_groups=[0x001D],
        )

        failures = check_sample_policies(sample, self.registry)

        self.assertIn("PQ group policy", failures)

    def test_alps_type_must_match_registry(self) -> None:
        sample = make_sample(
            "non_ru_egress",
            extensions=[make_extension(0xFE0D), make_extension(0x4469)],
            supported_groups=[0x11EC],
            key_share_groups=[0x11EC],
        )

        failures = check_sample_policies(sample, self.registry)

        self.assertIn("ALPS policy", failures)

    def test_profile_must_match_platform_hints(self) -> None:
        sample = make_sample(
            "non_ru_egress",
            profile="IOS14",
            device_class="desktop",
            os_family="linux",
        )

        failures = check_sample_policies(sample, self.registry)

        self.assertIn("Profile matches platform hints", failures)

    def test_profile_must_match_fixture_family_allow_list(self) -> None:
        sample = make_sample(
            "non_ru_egress",
            extensions=[make_extension(0xFE0D), make_extension(0x44CD)],
            supported_groups=[0x11EC],
            key_share_groups=[0x11EC],
            fixture_family_id="android_okhttp_like",
        )

        failures = check_sample_policies(sample, self.registry)

        self.assertIn("Profile matches fixture provenance tags", failures)

    def test_profile_must_match_transport_allow_list(self) -> None:
        sample = make_sample(
            "non_ru_egress",
            extensions=[make_extension(0xFE0D), make_extension(0x44CD)],
            supported_groups=[0x11EC],
            key_share_groups=[0x11EC],
            transport="udp_quic_tls",
        )

        failures = check_sample_policies(sample, self.registry)

        self.assertIn("Profile matches fixture provenance tags", failures)

    def test_profile_must_match_source_kind_allow_list(self) -> None:
        sample = make_sample(
            "non_ru_egress",
            extensions=[make_extension(0xFE0D), make_extension(0x44CD)],
            supported_groups=[0x11EC],
            key_share_groups=[0x11EC],
            source_kind="advisory_code_sample",
        )

        failures = check_sample_policies(sample, self.registry)

        self.assertIn("Profile matches fixture provenance tags", failures)

    def test_registry_fixture_metadata_backfills_missing_provenance_tags(self) -> None:
        sample = make_sample(
            "non_ru_egress",
            extensions=[make_extension(0xFE0D), make_extension(0x44CD)],
            supported_groups=[0x11EC],
            key_share_groups=[0x11EC],
            source_kind="unknown",
            fixture_family_id="",
            tls_gen="unknown",
            source_path="/captures/chrome133.pcapng",
            source_sha256="sha-from-artifact",
        )
        self.registry["fixtures"] = {
            "fx_chrome133": {
                "source_path": "/captures/chrome133.pcapng",
                "source_sha256": "sha-from-artifact",
                "source_kind": "browser_capture",
                "family": "chrome133_like",
                "tls_gen": "tls13",
                "transport": "tcp",
            }
        }
        self.registry["profiles"]["Chrome133"]["include_fixture_ids"] = ["fx_chrome133"]

        failures = check_sample_policies(sample, self.registry)

        self.assertNotIn("Profile matches fixture provenance tags", failures)

    def test_registry_fixture_metadata_requires_source_sha256_for_backfill(self) -> None:
        sample = make_sample(
            "non_ru_egress",
            extensions=[make_extension(0xFE0D), make_extension(0x44CD)],
            supported_groups=[0x11EC],
            key_share_groups=[0x11EC],
            source_kind="unknown",
            fixture_family_id="",
            tls_gen="unknown",
            source_path="/captures/chrome133.pcapng",
            source_sha256="",
        )
        self.registry["fixtures"] = {
            "fx_chrome133": {
                "source_path": "/captures/chrome133.pcapng",
                "source_sha256": "sha-from-artifact",
                "source_kind": "browser_capture",
                "family": "chrome133_like",
                "tls_gen": "tls13",
                "transport": "tcp",
            }
        }
        self.registry["profiles"]["Chrome133"]["include_fixture_ids"] = ["fx_chrome133"]

        failures = check_sample_policies(sample, self.registry)

        self.assertIn("Profile matches fixture provenance tags", failures)

    def test_fixed_order_policy_rejects_reordered_extensions(self) -> None:
        sample = make_sample(
            "non_ru_egress",
            profile="IOS14",
            device_class="mobile",
            os_family="ios",
            non_grease_extensions_without_padding=[0x002B, 0x000D, 0x0010],
            fixture_family_id="ios14_like",
            source_path="/captures/ios14.pcapng",
            source_sha256="ios-sha",
        )
        self.registry["fixtures"] = {
            "fx_ios14": {
                "source_path": "/captures/ios14.pcapng",
                "source_sha256": "ios-sha",
                "source_kind": "browser_capture",
                "family": "ios14_like",
                "tls_gen": "tls13",
                "transport": "tcp",
                "non_grease_extensions_without_padding": ["0x000D", "0x002B", "0x0010"],
            }
        }
        self.registry["profiles"]["IOS14"]["include_fixture_ids"] = ["fx_ios14"]

        failures = check_sample_policies(sample, self.registry)

        self.assertIn("Extension order policy", failures)

    def test_chrome_shuffle_policy_accepts_non_anchor_permutation(self) -> None:
        sample = make_sample(
            "non_ru_egress",
            extensions=[make_extension(0xFE0D), make_extension(0x44CD)],
            supported_groups=[0x11EC],
            key_share_groups=[0x11EC],
            non_grease_extensions_without_padding=[0x002B, 0x0010, 0x000D, 0xFF01],
            source_path="/captures/chrome133.pcapng",
            source_sha256="chrome-sha",
        )
        self.registry["fixtures"] = {
            "fx_chrome133": {
                "source_path": "/captures/chrome133.pcapng",
                "source_sha256": "chrome-sha",
                "source_kind": "browser_capture",
                "family": "chrome133_like",
                "tls_gen": "tls13",
                "transport": "tcp",
                "non_grease_extensions_without_padding": ["0x000D", "0x002B", "0xFF01", "0x0010"],
            }
        }
        self.registry["profiles"]["Chrome133"]["include_fixture_ids"] = ["fx_chrome133"]

        failures = check_sample_policies(sample, self.registry)

        self.assertNotIn("Extension order policy", failures)

    def test_chrome_shuffle_policy_rejects_missing_extension_from_allowed_pool(self) -> None:
        sample = make_sample(
            "non_ru_egress",
            extensions=[make_extension(0xFE0D), make_extension(0x44CD)],
            supported_groups=[0x11EC],
            key_share_groups=[0x11EC],
            non_grease_extensions_without_padding=[0x002B, 0x0010, 0x000D],
            source_path="/captures/chrome133.pcapng",
            source_sha256="chrome-sha",
        )
        self.registry["fixtures"] = {
            "fx_chrome133": {
                "source_path": "/captures/chrome133.pcapng",
                "source_sha256": "chrome-sha",
                "source_kind": "browser_capture",
                "family": "chrome133_like",
                "tls_gen": "tls13",
                "transport": "tcp",
                "non_grease_extensions_without_padding": ["0x000D", "0x002B", "0xFF01", "0x0010"],
            }
        }
        self.registry["profiles"]["Chrome133"]["include_fixture_ids"] = ["fx_chrome133"]

        failures = check_sample_policies(sample, self.registry)

        self.assertIn("Extension order policy", failures)

    def test_extension_order_policy_accepts_any_matching_fixture_candidate(self) -> None:
        sample = make_sample(
            "non_ru_egress",
            profile="IOS14",
            device_class="mobile",
            os_family="ios",
            non_grease_extensions_without_padding=[0x002B, 0x0010, 0x000D],
            fixture_family_id="ios14_like",
            source_path="/captures/ios14.pcapng",
            source_sha256="ios-sha",
        )
        self.registry["fixtures"] = {
            "fx_ios14_a": {
                "source_path": "/captures/ios14.pcapng",
                "source_sha256": "ios-sha",
                "source_kind": "browser_capture",
                "family": "ios14_like",
                "tls_gen": "tls13",
                "transport": "tcp",
                "non_grease_extensions_without_padding": ["0x000D", "0x002B", "0x0010"],
            },
            "fx_ios14_b": {
                "source_path": "/captures/ios14.pcapng",
                "source_sha256": "ios-sha",
                "source_kind": "browser_capture",
                "family": "ios14_like",
                "tls_gen": "tls13",
                "transport": "tcp",
                "non_grease_extensions_without_padding": ["0x002B", "0x0010", "0x000D"],
            },
        }
        self.registry["profiles"]["IOS14"]["include_fixture_ids"] = ["fx_ios14_a", "fx_ios14_b"]

        failures = check_sample_policies(sample, self.registry)

        self.assertNotIn("Extension order policy", failures)

    def test_optional_ech_policy_allows_absent_new_ech(self) -> None:
        self.registry["profiles"]["Chrome133"]["ech_type"] = {"allow_present": True, "allow_absent": True}
        sample = make_sample(
            "non_ru_egress",
            extensions=[make_extension(0x44CD)],
            supported_groups=[0x11EC],
            key_share_groups=[0x11EC],
        )

        failures = check_sample_policies(sample, self.registry)

        self.assertNotIn("ECH route policy", failures)

    def test_optional_pq_group_policy_allows_absent_group(self) -> None:
        self.registry["profiles"]["Chrome133"]["pq_group"] = {
            "allowed_groups": ["0x11EC"],
            "allow_absent": True,
        }
        sample = make_sample(
            "non_ru_egress",
            extensions=[make_extension(0xFE0D), make_extension(0x44CD)],
            supported_groups=[0x001D],
            key_share_groups=[0x001D],
        )

        failures = check_sample_policies(sample, self.registry)

        self.assertNotIn("PQ group policy", failures)

    def test_optional_alps_policy_allows_absent_extension(self) -> None:
        self.registry["profiles"]["Chrome133"]["alps_type"] = {
            "allowed_types": ["0x44CD"],
            "allow_absent": True,
        }
        sample = make_sample(
            "non_ru_egress",
            extensions=[make_extension(0xFE0D)],
            supported_groups=[0x11EC],
            key_share_groups=[0x11EC],
        )

        failures = check_sample_policies(sample, self.registry)

        self.assertNotIn("ALPS policy", failures)

    def test_rejects_known_telegram_ja3_fingerprint(self) -> None:
        sample = make_sample(
            "non_ru_egress",
            extensions=[
                make_extension(0xFE0D),
                make_extension(0x44CD),
                make_extension_with_body(0x000B, "0100"),
                make_extension_with_body(0x000D, "000400010201"),
            ],
            supported_groups=[0x11EC],
            key_share_groups=[0x11EC],
            non_grease_extensions_without_padding=[0xFE0D, 0x44CD, 0x000B, 0x000D],
            source_path="/captures/chrome133.pcapng",
            source_sha256="telegram-like-sha",
        )
        sample = ClientHello(
            raw=b"",
            profile=sample.profile,
            extensions=sample.extensions,
            cipher_suites=[0x1301, 0x1302],
            supported_groups=sample.supported_groups,
            key_share_groups=sample.key_share_groups,
            non_grease_extensions_without_padding=sample.non_grease_extensions_without_padding,
            alpn_protocols=sample.alpn_protocols,
            metadata=sample.metadata,
            ech_payload_length=sample.ech_payload_length,
        )
        self.registry["fixtures"] = {
            "fx_chrome133": {
                "source_path": "/captures/chrome133.pcapng",
                "source_sha256": "telegram-like-sha",
                "source_kind": "browser_capture",
                "family": "chrome133_like",
                "tls_gen": "tls13",
                "transport": "tcp",
                "non_grease_extensions_without_padding": ["0xFE0D", "0x44CD", "0x000B", "0x000D"],
            }
        }
        self.registry["profiles"]["Chrome133"]["include_fixture_ids"] = ["fx_chrome133"]
        self.registry["profiles"]["Chrome133"]["fingerprint_policy"] = {
            "require_anti_telegram_ja3": True,
            "telegram_ja3_hashes": [compute_ja3_hash(sample)],
        }

        failures = check_sample_policies(sample, self.registry)

        self.assertIn("Anti-Telegram JA3", failures)


class EchPayloadVarianceTest(unittest.TestCase):
    def setUp(self) -> None:
        self.registry = {
            "profiles": {
                "Chrome133": {
                    "ech_type": "0xFE0D",
                    "fingerprint_policy": {
                        "require_noncollapsed_randomized_hashes": True,
                        "min_unique_ja3_per_64": 2,
                        "min_unique_ja4_per_64": 2,
                    },
                },
                "Firefox148": {
                    "ech_type": "0xFE0D",
                    "fingerprint_policy": {
                        "require_noncollapsed_randomized_hashes": False,
                    },
                }
            }
        }

    def test_rejects_singleton_lengths(self) -> None:
        samples = [
            make_sample("non_ru_egress", payload_length=208, extensions=[make_extension(0xFE0D)]) for _ in range(64)
        ]

        self.assertFalse(check_ech_payload_variance(samples, self.registry))

    def test_accepts_multiple_allowed_lengths(self) -> None:
        samples = []
        for _ in range(32):
            samples.append(
                make_sample(
                    "non_ru_egress",
                    payload_length=144,
                    extensions=[
                        make_extension(0xFE0D),
                        make_extension_with_body(0x000B, "0100"),
                        make_extension_with_body(0x000D, "000400010201"),
                    ],
                    supported_groups=[0x001D],
                    key_share_groups=[0x001D],
                )
            )
        for _ in range(32):
            samples.append(
                make_sample(
                    "non_ru_egress",
                    payload_length=208,
                    extensions=[
                        make_extension(0xFE0D),
                        make_extension_with_body(0x000B, "0100"),
                        make_extension_with_body(0x000D, "000400030201"),
                    ],
                    supported_groups=[0x001D, 0x0017],
                    key_share_groups=[0x001D],
                )
            )

        self.assertTrue(check_ech_payload_variance(samples, self.registry))

    def test_rejects_collapsed_ja3_hashes_when_uniqueness_floor_required(self) -> None:
        samples = [
            make_sample(
                "non_ru_egress",
                payload_length=144 if index % 2 == 0 else 208,
                extensions=[
                    make_extension(0xFE0D),
                    make_extension_with_body(0x000B, "0100"),
                    make_extension_with_body(0x000D, "000400010201"),
                ],
                supported_groups=[0x001D],
                key_share_groups=[0x001D],
            )
            for index in range(64)
        ]

        self.assertFalse(check_ech_payload_variance(samples, self.registry))

    def test_rejects_collapsed_ja4_signatures_when_uniqueness_floor_required(self) -> None:
        samples = [
            make_sample(
                "non_ru_egress",
                payload_length=144 if index % 2 == 0 else 208,
                extensions=[
                    make_extension(0xFE0D),
                    make_extension_with_body(0x000B, "0100"),
                    make_extension_with_body(0x000D, "000400010201"),
                ],
                supported_groups=[0x001D] if index % 2 == 0 else [0x0017],
                key_share_groups=[0x001D],
                cipher_suites=[0x1301, 0x1302],
                alpn_protocols=["h2", "http/1.1"],
            )
            for index in range(64)
        ]

        self.assertFalse(check_ech_payload_variance(samples, self.registry))

    def test_skips_variance_gate_for_profiles_without_dispersion_requirement(self) -> None:
        samples = [
            make_sample(
                "non_ru_egress",
                profile="Firefox148",
                payload_length=239,
                extensions=[make_extension(0xFE0D)],
            )
        ]

        self.assertTrue(check_ech_payload_variance(samples, self.registry))


class FingerprintPolicyConfigValidationTest(unittest.TestCase):
    def test_rejects_exact_ja3_pin_when_disabled(self) -> None:
        registry = {
            "profiles": {
                "Chrome133": {
                    "fingerprint_policy": {
                        "allow_exact_ja3_pin": False,
                        "exact_ja3_hashes": ["0123456789abcdef0123456789abcdef"],
                    }
                }
            }
        }

        failures = validate_fingerprint_policy_config(registry)

        self.assertEqual(["profile[Chrome133]: Exact JA3 pin disabled"], failures)

    def test_rejects_exact_ja4_pin_when_disabled(self) -> None:
        registry = {
            "profiles": {
                "Chrome133": {
                    "fingerprint_policy": {
                        "allow_exact_ja4_pin": False,
                        "exact_ja4_signatures": ["t13d1516h2_deadbeefdead_beadfeedcafe"],
                    }
                }
            }
        }

        failures = validate_fingerprint_policy_config(registry)

        self.assertEqual(["profile[Chrome133]: Exact JA4 pin disabled"], failures)

    def test_allows_exact_pins_when_explicitly_enabled(self) -> None:
        registry = {
            "profiles": {
                "Chrome133": {
                    "fingerprint_policy": {
                        "allow_exact_ja3_pin": True,
                        "allow_exact_ja4_pin": True,
                        "exact_ja3_hashes": ["0123456789abcdef0123456789abcdef"],
                        "exact_ja4_signatures": ["t13d1516h2_deadbeefdead_beadfeedcafe"],
                    }
                }
            }
        }

        failures = validate_fingerprint_policy_config(registry)

        self.assertEqual([], failures)


class Ja4TelemetryTest(unittest.TestCase):
    def test_computes_sorted_ja4_signature_for_reordered_clienthello(self) -> None:
        sample_a = make_sample(
            "non_ru_egress",
            cipher_suites=[0x1302, 0x1301],
            extensions=[
                make_extension(0x002B),
                make_extension(0x0000),
                make_extension(0x0010),
                make_extension_with_body(0x000D, "000400030201"),
                make_extension(0xFE0D),
            ],
            alpn_protocols=["h2", "http/1.1"],
        )
        sample_b = make_sample(
            "non_ru_egress",
            cipher_suites=[0x1301, 0x1302],
            extensions=[
                make_extension(0xFE0D),
                make_extension_with_body(0x000D, "000400030201"),
                make_extension(0x0010),
                make_extension(0x0000),
                make_extension(0x002B),
            ],
            alpn_protocols=["h2", "http/1.1"],
        )

        signature_a = compute_ja4_signature(sample_a)
        signature_b = compute_ja4_signature(sample_b)

        self.assertEqual(signature_a, signature_b)
        self.assertTrue(signature_a.startswith("t13d0205h2_"))

    def test_collects_ja3_and_ja4_telemetry_per_profile(self) -> None:
        sample_a = make_sample(
            "non_ru_egress",
            cipher_suites=[0x1301, 0x1302],
            extensions=[
                make_extension(0xFE0D),
                make_extension(0x0010),
                make_extension_with_body(0x000D, "000400030201"),
            ],
            alpn_protocols=["h2", "http/1.1"],
        )
        sample_b = make_sample(
            "non_ru_egress",
            cipher_suites=[0x1301, 0x1303],
            extensions=[
                make_extension(0xFE0D),
                make_extension(0x0010),
                make_extension_with_body(0x000D, "000400050403"),
            ],
            alpn_protocols=["h2", "http/1.1"],
        )
        sample_c = make_sample(
            "non_ru_egress",
            profile="Firefox148",
            cipher_suites=[0x1301],
            extensions=[make_extension(0x0010)],
            alpn_protocols=["http/1.1"],
        )

        telemetry = collect_fingerprint_telemetry([sample_a, sample_b, sample_c])

        self.assertEqual(2, telemetry["profiles"]["Chrome133"]["sample_count"])
        self.assertEqual(1, telemetry["profiles"]["Firefox148"]["sample_count"])
        self.assertEqual(3, telemetry["unique_ja3_hash_count"])
        self.assertEqual(3, telemetry["unique_ja4_signature_count"])


if __name__ == "__main__":
    unittest.main()