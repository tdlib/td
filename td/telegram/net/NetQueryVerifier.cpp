//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/net/NetQueryVerifier.h"

#include "td/telegram/Global.h"
#include "td/telegram/net/NetQueryDispatcher.h"
#include "td/telegram/Td.h"

#include "td/utils/common.h"

namespace td {

void NetQueryVerifier::verify(NetQueryPtr query, string nonce) {
  CHECK(query->is_ready());
  CHECK(query->is_error());

  auto query_id = next_query_id_++;
  queries_.emplace(query_id, std::make_pair(std::move(query), nonce));

  send_closure(G()->td(), &Td::send_update,
               td_api::make_object<td_api::updateApplicationVerificationRequired>(query_id, nonce));
}

void NetQueryVerifier::tear_down() {
  for (auto &it : queries_) {
    it.second.first->set_error(Global::request_aborted_error());
    G()->net_query_dispatcher().dispatch(std::move(it.second.first));
  }
  queries_.clear();
  parent_.reset();
}

}  // namespace td
