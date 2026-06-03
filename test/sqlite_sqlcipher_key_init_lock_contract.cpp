// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

#include <string_view>

namespace {

td::string normalize_for_contract(td::Slice source) {
  td::string normalized;
  normalized.reserve(source.size());
  for (auto c : source) {
    const auto b = static_cast<unsigned char>(c);
    if (b == ' ' || b == '\t' || b == '\r' || b == '\n') {
      continue;
    }
    normalized.push_back(c);
  }
  return normalized;
}

td::string extract_region(std::string_view source, td::Slice begin_marker, td::Slice end_marker) {
  const auto begin = source.find(begin_marker.str());
  CHECK(begin != td::string::npos);
  const auto end = source.find(end_marker.str(), begin + begin_marker.size());
  CHECK(end != td::string::npos);
  CHECK(end > begin);
  return td::string(source.substr(begin, end - begin));
}

}  // namespace

TEST(DBSqlcipherKeyInitLockContract, SqlcipherKeyPathTakesProcessWideMutexBeforeKeyPragma) {
  const auto source = td::mtproto::test::read_repo_text_file("tddb/td/db/SqliteDb.cpp");
  const auto region = normalize_for_contract(extract_region(
      source, "Result<SqliteDb> SqliteDb::do_open_with_key(CSlice path, bool allow_creation, const DbKey &db_key,",
      "TRY_STATUS_PREFIX(db.check_encryption(), \"Can't check database: \""));

  ASSERT_TRUE(region.find("if(!db_key.is_empty()){autokey_init_lock=sqlcipher_key_init_mutex.lock();") !=
              td::string::npos);
  ASSERT_TRUE(region.find("TRY_STATUS(db.exec(PSLICE()<<\"PRAGMAkey=\"<<key));") != td::string::npos);
}

TEST(DBSqlcipherKeyInitLockContract, SqlcipherKeyPathRetainsCipherCompatibilityPathUnderLock) {
  const auto source = td::mtproto::test::read_repo_text_file("tddb/td/db/SqliteDb.cpp");
  const auto region = normalize_for_contract(extract_region(
      source, "Result<SqliteDb> SqliteDb::do_open_with_key(CSlice path, bool allow_creation, const DbKey &db_key,",
      "TRY_STATUS_PREFIX(db.check_encryption(), \"Can't check database: \""));

  ASSERT_TRUE(region.find("autokey_init_lock=sqlcipher_key_init_mutex.lock();if(db.check_encryption().is_ok()){") !=
              td::string::npos);
  ASSERT_TRUE(
      region.find(
          "if(cipher_version!=0){LOG(INFO)<<\"TryingSQLCiphercompatibilitymodewithversion=\"<<cipher_version;") !=
      td::string::npos);
}

TEST(DBSqlcipherKeyInitLockContract, EncryptedPathKeepsPostKeyEncryptionCheckInsideLockScope) {
  const auto source = td::mtproto::test::read_repo_text_file("tddb/td/db/SqliteDb.cpp");
  const auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(normalized.find("db.set_cipher_version(cipher_version);TRY_STATUS_PREFIX(db.check_encryption(),\"Can'"
                              "tcheckdatabase:\");returnstd::move(db);") != td::string::npos);
}
