//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/net/Session.h"

#include "td/telegram/telegram_api.h"

#include "td/telegram/DhCache.h"
#include "td/telegram/Global.h"
#include "td/telegram/net/MtprotoHeader.h"
#include "td/telegram/net/NetQuery.h"
#include "td/telegram/net/NetQueryDispatcher.h"
#include "td/telegram/net/NetType.h"
#include "td/telegram/StateManager.h"
#include "td/telegram/UniqueId.h"

#include "td/mtproto/Handshake.h"
#include "td/mtproto/HandshakeActor.h"
#include "td/mtproto/RawConnection.h"
#include "td/mtproto/SessionConnection.h"

#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Random.h"
#include "td/utils/Slice.h"
#include "td/utils/Time.h"
#include "td/utils/Timer.h"
#include "td/utils/tl_parsers.h"

#include <algorithm>
#include <tuple>
#include <utility>

namespace td {

namespace detail {

class GenAuthKeyActor : public Actor {
 public:
  GenAuthKeyActor(std::unique_ptr<mtproto::AuthKeyHandshake> handshake,
                  std::unique_ptr<mtproto::AuthKeyHandshakeContext> context,
                  Promise<std::unique_ptr<mtproto::RawConnection>> connection_promise,
                  Promise<std::unique_ptr<mtproto::AuthKeyHandshake>> handshake_promise,
                  std::shared_ptr<Session::Callback> callback)
      : handshake_(std::move(handshake))
      , context_(std::move(context))
      , connection_promise_(std::move(connection_promise))
      , handshake_promise_(std::move(handshake_promise))
      , callback_(std::move(callback)) {
  }

  void on_network(uint32 network_generation) {
    if (network_generation_ != network_generation) {
      send_closure(std::move(child_), &mtproto::HandshakeActor::close);
    }
  }

 private:
  uint32 network_generation_ = 0;
  std::unique_ptr<mtproto::AuthKeyHandshake> handshake_;
  std::unique_ptr<mtproto::AuthKeyHandshakeContext> context_;
  Promise<std::unique_ptr<mtproto::RawConnection>> connection_promise_;
  Promise<std::unique_ptr<mtproto::AuthKeyHandshake>> handshake_promise_;
  std::shared_ptr<Session::Callback> callback_;
  CancellationToken cancellation_token_{true};

  ActorOwn<mtproto::HandshakeActor> child_;

  void start_up() override {
    // Bug in Android clang and MSVC
    // std::tuple<Result<int>> b(std::forward_as_tuple(Result<int>()));

    callback_->request_raw_connection(PromiseCreator::cancellable_lambda(
        cancellation_token_,
        [actor_id = actor_id(this)](Result<std::unique_ptr<mtproto::RawConnection>> r_raw_connection) {
          send_closure(actor_id, &GenAuthKeyActor::on_connection, std::move(r_raw_connection), false);
        }));
  }

  void hangup() override {
    if (connection_promise_) {
      connection_promise_.set_error(Status::Error(1, "Cancelled"));
    }
    if (handshake_promise_) {
      handshake_promise_.set_error(Status::Error(1, "Cancelled"));
    }
    stop();
  }

  void on_connection(Result<std::unique_ptr<mtproto::RawConnection>> r_raw_connection, bool dummy) {
    if (r_raw_connection.is_error()) {
      connection_promise_.set_error(r_raw_connection.move_as_error());
      handshake_promise_.set_value(std::move(handshake_));
      return;
    }

    auto raw_connection = r_raw_connection.move_as_ok();
    VLOG(dc) << "Receive raw connection " << raw_connection.get();
    network_generation_ = raw_connection->extra_;
    child_ = create_actor_on_scheduler<mtproto::HandshakeActor>(
        "HandshakeActor", G()->get_slow_net_scheduler_id(), std::move(handshake_), std::move(raw_connection),
        std::move(context_), 10, std::move(connection_promise_), std::move(handshake_promise_));
  }
};

}  // namespace detail

Session::Session(unique_ptr<Callback> callback, std::shared_ptr<AuthDataShared> shared_auth_data, int32 dc_id,
                 bool is_main, bool use_pfs, bool is_cdn, const mtproto::AuthKey &tmp_auth_key,
                 std::vector<mtproto::ServerSalt> server_salts)
    : dc_id_(dc_id), is_main_(is_main), is_cdn_(is_cdn) {
  VLOG(dc) << "Start connection";

  shared_auth_data_ = std::move(shared_auth_data);
  auth_data_.set_use_pfs(use_pfs);
  auth_data_.set_main_auth_key(shared_auth_data_->get_auth_key());
  auth_data_.set_server_time_difference(shared_auth_data_->get_server_time_difference());
  auth_data_.set_future_salts(shared_auth_data_->get_future_salts(), Time::now());
  if (use_pfs && !tmp_auth_key.empty()) {
    auth_data_.set_tmp_auth_key(tmp_auth_key);
    auth_data_.set_future_salts(std::move(server_salts), Time::now());
  }
  uint64 session_id = 0;
  Random::secure_bytes(reinterpret_cast<uint8 *>(&session_id), sizeof(session_id));
  auth_data_.session_id_ = session_id;
  LOG(WARNING) << "Generate new session_id " << session_id << " for " << (use_pfs ? "temp " : "")
               << (is_cdn ? "CDN " : "") << "auth key " << auth_data_.get_auth_key().id() << " for DC" << dc_id;

  callback_ = std::shared_ptr<Callback>(callback.release());

  main_connection_.connection_id = 0;
  long_poll_connection_.connection_id = 1;

  if (is_cdn) {
    auth_data_.set_header(G()->mtproto_header().get_anonymous_header().str());
  } else {
    auth_data_.set_header(G()->mtproto_header().get_default_header().str());
  }
  last_activity_timestamp_ = Time::now();
}

void Session::start_up() {
  class StateCallback : public StateManager::Callback {
   public:
    explicit StateCallback(ActorId<Session> session) : session_(std::move(session)) {
    }
    bool on_network(NetType network_type, uint32 network_generation) override {
      send_closure(session_, &Session::on_network, network_type != NetType::None, network_generation);
      return session_.is_alive();
    }
    bool on_online(bool online_flag) override {
      send_closure(session_, &Session::on_online, online_flag);
      return session_.is_alive();
    }

   private:
    ActorId<Session> session_;
  };
  send_closure(G()->state_manager(), &StateManager::add_callback, make_unique<StateCallback>(actor_id(this)));

  yield();
}

void Session::on_network(bool network_flag, uint32 network_generation) {
  was_on_network_ = true;
  network_flag_ = network_flag;
  if (network_generation_ != network_generation) {
    network_generation_ = network_generation;
    connection_close(&main_connection_);
    connection_close(&long_poll_connection_);
  }

  for (auto &handshake_info : handshake_info_) {
    if (handshake_info.actor_.empty()) {
      continue;
    }
    send_closure(handshake_info.actor_, &detail::GenAuthKeyActor::on_network, network_generation);
  }

  loop();
}

void Session::on_online(bool online_flag) {
  online_flag_ = online_flag;
  connection_online_update(true);
  loop();
}

void Session::connection_online_update(bool force) {
  bool new_connection_online_flag =
      online_flag_ && (has_queries() || last_activity_timestamp_ + 10 > Time::now_cached() || is_main_);
  if (connection_online_flag_ == new_connection_online_flag && !force) {
    return;
  }
  connection_online_flag_ = new_connection_online_flag;
  VLOG(dc) << "Set connection_online " << connection_online_flag_;
  if (is_main_) {
    if (main_connection_.connection) {
      main_connection_.connection->set_online(connection_online_flag_);
    }
    if (long_poll_connection_.connection) {
      long_poll_connection_.connection->set_online(connection_online_flag_);
    }
  } else {
    // TODO: support online state in media connections.
    if (connection_online_flag_) {
      connection_close(&main_connection_);
      connection_close(&long_poll_connection_);
    }
    return;
  }
}

void Session::send(NetQueryPtr &&query) {
  last_activity_timestamp_ = Time::now();

  query->debug("Session: received from SessionProxy");
  query->set_session_id(auth_data_.session_id_);
  VLOG(net_query) << "got query " << query;
  if (query->update_is_ready()) {
    return_query(std::move(query));
    return;
  }
  add_query(std::move(query));
  loop();
}

void Session::on_result(NetQueryPtr query) {
  CHECK(UniqueId::extract_type(query->id()) == UniqueId::BindKey);
  if (last_bind_id_ != query->id()) {
    query->clear();
    return;
  }

  LOG(INFO) << "ANSWER TO BindKey" << query;
  Status status;
  tmp_auth_key_id_ = 0;
  last_bind_id_ = 0;
  if (query->is_error()) {
    status = std::move(query->error());
  } else {
    auto r_flag = fetch_result<telegram_api::auth_bindTempAuthKey>(query->ok());
    if (r_flag.is_error()) {
      status = r_flag.move_as_error();
    } else {
      auto flag = r_flag.move_as_ok();
      if (!flag) {
        status = Status::Error("Returned false");
      }
    }
  }
  if (status.is_ok()) {
    LOG(INFO) << "BOUND!" << tag("tmp_id", auth_data_.get_tmp_auth_key().id());
    auth_data_.on_bind();
    on_tmp_auth_key_updated();
  } else {
    LOG(ERROR) << "BindKey failed: " << status;
  }
  query->clear();
  yield();
}

void Session::return_query(NetQueryPtr &&query) {
  last_activity_timestamp_ = Time::now();

  query->set_session_id(0);
  G()->net_query_dispatcher().dispatch(std::move(query));
}

void Session::flush_pending_invoke_after_queries() {
  while (!pending_invoke_after_queries_.empty()) {
    auto &query = pending_invoke_after_queries_.front();
    pending_queries_.push_back(std::move(query));
    pending_invoke_after_queries_.pop_front();
  }
}

void Session::close() {
  close_flag_ = true;
  connection_close(&main_connection_);
  connection_close(&long_poll_connection_);

  for (auto &it : sent_queries_) {
    auto &query = it.second.query;
    query->set_message_id(0);
    query->cancel_slot_.clear_event();
    pending_queries_.push_back(std::move(query));
  }
  sent_queries_.clear();
  sent_containers_.clear();

  flush_pending_invoke_after_queries();
  CHECK(sent_queries_.empty());
  while (!pending_queries_.empty()) {
    auto &query = pending_queries_.front();
    query->set_error_resend();
    return_query(std::move(query));
    pending_queries_.pop_front();
  }

  callback_->on_closed();
  stop();
}

void Session::hangup() {
  LOG(DEBUG) << "HANGUP";
  close();
}

void Session::raw_event(const Event::Raw &event) {
  auto message_id = event.u64;
  auto it = sent_queries_.find(message_id);
  if (it == sent_queries_.end()) {
    return;
  }

  dec_container(it->first, &it->second);
  mark_as_known(it->first, &it->second);

  auto query = std::move(it->second.query);
  query->set_message_id(0);
  query->cancel_slot_.clear_event();
  sent_queries_.erase(it);
  return_query(std::move(query));

  LOG(DEBUG) << "Drop answer " << tag("message_id", format::as_hex(message_id));
  if (main_connection_.state == ConnectionInfo::State::Ready) {
    main_connection_.connection->cancel_answer(message_id);
  } else {
    to_cancel_.push_back(message_id);
  }
  loop();
}

/** Connection::Callback **/
void Session::on_connected() {
  if (is_main_) {
    connection_token_ = StateManager::connection(G()->state_manager());
  }
}
Status Session::on_pong() {
  constexpr int MAX_QUERY_TIMEOUT = 60;
  constexpr int MIN_CONNECTION_ACTIVE = 60;
  if (current_info_ == &main_connection_ &&
      Timestamp::at(current_info_->created_at + MIN_CONNECTION_ACTIVE).is_in_past()) {
    Status status;
    if (!unknown_queries_.empty()) {
      status = Status::Error(PSLICE() << "No state info for " << unknown_queries_.size() << " queries for "
                                      << format::as_time(Time::now_cached() - current_info_->created_at));
    }
    if (!sent_queries_list_.empty()) {
      for (auto it = sent_queries_list_.prev; it != &sent_queries_list_; it = it->prev) {
        auto query = Query::from_list_node(it);
        if (Timestamp::at(query->sent_at_ + MAX_QUERY_TIMEOUT).is_in_past()) {
          if (status.is_ok()) {
            status = Status::Error(PSLICE() << "No answer for " << query->query << " for "
                                            << format::as_time(Time::now_cached() - query->sent_at_));
          }
          query->ack = false;
        } else {
          break;
        }
      }
      if (status.is_error()) {
        return status;
      }
    }
  }
  return Status::OK();
}
void Session::on_auth_key_updated() {
  shared_auth_data_->set_auth_key(auth_data_.get_main_auth_key());
}
void Session::on_tmp_auth_key_updated() {
  callback_->on_tmp_auth_key_updated(auth_data_.get_tmp_auth_key());
}

void Session::on_server_salt_updated() {
  if (auth_data_.use_pfs()) {
    callback_->on_server_salt_updated(auth_data_.get_future_salts());
    return;
  }
  shared_auth_data_->set_future_salts(auth_data_.get_future_salts());
}

void Session::on_server_time_difference_updated() {
  shared_auth_data_->update_server_time_difference(auth_data_.get_server_time_difference());
}

void Session::on_before_close() {
  unsubscribe_before_close(current_info_->connection->get_pollable());
}

void Session::on_closed(Status status) {
  if (!close_flag_ && is_main_) {
    connection_token_.reset();
  }

  if (status.is_error()) {
    LOG(WARNING) << "on_closed: " << status << " " << current_info_->connection->get_name();
  } else {
    LOG(INFO) << "on_closed: " << status << " " << current_info_->connection->get_name();
  }

  if (status.is_error() && status.code() == -404) {
    if (auth_data_.use_pfs()) {
      LOG(WARNING) << "Invalidate tmp_key";
      auth_data_.drop_tmp_auth_key();
      on_tmp_auth_key_updated();
      yield();
    } else if (is_cdn_) {
      LOG(WARNING) << "Invalidate cdn tmp_key";
      auth_data_.drop_main_auth_key();
      on_auth_key_updated();
      on_session_failed(std::move(status));
    }
  }

  // resend all queries without ack.
  for (auto it = sent_queries_.begin(); it != sent_queries_.end();) {
    if (!it->second.ack && it->second.connection_id == current_info_->connection_id) {
      // container vector leak otherwise
      cleanup_container(it->first, &it->second);

      // mark query as unknown
      if (status.is_error() && status.code() == 500) {
        cleanup_container(it->first, &it->second);
        mark_as_known(it->first, &it->second);

        auto &query = it->second.query;
        VLOG(net_query) << "resend query (on_disconnected, no ack) " << query;
        query->set_message_id(0);
        query->cancel_slot_.clear_event();
        query->set_error(Status::Error(500, PSLICE() << "Session failed: " << status.message()),
                         current_info_->connection->get_name().str());
        return_query(std::move(query));
        it = sent_queries_.erase(it);
      } else {
        mark_as_unknown(it->first, &it->second);
        ++it;
      }
    } else {
      ++it;
    }
  }

  current_info_->connection.reset();
  current_info_->state = ConnectionInfo::State::Empty;
}

void Session::on_session_created(uint64 unique_id, uint64 first_id) {
  // TODO: use unique_id
  // send updatesTooLong to force getDifference
  LOG(INFO) << "new_session_created " << unique_id << " " << first_id;
  if (is_main_) {
    LOG(DEBUG) << "Sending updatesTooLong to force getDifference";
    telegram_api::updatesTooLong too_long_;
    auto storer = create_storer(too_long_);
    BufferSlice packet(storer.size());
    auto real_size = storer.store(packet.as_slice().ubegin());
    CHECK(real_size == packet.size());
    return_query(G()->net_query_creator().create_result(0, std::move(packet)));
  }

  for (auto it = sent_queries_.begin(); it != sent_queries_.end();) {
    Query *query_ptr = &it->second;
    if (query_ptr->container_id < first_id) {
      // container vector leak otherwise
      cleanup_container(it->first, &it->second);
      mark_as_known(it->first, &it->second);

      auto &query = it->second.query;
      VLOG(net_query) << "resend query (on_session_created) " << query;
      query->set_message_id(0);
      query->cancel_slot_.clear_event();
      resend_query(std::move(query));
      it = sent_queries_.erase(it);
    } else {
      ++it;
    }
  }
}

void Session::on_session_failed(Status status) {
  if (status.is_error()) {
    LOG(WARNING) << "Session failed: " << status;
  } else {
    LOG(INFO) << "Session will be closed soon";
  }
  // this connection will be closed soon
  close_flag_ = true;
  callback_->on_failed();
}

void Session::on_container_sent(uint64 container_id, vector<uint64> msg_ids) {
  auto erase_from = std::remove_if(msg_ids.begin(), msg_ids.end(), [&](uint64 msg_id) {
    auto it = sent_queries_.find(msg_id);
    if (it == sent_queries_.end()) {
      return true;  // remove
    }
    it->second.container_id = container_id;
    return false;
  });
  msg_ids.erase(erase_from, msg_ids.end());
  if (msg_ids.empty()) {
    return;
  }
  auto size = msg_ids.size();
  sent_containers_.emplace(container_id, ContainerInfo{size, std::move(msg_ids)});
}

void Session::on_message_ack(uint64 id) {
  on_message_ack_impl(id, 1);
}
void Session::on_message_ack_impl(uint64 id, int32 type) {
  auto cit = sent_containers_.find(id);
  if (cit != sent_containers_.end()) {
    auto container_info = std::move(cit->second);
    for (auto message_id : container_info.message_ids) {
      on_message_ack_impl_inner(message_id, type, true);
    }
    sent_containers_.erase(cit);
    return;
  }

  on_message_ack_impl_inner(id, type, false);
}

void Session::on_message_ack_impl_inner(uint64 id, int32 type, bool in_container) {
  auto it = sent_queries_.find(id);
  if (it == sent_queries_.end()) {
    return;
  }
  VLOG(net_query) << "Ack " << tag("msg_id", id) << it->second.query;
  it->second.ack = true;
  it->second.query->debug_ack |= type;
  it->second.query->quick_ack_promise_.set_value(Unit());
  if (!in_container) {
    cleanup_container(id, &it->second);
  }
  mark_as_known(it->first, &it->second);
}

void Session::dec_container(uint64 message_id, Query *query) {
  if (query->container_id == message_id) {
    // message was sent without any container
    return;
  }
  auto it = sent_containers_.find(query->container_id);
  if (it == sent_containers_.end()) {
    return;
  }
  CHECK(it->second.ref_cnt > 0);
  it->second.ref_cnt--;
  if (it->second.ref_cnt == 0) {
    sent_containers_.erase(it);
  }
}
void Session::cleanup_container(uint64 message_id, Query *query) {
  if (query->container_id == message_id) {
    // message was sent without any container
    return;
  }

  // we can forget container now, since we have an answer for its part.
  // TODO: we can do it only for one element per container
  sent_containers_.erase(query->container_id);
}

void Session::mark_as_known(uint64 id, Query *query) {
  query->query->debug_unknown = false;
  if (!query->unknown) {
    return;
  }
  VLOG(net_query) << "Mark as known " << tag("msg_id", id) << query->query;
  query->unknown = false;
  unknown_queries_.erase(id);
  if (unknown_queries_.empty()) {
    flush_pending_invoke_after_queries();
  }
}

void Session::mark_as_unknown(uint64 id, Query *query) {
  query->query->debug_unknown = true;
  if (query->unknown) {
    return;
  }
  VLOG(net_query) << "Mark as unknown " << tag("msg_id", id) << query->query;
  query->unknown = true;
  unknown_queries_.insert(id);
}

Status Session::on_message_result_ok(uint64 id, BufferSlice packet, size_t original_size) {
  // Steal authorization information.
  // It is a dirty hack, yep.
  TlParser parser(packet.as_slice());
  int32 ID = parser.fetch_int();
  if (!parser.get_error()) {
    if (ID == telegram_api::auth_authorization::ID) {
      LOG(INFO) << "GOT AUTHORIZATION!";
      auth_data_.set_auth_flag(true);
      shared_auth_data_->set_auth_key(auth_data_.get_main_auth_key());
    }
  }

  if (id == 0) {
    if (is_cdn_) {
      return Status::Error("Got update from CDN connection");
    }
    return_query(G()->net_query_creator().create_result(0, std::move(packet)));
    return Status::OK();
  }
  auto it = sent_queries_.find(id);
  if (it == sent_queries_.end()) {
    LOG(DEBUG) << "DROP result to " << tag("request_id", format::as_hex(id)) << tag("tl", format::as_hex(ID));

    if (packet.size() > 16 * 1024) {
      dropped_size_ += packet.size();
      if (dropped_size_ > (256 * 1024)) {
        auto dropped_size = dropped_size_;
        dropped_size_ = 0;
        return Status::Error(
            2, PSLICE() << "Too much dropped packets " << tag("total_size", format::as_size(dropped_size)));
      }
    }
    return Status::OK();
  }
  auth_data_.on_api_response();
  Query *query_ptr = &it->second;
  VLOG(net_query) << "Return query result " << query_ptr->query;

  cleanup_container(id, query_ptr);
  mark_as_known(id, query_ptr);
  query_ptr->query->on_net_read(original_size);
  query_ptr->query->set_ok(std::move(packet));
  query_ptr->query->set_message_id(0);
  query_ptr->query->cancel_slot_.clear_event();
  return_query(std::move(query_ptr->query));

  sent_queries_.erase(it);
  return Status::OK();
}

void Session::on_message_result_error(uint64 id, int error_code, BufferSlice message) {
  // UNAUTHORIZED
  // TODO: some errors shouldn't cause loss of authorizations. Especially when PFS will be used
  if (error_code == 401 && message.as_slice() != CSlice("SESSION_PASSWORD_NEEDED")) {
    if (auth_data_.use_pfs() && message.as_slice() == CSlice("AUTH_KEY_PERM_EMPTY")) {
      LOG(ERROR) << "Receive AUTH_KEY_PERM_EMPTY in session " << auth_data_.session_id_ << " for auth key "
                 << auth_data_.get_tmp_auth_key().id();
      auth_data_.drop_tmp_auth_key();
      on_tmp_auth_key_updated();
      error_code = 500;
    } else {
      LOG(WARNING) << "Lost authorization due to " << tag("msg", message.as_slice());
      auth_data_.set_auth_flag(false);
      shared_auth_data_->set_auth_key(auth_data_.get_main_auth_key());
      auth_lost_flag_ = true;
    }
  }

  if (id == 0) {
    LOG(WARNING) << "Session got error update";
    return;
  }

  LOG(DEBUG) << "Session::on_error " << tag("id", id) << tag("error_code", error_code)
             << tag("msg", message.as_slice());
  auto it = sent_queries_.find(id);
  if (it == sent_queries_.end()) {
    return;
  }

  Query *query_ptr = &it->second;
  VLOG(net_query) << "return query error " << query_ptr->query;

  cleanup_container(id, query_ptr);
  mark_as_known(id, query_ptr);
  query_ptr->query->set_error(Status::Error(error_code, message.as_slice()),
                              current_info_->connection->get_name().str());
  query_ptr->query->set_message_id(0);
  query_ptr->query->cancel_slot_.clear_event();
  return_query(std::move(query_ptr->query));

  sent_queries_.erase(it);
}

void Session::on_message_failed_inner(uint64 id, bool in_container) {
  LOG(INFO) << "message inner failed " << id;
  auto it = sent_queries_.find(id);
  if (it == sent_queries_.end()) {
    return;
  }

  Query *query_ptr = &it->second;
  if (!in_container) {
    cleanup_container(id, query_ptr);
  }
  mark_as_known(id, query_ptr);

  query_ptr->query->set_message_id(0);
  query_ptr->query->cancel_slot_.clear_event();
  query_ptr->query->debug_send_failed();
  resend_query(std::move(query_ptr->query));
  sent_queries_.erase(it);
}

void Session::on_message_failed(uint64 id, Status status) {
  LOG(INFO) << "on_message_failed " << tag("id", id) << tag("status", status);
  status.ignore();

  auto cit = sent_containers_.find(id);
  if (cit != sent_containers_.end()) {
    auto container_info = std::move(cit->second);
    for (auto message_id : container_info.message_ids) {
      on_message_failed_inner(message_id, true);
    }
    sent_containers_.erase(cit);
    return;
  }

  on_message_failed_inner(id, false);
}

void Session::on_message_info(uint64 id, int32 state, uint64 answer_id, int32 answer_size) {
  auto it = sent_queries_.find(id);
  if (it != sent_queries_.end()) {
    if (it->second.query->update_is_ready()) {
      dec_container(it->first, &it->second);
      mark_as_known(it->first, &it->second);

      auto query = std::move(it->second.query);
      query->set_message_id(0);
      query->cancel_slot_.clear_event();
      sent_queries_.erase(it);
      return_query(std::move(query));
      return;
    }
  }
  if (id != 0) {
    if (it == sent_queries_.end()) {
      return;
    }
    switch (state & 7) {
      case 1:
      case 2:
      case 3:
        // message not received by server
        return on_message_failed(id, Status::Error("Unknown message id"));
      case 0:
        if (answer_id == 0) {
          LOG(ERROR) << "Unexpected message_info.state == 0 " << tag("id", id) << tag("state", state)
                     << tag("answer_id", answer_id);
          return on_message_failed(id, Status::Error("Unexpected message_info.state == 0"));
        }
      // fallthrough
      case 4:
        on_message_ack_impl(id, 2);
        break;
      default:
        LOG(ERROR) << "Invalid message info " << tag("state", state);
    }
  }

  // ok, we are waiting for result of id. let's ask to resend it
  if (answer_id != 0) {
    if (it != sent_queries_.end()) {
      VLOG_IF(net_query, id != 0) << "Resend answer " << tag("msg_id", id) << tag("answer_id", answer_id)
                                  << tag("answer_size", answer_size) << it->second.query;
      it->second.query->debug("Session: resend answer");
    }
    current_info_->connection->resend_answer(answer_id);
  }
}

bool Session::has_queries() const {
  return !pending_invoke_after_queries_.empty() || !pending_queries_.empty() || !sent_queries_.empty();
}

void Session::resend_query(NetQueryPtr query) {
  if (UniqueId::extract_type(query->id()) == UniqueId::BindKey) {
    query->set_error_resend();
    return_query(std::move(query));
  } else {
    add_query(std::move(query));
  }
}

void Session::add_query(NetQueryPtr &&net_query) {
  net_query->debug("Session: pending");
  LOG_IF(FATAL, UniqueId::extract_type(net_query->id()) == UniqueId::BindKey)
      << "Add BindKey query inpo pending_queries_";
  pending_queries_.emplace_back(std::move(net_query));
}

void Session::connection_send_query(ConnectionInfo *info, NetQueryPtr &&net_query, uint64 message_id) {
  net_query->debug("Session: try send to mtproto::connection");
  CHECK(info->state == ConnectionInfo::State::Ready);
  current_info_ = info;

  if (net_query->update_is_ready()) {
    return return_query(std::move(net_query));
  }

  uint64 invoke_after_id = 0;
  NetQueryRef invoke_after = net_query->invoke_after();
  if (!invoke_after.empty()) {
    invoke_after_id = invoke_after->message_id();
    if (invoke_after->session_id() != auth_data_.session_id_ || invoke_after_id == 0) {
      net_query->set_error_resend_invoke_after();
      return return_query(std::move(net_query));
    }
    if (!unknown_queries_.empty()) {
      pending_invoke_after_queries_.push_back(std::move(net_query));
      return;
    }
  }

  net_query->debug("Session: send to mtproto::connection");
  auto r_message_id =
      info->connection->send_query(net_query->query().clone(), net_query->gzip_flag() == NetQuery::GzipFlag::On,
                                   message_id, invoke_after_id, static_cast<bool>(net_query->quick_ack_promise_));

  net_query->on_net_write(net_query->query().size());

  if (r_message_id.is_error()) {
    LOG(FATAL) << "Failed to send query: " << r_message_id.error();
  }
  message_id = r_message_id.ok();
  VLOG(net_query) << "send query to connection " << net_query << " [msg_id:" << format::as_hex(message_id) << "]"
                  << tag("invoke_after", format::as_hex(invoke_after_id));
  net_query->set_message_id(message_id);
  net_query->cancel_slot_.clear_event();
  CHECK(sent_queries_.find(message_id) == sent_queries_.end()) << message_id;
  net_query->debug_unknown = false;
  net_query->debug_ack = 0;
  if (!net_query->cancel_slot_.empty()) {
    LOG(DEBUG) << "set event for net_query cancellation " << tag("message_id", format::as_hex(message_id));
    net_query->cancel_slot_.set_event(EventCreator::raw(actor_id(), message_id));
  }
  auto status = sent_queries_.emplace(
      message_id, Query{message_id, std::move(net_query), main_connection_.connection_id, Time::now_cached()});
  sent_queries_list_.put(status.first->second.get_list_node());
  if (!status.second) {
    LOG(FATAL) << "Duplicate message_id oO [message_id=" << message_id << "]";
  }
}

void Session::connection_open(ConnectionInfo *info, bool ask_info) {
  CHECK(info->state == ConnectionInfo::State::Empty);
  if (!network_flag_) {
    return;
  }
  if (!auth_data_.has_auth_key(Time::now_cached())) {
    return;
  }
  info->ask_info = ask_info;

  info->state = ConnectionInfo::State::Connecting;
  info->cancellation_token_ = CancellationToken{true};
  // NB: rely on constant location of info
  auto promise = PromiseCreator::cancellable_lambda(
      info->cancellation_token_,
      [actor_id = actor_id(this), info = info](Result<std::unique_ptr<mtproto::RawConnection>> res) {
        send_closure(actor_id, &Session::connection_open_finish, info, std::move(res));
      });

  if (cached_connection_) {
    VLOG(dc) << "Reuse cached connection";
    promise.set_value(std::move(cached_connection_));
  } else {
    VLOG(dc) << "Request new connection";
    callback_->request_raw_connection(std::move(promise));
  }

  info->wakeup_at = Time::now_cached() + 1000;
}

void Session::connection_add(std::unique_ptr<mtproto::RawConnection> raw_connection) {
  VLOG(dc) << "Cache connection " << raw_connection.get();
  cached_connection_ = std::move(raw_connection);
  cached_connection_timestamp_ = Time::now();
}

void Session::connection_check_mode(ConnectionInfo *info) {
  if (close_flag_ || info->state != ConnectionInfo::State::Ready) {
    return;
  }
  if (info->mode != mode_) {
    LOG(WARNING) << "Close connection because of outdated mode_";
    connection_close(info);
  }
}

void Session::connection_open_finish(ConnectionInfo *info,
                                     Result<std::unique_ptr<mtproto::RawConnection>> r_raw_connection) {
  if (close_flag_ || info->state != ConnectionInfo::State::Connecting) {
    VLOG(dc) << "Ignore raw connection while closing";
    return;
  }
  current_info_ = info;
  if (r_raw_connection.is_error()) {
    LOG(WARNING) << "Failed to open socket: " << r_raw_connection.error();
    info->state = ConnectionInfo::State::Empty;
    yield();
    return;
  }

  auto raw_connection = r_raw_connection.move_as_ok();
  VLOG(dc) << "Receive raw connection " << raw_connection.get();
  if (raw_connection->extra_ != network_generation_) {
    LOG(WARNING) << "Got RawConnection with old network_generation";
    info->state = ConnectionInfo::State::Empty;
    yield();
    return;
  }

  Mode expected_mode =
      raw_connection->get_transport_type().type == mtproto::TransportType::Http ? Mode::Http : Mode::Tcp;
  if (mode_ != expected_mode) {
    VLOG(dc) << "Change mode " << mode_ << "--->" << expected_mode;
    mode_ = expected_mode;
    if (info->connection_id == 1 && mode_ != Mode::Http) {
      LOG(WARNING) << "Got tcp connection for long poll connection";
      connection_add(std::move(raw_connection));
      info->state = ConnectionInfo::State::Empty;
      yield();
      return;
    }
  }

  // mtproto::TransportType transport_type = raw_connection->get_transport_type();
  mtproto::SessionConnection::Mode mode;
  Slice mode_name;
  if (mode_ == Mode::Tcp) {
    mode = mtproto::SessionConnection::Mode::Tcp;
    mode_name = "Tcp";
  } else {
    if (info->connection_id == 0) {
      mode = mtproto::SessionConnection::Mode::Http;
      mode_name = "Http";
    } else {
      mode = mtproto::SessionConnection::Mode::HttpLongPoll;
      mode_name = "HttpLongPoll";
    }
  }
  auto name = PSTRING() << get_name() << "::Connect::" << mode_name << "::" << raw_connection->debug_str_;
  info->connection =
      make_unique<mtproto::SessionConnection>(mode, std::move(raw_connection), &auth_data_, DhCache::instance());
  if (is_main_) {
    info->connection->set_online(connection_online_flag_);
  }
  info->connection->set_name(name);
  info->connection->get_pollable().set_observer(this);
  subscribe(info->connection->get_pollable());
  info->mode = mode_;
  info->state = ConnectionInfo::State::Ready;
  info->created_at = Time::now_cached();
  info->wakeup_at = Time::now_cached() + 10;
  if (unknown_queries_.size() > 1024) {
    on_session_failed(Status::Error("Too much queries with unknown state"));
    return;
  }
  if (info->ask_info) {
    for (auto &id : unknown_queries_) {
      info->connection->get_state_info(id);
    }
    for (auto &id : to_cancel_) {
      info->connection->cancel_answer(id);
    }
    to_cancel_.clear();
  }
  yield();
}

void Session::connection_flush(ConnectionInfo *info) {
  CHECK(info->state == ConnectionInfo::State::Ready);
  current_info_ = info;
  info->wakeup_at = info->connection->flush(static_cast<mtproto::SessionConnection::Callback *>(this));
}

void Session::connection_close(ConnectionInfo *info) {
  current_info_ = info;
  if (info->state != ConnectionInfo::State::Ready) {
    return;
  }
  info->connection->force_close(static_cast<mtproto::SessionConnection::Callback *>(this));
  CHECK(info->state == ConnectionInfo::State::Empty);
}

bool Session::need_send_bind_key() {
  return auth_data_.use_pfs() && !auth_data_.get_bind_flag() && auth_data_.get_tmp_auth_key().id() != tmp_auth_key_id_;
}
bool Session::need_send_query() {
  return !close_flag_ && (!auth_data_.use_pfs() || auth_data_.get_bind_flag()) && !pending_queries_.empty();
}
bool Session::connection_send_bind_key(ConnectionInfo *info) {
  CHECK(info->state != ConnectionInfo::State::Empty);
  uint64 key_id = auth_data_.get_tmp_auth_key().id();
  if (key_id == tmp_auth_key_id_) {
    return false;
  }
  tmp_auth_key_id_ = key_id;
  last_bind_id_ = UniqueId::next(UniqueId::BindKey);

  int64 perm_auth_key_id = auth_data_.get_main_auth_key().id();
  int64 nonce = Random::secure_int64();
  int32 expire_at = static_cast<int32>(auth_data_.get_server_time(auth_data_.get_tmp_auth_key().expire_at()));
  int64 message_id;
  BufferSlice encrypted;
  std::tie(message_id, encrypted) = info->connection->encrypted_bind(perm_auth_key_id, nonce, expire_at);

  LOG(INFO) << "Bind key: " << tag("tmp", key_id) << tag("perm", static_cast<uint64>(perm_auth_key_id));
  NetQueryPtr query = G()->net_query_creator().create(
      last_bind_id_,
      create_storer(telegram_api::auth_bindTempAuthKey(perm_auth_key_id, nonce, expire_at, std::move(encrypted))));
  query->dispatch_ttl = 0;
  query->set_callback(actor_shared(this));
  connection_send_query(info, std::move(query), message_id);

  return true;
}

void Session::on_handshake_ready(Result<std::unique_ptr<mtproto::AuthKeyHandshake>> r_handshake) {
  auto handshake_id = narrow_cast<HandshakeId>(get_link_token() - 1);
  bool is_main = handshake_id == MainAuthKeyHandshake;
  auto &info = handshake_info_[handshake_id];
  info.flag_ = false;
  info.actor_.reset();

  if (r_handshake.is_error()) {
    LOG(ERROR) << "Handshake failed: " << r_handshake.move_as_error();
  } else {
    auto handshake = r_handshake.move_as_ok();
    if (!handshake->is_ready_for_finish()) {
      LOG(WARNING) << "Handshake is not yet ready";
      info.handshake_ = std::move(handshake);
    } else {
      if (is_main) {
        auth_data_.set_main_auth_key(std::move(handshake->auth_key));
        on_auth_key_updated();
      } else {
        if (is_main_) {
          registered_temp_auth_key_ = TempAuthKeyWatchdog::register_auth_key_id(handshake->auth_key.id());
        }
        auth_data_.set_tmp_auth_key(std::move(handshake->auth_key));
        on_tmp_auth_key_updated();
      }
      LOG(WARNING) << "Update auth key in session_id " << auth_data_.session_id_ << " to "
                   << auth_data_.get_auth_key().id();
      connection_close(&main_connection_);
      connection_close(&long_poll_connection_);

      // Salt of temporary key is different salt. Do not rewrite it
      if (auth_data_.use_pfs() ^ is_main) {
        auth_data_.set_server_salt(handshake->server_salt, Time::now_cached());
        on_server_salt_updated();
      }
      if (auth_data_.update_server_time_difference(handshake->server_time_diff)) {
        on_server_time_difference_updated();
      }
      LOG(INFO) << "Got " << (is_main ? "main" : "tmp") << " auth key";
    }
  }

  loop();
}

void Session::create_gen_auth_key_actor(HandshakeId handshake_id) {
  auto &info = handshake_info_[handshake_id];
  if (info.flag_) {
    return;
  }
  LOG(INFO) << "Create GenAuthKeyActor " << handshake_id;
  info.flag_ = true;
  bool is_main = handshake_id == MainAuthKeyHandshake;
  if (!info.handshake_) {
    info.handshake_ = std::make_unique<mtproto::AuthKeyHandshake>(dc_id_, is_main && !is_cdn_ ? 0 : 24 * 60 * 60);
  }
  class AuthKeyHandshakeContext : public mtproto::AuthKeyHandshakeContext {
   public:
    AuthKeyHandshakeContext(DhCallback *dh_callback, std::shared_ptr<PublicRsaKeyInterface> public_rsa_key)
        : dh_callback_(dh_callback), public_rsa_key_(std::move(public_rsa_key)) {
    }
    DhCallback *get_dh_callback() override {
      return dh_callback_;
    }
    PublicRsaKeyInterface *get_public_rsa_key_interface() override {
      return public_rsa_key_.get();
    }

   private:
    DhCallback *dh_callback_;
    std::shared_ptr<PublicRsaKeyInterface> public_rsa_key_;
  };
  info.actor_ = create_actor<detail::GenAuthKeyActor>(
      "GenAuthKey", std::move(info.handshake_),
      std::make_unique<AuthKeyHandshakeContext>(DhCache::instance(), shared_auth_data_->public_rsa_key()),
      PromiseCreator::lambda([self = actor_id(this)](Result<std::unique_ptr<mtproto::RawConnection>> r_connection) {
        if (r_connection.is_error()) {
          if (r_connection.error().code() != 1) {
            LOG(WARNING) << "Failed to open connection: " << r_connection.error();
          }
          return;
        }
        send_closure(self, &Session::connection_add, r_connection.move_as_ok());
      }),
      PromiseCreator::lambda(
          [self = actor_shared(this, handshake_id + 1), handshake_perf = PerfWarningTimer("handshake", 1000.1)](
              Result<std::unique_ptr<mtproto::AuthKeyHandshake>> handshake) mutable {
            // later is just to avoid lost hangup
            send_closure_later(std::move(self), &Session::on_handshake_ready, std::move(handshake));
          }),
      callback_);
}

void Session::auth_loop() {
  if (auth_data_.need_main_auth_key()) {
    create_gen_auth_key_actor(MainAuthKeyHandshake);
  }
  if (auth_data_.need_tmp_auth_key(Time::now_cached())) {
    create_gen_auth_key_actor(TmpAuthKeyHandshake);
  }
}

void Session::loop() {
  if (!was_on_network_) {
    return;
  }
  Time::now();  // update now

  if (cached_connection_timestamp_ < Time::now_cached() - 10) {
    cached_connection_.reset();
  }
  if (!is_main_ && !has_queries() && last_activity_timestamp_ < Time::now_cached() - ACTIVITY_TIMEOUT) {
    on_session_failed(Status::OK());
  }

  auth_loop();
  connection_online_update();

  double wakeup_at = 0;
  main_connection_.wakeup_at = 0;
  long_poll_connection_.wakeup_at = 0;

  auth_lost_flag_ = false;
  // NB: order is crucial. First long_poll_connection, then main_connection
  // Otherwise queries could be sent with big delay

  connection_check_mode(&main_connection_);
  connection_check_mode(&long_poll_connection_);
  if (mode_ == Mode::Http) {
    if (long_poll_connection_.state == ConnectionInfo::State::Ready) {
      connection_flush(&long_poll_connection_);
    }
    if (!close_flag_ && long_poll_connection_.state == ConnectionInfo::State::Empty) {
      connection_open(&long_poll_connection_);
    }
    relax_timeout_at(&wakeup_at, long_poll_connection_.wakeup_at);
  }

  if (main_connection_.state == ConnectionInfo::State::Ready) {
    // do not send queries before we have key and e.t.c
    // do not send queries before tmp_key is bound
    bool need_flush = true;
    while (main_connection_.state == ConnectionInfo::State::Ready) {
      if (auth_data_.is_ready(Time::now_cached())) {
        if (need_send_query()) {
          while (!pending_queries_.empty()) {
            auto &query = pending_queries_.front();
            connection_send_query(&main_connection_, std::move(query));
            pending_queries_.pop_front();
          }
          need_flush = true;
        }
        if (need_send_bind_key()) {
          // send auth.bindTempAuthKey
          connection_send_bind_key(&main_connection_);
          need_flush = true;
        }
      }
      if (need_flush) {
        connection_flush(&main_connection_);
        need_flush = false;
      } else {
        break;
      }
    }
  }
  if (!close_flag_ && main_connection_.state == ConnectionInfo::State::Empty) {
    connection_open(&main_connection_, true /*send ask_info*/);
  }

  if (auth_lost_flag_) {
    connection_close(&main_connection_);
    connection_close(&long_poll_connection_);
    auth_lost_flag_ = false;
    relax_timeout_at(&wakeup_at, Time::now_cached() + 0.1);
  }

  relax_timeout_at(&wakeup_at, main_connection_.wakeup_at);

  double wakeup_in = 0;
  if (wakeup_at != 0) {
    wakeup_in = wakeup_at - Time::now_cached();
    LOG(DEBUG) << "Wakeup after " << wakeup_in;
    set_timeout_at(wakeup_at);
  }
  // TODO: write proper condition..
  // LOG_IF(ERROR, !close_flag_ && ((wakeup_at == 0 && network_flag_) || wakeup_in < 0 || wakeup_in > 3000))
  // << "Bad timeout in: " << wakeup_in;
}

}  // namespace td
