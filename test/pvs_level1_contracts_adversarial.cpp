// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "test/stealth/SourceContractFileReader.h"

#include "td/utils/common.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Status.h"
#include "td/utils/tests.h"

#include <limits>
#include <random>

namespace {

td::string load_repo_text(td::Slice relative_path) {
  return td::mtproto::test::read_repo_text_file(relative_path);
}

}  // namespace

TEST(PvsLevel1Contracts, source_patterns_are_hardened) {
  const auto slice_h = load_repo_text("tdutils/td/utils/Slice.h");
  const auto status_h = load_repo_text("tdutils/td/utils/Status.h");
  const auto misc_h = load_repo_text("tdutils/td/utils/misc.h");

  ASSERT_EQ(td::string::npos, slice_h.find("const_cast<char *>(\"\")"));
  ASSERT_EQ(td::string::npos, status_h.find("std::unique_ptr<char[], Deleter>(new char[size])"));
  ASSERT_EQ(td::string::npos, status_h.find("CHECK(error_code == tmp.error_code)"));
  ASSERT_EQ(td::string::npos, misc_h.find("\n  to_lower_inplace(result);\n"));
}

TEST(PvsLevel1Contracts, source_patterns_level1_warning_slice) {
  const auto language_pack_cpp = load_repo_text("td/telegram/LanguagePackManager.cpp");
  const auto link_manager_cpp = load_repo_text("td/telegram/LinkManager.cpp");
  const auto message_entity_cpp = load_repo_text("td/telegram/MessageEntity.cpp");
  const auto telegram_misc_cpp = load_repo_text("td/telegram/misc.cpp");
  const auto http_date_cpp = load_repo_text("tdutils/td/utils/HttpDate.cpp");
  const auto td_db_cpp = load_repo_text("td/telegram/TdDb.cpp");
  const auto path_cpp = load_repo_text("tdutils/td/utils/port/path.cpp");
  const auto concurrent_hash_table_h = load_repo_text("tdutils/td/utils/ConcurrentHashTable.h");
  const auto hazard_pointers_h = load_repo_text("tdutils/td/utils/HazardPointers.h");

  ASSERT_EQ(td::string::npos, language_pack_cpp.find("to_lower_inplace(result->lang_code_);"));
  ASSERT_EQ(td::string::npos, language_pack_cpp.find("to_lower_inplace(difference->lang_code_);"));
  ASSERT_EQ(td::string::npos, language_pack_cpp.find("to_lower_inplace(language->lang_code_);"));
  ASSERT_EQ(td::string::npos, link_manager_cpp.find("to_lower_inplace(host);"));
  ASSERT_EQ(td::string::npos, message_entity_cpp.find("to_lower_inplace(domain_lower);"));
  ASSERT_EQ(td::string::npos, telegram_misc_cpp.find("to_lower_inplace(str);"));
  ASSERT_EQ(td::string::npos, http_date_cpp.find("to_lower_inplace(month_name);"));

  ASSERT_EQ(td::string::npos, path_cpp.find("mkpath(input_dir, 0750)"));
  ASSERT_EQ(td::string::npos, td_db_cpp.find("mkpath(dir, 0750)"));

  ASSERT_EQ(td::string::npos, concurrent_hash_table_h.find("\n    n = 1;\n"));

  ASSERT_EQ(
      td::string::npos,
      hazard_pointers_h.find("char pad[TD_CONCURRENCY_PAD - sizeof(std::array<std::atomic<T *>, MaxPointersN>)];"));
  ASSERT_EQ(td::string::npos, hazard_pointers_h.find(
                                  "char pad2[TD_CONCURRENCY_PAD - sizeof(std::vector<std::unique_ptr<T, Deleter>>)];"));
  ASSERT_EQ(td::string::npos,
            hazard_pointers_h.find("char pad2[TD_CONCURRENCY_PAD - sizeof(std::vector<ThreadData>)];"));
}

TEST(PvsLevel1Contracts, mutable_slice_default_contract) {
  td::MutableSlice empty;
  ASSERT_EQ(0u, empty.size());
  ASSERT_TRUE(empty.empty());
  ASSERT_TRUE(empty.data() != nullptr);
  ASSERT_EQ(empty.begin(), empty.end());
  ASSERT_EQ('\0', empty.data()[0]);
}

TEST(PvsLevel1Contracts, status_error_code_clamp_contract) {
  constexpr int kMinErrorCode = -(1 << 22) + 1;
  constexpr int kMaxErrorCode = (1 << 22) - 1;

  const auto low = td::Status::Error(std::numeric_limits<int>::min(), "low");
  ASSERT_TRUE(low.is_error());
  ASSERT_EQ(kMinErrorCode, low.code());

  const auto high = td::Status::Error(std::numeric_limits<int>::max(), "high");
  ASSERT_TRUE(high.is_error());
  ASSERT_EQ(kMaxErrorCode, high.code());

  const auto exact = td::Status::Error(42, "ok");
  ASSERT_TRUE(exact.is_error());
  ASSERT_EQ(42, exact.code());
}

TEST(PvsLevel1Adversarial, status_error_code_light_fuzz_clamp_invariants) {
  constexpr int kMinErrorCode = -(1 << 22) + 1;
  constexpr int kMaxErrorCode = (1 << 22) - 1;
  constexpr int kIterations = 20000;

  std::mt19937 rng(0x57A7E57u);
  std::uniform_int_distribution<int> dist(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());

  for (int i = 0; i < kIterations; i++) {
    const int input = dist(rng);
    const auto status = td::Status::Error(input, "fuzz");

    ASSERT_TRUE(status.is_error());
    ASSERT_TRUE(status.code() >= kMinErrorCode);
    ASSERT_TRUE(status.code() <= kMaxErrorCode);
  }
}

TEST(PvsLevel1Contracts, to_lower_ascii_contract) {
  td::string value = "AbC-09_Z z";
  auto lowered_view = td::to_lower_inplace(value);

  ASSERT_EQ(value.data(), lowered_view.data());
  ASSERT_EQ(value.size(), lowered_view.size());
  ASSERT_EQ("abc-09_z z", value);

  const auto lowered_copy = td::to_lower("HELLO-World_42");
  ASSERT_EQ("hello-world_42", lowered_copy);
}

TEST(PvsLevel1Adversarial, to_lower_inplace_binary_light_fuzz) {
  constexpr int kIterations = 15000;
  std::mt19937 rng(0xC001D00Du);
  std::uniform_int_distribution<int> len_dist(0, 128);
  std::uniform_int_distribution<int> byte_dist(0, 255);

  for (int i = 0; i < kIterations; i++) {
    td::string data;
    const int len = len_dist(rng);
    data.resize(static_cast<size_t>(len));
    for (int j = 0; j < len; j++) {
      data[static_cast<size_t>(j)] = static_cast<char>(byte_dist(rng));
    }

    const auto before = data;
    auto lowered = td::to_lower_inplace(data);

    ASSERT_EQ(data.data(), lowered.data());
    ASSERT_EQ(data.size(), lowered.size());

    for (size_t pos = 0; pos < before.size(); pos++) {
      ASSERT_EQ(td::to_lower(before[pos]), data[pos]);
    }
  }
}

TEST(PvsLevel1Contracts, logging_strip_predicate_contract_default_levels) {
  constexpr int kBuildStrip = STRIP_LOG;
  ASSERT_EQ(VERBOSITY_NAME(DEBUG), kBuildStrip);

  ASSERT_FALSE((td::detail::IsLogStripped<VERBOSITY_NAME(DEBUG), kBuildStrip>::value));
  ASSERT_FALSE((td::detail::IsLogStripped<VERBOSITY_NAME(INFO), kBuildStrip>::value));
  ASSERT_FALSE((td::detail::IsLogStripped<VERBOSITY_NAME(WARNING), kBuildStrip>::value));
  ASSERT_FALSE((td::detail::IsLogStripped<VERBOSITY_NAME(ERROR), kBuildStrip>::value));
  ASSERT_FALSE((td::detail::IsLogStripped<VERBOSITY_NAME(FATAL), kBuildStrip>::value));
  ASSERT_TRUE((td::detail::IsLogStripped<VERBOSITY_NAME(NEVER), kBuildStrip>::value));
}

TEST(PvsLevel1Contracts, logging_macro_source_contract_no_identical_integral_constant_compare) {
  const auto logging_h = load_repo_text("tdutils/td/utils/logging.h");

  ASSERT_EQ(td::string::npos, logging_h.find("integral_constant<int, VERBOSITY_NAME(strip_level)>() >"));
  ASSERT_TRUE(logging_h.find("IsLogStripped<VERBOSITY_NAME(strip_level), STRIP_LOG>::value") != td::string::npos);
}

TEST(PvsLevel1Adversarial, logging_strip_predicate_matches_numeric_relation_for_full_level_grid) {
  constexpr int kLevels[] = {VERBOSITY_NAME(PLAIN),   VERBOSITY_NAME(FATAL), VERBOSITY_NAME(ERROR),
                             VERBOSITY_NAME(WARNING), VERBOSITY_NAME(INFO),  VERBOSITY_NAME(DEBUG),
                             VERBOSITY_NAME(NEVER)};

  for (int strip_level : kLevels) {
    for (int build_strip_level : kLevels) {
      const bool expected = strip_level > build_strip_level;
      const bool observed = td::detail::is_log_stripped(strip_level, build_strip_level);
      ASSERT_EQ(expected, observed);
    }
  }
}

TEST(PvsLevel1Adversarial, logging_strip_predicate_light_fuzz_matches_arithmetic_reference) {
  constexpr int kIterations = 30000;
  std::mt19937 rng(0x9D219A73u);
  std::uniform_int_distribution<int> dist(-2048, 2048);

  for (int i = 0; i < kIterations; i++) {
    const int strip_level = dist(rng);
    const int build_strip_level = dist(rng);
    const bool expected = strip_level > build_strip_level;
    const bool observed = td::detail::is_log_stripped(strip_level, build_strip_level);
    ASSERT_EQ(expected, observed);
  }
}

TEST(PvsLevel1Adversarial, logging_strip_predicate_stress_is_deterministic) {
  constexpr int kIters = 200000;
  td::int32 checksum = 0;

  for (int i = 0; i < kIters; i++) {
    const int strip_level = (i % 11) - 5;
    const int build_strip_level = ((i * 7) % 13) - 6;
    if (td::detail::is_log_stripped(strip_level, build_strip_level)) {
      checksum++;
    }
  }

  ASSERT_EQ(92308, checksum);
}
