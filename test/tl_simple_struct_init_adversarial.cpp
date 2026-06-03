// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
//
// ADVERSARIAL SOURCE-INSPECTION TESTS: tl_simple.h struct initialization
//
// Adversarial / black-hat scenarios for the CWE-457 NSDMI fix.
// We use source inspection rather than include-and-compile because
// tl_simple.h pulls in <iostream> which conflicts with td::StringBuilder
// template overloads in the test framework.

#include "test/stealth/SourceContractFileReader.h"

#include "td/utils/tests.h"

#include <string>

namespace {

std::string load_tl_simple_source() {
  return td::mtproto::test::read_repo_text_file("tdtl/td/tl/tl_simple.h");
}

std::size_t count_occurrences(const std::string &haystack, const std::string &needle) {
  std::size_t count = 0;
  std::size_t pos = 0;
  while ((pos = haystack.find(needle, pos)) != std::string::npos) {
    ++count;
    pos += needle.size();
  }
  return count;
}

}  // namespace

// ADVERSARIAL: No raw uninitialized pointer declaration for Arg::type.
// Scopes the search to within the Arg struct body only.
TEST(TlSimpleStructInitAdversarial, arg_type_has_no_uninitialized_declaration) {
  const auto src = load_tl_simple_source();
  ASSERT_FALSE(src.empty());
  // Locate the Arg struct body.
  const auto struct_pos = src.find("struct Arg {");
  ASSERT_TRUE(struct_pos != std::string::npos);
  const auto close = src.find("};", struct_pos);
  ASSERT_TRUE(close != std::string::npos);
  const auto body = src.substr(struct_pos, close - struct_pos + 2);
  // The old (vulnerable) form must no longer be present inside Arg.
  ASSERT_TRUE(body.find("const Type *type;") == std::string::npos);
}

// ADVERSARIAL: No raw uninitialized int32_t id declaration.
TEST(TlSimpleStructInitAdversarial, constructor_id_has_no_uninitialized_declaration) {
  const auto src = load_tl_simple_source();
  ASSERT_FALSE(src.empty());
  // "id;" with no initializer must not exist near Constructor struct.
  const auto struct_pos = src.find("struct Constructor {");
  ASSERT_TRUE(struct_pos != std::string::npos);
  // Find the closing brace of Constructor.
  const auto close_brace = src.find("};", struct_pos);
  const auto body = src.substr(struct_pos, close_brace - struct_pos + 2);
  ASSERT_TRUE(body.find("std::int32_t id;") == std::string::npos);
}

// ADVERSARIAL: No raw uninitialized pointer for Constructor::type.
TEST(TlSimpleStructInitAdversarial, constructor_type_has_no_uninitialized_declaration) {
  const auto src = load_tl_simple_source();
  ASSERT_FALSE(src.empty());
  // The old (vulnerable) form must not exist.
  ASSERT_TRUE(src.find("const CustomType *type;") == std::string::npos);
}

// ADVERSARIAL: NSDMI initializers must appear exactly once each (not duplicated).
TEST(TlSimpleStructInitAdversarial, arg_type_nsdmi_appears_at_least_once) {
  const auto src = load_tl_simple_source();
  ASSERT_FALSE(src.empty());
  ASSERT_TRUE(count_occurrences(src, "const Type *type{nullptr}") >= 1u);
}

TEST(TlSimpleStructInitAdversarial, constructor_type_nsdmi_appears_exactly_once) {
  const auto src = load_tl_simple_source();
  ASSERT_FALSE(src.empty());
  ASSERT_EQ(1u, count_occurrences(src, "const CustomType *type{nullptr}"));
}

// ADVERSARIAL: Function struct also must not have uninitialized type or id.
TEST(TlSimpleStructInitAdversarial, function_struct_has_no_uninitialized_id) {
  const auto src = load_tl_simple_source();
  ASSERT_FALSE(src.empty());
  const auto struct_pos = src.find("struct Function {");
  ASSERT_TRUE(struct_pos != std::string::npos);
  const auto close = src.find("};", struct_pos);
  const auto body = src.substr(struct_pos, close - struct_pos + 2);
  ASSERT_TRUE(body.find("std::int32_t id;") == std::string::npos);
}

TEST(TlSimpleStructInitAdversarial, function_struct_has_no_uninitialized_type_ptr) {
  const auto src = load_tl_simple_source();
  ASSERT_FALSE(src.empty());
  const auto struct_pos = src.find("struct Function {");
  ASSERT_TRUE(struct_pos != std::string::npos);
  const auto close = src.find("};", struct_pos);
  const auto body = src.substr(struct_pos, close - struct_pos + 2);
  ASSERT_TRUE(body.find("const Type *type;") == std::string::npos);
}

// ADVERSARIAL: Attempt to find any other pointer member in Arg that lacks NSDMI.
TEST(TlSimpleStructInitAdversarial, arg_struct_has_no_raw_pointer_members_without_init) {
  const auto src = load_tl_simple_source();
  ASSERT_FALSE(src.empty());

  const auto struct_start = src.find("struct Arg {");
  ASSERT_TRUE(struct_start != std::string::npos);
  const auto struct_end = src.find("};", struct_start);
  ASSERT_TRUE(struct_end != std::string::npos);
  const auto body = src.substr(struct_start, struct_end - struct_start);

  // Check that any "* " pattern is followed by an initializer (brace or =).
  // This is a heuristic; it fails on multi-line decls.
  std::size_t pos = 0;
  bool found_uninit_ptr = false;
  while ((pos = body.find("*", pos)) != std::string::npos) {
    // Look for the end of this declaration (semicolon).
    const auto semi = body.find(';', pos);
    if (semi == std::string::npos) {
      break;
    }
    const auto decl = body.substr(pos, semi - pos);
    // If the declaration contains neither '{' nor '=' it's uninitialized.
    if (decl.find('{') == std::string::npos && decl.find('=') == std::string::npos) {
      found_uninit_ptr = true;
      break;
    }
    pos = semi + 1;
  }
  ASSERT_FALSE(found_uninit_ptr);
}
