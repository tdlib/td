// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "td/tl/tl_config.h"
#include "td/tl/tl_simple.h"

#include <memory>

namespace {

class DuplicateFunctionIdSchemaFixture {
 public:
  DuplicateFunctionIdSchemaFixture() {
    auto result_type_owner = std::make_unique<td::tl::tl_type>(0x720001, "dup.Result");
    result_type_owner->constructors_num = 0;

    auto first_function_owner = std::make_unique<td::tl::tl_combinator>(0x720101, "getDupResult");
    first_function_owner->type_id = result_type_owner->id;

    auto second_function_owner = std::make_unique<td::tl::tl_combinator>(0x720101, "getDupResult");
    second_function_owner->type_id = result_type_owner->id;

    config_.add_type(result_type_owner.release());
    config_.add_function(first_function_owner.release());
    config_.add_function(second_function_owner.release());
  }

  td::tl::tl_config config_;
};

}  // namespace

TEST(TlSimpleSchemaIdentityAdversarial, duplicate_function_ids_cannot_split_schema_function_identity) {
  DuplicateFunctionIdSchemaFixture fixture;

  td::tl::simple::Schema schema(fixture.config_);

  ASSERT_EQ(2u, schema.functions.size());
  ASSERT_EQ(schema.functions[0], schema.functions[1]);
}