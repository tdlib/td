// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/utils/tests.h"

#include <fstream>
#include <iterator>

namespace {

td::string read_text_file(td::Slice path) {
  std::ifstream input(path.str(), std::ios::binary);
  CHECK(input.is_open());
  return td::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

TEST(SourceLayoutAdversarial, RetainedBlocksRemainPresentInTransportSources) {
  auto key_source = read_text_file("td/telegram/net/PublicRsaKeySharedMain.cpp");
  auto config_source = read_text_file("td/telegram/ConfigManager.cpp");

  ASSERT_TRUE(key_source.find("retained_primary_block") != td::string::npos);
  ASSERT_TRUE(key_source.find("retained_secondary_block") != td::string::npos);
  ASSERT_TRUE(config_source.find("retained_auxiliary_block") != td::string::npos);
}

TEST(SourceLayoutAdversarial, RetainedBlocksBypassDirectOpenSslPath) {
  auto key_source = read_text_file("td/telegram/net/PublicRsaKeySharedMain.cpp");
  auto config_source = read_text_file("td/telegram/ConfigManager.cpp");

  ASSERT_TRUE(key_source.find("RSA::from_pem_public_key(retained_primary_block())") == td::string::npos);
  ASSERT_TRUE(key_source.find("RSA::from_pem_public_key(retained_secondary_block())") == td::string::npos);
  ASSERT_TRUE(config_source.find("RSA::from_pem_public_key(retained_auxiliary_block())") == td::string::npos);
}

TEST(SourceLayoutAdversarial, StorePathRemainsTheOnlyLiveLoader) {
  auto key_source = read_text_file("td/telegram/net/PublicRsaKeySharedMain.cpp");
  auto config_source = read_text_file("td/telegram/ConfigManager.cpp");

  ASSERT_TRUE(key_source.find("BlobStore::load(role)") != td::string::npos);
  ASSERT_TRUE(key_source.find("add_store_key(keys, mtproto::BlobRole::Primary)") != td::string::npos);
  ASSERT_TRUE(key_source.find("add_store_key(keys, mtproto::BlobRole::Secondary)") != td::string::npos);
  ASSERT_TRUE(config_source.find("BlobStore::load(mtproto::BlobRole::Auxiliary)") != td::string::npos);
}

}  // namespace