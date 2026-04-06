#include "td/mtproto/BrowserProfileSelector.h"

namespace td {
namespace mtproto {

BrowserProfile BrowserProfileSelector::get_default_chrome() {
#if TD_DARWIN
  return get_chrome_darwin_profile();
#else
  return get_chrome_profile();
#endif
}

BrowserProfile BrowserProfileSelector::get_default_firefox() {
  return get_firefox_profile();

}

}  // namespace mtproto
}  // namespace td
