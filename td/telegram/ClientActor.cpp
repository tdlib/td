//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ClientActor.h"

#include "td/telegram/td_api.h"

#include "td/telegram/net/NetQueryCounter.h"
#include "td/telegram/net/NetQueryStats.h"
#include "td/telegram/Td.h"

namespace td {

ClientActor::ClientActor(unique_ptr<TdCallback> callback, Options options) {
  Td::Options td_options;
  td_options.net_query_stats = options.net_query_stats;
  td_ = create_actor<Td>("Td", std::move(callback), std::move(td_options));
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

std::shared_ptr<NetQueryStats> create_net_query_stats() {
  return std::make_shared<NetQueryStats>();
}

void dump_pending_network_queries(NetQueryStats &stats) {
  stats.dump_pending_network_queries();
}

uint64 get_pending_network_query_count(NetQueryStats &stats) {
  return stats.get_count();
}

}  // namespace td
