// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "test/logging_hardening_test_utils.h"

#include "td/utils/tests.h"

namespace {

using td::logging_hardening::test::load_repo_text;
using td::logging_hardening::test::normalize_for_contract;

TEST(LoggingTde2eBlkchContract, HeaderDeclaresAtomicVerbosityTag) {
  auto header = load_repo_text("tde2e/td/e2e/TestBlockchain.h");
  auto normalized = normalize_for_contract(header);

  ASSERT_TRUE(header.find("#include <atomic>") != td::string::npos);
  ASSERT_TRUE(normalized.find("externstd::atomic<int>VERBOSITY_NAME(blkch);") != td::string::npos);
  ASSERT_TRUE(normalized.find("externintVERBOSITY_NAME(blkch);") == td::string::npos);
}

TEST(LoggingTde2eBlkchContract, SourceDefinesAtomicVerbosityTag) {
  auto source = load_repo_text("tde2e/td/e2e/TestBlockchain.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(normalized.find("std::atomic<int>VERBOSITY_NAME(blkch)") != td::string::npos);
  ASSERT_TRUE(normalized.find("intVERBOSITY_NAME(blkch)=") == td::string::npos);
}

}  // namespace
