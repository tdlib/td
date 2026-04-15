# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT

import pathlib
import sys
import unittest


THIS_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = THIS_DIR.parent.parent
TOOLS_CATALOG_SYNC_DIR = REPO_ROOT / "tools" / "catalog_sync"
if str(TOOLS_CATALOG_SYNC_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_CATALOG_SYNC_DIR))

import refresh_static_tables


class StaticTableCodegenAdversarialTest(unittest.TestCase):
    def setUp(self) -> None:
        self.inputs = refresh_static_tables.load_repo_material(REPO_ROOT)

    def test_rejects_missing_required_role(self) -> None:
        public_keys = dict(self.inputs.public_keys)
        public_keys.pop("auxiliary")
        with self.assertRaisesRegex(ValueError, "missing required roles"):
            refresh_static_tables.build_static_table_artifacts(public_keys, self.inputs.sentinels)

    def test_rejects_wrong_sentinel_count(self) -> None:
        with self.assertRaisesRegex(ValueError, "exactly 4 sentinels"):
            refresh_static_tables.build_static_table_artifacts(self.inputs.public_keys, self.inputs.sentinels[:3])

    def test_rejects_wrong_sentinel_size(self) -> None:
        invalid_sentinels = list(self.inputs.sentinels)
        invalid_sentinels[0] = invalid_sentinels[0][:-1]
        with self.assertRaisesRegex(ValueError, "32-byte"):
            refresh_static_tables.build_static_table_artifacts(self.inputs.public_keys, invalid_sentinels)

    def test_rejects_malformed_public_key_pem(self) -> None:
        public_keys = dict(self.inputs.public_keys)
        public_keys["primary"] = "not a PEM"
        with self.assertRaisesRegex(ValueError, "public key PEM"):
            refresh_static_tables.build_static_table_artifacts(public_keys, self.inputs.sentinels)

    def test_distinct_check_labels_stay_domain_separated(self) -> None:
        generated = refresh_static_tables.build_static_table_artifacts(self.inputs.public_keys, self.inputs.sentinels)
        primary_values = [
            generated.cross_checks["catalog_weight_table"]["primary"],
            generated.cross_checks["route_window_table"]["primary"],
            generated.cross_checks["session_blend_table"]["primary"],
        ]
        self.assertEqual(len(primary_values), len(set(primary_values)))

    def test_tampered_shard_fails_closed_during_recovery(self) -> None:
        generated = refresh_static_tables.build_static_table_artifacts(self.inputs.public_keys, self.inputs.sentinels)
        tampered_shards = {
            group_name: {role: bytes(value) for role, value in values.items()}
            for group_name, values in generated.shards.items()
        }
        shard = bytearray(tampered_shards["protocol_fingerprint_table"]["primary"])
        shard[17] ^= 0x01
        tampered_shards["protocol_fingerprint_table"]["primary"] = bytes(shard)
        with self.assertRaisesRegex(ValueError, "MAC mismatch"):
            refresh_static_tables.recover_slot_fingerprints(tampered_shards, self.inputs.sentinels)


if __name__ == "__main__":
    unittest.main()