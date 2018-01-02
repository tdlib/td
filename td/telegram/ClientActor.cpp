//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ClientActor.h"

#include "td/telegram/td_api.h"

#include "td/telegram/net/NetQueryCounter.h"
#include "td/telegram/Td.h"

namespace td {
ClientActor::ClientActor(unique_ptr<TdCallback> callback) {
  td_ = create_actor<Td>("Td", std::move(callback));
}

void ClientActor::request(uint64 id, td_api::object_ptr<td_api::Function> request) {
  send_closure(td_, &Td::request, id, std::move(request));
}

ClientActor::~ClientActor() = default;

ClientActor::ClientActor(ClientActor &&other) = default;

ClientActor &ClientActor::operator=(ClientActor &&other) = default;

td_api::object_ptr<td_api::Object> ClientActor::execute(td_api::object_ptr<td_api::Function> request) {
  return Td::static_request(std::move(request));
}

uint64 get_pending_network_query_count() {
  return NetQueryCounter::get_count();
}

}  // namespace td
