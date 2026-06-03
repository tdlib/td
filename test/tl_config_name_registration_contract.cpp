// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"
#include "tdtl/td/tl/tl_config.h"

#include <memory>
#include <string>

TEST(TlConfigNameRegistrationContract, later_type_registration_replaces_same_name_lookup) {
  td::tl::tl_config config;

  auto first = std::make_unique<td::tl::tl_type>(1, "dup.Type");
  auto second = std::make_unique<td::tl::tl_type>(2, "dup.Type");
  auto *first_ptr = first.get();
  auto *second_ptr = second.get();

  config.add_type(first.release());
  config.add_type(second.release());

  ASSERT_EQ(first_ptr, config.get_type(1));
  ASSERT_EQ(second_ptr, config.get_type(2));
  ASSERT_EQ(second_ptr, config.get_type(std::string("dup.Type")));
}

TEST(TlConfigNameRegistrationContract, type_lookup_distinguishes_embedded_nul_names) {
  td::tl::tl_config config;

  const std::string first_name("dup\0alpha", 9);
  const std::string second_name("dup\0beta", 8);
  auto first = std::make_unique<td::tl::tl_type>(10, first_name);
  auto second = std::make_unique<td::tl::tl_type>(11, second_name);
  auto *first_ptr = first.get();
  auto *second_ptr = second.get();

  config.add_type(first.release());
  config.add_type(second.release());

  ASSERT_EQ(first_ptr, config.get_type(first_name));
  ASSERT_EQ(second_ptr, config.get_type(second_name));
}

TEST(TlConfigNameRegistrationContract, later_function_registration_replaces_same_name_lookup) {
  td::tl::tl_config config;

  auto first = std::make_unique<td::tl::tl_combinator>(101, "dup.function");
  auto second = std::make_unique<td::tl::tl_combinator>(102, "dup.function");
  auto *first_ptr = first.get();
  auto *second_ptr = second.get();

  config.add_function(first.release());
  config.add_function(second.release());

  ASSERT_EQ(first_ptr, config.get_function(101));
  ASSERT_EQ(second_ptr, config.get_function(102));
  ASSERT_EQ(second_ptr, config.get_function(std::string("dup.function")));
}
