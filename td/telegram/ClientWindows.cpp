//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/TdWindowsApi.h"

#include "td/telegram/Client.h"

#include "td/utils/port/CxCli.h"

namespace TdWindows {
using namespace CxCli;

public interface class ClientResultHandler {
  void OnResult(BaseObject^ object);
};

public ref class Client sealed {
public:
  void Send(Function^ function, ClientResultHandler^ handler) {
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

  BaseObject^ Execute(Function^ function) {
    if (function == nullptr) {
      throw REF_NEW NullReferenceException("Function can't be null");
    }

    td::Client::Request request;
    request.id = 0;
    request.function = td::td_api::move_object_as<td::td_api::Function>(ToUnmanaged(function)->get_object_ptr());
    return FromUnmanaged(*client->execute(std::move(request)).object);
  }

  void SetUpdatesHandler(ClientResultHandler^ handler) {
    handlers[0] = handler;
  }

  void Run() {
    while (true) {
      auto response = client->receive(10.0);
      if (response.object != nullptr) {
        ProcessResult(response.id, FromUnmanaged(*response.object));

        if (response.object->get_id() == td::td_api::updateAuthorizationState::ID &&
            static_cast<td::td_api::updateAuthorizationState &>(*response.object).authorization_state_->get_id() ==
            td::td_api::authorizationStateClosed::ID) {
          break;
        }
      }
    }
  }

  static Client^ Create(ClientResultHandler^ updatesHandler) {
    return REF_NEW Client(updatesHandler);
  }

private:
  Client(ClientResultHandler^ updatesHandler) {
    client = new td::Client();
    handlers[0] = updatesHandler;
  }

  ~Client() {
    delete client;
  }

  std::int64_t currentId = 0;
  ConcurrentDictionary<std::uint64_t, ClientResultHandler^> handlers;
  td::Client *client = nullptr;

  void ProcessResult(std::uint64_t id, BaseObject^ object) {
    ClientResultHandler^ handler;
    // update handler stays forever
    if (id == 0 ? handlers.TryGetValue(id, handler) : handlers.TryRemove(id, handler)) {
      // TODO try/catch
      handler->OnResult(object);
    }
  }
};

}  // namespace TdWindows
