#pragma once

#include "td/mtproto/BrowserProfile.h"

namespace td {
namespace mtproto {

class BrowserProfileSelector {
 public:
  static BrowserProfile get_default_chrome();
  static BrowserProfile get_default_firefox();
};

}  // namespace mtproto
}  // namespace td
