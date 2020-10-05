//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma managed(push, off)
#include "td/telegram/Client.h"
#pragma managed(pop)

#include "td/telegram/TdDotNetApi.h"

#include "td/utils/port/CxCli.h"

#pragma managed(push, off)
#include <cstdint>
#pragma managed(pop)

namespace Telegram {
namespace Td {

using namespace CxCli;

/// <summary>
/// Interface for handler for results of queries to TDLib and incoming updates from TDLib.
/// </summary>
public interface class ClientResultHandler {
  /// <summary>
  /// Callback called on result of query to TDLib or incoming update from TDLib.
  /// </summary>
  /// <param name="object">Result of query or update of type Telegram.Td.Api.Update about new events.</param>
  void OnResult(Api::BaseObject^ object);
};

/// <summary>
/// Main class for interaction with the TDLib.
/// </summary>
public ref class Client sealed {
public:
  /// <summary>
  /// Sends a request to the TDLib.
  /// </summary>
  /// <param name="function">Object representing a query to the TDLib.</param>
  /// <param name="handler">Result handler with OnResult method which will be called with result
  /// of the query or with Telegram.Td.Api.Error as parameter. If it is null, nothing will be called.</param>
  /// <exception cref="NullReferenceException">Thrown when query is null.</exception>
  void Send(Api::Function^ function, ClientResultHandler^ handler) {
    if (function == nullptr) {
      throw REF_NEW NullReferenceException("Function can't be null");
    }

    std::uint64_t queryId = Increment(currentId);
    if (handler != nullptr) {
      handlers[queryId] = handler;
    }
    td::Client::Request request;
    request.id = queryId;
    request.function = td::td_api::move_object_as<td::td_api::Function>(ToUnmanaged(function)->get_object_ptr());
    client->send(std::move(request));
  }

  /// <summary>
  /// Synchronously executes a TDLib request. Only a few marked accordingly requests can be executed synchronously.
  /// </summary>
  /// <param name="function">Object representing a query to the TDLib.</param>
  /// <returns>Returns request result.</returns>
  /// <exception cref="NullReferenceException">Thrown when query is null.</exception>
  static Api::BaseObject^ Execute(Api::Function^ function) {
    if (function == nullptr) {
      throw REF_NEW NullReferenceException("Function can't be null");
    }

    td::Client::Request request;
    request.id = 0;
    request.function = td::td_api::move_object_as<td::td_api::Function>(ToUnmanaged(function)->get_object_ptr());
    return Api::FromUnmanaged(*td::Client::execute(std::move(request)).object);
  }

  /// <summary>
  /// Launches a cycle which will fetch all results of queries to TDLib and incoming updates from TDLib.
  /// Must be called once on a separate dedicated thread, on which all updates and query results will be handled.
  /// Returns only when TDLib instance is closed.
  /// </summary>
  void Run() {
    while (true) {
      auto response = client->receive(10.0);
      if (response.object != nullptr) {
        ProcessResult(response.id, Api::FromUnmanaged(*response.object));

        if (response.object->get_id() == td::td_api::updateAuthorizationState::ID &&
            static_cast<td::td_api::updateAuthorizationState &>(*response.object).authorization_state_->get_id() ==
            td::td_api::authorizationStateClosed::ID) {
          break;
        }
      }
    }
  }

  /// <summary>
  /// Creates new Client.
  /// </summary>
  /// <param name="updateHandler">Handler for incoming updates.</param>
  /// <returns>Returns created Client.</returns>
  static Client^ Create(ClientResultHandler^ updateHandler) {
    return REF_NEW Client(updateHandler);
  }

private:
  Client(ClientResultHandler^ updateHandler) {
    client = new td::Client();
    handlers[0] = updateHandler;
  }

  ~Client() {
    delete client;
  }

  std::int64_t currentId = 0;
  ConcurrentDictionary<std::uint64_t, ClientResultHandler^> handlers;
  td::Client *client = nullptr;

  void ProcessResult(std::uint64_t id, Api::BaseObject^ object) {
    ClientResultHandler^ handler;
    // update handler stays forever
    if (id == 0 ? handlers.TryGetValue(id, handler) : handlers.TryRemove(id, handler)) {
      // TODO try/catch
      handler->OnResult(object);
    }
  }
};

}  // namespace Td
}  // namespace Telegram
