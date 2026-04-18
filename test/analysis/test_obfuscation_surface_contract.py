# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT

import pathlib
import re
import unittest


THIS_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = THIS_DIR.parent.parent

SCAN_DIRS = (
    REPO_ROOT / "td",
    REPO_ROOT / "tdnet",
    REPO_ROOT / "tdutils",
    REPO_ROOT / "test" / "stealth",
)

SCAN_SUFFIXES = (".h", ".hpp", ".cpp", ".cc", ".cxx")

FORBIDDEN_PATTERNS = (
    r"\bTransportTrustHealth\b",
    r"\btransport_trust\b",
    r"\bset_allow_insecure_pfs_for_tests\b",
    r"\bnote_use_pfs_disable_attempt\b",
    r"\buse_pfs_disable_attempt_total\b",
    r"\bget_transport_trust_health_snapshot\b",
    r"\breset_transport_trust_health_for_tests\b",
    r"\bTrustRegistry\b",
    r"\bRsaKeyVault\b",
    r"\bvalidate_loaded_fingerprint\b",
    r"\bvalidate_selected_rsa_fingerprint\b",
    r"\bvalidate_injected_public_key_fingerprint\b",
    r"\bvalidate_recovery_key_fingerprint\b",
    r"\bVerifiedKeySource\b",
    r"\bKeyRegistryCrosscheck\b",
)


def iter_source_files() -> list[pathlib.Path]:
    files: list[pathlib.Path] = []
    for scan_dir in SCAN_DIRS:
        for path in scan_dir.rglob("*"):
            if path.is_file() and path.suffix in SCAN_SUFFIXES:
                files.append(path)
    return sorted(files)


def scan_forbidden_tokens(text: str) -> list[tuple[str, int, str]]:
    findings: list[tuple[str, int, str]] = []
    lines = text.splitlines()
    for pattern in FORBIDDEN_PATTERNS:
        compiled = re.compile(pattern)
        for line_no, line in enumerate(lines, start=1):
            if compiled.search(line):
                findings.append((pattern, line_no, line.strip()))
    return findings


class ObfuscationSurfaceContractTest(unittest.TestCase):
    def test_runtime_and_stealth_surfaces_do_not_reintroduce_old_identifiers(self) -> None:
        violations: list[str] = []
        for path in iter_source_files():
            findings = scan_forbidden_tokens(path.read_text(encoding="utf-8"))
            for pattern, line_no, line in findings:
                rel = path.relative_to(REPO_ROOT).as_posix()
                violations.append(f"{rel}:{line_no}: {pattern}: {line}")

        self.assertEqual(
            [],
            violations,
            msg=(
                "old high-signal identifier leaked back into runtime/test surfaces:\n"
                + "\n".join(violations[:80])
            ),
        )

    def test_cover_surface_files_and_symbols_exist(self) -> None:
        expected_files = (
            REPO_ROOT / "td" / "telegram" / "net" / "NetReliabilityMonitor.h",
            REPO_ROOT / "td" / "telegram" / "ReferenceTable.h",
            REPO_ROOT / "td" / "mtproto" / "BlobStore.h",
        )
        for path in expected_files:
            self.assertTrue(path.exists(), msg=f"expected obfuscation cover file missing: {path}")

        expected_symbol_seams = {
            REPO_ROOT / "td" / "telegram" / "net" / "NetReliabilityMonitor.h": (
                "namespace net_health",
                "NetMonitorState",
                "get_net_monitor_snapshot",
            ),
            REPO_ROOT / "td" / "telegram" / "ReferenceTable.h": (
                "ReferenceTable",
                "slot_value",
                "contains_host",
            ),
            REPO_ROOT / "td" / "mtproto" / "BlobStore.h": (
                "class BlobStore",
                "BlobRole",
                "verify_bundle",
            ),
        }

        for path, symbols in expected_symbol_seams.items():
            text = path.read_text(encoding="utf-8")
            for symbol in symbols:
                self.assertIn(
                    symbol,
                    text,
                    msg=f"expected cover symbol {symbol} missing from {path.relative_to(REPO_ROOT).as_posix()}",
                )


if __name__ == "__main__":
    unittest.main()
