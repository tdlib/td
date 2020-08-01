//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

///\file

#include "td/telegram/TdCallback.h"

#include "td/telegram/td_api.h"
#include "td/telegram/td_api.hpp"

#include "td/actor/actor.h"

#include "td/utils/common.h"

#include <memory>

namespace td {

class NetQueryStats;
class Td;

/**
 * This is a low-level Actor interface for interaction with TDLib. The interface is a lot more flexible than
 * the Client interface, however, for most usages the Client interface should be sufficient.
 */
class ClientActor : public Actor {
 public:
  struct Options {
    std::shared_ptr<NetQueryStats> net_query_stats;

    Options() {
    }
  };

  /**
   * Creates a ClientActor using the specified callback.
   * \param[in] callback Callback for outgoing notifications from TDLib.
   */
  explicit ClientActor(unique_ptr<TdCallback> callback, Options options = {});

  /**
   * Sends one request to TDLib. The answer will be received via callback.
   * \param[in] id Request identifier, must be positive.
   * \param[in] request The request.
   */
  void request(uint64 id, td_api::object_ptr<td_api::Function> request);

  /**
   * Synchronously executes a TDLib request. Only a few requests can be executed synchronously.
   * May be called from any thread.
   * \param[in] request Request to the TDLib.
   * \return The request response.
   */
  static td_api::object_ptr<td_api::Object> execute(td_api::object_ptr<td_api::Function> request);

  /**
   * Destroys the ClientActor and the TDLib instance.
   */
  ~ClientActor();

  /**
   * Move constructor.
   */
  ClientActor(ClientActor &&other);

  /**
   * Move assignment operator.
   */
  ClientActor &operator=(ClientActor &&other);

  ClientActor(const ClientActor &other) = delete;
  ClientActor &operator=(const ClientActor &other) = delete;

 private:
  ActorOwn<Td> td_;
};

std::shared_ptr<NetQueryStats> create_net_query_stats();

/**
 * Dumps information about all pending network queries to the internal TDLib log.
 * This is useful for library debugging.
 */
void dump_pending_network_queries(NetQueryStats &stats);

/**
 * Returns the current number of pending network queries. Useful for library debugging.
 * \return Number of currently pending network queries.
 */
uint64 get_pending_network_query_count(NetQueryStats &stats);

}  // namespace td
