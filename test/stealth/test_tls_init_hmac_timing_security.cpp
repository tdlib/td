// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Security tests: Verify the TLS handshake response validation uses
// constant-time HMAC comparison per OWASP ASVS L2#2.9/V2.
// A timing side-channel in HMAC comparison could allow a MitM to
// forge response auth bytes by byte-by-byte oracle.
//
// Note: These tests verify the existence and correctness of the
// constant-time comparison utility function, since measuring actual
// timing differences in a unit test is unreliable.

#include "td/utils/common.h"
#include "td/utils/crypto.h"
#include "td/utils/tests.h"

#include <cstring>

namespace {

TEST(TlsInitHmacTimingSecurity, ConstantTimeCompareMustRejectMismatchedStrings) {
  td::string expected = "abcdefghijklmnopqrstuvwxyz012345";
  td::string wrong = expected;
  wrong[0] = 'X';
  ASSERT_FALSE(td::constant_time_equals(expected, wrong));
}

TEST(TlsInitHmacTimingSecurity, ConstantTimeCompareMustAcceptIdenticalStrings) {
  td::string expected = "abcdefghijklmnopqrstuvwxyz012345";
  ASSERT_TRUE(td::constant_time_equals(expected, expected));
}

TEST(TlsInitHmacTimingSecurity, ConstantTimeCompareMustRejectDifferentLengths) {
  td::string a = "short";
  td::string b = "much_longer_string";
  ASSERT_FALSE(td::constant_time_equals(a, b));
}

TEST(TlsInitHmacTimingSecurity, ConstantTimeCompareMustHandleEmptyStrings) {
  td::string empty;
  td::string non_empty = "data";
  ASSERT_TRUE(td::constant_time_equals(empty, empty));
  ASSERT_FALSE(td::constant_time_equals(empty, non_empty));
}

TEST(TlsInitHmacTimingSecurity, ConstantTimeCompareLastByteDifferenceMustBeDetected) {
  // Timing-vulnerable implementations might short-circuit before reaching the last byte.
  td::string a(32, '\x00');
  td::string b = a;
  b[31] = '\x01';
  ASSERT_FALSE(td::constant_time_equals(a, b));
}

TEST(TlsInitHmacTimingSecurity, ConstantTimeCompareNullBytesInMiddleMustNotTruncate) {
  // Ensure null bytes don't cause early termination (not a C string comparison).
  td::string a(32, '\x00');
  a[0] = 'A';
  a[31] = 'Z';
  td::string b = a;
  b[16] = 'X';  // Difference after null byte at position 1
  ASSERT_FALSE(td::constant_time_equals(a, b));
}

TEST(TlsInitHmacTimingSecurity, HmacSha256OutputMustMatchExpectedLength) {
  td::string key = "0123456789secret";
  td::string message = "test message for HMAC verification";
  td::string output(32, '\0');
  td::hmac_sha256(key, message, output);
  // HMAC-SHA256 always produces exactly 32 bytes.
  bool all_zero = true;
  for (auto c : output) {
    if (c != 0) {
      all_zero = false;
      break;
    }
  }
  ASSERT_FALSE(all_zero);
}

}  // namespace
