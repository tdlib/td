// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/ProxySecret.h"

#include "td/utils/tests.h"

namespace {

using td::mtproto::ProxySecret;

constexpr td::uint64 kLcgMul = 6364136223846793005ULL;
constexpr td::uint64 kLcgInc = 1442695040888963407ULL;

bool is_ascii_alnum(unsigned char c) {
  return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

bool is_valid_label_char(unsigned char c) {
  return is_ascii_alnum(c) || c == '-';
}

td::uint64 next_lcg(td::uint64 &state) {
  state = state * kLcgMul + kLcgInc;
  return state;
}

td::string make_tls_emulation_secret(td::Slice domain) {
  td::string secret;
  secret.reserve(17 + domain.size());
  secret.push_back(static_cast<char>(0xee));
  secret += "0123456789abcdef";
  secret += domain.str();
  return secret;
}

bool must_reject_as_hostname(td::Slice domain) {
  if (domain.empty() || domain.size() > ProxySecret::MAX_DOMAIN_LENGTH) {
    return true;
  }

  size_t label_size = 0;
  bool label_starts = true;
  bool label_ends_with_hyphen = false;
  for (auto c : domain) {
    auto byte = static_cast<unsigned char>(c);
    if (byte == '.') {
      if (label_size == 0 || label_ends_with_hyphen) {
        return true;
      }
      label_size = 0;
      label_starts = true;
      label_ends_with_hyphen = false;
      continue;
    }

    if (!is_valid_label_char(byte)) {
      return true;
    }
    if (label_starts && byte == '-') {
      return true;
    }

    label_starts = false;
    label_ends_with_hyphen = (byte == '-');
    label_size++;
    if (label_size > 63) {
      return true;
    }
  }

  return label_size == 0 || label_ends_with_hyphen;
}

td::string make_hostile_domain_candidate(td::uint64 &state) {
  static constexpr char kHostileAlphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._";

  auto length = 1 + static_cast<size_t>(next_lcg(state) % (ProxySecret::MAX_DOMAIN_LENGTH + 24));
  td::string domain;
  domain.reserve(length);
  for (size_t i = 0; i < length; i++) {
    auto lane = next_lcg(state) % 16;
    if (lane == 0) {
      domain.push_back('\0');
    } else if (lane == 1) {
      domain.push_back('\n');
    } else if (lane == 2) {
      domain.push_back(static_cast<char>(0xff));
    } else {
      auto idx = static_cast<size_t>(next_lcg(state) % (sizeof(kHostileAlphabet) - 1));
      domain.push_back(kHostileAlphabet[idx]);
    }
  }

  if (!domain.empty() && (next_lcg(state) & 1) == 0) {
    domain[0] = '-';
  }
  if (!domain.empty() && (next_lcg(state) & 3) == 0) {
    domain[domain.size() - 1] = '-';
  }
  if (domain.size() > 3 && (next_lcg(state) & 3) == 1) {
    domain[1] = '.';
    domain[2] = '.';
  }
  return domain;
}

char sample_alnum(td::uint64 &state) {
  static constexpr char kAlphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
  auto idx = static_cast<size_t>(next_lcg(state) % (sizeof(kAlphabet) - 1));
  return kAlphabet[idx];
}

char sample_middle(td::uint64 &state) {
  static constexpr char kAlphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789-";
  auto idx = static_cast<size_t>(next_lcg(state) % (sizeof(kAlphabet) - 1));
  return kAlphabet[idx];
}

td::string make_valid_domain_candidate(td::uint64 &state) {
  for (size_t attempt = 0; attempt < 16; attempt++) {
    auto label_count = 1 + static_cast<size_t>(next_lcg(state) % 5);
    td::string domain;
    for (size_t i = 0; i < label_count; i++) {
      auto label_size = 1 + static_cast<size_t>(next_lcg(state) % 24);
      if (!domain.empty()) {
        domain.push_back('.');
      }

      domain.push_back(sample_alnum(state));
      for (size_t j = 1; j + 1 < label_size; j++) {
        domain.push_back(sample_middle(state));
      }
      if (label_size > 1) {
        domain.push_back(sample_alnum(state));
      }
    }

    if (domain.size() <= ProxySecret::MAX_DOMAIN_LENGTH) {
      return domain;
    }
  }

  return "a.example.com";
}

TEST(ProxySecretTlsDomainValidationAdversarial, LightFuzzRejectsMalformedHostnamesByValidationOracle) {
  td::uint64 state = 0xBADC0FFEE0DDF00DULL;

  for (size_t i = 0; i < 6000; i++) {
    auto domain = make_hostile_domain_candidate(state);
    if (!must_reject_as_hostname(domain)) {
      continue;
    }

    auto r_secret = ProxySecret::from_binary(make_tls_emulation_secret(domain));
    ASSERT_TRUE(r_secret.is_error());

    // Truncation must not bypass malformed-label rejection for deterministic
    // hyphen-edge domains whose violating byte appears near the front.
    td::string prefixed_invalid = "-" + domain;
    auto r_truncated = ProxySecret::from_binary(make_tls_emulation_secret(prefixed_invalid), true);
    ASSERT_TRUE(r_truncated.is_error());
  }
}

TEST(ProxySecretTlsDomainValidationAdversarial, LightFuzzAcceptsStrictAsciiHostnamesAcrossSeedMatrix) {
  td::uint64 state = 0x0123456789ABCDEFULL;

  for (size_t i = 0; i < 6000; i++) {
    auto domain = make_valid_domain_candidate(state);
    ASSERT_FALSE(must_reject_as_hostname(domain));

    auto r_secret = ProxySecret::from_binary(make_tls_emulation_secret(domain));
    ASSERT_TRUE(r_secret.is_ok());
    ASSERT_TRUE(r_secret.ok().emulate_tls());
  }
}

TEST(ProxySecretTlsDomainValidationAdversarial, EncodedLengthGuardRejectsOversizedInputsAcrossDeterministicMatrix) {
  constexpr size_t kMaxNormalEncodedLength = 2 * (17 + ProxySecret::MAX_DOMAIN_LENGTH);
  constexpr size_t kMaxTruncationEncodedLength = 2 * (17 + ProxySecret::MAX_DOMAIN_LENGTH + 1);

  const size_t oversized_lengths[] = {
      kMaxTruncationEncodedLength + 1,
      kMaxTruncationEncodedLength + 9,
      kMaxTruncationEncodedLength + 257,
      kMaxTruncationEncodedLength + 4097,
  };

  for (auto encoded_length : oversized_lengths) {
    auto encoded = td::string(encoded_length, 'a');

    auto r_normal = ProxySecret::from_link(encoded, false);
    ASSERT_TRUE(r_normal.is_error());
    ASSERT_TRUE(r_normal.error().message().str().find("encoded_length_out_of_bounds") != td::string::npos);

    auto r_trunc = ProxySecret::from_link(encoded, true);
    ASSERT_TRUE(r_trunc.is_error());
    ASSERT_TRUE(r_trunc.error().message().str().find("encoded_length_out_of_bounds") != td::string::npos);

    ASSERT_TRUE(encoded_length > kMaxNormalEncodedLength);
    ASSERT_TRUE(encoded_length > kMaxTruncationEncodedLength);
  }
}

}  // namespace
