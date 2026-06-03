// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
//
// CONTRACT TESTS: tl_simple.h struct initialization
//
// These are source-inspection contract tests for the CWE-457 fixes.
// tl_simple.h includes <iostream> which is incompatible with the td
// StringBuilder template resolution in the test framework; therefore we
// inspect the source text directly rather than compiling tl_simple.h
// into this translation unit.

#include "test/stealth/SourceContractFileReader.h"

#include "td/utils/tests.h"

#include <string>

namespace {

std::string load_tl_simple_source() {
  return td::mtproto::test::read_repo_text_file("tdtl/td/tl/tl_simple.h");
}

}  // namespace

// Contract: Arg::type must have a non-default-member-initializer (NSDMI) of nullptr.
TEST(TlSimpleStructInitContract, arg_type_has_nullptr_nsdmi) {
  const auto src = load_tl_simple_source();
  ASSERT_FALSE(src.empty());
  ASSERT_TRUE(src.find("const Type *type{nullptr}") != std::string::npos);
}

// Contract: Constructor::id must have a NSDMI of 0.
TEST(TlSimpleStructInitContract, constructor_id_has_zero_nsdmi) {
  const auto src = load_tl_simple_source();
  ASSERT_FALSE(src.empty());
  // Accept both "id{0}" and "id = 0" style NSDMI.
  const bool nsdmi_found = src.find("id{0}") != std::string::npos || src.find("id = 0") != std::string::npos;
  ASSERT_TRUE(nsdmi_found);
}

// Contract: Constructor::type must have a NSDMI of nullptr.
TEST(TlSimpleStructInitContract, constructor_type_has_nullptr_nsdmi) {
  const auto src = load_tl_simple_source();
  ASSERT_FALSE(src.empty());
  ASSERT_TRUE(src.find("const CustomType *type{nullptr}") != std::string::npos);
}

// Regression: the specific NSDMI text must come AFTER the Arg struct opening.
TEST(TlSimpleStructInitContract, arg_type_nsdmi_is_inside_arg_struct) {
  const auto src = load_tl_simple_source();
  ASSERT_FALSE(src.empty());

  const auto struct_pos = src.find("struct Arg {");
  ASSERT_TRUE(struct_pos != std::string::npos);

  const auto nsdmi_pos = src.find("const Type *type{nullptr}", struct_pos);
  ASSERT_TRUE(nsdmi_pos != std::string::npos);

  // The NSDMI must appear before the next struct keyword (i.e., still inside Arg).
  const auto next_struct = src.find("\nstruct ", struct_pos + 1);
  ASSERT_TRUE(nsdmi_pos < next_struct);
}

TEST(TlSimpleStructInitContract, constructor_id_nsdmi_is_inside_constructor_struct) {
  const auto src = load_tl_simple_source();
  ASSERT_FALSE(src.empty());

  const auto struct_pos = src.find("struct Constructor {");
  ASSERT_TRUE(struct_pos != std::string::npos);

  const auto nsdmi_pos = src.find("id{0}", struct_pos);
  ASSERT_TRUE(nsdmi_pos != std::string::npos);

  const auto next_struct = src.find("\nstruct ", struct_pos + 1);
  // nsdmi must be before next struct or end of file.
  if (next_struct != std::string::npos) {
    ASSERT_TRUE(nsdmi_pos < next_struct);
  }
}
