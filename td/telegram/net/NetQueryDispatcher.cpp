//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/net/NetQueryDispatcher.h"

#include "td/telegram/Global.h"
#include "td/telegram/net/AuthDataShared.h"
#include "td/telegram/net/DcAuthManager.h"
#include "td/telegram/net/NetQuery.h"
#include "td/telegram/net/NetQueryDelayer.h"
#include "td/telegram/net/PublicRsaKeyShared.h"
#include "td/telegram/net/PublicRsaKeyWatchdog.h"
#include "td/telegram/net/SessionMultiProxy.h"
#include "td/telegram/SequenceDispatcher.h"
#include "td/telegram/Td.h"
#include "td/telegram/TdDb.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/port/sleep.h"
#include "td/utils/Slice.h"
#include "td/utils/SliceBuilder.h"

namespace td {

void NetQueryDispatcher::complete_net_query(NetQueryPtr net_query) {
  auto callback = net_query->move_callback();
  if (callback.empty()) {
    net_query->debug("sent to td (no callback)");
    send_closure_later(G()->td(), &Td::on_result, std::move(net_query));
  } else {
    net_query->debug("sent to callback", true);
    send_closure_later(std::move(callback), &NetQueryCallback::on_result, std::move(net_query));
  }
}

void NetQueryDispatcher::dispatch(NetQueryPtr net_query) {
  // net_query->debug("dispatch");
  if (stop_flag_.load(std::memory_order_relaxed)) {
    net_query->set_error(Global::request_aborted_error());
    return complete_net_query(std::move(net_query));
  }
  if (G()->get_option_boolean("test_flood_wait")) {
    net_query->set_error(Status::Error(429, "Too Many Requests: retry after 10"));
    return complete_net_query(std::move(net_query));
    //    if (net_query->is_ok() && net_query->tl_constructor() == telegram_api::messages_sendMessage::ID) {
    //      net_query->set_error(Status::Error(420, "FLOOD_WAIT_10"));
    //    }
  }
  if (net_query->tl_constructor() == telegram_api::account_getPassword::ID && false) {
    net_query->set_error(Status::Error(429, "Too Many Requests: retry after 10"));
    return complete_net_query(std::move(net_query));
  }

  if (!net_query->in_sequence_dispatcher() && !net_query->get_chain_ids().empty()) {
    net_query->debug("sent to main sequence dispatcher");
    send_closure_later(sequence_dispatcher_, &MultiSequenceDispatcher::send, std::move(net_query));
    return;
  }

  if (net_query->is_ready() && net_query->is_error()) {
    auto code = net_query->error().code();
    if (code == 303) {
      try_fix_migrate(net_query);
    } else if (code == NetQuery::Resend) {
      net_query->resend();
    } else if (code < 0 || code == 500 ||
               (code == 420 && !begins_with(net_query->error().message(), "STORY_SEND_FLOOD_"))) {
      net_query->debug("sent to NetQueryDelayer");
      return send_closure_later(delayer_, &NetQueryDelayer::delay, std::move(net_query));
    }
  }

  if (!net_query->is_ready()) {
    if (net_query->dispatch_ttl_ == 0) {
      net_query->set_error(Status::Error("DispatchTtlError"));
    }
  }

  auto dest_dc_id = net_query->dc_id();
  if (dest_dc_id.is_main()) {
    dest_dc_id = DcId::internal(main_dc_id_.load(std::memory_order_relaxed));
  }
  if (!net_query->is_ready() && wait_dc_init(dest_dc_id, true).is_error()) {
    net_query->set_error(Status::Error(PSLICE() << "No such dc " << dest_dc_id));
  }

  if (net_query->is_ready()) {
    return complete_net_query(std::move(net_query));
  }

  if (net_query->dispatch_ttl_ > 0) {
    net_query->dispatch_ttl_--;
  }

  auto dc_pos = static_cast<size_t>(dest_dc_id.get_raw_id() - 1);
  CHECK(dc_pos < dcs_.size());
  switch (net_query->type()) {
    case NetQuery::Type::Common:
      net_query->debug(PSTRING() << "sent to main session multi proxy " << dest_dc_id);
      send_closure_later(dcs_[dc_pos].main_session_, &SessionMultiProxy::send, std::move(net_query));
      break;
    case NetQuery::Type::Upload:
      net_query->debug(PSTRING() << "sent to upload session multi proxy " << dest_dc_id);
      send_closure_later(dcs_[dc_pos].upload_session_, &SessionMultiProxy::send, std::move(net_query));
      break;
    case NetQuery::Type::Download:
      net_query->debug(PSTRING() << "sent to download session multi proxy " << dest_dc_id);
      send_closure_later(dcs_[dc_pos].download_session_, &SessionMultiProxy::send, std::move(net_query));
      break;
    case NetQuery::Type::DownloadSmall:
      net_query->debug(PSTRING() << "sent to download small session multi proxy " << dest_dc_id);
      send_closure_later(dcs_[dc_pos].download_small_session_, &SessionMultiProxy::send, std::move(net_query));
      break;
  }
}

Status NetQueryDispatcher::wait_dc_init(DcId dc_id, bool force) {
  // TODO: optimize
  if (!dc_id.is_exact()) {
    return Status::Error("Not exact DC");
  }
  auto pos = static_cast<size_t>(dc_id.get_raw_id() - 1);
  if (pos >= dcs_.size()) {
    return Status::Error("Too big DC ID");
  }
  auto &dc = dcs_[pos];

  bool should_init = false;
  if (!dc.is_valid_) {
    if (!force) {
      return Status::Error("Invalid DC");
    }
    bool expected = false;
    should_init =
        dc.is_valid_.compare_exchange_strong(expected, true, std::memory_order_seq_cst, std::memory_order_seq_cst);
  }

  if (should_init) {
    std::lock_guard<std::mutex> guard(main_dc_id_mutex_);
    if (stop_flag_.load(std::memory_order_relaxed) || need_destroy_auth_key_) {
      return Status::Error("Closing");
    }
    // init dc
    dc.id_ = dc_id;
    decltype(common_public_rsa_key_) public_rsa_key;
    bool is_cdn = false;
    if (dc_id.is_internal()) {
      public_rsa_key = common_public_rsa_key_;
    } else {
      public_rsa_key = std::make_shared<PublicRsaKeyShared>(dc_id, G()->is_test_dc());
      send_closure_later(public_rsa_key_watchdog_, &PublicRsaKeyWatchdog::add_public_rsa_key, public_rsa_key);
      is_cdn = true;
    }
    auto auth_data = AuthDataShared::create(dc_id, std::move(public_rsa_key), td_guard_);
    int32 session_count = get_session_count();
    bool use_pfs = get_use_pfs();

    int32 slow_net_scheduler_id = G()->get_slow_net_scheduler_id();

    auto raw_dc_id = dc_id.get_raw_id();
    bool is_premium = G()->get_option_boolean("is_premium");
    int32 upload_session_count = (raw_dc_id != 2 && raw_dc_id != 4) || is_premium ? 8 : 4;
    int32 download_session_count = is_premium ? 8 : 2;
    int32 download_small_session_count = is_premium ? 8 : 2;
    int32 main_session_scheduler_id = G()->use_sqlite_pmc() ? -1 : G()->get_database_scheduler_id();
    dc.main_session_ = create_actor_on_scheduler<SessionMultiProxy>(
        PSLICE() << "SessionMultiProxy:" << raw_dc_id << ":main", main_session_scheduler_id, session_count, auth_data,
        true, raw_dc_id == main_dc_id_, use_pfs, false, false, is_cdn);
    dc.upload_session_ = create_actor_on_scheduler<SessionMultiProxy>(
        PSLICE() << "SessionMultiProxy:" << raw_dc_id << ":upload", slow_net_scheduler_id, upload_session_count,
        auth_data, false, false, use_pfs, false, true, is_cdn);
    dc.download_session_ = create_actor_on_scheduler<SessionMultiProxy>(
        PSLICE() << "SessionMultiProxy:" << raw_dc_id << ":download", slow_net_scheduler_id, download_session_count,
        auth_data, false, false, use_pfs, true, true, is_cdn);
    dc.download_small_session_ = create_actor_on_scheduler<SessionMultiProxy>(
        PSLICE() << "SessionMultiProxy:" << raw_dc_id << ":download_small", slow_net_scheduler_id,
        download_small_session_count, auth_data, false, false, use_pfs, true, true, is_cdn);
    dc.is_inited_ = true;
    if (dc_id.is_internal()) {
      send_closure_later(dc_auth_manager_, &DcAuthManager::add_dc, std::move(auth_data));
    }
  } else {
    while (!dc.is_inited_) {
      if (stop_flag_.load(std::memory_order_relaxed)) {
        return Status::Error("Closing");
      }
#if !TD_THREAD_UNSUPPORTED
      usleep_for(1);
#endif
    }
  }
  return Status::OK();
}

void NetQueryDispatcher::dispatch_with_callback(NetQueryPtr net_query, ActorShared<NetQueryCallback> callback) {
  net_query->set_callback(std::move(callback));
  dispatch(std::move(net_query));
}

void NetQueryDispatcher::stop() {
  std::lock_guard<std::mutex> guard(main_dc_id_mutex_);
  td_guard_.reset();
  stop_flag_ = true;
  delayer_.reset();
  for (auto &dc : dcs_) {
    dc.main_session_.reset();
    dc.upload_session_.reset();
    dc.download_session_.reset();
    dc.download_small_session_.reset();
  }
  public_rsa_key_watchdog_.reset();
  dc_auth_manager_.reset();
  sequence_dispatcher_.reset();
}

void NetQueryDispatcher::update_session_count() {
  std::lock_guard<std::mutex> guard(main_dc_id_mutex_);
  int32 session_count = get_session_count();
  bool use_pfs = get_use_pfs();
  for (int32 i = 1; i < DcId::MAX_RAW_DC_ID; i++) {
    if (is_dc_inited(i)) {
      send_closure_later(dcs_[i - 1].main_session_, &SessionMultiProxy::update_options, session_count, use_pfs,
                         need_destroy_auth_key_);
      send_closure_later(dcs_[i - 1].upload_session_, &SessionMultiProxy::update_use_pfs, use_pfs);
      send_closure_later(dcs_[i - 1].download_session_, &SessionMultiProxy::update_use_pfs, use_pfs);
      send_closure_later(dcs_[i - 1].download_small_session_, &SessionMultiProxy::update_use_pfs, use_pfs);
    }
  }
}
void NetQueryDispatcher::destroy_auth_keys(Promise<> promise) {
  for (int32 i = 1; i < DcId::MAX_RAW_DC_ID && i <= 5; i++) {
    auto dc_id = DcId::internal(i);
    if (!is_dc_inited(i) && !AuthDataShared::get_auth_key_for_dc(dc_id).empty()) {
      wait_dc_init(dc_id, true).ignore();
    }
  }

  std::lock_guard<std::mutex> guard(main_dc_id_mutex_);
  LOG(INFO) << "Destroy auth keys";
  need_destroy_auth_key_ = true;
  for (int32 i = 1; i < DcId::MAX_RAW_DC_ID; i++) {
    if (is_dc_inited(i) && dcs_[i - 1].id_.is_internal()) {
      send_closure_later(dcs_[i - 1].main_session_, &SessionMultiProxy::destroy_auth_key);
    }
  }
  send_closure_later(dc_auth_manager_, &DcAuthManager::destroy, std::move(promise));
}

void NetQueryDispatcher::update_use_pfs() {
  std::lock_guard<std::mutex> guard(main_dc_id_mutex_);
  bool use_pfs = get_use_pfs();
  for (int32 i = 1; i < DcId::MAX_RAW_DC_ID; i++) {
    if (is_dc_inited(i)) {
      send_closure_later(dcs_[i - 1].main_session_, &SessionMultiProxy::update_use_pfs, use_pfs);
      send_closure_later(dcs_[i - 1].upload_session_, &SessionMultiProxy::update_use_pfs, use_pfs);
      send_closure_later(dcs_[i - 1].download_session_, &SessionMultiProxy::update_use_pfs, use_pfs);
      send_closure_later(dcs_[i - 1].download_small_session_, &SessionMultiProxy::update_use_pfs, use_pfs);
    }
  }
}

void NetQueryDispatcher::update_mtproto_header() {
  std::lock_guard<std::mutex> guard(main_dc_id_mutex_);
  for (int32 i = 1; i < DcId::MAX_RAW_DC_ID; i++) {
    if (is_dc_inited(i)) {
      send_closure_later(dcs_[i - 1].main_session_, &SessionMultiProxy::update_mtproto_header);
      send_closure_later(dcs_[i - 1].upload_session_, &SessionMultiProxy::update_mtproto_header);
      send_closure_later(dcs_[i - 1].download_session_, &SessionMultiProxy::update_mtproto_header);
      send_closure_later(dcs_[i - 1].download_small_session_, &SessionMultiProxy::update_mtproto_header);
    }
  }
}

bool NetQueryDispatcher::is_dc_inited(int32 raw_dc_id) {
  return dcs_[raw_dc_id - 1].is_valid_.load(std::memory_order_relaxed);
}

int32 NetQueryDispatcher::get_session_count() {
  return max(narrow_cast<int32>(G()->get_option_integer("session_count")), 1);
}

bool NetQueryDispatcher::get_use_pfs() {
  return G()->get_option_boolean("use_pfs") || get_session_count() > 1;
}

NetQueryDispatcher::NetQueryDispatcher(const std::function<ActorShared<>()> &create_reference) {
  auto s_main_dc_id = G()->td_db()->get_binlog_pmc()->get("main_dc_id");
  if (!s_main_dc_id.empty()) {
    main_dc_id_ = to_integer<int32>(s_main_dc_id);
  }
  LOG(INFO) << tag("main_dc_id", main_dc_id_.load(std::memory_order_relaxed));
  delayer_ = create_actor<NetQueryDelayer>("NetQueryDelayer", create_reference());
  dc_auth_manager_ = create_actor<DcAuthManager>("DcAuthManager", create_reference());
  common_public_rsa_key_ = std::make_shared<PublicRsaKeyShared>(DcId::empty(), G()->is_test_dc());
  public_rsa_key_watchdog_ = create_actor<PublicRsaKeyWatchdog>("PublicRsaKeyWatchdog", create_reference());
  sequence_dispatcher_ = MultiSequenceDispatcher::create("MultiSequenceDispatcher");

  td_guard_ = create_shared_lambda_guard([actor = create_reference()] {});
}

NetQueryDispatcher::NetQueryDispatcher() = default;
NetQueryDispatcher::~NetQueryDispatcher() = default;

void NetQueryDispatcher::try_fix_migrate(NetQueryPtr &net_query) {
  auto error_message = net_query->error().message();
  static constexpr CSlice prefixes[] = {"PHONE_MIGRATE_", "NETWORK_MIGRATE_", "USER_MIGRATE_"};
  for (auto &prefix : prefixes) {
    if (error_message.substr(0, prefix.size()) == prefix) {
      auto new_main_dc_id = to_integer<int32>(error_message.substr(prefix.size()));
      set_main_dc_id(new_main_dc_id);

      if (!net_query->dc_id().is_main()) {
        LOG(ERROR) << "Receive " << error_message << " for query to non-main DC" << net_query->dc_id();
        net_query->resend(DcId::internal(new_main_dc_id));
      } else {
        net_query->resend();
      }
      break;
    }
  }
}

void NetQueryDispatcher::set_main_dc_id(int32 new_main_dc_id) {
  if (!DcId::is_valid(new_main_dc_id)) {
    LOG(ERROR) << "Receive wrong DC " << new_main_dc_id;
    return;
  }
  if (new_main_dc_id == main_dc_id_.load(std::memory_order_relaxed)) {
    return;
  }

  // Very rare event; mutex is ok.
  std::lock_guard<std::mutex> guard(main_dc_id_mutex_);
  if (new_main_dc_id == main_dc_id_) {
    return;
  }

  LOG(INFO) << "Update main DcId from " << main_dc_id_.load(std::memory_order_relaxed) << " to " << new_main_dc_id;
  if (is_dc_inited(main_dc_id_.load(std::memory_order_relaxed))) {
    send_closure_later(dcs_[main_dc_id_ - 1].main_session_, &SessionMultiProxy::update_main_flag, false);
  }
  main_dc_id_ = new_main_dc_id;
  if (is_dc_inited(main_dc_id_.load(std::memory_order_relaxed))) {
    send_closure_later(dcs_[main_dc_id_ - 1].main_session_, &SessionMultiProxy::update_main_flag, true);
  }
  send_closure_later(dc_auth_manager_, &DcAuthManager::update_main_dc,
                     DcId::internal(main_dc_id_.load(std::memory_order_relaxed)));
  G()->td_db()->get_binlog_pmc()->set("main_dc_id", to_string(main_dc_id_.load(std::memory_order_relaxed)));
}

void NetQueryDispatcher::check_authorization_is_ok() {
  send_closure(dc_auth_manager_, &DcAuthManager::check_authorization_is_ok);
}

}  // namespace td
