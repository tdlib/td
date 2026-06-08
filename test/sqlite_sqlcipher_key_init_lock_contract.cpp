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

TEST(DBSqlcipherKeyInitLockContract, SqlcipherKeyPathTakesProcessWideMutexBeforeNativeOpen) {
  const auto source = td::mtproto::test::read_repo_text_file("tddb/td/db/SqliteDb.cpp");
  const auto wrapper = normalize_for_contract(extract_region(
      source, "Result<SqliteDb> SqliteDb::open_with_key(CSlice path, bool allow_creation, const DbKey &db_key,",
      "Result<SqliteDb> SqliteDb::do_open_with_key"));
  const auto key_region = normalize_for_contract(extract_region(
      source, "Result<SqliteDb> SqliteDb::do_open_with_key(CSlice path, bool allow_creation, const DbKey &db_key,",
      "TRY_STATUS_PREFIX(db.check_encryption(), \"Can't check database: \""));

  ASSERT_TRUE(wrapper.find("if(!db_key.is_empty()){autokey_init_lock=detail::RawSqliteDb::lock_sqlcipher_key_init_mutex();") !=
              td::string::npos);
  ASSERT_TRUE(wrapper.find("autores=do_open_with_key(path,allow_creation,db_key,") != td::string::npos);
  ASSERT_TRUE(wrapper.find("returndo_open_with_key(path,false,db_key,3);") != td::string::npos);
  ASSERT_TRUE(key_region.find("TRY_STATUS(db.exec(PSLICE()<<\"PRAGMAkey=\"<<key));") != td::string::npos);
}

TEST(DBSqlcipherKeyInitLockContract, SqlcipherKeyPathRetainsCipherCompatibilityPathUnderLock) {
  const auto source = td::mtproto::test::read_repo_text_file("tddb/td/db/SqliteDb.cpp");
  const auto wrapper = normalize_for_contract(extract_region(
      source, "Result<SqliteDb> SqliteDb::open_with_key(CSlice path, bool allow_creation, const DbKey &db_key,",
      "Result<SqliteDb> SqliteDb::do_open_with_key"));
  const auto key_region = normalize_for_contract(extract_region(
      source, "Result<SqliteDb> SqliteDb::do_open_with_key(CSlice path, bool allow_creation, const DbKey &db_key,",
      "TRY_STATUS_PREFIX(db.check_encryption(), \"Can't check database: \""));

  ASSERT_TRUE(wrapper.find("autokey_init_lock=detail::RawSqliteDb::lock_sqlcipher_key_init_mutex();") <
              wrapper.find("autores=do_open_with_key(path,allow_creation,db_key,"));
  ASSERT_TRUE(key_region.find("if(db.check_encryption().is_ok()){") != td::string::npos);
  ASSERT_TRUE(
      key_region.find(
          "if(cipher_version!=0){LOG(INFO)<<\"TryingSQLCiphercompatibilitymodewithversion=\"<<cipher_version;") !=
      td::string::npos);
}

TEST(DBSqlcipherKeyInitLockContract, EncryptedPathKeepsPostKeyEncryptionCheckInsideLockScope) {
  const auto source = td::mtproto::test::read_repo_text_file("tddb/td/db/SqliteDb.cpp");
  const auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(normalized.find("db.set_cipher_version(cipher_version);TRY_STATUS_PREFIX(db.check_encryption(),\"Can'"
                              "tcheckdatabase:\");db.raw_->set_close_under_sqlcipher_key_init_mutex();"
                              "returnstd::move(db);") != td::string::npos);
}

TEST(DBSqlcipherKeyInitLockContract, EncryptedHandleCloseUsesSqlcipherMutex) {
  const auto db_source = normalize_for_contract(td::mtproto::test::read_repo_text_file("tddb/td/db/SqliteDb.cpp"));
  const auto raw_header = normalize_for_contract(td::mtproto::test::read_repo_text_file("tddb/td/db/detail/RawSqliteDb.h"));
  const auto raw_source = normalize_for_contract(td::mtproto::test::read_repo_text_file("tddb/td/db/detail/RawSqliteDb.cpp"));

  ASSERT_TRUE(raw_header.find("voidset_close_under_sqlcipher_key_init_mutex()") != td::string::npos);
  ASSERT_TRUE(raw_source.find("Mutexsqlcipher_key_init_mutex;") != td::string::npos);
  ASSERT_TRUE(raw_source.find("if(close_under_sqlcipher_key_init_mutex_){autokey_init_lock=lock_sqlcipher_key_init_mutex();"
                              "close();return;}close();") != td::string::npos);
  ASSERT_TRUE(db_source.find("db.raw_->set_close_under_sqlcipher_key_init_mutex();returnstd::move(db);") !=
              td::string::npos);
}
