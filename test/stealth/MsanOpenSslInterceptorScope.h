// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs

#pragma once

#if defined(__has_feature)
#if __has_feature(memory_sanitizer)
#include <sanitizer/msan_interface.h>
#define TD_TEST_MSAN_ACTIVE 1
#endif
#endif
#if defined(__SANITIZE_MEMORY__)
#include <sanitizer/msan_interface.h>
#define TD_TEST_MSAN_ACTIVE 1
#endif
#ifndef TD_TEST_MSAN_ACTIVE
#define TD_TEST_MSAN_ACTIVE 0
#endif

namespace td::mtproto::test {

class MsanOpenSslInterceptorScope final {
 public:
  MsanOpenSslInterceptorScope() {
#if TD_TEST_MSAN_ACTIVE
    __msan_scoped_disable_interceptor_checks();
#endif
  }

  ~MsanOpenSslInterceptorScope() {
#if TD_TEST_MSAN_ACTIVE
    __msan_scoped_enable_interceptor_checks();
#endif
  }
};

}  // namespace td::mtproto::test
