// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
//
// CONTRACT TESTS: tl-parser.c memory leak fixes
//
// Risk: CWE-401 (Memory leak in tl_parse_type_term and tl_parse_nat_term).
// When tl_parse_term() returns a non-null node with the wrong type tag,
// the error path printed the message and returned 0 WITHOUT freeing the
// allocated tl_combinator_tree node.

#include "td/utils/tests.h"

#include <fstream>
#include <iterator>
#include <string>

#ifndef TELEMT_TEST_REPO_ROOT
#define TELEMT_TEST_REPO_ROOT ""
#endif

namespace {

std::string load_tl_parser_source() {
  std::string path(TELEMT_TEST_REPO_ROOT);
  if (!path.empty()) {
    path += '/';
  }
  path += "td/generate/tl-parser/tl-parser.c";
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) {
    return {};
  }
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
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

// Extract the body of the first function matching sig_prefix, starting from
// its first '{' and ending at the matching '}'.
std::string extract_function_body(const std::string &src, const std::string &sig_prefix) {
  auto fn_pos = src.find(sig_prefix);
  if (fn_pos == std::string::npos) {
    return {};
  }
  auto brace_pos = src.find('{', fn_pos);
  if (brace_pos == std::string::npos) {
    return {};
  }
  int depth = 0;
  std::size_t end_pos = brace_pos;
  for (std::size_t i = brace_pos; i < src.size(); ++i) {
    if (src[i] == '{') {
      ++depth;
    } else if (src[i] == '}') {
      --depth;
      if (depth == 0) {
        end_pos = i;
        break;
      }
    }
  }
  return src.substr(brace_pos, end_pos - brace_pos + 1);
}

}  // namespace

// ---------------------------------------------------------------------------
// Contract: tfree(Z,...) must appear at least once in the source
// ---------------------------------------------------------------------------

TEST(TlParserMemoryLeakContract, tfree_Z_appears_in_source) {
  const auto src = load_tl_parser_source();
  ASSERT_FALSE(src.empty());
  ASSERT_TRUE(src.find("tfree(Z, sizeof(*Z))") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Contract: tl_parse_type_term error path must free Z
// ---------------------------------------------------------------------------

TEST(TlParserMemoryLeakContract, tl_parse_type_term_function_body_contains_tfree_Z) {
  const auto src = load_tl_parser_source();
  ASSERT_FALSE(src.empty());

  const auto fn_body = extract_function_body(src, "struct tl_combinator_tree *tl_parse_type_term");
  ASSERT_FALSE(fn_body.empty());
  ASSERT_TRUE(fn_body.find("tfree(Z,") != std::string::npos);
}

// tfree(Z,...) must appear before TL_FAIL in tl_parse_type_term's error path.
TEST(TlParserMemoryLeakContract, tl_parse_type_term_tfree_before_TL_FAIL) {
  const auto src = load_tl_parser_source();
  ASSERT_FALSE(src.empty());

  const std::string fn_sig = "struct tl_combinator_tree *tl_parse_type_term";
  auto fn_pos = src.find(fn_sig);
  ASSERT_TRUE(fn_pos != std::string::npos);

  auto if_z_pos = src.find("if (Z) {", fn_pos);
  ASSERT_TRUE(if_z_pos != std::string::npos);

  auto tfree_pos = src.find("tfree(Z,", if_z_pos);
  auto tl_fail_pos = src.find("TL_FAIL", if_z_pos);
  ASSERT_TRUE(tfree_pos != std::string::npos);
  ASSERT_TRUE(tl_fail_pos != std::string::npos);
  ASSERT_TRUE(tfree_pos < tl_fail_pos);
}

// ---------------------------------------------------------------------------
// Contract: tl_parse_nat_term error path must free Z
// ---------------------------------------------------------------------------

TEST(TlParserMemoryLeakContract, tl_parse_nat_term_function_body_contains_tfree_Z) {
  const auto src = load_tl_parser_source();
  ASSERT_FALSE(src.empty());

  const auto fn_body = extract_function_body(src, "struct tl_combinator_tree *tl_parse_nat_term");
  ASSERT_FALSE(fn_body.empty());
  ASSERT_TRUE(fn_body.find("tfree(Z,") != std::string::npos);
}

// Both functions must have the fix — verify at least 2 occurrences of tfree(Z,
TEST(TlParserMemoryLeakContract, two_tfree_Z_calls_in_source) {
  const auto src = load_tl_parser_source();
  ASSERT_FALSE(src.empty());
  ASSERT_TRUE(count_occurrences(src, "tfree(Z, sizeof(*Z))") >= 2u);
}

// ---------------------------------------------------------------------------
// Additional: verify no regression from removing the free call
// ---------------------------------------------------------------------------

TEST(TlParserMemoryLeakContract, tl_parse_type_term_source_contains_error_log_and_free) {
  const auto src = load_tl_parser_source();
  ASSERT_FALSE(src.empty());

  const auto fn_body = extract_function_body(src, "struct tl_combinator_tree *tl_parse_type_term");
  ASSERT_FALSE(fn_body.empty());
  // Both the error log AND the free must be present in the error path.
  ASSERT_TRUE(fn_body.find("TL_ERROR(\"type_term") != std::string::npos);
  ASSERT_TRUE(fn_body.find("tfree(Z,") != std::string::npos);
}

TEST(TlParserMemoryLeakContract, tl_parse_nat_term_source_contains_error_log_and_free) {
  const auto src = load_tl_parser_source();
  ASSERT_FALSE(src.empty());

  const auto fn_body = extract_function_body(src, "struct tl_combinator_tree *tl_parse_nat_term");
  ASSERT_FALSE(fn_body.empty());
  ASSERT_TRUE(fn_body.find("TL_ERROR(\"nat_term") != std::string::npos);
  ASSERT_TRUE(fn_body.find("tfree(Z,") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Contract: speculative parse_args* branches must delete parsed S before
// rewinding parser state with load_parse(...).
// ---------------------------------------------------------------------------

TEST(TlParserMemoryLeakContract, parse_args2_rewind_paths_delete_speculative_S_before_load_parse) {
  const auto src = load_tl_parser_source();
  ASSERT_FALSE(src.empty());

  const auto fn_body = extract_function_body(src, "struct tree *parse_args2(void)");
  ASSERT_FALSE(fn_body.empty());

  auto optional_pos = fn_body.find("PARSE_TRY(parse_optional_arg_def);");
  ASSERT_TRUE(optional_pos != std::string::npos);
  auto optional_region = fn_body.substr(optional_pos, 220);
  auto optional_delete_pos = optional_region.find("tree_delete(S);");
  auto optional_rewind_pos = optional_region.find("load_parse(so);");
  ASSERT_TRUE(optional_delete_pos != std::string::npos);
  ASSERT_TRUE(optional_rewind_pos != std::string::npos);
  ASSERT_TRUE(optional_delete_pos < optional_rewind_pos);

  auto multiplicity_pos = fn_body.find("PARSE_TRY(parse_multiplicity);");
  ASSERT_TRUE(multiplicity_pos != std::string::npos);
  auto multiplicity_region = fn_body.substr(multiplicity_pos, 220);
  auto multiplicity_delete_pos = multiplicity_region.find("tree_delete(S);");
  auto multiplicity_rewind_pos = multiplicity_region.find("load_parse(save2);");
  ASSERT_TRUE(multiplicity_delete_pos != std::string::npos);
  ASSERT_TRUE(multiplicity_rewind_pos != std::string::npos);
  ASSERT_TRUE(multiplicity_delete_pos < multiplicity_rewind_pos);
}

TEST(TlParserMemoryLeakContract, tl_try_macro_frees_partial_trees_before_TL_FAIL_on_union_error) {
  const auto src = load_tl_parser_source();
  ASSERT_FALSE(src.empty());

  auto macro_pos = src.find("#define TL_TRY(f, x)");
  ASSERT_TRUE(macro_pos != std::string::npos);
  auto macro_end = src.find("#define TL_ERROR", macro_pos);
  ASSERT_TRUE(macro_end != std::string::npos);
  auto macro_region = src.substr(macro_pos, macro_end - macro_pos);

  auto union_pos = macro_region.find("tl_union(x, _t)");
  ASSERT_TRUE(union_pos != std::string::npos);
  auto fail_guard_pos = macro_region.find("if (!_u)", union_pos);
  ASSERT_TRUE(fail_guard_pos != std::string::npos);

  auto free_x_pos = macro_region.find("tl_free_combinator_tree(x);", fail_guard_pos);
  auto free_t_pos = macro_region.find("tl_free_combinator_tree(_t);", fail_guard_pos);
  auto fail_pos = macro_region.find("TL_FAIL", fail_guard_pos);
  ASSERT_TRUE(free_x_pos != std::string::npos);
  ASSERT_TRUE(free_t_pos != std::string::npos);
  ASSERT_TRUE(fail_pos != std::string::npos);
  ASSERT_TRUE(free_x_pos < fail_pos);
  ASSERT_TRUE(free_t_pos < fail_pos);
}
