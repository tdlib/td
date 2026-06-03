// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "test/stealth/SourceContractFileReader.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

namespace {

td::string load_repo_text(td::Slice relative_path) {
  return td::mtproto::test::read_repo_text_file(relative_path);
}

}  // namespace

TEST(TdutilsHeapHashMiscContract, heap_source_uses_explicit_memsize_arity_and_named_constants) {
  const auto source = load_repo_text("tdutils/td/utils/Heap.h");

  ASSERT_TRUE(source.find("static constexpr size_t kArity") != td::string::npos);
  ASSERT_TRUE(source.find("static constexpr size_t kShrinkDivisor") != td::string::npos);
  ASSERT_TRUE(source.find("i * kArity") != td::string::npos);
  ASSERT_TRUE(source.find("(pos - 1) / kArity") != td::string::npos);

  ASSERT_EQ(td::string::npos, source.find("i * K"));
  ASSERT_EQ(td::string::npos, source.find("/ 4"));
}

TEST(TdutilsHeapHashMiscContract, hash_table_utils_source_uses_named_u64_shift_constant) {
  const auto source = load_repo_text("tdutils/td/utils/HashTableUtils.h");

  ASSERT_TRUE(source.find("static constexpr uint32 kU64HighShiftBits") != td::string::npos);
  ASSERT_TRUE(source.find("value >> kU64HighShiftBits") != td::string::npos);

  ASSERT_EQ(td::string::npos, source.find("value >> 32"));
}

TEST(TdutilsHeapHashMiscContract, misc_source_uses_named_ascii_case_bit_and_explicit_alignment_mask_cast) {
  const auto source = load_repo_text("tdutils/td/utils/misc.h");

  ASSERT_TRUE(source.find("static constexpr char kAsciiCaseBit") != td::string::npos);
  ASSERT_TRUE(source.find("c |= kAsciiCaseBit") != td::string::npos);
  ASSERT_TRUE(source.find("static_cast<std::uintptr_t>(Alignment - 1)") != td::string::npos);

  ASSERT_EQ(td::string::npos, source.find("c |= 0x20"));
}
