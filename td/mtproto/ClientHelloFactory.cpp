#include "td/mtproto/ClientHelloFactory.h"

#include "td/mtproto/BrowserProfileSelector.h"
#include "td/mtproto/ClientHelloExecutor.h"
#include "td/mtproto/ClientHelloOpMapper.h"

namespace td {
namespace mtproto {

string ClientHelloFactory::build_default(Slice domain, Slice secret, int32 unix_time) {
  auto profile = BrowserProfileSelector::get_default_firefox();
  auto ops = ClientHelloOpMapper::map(profile);
  return ClientHelloExecutor::execute(ops, domain, secret, unix_time, profile.grease.value_count).move_as_ok();
}

}  // namespace mtproto
}  // namespace td
