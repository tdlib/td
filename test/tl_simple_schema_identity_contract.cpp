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

class DuplicateTypeIdSchemaFixture {
 public:
  DuplicateTypeIdSchemaFixture() {
    auto first_type_owner = std::make_unique<td::tl::tl_type>(0x710001, "dup.Type");
    auto second_type_owner = std::make_unique<td::tl::tl_type>(0x710001, "dup.Type");
    first_type_owner->constructors_num = 1;
    second_type_owner->constructors_num = 0;

    auto constructor_owner = std::make_unique<td::tl::tl_combinator>(0x710101, "dupType");
    constructor_owner->type_id = first_type_owner->id;
    first_type_owner->add_constructor(constructor_owner.release());

    config_.add_type(first_type_owner.release());
    config_.add_type(second_type_owner.release());
  }

  td::tl::tl_config config_;
};

}  // namespace

TEST(TlSimpleSchemaIdentityContract, duplicate_type_ids_reuse_same_schema_custom_type_instance) {
  DuplicateTypeIdSchemaFixture fixture;

  td::tl::simple::Schema schema(fixture.config_);

  ASSERT_EQ(2u, schema.custom_types.size());
  ASSERT_EQ(schema.custom_types[0], schema.custom_types[1]);
}

TEST(TlSimpleSchemaIdentityContract, duplicate_type_ids_keep_constructor_owned_by_reused_custom_type) {
  DuplicateTypeIdSchemaFixture fixture;

  td::tl::simple::Schema schema(fixture.config_);

  ASSERT_EQ(2u, schema.custom_types.size());
  ASSERT_EQ(1u, schema.custom_types[0]->constructors.size());
  ASSERT_EQ(schema.custom_types[0], schema.custom_types[0]->constructors[0]->type);
}