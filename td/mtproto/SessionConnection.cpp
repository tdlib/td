//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/mtproto/SessionConnection.h"

#include "td/mtproto/AuthData.h"
#include "td/mtproto/AuthKey.h"
#include "td/mtproto/CryptoStorer.h"
#include "td/mtproto/PacketStorer.h"
#include "td/mtproto/Transport.h"
#include "td/mtproto/utils.h"

#include "td/utils/as.h"
#include "td/utils/common.h"
#include "td/utils/format.h"
#include "td/utils/Gzip.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Random.h"
#include "td/utils/ScopeGuard.h"
#include "td/utils/Time.h"
#include "td/utils/tl_parsers.h"

#include "td/mtproto/mtproto_api.h"
#include "td/mtproto/mtproto_api.hpp"

#include <algorithm>
#include <iterator>
#include <type_traits>

namespace td {
namespace mtproto_api {

const int32 msg_container::ID;

class rpc_result {
 public:
  static const int32 ID = -212046591;
};

}  // namespace mtproto_api

namespace mtproto {
/**
 * TODO-list.
 *
 * 1. Should I check input salt?
 *
 * 1. Cancellation of rpc request
 *  input:
 *   - rpc_drop_answer#58e4a740 req_msg_id:long = RpcDropAnswer;
 *  output:
 *   - rpc_answer_unknown#5e2ad36e = RpcDropAnswer;
 *    no ack
 *   - rpc_answer_dropped_running#cd78e586 = RpcDropAnswer;
 *    same answer will be returned to original query
 *    ack
 *   - rpc_answer_dropped#a43ad8b7 msg_id:long seq_no:int bytes:int = RpcDropAnswer;
 *    ack
 *  Alternative is destroy session
 *
 * 5. Destroy session
 * (?) when session will be destroyed otherwise
 *  Must be call in different session
 *  input:
 *   - destroy_session#e7512126 session_id:long = DestroySessionRes;
 *  output:
 *   - destroy_session_ok#e22045fc session_id:long = DestroySessionRes;
 *   - destroy_session_none#62d350c9 session_id:long = DestroySessionRes;
 *

 * DONE:
 * 3. Ping pong
 *  input:
 *   - ping#7abe77ec ping_id:long = Pong;
 *   - pong#347773c5 msg_id:long ping_id:long = Pong;
 *
 * 4. Ping + deferred connection closure
 *  input:
 *   - ping_delay_disconnect#f3427b8c ping_id:long disconnect_delay:int = Pong;
 *
 * 6. New session creation
 *  A notification about new session.
 *  It is reasonable to store unique_id with current session, in order to process duplicated notifications once.
 *
 *  Causes all older than first_msg_id to be re-sent.
 *  Also there is a gap in updates, so getDifference MUST be sent
 *  output:
 *   - new_session_created#9ec20908 first_msg_id:long unique_id:long server_salt:long = NewSession
 *
 *
 * 7. Containers
 *   I should pack output messages as containers
 *   - msg_container#73f1f8dc messages:vector message = MessageContainer;
 *     message msg_id:long seqno:int bytes:int body:Object = Message;
 *
 * 8. Packed Object
 *  I should pack big output messages with gzip
 *   - gzip_packed#3072cfa1 packed_data:string = Object;
 *
 * 9. Ack
 *  I should actually send acks
 *  (?) Does updates need ack
 *  - msgs_ack#62d6b459 msg_ids:Vector long = MsgsAck;
 *
 * 10. Errors
 *  output:
 *   - bad_msg_notification#a7eff811 bad_msg_id:long bad_msg_seqno:int error_code:int = BadMsgNotification;
 *   - bad_server_salt#edab447b bad_msg_id:long bad_msg_seqno:int error_code:int new_server_salt:long =
 *         BadMsgNotification;
 *
 *  error codes:
 *   16: msg_id is too low. -- lite resend. It will be automatially packed in a container. I hope.
 *   17: msg_id is too high. -- fail connection.
 *   18: msg_id % 4 != 0. -- Error and fail connection.
 *   19: container msg_id is the same as msg_id of a previously received message. MUST NEVER HAPPENS. Error and fail
 *   connection.
 *
 *   20: message is to old -- full resend. (or fail query, if we are afraid of double send)
 *
 *   32: seq_no is too low. (msg_id1 < msg_id2 <==> seq_no1 < seq_no2). Error and fail connection
 *   33: seq_no is too high. Error and fail connection.
 *   34: (?) an even msg_seqno expected (irrelevant message), but odd received. (Fail and call a developer...)
 *   35: (?) odd msg_seqno expected (relevant message), but even received (Fail and call a developer)
 *
 *   48: incorrect server salt (in bad_server_salt message)
 *
 *   64: (?) invalid container
 *
 * 2. Get future salts
 *  input:
 *   - get_future_salts#b921bd04 num:int = FutureSalts;
 *  output:
 *   - future_salts#ae500895 req_msg_id:long now:int salts:vector future_salt = FutureSalts;
 *     future_salt#0949d9dc valid_since:int valid_until:int salt:long = FutureSalt;
 *
 * 1. ping-pong
 * 3. Delayed ack.
 * 4. Delayed packet.
 * HTTP transport (support of several connections)
 * get future salts
 *
 * 11. Explicit request of messages states
 *  input:
 *   - msgs_state_req#da69fb52 msg_ids:Vector long = MsgsStateReq;
 * 12. States of messages in answer
 *  output:
 *   - msgs_state_info#04deb57d req_msg_id:long info:string = MsgsStateInfo;
 *  info contain one byte for each queired msg_id
 *  bytes:
 *    1: unknown message_id
 *    2: message not received (msg_id in stored range)
 *    3: message not receiver (msg_id is too high)
 *    4: message received. No extra ack will be sent
 *    +8: message is already acknowledged
 *    +16: message don't requires acknowledgement
 *    +32: RPC query contained in message being processed or the processing has already been completed
 *    +64: content-related response to message has already been generated
 *    +128: other party knows for a fact that message is already received
 *
 * 13. Voluntary Communication of Status of Messages
 *  output:
 *   - msgs_all_info#8cc0d131 msg_ids:Vector long info:string = MsgsAllInfo
 *
 */

class OnPacket {
  const MsgInfo &info_;
  SessionConnection *connection_;
  Status *status_;

 public:
  OnPacket(const MsgInfo &info, SessionConnection *connection, Status *status)
      : info_(info), connection_(connection), status_(status) {
  }

  template <class T>
  void operator()(const T &func) const {
    *status_ = connection_->on_packet(info_, func);
  }
};

unique_ptr<RawConnection> SessionConnection::move_as_raw_connection() {
  return std::move(raw_connection_);
}

/*** SessionConnection ***/
BufferSlice SessionConnection::as_buffer_slice(Slice packet) {
  return current_buffer_slice_->from_slice(packet);
}

Status SessionConnection::parse_message(TlParser &parser, MsgInfo *info, Slice *packet, bool crypto_flag) {
  // msg_id:long seqno:int bytes:int
  parser.check_len(sizeof(int64) + (crypto_flag ? sizeof(int32) : 0) + sizeof(int32));
  if (parser.get_error() != nullptr) {
    return Status::Error(PSLICE() << "Failed to parse mtproto_api::message: " << parser.get_error());
  }
  info->message_id = parser.fetch_long_unsafe();
  if (crypto_flag) {
    info->seq_no = parser.fetch_int_unsafe();
  }
  uint32 bytes = parser.fetch_int_unsafe();

  if (bytes % sizeof(int32) != 0) {
    return Status::Error(PSLICE() << "Failed to parse mtproto_api::message: size of message [" << bytes
                                  << "] is not divisible by 4");
  }

  *packet = parser.fetch_string_raw<Slice>(bytes);
  if (parser.get_error() != nullptr) {
    return Status::Error(PSLICE() << "Failed to parse mtproto_api::message: " << parser.get_error());
  }

  info->size = bytes;

  return Status::OK();
}

Status SessionConnection::on_packet_container(const MsgInfo &info, Slice packet) {
  auto old_container_id = container_id_;
  container_id_ = info.message_id;
  SCOPE_EXIT {
    container_id_ = old_container_id;
  };

  TlParser parser(packet);
  parser.fetch_int();
  int32 size = parser.fetch_int();
  if (parser.get_error()) {
    return Status::Error(PSLICE() << "Failed to parse mtproto_api::rpc_container: " << parser.get_error());
  }
  for (int i = 0; i < size; i++) {
    TRY_STATUS(parse_packet(parser));
  }
  return Status::OK();
}

Status SessionConnection::on_packet_rpc_result(const MsgInfo &info, Slice packet) {
  TlParser parser(packet);
  parser.fetch_int();
  uint64 req_msg_id = parser.fetch_long();
  if (parser.get_error()) {
    return Status::Error(PSLICE() << "Failed to parse mtproto_api::rpc_result: " << parser.get_error());
  }

  auto object_begin_pos = packet.size() - parser.get_left_len();
  int32 id = parser.fetch_int();
  if (id == mtproto_api::rpc_error::ID) {
    mtproto_api::rpc_error rpc_error(parser);
    if (parser.get_error()) {
      return Status::Error(PSLICE() << "Failed to parse mtproto_api::rpc_error: " << parser.get_error());
    }
    return on_packet(info, req_msg_id, rpc_error);
  } else if (id == mtproto_api::gzip_packed::ID) {
    mtproto_api::gzip_packed gzip(parser);
    if (parser.get_error()) {
      return Status::Error(PSLICE() << "Failed to parse mtproto_api::gzip_packed: " << parser.get_error());
    }
    // yep, gzip in rpc_result
    BufferSlice object = gzdecode(gzip.packed_data_);
    // send header no more optimization
    return callback_->on_message_result_ok(req_msg_id, std::move(object), info.size);
  }

  return callback_->on_message_result_ok(req_msg_id, as_buffer_slice(packet.substr(object_begin_pos)), info.size);
}

template <class T>
Status SessionConnection::on_packet(const MsgInfo &info, const T &packet) {
  LOG(ERROR) << "Unsupported: " << to_string(packet);
  return Status::OK();
}
Status SessionConnection::on_packet(const MsgInfo &info, const mtproto_api::destroy_auth_key_ok &destroy_auth_key) {
  return on_destroy_auth_key(destroy_auth_key);
}
Status SessionConnection::on_packet(const MsgInfo &info, const mtproto_api::destroy_auth_key_none &destroy_auth_key) {
  return on_destroy_auth_key(destroy_auth_key);
}
Status SessionConnection::on_packet(const MsgInfo &info, const mtproto_api::destroy_auth_key_fail &destroy_auth_key) {
  return on_destroy_auth_key(destroy_auth_key);
}

Status SessionConnection::on_destroy_auth_key(const mtproto_api::DestroyAuthKeyRes &destroy_auth_key) {
  LOG_CHECK(need_destroy_auth_key_) << static_cast<int32>(mode_);
  LOG(INFO) << to_string(destroy_auth_key);
  return callback_->on_destroy_auth_key();
}

Status SessionConnection::on_packet(const MsgInfo &info, const mtproto_api::rpc_error &rpc_error) {
  return on_packet(info, 0, rpc_error);
}

Status SessionConnection::on_packet(const MsgInfo &info, uint64 req_msg_id, const mtproto_api::rpc_error &rpc_error) {
  VLOG(mtproto) << "ERROR " << tag("code", rpc_error.error_code_) << tag("message", rpc_error.error_message_)
                << tag("req_msg_id", req_msg_id);
  if (req_msg_id != 0) {
    callback_->on_message_result_error(req_msg_id, rpc_error.error_code_, as_buffer_slice(rpc_error.error_message_));
  } else {
    LOG(WARNING) << "Receive rpc_error as update: [" << rpc_error.error_code_ << "][" << rpc_error.error_message_
                 << "]";
  }
  return Status::OK();
}

Status SessionConnection::on_packet(const MsgInfo &info, const mtproto_api::new_session_created &new_session_created) {
  VLOG(mtproto) << "NEW_SESSION_CREATED: [first_msg_id:" << format::as_hex(new_session_created.first_msg_id_)
                << "] [unique_id:" << format::as_hex(new_session_created.unique_id_)
                << "] [server_salt:" << format::as_hex(new_session_created.server_salt_) << "]";
  callback_->on_session_created(new_session_created.unique_id_, new_session_created.first_msg_id_);
  return Status::OK();
}

Status SessionConnection::on_packet(const MsgInfo &info,
                                    const mtproto_api::bad_msg_notification &bad_msg_notification) {
  MsgInfo bad_info{info.session_id, bad_msg_notification.bad_msg_id_, bad_msg_notification.bad_msg_seqno_, 0};
  enum Code {
    MsgIdTooLow = 16,
    MsgIdTooHigh = 17,
    MsgIdMod4 = 18,
    MsgIdCollision = 19,

    MsgIdTooOld = 20,

    SeqNoTooLow = 32,
    SeqNoTooHigh = 33,
    SeqNoNotEven = 34,
    SeqNoNotOdd = 35,

    InvalidContainer = 64
  };
  Slice common = ". BUG! CALL FOR A DEVELOPER! Session will be closed";
  switch (bad_msg_notification.error_code_) {
    case MsgIdTooLow: {
      LOG(WARNING) << bad_info << ": MessageId is too low. Message will be re-sent";
      // time will be updated automagically
      on_message_failed(bad_info.message_id, Status::Error("MessageId is too low"));
      break;
    }
    case MsgIdTooHigh: {
      LOG(WARNING) << bad_info << ": MessageId is too high. Session will be closed";
      // All this queries will be re-sent by parent
      to_send_.clear();
      callback_->on_session_failed(Status::Error("MessageId is too high"));
      return Status::Error("MessageId is too high");
    }
    case MsgIdMod4: {
      LOG(ERROR) << bad_info << ": MessageId is not divisible by 4" << common;
      return Status::Error("MessageId is not divisible by 4");
    }
    case MsgIdCollision: {
      LOG(ERROR) << bad_info << ": Container and older message MessageId collision" << common;
      return Status::Error("Container and older message MessageId collision");
    }

    case MsgIdTooOld: {
      LOG(WARNING) << bad_info << ": MessageId is too old. Message will be re-sent";
      on_message_failed(bad_info.message_id, Status::Error("MessageId is too old"));
      break;
    }

    case SeqNoTooLow: {
      LOG(ERROR) << bad_info << ": SeqNo is too low" << common;
      return Status::Error("SeqNo is too low");
    }
    case SeqNoTooHigh: {
      LOG(ERROR) << bad_info << ": SeqNo is too high" << common;
      return Status::Error("SeqNo is too high");
    }
    case SeqNoNotEven: {
      LOG(ERROR) << bad_info << ": SeqNo is not even for an irrelevant message" << common;
      return Status::Error("SeqNo is not even for an irrelevant message");
    }
    case SeqNoNotOdd: {
      LOG(ERROR) << bad_info << ": SeqNo is not odd for an irrelevant message" << common;
      return Status::Error("SeqNo is not odd for an irrelevant message");
    }

    case InvalidContainer: {
      LOG(ERROR) << bad_info << ": Invalid Contailer" << common;
      return Status::Error("Invalid Contailer");
    }

    default: {
      LOG(ERROR) << bad_info << ": Unknown error [code:" << bad_msg_notification.error_code_ << "]" << common;
      return Status::Error("Unknown error code");
    }
  }
  return Status::OK();
}

Status SessionConnection::on_packet(const MsgInfo &info, const mtproto_api::bad_server_salt &bad_server_salt) {
  MsgInfo bad_info{info.session_id, bad_server_salt.bad_msg_id_, bad_server_salt.bad_msg_seqno_, 0};
  VLOG(mtproto) << "BAD_SERVER_SALT: " << bad_info;
  auth_data_->set_server_salt(bad_server_salt.new_server_salt_, Time::now_cached());
  callback_->on_server_salt_updated();

  on_message_failed(bad_info.message_id, Status::Error("Bad server salt"));
  return Status::OK();
}

Status SessionConnection::on_packet(const MsgInfo &info, const mtproto_api::msgs_ack &msgs_ack) {
  for (auto id : msgs_ack.msg_ids_) {
    callback_->on_message_ack(id);
  }
  return Status::OK();
}

Status SessionConnection::on_packet(const MsgInfo &info, const mtproto_api::gzip_packed &gzip_packed) {
  BufferSlice res = gzdecode(gzip_packed.packed_data_);
  auto guard = set_buffer_slice(&res);
  return on_slice_packet(info, res.as_slice());
}

Status SessionConnection::on_packet(const MsgInfo &info, const mtproto_api::pong &pong) {
  VLOG(mtproto) << "PONG";
  last_pong_at_ = Time::now_cached();
  return callback_->on_pong();
}
Status SessionConnection::on_packet(const MsgInfo &info, const mtproto_api::future_salts &salts) {
  VLOG(mtproto) << "FUTURE_SALTS";
  std::vector<ServerSalt> new_salts;
  for (auto &it : salts.salts_) {
    new_salts.push_back(
        ServerSalt{it->salt_, static_cast<double>(it->valid_since_), static_cast<double>(it->valid_until_)});
  }
  auth_data_->set_future_salts(new_salts, Time::now_cached());
  callback_->on_server_salt_updated();

  return Status::OK();
}

Status SessionConnection::on_msgs_state_info(const std::vector<int64> &ids, Slice info) {
  if (ids.size() != info.size()) {
    return Status::Error(PSLICE() << tag("ids.size()", ids.size()) << " != " << tag("info.size()", info.size()));
  }
  size_t i = 0;
  for (auto id : ids) {
    callback_->on_message_info(id, info[i], 0, 0);
    i++;
  }
  return Status::OK();
}

Status SessionConnection::on_packet(const MsgInfo &info, const mtproto_api::msgs_state_info &msgs_state_info) {
  auto it = service_queries_.find(msgs_state_info.req_msg_id_);
  if (it == service_queries_.end()) {
    return Status::Error("Unknown msgs_state_info");
  }
  SCOPE_EXIT {
    service_queries_.erase(it);
  };
  if (it->second.type != ServiceQuery::GetStateInfo) {
    return Status::Error("Got msg_state_info in response not to GetStateInfo");
  }
  return on_msgs_state_info(it->second.message_ids, msgs_state_info.info_);
}

Status SessionConnection::on_packet(const MsgInfo &info, const mtproto_api::msgs_all_info &msgs_all_info) {
  return on_msgs_state_info(msgs_all_info.msg_ids_, msgs_all_info.info_);
}

Status SessionConnection::on_packet(const MsgInfo &info, const mtproto_api::msg_detailed_info &msg_detailed_info) {
  callback_->on_message_info(msg_detailed_info.msg_id_, msg_detailed_info.status_, msg_detailed_info.answer_msg_id_,
                             msg_detailed_info.bytes_);
  return Status::OK();
}

Status SessionConnection::on_packet(const MsgInfo &info,
                                    const mtproto_api::msg_new_detailed_info &msg_new_detailed_info) {
  callback_->on_message_info(0, 0, msg_new_detailed_info.answer_msg_id_, msg_new_detailed_info.bytes_);
  return Status::OK();
}

Status SessionConnection::on_slice_packet(const MsgInfo &info, Slice packet) {
  if (info.seq_no & 1) {
    send_ack(info.message_id);
  }
  TlParser parser(packet);
  tl_object_ptr<mtproto_api::Object> object = mtproto_api::Object::fetch(parser);
  parser.fetch_end();
  if (parser.get_error()) {
    // msg_container is not real tl object
    if (packet.size() >= 4 && as<int32>(packet.begin()) == mtproto_api::msg_container::ID) {
      return on_packet_container(info, packet);
    }
    if (packet.size() >= 4 && as<int32>(packet.begin()) == mtproto_api::rpc_result::ID) {
      return on_packet_rpc_result(info, packet);
    }

    // It is an update... I hope.
    auto status = auth_data_->check_update(info.message_id);
    if (status.is_error()) {
      if (status.code() == 2) {
        LOG(WARNING) << "Receive too old update: " << status;
        callback_->on_session_failed(Status::Error("Receive too old update"));
        return status;
      }
      VLOG(mtproto) << "Skip update " << info.message_id << " from " << get_name() << " created in "
                    << (Time::now() - created_at_) << ": " << status;
      return Status::OK();
    } else {
      VLOG(mtproto) << "Got update from " << get_name() << " created in " << (Time::now() - created_at_)
                    << " in container " << container_id_ << " from session " << auth_data_->get_session_id()
                    << " with message_id " << info.message_id << ", main_message_id = " << main_message_id_
                    << ", seq_no = " << info.seq_no << " and original size " << info.size;
      return callback_->on_message_result_ok(0, as_buffer_slice(packet), info.size);
    }
  }

  Status status;
  downcast_call(*object, OnPacket(info, this, &status));
  return status;
}

Status SessionConnection::parse_packet(TlParser &parser) {
  MsgInfo info;
  Slice packet;
  TRY_STATUS(parse_message(parser, &info, &packet, true));
  return on_slice_packet(info, packet);
}

Status SessionConnection::on_main_packet(const PacketInfo &info, Slice packet) {
  // Update pong here too. Real pong can be delayed by lots of big packets
  last_pong_at_ = Time::now_cached();

  if (!connected_flag_) {
    connected_flag_ = true;
    callback_->on_connected();
  }

  VLOG(raw_mtproto) << "Got packet of size " << packet.size() << " from session " << format::as_hex(info.session_id)
                    << ":" << format::as_hex_dump<4>(packet);
  if (info.no_crypto_flag) {
    return Status::Error("Unencrypted packet");
  }

  TlParser parser(packet);
  TRY_STATUS(parse_packet(parser));
  parser.fetch_end();
  if (parser.get_error()) {
    return Status::Error(PSLICE() << "Failed to parse packet: " << parser.get_error());
  }
  return Status::OK();
}

void SessionConnection::on_message_failed(uint64 id, Status status) {
  callback_->on_message_failed(id, std::move(status));

  sent_destroy_auth_key_ = false;

  if (id == last_ping_message_id_ || id == last_ping_container_id_) {
    // restart ping immediately
    last_ping_at_ = 0;
    last_ping_message_id_ = 0;
    last_ping_container_id_ = 0;
  }

  auto cit = container_to_service_msg_.find(id);
  if (cit != container_to_service_msg_.end()) {
    for (auto nid : cit->second) {
      on_message_failed_inner(nid);
    }
  } else {
    on_message_failed_inner(id);
  }
}
void SessionConnection::on_message_failed_inner(uint64 id) {
  auto it = service_queries_.find(id);
  if (it == service_queries_.end()) {
    return;
  }
  switch (it->second.type) {
    case ServiceQuery::ResendAnswer: {
      for (auto message_id : it->second.message_ids) {
        resend_answer(message_id);
      }
      break;
    }
    case ServiceQuery::GetStateInfo: {
      for (auto message_id : it->second.message_ids) {
        get_state_info(message_id);
      }
      break;
    }
  }
  service_queries_.erase(id);
}

bool SessionConnection::must_flush_packet() {
  flush_packet_at_ = 0;

  // we need key to send just something
  if (!auth_data_->has_auth_key(Time::now_cached())) {
    return false;
  }

  // transport is ready
  if (!raw_connection_->can_send()) {
    return false;
  }

  bool has_salt = auth_data_->has_salt(Time::now_cached());
  // do not send anything in long poll connection before we have salt
  if (mode_ == Mode::HttpLongPoll && !has_salt) {
    return false;
  }

  // http_wait
  if (mode_ == Mode::HttpLongPoll) {
    return true;
  }
  // queries and acks (+ resend & get_info)
  if (has_salt && force_send_at_ != 0) {
    if (Time::now_cached() > force_send_at_) {
      return true;
    } else {
      relax_timeout_at(&flush_packet_at_, force_send_at_);
    }
  }

  // ping
  if (has_salt) {
    if (must_ping()) {
      return true;
    }
    relax_timeout_at(&flush_packet_at_, last_ping_at_ + ping_must_delay());
  }
  // get_future_salt
  if (!has_salt) {
    if (last_get_future_salt_at_ == 0) {
      return true;
    }
    auto get_future_salts_at = last_get_future_salt_at_ + 60;
    if (get_future_salts_at < Time::now_cached()) {
      return true;
    }
    relax_timeout_at(&flush_packet_at_, get_future_salts_at);
  }

  if (has_salt && need_destroy_auth_key_ && !sent_destroy_auth_key_) {
    return true;
  }

  return false;
}

Status SessionConnection::before_write() {
  while (must_flush_packet()) {
    flush_packet();
  }
  return Status::OK();
}

Status SessionConnection::on_raw_packet(const PacketInfo &info, BufferSlice packet) {
  auto old_main_message_id = main_message_id_;
  main_message_id_ = info.message_id;
  SCOPE_EXIT {
    main_message_id_ = old_main_message_id;
  };

  if (info.no_crypto_flag) {
    return Status::Error("Unexpected unencrypted packet");
  }

  bool time_difference_was_updated = false;
  auto status =
      auth_data_->check_packet(info.session_id, info.message_id, Time::now_cached(), time_difference_was_updated);
  if (time_difference_was_updated) {
    callback_->on_server_time_difference_updated();
  }
  if (status.is_error()) {
    if (status.code() == 1) {
      LOG(WARNING) << "Packet ignored " << status;
      send_ack(info.message_id);
      return Status::OK();
    } else if (status.code() == 2) {
      LOG(WARNING) << "Receive too old packet: " << status;
      callback_->on_session_failed(Status::Error("Receive too old packet"));
      return status;
    } else {
      return status;
    }
  }

  auto guard = set_buffer_slice(&packet);
  TRY_STATUS(on_main_packet(info, packet.as_slice()));
  return Status::OK();
}

Status SessionConnection::on_quick_ack(uint64 quick_ack_token) {
  callback_->on_message_ack(quick_ack_token);
  return Status::OK();
}

void SessionConnection::on_read(size_t size) {
  last_read_at_ = Time::now_cached();
}

SessionConnection::SessionConnection(Mode mode, unique_ptr<RawConnection> raw_connection, AuthData *auth_data)
    : raw_connection_(std::move(raw_connection)), auth_data_(auth_data) {
  state_ = Init;
  mode_ = mode;
  created_at_ = Time::now();
}

PollableFdInfo &SessionConnection::get_poll_info() {
  return raw_connection_->get_poll_info();
}

Status SessionConnection::init() {
  CHECK(state_ == Init);
  last_pong_at_ = Time::now_cached();
  last_read_at_ = Time::now_cached();
  state_ = Run;
  return Status::OK();
}

void SessionConnection::set_online(bool online_flag, bool is_main) {
  bool need_ping = online_flag || !online_flag_;
  online_flag_ = online_flag;
  is_main_ = is_main;
  auto now = Time::now();
  if (need_ping) {
    last_pong_at_ = now - ping_disconnect_delay() + rtt();
    last_read_at_ = now - read_disconnect_delay() + rtt();
  } else {
    last_pong_at_ = now;
    last_read_at_ = now;
  }
  last_ping_at_ = 0;
  last_ping_message_id_ = 0;
  last_ping_container_id_ = 0;
}

void SessionConnection::do_close(Status status) {
  state_ = Closed;
  // NB: this could be destroyed after on_closed
  callback_->on_closed(std::move(status));
}

void SessionConnection::send_crypto(const Storer &storer, uint64 quick_ack_token) {
  CHECK(state_ != Closed);
  raw_connection_->send_crypto(storer, auth_data_->get_session_id(), auth_data_->get_server_salt(Time::now_cached()),
                               auth_data_->get_auth_key(), quick_ack_token);
}

Result<uint64> SessionConnection::send_query(BufferSlice buffer, bool gzip_flag, int64 message_id,
                                             uint64 invoke_after_id, bool use_quick_ack) {
  CHECK(mode_ != Mode::HttpLongPoll);  // "LongPoll connection is only for http_wait"
  if (message_id == 0) {
    message_id = auth_data_->next_message_id(Time::now_cached());
  }
  auto seq_no = auth_data_->next_seq_no(true);
  if (to_send_.empty()) {
    send_before(Time::now_cached() + QUERY_DELAY);
  }
  to_send_.push_back(MtprotoQuery{message_id, seq_no, std::move(buffer), gzip_flag, invoke_after_id, use_quick_ack});
  VLOG(mtproto) << "Invoke query " << message_id << " of size " << to_send_.back().packet.size() << " with seq_no "
                << seq_no << " after " << invoke_after_id << (use_quick_ack ? " with quick ack" : "");

  return message_id;
}

void SessionConnection::get_state_info(int64 message_id) {
  if (to_get_state_info_.empty()) {
    send_before(Time::now_cached());
  }
  to_get_state_info_.push_back(message_id);
}

void SessionConnection::resend_answer(int64 message_id) {
  if (to_resend_answer_.empty()) {
    send_before(Time::now_cached() + RESEND_ANSWER_DELAY);
  }
  to_resend_answer_.push_back(message_id);
}
void SessionConnection::cancel_answer(int64 message_id) {
  if (to_cancel_answer_.empty()) {
    send_before(Time::now_cached() + RESEND_ANSWER_DELAY);
  }
  to_cancel_answer_.push_back(message_id);
}

void SessionConnection::destroy_key() {
  LOG(INFO) << "Set need_destroy_auth_key to true";
  need_destroy_auth_key_ = true;
}

std::pair<uint64, BufferSlice> SessionConnection::encrypted_bind(int64 perm_key, int64 nonce, int32 expires_at) {
  int64 temp_key = auth_data_->get_tmp_auth_key().id();

  mtproto_api::bind_auth_key_inner object(nonce, temp_key, perm_key, auth_data_->get_session_id(), expires_at);
  auto object_storer = create_storer(object);
  auto size = object_storer.size();
  auto object_packet = BufferWriter{size, 0, 0};
  auto real_size = object_storer.store(object_packet.as_slice().ubegin());
  CHECK(size == real_size);

  MtprotoQuery query{
      auth_data_->next_message_id(Time::now_cached()), 0, object_packet.as_buffer_slice(), false, 0, false};
  PacketStorer<QueryImpl> query_storer(query, Slice());

  PacketInfo info;
  info.version = 1;
  info.no_crypto_flag = false;
  info.salt = Random::secure_int64();
  info.session_id = Random::secure_int64();

  const AuthKey &main_auth_key = auth_data_->get_main_auth_key();
  auto packet = BufferWriter{Transport::write(query_storer, main_auth_key, &info), 0, 0};
  Transport::write(query_storer, main_auth_key, &info, packet.as_slice());
  return std::make_pair(query.message_id, packet.as_buffer_slice());
}

void SessionConnection::send_ack(uint64 message_id) {
  VLOG(mtproto) << "Send ack: [msg_id:" << format::as_hex(message_id) << "]";
  if (to_ack_.empty()) {
    send_before(Time::now_cached() + ACK_DELAY);
  }
  auto ack = static_cast<int64>(message_id);
  // an easiest way to eliminate duplicated acks for gzipped packets
  if (to_ack_.empty() || to_ack_.back() != ack) {
    to_ack_.push_back(ack);
  }
}

// don't send ping in poll mode.
bool SessionConnection::may_ping() const {
  return last_ping_at_ == 0 || (mode_ != Mode::HttpLongPoll && last_ping_at_ + ping_may_delay() < Time::now_cached());
}

bool SessionConnection::must_ping() const {
  return last_ping_at_ == 0 || (mode_ != Mode::HttpLongPoll && last_ping_at_ + ping_must_delay() < Time::now_cached());
}

void SessionConnection::flush_packet() {
  bool has_salt = auth_data_->has_salt(Time::now_cached());
  // ping
  uint64 container_id = 0;
  int64 ping_id = 0;
  if (has_salt && may_ping()) {
    ping_id = ++cur_ping_id_;
    last_ping_at_ = Time::now_cached();
  }

  // http_wait
  int max_delay = -1;
  int max_after = -1;
  int max_wait = -1;
  if (mode_ == Mode::HttpLongPoll) {
    max_delay = HTTP_MAX_DELAY;
    max_after = HTTP_MAX_AFTER;
    auto time_to_disconnect =
        min(ping_disconnect_delay() + last_pong_at_, read_disconnect_delay() + last_read_at_) - Time::now_cached();
    max_wait = min(http_max_wait(), static_cast<int>(1000 * max(0.1, time_to_disconnect - rtt())));
  } else if (mode_ == Mode::Http) {
    max_delay = HTTP_MAX_DELAY;
    max_after = HTTP_MAX_AFTER;
    max_wait = 0;
  }

  // future salts
  int future_salt_n = 0;
  if (mode_ != Mode::HttpLongPoll) {
    if (auth_data_->need_future_salts(Time::now_cached()) &&
        (last_get_future_salt_at_ == 0 || last_get_future_salt_at_ + 60 < Time::now_cached())) {
      last_get_future_salt_at_ = Time::now_cached();
      future_salt_n = 64;
    }
  }

  size_t send_till = 0, send_size = 0;
  // send at most 1020 queries, of total size 2^15
  // don't send anything if have no salt
  if (has_salt) {
    while (send_till < to_send_.size() && send_till < 1020 && send_size < (1 << 15)) {
      send_size += to_send_[send_till].packet.size();
      send_till++;
    }
  }
  std::vector<MtprotoQuery> queries;
  if (send_till == to_send_.size()) {
    queries = std::move(to_send_);
  } else if (send_till != 0) {
    queries.reserve(send_till);
    std::move(to_send_.begin(), to_send_.begin() + send_till, std::back_inserter(queries));
    to_send_.erase(to_send_.begin(), to_send_.begin() + send_till);
  }

  bool destroy_auth_key = need_destroy_auth_key_ && !sent_destroy_auth_key_;

  if (queries.empty() && to_ack_.empty() && ping_id == 0 && max_delay < 0 && future_salt_n == 0 &&
      to_resend_answer_.empty() && to_cancel_answer_.empty() && to_get_state_info_.empty() && !destroy_auth_key) {
    force_send_at_ = 0;
    return;
  }

  sent_destroy_auth_key_ |= destroy_auth_key;

  VLOG(mtproto) << "Sent packet: " << tag("query_count", queries.size()) << tag("ack_cnt", to_ack_.size())
                << tag("ping", ping_id != 0) << tag("http_wait", max_delay >= 0)
                << tag("future_salt", future_salt_n > 0) << tag("get_info", to_get_state_info_.size())
                << tag("resend", to_resend_answer_.size()) << tag("cancel", to_cancel_answer_.size())
                << tag("destroy_key", destroy_auth_key) << tag("auth_id", auth_data_->get_auth_key().id());

  auto cut_tail = [](auto &v, size_t size, Slice name) {
    if (size >= v.size()) {
      return std::move(v);
    }
    LOG(WARNING) << "Too much ids in container: " << v.size() << " " << name;
    std::decay_t<decltype(v)> res(std::make_move_iterator(v.end() - size), std::make_move_iterator(v.end()));
    v.resize(v.size() - size);
    return res;
  };

  // no more than 8192 ids per container..
  auto to_resend_answer = cut_tail(to_resend_answer_, 8192, "resend_answer");
  uint64 resend_answer_id = 0;
  CHECK(queries.size() <= 1020);
  auto to_cancel_answer = cut_tail(to_cancel_answer_, 1020 - queries.size(), "cancel_answer");
  auto to_get_state_info = cut_tail(to_get_state_info_, 8192, "get_state_info");
  uint64 get_state_info_id = 0;
  auto to_ack = cut_tail(to_ack_, 8192, "ack");
  uint64 ping_message_id = 0;

  bool use_quick_ack =
      std::any_of(queries.begin(), queries.end(), [](const auto &query) { return query.use_quick_ack; });

  {
    uint64 parent_message_id = 0;
    auto storer = PacketStorer<CryptoImpl>(
        queries, auth_data_->get_header(), std::move(to_ack), ping_id, ping_disconnect_delay() + 2, max_delay,
        max_after, max_wait, future_salt_n, to_get_state_info, to_resend_answer, to_cancel_answer, destroy_auth_key,
        auth_data_, &container_id, &get_state_info_id, &resend_answer_id, &ping_message_id, &parent_message_id);

    auto quick_ack_token = use_quick_ack ? parent_message_id : 0;
    send_crypto(storer, quick_ack_token);
  }

  if (resend_answer_id) {
    service_queries_.insert({resend_answer_id, ServiceQuery{ServiceQuery::ResendAnswer, std::move(to_resend_answer)}});
  }
  if (get_state_info_id) {
    service_queries_.insert(
        {get_state_info_id, ServiceQuery{ServiceQuery::GetStateInfo, std::move(to_get_state_info)}});
  }
  if (ping_id != 0) {
    last_ping_container_id_ = container_id;
    last_ping_message_id_ = ping_message_id;
  }

  if (container_id != 0) {
    vector<uint64> ids = transform(queries, [](const MtprotoQuery &x) { return static_cast<uint64>(x.message_id); });

    // some acks may be lost here. Nobody will resend them if something goes wrong with query.
    // It is mostly problem for server. We will just drop this answers in next connection
    //
    // get future salt too.
    // So I will re-ask salt if have no answer in 60 second.
    callback_->on_container_sent(container_id, std::move(ids));

    if (resend_answer_id) {
      container_to_service_msg_[container_id].push_back(resend_answer_id);
    }
    if (get_state_info_id) {
      container_to_service_msg_[container_id].push_back(get_state_info_id);
    }
  }

  to_ack_.clear();
  if (to_send_.empty()) {
    force_send_at_ = 0;
  }
}

void SessionConnection::send_before(double tm) {
  if (force_send_at_ == 0 || force_send_at_ > tm) {
    force_send_at_ = tm;
  }
}

Status SessionConnection::do_flush() {
  CHECK(raw_connection_);
  CHECK(state_ != Closed);
  if (state_ == Init) {
    TRY_STATUS(init());
  }
  if (!auth_data_->has_auth_key(Time::now_cached())) {
    return Status::Error("No auth key");
  }

  TRY_STATUS(raw_connection_->flush(auth_data_->get_auth_key(), *this));

  if (last_pong_at_ + ping_disconnect_delay() < Time::now_cached()) {
    auto stats_callback = raw_connection_->stats_callback();
    if (stats_callback != nullptr) {
      stats_callback->on_error();
    }
    return Status::Error(PSLICE() << "Ping timeout of " << ping_disconnect_delay() << " seconds expired");
  }

  if (last_read_at_ + read_disconnect_delay() < Time::now_cached()) {
    auto stats_callback = raw_connection_->stats_callback();
    if (stats_callback != nullptr) {
      stats_callback->on_error();
    }
    return Status::Error(PSLICE() << "Read timeout of " << read_disconnect_delay() << " seconds expired");
  }

  return Status::OK();
}

double SessionConnection::flush(SessionConnection::Callback *callback) {
  callback_ = callback;
  wakeup_at_ = 0;
  auto status = do_flush();
  // check error
  if (status.is_error()) {
    do_close(std::move(status));
    return 0;
  }

  // wakeup_at
  // two independent timeouts
  // 1. close connection after PING_DISCONNECT_DELAY after last_pong.
  // 2. the one returned by must_flush_packet
  relax_timeout_at(&wakeup_at_, last_pong_at_ + ping_disconnect_delay() + 0.002);
  relax_timeout_at(&wakeup_at_, last_read_at_ + read_disconnect_delay() + 0.002);
  // CHECK(wakeup_at > Time::now_cached());

  relax_timeout_at(&wakeup_at_, flush_packet_at_);
  return wakeup_at_;
}

void SessionConnection::force_close(SessionConnection::Callback *callback) {
  CHECK(state_ != Closed);
  callback_ = callback;
  do_close(Status::OK());
}

}  // namespace mtproto
}  // namespace td
