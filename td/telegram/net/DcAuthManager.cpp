//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/net/DcAuthManager.h"

#include "td/actor/actor.h"

#include "td/telegram/ConfigShared.h"
#include "td/telegram/Global.h"
#include "td/telegram/net/AuthDataShared.h"
#include "td/telegram/net/NetQuery.h"
#include "td/telegram/net/NetQueryDispatcher.h"
#include "td/telegram/TdDb.h"
#include "td/telegram/UniqueId.h"

#include "td/telegram/telegram_api.h"

#include "td/utils/logging.h"
#include "td/utils/misc.h"

#include <algorithm>
#include <limits>

namespace td {

int VERBOSITY_NAME(dc) = VERBOSITY_NAME(DEBUG) + 2;

DcAuthManager::DcAuthManager(ActorShared<> parent) {
  parent_ = std::move(parent);
  auto s_main_dc_id = G()->td_db()->get_binlog_pmc()->get("main_dc_id");
  if (!s_main_dc_id.empty()) {
    auto main_dc_id = to_integer<int32>(s_main_dc_id);
    if (DcId::is_valid(main_dc_id)) {
      main_dc_id_ = DcId::internal(main_dc_id);
      VLOG(dc) << "Init main DcId to " << main_dc_id_;
    } else {
      LOG(ERROR) << "Receive invalid main DcId " << main_dc_id;
    }
  }
}

void DcAuthManager::add_dc(std::shared_ptr<AuthDataShared> auth_data) {
  VLOG(dc) << "Register " << auth_data->dc_id();
  class Listener : public AuthDataShared::Listener {
   public:
    explicit Listener(ActorShared<DcAuthManager> dc_manager) : dc_manager_(std::move(dc_manager)) {
    }
    bool notify() override {
      if (!dc_manager_.is_alive()) {
        return false;
      }
      send_closure(dc_manager_, &DcAuthManager::update_auth_key_state);
      return true;
    }

   private:
    ActorShared<DcAuthManager> dc_manager_;
  };

  DcInfo info;
  info.dc_id = auth_data->dc_id();
  CHECK(info.dc_id.is_exact());
  info.shared_auth_data = std::move(auth_data);
  auto state_was_auth = info.shared_auth_data->get_auth_key_state();
  info.auth_key_state = state_was_auth.first;
  VLOG(dc) << "Add " << info.dc_id << " with auth key state " << info.auth_key_state
           << " and was_auth = " << state_was_auth.second;
  was_auth_ |= state_was_auth.second;
  if (!main_dc_id_.is_exact()) {
    main_dc_id_ = info.dc_id;
    VLOG(dc) << "Set main DcId to " << main_dc_id_;
  }
  info.shared_auth_data->add_auth_key_listener(make_unique<Listener>(actor_shared(this, info.dc_id.get_raw_id())));
  dcs_.emplace_back(std::move(info));
  loop();
}

void DcAuthManager::update_main_dc(DcId new_main_dc_id) {
  main_dc_id_ = new_main_dc_id;
  VLOG(dc) << "Update main DcId to " << main_dc_id_;
  loop();
}

DcAuthManager::DcInfo &DcAuthManager::get_dc(int32 dc_id) {
  auto *dc = find_dc(dc_id);
  CHECK(dc);
  return *dc;
}
DcAuthManager::DcInfo *DcAuthManager::find_dc(int32 dc_id) {
  auto it = std::find_if(dcs_.begin(), dcs_.end(), [&](auto &x) { return x.dc_id.get_raw_id() == dc_id; });
  if (it == dcs_.end()) {
    return nullptr;
  }
  return &*it;
}

void DcAuthManager::update_auth_key_state() {
  int32 dc_id = narrow_cast<int32>(get_link_token());
  auto &dc = get_dc(dc_id);
  auto state_was_auth = dc.shared_auth_data->get_auth_key_state();
  VLOG(dc) << "Update " << dc_id << " auth key state from " << dc.auth_key_state << " to " << state_was_auth.first
           << " with was_auth = " << state_was_auth.second;
  dc.auth_key_state = state_was_auth.first;
  was_auth_ |= state_was_auth.second;

  loop();
}

void DcAuthManager::on_result(NetQueryPtr result) {
  int32 dc_id = narrow_cast<int32>(get_link_token());
  auto &dc = get_dc(dc_id);
  CHECK(dc.wait_id == result->id());
  dc.wait_id = std::numeric_limits<decltype(dc.wait_id)>::max();
  switch (dc.state) {
    case DcInfo::State::Import: {
      if (result->is_error()) {
        LOG(WARNING) << "DC auth_exportAuthorization error: " << result->error();
        dc.state = DcInfo::State::Export;
        break;
      }
      auto r_result_auth_exported = fetch_result<telegram_api::auth_exportAuthorization>(result->ok());
      if (r_result_auth_exported.is_error()) {
        LOG(WARNING) << "Failed to parse result to auth_exportAuthorization: " << r_result_auth_exported.error();
        dc.state = DcInfo::State::Export;
        break;
      }
      auto result_auth_exported = r_result_auth_exported.move_as_ok();
      dc.export_id = result_auth_exported->id_;
      dc.export_bytes = std::move(result_auth_exported->bytes_);
      break;
    }
    case DcInfo::State::BeforeOk: {
      if (result->is_error()) {
        LOG(WARNING) << "DC authImport error: " << result->error();
        dc.state = DcInfo::State::Export;
        break;
      }
      auto result_auth = fetch_result<telegram_api::auth_importAuthorization>(result->ok());
      if (result_auth.is_error()) {
        LOG(WARNING) << "Failed to parse result to auth_importAuthorization: " << result_auth.error();
        dc.state = DcInfo::State::Export;
        break;
      }
      dc.state = DcInfo::State::Ok;
      break;
    }
    default:
      UNREACHABLE();
  }
  result->clear();
  loop();
}

void DcAuthManager::dc_loop(DcInfo &dc) {
  VLOG(dc) << "In dc_loop: " << dc.dc_id << " " << dc.auth_key_state;
  if (dc.auth_key_state == AuthKeyState::OK) {
    return;
  }
  if (dc.state == DcInfo::State::Ok) {
    LOG(WARNING) << "Lost key in " << dc.dc_id << ", restart dc_loop";
    dc.state = DcInfo::State::Waiting;
  }
  CHECK(dc.shared_auth_data);
  switch (dc.state) {
    case DcInfo::State::Waiting: {
      // wait for timeout
      // break;
    }
    case DcInfo::State::Export: {
      // send auth.exportAuthorization to auth_dc
      VLOG(dc) << "Send exportAuthorization to " << dc.dc_id;
      auto id = UniqueId::next();
      auto query = G()->net_query_creator().create(id, telegram_api::auth_exportAuthorization(dc.dc_id.get_raw_id()),
                                                   DcId::main(), NetQuery::Type::Common, NetQuery::AuthFlag::On);
      query->total_timeout_limit_ = 60 * 60 * 24;
      G()->net_query_dispatcher().dispatch_with_callback(std::move(query), actor_shared(this, dc.dc_id.get_raw_id()));
      dc.wait_id = id;
      dc.export_id = -1;
      dc.state = DcInfo::State::Import;
      break;
    }
    case DcInfo::State::Import: {
      // send auth.importAuthorization to dc
      if (dc.export_id == -1) {
        break;
      }
      uint64 id = UniqueId::next();
      VLOG(dc) << "Send importAuthorization to " << dc.dc_id;
      auto query = G()->net_query_creator().create(
          id, telegram_api::auth_importAuthorization(dc.export_id, std::move(dc.export_bytes)), dc.dc_id,
          NetQuery::Type::Common, NetQuery::AuthFlag::Off);
      query->total_timeout_limit_ = 60 * 60 * 24;
      G()->net_query_dispatcher().dispatch_with_callback(std::move(query), actor_shared(this, dc.dc_id.get_raw_id()));
      dc.wait_id = id;
      dc.state = DcInfo::State::BeforeOk;
      break;
    }
    case DcInfo::State::BeforeOk:
      break;
    case DcInfo::State::Ok:
      break;
  }
}

void DcAuthManager::destroy(Promise<> promise) {
  destroy_promise_ = std::move(promise);
  loop();
}

void DcAuthManager::destroy_loop() {
  if (!destroy_promise_) {
    return;
  }
  bool is_ready{true};
  for (auto &dc : dcs_) {
    is_ready &= dc.auth_key_state == AuthKeyState::Empty;
  }

  if (is_ready) {
    VLOG(dc) << "Destroy auth keys loop is ready, all keys are destroyed";
    destroy_promise_.set_value(Unit());
  } else {
    VLOG(dc) << "DC is not ready for destroying auth key";
  }
}

void DcAuthManager::loop() {
  if (close_flag_) {
    VLOG(dc) << "Skip loop because close_flag";
    return;
  }
  destroy_loop();
  if (!main_dc_id_.is_exact()) {
    VLOG(dc) << "Skip loop because main_dc_id is unknown";
    return;
  }
  auto main_dc = find_dc(main_dc_id_.get_raw_id());
  if (!main_dc || main_dc->auth_key_state != AuthKeyState::OK) {
    VLOG(dc) << "Main is " << main_dc_id_ << ", main auth key state is "
             << (main_dc ? main_dc->auth_key_state : AuthKeyState::Empty) << ", was_auth = " << was_auth_;
    if (was_auth_) {
      G()->shared_config().set_option_boolean("auth", false);
      destroy_loop();
    }
    VLOG(dc) << "Skip loop because auth state of main DcId " << main_dc_id_.get_raw_id() << " is "
             << (main_dc != nullptr ? (PSTRING() << main_dc->auth_key_state) : "unknown");

    return;
  }
  for (auto &dc : dcs_) {
    dc_loop(dc);
  }
}

}  // namespace td
