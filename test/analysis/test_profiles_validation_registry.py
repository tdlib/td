# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

import pathlib
import sys
import unittest
import json


THIS_DIR = pathlib.Path(__file__).resolve().parent
if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))

from check_fixture_registry_complete import validate_registry_completeness
from common_tls import load_profile_registry


REGISTRY_PATH = THIS_DIR / "profiles_validation.json"


class ProfilesValidationRegistryTest(unittest.TestCase):
    def test_checked_in_registry_exists(self) -> None:
        self.assertTrue(REGISTRY_PATH.exists())

    def test_checked_in_registry_has_no_placeholder_or_dangling_fixture_ids(self) -> None:
        registry = load_profile_registry(REGISTRY_PATH)

        failures = validate_registry_completeness(registry)

        self.assertEqual([], failures)

    def test_checked_in_registry_covers_non_linux_reviewed_corpus(self) -> None:
        registry = load_profile_registry(REGISTRY_PATH)
        profiles = registry.get("profiles", {})
        fixture_roots = [
            THIS_DIR / "fixtures" / "clienthello" / "android",
            THIS_DIR / "fixtures" / "clienthello" / "ios",
            THIS_DIR / "fixtures" / "clienthello" / "macos",
        ]

        missing_profiles: list[str] = []
        for fixture_root in fixture_roots:
            for artifact_path in sorted(fixture_root.glob("*.json")):
                artifact = json.loads(artifact_path.read_text(encoding="utf-8"))
                profile_id = artifact.get("profile_id")
                if not isinstance(profile_id, str) or not profile_id:
                    missing_profiles.append(f"{artifact_path.name}:missing-profile-id")
                    continue
                if profile_id not in profiles:
                    missing_profiles.append(profile_id)

        self.assertEqual([], missing_profiles)


if __name__ == "__main__":
    unittest.main()