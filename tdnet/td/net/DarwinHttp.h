#pragma once

#include "td/actor/PromiseFuture.h"

#include "td/utils/buffer.h"

namespace td {
class DarwinHttp {
 public:
  static void get(CSlice url, Promise<BufferSlice> promise);
  static void post(CSlice url, Slice data, Promise<BufferSlice> promise);
};
}  // namespace td
