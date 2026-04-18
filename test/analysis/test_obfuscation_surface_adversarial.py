# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT

import unittest

import test_obfuscation_surface_contract as contract


class ObfuscationSurfaceAdversarialTest(unittest.TestCase):
    def test_scanner_flags_old_trust_registry_name(self) -> None:
        probe = """
        #include \"td/telegram/TrustRegistry.h\"
        auto x = TrustRegistry::slot_value(0);
        """
        findings = contract.scan_forbidden_tokens(probe)
        self.assertTrue(any("TrustRegistry" in item[0] for item in findings))

    def test_scanner_flags_old_transport_health_namespace_and_api(self) -> None:
        probe = """
        namespace transport_trust {
        void reset_transport_trust_health_for_tests();
        }
        """
        findings = contract.scan_forbidden_tokens(probe)
        matched_patterns = {pattern for pattern, _, _ in findings}
        self.assertIn(r"\btransport_trust\b", matched_patterns)
        self.assertIn(r"\breset_transport_trust_health_for_tests\b", matched_patterns)

    def test_scanner_ignores_cover_identifiers(self) -> None:
        probe = """
        namespace net_health {
        auto state = get_net_monitor_snapshot();
        auto ok = ReferenceTable::contains_host("example.org");
        }
        """
        findings = contract.scan_forbidden_tokens(probe)
        self.assertEqual([], findings)


if __name__ == "__main__":
    unittest.main()
