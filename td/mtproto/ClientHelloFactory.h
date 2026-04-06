#pragma once

#include "td/utils/Slice.h"
#include "td/utils/common.h"

namespace td {
namespace mtproto {

class ClientHelloFactory {
 public:
  static string build_default(Slice domain, Slice secret, int32 unix_time);
};

}  // namespace mtproto
}  // namespace td
