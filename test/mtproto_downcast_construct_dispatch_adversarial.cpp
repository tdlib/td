// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/mtproto/mtproto_api.hpp"

#include "td/utils/tests.h"

#include <cstdint>

TEST(MtprotoDowncastConstructDispatchAdversarial, rejects_unknown_constructor_fail_closed) {
  bool called = false;
  bool ok = td::mtproto_api::downcast_construct_call(static_cast<std::int32_t>(0x7f00a11c),
                                                     static_cast<td::mtproto_api::Object *>(nullptr),
                                                     [&](auto &) { called = true; });

  ASSERT_FALSE(ok);
  ASSERT_FALSE(called);
}

TEST(MtprotoDowncastConstructDispatchAdversarial, rejects_hostile_high_range_constructor_ids) {
  constexpr std::int32_t kBase = static_cast<std::int32_t>(0x7f000000);
  for (std::int32_t suffix = 1; suffix <= 4096; suffix++) {
    bool called = false;
    bool ok = td::mtproto_api::downcast_construct_call(static_cast<std::int32_t>(kBase | suffix),
                                                       static_cast<td::mtproto_api::Object *>(nullptr),
                                                       [&](auto &) { called = true; });

    ASSERT_FALSE(ok);
    ASSERT_FALSE(called);
  }
}
