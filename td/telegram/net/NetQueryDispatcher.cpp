//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/net/NetQueryDispatcher.h"

#include "td/telegram/Global.h"
#include "td/telegram/net/AuthDataShared.h"
#include "td/telegram/net/DcAuthManager.h"
#include "td/telegram/net/DcOptions.h"
#include "td/telegram/net/NetQuery.h"
#include "td/telegram/net/NetQueryDelayer.h"
#include "td/telegram/net/NetQueryVerifier.h"
#include "td/telegram/net/PublicRsaKeySharedCdn.h"
#include "td/telegram/net/PublicRsaKeySharedMain.h"
#include "td/telegram/net/PublicRsaKeyWatchdog.h"
#include "td/telegram/net/SessionMultiProxy.h"
#include "td/telegram/net/StealthConnectionCountPolicy.h"
#include "td/telegram/SequenceDispatcher.h"
#include "td/telegram/ReferenceTable.h"
#include "td/telegram/SessionBlendTable.h"
#include "td/telegram/Td.h"
#include "td/telegram/TdDb.h"
#include "td/telegram/telegram_api.h"

#include "td/mtproto/PacketAlignmentSeeds.h"
#include "td/mtproto/RSA.h"

#include "td/net/SessionTicketSeeds.h"

#include "td/telegram/net/ConfigCacheSeeds.h"

#include "td/utils/common.h"
#include "td/utils/crypto.h"
#include "td/utils/format.h"
#include "td/utils/HashIndexSeeds.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/port/sleep.h"
#include "td/utils/Slice.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/Time.h"
#include "td/utils/tl_helpers.h"
#include "td/utils/UInt.h"

namespace td {

#define TD_TEST_VERIFICATION 0

namespace {

template <size_t N>
void append_bytes(string &target, const unsigned char (&bytes)[N]) {
  target.append(reinterpret_cast<const char *>(bytes), N);
}

uint64 load_uint64_le(Slice slice) {
  CHECK(slice.size() == 8);
  uint64 result = 0;
  for (size_t i = 0; i < 8; i++) {
    result |= static_cast<uint64>(static_cast<unsigned char>(slice[i])) << (i * 8);
  }
  return result;
}

}  // namespace

void NetQueryDispatcher::complete_net_query(NetQueryPtr net_query) {
  auto callback = net_query->move_callback();
  if (callback.empty()) {
    net_query->debug("sent to handler");
    send_closure_later(G()->td(), &Td::on_result, std::move(net_query));
  } else {
    net_query->debug("sent to callback", true);
    send_closure_later(std::move(callback), &NetQueryCallback::on_result, std::move(net_query));
  }
}

bool NetQueryDispatcher::check_stop_flag(NetQueryPtr &net_query) const {
  if (stop_flag_.load(std::memory_order_relaxed)) {
    net_query->set_error(Global::request_aborted_error());
    complete_net_query(std::move(net_query));
    return true;
  }
  return false;
}

void NetQueryDispatcher::dispatch(NetQueryPtr net_query) {
  if (check_stop_flag(net_query)) {
    return;
  }
  if (false && G()->get_option_boolean("test_flood_wait")) {
    net_query->set_error(Status::Error(429, "Too Many Requests: retry after 10"));
    return complete_net_query(std::move(net_query));
    //    if (net_query->is_ok() && net_query->tl_constructor() == telegram_api::messages_sendMessage::ID) {
    //      net_query->set_error(Status::Error(420, "FLOOD_WAIT_10"));
    //    }
  }
  if (false && net_query->tl_constructor() == telegram_api::account_getPassword::ID) {
    net_query->set_error(Status::Error(429, "Too Many Requests: retry after 10"));
    return complete_net_query(std::move(net_query));
  }
#if TD_TEST_VERIFICATION
  if (net_query->tl_constructor() == telegram_api::account_getAuthorizations::ID &&
      !net_query->has_verification_prefix() && !net_query->is_ready()) {
    net_query->set_error(Status::Error(403, "APNS_VERIFY_CHECK_ABCD"));
  }
  if (net_query->tl_constructor() == telegram_api::auth_sendCode::ID && !net_query->has_verification_prefix() &&
      !net_query->is_ready()) {
    net_query->set_error(Status::Error(403, "RECAPTCHA_CHECK_AB_CD__KEY"));
  }
#endif

  if (!net_query->in_sequence_dispatcher() && !net_query->get_chain_ids().empty()) {
    net_query->debug("sent to main sequence dispatcher");
    std::lock_guard<std::mutex> guard(mutex_);
    if (check_stop_flag(net_query)) {
      return;
    }
    send_closure_later(sequence_dispatcher_, &MultiSequenceDispatcher::send, std::move(net_query));
    return;
  }

  if (net_query->is_ready() && net_query->is_error()) {
    auto code = net_query->error().code();
    auto message = net_query->error().message();
    if (code == 303) {
      try_fix_migrate(net_query);
    } else if (code == NetQuery::Resend) {
      net_query->resend();
    } else if (code == 420 && message == "FROZEN_METHOD_INVALID") {
      net_query->set_error(Status::Error(406, message));
    } else if (code < 0 || code == 500 ||
               (code == 420 && !begins_with(net_query->error().message(), "STORY_SEND_FLOOD_") &&
                !begins_with(net_query->error().message(), "PREMIUM_SUB_ACTIVE_UNTIL_"))) {
      net_query->debug("sent to NetQueryDelayer");
      std::lock_guard<std::mutex> guard(mutex_);
      if (check_stop_flag(net_query)) {
        return;
      }
      return send_closure_later(delayer_, &NetQueryDelayer::delay, std::move(net_query));
#if TD_ANDROID || TD_DARWIN_IOS || TD_DARWIN_VISION_OS || TD_DARWIN_WATCH_OS || TD_TEST_VERIFICATION
    } else if (code == 403) {
      Slice captcha_prefix = "RECAPTCHA_CHECK_";
      if (begins_with(net_query->error().message(), captcha_prefix)) {
        net_query->debug("sent to NetQueryVerifier");
        std::lock_guard<std::mutex> guard(mutex_);
        if (check_stop_flag(net_query)) {
          return;
        }
        auto parameters = net_query->error().message().substr(captcha_prefix.size());
        string action;
        string recaptcha_key_id;
        for (std::size_t i = 0; i + 1 < parameters.size(); i++) {
          if (parameters[i] == '_' && parameters[i + 1] == '_') {
            action = parameters.substr(0, i).str();
            recaptcha_key_id = parameters.substr(i + 2).str();
          }
        }
        return send_closure_later(verifier_, &NetQueryVerifier::check_recaptcha, std::move(net_query),
                                  std::move(action), std::move(recaptcha_key_id));
      }
#if TD_ANDROID
      Slice prefix("INTEGRITY_CHECK_CLASSIC_");
#else
      Slice prefix("APNS_VERIFY_CHECK_");
#endif
      if (begins_with(net_query->error().message(), prefix)) {
        net_query->debug("sent to NetQueryVerifier");
        std::lock_guard<std::mutex> guard(mutex_);
        if (check_stop_flag(net_query)) {
          return;
        }
        string nonce = net_query->error().message().substr(prefix.size()).str();
        return send_closure_later(verifier_, &NetQueryVerifier::verify, std::move(net_query), std::move(nonce));
      }
#endif
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
  std::lock_guard<std::mutex> guard(mutex_);
  if (check_stop_flag(net_query)) {
    return;
  }
  auto query_type =
      resolve_connection_count_routed_query_type(net_query->type(), get_connection_count_plan(dest_dc_id.get_raw_id()));
  switch (query_type) {
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
    default:
      UNREACHABLE();
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
    std::lock_guard<std::mutex> guard(mutex_);
    if (stop_flag_.load(std::memory_order_relaxed) || need_destroy_auth_key_) {
      return Status::Error("Closing");
    }
    // init dc
    dc.id_ = dc_id;
    std::shared_ptr<mtproto::PublicRsaKeyInterface> public_rsa_key;
    bool is_cdn = false;
    if (dc_id.is_internal()) {
      public_rsa_key = PublicRsaKeySharedMain::create(G()->is_test_dc());
      vector<int64> expected_fingerprints = {ReferenceTable::slot_value(mtproto::BlobRole::Primary),
                     ReferenceTable::slot_value(mtproto::BlobRole::Secondary)};
      auto rsa_result = public_rsa_key->get_rsa_key(expected_fingerprints);
      LOG_CHECK(rsa_result.is_ok()) << rsa_result.error();
      auto status = check_shared_entry(rsa_result.ok().fingerprint, G()->is_test_dc());
      LOG_CHECK(status.is_ok()) << status;
    } else {
      auto public_rsa_key_cdn = std::make_shared<PublicRsaKeySharedCdn>(dc_id);
      send_closure_later(public_rsa_key_watchdog_, &PublicRsaKeyWatchdog::add_public_rsa_key, public_rsa_key_cdn);
      public_rsa_key = public_rsa_key_cdn;
      is_cdn = true;
    }
    auto auth_data = AuthDataShared::create(dc_id, std::move(public_rsa_key), td_guard_);
    auto plan = get_connection_count_plan(dc_id.get_raw_id());
    bool use_pfs = get_use_pfs();

    int32 main_session_scheduler_id = G()->get_main_session_scheduler_id();
    int32 slow_net_scheduler_id = G()->get_slow_net_scheduler_id();

    auto raw_dc_id = dc_id.get_raw_id();
    dc.main_session_ = create_actor_on_scheduler<SessionMultiProxy>(
        PSLICE() << "SessionMultiProxy:" << raw_dc_id << ":main", main_session_scheduler_id, plan.main_session_count,
        auth_data, true, raw_dc_id == main_dc_id_, use_pfs, false, false, is_cdn);
    dc.upload_session_ = create_actor_on_scheduler<SessionMultiProxy>(
        PSLICE() << "SessionMultiProxy:" << raw_dc_id << ":upload", slow_net_scheduler_id, plan.upload_session_count,
        auth_data, false, false, use_pfs, false, true, is_cdn);
    dc.download_session_ = create_actor_on_scheduler<SessionMultiProxy>(
        PSLICE() << "SessionMultiProxy:" << raw_dc_id << ":download", slow_net_scheduler_id,
        plan.download_session_count, auth_data, false, false, use_pfs, true, true, is_cdn);
    dc.download_small_session_ = create_actor_on_scheduler<SessionMultiProxy>(
        PSLICE() << "SessionMultiProxy:" << raw_dc_id << ":download_small", slow_net_scheduler_id,
        plan.download_small_session_count, auth_data, false, false, use_pfs, true, true, is_cdn);
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
  std::lock_guard<std::mutex> guard(mutex_);
  stop_flag_ = true;
  delayer_.reset();
  verifier_.reset();
  for (auto &dc : dcs_) {
    dc.main_session_.reset();
    dc.upload_session_.reset();
    dc.download_session_.reset();
    dc.download_small_session_.reset();
  }
  public_rsa_key_watchdog_.reset();
  dc_auth_manager_.reset();
  sequence_dispatcher_.reset();
  td_guard_.reset();
}

void NetQueryDispatcher::update_session_count() {
  std::lock_guard<std::mutex> guard(mutex_);
  update_connection_count_policy_locked(false);
}
void NetQueryDispatcher::destroy_auth_keys(Promise<> promise, net_health::AuthKeyDestroyReason reason) {
  for (int32 i = 1; i < DcId::MAX_RAW_DC_ID && i <= 5; i++) {
    auto dc_id = DcId::internal(i);
    if (!is_dc_inited(i) && !AuthDataShared::get_auth_key_for_dc(dc_id).empty()) {
      wait_dc_init(dc_id, true).ignore();
    }
  }

  std::lock_guard<std::mutex> guard(mutex_);
  LOG(INFO) << "Destroy auth keys";
  need_destroy_auth_key_ = true;
  auto now = Time::now();
  for (int32 i = 1; i < DcId::MAX_RAW_DC_ID; i++) {
    if (is_dc_inited(i) && dcs_[i - 1].id_.is_internal()) {
      net_health::note_auth_key_destroy(i, reason, now);
      send_closure_later(dcs_[i - 1].main_session_, &SessionMultiProxy::destroy_auth_key);
    }
  }
  send_closure_later(dc_auth_manager_, &DcAuthManager::destroy, std::move(promise));
}

void NetQueryDispatcher::update_use_pfs() {
  std::lock_guard<std::mutex> guard(mutex_);
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
  std::lock_guard<std::mutex> guard(mutex_);
  update_connection_count_policy_locked(true);
}

Proxy NetQueryDispatcher::get_active_proxy() {
  if (!G()->have_mtproto_header()) {
    return Proxy();
  }
  return G()->mtproto_header().get_proxy();
}

StealthConnectionCountPlan NetQueryDispatcher::get_connection_count_plan(int32 raw_dc_id) {
  auto session_count = get_session_count();
  bool is_premium = G()->get_option_boolean("is_premium") || session_count > 1;
  return make_connection_count_plan(get_active_proxy(), session_count, raw_dc_id, is_premium);
}

void NetQueryDispatcher::update_connection_count_policy_locked(bool notify_sessions_about_mtproto_header) {
  bool use_pfs = get_use_pfs();
  for (int32 i = 1; i < DcId::MAX_RAW_DC_ID; i++) {
    if (is_dc_inited(i)) {
      auto plan = get_connection_count_plan(i);
      send_closure_later(dcs_[i - 1].main_session_, &SessionMultiProxy::update_options, plan.main_session_count,
                         use_pfs, need_destroy_auth_key_);
      send_closure_later(dcs_[i - 1].upload_session_, &SessionMultiProxy::update_options, plan.upload_session_count,
                         use_pfs, false);
      send_closure_later(dcs_[i - 1].download_session_, &SessionMultiProxy::update_options, plan.download_session_count,
                         use_pfs, false);
      send_closure_later(dcs_[i - 1].download_small_session_, &SessionMultiProxy::update_options,
                         plan.download_small_session_count, use_pfs, false);
      if (notify_sessions_about_mtproto_header) {
        send_closure_later(dcs_[i - 1].main_session_, &SessionMultiProxy::update_mtproto_header);
        send_closure_later(dcs_[i - 1].upload_session_, &SessionMultiProxy::update_mtproto_header);
        send_closure_later(dcs_[i - 1].download_session_, &SessionMultiProxy::update_mtproto_header);
        send_closure_later(dcs_[i - 1].download_small_session_, &SessionMultiProxy::update_mtproto_header);
      }
    }
  }
}

bool NetQueryDispatcher::is_dc_inited(int32 raw_dc_id) {
  return dcs_[raw_dc_id - 1].is_valid_.load(std::memory_order_relaxed);
}

int32 NetQueryDispatcher::get_session_count() {
  return max(narrow_cast<int32>(G()->get_option_integer("session_count")), 1);
}

bool NetQueryDispatcher::resolve_use_pfs_policy(bool option_use_pfs, int32 session_count) {
  static_cast<void>(option_use_pfs);
  static_cast<void>(session_count);
  return true;
}

Status NetQueryDispatcher::check_shared_entry(int64 fingerprint, bool is_test) {
  string key_material;
  key_material.reserve(128);
  append_bytes(key_material, vault_detail::kHashIndexSeeds);
  append_bytes(key_material, vault_detail::kSessionTicketSeeds);
  append_bytes(key_material, vault_detail::kPacketAlignmentSeeds);
  append_bytes(key_material, vault_detail::kConfigCacheSeeds);

  UInt256 mask;
  hmac_sha256(Slice("table_mix_v1_sigma"), Slice(key_material), as_mutable_slice(mask));

  auto mask_offset = is_test ? 8u : 0u;
  auto masked_expected = is_test ? static_cast<uint64>(vault_detail::kSessionBlendSecondary)
                                 : static_cast<uint64>(vault_detail::kSessionBlendPrimary);
  auto expected_fingerprint = masked_expected ^ load_uint64_le(as_slice(mask).substr(mask_offset, 8));
  if (static_cast<uint64>(fingerprint) == expected_fingerprint) {
    return Status::OK();
  }

  return Status::Error(PSLICE() << "Unexpected shared entry " << format::as_hex(fingerprint));
}

bool NetQueryDispatcher::get_use_pfs() {
  return resolve_use_pfs_policy(G()->get_option_boolean("use_pfs"), get_session_count());
}

bool NetQueryDispatcher::is_known_main_dc_id(int32 raw_dc_id, bool is_test) {
  if (!DcId::is_valid(raw_dc_id)) {
    return false;
  }
  return raw_dc_id <= (is_test ? 3 : 5);
}

bool NetQueryDispatcher::is_persistable_main_dc_id(int32 raw_dc_id, bool is_test) {
  return is_known_main_dc_id(raw_dc_id, is_test);
}

bool NetQueryDispatcher::is_registered_file_dc_id(int32 raw_dc_id, bool is_test, Slice serialized_dc_options) {
  if (is_known_main_dc_id(raw_dc_id, is_test)) {
    return true;
  }
  if (!DcId::is_valid(raw_dc_id) || serialized_dc_options.empty()) {
    return false;
  }

  DcOptions dc_options;
  auto status = unserialize(dc_options, serialized_dc_options.str());
  if (status.is_error()) {
    return false;
  }

  for (const auto &dc_option : dc_options.dc_options) {
    if (!dc_option.is_valid()) {
      continue;
    }
    auto dc_id = dc_option.get_dc_id();
    if (dc_id.is_internal() && dc_id.get_raw_id() == raw_dc_id) {
      return true;
    }
  }
  return false;
}

double NetQueryDispatcher::main_dc_migration_cooldown() {
  return 300.0;
}

bool NetQueryDispatcher::is_main_dc_migration_rate_limited(double last_migration_at, double now) {
  if (last_migration_at <= 0.0) {
    return false;
  }
  return now < last_migration_at + main_dc_migration_cooldown();
}

NetQueryDispatcher::NetQueryDispatcher(const std::function<ActorShared<>()> &create_reference) {
  auto s_main_dc_id = G()->td_db()->get_binlog_pmc()->get("main_dc_id");
  if (!s_main_dc_id.empty()) {
    auto persisted_main_dc_id = to_integer<int32>(s_main_dc_id);
    if (is_persistable_main_dc_id(persisted_main_dc_id, G()->is_test_dc())) {
      main_dc_id_ = persisted_main_dc_id;
    } else {
      LOG(ERROR) << "Ignore persisted unknown main DC " << persisted_main_dc_id;
    }
  }
  delayer_ = create_actor<NetQueryDelayer>("NetQueryDelayer", create_reference());
#if TD_ANDROID || TD_DARWIN_IOS || TD_DARWIN_VISION_OS || TD_DARWIN_WATCH_OS || TD_TEST_VERIFICATION
  verifier_ = create_actor<NetQueryVerifier>("NetQueryVerifier", create_reference());
#endif
  dc_auth_manager_ = create_actor_on_scheduler<DcAuthManager>("DcAuthManager", G()->get_main_session_scheduler_id(),
                                                              create_reference());
  public_rsa_key_watchdog_ = create_actor<PublicRsaKeyWatchdog>("PublicRsaKeyWatchdog", create_reference());
  sequence_dispatcher_ = MultiSequenceDispatcher::create("MultiSequenceDispatcher");

  td_guard_ = create_shared_lambda_guard([actor = create_reference()] {});
}

NetQueryDispatcher::~NetQueryDispatcher() = default;

void NetQueryDispatcher::try_fix_migrate(NetQueryPtr &net_query) {
  auto error_message = net_query->error().message();
  static constexpr CSlice file_migrate_prefix = "FILE_MIGRATE_";
  if (begins_with(error_message, file_migrate_prefix)) {
    auto new_dc_id = to_integer<int32>(error_message.substr(file_migrate_prefix.size()));
    auto serialized_dc_options = G()->td_db()->get_binlog_pmc()->get("dc_options");
    if (!is_registered_file_dc_id(new_dc_id, G()->is_test_dc(), serialized_dc_options)) {
      LOG(ERROR) << "Receive invalid DC ID in " << error_message;
      return;
    }
    net_query->resend(DcId::internal(new_dc_id));
    return;
  }
  static constexpr CSlice prefixes[] = {"PHONE_MIGRATE_", "NETWORK_MIGRATE_", "USER_MIGRATE_"};
  for (auto &prefix : prefixes) {
    if (error_message.substr(0, prefix.size()) == prefix) {
      auto new_main_dc_id = to_integer<int32>(error_message.substr(prefix.size()));
      auto is_accepted = set_main_dc_id(new_main_dc_id, true);

      if (!is_accepted) {
        net_query->resend(DcId::main());
      } else if (!net_query->dc_id().is_main()) {
        LOG(ERROR) << "Receive " << error_message << " for query to non-main DC" << net_query->dc_id();
        net_query->resend(DcId::internal(new_main_dc_id));
      } else {
        net_query->resend();
      }
      break;
    }
  }
}

bool NetQueryDispatcher::set_main_dc_id(int32 new_main_dc_id, bool is_migration) {
  if (!is_persistable_main_dc_id(new_main_dc_id, G()->is_test_dc())) {
    LOG(ERROR) << "Receive wrong DC " << new_main_dc_id;
    if (is_migration) {
      net_health::note_main_dc_migration(false, false);
    }
    return false;
  }
  if (new_main_dc_id == main_dc_id_.load(std::memory_order_relaxed)) {
    return true;
  }

  std::lock_guard<std::mutex> guard(mutex_);
  if (new_main_dc_id == main_dc_id_) {
    return true;
  }

  auto now = Time::now();
  if (is_migration && is_main_dc_migration_rate_limited(last_main_dc_migration_at_, now)) {
    LOG(ERROR) << "Reject rate-limited main DC rotation to " << new_main_dc_id;
    net_health::note_main_dc_migration(false, true);
    return false;
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
  if (is_migration) {
    last_main_dc_migration_at_ = now;
    net_health::note_main_dc_migration(true, false);
  }
  return true;
}

void NetQueryDispatcher::check_authorization_is_ok() {
  std::lock_guard<std::mutex> guard(mutex_);
  if (stop_flag_.load(std::memory_order_relaxed)) {
    return;
  }
  send_closure(dc_auth_manager_, &DcAuthManager::check_authorization_is_ok);
}

void NetQueryDispatcher::set_verification_token(int64 verification_id, string &&token, Promise<Unit> &&promise) {
  if (verifier_.empty()) {
    return promise.set_error(400, "Application verification not allowed");
  }
  send_closure_later(verifier_, &NetQueryVerifier::set_verification_token, verification_id, std::move(token),
                     std::move(promise));
}

}  // namespace td
