# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import pathlib
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
MEMORY_LOG = REPO_ROOT / "tdutils" / "td" / "utils" / "MemoryLog.h"
CRYPTO_CPP = REPO_ROOT / "tdutils" / "td" / "utils" / "crypto.cpp"
MSAN_IGNORELIST = REPO_ROOT / "tools" / "ci" / "msan.ignorelist"
MULTITIMEOUT_CPP = REPO_ROOT / "tdactor" / "td" / "actor" / "MultiTimeout.cpp"
TESTS_CPP = REPO_ROOT / "tdutils" / "td" / "utils" / "tests.cpp"
SOURCE_LAYOUT_IMAGE_CONTRACT = REPO_ROOT / "test" / "stealth" / "test_source_layout_image_contract.cpp"
LOGGING_FIXTURE_ROUTE_ECH = REPO_ROOT / "test" / "stealth" / "test_logging_fixture_route_ech_integration.cpp"
TDUTILS_MISC_TEST = REPO_ROOT / "tdutils" / "test" / "misc.cpp"


class SanitizerRuntimeContractsTest(unittest.TestCase):
    def test_memory_log_serializes_ring_buffer_writes(self) -> None:
        source = MEMORY_LOG.read_text(encoding="utf-8")

        self.assertIn('#include "td/utils/SpinLock.h"', source)
        self.assertIn("SpinLock lock_", source)

        do_append = source[source.index("void do_append") : source.index("  char buffer_")]
        lock_pos = do_append.index("auto lock = lock_.lock();")
        first_write_pos = min(
            pos
            for pos in [
                do_append.index("std::memcpy"),
                do_append.index("buffer_[start_pos]"),
                do_append.index("std::snprintf"),
            ]
        )
        self.assertLess(lock_pos, first_write_pos)

    def test_hmac_outputs_are_unpoisoned_for_msan_after_openssl_writes(self) -> None:
        source = CRYPTO_CPP.read_text(encoding="utf-8")

        self.assertIn("TD_CRYPTO_MSAN_ACTIVE", source)
        for function_name in ("hmac_sha256", "hmac_sha512"):
            start = source.index(f"void {function_name}(Slice key, Slice message, MutableSlice dest)")
            body = source[start : source.index("\n}", start)]
            self.assertIn("__msan_unpoison(dest.ubegin(), dest.size());", body)

    def test_rsa_pem_paths_scope_msan_interceptor_checks(self) -> None:
        source = CRYPTO_CPP.read_text(encoding="utf-8")

        encrypt_start = source.index("Result<BufferSlice> rsa_encrypt_pkcs1_oaep")
        decrypt_start = source.index("Result<BufferSlice> rsa_decrypt_pkcs1_oaep")
        encrypt_body = source[encrypt_start:decrypt_start]
        decrypt_body = source[decrypt_start : source.index("\nStatus create_openssl_error", decrypt_start)]

        for body in (encrypt_body, decrypt_body):
            self.assertIn("ScopedMsanInterceptorChecks scoped_msan_interceptor_checks;", body)
            self.assertLess(body.index("ScopedMsanInterceptorChecks"), body.index("PEM_read_bio_"))
            self.assertIn("__msan_unpoison(&outlen, sizeof(outlen));", body)
            self.assertIn("__msan_unpoison(res.as_mutable_slice().ubegin(), outlen);", body)

    def test_msan_ignorelist_suppresses_only_libstdcxx_rb_tree_node_noise(self) -> None:
        source = MSAN_IGNORELIST.read_text(encoding="utf-8")

        self.assertIn("src:*/bits/stl_tree.h", source)
        self.assertIn("fun:*_Rb_tree*", source)
        self.assertNotIn("src:*/tddb/td/db/TQueue.cpp", source)
        self.assertNotIn("poison_in_malloc=0", source)

    def test_msan_annotations_cover_inlined_libstdcxx_rb_tree_call_sites(self) -> None:
        tests_source = TESTS_CPP.read_text(encoding="utf-8")
        multitimeout_source = MULTITIMEOUT_CPP.read_text(encoding="utf-8")

        self.assertIn("TD_TESTS_MSAN_NO_SANITIZE", tests_source)
        self.assertLess(tests_source.index("TD_TESTS_MSAN_NO_SANITIZE"), tests_source.index("Status verify_test"))
        self.assertIn('no_sanitize("memory")', tests_source)

        self.assertIn("TD_MULTITIMEOUT_MSAN_NO_SANITIZE", multitimeout_source)
        self.assertLess(
            multitimeout_source.index("TD_MULTITIMEOUT_MSAN_NO_SANITIZE"),
            multitimeout_source.index("void MultiTimeout::set_timeout_at"),
        )
        self.assertIn('no_sanitize("memory")', multitimeout_source)

    def test_source_layout_image_counts_retained_material_not_generic_pem_headers(self) -> None:
        source = SOURCE_LAYOUT_IMAGE_CONTRACT.read_text(encoding="utf-8")
        test_body = source[source.index("ProcessImageContainsAtLeastThreeRetainedBlockMarkers") :]

        self.assertIn("kRetainedMaterialMarkers", source)
        self.assertNotIn('count_token(image, "BEGIN RSA PUBLIC KEY")', test_body)

    def test_logging_fixture_file_exists_avoids_iostream_state_under_msan(self) -> None:
        source = LOGGING_FIXTURE_ROUTE_ECH.read_text(encoding="utf-8")
        helper_body = source[source.index("bool repo_file_exists") : source.index("TEST(", source.index("bool repo_file_exists"))]

        self.assertIn("td::read_file_str", helper_body)
        self.assertNotIn("std::ifstream", helper_body)

    def test_to_double_skips_libstdcxx_locale_switching_under_msan(self) -> None:
        source = TDUTILS_MISC_TEST.read_text(encoding="utf-8")
        test_body = source[source.index("TEST(Misc, to_double)") : source.index("TEST(Misc, fixed_double")]

        self.assertIn("TD_HAS_FEATURE_MEMORY_SANITIZER", test_body)
        self.assertIn("__SANITIZE_MEMORY__", test_body)
        self.assertIn("return;", test_body)


if __name__ == "__main__":
    unittest.main()
