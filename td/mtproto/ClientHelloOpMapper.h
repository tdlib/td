#pragma once

#include "td/mtproto/BrowserProfile.h"

namespace td {
namespace mtproto {

class ClientHelloOpMapper {
 public:
  static vector<ClientHelloOp> map(const BrowserProfile &profile);
};

}  // namespace mtproto
}  // namespace td
