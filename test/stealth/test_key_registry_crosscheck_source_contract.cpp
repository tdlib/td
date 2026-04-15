// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <fstream>
#include <iterator>

namespace {

td::string read_text_file(td::Slice path) {
  std::ifstream input(path.str(), std::ios::binary);
  CHECK(input.is_open());
  return td::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

TEST(SlotCatalogSourceContract, EnrollmentCheckStaysAheadOfKeyInsertion) {
  auto source = read_text_file("td/telegram/net/PublicRsaKeySharedMain.cpp");

  auto validation_pos = source.find("check_enrolled_slot(");
  auto insertion_pos = source.find("keys.push_back(");

  ASSERT_TRUE(validation_pos != td::string::npos);
  ASSERT_TRUE(insertion_pos != td::string::npos);
  ASSERT_TRUE(validation_pos < insertion_pos);
}

TEST(SlotCatalogSourceContract, HandshakeCheckStaysAheadOfRsaEncrypt) {
  auto source = read_text_file("td/mtproto/Handshake.cpp");

  auto validation_pos = source.find("check_selected_slot(");
  auto encrypt_pos = source.find("rsa_key.rsa.encrypt(");

  ASSERT_TRUE(validation_pos != td::string::npos);
  ASSERT_TRUE(encrypt_pos != td::string::npos);
  ASSERT_TRUE(validation_pos < encrypt_pos);
}

TEST(SlotCatalogSourceContract, SessionInjectionCheckStaysAheadOfSharedAuthCreation) {
  auto source = read_text_file("td/telegram/net/NetQueryDispatcher.cpp");

  auto validation_pos = source.find("check_bound_slot(");
  auto create_pos = source.find("AuthDataShared::create(");

  ASSERT_TRUE(validation_pos != td::string::npos);
  ASSERT_TRUE(create_pos != td::string::npos);
  ASSERT_TRUE(validation_pos < create_pos);
}

TEST(SlotCatalogSourceContract, RecoveryCheckStaysAheadOfSignatureVerification) {
  auto source = read_text_file("td/telegram/ConfigManager.cpp");

  auto validation_pos = source.find("check_payload_slot(");
  auto decrypt_pos = source.find("rsa.decrypt_signature(");

  ASSERT_TRUE(validation_pos != td::string::npos);
  ASSERT_TRUE(decrypt_pos != td::string::npos);
  ASSERT_TRUE(validation_pos < decrypt_pos);
}

}  // namespace