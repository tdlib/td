# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import pathlib
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
CMAKE_TESTS = REPO_ROOT / "test" / "CMakeLists.txt"
SQLCIPHER_ADVERSARIAL = REPO_ROOT / "test" / "sqlite_sqlcipher_key_init_lock_adversarial.cpp"


class CTestExactParallelismContractTest(unittest.TestCase):
    def test_db_and_timing_sensitive_exact_tests_run_serially(self) -> None:
        source = CMAKE_TESTS.read_text(encoding="utf-8")

        self.assertIn("Test_DB_sqlite_phase3_.*", source)
        self.assertIn("Test_DBSqlcipherKeyInitLockAdversarial_adversarial_sqlcipher_.*", source)
        self.assertIn("Test_AuthenticationTimingConsistency_HmacVerificationConstantTime", source)
        serial_block = source[
            source.index("Test_DB_key_value") : source.index("# Tests that create database/filesystem artefacts")
        ]
        self.assertIn("Test_DB_sqlite_phase3_.*", serial_block)
        self.assertIn("Test_DBSqlcipherKeyInitLockAdversarial_adversarial_sqlcipher_.*", serial_block)
        self.assertIn("Test_AuthenticationTimingConsistency_HmacVerificationConstantTime", serial_block)
        self.assertIn("RUN_SERIAL", serial_block)
        self.assertIn("TRUE", serial_block)

    def test_sqlcipher_adversarial_error_path_avoids_vector_bool_race(self) -> None:
        source = SQLCIPHER_ADVERSARIAL.read_text(encoding="utf-8")

        self.assertNotIn("std::vector<bool> got_error", source)
        self.assertIn("std::vector<char> got_error", source)


if __name__ == "__main__":
    unittest.main()
