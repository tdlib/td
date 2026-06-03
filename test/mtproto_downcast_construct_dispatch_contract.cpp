// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/mtproto/mtproto_api.hpp"

#include "td/utils/tests.h"

#include <type_traits>

namespace {

template <class Base, class Derived>
void assert_dispatch_preserves_object_overload_contract() {
  Derived derived;
  Base &base = derived;

  bool called = false;
  bool ok = td::mtproto_api::downcast_call(base, [&](auto &value) {
    using ValueType = std::decay_t<decltype(value)>;
    ASSERT_TRUE((std::is_same_v<ValueType, Derived>));
    called = true;
  });

  ASSERT_TRUE(ok);
  ASSERT_TRUE(called);
}

}  // namespace

TEST(MtprotoDowncastConstructDispatchContract, constructor_only_dispatch_maps_to_target_type) {
  bool called = false;
  bool ok = td::mtproto_api::downcast_construct_call(
      td::mtproto_api::msgs_ack::ID, static_cast<td::mtproto_api::Object *>(nullptr), [&](auto &value) {
        using ValueType = td::mtproto_api::downcast_call_target_t<decltype(value)>;
        ASSERT_TRUE((std::is_same_v<ValueType, td::mtproto_api::msgs_ack>));
        called = true;
      });

  ASSERT_TRUE(ok);
  ASSERT_TRUE(called);
}

TEST(MtprotoDowncastConstructDispatchContract, preserves_existing_object_based_downcast_call_contract) {
  assert_dispatch_preserves_object_overload_contract<td::mtproto_api::Object, td::mtproto_api::destroy_auth_key_none>();
}
