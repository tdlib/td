// Copyright 2026 tdlib-obf authors. All rights reserved.
// Distributed under the Boost Software License, Version 1.0.
//
// Sonar BLOCKER tl-parser-memory adversarial tests.
// These verify that old bad patterns (bare TL_FAIL without RAII cleanup,
// missing NOSONAR on false-positive sites) are absent from the source.

#include "td/utils/tests.h"

#include <cstdio>
#include <string>

#ifndef TELEMT_TEST_REPO_ROOT
#define TELEMT_TEST_REPO_ROOT ""
#endif

namespace {

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

static std::string normalize_ws(const std::string &s) {
  std::string out;
  bool prev = false;
  for (char c : s) {
    char d = (c == '\n' || c == '\t') ? ' ' : c;
    if (d == ' ' && prev) {
      continue;
    }
    out.push_back(d);
    prev = (d == ' ');
  }
  return out;
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

// ---------------------------------------------------------------------------
// tl_type_check: old pattern — bare return without tree_clear_var_value(v)
// after check_constructors_equal inside the nested loop.
// ---------------------------------------------------------------------------
TEST(SonarBlockerTlParserMemoryAdversarial, tl_type_check_no_bare_check_constructors_equal_without_clear) {
  auto src = read_source_file("td/generate/tl-parser/tl-parser.c");
  ASSERT_TRUE(!src.empty());
  auto tl_type_check = extract_region(src, "void tl_type_check(struct tl_type *t)", "int tl_add_constructor");
  ASSERT_TRUE(!tl_type_check.empty());
  // After the fix, tree_clear_var_value(v) MUST appear after the
  // check_constructors_equal call in tl_type_check.  We verify that the
  // source does NOT contain the old naked pattern where the call appears
  // alone inside the inner for-loop body with NO subsequent free.
  // The positive test (contract) already asserts tree_clear_var_value(v)
  // exists; here we verify through a different angle that the var_value
  // tree is NOT left without a clear in tl_type_check.
  auto norm = normalize_ws(tl_type_check);
  // Find check_constructors_equal: the ONLY call site inside tl_type_check
  // is directly followed by the conditional + tree_clear_var_value.
  // Locate first occurrence near "check_constructors_equal".
  size_t call_pos = norm.find("check_constructors_equal(t->constructors[i]->right");
  ASSERT_TRUE(call_pos != std::string::npos);
  // Within 200 chars after that call, tree_clear_var_value must appear.
  auto region = norm.substr(call_pos, 250);
  ASSERT_TRUE(region.find("tree_clear_var_value") != std::string::npos);
}

// ---------------------------------------------------------------------------
// tl_parse_partial_comb_app_decl: verify OLD PATTERN of bare TL_FAIL without
// L/R/X frees is gone (the error return inside the loop).
// ---------------------------------------------------------------------------
TEST(SonarBlockerTlParserMemoryAdversarial, tl_parse_partial_comb_app_decl_no_bare_TL_FAIL_after_z_change_error) {
  auto src = read_source_file("td/generate/tl-parser/tl-parser.c");
  ASSERT_TRUE(!src.empty());
  auto region_src = extract_region(src, "int tl_parse_partial_comb_app_decl(struct tree *T, int fun)",
                                   "int tl_parse_partial_app_decl(struct tree *T, int fun)");
  ASSERT_TRUE(!region_src.empty());
  auto norm = normalize_ws(region_src);
  // After "tl_tree_change_is_error(z_change)", within 150 chars, must
  // contain tl_free_combinator_tree before TL_FAIL.
  size_t z_error_pos = norm.find("tl_tree_change_is_error(z_change)");
  ASSERT_TRUE(z_error_pos != std::string::npos);
  auto region = norm.substr(z_error_pos, 200);
  auto free_pos = region.find("tl_free_combinator_tree");
  auto fail_pos = region.find("TL_FAIL");
  ASSERT_TRUE(free_pos != std::string::npos);
  ASSERT_TRUE(fail_pos != std::string::npos);
  ASSERT_TRUE(free_pos < fail_pos);
}

// ---------------------------------------------------------------------------
// Verify that tl_parse_partial_comb_app_decl K-null error path frees memory.
// ---------------------------------------------------------------------------
TEST(SonarBlockerTlParserMemoryAdversarial, tl_parse_partial_comb_app_decl_K_null_path_frees_before_fail) {
  auto src = read_source_file("td/generate/tl-parser/tl-parser.c");
  ASSERT_TRUE(!src.empty());
  auto region_src = extract_region(src, "int tl_parse_partial_comb_app_decl(struct tree *T, int fun)",
                                   "int tl_parse_partial_app_decl(struct tree *T, int fun)");
  ASSERT_TRUE(!region_src.empty());
  auto norm = normalize_ws(region_src);
  // After "not enougth variables" error, must have frees before TL_FAIL.
  size_t kerr_pos = norm.find("not enougth variables");
  ASSERT_TRUE(kerr_pos != std::string::npos);
  auto region = norm.substr(kerr_pos, 300);
  auto free_pos = region.find("tl_free_combinator_tree(L)");
  auto fail_pos = region.find("TL_FAIL");
  ASSERT_TRUE(free_pos != std::string::npos);
  ASSERT_TRUE(fail_pos != std::string::npos);
  ASSERT_TRUE(free_pos < fail_pos);
}

// ---------------------------------------------------------------------------
// Verify tl_parse_opt_args R leak is fixed (no bare TL_FAIL while R live).
// ---------------------------------------------------------------------------
TEST(SonarBlockerTlParserMemoryAdversarial, tl_parse_opt_args_variable_name_error_frees_R) {
  auto src = read_source_file("td/generate/tl-parser/tl-parser.c");
  ASSERT_TRUE(!src.empty());
  auto region_src = extract_region(src, "struct tl_combinator_tree *tl_parse_opt_args(struct tree *T)",
                                   "struct tl_combinator_tree *tl_parse_args134(struct tree *T)");
  ASSERT_TRUE(!region_src.empty());
  auto norm = normalize_ws(region_src);
  // "Optargs can be only of type # or Type" is the first TL_FAIL after R is set.
  size_t err_pos = norm.find("Optargs can be only of type");
  ASSERT_TRUE(err_pos != std::string::npos);
  auto region = norm.substr(err_pos, 200);
  auto free_pos = region.find("tl_free_combinator_tree(R)");
  auto fail_pos = region.find("TL_FAIL");
  ASSERT_TRUE(free_pos != std::string::npos);
  ASSERT_TRUE(fail_pos != std::string::npos);
  ASSERT_TRUE(free_pos < fail_pos);
}

// ---------------------------------------------------------------------------
// Verify tl_parse_result_type does NOT return with a bare TL_FAIL
// after tl_finish_subtree(L) fails (L leaked in old code).
// ---------------------------------------------------------------------------
TEST(SonarBlockerTlParserMemoryAdversarial, tl_parse_result_type_finish_subtree_fail_frees_L) {
  auto src = read_source_file("td/generate/tl-parser/tl-parser.c");
  ASSERT_TRUE(!src.empty());
  auto region_src = extract_region(src, "struct tl_combinator_tree *tl_parse_result_type(struct tree *T)", "int __ok;");
  ASSERT_TRUE(!region_src.empty());
  auto norm = normalize_ws(region_src);
  // "tl_finish_subtree(L)" is called in tl_parse_result_type; on failure,
  // L must be freed before TL_FAIL.
  size_t fs_pos = norm.find("tl_finish_subtree(L)");
  ASSERT_TRUE(fs_pos != std::string::npos);
  // Look at the if(!...) pattern that wraps the failure:
  // "if (!tl_finish_subtree(L))" should have a free+TL_FAIL sequence.
  size_t if_fail_pos = norm.find("if (!tl_finish_subtree(L))");
  ASSERT_TRUE(if_fail_pos != std::string::npos);
  auto region = norm.substr(if_fail_pos, 200);
  auto free_pos = region.find("tl_free_combinator_tree(L)");
  auto fail_pos = region.find("TL_FAIL");
  ASSERT_TRUE(free_pos != std::string::npos);
  ASSERT_TRUE(fail_pos != std::string::npos);
  ASSERT_TRUE(free_pos < fail_pos);
}

// ---------------------------------------------------------------------------
// Verify RwMutex.h carries NOSONAR comments.
// ---------------------------------------------------------------------------
TEST(SonarBlockerTlParserMemoryAdversarial, rw_mutex_h_has_nosonar_not_bare_unsafe_implementations) {
  auto src = read_source_file("tdutils/td/utils/port/RwMutex.h");
  ASSERT_TRUE(!src.empty());
  ASSERT_TRUE(src.find("NOSONAR") != std::string::npos);
}

}  // namespace
