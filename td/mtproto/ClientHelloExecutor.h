#pragma once

#include "td/mtproto/ClientHelloOp.h"

#include "td/utils/Status.h"

namespace td {
namespace mtproto {

class ClientHelloExecutor {
 public:
  static Result<string> execute(const vector<ClientHelloOp> &ops, Slice domain, Slice secret, int32 unix_time,
                                size_t grease_value_count);
};

}  // namespace mtproto
}  // namespace td
