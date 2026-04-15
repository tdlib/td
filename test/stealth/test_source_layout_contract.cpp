// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/mtproto/BlobStore.h"
#include "td/telegram/ReferenceTable.h"

#include "td/utils/tests.h"

#include "td/mtproto/mtproto_api.h"

#include "td/utils/as.h"
#include "td/utils/crypto.h"
#include "td/utils/tl_storers.h"
#include <fstream>
#include <iterator>

#include <openssl/pem.h>
#include <openssl/rsa.h>

namespace {

td::string read_text_file(td::Slice path) {
  std::ifstream input(path.str(), std::ios::binary);
  CHECK(input.is_open());
  return td::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

td::string extract_pem_literal(const td::string &content, td::Slice function_signature) {
  auto signature_pos = content.find(function_signature.str());
  CHECK(signature_pos != td::string::npos);

  auto return_pos = content.find("return", signature_pos);
  CHECK(return_pos != td::string::npos);

  auto end_pos = content.find(';', return_pos);
  CHECK(end_pos != td::string::npos);

  td::string pem;
  size_t position = return_pos;
  while (true) {
    auto quote_pos = content.find('"', position);
    if (quote_pos == td::string::npos || quote_pos >= end_pos) {
      break;
    }

    for (size_t index = quote_pos + 1; index < end_pos; index++) {
      auto ch = content[index];
      if (ch == '\\') {
        CHECK(index + 1 < end_pos);
        auto escaped = content[++index];
        switch (escaped) {
          case 'n':
            pem.push_back('\n');
            break;
          case '\\':
            pem.push_back('\\');
            break;
          case '"':
            pem.push_back('"');
            break;
          default:
            pem.push_back(escaped);
            break;
        }
        continue;
      }
      if (ch == '"') {
        position = index + 1;
        break;
      }
      pem.push_back(ch);
    }
  }

  CHECK(!pem.empty());
  if (pem.back() != '\n') {
    pem.push_back('\n');
  }
  return pem;
}

td::int64 compute_legacy_rsa_public_key_fingerprint(td::Slice pem) {
  auto *input = BIO_new_mem_buf(pem.begin(), static_cast<int>(pem.size()));
  CHECK(input != nullptr);
  SCOPE_EXIT {
    BIO_free(input);
  };

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
  auto *rsa = PEM_read_bio_RSAPublicKey(input, nullptr, nullptr, nullptr);
  CHECK(rsa != nullptr);
  SCOPE_EXIT {
    RSA_free(rsa);
  };

  const BIGNUM *n_num = nullptr;
  const BIGNUM *e_num = nullptr;
  RSA_get0_key(rsa, &n_num, &e_num, nullptr);
  CHECK(n_num != nullptr);
  CHECK(e_num != nullptr);

  auto n = td::BigNum::from_raw(BN_dup(n_num));
  auto e = td::BigNum::from_raw(BN_dup(e_num));
  auto n_str = n.to_binary();
  auto e_str = e.to_binary();
  td::mtproto_api::rsa_public_key public_key(n_str, e_str);
  size_t size = td::tl_calc_length(public_key);
  std::vector<unsigned char> tmp(size);
  size = td::tl_store_unsafe(public_key, tmp.data());
  CHECK(size == tmp.size());

  unsigned char key_sha1[20];
  td::sha1(td::Slice(tmp.data(), tmp.size()), key_sha1);
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
  return td::as<td::int64>(key_sha1 + 12);
}

TEST(SourceLayoutContract, BundledPrimaryAndSecondaryBlocksMatchSlots) {
  auto source = read_text_file("td/telegram/net/PublicRsaKeySharedMain.cpp");

  auto main_fingerprint =
      compute_legacy_rsa_public_key_fingerprint(extract_pem_literal(source, "CSlice retained_primary_block()"));
  auto test_fingerprint =
      compute_legacy_rsa_public_key_fingerprint(extract_pem_literal(source, "CSlice retained_secondary_block()"));

  ASSERT_EQ(td::ReferenceTable::slot_value(td::mtproto::BlobRole::Primary), main_fingerprint);
  ASSERT_EQ(td::ReferenceTable::slot_value(td::mtproto::BlobRole::Secondary), test_fingerprint);
}

TEST(SourceLayoutContract, BundledAuxiliaryBlockMatchesSlot) {
  auto source = read_text_file("td/telegram/ConfigManager.cpp");
  auto recovery_fingerprint =
      compute_legacy_rsa_public_key_fingerprint(extract_pem_literal(source, "CSlice retained_auxiliary_block()"));

  ASSERT_EQ(td::ReferenceTable::slot_value(td::mtproto::BlobRole::Auxiliary), recovery_fingerprint);
}

}  // namespace