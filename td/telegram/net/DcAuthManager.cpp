//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
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
#include "td/telegram/UniqueId.h"

#include "td/telegram/telegram_api.h"

#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"

#include <algorithm>
#include <limits>

namespace td {

DcAuthManager::DcAuthManager(ActorShared<> parent) {
  parent_ = std::move(parent);
  auto s_main_dc_id = G()->td_db()->get_binlog_pmc()->get("main_dc_id");
  if (!s_main_dc_id.empty()) {
    main_dc_id_ = DcId::internal(to_integer<int32>(s_main_dc_id));
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
      send_closure(dc_manager_, &DcAuthManager::update_auth_state);
      return true;
    }

   private:
    ActorShared<DcAuthManager> dc_manager_;
  };

  DcInfo info;
  info.dc_id = auth_data->dc_id();
  CHECK(info.dc_id.is_exact());
  info.shared_auth_data = std::move(auth_data);
  auto state_was_auth = info.shared_auth_data->get_auth_state();
  info.auth_state = state_was_auth.first;
  was_auth_ |= state_was_auth.second;
  if (!main_dc_id_.is_exact()) {
    main_dc_id_ = info.dc_id;
  }
  info.shared_auth_data->add_auth_key_listener(std::make_unique<Listener>(actor_shared(this, info.dc_id.get_raw_id())));
  dcs_.emplace_back(std::move(info));
  loop();
}

void DcAuthManager::update_main_dc(DcId new_main_dc_id) {
  main_dc_id_ = new_main_dc_id;
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

void DcAuthManager::update_auth_state() {
  int32 dc_id = narrow_cast<int32>(get_link_token());
  auto &dc = get_dc(dc_id);
  auto state_was_auth = dc.shared_auth_data->get_auth_state();
  VLOG(dc) << "Update dc auth state " << tag("dc_id", dc_id) << tag("old_auth_state", dc.auth_state)
           << tag("new_auth_state", state_was_auth.first);
  dc.auth_state = state_was_auth.first;
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
      auto result_auth_exported = fetch_result<telegram_api::auth_exportAuthorization>(result->ok());
      if (result_auth_exported.is_error()) {
        LOG(WARNING) << "Failed to parse result to auth_exportAuthorization: " << result_auth_exported.error();
        dc.state = DcInfo::State::Export;
        break;
      }
      dc.export_id = result_auth_exported.ok()->id_;
      dc.export_bytes = std::move(result_auth_exported.ok()->bytes_);
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
  VLOG(dc) << "dc_loop " << dc.dc_id << " " << dc.auth_state;
  if (dc.auth_state == AuthState::OK) {
    return;
  }
  CHECK(dc.shared_auth_data);
  switch (dc.state) {
    case DcInfo::State::Waiting: {
      // wait for timeout
      //      break;
    }
    case DcInfo::State::Export: {
      // send auth.exportAuthorization to auth_dc
      VLOG(dc) << "Send exportAuthorization to " << dc.dc_id;
      auto id = UniqueId::next();
      G()->net_query_dispatcher().dispatch_with_callback(
          G()->net_query_creator().create(
              id, create_storer(telegram_api::auth_exportAuthorization(dc.dc_id.get_raw_id())), DcId::main(),
              NetQuery::Type::Common, NetQuery::AuthFlag::On, NetQuery::GzipFlag::On, 60 * 60 * 24),
          actor_shared(this, dc.dc_id.get_raw_id()));
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
      G()->net_query_dispatcher().dispatch_with_callback(
          G()->net_query_creator().create(
              id, create_storer(telegram_api::auth_importAuthorization(dc.export_id, std::move(dc.export_bytes))),
              dc.dc_id, NetQuery::Type::Common, NetQuery::AuthFlag::Off, NetQuery::GzipFlag::On, 60 * 60 * 24),
          actor_shared(this, dc.dc_id.get_raw_id()));
      dc.wait_id = id;
      dc.state = DcInfo::State::BeforeOk;
      break;
    }
    case DcInfo::State::BeforeOk: {
      break;
    }
    case DcInfo::State::Ok: {
      break;
    }
  }
}

void DcAuthManager::loop() {
  if (close_flag_) {
    VLOG(dc) << "Skip loop because close_flag";
    return;
  }
  if (!main_dc_id_.is_exact()) {
    VLOG(dc) << "Skip loop because main_dc_id is unknown";
    return;
  }
  auto main_dc = find_dc(main_dc_id_.get_raw_id());
  if (!main_dc || main_dc->auth_state != AuthState::OK) {
    if (was_auth_) {
      G()->shared_config().set_option_boolean("auth", false);
    }
    VLOG(dc) << "Skip loop because auth state of main dc " << main_dc_id_.get_raw_id() << " is "
             << (main_dc != nullptr ? (PSTRING() << main_dc->auth_state) : "unknown");

    return;
  }
  for (auto &dc : dcs_) {
    dc_loop(dc);
  }
}
}  // namespace td
