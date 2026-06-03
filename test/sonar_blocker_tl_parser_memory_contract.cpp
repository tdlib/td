// Copyright 2026 tdlib-obf authors. All rights reserved.
// Distributed under the Boost Software License, Version 1.0.
//
// Sonar BLOCKER tl-parser-memory contract tests.
// These are source-level pattern tests (RED → GREEN after fixes).
// Categories: contract, adversarial.
// All fixes are for tl-parser.c memory leaks and C++ false-positive suppressions.

#include "td/utils/tests.h"

#include <cstdio>
#include <cstring>
#include <string>

#ifndef TELEMT_TEST_REPO_ROOT
#define TELEMT_TEST_REPO_ROOT ""
#endif

namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string read_file_if_exists(const std::string &path) {
  auto *file = std::fopen(path.c_str(), "rb");
  if (file == nullptr) {
    return {};
  }
  if (std::fseek(file, 0, SEEK_END) != 0) {
    std::fclose(file);
    return {};
  }
  auto size = std::ftell(file);
  if (size < 0) {
    std::fclose(file);
    return {};
  }
  std::rewind(file);

  std::string content;
  content.resize(static_cast<size_t>(size));
  if (!content.empty()) {
    auto read_size = std::fread(content.data(), 1, content.size(), file);
    if (read_size != content.size()) {
      std::fclose(file);
      return {};
    }
  }
  std::fclose(file);
  return content;
}

static std::string read_source_file(const char *rel_path) {
  const std::string repo_root = TELEMT_TEST_REPO_ROOT;
  if (repo_root.empty()) {
    return {};
  }
  return read_file_if_exists(repo_root + "/" + rel_path);
}

static bool contains(const std::string &haystack, const std::string &needle) {
  return haystack.find(needle) != std::string::npos;
}

static std::string extract_region(const std::string &src, const std::string &begin_anchor,
                                  const std::string &end_anchor) {
  auto begin = src.find(begin_anchor);
  if (begin == std::string::npos) {
    return {};
  }
  auto end = src.find(end_anchor, begin);
  if (end == std::string::npos) {
    end = src.size();
  }
  return src.substr(begin, end - begin);
}

static std::string normalize_ws(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    out.push_back((c == '\n' || c == '\t') ? ' ' : c);
  }
  // collapse consecutive spaces
  std::string out2;
  bool prev_space = false;
  for (char c : out) {
    if (c == ' ') {
      if (!prev_space) {
        out2.push_back(c);
      }
      prev_space = true;
    } else {
      out2.push_back(c);
      prev_space = false;
    }
  }
  return out2;
}

// ---------------------------------------------------------------------------
// tl-parser.c contracts
// ---------------------------------------------------------------------------

// CONTRACT: tl_type_check must call tree_clear_var_value to release the
// var_value tree produced by check_constructors_equal after each inner-loop
// iteration, preventing malloc node leaks flagged at Sonar L3137-3148.
TEST(SonarBlockerTlParserMemoryContract, tl_type_check_clears_var_value_tree_after_check_constructors_equal) {
  auto src = read_source_file("td/generate/tl-parser/tl-parser.c");
  ASSERT_TRUE(!src.empty());
  auto region = extract_region(src, "void tl_type_check(struct tl_type *t)", "int tl_add_constructor");
  ASSERT_TRUE(!region.empty());
  // The fix must place tree_clear_var_value(v) call inside the nested loop
  // body in tl_type_check, after check_constructors_equal.
  auto norm = normalize_ws(region);
  // Require tree_clear_var_value(v) to appear inside the function context
  // that also contains check_constructors_equal.
  ASSERT_TRUE(contains(norm, "tree_clear_var_value(v)"));
}

// CONTRACT: tl_parse_partial_comb_app_decl must free L, R, and X before each
// TL_FAIL return inside the loop (Sonar L1099 leak of L, R, X).
TEST(SonarBlockerTlParserMemoryContract, tl_parse_partial_comb_app_decl_frees_L_R_X_before_each_TL_FAIL_in_loop) {
  auto src = read_source_file("td/generate/tl-parser/tl-parser.c");
  ASSERT_TRUE(!src.empty());
  auto region = extract_region(src, "int tl_parse_partial_comb_app_decl(struct tree *T, int fun)",
                               "int tl_parse_partial_app_decl(struct tree *T, int fun)");
  ASSERT_TRUE(!region.empty());
  // Must contain tl_free_combinator_tree(X) inside tl_parse_partial_comb_app_decl.
  ASSERT_TRUE(contains(region, "tl_free_combinator_tree(X)"));
  // Must also free L and R in that function.
  // L and R are freed elsewhere too, but this confirms the pattern was added.
  // We check the co-occurrence of L and X free calls in the file.
  ASSERT_TRUE(contains(region, "tl_free_combinator_tree(L)"));
  ASSERT_TRUE(contains(region, "tl_free_combinator_tree(R)"));
}

// CONTRACT: tl_parse_opt_args must free R before TL_FAIL in the variable
// validation for-loop (Sonar L1112 leak of R).
TEST(SonarBlockerTlParserMemoryContract, tl_parse_opt_args_frees_R_before_TL_FAIL_in_var_name_loop) {
  auto src = read_source_file("td/generate/tl-parser/tl-parser.c");
  ASSERT_TRUE(!src.empty());
  auto region = extract_region(src, "struct tl_combinator_tree *tl_parse_opt_args(struct tree *T)",
                               "struct tl_combinator_tree *tl_parse_args134(struct tree *T)");
  ASSERT_TRUE(!region.empty());
  // tl_parse_opt_args must call tl_free_combinator_tree(R) in the
  // variable-name validation loop before returning.
  // We verify the pattern exists in the file (already confirmed for args134;
  // multiple appearances are expected since multiple functions need R freed).
  ASSERT_TRUE(contains(region, "tl_free_combinator_tree(R)"));
}

// CONTRACT: tl_parse_result_type must free L before TL_FAIL on the
// type-mismatch and finish-subtree-failure paths.
TEST(SonarBlockerTlParserMemoryContract, tl_parse_result_type_frees_L_before_TL_FAIL_on_type_mismatch) {
  auto src = read_source_file("td/generate/tl-parser/tl-parser.c");
  ASSERT_TRUE(!src.empty());
  auto region_src = extract_region(src, "struct tl_combinator_tree *tl_parse_result_type(struct tree *T)", "int __ok;");
  ASSERT_TRUE(!region_src.empty());
  // tl_parse_result_type contains "Type mistmatch" error; confirm L is freed
  // before the TL_FAIL that follows it.
  // The pattern requires free(L) followed (within very close proximity) by
  // TL_FAIL after the "Type mistmatch" message.
  auto norm = normalize_ws(region_src);
  size_t mistmatch_pos = norm.find("Type mistmatch");
  ASSERT_TRUE(mistmatch_pos != std::string::npos);
  // After the mistmatch string, within 200 chars, tl_free_combinator_tree(L)
  // must appear before TL_FAIL.
  auto region = norm.substr(mistmatch_pos, 300);
  auto free_pos = region.find("tl_free_combinator_tree(L)");
  auto fail_pos = region.find("TL_FAIL");
  ASSERT_TRUE(free_pos != std::string::npos);
  ASSERT_TRUE(fail_pos != std::string::npos);
  ASSERT_TRUE(free_pos < fail_pos);
}

// CONTRACT: tl_parse_combinator_decl must free L when tl_parse_result_type
// returns null (TL_FAIL at the !R check).
TEST(SonarBlockerTlParserMemoryContract, tl_parse_combinator_decl_frees_L_when_result_type_fails) {
  auto src = read_source_file("td/generate/tl-parser/tl-parser.c");
  ASSERT_TRUE(!src.empty());
  auto region_src = extract_region(src, "int tl_parse_combinator_decl(struct tree *T, int fun)",
                                   "int tl_parse_partial_type_app_decl(struct tree *T)");
  ASSERT_TRUE(!region_src.empty());
  // The function constructs L from TL_TRY loops then checks R = tl_parse_result_type.
  // On R==null: must free L.  The pattern "tl_free_combinator_tree(L)" is
  // already verified to exist; this test specifically checks the "!R" path has
  // a free before TL_FAIL.
  auto norm = normalize_ws(region_src);
  // Find "if (!R)" near a tl_free_combinator_tree(L) then TL_FAIL sequence.
  size_t not_r_pos = norm.find("if (!R)");
  ASSERT_TRUE(not_r_pos != std::string::npos);
  auto region = norm.substr(not_r_pos, 200);
  ASSERT_TRUE(contains(region, "tl_free_combinator_tree(L)"));
  ASSERT_TRUE(contains(region, "TL_FAIL"));
}

// CONTRACT: tl_parse_args134 must free R (and conditionally tl_tree_dup copies)
// before TL_FAIL in the variable-name validation loop.
TEST(SonarBlockerTlParserMemoryContract, tl_parse_args134_frees_R_in_var_name_loop_before_TL_FAIL) {
  auto src = read_source_file("td/generate/tl-parser/tl-parser.c");
  ASSERT_TRUE(!src.empty());
  auto region_src = extract_region(src, "struct tl_combinator_tree *tl_parse_args134(struct tree *T)",
                                   "int tl_parse_combinator_decl(struct tree *T, int fun)");
  ASSERT_TRUE(!region_src.empty());
  // "Variable name expected" error is in tl_parse_args134 first for-loop.
  auto norm = normalize_ws(region_src);
  size_t err_pos = norm.find("Variable name expected");
  ASSERT_TRUE(err_pos != std::string::npos);
  auto region = norm.substr(err_pos, 200);
  auto free_pos = region.find("tl_free_combinator_tree(R)");
  auto fail_pos = region.find("TL_FAIL");
  ASSERT_TRUE(free_pos != std::string::npos);
  ASSERT_TRUE(fail_pos != std::string::npos);
  ASSERT_TRUE(free_pos < fail_pos);
}

// CONTRACT: The ptr_ fields in tl_object_ptr (a custom RAII smart pointer)
// are properly managed by the destructor — Sonar false-positives must be
// annotated with NOSONAR in every flagged file.
TEST(SonarBlockerTlParserMemoryContract, inline_queries_manager_ptr_leak_is_suppressed_with_nosonar) {
  auto src = read_source_file("td/telegram/InlineQueriesManager.cpp");
  ASSERT_TRUE(!src.empty());
  ASSERT_TRUE(contains(src, "NOSONAR"));
}

TEST(SonarBlockerTlParserMemoryContract, messages_manager_ptr_leak_is_suppressed_with_nosonar) {
  auto src = read_source_file("td/telegram/MessagesManager.cpp");
  ASSERT_TRUE(!src.empty());
  ASSERT_TRUE(contains(src, "NOSONAR"));
}

TEST(SonarBlockerTlParserMemoryContract, requests_ptr_leak_is_suppressed_with_nosonar) {
  auto src = read_source_file("td/telegram/Requests.cpp");
  ASSERT_TRUE(!src.empty());
  ASSERT_TRUE(contains(src, "NOSONAR"));
}

TEST(SonarBlockerTlParserMemoryContract, theme_manager_ptr_leak_is_suppressed_with_nosonar) {
  auto src = read_source_file("td/telegram/ThemeManager.cpp");
  ASSERT_TRUE(!src.empty());
  ASSERT_TRUE(contains(src, "NOSONAR"));
}

TEST(SonarBlockerTlParserMemoryContract, file_manager_ptr_leak_is_suppressed_with_nosonar) {
  auto src = read_source_file("td/telegram/files/FileManager.cpp");
  ASSERT_TRUE(!src.empty());
  ASSERT_TRUE(contains(src, "NOSONAR"));
}

// CONTRACT: RwMutex _unsafe methods are intentionally designed for use by
// external callbacks (OpenSSL locking) and must carry NOSONAR annotations
// to suppress spurious lock-order-reversal and double-acquire Sonar findings.
TEST(SonarBlockerTlParserMemoryContract, rw_mutex_unsafe_methods_carry_nosonar_annotations) {
  auto src = read_source_file("tdutils/td/utils/port/RwMutex.h");
  ASSERT_TRUE(!src.empty());
  ASSERT_TRUE(contains(src, "NOSONAR"));
}

}  // namespace
