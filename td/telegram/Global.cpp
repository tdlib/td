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
#include "td/telegram/Version.h"

#include "td/actor/MultiPromise.h"
#include "td/actor/PromiseFuture.h"

#include "td/utils/format.h"
#include "td/utils/port/Clocks.h"
#include "td/utils/tl_helpers.h"

#include <algorithm>

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

const MtprotoHeader &Global::mtproto_header() const {
  return *mtproto_header_;
}
void Global::set_mtproto_header(std::unique_ptr<MtprotoHeader> mtproto_header) {
  mtproto_header_ = std::move(mtproto_header);
}

Status Global::init(const TdParameters &parameters, ActorId<Td> td, std::unique_ptr<TdDb> td_db) {
  parameters_ = parameters;

  gc_scheduler_id_ = std::min(Scheduler::instance()->sched_id() + 2, Scheduler::instance()->sched_count() - 1);
  slow_net_scheduler_id_ = std::min(Scheduler::instance()->sched_id() + 3, Scheduler::instance()->sched_count() - 1);

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

void Global::set_net_query_dispatcher(std::unique_ptr<NetQueryDispatcher> net_query_dispatcher) {
  net_query_dispatcher_ = std::move(net_query_dispatcher);
}
void Global::set_shared_config(std::unique_ptr<ConfigShared> shared_config) {
  shared_config_ = std::move(shared_config);
}

}  // namespace td
