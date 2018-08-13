//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/Global.h"

#include "td/telegram/ConfigShared.h"
#include "td/telegram/net/ConnectionCreator.h"
#include "td/telegram/net/MtprotoHeader.h"
#include "td/telegram/net/NetQueryDispatcher.h"
#include "td/telegram/net/TempAuthKeyWatchdog.h"
#include "td/telegram/TdDb.h"

#include "td/actor/PromiseFuture.h"

#include "td/db/Pmc.h"

#include "td/utils/format.h"
#include "td/utils/port/Clocks.h"
#include "td/utils/tl_helpers.h"

#include <cmath>

namespace td {

Global::Global() = default;

Global::~Global() = default;

void Global::close_all(Promise<> on_finished) {
  td_db_->close_all(std::move(on_finished));
  state_manager_.clear();
  parameters_ = TdParameters();
}
void Global::close_and_destroy_all(Promise<> on_finished) {
  td_db_->close_and_destroy_all(std::move(on_finished));
  state_manager_.clear();
  parameters_ = TdParameters();
}

ActorId<ConnectionCreator> Global::connection_creator() const {
  return connection_creator_.get();
}
void Global::set_connection_creator(ActorOwn<ConnectionCreator> connection_creator) {
  connection_creator_ = std::move(connection_creator);
}

ActorId<TempAuthKeyWatchdog> Global::temp_auth_key_watchdog() const {
  return temp_auth_key_watchdog_.get();
}
void Global::set_temp_auth_key_watchdog(ActorOwn<TempAuthKeyWatchdog> actor) {
  temp_auth_key_watchdog_ = std::move(actor);
}

MtprotoHeader &Global::mtproto_header() {
  return *mtproto_header_;
}
void Global::set_mtproto_header(std::unique_ptr<MtprotoHeader> mtproto_header) {
  mtproto_header_ = std::move(mtproto_header);
}

Status Global::init(const TdParameters &parameters, ActorId<Td> td, std::unique_ptr<TdDb> td_db) {
  parameters_ = parameters;

  gc_scheduler_id_ = min(Scheduler::instance()->sched_id() + 2, Scheduler::instance()->sched_count() - 1);
  slow_net_scheduler_id_ = min(Scheduler::instance()->sched_id() + 3, Scheduler::instance()->sched_count() - 1);

  td_ = td;
  td_db_ = std::move(td_db);

  string save_diff_str = this->td_db()->get_binlog_pmc()->get("server_time_difference");
  if (save_diff_str.empty()) {
    server_time_difference_ = Clocks::system() - Time::now();
    server_time_difference_was_updated_ = false;
  } else {
    double save_diff;
    unserialize(save_diff, save_diff_str).ensure();
    double diff = save_diff + Clocks::system() - Time::now();
    LOG(DEBUG) << "LOAD: " << tag("server_time_difference", diff);
    server_time_difference_ = diff;
    server_time_difference_was_updated_ = false;
  }

  return Status::OK();
}

void Global::update_server_time_difference(double diff) {
  if (!server_time_difference_was_updated_ || server_time_difference_ < diff) {
    server_time_difference_ = diff;
    server_time_difference_was_updated_ = true;

    // diff = server_time - Time::now
    // save_diff = server_time - Clocks::system
    double save_diff = diff + Time::now() - Clocks::system();
    auto str = serialize(save_diff);
    td_db()->get_binlog_pmc()->set("server_time_difference", str);
  }
}

DcId Global::get_webfile_dc_id() const {
  int32 dc_id = shared_config_->get_option_integer("webfile_dc_id");
  if (!DcId::is_valid(dc_id)) {
    if (is_test_dc()) {
      dc_id = 2;
    } else {
      dc_id = 4;
    }

    CHECK(DcId::is_valid(dc_id));
  }

  return DcId::internal(dc_id);
}

void Global::set_net_query_dispatcher(std::unique_ptr<NetQueryDispatcher> net_query_dispatcher) {
  net_query_dispatcher_ = std::move(net_query_dispatcher);
}

void Global::set_shared_config(std::unique_ptr<ConfigShared> shared_config) {
  shared_config_ = std::move(shared_config);
}

int64 Global::get_location_key(double latitude, double longitude) {
  const double PI = 3.14159265358979323846;
  latitude *= PI / 180;
  longitude *= PI / 180;

  int64 key = 0;
  if (latitude < 0) {
    latitude = -latitude;
    key = 65536;
  }

  double f = std::tan(PI / 4 - latitude / 2);
  key += static_cast<int64>(f * std::cos(longitude) * 128) * 256;
  key += static_cast<int64>(f * std::sin(longitude) * 128);
  return key;
}

int64 Global::get_location_access_hash(double latitude, double longitude) {
  auto it = location_access_hashes_.find(get_location_key(latitude, longitude));
  if (it == location_access_hashes_.end()) {
    return 0;
  }
  return it->second;
}

void Global::add_location_access_hash(double latitude, double longitude, int64 access_hash) {
  if (access_hash == 0) {
    return;
  }

  location_access_hashes_[get_location_key(latitude, longitude)] = access_hash;
}

}  // namespace td
