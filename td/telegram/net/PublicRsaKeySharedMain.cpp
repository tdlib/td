//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/net/PublicRsaKeySharedMain.h"

#include "td/telegram/net/NetReliabilityMonitor.h"

#include "td/mtproto/PacketAlignmentSeeds.h"
#include "td/mtproto/BlobStore.h"

#include "td/net/SessionTicketSeeds.h"

#include "td/telegram/net/ConfigCacheSeeds.h"

#include "td/utils/crypto.h"
#include "td/utils/format.h"
#include "td/utils/HashIndexSeeds.h"
#include "td/utils/CatalogWeightTable.h"
#include "td/utils/Slice.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/UInt.h"

namespace td {

namespace {

CSlice retained_primary_block() {
  return "-----BEGIN RSA PUBLIC KEY-----\n"
         "MIIBCgKCAQEA6LszBcC1LGzyr992NzE0ieY+BSaOW622Aa9Bd4ZHLl+TuFQ4lo4g\n"
         "5nKaMBwK/BIb9xUfg0Q29/2mgIR6Zr9krM7HjuIcCzFvDtr+L0GQjae9H0pRB2OO\n"
         "62cECs5HKhT5DZ98K33vmWiLowc621dQuwKWSQKjWf50XYFw42h21P2KXUGyp2y/\n"
         "+aEyZ+uVgLLQbRA1dEjSDZ2iGRy12Mk5gpYc397aYp438fsJoHIgJ2lgMv5h7WY9\n"
         "t6N/byY9Nw9p21Og3AoXSL2q/2IJ1WRUhebgAdGVMlV1fkuOQoEzR7EdpqtQD9Cs\n"
         "5+bfo3Nhmcyvk5ftB0WkJ9z6bNZ7yxrP8wIDAQAB\n"
         "-----END RSA PUBLIC KEY-----";
}

CSlice retained_secondary_block() {
  return "-----BEGIN RSA PUBLIC KEY-----\n"
         "MIIBCgKCAQEAyMEdY1aR+sCR3ZSJrtztKTKqigvO/vBfqACJLZtS7QMgCGXJ6XIR\n"
         "yy7mx66W0/sOFa7/1mAZtEoIokDP3ShoqF4fVNb6XeqgQfaUHd8wJpDWHcR2OFwv\n"
         "plUUI1PLTktZ9uW2WE23b+ixNwJjJGwBDJPQEQFBE+vfmH0JP503wr5INS1poWg/\n"
         "j25sIWeYPHYeOrFp/eXaqhISP6G+q2IeTaWTXpwZj4LzXq5YOpk4bYEQ6mvRq7D1\n"
         "aHWfYmlEGepfaYR8Q0YqvvhYtMte3ITnuSJs171+GDqpdKcSwHnd6FudwGO4pcCO\n"
         "j4WcDuXc2CTHgH8gFTNhp/Y8/SpDOhvn9QIDAQAB\n"
         "-----END RSA PUBLIC KEY-----";
}

void touch_retained_block(CSlice pem) {
  volatile unsigned char sink = 0;
  sink ^= static_cast<unsigned char>(pem[0]);
  sink ^= static_cast<unsigned char>(pem[pem.size() - 1]);
  static_cast<void>(sink);
}

template <size_t N>
void append_bytes(string &target, const unsigned char (&bytes)[N]) {
  target.append(reinterpret_cast<const char *>(bytes), N);
}

uint64 load_uint64_le(Slice slice) {
  CHECK(slice.size() == 8);
  uint64 result = 0;
  for (size_t i = 0; i < 8; i++) {
    result |= static_cast<uint64>(static_cast<unsigned char>(slice[i])) << (i * 8);
  }
  return result;
}

}  // namespace

size_t PublicRsaKeySharedMain::expected_entry_count(bool is_test) {
  static_cast<void>(is_test);
  return 1;
}

Status PublicRsaKeySharedMain::validate_entry_count(size_t observed_entry_count, bool is_test) {
  auto expected_count = expected_entry_count(is_test);
  if (observed_entry_count == expected_count) {
    return Status::OK();
  }

  net_health::note_main_key_set_cardinality_failure(is_test, observed_entry_count, expected_count);
  return Status::Error(PSLICE() << "Unexpected entry count " << observed_entry_count << ", expected "
                                << expected_count);
}

Status PublicRsaKeySharedMain::check_catalog_entry(int64 fingerprint, bool is_test) {
  string key_material;
  key_material.reserve(128);
  append_bytes(key_material, vault_detail::kHashIndexSeeds);
  append_bytes(key_material, vault_detail::kSessionTicketSeeds);
  append_bytes(key_material, vault_detail::kPacketAlignmentSeeds);
  append_bytes(key_material, vault_detail::kConfigCacheSeeds);

  UInt256 mask;
  hmac_sha256(Slice("table_mix_v1_gamma"), Slice(key_material), as_mutable_slice(mask));

  auto mask_offset = is_test ? 8u : 0u;
  auto masked_expected = is_test ? static_cast<uint64>(vault_detail::kCatalogWeightSecondary)
                                 : static_cast<uint64>(vault_detail::kCatalogWeightPrimary);
  auto expected_fingerprint = masked_expected ^ load_uint64_le(as_slice(mask).substr(mask_offset, 8));
  if (static_cast<uint64>(fingerprint) == expected_fingerprint) {
    return Status::OK();
  }

  return Status::Error(PSLICE() << "Unexpected catalog entry " << format::as_hex(fingerprint));
}

std::shared_ptr<PublicRsaKeySharedMain> PublicRsaKeySharedMain::create(bool is_test) {
  auto add_store_key = [](vector<RsaKey> &keys, mtproto::BlobRole role) {
    auto rsa_result = mtproto::BlobStore::load(role);
    LOG_CHECK(rsa_result.is_ok()) << rsa_result.error();
    auto rsa = rsa_result.move_as_ok();
    auto status = check_catalog_entry(rsa.get_fingerprint(), role == mtproto::BlobRole::Secondary);
    LOG_CHECK(status.is_ok()) << status;
    keys.push_back(RsaKey{rsa.clone(), rsa.get_fingerprint()});
  };

  if (is_test) {
    static auto test_public_rsa_key = [&] {
      vector<RsaKey> keys;
      touch_retained_block(retained_secondary_block());
      add_store_key(keys, mtproto::BlobRole::Secondary);
      auto status = validate_entry_count(keys.size(), true);
      LOG_CHECK(status.is_ok()) << status;
      return std::make_shared<PublicRsaKeySharedMain>(std::move(keys));
    }();
    return test_public_rsa_key;
  } else {
    static auto main_public_rsa_key = [&] {
      vector<RsaKey> keys;
      touch_retained_block(retained_primary_block());
      add_store_key(keys, mtproto::BlobRole::Primary);
      auto status = validate_entry_count(keys.size(), false);
      LOG_CHECK(status.is_ok()) << status;
      return std::make_shared<PublicRsaKeySharedMain>(std::move(keys));
    }();
    return main_public_rsa_key;
  }
}

Result<mtproto::PublicRsaKeyInterface::RsaKey> PublicRsaKeySharedMain::get_rsa_key(const vector<int64> &fingerprints) {
  for (auto fingerprint : fingerprints) {
    for (const auto &key : keys_) {
      if (key.fingerprint == fingerprint) {
        return RsaKey{key.rsa.clone(), fingerprint};
      }
    }
  }
  return Status::Error(PSLICE() << "Unknown entry set " << fingerprints);
}

void PublicRsaKeySharedMain::drop_keys() {
  // nothing to do
}

}  // namespace td
